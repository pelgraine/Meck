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

// Config file paths
#define MODEM_CONFIG_FILE "/sms/modem.cfg"
#define APN_CONFIG_FILE   "/sms/apn.cfg"

// ---------------------------------------------------------------------------
// Public API - SMS (unchanged)
// ---------------------------------------------------------------------------

void ModemManager::begin() {
  MESH_DEBUG_PRINTLN("[Modem] begin()");

  _state = ModemState::OFF;
  _csq = 99;
  _operator[0] = '\0';
  _callPhone[0] = '\0';
  _callStartTime = 0;
  _urcPos = 0;
  _imei[0] = '\0';
  _imsi[0] = '\0';
  _apn[0] = '\0';
  strcpy(_apnSource, "none");

  // Create FreeRTOS primitives
  _sendQueue   = xQueueCreate(MODEM_SEND_QUEUE_SIZE, sizeof(SMSOutgoing));
  _recvQueue   = xQueueCreate(MODEM_RECV_QUEUE_SIZE, sizeof(SMSIncoming));
  _callCmdQueue = xQueueCreate(MODEM_CALL_CMD_QUEUE_SIZE, sizeof(CallCommand));
  _callEvtQueue = xQueueCreate(MODEM_CALL_EVT_QUEUE_SIZE, sizeof(CallEvent));
  _uartMutex   = xSemaphoreCreateMutex();

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

  // Hang up any active call first
  if (isCallActive()) {
    CallCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.cmd = CallCmd::HANGUP;
    xQueueSend(_callCmdQueue, &cmd, pdMS_TO_TICKS(500));
    vTaskDelay(pdMS_TO_TICKS(2000));  // Give time for AT+CHUP
  }

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

// ---------------------------------------------------------------------------
// Public API - Voice Calls
// ---------------------------------------------------------------------------

bool ModemManager::dialCall(const char* phone) {
  if (!_callCmdQueue) return false;
  if (isCallActive()) return false;  // Already in a call

  CallCommand cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd = CallCmd::DIAL;
  strncpy(cmd.phone, phone, SMS_PHONE_LEN - 1);

  return xQueueSend(_callCmdQueue, &cmd, 0) == pdTRUE;
}

bool ModemManager::answerCall() {
  if (!_callCmdQueue) return false;

  CallCommand cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd = CallCmd::ANSWER;

  return xQueueSend(_callCmdQueue, &cmd, 0) == pdTRUE;
}

bool ModemManager::hangupCall() {
  if (!_callCmdQueue) return false;

  CallCommand cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd = CallCmd::HANGUP;

  return xQueueSend(_callCmdQueue, &cmd, 0) == pdTRUE;
}

bool ModemManager::sendDTMF(char digit) {
  if (!_callCmdQueue) return false;
  if (_state != ModemState::IN_CALL) return false;

  CallCommand cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd = CallCmd::DTMF;
  cmd.dtmf = digit;

  return xQueueSend(_callCmdQueue, &cmd, 0) == pdTRUE;
}

bool ModemManager::setCallVolume(uint8_t level) {
  if (!_callCmdQueue) return false;

  CallCommand cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.cmd = CallCmd::SET_VOLUME;
  cmd.volume = level > 5 ? 5 : level;

  return xQueueSend(_callCmdQueue, &cmd, 0) == pdTRUE;
}

bool ModemManager::pollCallEvent(CallEvent& out) {
  if (!_callEvtQueue) return false;
  return xQueueReceive(_callEvtQueue, &out, 0) == pdTRUE;
}

// ---------------------------------------------------------------------------
// State helpers
// ---------------------------------------------------------------------------

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
    case ModemState::DIALING:      return "DIALING";
    case ModemState::RINGING_IN:   return "INCOMING";
    case ModemState::IN_CALL:      return "IN CALL";
    default:                       return "???";
  }
}

// ---------------------------------------------------------------------------
// Persistent modem enable/disable config
// ---------------------------------------------------------------------------

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
// APN Configuration
// ---------------------------------------------------------------------------

