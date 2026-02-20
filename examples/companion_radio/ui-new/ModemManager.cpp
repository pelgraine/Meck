#ifdef HAS_4G_MODEM

#include "ModemManager.h"
#include <Mesh.h>   // For MESH_DEBUG_PRINTLN
#include <SD.h>     // For modem config persistence
#include <time.h>
#include <sys/time.h>

// Global singleton
ModemManager modemManager;

// Use Serial1 for modem UART
#define MODEM_SERIAL Serial1
#define MODEM_BAUD   115200

// AT response buffer
#define AT_BUF_SIZE  512
static char _atBuf[AT_BUF_SIZE];

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ModemManager::begin() {
  MESH_DEBUG_PRINTLN("[Modem] begin()");

  _state = ModemState::OFF;
  _csq = 99;
  _operator[0] = '\0';

  // Create FreeRTOS primitives
  _sendQueue = xQueueCreate(MODEM_SEND_QUEUE_SIZE, sizeof(SMSOutgoing));
  _recvQueue = xQueueCreate(MODEM_RECV_QUEUE_SIZE, sizeof(SMSIncoming));
  _uartMutex = xSemaphoreCreateMutex();

  // Launch background task on Core 0
  xTaskCreatePinnedToCore(
    taskEntry,
    "modem",
    MODEM_TASK_STACK_SIZE,
    this,
    MODEM_TASK_PRIORITY,
    &_taskHandle,
    MODEM_TASK_CORE
  );
}

void ModemManager::shutdown() {
  if (!_taskHandle) return;

  MESH_DEBUG_PRINTLN("[Modem] shutdown()");

  // Tell modem to power off gracefully
  if (xSemaphoreTake(_uartMutex, pdMS_TO_TICKS(2000))) {
    sendAT("AT+CPOF", "OK", 5000);
    xSemaphoreGive(_uartMutex);
  }

  // Cut modem power
  digitalWrite(MODEM_POWER_EN, LOW);

  // Delete task
  vTaskDelete(_taskHandle);
  _taskHandle = nullptr;
  _state = ModemState::OFF;
}

bool ModemManager::sendSMS(const char* phone, const char* body) {
  if (!_sendQueue) return false;

  SMSOutgoing msg;
  memset(&msg, 0, sizeof(msg));
  strncpy(msg.phone, phone, SMS_PHONE_LEN - 1);
  strncpy(msg.body, body, SMS_BODY_LEN - 1);

  return xQueueSend(_sendQueue, &msg, 0) == pdTRUE;
}

bool ModemManager::recvSMS(SMSIncoming& out) {
  if (!_recvQueue) return false;
  return xQueueReceive(_recvQueue, &out, 0) == pdTRUE;
}

int ModemManager::getSignalBars() const {
  if (_csq == 99 || _csq == 0) return 0;
  if (_csq <= 5)  return 1;
  if (_csq <= 10) return 2;
  if (_csq <= 15) return 3;
  if (_csq <= 20) return 4;
  return 5;
}

const char* ModemManager::stateToString(ModemState s) {
  switch (s) {
    case ModemState::OFF:          return "OFF";
    case ModemState::POWERING_ON:  return "PWR ON";
    case ModemState::INITIALIZING: return "INIT";
    case ModemState::REGISTERING:  return "REG";
    case ModemState::READY:        return "READY";
    case ModemState::ERROR:        return "ERROR";
    case ModemState::SENDING_SMS:  return "SENDING";
    default:                       return "???";
  }
}

// ---------------------------------------------------------------------------
// Persistent modem enable/disable config
// ---------------------------------------------------------------------------

#define MODEM_CONFIG_FILE "/sms/modem.cfg"

bool ModemManager::loadEnabledConfig() {
  File f = SD.open(MODEM_CONFIG_FILE, FILE_READ);
  if (!f) {
    // No config file = enabled by default
    return true;
  }
  char c = '1';
  if (f.available()) c = f.read();
  f.close();
  return (c != '0');
}

void ModemManager::saveEnabledConfig(bool enabled) {
  // Ensure /sms directory exists
  if (!SD.exists("/sms")) SD.mkdir("/sms");
  File f = SD.open(MODEM_CONFIG_FILE, FILE_WRITE);
  if (f) {
    f.print(enabled ? '1' : '0');
    f.close();
    Serial.printf("[Modem] Config saved: %s\n", enabled ? "ENABLED" : "DISABLED");
  }
}

