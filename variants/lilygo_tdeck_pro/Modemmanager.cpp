#ifdef HAS_4G_MODEM

#include "ModemManager.h"
#include <Mesh.h>   // For MESH_DEBUG_PRINTLN

// Global singleton
ModemManager modemManager;

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

ModemManager::ModemManager()
  : _taskHandle(nullptr)
  , _sendQueue(nullptr)
  , _recvQueue(nullptr)
  , _uartMutex(nullptr)
  , _state(ModemState::OFF)
  , _csq(99)
  , _hasSIM(false)
  , _recvCallback(nullptr)
{
  memset(_operator, 0, sizeof(_operator));
  memset(_ownNumber, 0, sizeof(_ownNumber));
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ModemManager::begin() {
  if (_taskHandle) return;   // already running

  MESH_DEBUG_PRINTLN("[Modem] begin()");

  _sendQueue = xQueueCreate(SMS_SEND_QUEUE_SIZE, sizeof(SMSOutgoing));
  _recvQueue = xQueueCreate(SMS_RECV_QUEUE_SIZE, sizeof(SMSIncoming));
  _uartMutex = xSemaphoreCreateMutex();

  if (!_sendQueue || !_recvQueue || !_uartMutex) {
    MESH_DEBUG_PRINTLN("[Modem] FATAL: queue/mutex creation failed");
    _state = ModemState::ERROR;
    return;
  }

  // Spawn task pinned to core 0 (keep core 1 for mesh radio)
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

  // Tell modem to power off
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
    case ModemState::POWERING_ON:  return "POWERING ON";
    case ModemState::INITIALIZING: return "INIT";
    case ModemState::REGISTERING:  return "REGISTERING";
    case ModemState::READY:        return "READY";
    case ModemState::ERROR:        return "ERROR";
    case ModemState::SENDING_SMS:  return "SENDING";
    default:                       return "???";
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

  // ---- Phase 1: Power on ----
  _state = ModemState::POWERING_ON;
  if (!modemPowerOn()) {
    MESH_DEBUG_PRINTLN("[Modem] power-on failed");
    _state = ModemState::ERROR;
    // Don't return — stay alive so state can be queried.  Retry periodically.
    while (true) {
      vTaskDelay(pdMS_TO_TICKS(30000));
      _state = ModemState::POWERING_ON;
      if (modemPowerOn()) break;
      _state = ModemState::ERROR;
    }
  }

  // ---- Phase 2: Initialize ----
  _state = ModemState::INITIALIZING;
  if (!modemInit()) {
    MESH_DEBUG_PRINTLN("[Modem] init failed");
    _state = ModemState::ERROR;
    // stay alive for status reporting
    vTaskDelay(portMAX_DELAY);
  }

  // ---- Phase 3: Wait for network registration ----
  _state = ModemState::REGISTERING;
  if (!modemWaitRegistration()) {
    MESH_DEBUG_PRINTLN("[Modem] registration failed/timed out");
    _state = ModemState::ERROR;
    vTaskDelay(portMAX_DELAY);
  }

  _state = ModemState::READY;
  MESH_DEBUG_PRINTLN("[Modem] READY — operator: %s  CSQ: %d", _operator, _csq);

  // Delete any old read SMS from SIM to free slots
  deleteAllReadSMS();

  // ---- Phase 4: Main loop ----
  unsigned long lastStatusPoll = 0;

  while (true) {
    // --- Check for outgoing SMS in the queue ---
    SMSOutgoing outMsg;
    if (xQueueReceive(_sendQueue, &outMsg, 0) == pdTRUE) {
      _state = ModemState::SENDING_SMS;
      bool ok = doSendSMS(outMsg);
      MESH_DEBUG_PRINTLN("[Modem] sendSMS to %s: %s", outMsg.phone, ok ? "OK" : "FAIL");
      _state = ModemState::READY;
    }

    // --- Check for incoming SMS ---
    checkForNewSMS();

    // --- Periodic status poll (signal, registration) ---
    if (millis() - lastStatusPoll > MODEM_STATUS_POLL_MS) {
      pollStatus();
      lastStatusPoll = millis();
    }

    // Yield to other tasks — don't spin.  50ms gives responsive SMS
    // receive while keeping CPU usage minimal.
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

// ---------------------------------------------------------------------------
// Modem power-on sequence
// ---------------------------------------------------------------------------

bool ModemManager::modemPowerOn() {
  MESH_DEBUG_PRINTLN("[Modem] powering on...");

  // Enable modem power rail
  pinMode(MODEM_POWER_EN, OUTPUT);
  digitalWrite(MODEM_POWER_EN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(100));

  // Configure control pins
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);    // DTR LOW = modem awake

  // PWRKEY pulse (active LOW for A7682E — pull low for ~1.5s then release)
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, LOW);
  vTaskDelay(pdMS_TO_TICKS(MODEM_PWRKEY_PULSE_MS));
  digitalWrite(MODEM_PWRKEY, HIGH);

  // Wait for modem to boot
  vTaskDelay(pdMS_TO_TICKS(MODEM_POWER_SETTLE_MS));

  // Initialize UART
  // GPIO 10 was previously OUTPUT HIGH (peripheral power).
  // Serial1.begin() reconfigures it as UART RX — idle state is HIGH,
  // so peripheral power is maintained.
  MODEM_SERIAL.begin(MODEM_BAUD, SERIAL_8N1, MODEM_RX, MODEM_TX);
  vTaskDelay(pdMS_TO_TICKS(500));

  // Try a few AT pings
  flushInput();
  for (int i = 0; i < 5; i++) {
    if (sendAT("AT", "OK", 1000)) {
      MESH_DEBUG_PRINTLN("[Modem] AT responded on attempt %d", i + 1);
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }

  MESH_DEBUG_PRINTLN("[Modem] no AT response after 5 attempts");
  return false;
}

// ---------------------------------------------------------------------------
// Modem initialization (after AT responds)
// ---------------------------------------------------------------------------

bool ModemManager::modemInit() {
  // Disable echo
  sendAT("ATE0", "OK", 1000);

  // Check SIM
  char buf[64];
  if (sendAT("AT+CPIN?", "+CPIN:", buf, sizeof(buf), 5000)) {
    _hasSIM = (strstr(buf, "READY") != nullptr);
    if (!_hasSIM) {
      MESH_DEBUG_PRINTLN("[Modem] SIM not ready: %s", buf);
      return false;
    }
  } else {
    MESH_DEBUG_PRINTLN("[Modem] SIM check failed");
    return false;
  }

  // Set SMS text mode
  if (!sendAT("AT+CMGF=1", "OK", 2000)) {
    MESH_DEBUG_PRINTLN("[Modem] failed to set text mode");
    return false;
  }

  // Set SMS storage to SIM (SM) for send/recv/read
  sendAT("AT+CPMS=\"SM\",\"SM\",\"SM\"", "OK", 2000);

  // Enable new SMS notification via URC: +CMTI: "SM",<index>
  // Mode 1 = notify index; we'll then read it ourselves
  sendAT("AT+CNMI=2,1,0,0,0", "OK", 2000);

  // Set character set to GSM (most compatible for SMS)
  sendAT("AT+CSCS=\"GSM\"", "OK", 1000);

  // Try to read own number (may not work on all carriers)
  if (sendAT("AT+CNUM", "+CNUM:", buf, sizeof(buf), 2000)) {
    // Response like: +CNUM: "","+1234567890",145
    char* q1 = strchr(buf, '"');
    if (q1) {
      q1++;  // skip first quote
      char* q2 = strchr(q1, '"');
      if (q2) {
        q2++;  // skip comma+quote
        char* q3 = strchr(q2, '"');
        if (q3) {
          q3++;
          char* q4 = strchr(q3, '"');
          if (q4) {
            int len = q4 - q3;
            if (len > 0 && len < SMS_PHONE_LEN) {
              strncpy(_ownNumber, q3, len);
              _ownNumber[len] = '\0';
            }
          }
        }
      }
    }
  }

  MESH_DEBUG_PRINTLN("[Modem] init complete, SIM OK, own#: %s",
                     _ownNumber[0] ? _ownNumber : "(unknown)");
  return true;
}

// ---------------------------------------------------------------------------
// Wait for network registration
// ---------------------------------------------------------------------------

bool ModemManager::modemWaitRegistration() {
  unsigned long start = millis();
  char buf[80];

  while (millis() - start < MODEM_REG_TIMEOUT_MS) {
    if (sendAT("AT+CREG?", "+CREG:", buf, sizeof(buf), 2000)) {
      // +CREG: 0,1  (registered, home) or +CREG: 0,5 (roaming)
      char* comma = strchr(buf, ',');
      if (comma) {
        int stat = atoi(comma + 1);
        if (stat == 1 || stat == 5) {
          // Registered — get operator name
          if (sendAT("AT+COPS?", "+COPS:", buf, sizeof(buf), 2000)) {
            // +COPS: 0,0,"OperatorName",7
            char* q1 = strchr(buf, '"');
            if (q1) {
              q1++;
              char* q2 = strchr(q1, '"');
              if (q2) {
                int len = q2 - q1;
                if (len >= (int)sizeof(_operator)) len = sizeof(_operator) - 1;
                strncpy(_operator, q1, len);
                _operator[len] = '\0';
              }
            }
          }
          // Get signal quality
          pollStatus();
          return true;
        }
      }
    }
    MESH_DEBUG_PRINTLN("[Modem] waiting for registration... (%lus)",
                       (millis() - start) / 1000);
    vTaskDelay(pdMS_TO_TICKS(2000));
  }

  return false;  // timed out
}

// ---------------------------------------------------------------------------
// AT command helpers
// ---------------------------------------------------------------------------

bool ModemManager::sendAT(const char* cmd, const char* expect,
                           char* respBuf, size_t respLen, uint32_t timeoutMs) {
  if (xSemaphoreTake(_uartMutex, pdMS_TO_TICKS(timeoutMs)) != pdTRUE) {
    return false;
  }

  flushInput();
  MODEM_SERIAL.println(cmd);

  char line[128];
  unsigned long deadline = millis() + timeoutMs;
  bool found = false;

  while (millis() < deadline) {
    readLine(line, sizeof(line), deadline - millis());
    if (line[0] == '\0') continue;

    // Check for the expected prefix
    if (strstr(line, expect)) {
      if (respBuf && respLen > 0) {
        strncpy(respBuf, line, respLen - 1);
        respBuf[respLen - 1] = '\0';
      }
      found = true;
      // Continue reading to consume the trailing "OK" or extra lines
      // so they don't pollute the next command
      readLine(line, sizeof(line), 200);
      break;
    }

    // Also accept bare OK
    if (strcmp(expect, "OK") == 0 && strstr(line, "OK")) {
      if (respBuf && respLen > 0) {
        strncpy(respBuf, "OK", respLen - 1);
      }
      found = true;
      break;
    }

    // Check for error
    if (strstr(line, "ERROR")) {
      break;
    }
  }

  xSemaphoreGive(_uartMutex);
  return found;
}

bool ModemManager::sendAT(const char* cmd, const char* expect, uint32_t timeoutMs) {
  return sendAT(cmd, expect, nullptr, 0, timeoutMs);
}

void ModemManager::readLine(char* buf, size_t maxLen, uint32_t timeoutMs) {
  size_t pos = 0;
  unsigned long deadline = millis() + timeoutMs;
  buf[0] = '\0';

  while (millis() < deadline && pos < maxLen - 1) {
    if (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (c == '\n') {
        break;       // end of line
      }
      if (c != '\r' && c >= 0x20) {
        buf[pos++] = c;
      }
    } else {
      vTaskDelay(1);   // yield while waiting for data
    }
  }
  buf[pos] = '\0';
}

void ModemManager::flushInput() {
  while (MODEM_SERIAL.available()) {
    MODEM_SERIAL.read();
  }
}

// ---------------------------------------------------------------------------
// SMS send
// ---------------------------------------------------------------------------

bool ModemManager::doSendSMS(const SMSOutgoing& sms) {
  if (xSemaphoreTake(_uartMutex, pdMS_TO_TICKS(10000)) != pdTRUE) {
    return false;
  }

  flushInput();

  // AT+CMGS="<phone>"
  char cmd[48];
  snprintf(cmd, sizeof(cmd), "AT+CMGS=\"%s\"", sms.phone);
  MODEM_SERIAL.println(cmd);

  // Wait for the '>' prompt
  unsigned long deadline = millis() + 5000;
  bool gotPrompt = false;
  while (millis() < deadline) {
    if (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (c == '>') {
        gotPrompt = true;
        break;
      }
    }
    vTaskDelay(1);
  }

  if (!gotPrompt) {
    // Send ESC to cancel
    MODEM_SERIAL.write(0x1B);
    vTaskDelay(pdMS_TO_TICKS(500));
    xSemaphoreGive(_uartMutex);
    return false;
  }

  // Send message body followed by Ctrl+Z (0x1A)
  MODEM_SERIAL.print(sms.body);
  MODEM_SERIAL.write(0x1A);

  // Wait for +CMGS: <mr> or OK (up to 30s for network confirmation)
  char line[128];
  bool success = false;
  deadline = millis() + 30000;

  while (millis() < deadline) {
    readLine(line, sizeof(line), deadline - millis());
    if (strstr(line, "+CMGS:") || strstr(line, "OK")) {
      success = true;
      break;
    }
    if (strstr(line, "ERROR")) {
      break;
    }
  }

  xSemaphoreGive(_uartMutex);
  return success;
}

// ---------------------------------------------------------------------------
// SMS receive — check for +CMTI URCs and read new messages
// ---------------------------------------------------------------------------

void ModemManager::checkForNewSMS() {
  if (xSemaphoreTake(_uartMutex, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  // Read any pending unsolicited data
  while (MODEM_SERIAL.available()) {
    char line[128];
    readLine(line, sizeof(line), 100);

    if (line[0] == '\0') continue;

    // Look for +CMTI: "SM",<index>
    char* cmti = strstr(line, "+CMTI:");
    if (cmti) {
      char* comma = strchr(cmti, ',');
      if (comma) {
        int idx = atoi(comma + 1);
        MESH_DEBUG_PRINTLN("[Modem] new SMS at index %d", idx);

        // Release mutex before read (readSMS will re-acquire)
        xSemaphoreGive(_uartMutex);

        SMSIncoming incoming;
        if (readSMS(idx, incoming)) {
          // Push to receive queue
          if (xQueueSend(_recvQueue, &incoming, 0) != pdTRUE) {
            MESH_DEBUG_PRINTLN("[Modem] recv queue full, SMS dropped");
          }
          // Notify via callback
          if (_recvCallback) {
            _recvCallback(incoming);
          }
          // Delete from SIM after reading
          deleteSMS(idx);
        }
        return;  // mutex already released
      }
    }
  }

  xSemaphoreGive(_uartMutex);
}

bool ModemManager::readSMS(int index, SMSIncoming& out) {
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CMGR=%d", index);

  char buf[256];
  memset(&out, 0, sizeof(out));

  if (xSemaphoreTake(_uartMutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
    return false;
  }

  flushInput();
  MODEM_SERIAL.println(cmd);

  // Response format (text mode):
  //   +CMGR: "REC UNREAD","+1234567890","","25/01/15,10:30:00+00"
  //   Message body here
  //   OK

  char headerLine[160];
  readLine(headerLine, sizeof(headerLine), 3000);  // may be echo or empty

  // Skip echo or empty lines until we find +CMGR:
  unsigned long deadline = millis() + 3000;
  while (millis() < deadline) {
    if (strstr(headerLine, "+CMGR:")) break;
    readLine(headerLine, sizeof(headerLine), deadline - millis());
  }

  if (!strstr(headerLine, "+CMGR:")) {
    xSemaphoreGive(_uartMutex);
    return false;
  }

  // Parse phone number from header
  // +CMGR: "REC UNREAD","+1234567890","","25/01/15,10:30:00+00"
  char* firstComma = strchr(headerLine, ',');
  if (firstComma) {
    char* q1 = strchr(firstComma, '"');
    if (q1) {
      q1++;
      char* q2 = strchr(q1, '"');
      if (q2) {
        int len = q2 - q1;
        if (len >= SMS_PHONE_LEN) len = SMS_PHONE_LEN - 1;
        strncpy(out.phone, q1, len);
        out.phone[len] = '\0';
      }
    }
  }

  // Next line is the message body
  readLine(buf, sizeof(buf), 2000);
  strncpy(out.body, buf, SMS_BODY_LEN - 1);
  out.body[SMS_BODY_LEN - 1] = '\0';

  // Timestamp: use current time since parsing modem timestamps is fragile
  out.timestamp = millis() / 1000;  // caller should set from RTC if available

  // Drain any remaining lines (OK, etc.)
  readLine(buf, sizeof(buf), 500);

  xSemaphoreGive(_uartMutex);
  return (out.phone[0] != '\0');
}

bool ModemManager::deleteSMS(int index) {
  char cmd[24];
  snprintf(cmd, sizeof(cmd), "AT+CMGD=%d", index);
  return sendAT(cmd, "OK", 3000);
}

void ModemManager::deleteAllReadSMS() {
  // AT+CMGD=1,1 = delete all read messages
  sendAT("AT+CMGD=1,1", "OK", 5000);
}

// ---------------------------------------------------------------------------
// Status polling
// ---------------------------------------------------------------------------

void ModemManager::pollStatus() {
  char buf[64];

  // Signal quality
  if (sendAT("AT+CSQ", "+CSQ:", buf, sizeof(buf), 2000)) {
    char* colon = strchr(buf, ':');
    if (colon) {
      _csq = atoi(colon + 1);
    }
  }

  // Registration still valid?
  if (sendAT("AT+CREG?", "+CREG:", buf, sizeof(buf), 2000)) {
    char* comma = strchr(buf, ',');
    if (comma) {
      int stat = atoi(comma + 1);
      if (stat != 1 && stat != 5) {
        MESH_DEBUG_PRINTLN("[Modem] lost registration (stat=%d)", stat);
        // Don't change state to ERROR — it may re-register.
        // Just log it; the UI can show signal bars = 0.
      }
    }
  }
}

#endif // HAS_4G_MODEM