void ModemManager::setAPN(const char* apn) {
  strncpy(_apn, apn, sizeof(_apn) - 1);
  _apn[sizeof(_apn) - 1] = '\0';
  strcpy(_apnSource, "user");
  saveAPNConfig(apn);
  MESH_DEBUG_PRINTLN("[Modem] APN set by user: %s", _apn);
}

bool ModemManager::loadAPNConfig(char* apnOut, int maxLen) {
  File f = SD.open(APN_CONFIG_FILE, FILE_READ);
  if (!f) { return false; }
  String line = f.readStringUntil('\n');
  f.close();
  line.trim();
  if (line.length() == 0) return false;
  strncpy(apnOut, line.c_str(), maxLen - 1);
  apnOut[maxLen - 1] = '\0';
  return true;
}

void ModemManager::saveAPNConfig(const char* apn) {
  if (!SD.exists("/sms")) SD.mkdir("/sms");
  File f = SD.open(APN_CONFIG_FILE, FILE_WRITE);
  if (f) {
    f.println(apn);
    f.close();
    Serial.printf("[Modem] APN config saved: %s\n", apn);
  }
}

// ---------------------------------------------------------------------------
// APN Resolution — called during init after network registration
//
// Priority:
//   1. User-configured APN (from /sms/apn.cfg)
//   2. Network-provisioned APN (AT+CGDCONT? — modem already has one)
//   3. Auto-detected from IMSI via embedded ApnDatabase
//   4. Blank (some carriers work with empty APN)
// ---------------------------------------------------------------------------

void ModemManager::resolveAPN() {
  // 1. Check for user-configured APN on SD card
  char userApn[64];
  if (loadAPNConfig(userApn, sizeof(userApn))) {
    strncpy(_apn, userApn, sizeof(_apn) - 1);
    strcpy(_apnSource, "user");
    MESH_DEBUG_PRINTLN("[Modem] APN from user config: %s", _apn);

    // Apply to modem
    char cmd[80];
    snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", _apn);
    sendAT(cmd, "OK", 3000);
    return;
  }

  // 2. Check if modem already has a network-provisioned APN
  if (sendAT("AT+CGDCONT?", "OK", 3000)) {
    // Response: +CGDCONT: 1,"IP","telstra.internet",,0,0
    char* p = strstr(_atBuf, "+CGDCONT:");
    if (p) {
      char* q1 = strchr(p, '"');     // first quote (before IP)
      if (q1) q1 = strchr(q1 + 1, '"');  // close quote of IP
      if (q1) q1 = strchr(q1 + 1, '"');  // open quote of APN
      if (q1) {
        q1++;
        char* q2 = strchr(q1, '"');
        if (q2 && q2 > q1) {
          int len = q2 - q1;
          if (len > 0 && len < (int)sizeof(_apn)) {
            memcpy(_apn, q1, len);
            _apn[len] = '\0';
            strcpy(_apnSource, "network");
            MESH_DEBUG_PRINTLN("[Modem] APN from network/modem: %s", _apn);
            return;
          }
        }
      }
    }
  }

  // 3. Auto-detect from IMSI using embedded database
  if (_imsi[0]) {
    const ApnEntry* entry = apnLookupFromIMSI(_imsi);
    if (entry) {
      strncpy(_apn, entry->apn, sizeof(_apn) - 1);
      strcpy(_apnSource, "auto");
      MESH_DEBUG_PRINTLN("[Modem] APN auto-detected: %s (%s)", _apn, entry->carrier);

      // Apply to modem
      char cmd[80];
      snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", _apn);
      sendAT(cmd, "OK", 3000);
      return;
    }
  }

  // 4. No APN found — leave blank
  _apn[0] = '\0';
  strcpy(_apnSource, "none");
  MESH_DEBUG_PRINTLN("[Modem] APN: none detected (IMSI=%s)", _imsi[0] ? _imsi : "unknown");
}