// ---------------------------------------------------------------------------
// FreeRTOS Task
// ---------------------------------------------------------------------------

void ModemManager::taskEntry(void* param) {
  static_cast<ModemManager*>(param)->taskLoop();
}

void ModemManager::taskLoop() {
  MESH_DEBUG_PRINTLN("[Modem] task started on core %d", xPortGetCoreID());

restart:
  // ---- Phase 1: Power on ----
  _state = ModemState::POWERING_ON;
  if (!modemPowerOn()) {
    MESH_DEBUG_PRINTLN("[Modem] power-on failed, retry in 30s");
    _state = ModemState::ERROR;
    vTaskDelay(pdMS_TO_TICKS(30000));
    goto restart;
  }

  // ---- Phase 2: Initialize ----
  _state = ModemState::INITIALIZING;
  MESH_DEBUG_PRINTLN("[Modem] initializing...");

  // Basic AT check
  {
    bool atOk = false;
    for (int i = 0; i < 10; i++) {
      MESH_DEBUG_PRINTLN("[Modem] init AT check %d/10", i + 1);
      if (sendAT("AT", "OK", 1000)) { atOk = true; break; }
      vTaskDelay(pdMS_TO_TICKS(500));
    }
    if (!atOk) {
      MESH_DEBUG_PRINTLN("[Modem] AT check failed — retry from power-on in 30s");
      _state = ModemState::ERROR;
      vTaskDelay(pdMS_TO_TICKS(30000));
      goto restart;
    }
  }

  // Disable echo
  sendAT("ATE0", "OK");

  // Set SMS text mode
  sendAT("AT+CMGF=1", "OK");

  // Set character set to GSM (compatible with most networks)
  sendAT("AT+CSCS=\"GSM\"", "OK");

  // Enable SMS notification via +CMTI URC (new message indication)
  sendAT("AT+CNMI=2,1,0,0,0", "OK");

  // Enable automatic time zone update from network (needed for AT+CCLK)
  sendAT("AT+CTZU=1", "OK");

  // ---- Phase 3: Wait for network registration ----
  _state = ModemState::REGISTERING;
  MESH_DEBUG_PRINTLN("[Modem] waiting for network registration...");

  bool registered = false;
  for (int i = 0; i < 60; i++) {  // up to 60 seconds
    if (sendAT("AT+CREG?", "OK", 2000)) {
      // Full response now in _atBuf, e.g.: "\r\n+CREG: 0,1\r\n\r\nOK\r\n"
      // stat: 1=registered home, 5=registered roaming
      char* p = strstr(_atBuf, "+CREG:");
      if (p) {
        int n, stat;
        if (sscanf(p, "+CREG: %d,%d", &n, &stat) == 2) {
          MESH_DEBUG_PRINTLN("[Modem] CREG: n=%d stat=%d", n, stat);
          if (stat == 1 || stat == 5) {
            registered = true;
            MESH_DEBUG_PRINTLN("[Modem] registered (stat=%d)", stat);
            break;
          }
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  if (!registered) {
    MESH_DEBUG_PRINTLN("[Modem] registration timeout - continuing anyway");
    // Don't set ERROR; some networks are slow but SMS may still work
  }

  // Query operator name
  if (sendAT("AT+COPS?", "OK", 5000)) {
    // +COPS: 0,0,"Operator Name",7
    char* p = strchr(_atBuf, '"');
    if (p) {
      p++;
      char* e = strchr(p, '"');
      if (e) {
        int len = e - p;
        if (len >= (int)sizeof(_operator)) len = sizeof(_operator) - 1;
        memcpy(_operator, p, len);
        _operator[len] = '\0';
        MESH_DEBUG_PRINTLN("[Modem] operator: %s", _operator);
      }
    }
  }

  // Initial signal query
  pollCSQ();

  // Sync ESP32 system clock from modem network time
  // Network time may take a few seconds to arrive after registration
  bool clockSet = false;
  for (int attempt = 0; attempt < 5 && !clockSet; attempt++) {
    if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(2000));
    if (sendAT("AT+CCLK?", "OK", 3000)) {
      // Response: +CCLK: "YY/MM/DD,HH:MM:SS±TZ"  (TZ in quarter-hours)
      char* p = strstr(_atBuf, "+CCLK:");
      if (p) {
        int yy = 0, mo = 0, dd = 0, hh = 0, mm = 0, ss = 0, tz = 0;
        if (sscanf(p, "+CCLK: \"%d/%d/%d,%d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss) >= 6) {
          // Skip if modem clock not synced (default is 1970 = yy 70, or yy 0)
          if (yy < 24 || yy > 50) {
            MESH_DEBUG_PRINTLN("[Modem] CCLK not synced yet (yy=%d), retrying...", yy);
            continue;
          }

          // Parse timezone offset (e.g. "+40" = UTC+10 in quarter-hours)
          char* tzp = p + 7; // skip "+CCLK: "
          while (*tzp && *tzp != '+' && *tzp != '-') tzp++;
          if (*tzp) tz = atoi(tzp);

          struct tm t = {};
          t.tm_year = yy + 100;  // years since 1900
          t.tm_mon  = mo - 1;    // 0-based
          t.tm_mday = dd;
          t.tm_hour = hh;
          t.tm_min  = mm;
          t.tm_sec  = ss;
          time_t epoch = mktime(&t);  // treats input as UTC (no TZ set on ESP32)
          epoch -= (tz * 15 * 60);    // subtract local offset to get real UTC

          struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
          settimeofday(&tv, nullptr);
          clockSet = true;
          MESH_DEBUG_PRINTLN("[Modem] System clock set: %04d-%02d-%02d %02d:%02d:%02d (tz=%+d qh, epoch=%lu)",
                             yy + 2000, mo, dd, hh, mm, ss, tz, (unsigned long)epoch);
        }
      }
    }
  }
  if (!clockSet) {
    MESH_DEBUG_PRINTLN("[Modem] WARNING: Could not sync system clock from network");
  }

  // Delete any stale SMS on SIM to free slots
  sendAT("AT+CMGD=1,4", "OK", 5000);  // Delete all read messages

  _state = ModemState::READY;
  MESH_DEBUG_PRINTLN("[Modem] READY (CSQ=%d, operator=%s)", _csq, _operator);

  // ---- Phase 4: Main loop ----
  unsigned long lastCSQPoll = 0;
  unsigned long lastSMSPoll = 0;
  const unsigned long CSQ_POLL_INTERVAL = 30000;  // 30s
  const unsigned long SMS_POLL_INTERVAL = 10000;  // 10s

  while (true) {
    // Check for outgoing SMS in queue
    SMSOutgoing outMsg;
    if (xQueueReceive(_sendQueue, &outMsg, 0) == pdTRUE) {
      _state = ModemState::SENDING_SMS;
      bool ok = doSendSMS(outMsg.phone, outMsg.body);
      MESH_DEBUG_PRINTLN("[Modem] SMS send %s to %s", ok ? "OK" : "FAIL", outMsg.phone);
      _state = ModemState::READY;
    }

    // Poll for incoming SMS periodically (not every loop iteration)
    if (millis() - lastSMSPoll > SMS_POLL_INTERVAL) {
      pollIncomingSMS();
      lastSMSPoll = millis();
    }

    // Periodic signal strength update
    if (millis() - lastCSQPoll > CSQ_POLL_INTERVAL) {
      pollCSQ();
      lastCSQPoll = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(500));  // 500ms loop — responsive for sends, calm for polls
  }
}

// ---------------------------------------------------------------------------
// Hardware Control
// ---------------------------------------------------------------------------

bool ModemManager::modemPowerOn() {
  MESH_DEBUG_PRINTLN("[Modem] powering on...");

  // Enable modem power supply (BOARD_6609_EN)
  pinMode(MODEM_POWER_EN, OUTPUT);
  digitalWrite(MODEM_POWER_EN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(500));
  MESH_DEBUG_PRINTLN("[Modem] power supply enabled (GPIO %d HIGH)", MODEM_POWER_EN);

  // Reset pulse — drive RST low briefly then release
  // (Some A7682E boards need this to clear stuck states)
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, LOW);
  vTaskDelay(pdMS_TO_TICKS(200));
  digitalWrite(MODEM_RST, HIGH);
  vTaskDelay(pdMS_TO_TICKS(500));
  MESH_DEBUG_PRINTLN("[Modem] reset pulse done (GPIO %d)", MODEM_RST);

  // PWRKEY toggle: pull low for ≥1.5s then release
  // A7682E datasheet: PWRKEY low >1s triggers power-on
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);   // Start high (idle state)
  vTaskDelay(pdMS_TO_TICKS(100));
  digitalWrite(MODEM_PWRKEY, LOW);    // Active-low trigger
  vTaskDelay(pdMS_TO_TICKS(1500));
  digitalWrite(MODEM_PWRKEY, HIGH);   // Release
  MESH_DEBUG_PRINTLN("[Modem] PWRKEY toggled, waiting for boot...");

  // Wait for modem to boot — A7682E needs 3-5 seconds after PWRKEY
  vTaskDelay(pdMS_TO_TICKS(5000));

  // Assert DTR LOW — many cellular modems require DTR active (LOW) for AT mode
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);
  MESH_DEBUG_PRINTLN("[Modem] DTR asserted LOW (GPIO %d)", MODEM_DTR);

  // Configure UART
  // NOTE: variant.h pin names are modem-perspective, so:
  //   MODEM_RX (GPIO 10) = modem receives = ESP32 TX out
  //   MODEM_TX (GPIO 11) = modem transmits = ESP32 RX in
  // Serial1.begin(baud, config, ESP32_RX, ESP32_TX)
  MODEM_SERIAL.begin(MODEM_BAUD, SERIAL_8N1, MODEM_TX, MODEM_RX);
  vTaskDelay(pdMS_TO_TICKS(500));
  MESH_DEBUG_PRINTLN("[Modem] UART started (ESP32 RX=%d TX=%d @ %d)", MODEM_TX, MODEM_RX, MODEM_BAUD);

  // Drain any boot garbage from UART
  while (MODEM_SERIAL.available()) MODEM_SERIAL.read();

  // Test communication — generous attempts
  for (int i = 0; i < 10; i++) {
    MESH_DEBUG_PRINTLN("[Modem] AT probe attempt %d/10", i + 1);
    if (sendAT("AT", "OK", 1500)) {
      MESH_DEBUG_PRINTLN("[Modem] AT responded OK");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  MESH_DEBUG_PRINTLN("[Modem] no AT response after power-on");
  return false;
}

// ---------------------------------------------------------------------------
// AT Command Helpers (called only from modem task)
// ---------------------------------------------------------------------------

bool ModemManager::sendAT(const char* cmd, const char* expect, uint32_t timeout_ms) {
  // Flush any pending data
  while (MODEM_SERIAL.available()) MODEM_SERIAL.read();

  Serial.printf("[Modem] TX: %s\n", cmd);
  MODEM_SERIAL.println(cmd);
  bool ok = waitResponse(expect, timeout_ms, _atBuf, AT_BUF_SIZE);
  if (_atBuf[0]) {
    // Trim trailing whitespace for cleaner log output
    int len = strlen(_atBuf);
    while (len > 0 && (_atBuf[len-1] == '\r' || _atBuf[len-1] == '\n')) _atBuf[--len] = '\0';
    Serial.printf("[Modem] RX: %s  [%s]\n", _atBuf, ok ? "OK" : "FAIL");
  } else {
    Serial.printf("[Modem] RX: (no response)  [TIMEOUT]\n");
  }
  return ok;
}

bool ModemManager::waitResponse(const char* expect, uint32_t timeout_ms,
                                 char* buf, size_t bufLen) {
  unsigned long start = millis();
  int pos = 0;

  if (buf && bufLen > 0) buf[0] = '\0';

  while (millis() - start < timeout_ms) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (buf && pos < (int)bufLen - 1) {
        buf[pos++] = c;
        buf[pos] = '\0';
      }
      // Check for expected response in accumulated buffer
      if (buf && expect && strstr(buf, expect)) {
        return true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  // Timeout — check one more time
  if (buf && expect && strstr(buf, expect)) return true;
  return false;
}

void ModemManager::pollCSQ() {
  if (sendAT("AT+CSQ", "OK", 2000)) {
    char* p = strstr(_atBuf, "+CSQ:");
    if (p) {
      int csq, ber;
      if (sscanf(p, "+CSQ: %d,%d", &csq, &ber) >= 1) {
        _csq = csq;
        MESH_DEBUG_PRINTLN("[Modem] CSQ=%d (bars=%d)", _csq, getSignalBars());
      }
    }
  }
}

void ModemManager::pollIncomingSMS() {
  // List all unread messages (wait for full OK response)
  if (!sendAT("AT+CMGL=\"REC UNREAD\"", "OK", 5000)) return;

  // Parse response: +CMGL: <index>,<stat>,<phone>,,<timestamp>\r\n<body>\r\n
  char* p = _atBuf;
  while ((p = strstr(p, "+CMGL:")) != nullptr) {
    int idx;
    char stat[16], phone[SMS_PHONE_LEN], timestamp[24];
    
    // Parse header line
    // +CMGL: 1,"REC UNREAD","+1234567890","","26/02/15,10:30:00+00"
    char* lineEnd = strchr(p, '\n');
    if (!lineEnd) break;

    // Extract index
    if (sscanf(p, "+CMGL: %d", &idx) != 1) { p = lineEnd + 1; continue; }

    // Extract phone number (between first and second quote pair after stat)
    char* q1 = strchr(p + 7, '"');  // skip "+CMGL: N,"
    if (!q1) { p = lineEnd + 1; continue; }
    q1++;  // skip opening quote of stat
    char* q2 = strchr(q1, '"');  // end of stat
    if (!q2) { p = lineEnd + 1; continue; }
    // Next quoted field is the phone number
    char* q3 = strchr(q2 + 1, '"');
    if (!q3) { p = lineEnd + 1; continue; }
    q3++;
    char* q4 = strchr(q3, '"');
    if (!q4) { p = lineEnd + 1; continue; }
    int phoneLen = q4 - q3;
    if (phoneLen >= SMS_PHONE_LEN) phoneLen = SMS_PHONE_LEN - 1;
    memcpy(phone, q3, phoneLen);
    phone[phoneLen] = '\0';

    // Body is on the next line
    p = lineEnd + 1;
    char* bodyEnd = strchr(p, '\r');
    if (!bodyEnd) bodyEnd = strchr(p, '\n');
    if (!bodyEnd) break;

    SMSIncoming incoming;
    memset(&incoming, 0, sizeof(incoming));
    strncpy(incoming.phone, phone, SMS_PHONE_LEN - 1);
    int bodyLen = bodyEnd - p;
    if (bodyLen >= SMS_BODY_LEN) bodyLen = SMS_BODY_LEN - 1;
    memcpy(incoming.body, p, bodyLen);
    incoming.body[bodyLen] = '\0';
    incoming.timestamp = (uint32_t)time(nullptr);  // Real epoch from modem-synced clock

    // Queue for main loop
    xQueueSend(_recvQueue, &incoming, 0);

    // Delete the message from SIM
    char delCmd[20];
    snprintf(delCmd, sizeof(delCmd), "AT+CMGD=%d", idx);
    sendAT(delCmd, "OK", 2000);

    MESH_DEBUG_PRINTLN("[Modem] SMS received from %s: %.40s...", phone, incoming.body);

    p = bodyEnd + 1;
  }
}

bool ModemManager::doSendSMS(const char* phone, const char* body) {
  MESH_DEBUG_PRINTLN("[Modem] doSendSMS to=%s len=%d", phone, strlen(body));

  // Set text mode (in case it was reset)
  sendAT("AT+CMGF=1", "OK");

  // Start SMS send
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", phone);
  Serial.printf("[Modem] TX: %s\n", cmd);
  MODEM_SERIAL.println(cmd);

  // Wait for '>' prompt
  unsigned long start = millis();
  bool gotPrompt = false;
  while (millis() - start < 5000) {
    if (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (c == '>') { gotPrompt = true; break; }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (!gotPrompt) {
    MESH_DEBUG_PRINTLN("[Modem] no '>' prompt for SMS send");
    MODEM_SERIAL.write(0x1B);  // ESC to cancel
    return false;
  }

  // Send body + Ctrl+Z
  MESH_DEBUG_PRINTLN("[Modem] got '>' prompt, sending body...");
  MODEM_SERIAL.print(body);
  MODEM_SERIAL.write(0x1A);  // Ctrl+Z to send

  // Wait for +CMGS or ERROR
  if (waitResponse("+CMGS:", 30000, _atBuf, AT_BUF_SIZE)) {
    MESH_DEBUG_PRINTLN("[Modem] SMS sent OK: %s", _atBuf);
    return true;
  }

  MESH_DEBUG_PRINTLN("[Modem] SMS send timeout/error: %s", _atBuf);
  return false;
}

#endif // HAS_4G_MODEM