// ---------------------------------------------------------------------------
// URC (Unsolicited Result Code) Handling
// ---------------------------------------------------------------------------
// The modem can send unsolicited messages at any time:
//   RING                        — incoming call ringing
//   +CLIP: "+1234...",145,...   — caller ID (after AT+CLIP=1)
//   NO CARRIER                  — call ended by remote
//   BUSY                        — outgoing call busy
//   NO ANSWER                   — outgoing call no answer
//   +CMTI: "SM",<idx>          — new SMS arrived
//
// drainURCs() accumulates bytes into a line buffer and calls
// processURCLine() for each complete line.
// ---------------------------------------------------------------------------

void ModemManager::drainURCs() {
  while (MODEM_SERIAL.available()) {
    char c = MODEM_SERIAL.read();

    // Accumulate into line buffer
    if (c == '\n') {
      // End of line — process if non-empty
      if (_urcPos > 0) {
        // Trim trailing \r
        while (_urcPos > 0 && _urcBuf[_urcPos - 1] == '\r') _urcPos--;
        _urcBuf[_urcPos] = '\0';

        if (_urcPos > 0) {
          processURCLine(_urcBuf);
        }
      }
      _urcPos = 0;
    } else if (c != '\r' || _urcPos > 0) {
      // Accumulate (skip leading \r)
      if (_urcPos < URC_BUF_SIZE - 1) {
        _urcBuf[_urcPos++] = c;
      }
    }
  }
}

void ModemManager::processURCLine(const char* line) {
  // --- RING: incoming call ---
  if (strcmp(line, "RING") == 0) {
    MESH_DEBUG_PRINTLN("[Modem] URC: RING");
    if (_state != ModemState::RINGING_IN && _state != ModemState::IN_CALL) {
      _state = ModemState::RINGING_IN;
      // Phone number will be filled by +CLIP if available
      // Queue event with empty phone (updated by +CLIP)
      // Only queue on first RING; subsequent RINGs are repeats
      if (_callPhone[0] == '\0') {
        queueCallEvent(CallEventType::INCOMING, "");
      }
    }
    return;
  }

  // --- +CLIP: caller ID ---
  // +CLIP: "+61412345678",145,,,,0
  if (strncmp(line, "+CLIP:", 6) == 0) {
    char* q1 = strchr(line + 6, '"');
    if (q1) {
      q1++;
      char* q2 = strchr(q1, '"');
      if (q2) {
        int len = q2 - q1;
        if (len >= SMS_PHONE_LEN) len = SMS_PHONE_LEN - 1;
        memcpy(_callPhone, q1, len);
        _callPhone[len] = '\0';
        MESH_DEBUG_PRINTLN("[Modem] URC: CLIP phone=%s", _callPhone);

        // Re-queue INCOMING event with the actual phone number
        // (replaces the empty-phone event from RING)
        if (_state == ModemState::RINGING_IN) {
          queueCallEvent(CallEventType::INCOMING, _callPhone);
        }
      }
    }
    return;
  }

  // --- NO CARRIER: call ended ---
  if (strcmp(line, "NO CARRIER") == 0) {
    MESH_DEBUG_PRINTLN("[Modem] URC: NO CARRIER");
    if (_state == ModemState::RINGING_IN) {
      // Incoming call ended before we answered — missed call
      queueCallEvent(CallEventType::MISSED, _callPhone);
    } else if (_state == ModemState::DIALING || _state == ModemState::IN_CALL) {
      uint32_t duration = 0;
      if (_state == ModemState::IN_CALL && _callStartTime > 0) {
        duration = (millis() - _callStartTime) / 1000;
      }
      queueCallEvent(CallEventType::ENDED, _callPhone, duration);
    }
    _state = ModemState::READY;
    _callPhone[0] = '\0';
    _callStartTime = 0;
    return;
  }

  // --- BUSY ---
  if (strcmp(line, "BUSY") == 0) {
    MESH_DEBUG_PRINTLN("[Modem] URC: BUSY");
    if (_state == ModemState::DIALING) {
      queueCallEvent(CallEventType::BUSY, _callPhone);
      _state = ModemState::READY;
      _callPhone[0] = '\0';
    }
    return;
  }

  // --- NO ANSWER ---
  if (strcmp(line, "NO ANSWER") == 0) {
    MESH_DEBUG_PRINTLN("[Modem] URC: NO ANSWER");
    if (_state == ModemState::DIALING) {
      queueCallEvent(CallEventType::NO_ANSWER, _callPhone);
      _state = ModemState::READY;
      _callPhone[0] = '\0';
    }
    return;
  }

  // --- +CMTI: new SMS indication ---
  // +CMTI: "SM",<index>
  // We don't need to act on this immediately since we poll for SMS,
  // but we can trigger an early poll
  if (strncmp(line, "+CMTI:", 6) == 0) {
    MESH_DEBUG_PRINTLN("[Modem] URC: CMTI (new SMS)");
    // Next SMS poll will pick it up; we just log it
    return;
  }

  // --- VOICE CALL: BEGIN — A76xx-specific: audio path established ---
  if (strncmp(line, "VOICE CALL: BEGIN", 17) == 0) {
    MESH_DEBUG_PRINTLN("[Modem] URC: VOICE CALL: BEGIN");
    if (_state == ModemState::DIALING) {
      _state = ModemState::IN_CALL;
      _callStartTime = millis();
      queueCallEvent(CallEventType::CONNECTED, _callPhone);
      MESH_DEBUG_PRINTLN("[Modem] Call connected (VOICE CALL: BEGIN)");
    }
    return;
  }

  // --- VOICE CALL: END — A76xx-specific: audio path closed ---
  // Format: "VOICE CALL: END: <duration>"
  if (strncmp(line, "VOICE CALL: END", 15) == 0) {
    MESH_DEBUG_PRINTLN("[Modem] URC: %s", line);
    // Parse duration if present: "VOICE CALL: END: 0:12"
    uint32_t duration = 0;
    const char* dp = strstr(line, "END:");
    if (dp) {
      dp += 4;
      while (*dp == ' ') dp++;
      int mins = 0, secs = 0;
      if (sscanf(dp, "%d:%d", &mins, &secs) == 2) {
        duration = mins * 60 + secs;
      }
    }
    if (_state == ModemState::RINGING_IN) {
      queueCallEvent(CallEventType::MISSED, _callPhone);
    } else if (_state == ModemState::IN_CALL || _state == ModemState::DIALING) {
      queueCallEvent(CallEventType::ENDED, _callPhone, duration);
    }
    _state = ModemState::READY;
    _callPhone[0] = '\0';
    _callStartTime = 0;
    return;
  }
}

void ModemManager::queueCallEvent(CallEventType type, const char* phone, uint32_t duration) {
  CallEvent evt;
  memset(&evt, 0, sizeof(evt));
  evt.type = type;
  evt.duration = duration;
  if (phone) {
    strncpy(evt.phone, phone, SMS_PHONE_LEN - 1);
  }
  xQueueSend(_callEvtQueue, &evt, 0);
}

// ---------------------------------------------------------------------------
// Call control (executed on modem task)
// ---------------------------------------------------------------------------

bool ModemManager::doDialCall(const char* phone) {
  MESH_DEBUG_PRINTLN("[Modem] doDialCall: %s", phone);

  strncpy(_callPhone, phone, SMS_PHONE_LEN - 1);
  _callPhone[SMS_PHONE_LEN - 1] = '\0';
  _state = ModemState::DIALING;

  // ATD<number>; — the semicolon makes it a voice call (not data)
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "ATD%s;", phone);

  if (!sendAT(cmd, "OK", 30000)) {
    MESH_DEBUG_PRINTLN("[Modem] ATD failed");
    queueCallEvent(CallEventType::DIAL_FAILED, phone);
    _state = ModemState::READY;
    _callPhone[0] = '\0';
    return false;
  }

  // ATD returned OK — call is being set up.
  // Connection/failure will come as URCs (NO CARRIER, BUSY, etc.)
  // or we detect active call via AT+CLCC polling.
  // For now, assume we're dialing and wait for URCs.
  MESH_DEBUG_PRINTLN("[Modem] ATD OK — dialing...");
  return true;
}

bool ModemManager::doAnswerCall() {
  MESH_DEBUG_PRINTLN("[Modem] doAnswerCall");

  if (sendAT("ATA", "OK", 10000)) {
    _state = ModemState::IN_CALL;
    _callStartTime = millis();
    queueCallEvent(CallEventType::CONNECTED, _callPhone);
    MESH_DEBUG_PRINTLN("[Modem] Call answered");
    return true;
  }

  MESH_DEBUG_PRINTLN("[Modem] ATA failed");
  return false;
}

bool ModemManager::doHangup() {
  MESH_DEBUG_PRINTLN("[Modem] doHangup (state=%d)", (int)_state);

  uint32_t duration = 0;
  if (_state == ModemState::IN_CALL && _callStartTime > 0) {
    duration = (millis() - _callStartTime) / 1000;
  }

  bool wasRinging = (_state == ModemState::RINGING_IN);

  // AT+CHUP is the 3GPP standard hangup for A76xx family (per TinyGSM)
  if (sendAT("AT+CHUP", "OK", 5000)) {
    if (wasRinging) {
      queueCallEvent(CallEventType::MISSED, _callPhone);
    } else {
      queueCallEvent(CallEventType::ENDED, _callPhone, duration);
    }
    _state = ModemState::READY;
    _callPhone[0] = '\0';
    _callStartTime = 0;
    MESH_DEBUG_PRINTLN("[Modem] Hangup OK");
    return true;
  }

  MESH_DEBUG_PRINTLN("[Modem] AT+CHUP failed");
  // Force state back to READY even if hangup fails
  _state = ModemState::READY;
  _callPhone[0] = '\0';
  _callStartTime = 0;
  return false;
}

bool ModemManager::doSendDTMF(char digit) {
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "AT+VTS=%c", digit);
  bool ok = sendAT(cmd, "OK", 3000);
  MESH_DEBUG_PRINTLN("[Modem] DTMF '%c' %s", digit, ok ? "OK" : "FAIL");
  return ok;
}

bool ModemManager::doSetVolume(uint8_t level) {
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "AT+CLVL=%d", level);
  bool ok = sendAT(cmd, "OK", 2000);
  MESH_DEBUG_PRINTLN("[Modem] Volume %d %s", level, ok ? "OK" : "FAIL");
  return ok;
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

  // --- Query device identity ---
  // IMEI (International Mobile Equipment Identity)
  if (sendAT("AT+GSN", "OK", 3000)) {
    // Response is just the IMEI number on its own line
    char* p = _atBuf;
    while (*p && !isdigit(*p)) p++;  // skip to first digit
    int i = 0;
    while (isdigit(p[i]) && i < 19) { _imei[i] = p[i]; i++; }
    _imei[i] = '\0';
    MESH_DEBUG_PRINTLN("[Modem] IMEI: %s", _imei);
  }

  // IMSI (International Mobile Subscriber Identity) — for APN auto-detection
  if (sendAT("AT+CIMI", "OK", 3000)) {
    char* p = _atBuf;
    while (*p && !isdigit(*p)) p++;
    int i = 0;
    while (isdigit(p[i]) && i < 19) { _imsi[i] = p[i]; i++; }
    _imsi[i] = '\0';
    MESH_DEBUG_PRINTLN("[Modem] IMSI: %s", _imsi);
  }

  // Set SMS text mode
  sendAT("AT+CMGF=1", "OK");

  // Set character set to GSM (compatible with most networks)
  sendAT("AT+CSCS=\"GSM\"", "OK");

  // Enable SMS notification via +CMTI URC (new message indication)
  sendAT("AT+CNMI=2,1,0,0,0", "OK");

  // Enable automatic time zone update from network (needed for AT+CCLK)
  sendAT("AT+CTZU=1", "OK");

  // --- Voice call setup ---
  // Enable caller ID presentation (CLIP) so we get +CLIP URCs on incoming calls
  sendAT("AT+CLIP=1", "OK");

  // Set audio output to loudspeaker mode (device speaker)
  // 1=earpiece, 3=loudspeaker — use loudspeaker for T-Deck Pro
  sendAT("AT+CSDVC=3", "OK", 1000);

  // Set initial call volume (mid-level)
  sendAT("AT+CLVL=3", "OK", 1000);

  // ---- Phase 3: Wait for network registration ----
  _state = ModemState::REGISTERING;
  MESH_DEBUG_PRINTLN("[Modem] waiting for network registration...");

  bool registered = false;
  for (int i = 0; i < 60; i++) {  // up to 60 seconds
    if (sendAT("AT+CREG?", "OK", 2000)) {
      // Full response now in _atBuf, e.g.: "\r\n+CREG: 0,1\r\n\r\nOK\r\n"
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
  }

  // Query operator name
  // AT+COPS=3,0 sets the format to "long alphanumeric" so AT+COPS?
  // returns "Optus" instead of "50502"
  sendAT("AT+COPS=3,0", "OK", 2000);

  if (sendAT("AT+COPS?", "OK", 5000)) {
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

  // If operator is still numeric (all digits), look up friendly name from IMSI
  if (_operator[0] && isdigit(_operator[0])) {
    bool allDigits = true;
    for (int i = 0; _operator[i]; i++) {
      if (!isdigit(_operator[i])) { allDigits = false; break; }
    }
    if (allDigits && _imsi[0]) {
      const ApnEntry* entry = apnLookupFromIMSI(_imsi);
      if (entry && entry->carrier) {
        strncpy(_operator, entry->carrier, sizeof(_operator) - 1);
        _operator[sizeof(_operator) - 1] = '\0';
        MESH_DEBUG_PRINTLN("[Modem] operator (from IMSI lookup): %s", _operator);
      }
    }
  }

  // Initial signal query
  pollCSQ();

  // Resolve APN (user config → network provisioned → IMSI auto-detect)
  resolveAPN();

  // Sync ESP32 system clock from modem network time
  bool clockSet = false;
  for (int attempt = 0; attempt < 5 && !clockSet; attempt++) {
    if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(2000));
    if (sendAT("AT+CCLK?", "OK", 3000)) {
      char* p = strstr(_atBuf, "+CCLK:");
      if (p) {
        int yy = 0, mo = 0, dd = 0, hh = 0, mm = 0, ss = 0, tz = 0;
        if (sscanf(p, "+CCLK: \"%d/%d/%d,%d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss) >= 6) {
          if (yy < 24 || yy > 50) {
            MESH_DEBUG_PRINTLN("[Modem] CCLK not synced yet (yy=%d), retrying...", yy);
            continue;
          }

          // Parse timezone offset
          char* tzp = p + 7;
          while (*tzp && *tzp != '+' && *tzp != '-') tzp++;
          if (*tzp) tz = atoi(tzp);

          struct tm t = {};
          t.tm_year = yy + 100;
          t.tm_mon  = mo - 1;
          t.tm_mday = dd;
          t.tm_hour = hh;
          t.tm_min  = mm;
          t.tm_sec  = ss;
          time_t epoch = mktime(&t);
          epoch -= (tz * 15 * 60);

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
  sendAT("AT+CMGD=1,4", "OK", 5000);

  _state = ModemState::READY;
  MESH_DEBUG_PRINTLN("[Modem] READY (CSQ=%d, operator=%s, APN=%s [%s], IMEI=%s)",
                     _csq, _operator, _apn[0] ? _apn : "(none)", _apnSource, _imei);

  // ---- Phase 4: Main loop ----
  unsigned long lastCSQPoll = 0;
  unsigned long lastSMSPoll = 0;
  unsigned long lastCLCCPoll = 0;
  const unsigned long CSQ_POLL_INTERVAL = 30000;   // 30s
  const unsigned long SMS_POLL_INTERVAL = 10000;   // 10s
  const unsigned long CLCC_POLL_INTERVAL = 2000;   // 2s (during dialing only)

  while (true) {
    // ================================================================
    // Step 1: Drain URCs — catch RING, NO CARRIER, +CLIP, etc.
    // This must run every iteration to avoid missing time-sensitive
    // events like incoming calls or call-ended notifications.
    // ================================================================
    drainURCs();

    // ================================================================
    // Step 2: Process call commands from main loop
    // ================================================================
    CallCommand callCmd;
    if (xQueueReceive(_callCmdQueue, &callCmd, 0) == pdTRUE) {
      switch (callCmd.cmd) {
        case CallCmd::DIAL:
          if (_state == ModemState::READY) {
            doDialCall(callCmd.phone);
          } else {
            MESH_DEBUG_PRINTLN("[Modem] Can't dial — state=%d", (int)_state);
            queueCallEvent(CallEventType::DIAL_FAILED, callCmd.phone);
          }
          break;

        case CallCmd::ANSWER:
          if (_state == ModemState::RINGING_IN) {
            doAnswerCall();
          }
          break;

        case CallCmd::HANGUP:
          if (isCallActive()) {
            doHangup();
          }
          break;

        case CallCmd::DTMF:
          if (_state == ModemState::IN_CALL) {
            doSendDTMF(callCmd.dtmf);
          }
          break;

        case CallCmd::SET_VOLUME:
          doSetVolume(callCmd.volume);
          break;
      }
    }

    // ================================================================
    // Step 3: Poll AT+CLCC during DIALING as fallback.
    // Primary detection is via "VOICE CALL: BEGIN" URC (handled by
    // drainURCs/processURCLine above). CLCC polling is a safety net
    // in case the URC is missed or delayed.
    // Skip when paused to avoid Core 0 contention with WiFi TLS.
    // ================================================================
    if (!_paused &&
        _state == ModemState::DIALING &&
        millis() - lastCLCCPoll > CLCC_POLL_INTERVAL) {
      if (sendAT("AT+CLCC", "OK", 2000)) {
        // +CLCC: 1,0,0,0,0,"number",129   — stat field:
        //   0=active, 1=held, 2=dialing, 3=alerting, 4=incoming, 5=waiting
        char* p = strstr(_atBuf, "+CLCC:");
        if (p) {
          int idx, dir, stat, mode, mpty;
          if (sscanf(p, "+CLCC: %d,%d,%d,%d,%d", &idx, &dir, &stat, &mode, &mpty) >= 3) {
            MESH_DEBUG_PRINTLN("[Modem] CLCC: stat=%d", stat);
            if (stat == 0) {
              // Call is active — remote answered
              _state = ModemState::IN_CALL;
              _callStartTime = millis();
              queueCallEvent(CallEventType::CONNECTED, _callPhone);
              MESH_DEBUG_PRINTLN("[Modem] Call connected (detected via CLCC)");
            }
            // stat 2=dialing, 3=alerting — still setting up, keep polling
          }
        } else {
          // No +CLCC line in response — no active calls
          // This shouldn't happen during DIALING unless the call ended
          // and we missed the URC. Check state and clean up.
          // (NO CARRIER URC should have been caught by drainURCs)
        }
      }
      lastCLCCPoll = millis();
    }

    // ================================================================
    // Step 4: SMS and signal polling (only when not in a call)
    // Skip when paused to avoid Core 0 contention with WiFi/TLS.
    // The modem task's sendAT() calls (AT+CMGL 5s, AT+CSQ 2s) do
    // tight UART poll loops that disrupt WiFi packet timing.
    // ================================================================
    if (!_paused && !isCallActive()) {
      // Check for outgoing SMS in queue
      SMSOutgoing outMsg;
      if (xQueueReceive(_sendQueue, &outMsg, 0) == pdTRUE) {
        _state = ModemState::SENDING_SMS;
        bool ok = doSendSMS(outMsg.phone, outMsg.body);
        MESH_DEBUG_PRINTLN("[Modem] SMS send %s to %s", ok ? "OK" : "FAIL", outMsg.phone);
        _state = ModemState::READY;
      }

      // Poll for incoming SMS periodically
      if (millis() - lastSMSPoll > SMS_POLL_INTERVAL) {
        pollIncomingSMS();
        lastSMSPoll = millis();
      }
    }

    // Periodic signal strength update (always, even during calls)
    if (!_paused && millis() - lastCSQPoll > CSQ_POLL_INTERVAL) {
      // Only poll CSQ if not actively in a call (avoid interrupting audio)
      if (!isCallActive()) {
        pollCSQ();
      }
      lastCSQPoll = millis();
    }

    // Shorter delay during active call states for responsive URC handling
    if (isCallActive()) {
      vTaskDelay(pdMS_TO_TICKS(100));  // 100ms — responsive to URCs
    } else {
      vTaskDelay(pdMS_TO_TICKS(500));  // 500ms — normal idle
    }
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

  // Reset pulse
  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, LOW);
  vTaskDelay(pdMS_TO_TICKS(200));
  digitalWrite(MODEM_RST, HIGH);
  vTaskDelay(pdMS_TO_TICKS(500));
  MESH_DEBUG_PRINTLN("[Modem] reset pulse done (GPIO %d)", MODEM_RST);

  // PWRKEY toggle
  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  vTaskDelay(pdMS_TO_TICKS(100));
  digitalWrite(MODEM_PWRKEY, LOW);
  vTaskDelay(pdMS_TO_TICKS(1500));
  digitalWrite(MODEM_PWRKEY, HIGH);
  MESH_DEBUG_PRINTLN("[Modem] PWRKEY toggled, waiting for boot...");

  vTaskDelay(pdMS_TO_TICKS(5000));

  // Assert DTR LOW
  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);
  MESH_DEBUG_PRINTLN("[Modem] DTR asserted LOW (GPIO %d)", MODEM_DTR);

  // Configure UART
  MODEM_SERIAL.begin(MODEM_BAUD, SERIAL_8N1, MODEM_TX, MODEM_RX);
  vTaskDelay(pdMS_TO_TICKS(500));
  MESH_DEBUG_PRINTLN("[Modem] UART started (ESP32 RX=%d TX=%d @ %d)", MODEM_TX, MODEM_RX, MODEM_BAUD);

  // Drain any boot garbage from UART
  while (MODEM_SERIAL.available()) MODEM_SERIAL.read();

  // Test communication
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
  // Before flushing, drain any pending URCs so we don't lose them
  drainURCs();

  Serial.printf("[Modem] TX: %s\n", cmd);
  MODEM_SERIAL.println(cmd);
  bool ok = waitResponse(expect, timeout_ms, _atBuf, AT_BUF_SIZE);
  if (_atBuf[0]) {
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
      // Also check for call-related URCs embedded in AT responses
      // (e.g. NO CARRIER can arrive during an AT+CLCC response)
      if (buf && strstr(buf, "NO CARRIER")) {
        processURCLine("NO CARRIER");
      }
      if (buf && strstr(buf, "BUSY")) {
        // Only process if we're in a call-related state
        if (_state == ModemState::DIALING) {
          processURCLine("BUSY");
        }
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
    char phone[SMS_PHONE_LEN];

    // Parse header line
    char* lineEnd = strchr(p, '\n');
    if (!lineEnd) break;

    // Extract index
    if (sscanf(p, "+CMGL: %d", &idx) != 1) { p = lineEnd + 1; continue; }

    // Extract phone number
    char* q1 = strchr(p + 7, '"');
    if (!q1) { p = lineEnd + 1; continue; }
    q1++;
    char* q2 = strchr(q1, '"');
    if (!q2) { p = lineEnd + 1; continue; }
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
    incoming.timestamp = (uint32_t)time(nullptr);

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