#ifdef HAS_4G_MODEM

#include "CellularMQTT.h"
#include <Mesh.h>
#include <SD.h>
#include <esp_mac.h>
#include <time.h> 
#include <sys/time.h>
#include <Update.h>

CellularMQTT cellularMQTT;

#define MODEM_SERIAL Serial1
#define MODEM_BAUD   115200

#define CONFIG_FILE  "/remote/mqtt.cfg"

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void CellularMQTT::begin() {
  Serial.println("[Cell] begin()");

  _state = CellState::OFF;
  _csq = 99;
  _reconnectDelay = MQTT_RECONNECT_MIN;
  _pubFailCount = 0;

  if (!loadConfig(_config)) {
    Serial.println("[Cell] ERROR: No /remote/mqtt.cfg — cannot start MQTT");
    _state = CellState::ERROR;
    return;
  }
  Serial.printf("[Cell] Config: broker=%s:%d user=%s id=%s\n",
                _config.broker, _config.port, _config.username, _config.deviceId);

  snprintf(_topicCmd,   sizeof(_topicCmd),   "meck/%s/cmd",       _config.deviceId);
  snprintf(_topicRsp,   sizeof(_topicRsp),   "meck/%s/rsp",       _config.deviceId);
  snprintf(_topicTelem, sizeof(_topicTelem), "meck/%s/telemetry",  _config.deviceId);
  snprintf(_topicOta,   sizeof(_topicOta),   "meck/%s/ota",        _config.deviceId);

  _cmdQueue = xQueueCreate(CMD_QUEUE_SIZE, sizeof(MQTTCommand));
  _rspQueue = xQueueCreate(RSP_QUEUE_SIZE, sizeof(MQTTResponse));
  _uartMutex = xSemaphoreCreateMutex();
  _telemetryMutex = xSemaphoreCreateMutex();

  xTaskCreatePinnedToCore(taskEntry, "cell", CELL_TASK_STACK_SIZE,
                          this, CELL_TASK_PRIORITY, &_taskHandle, CELL_TASK_CORE);
}

void CellularMQTT::stop() {
  if (!_taskHandle) return;
  mqttDisconnect();
  vTaskDelete(_taskHandle);
  _taskHandle = nullptr;
  _state = CellState::OFF;
}

bool CellularMQTT::recvCommand(MQTTCommand& out) {
  if (!_cmdQueue) return false;
  return xQueueReceive(_cmdQueue, &out, 0) == pdTRUE;
}

bool CellularMQTT::sendResponse(const char* topic, const char* payload) {
  if (!_rspQueue) return false;
  MQTTResponse rsp;
  memset(&rsp, 0, sizeof(rsp));
  strncpy(rsp.topic, topic, MQTT_TOPIC_MAX - 1);
  strncpy(rsp.payload, payload, MQTT_PAYLOAD_MAX - 1);
  return xQueueSend(_rspQueue, &rsp, 0) == pdTRUE;
}

void CellularMQTT::updateTelemetry(const TelemetryData& data) {
  if (xSemaphoreTake(_telemetryMutex, pdMS_TO_TICKS(50))) {
    memcpy(&_telemetry, &data, sizeof(data));
    xSemaphoreGive(_telemetryMutex);
  }
}

int CellularMQTT::getSignalBars() const {
  if (_csq == 99 || _csq == 0) return 0;
  if (_csq <= 5)  return 1;
  if (_csq <= 10) return 2;
  if (_csq <= 15) return 3;
  if (_csq <= 20) return 4;
  return 5;
}

const char* CellularMQTT::stateString() const {
  switch (_state) {
    case CellState::OFF:             return "OFF";
    case CellState::POWERING_ON:     return "PWR ON";
    case CellState::INITIALIZING:    return "INIT";
    case CellState::REGISTERING:     return "REG";
    case CellState::DATA_ACTIVATING: return "DATA";
    case CellState::MQTT_STARTING:   return "MQTT INIT";
    case CellState::MQTT_CONNECTING: return "MQTT CONN";
    case CellState::CONNECTED:       return "CONNECTED";
    case CellState::RECONNECTING:    return "RECONN";
    case CellState::OTA_IN_PROGRESS: return "OTA";
    case CellState::ERROR:           return "ERROR";
    default:                         return "???";
  }
}

void CellularMQTT::requestOTA(const char* url) {
  if (_state == CellState::OTA_IN_PROGRESS) {
    Serial.println("[OTA] Already in progress");
    return;
  }
  strncpy(_otaUrl, url, sizeof(_otaUrl) - 1);
  _otaUrl[sizeof(_otaUrl) - 1] = '\0';
  _otaPending = true;
  Serial.printf("[OTA] Requested: %s\n", url);
}

// ---------------------------------------------------------------------------
// Config file: /remote/mqtt.cfg
// Format (one value per line):
//   broker.hivemq.cloud
//   8883
//   myusername
//   mypassword
//   mydeviceid          (optional — auto-generated from MAC if omitted)
// ---------------------------------------------------------------------------

bool CellularMQTT::loadConfig(MQTTConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));

  File f = SD.open(CONFIG_FILE, FILE_READ);
  if (!f) return false;

  String line;

  line = f.readStringUntil('\n'); line.trim();
  if (line.length() == 0) { f.close(); return false; }
  strncpy(cfg.broker, line.c_str(), sizeof(cfg.broker) - 1);

  line = f.readStringUntil('\n'); line.trim();
  cfg.port = line.length() > 0 ? line.toInt() : 8883;

  line = f.readStringUntil('\n'); line.trim();
  strncpy(cfg.username, line.c_str(), sizeof(cfg.username) - 1);

  line = f.readStringUntil('\n'); line.trim();
  strncpy(cfg.password, line.c_str(), sizeof(cfg.password) - 1);

  // Optional device ID
  if (f.available()) {
    line = f.readStringUntil('\n'); line.trim();
    if (line.length() > 0) {
      strncpy(cfg.deviceId, line.c_str(), sizeof(cfg.deviceId) - 1);
    }
  }

  f.close();

  // Auto-generate device ID from ESP32 MAC if not provided
  if (cfg.deviceId[0] == '\0') {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(cfg.deviceId, sizeof(cfg.deviceId), "meck-%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
  }

  return cfg.broker[0] != '\0';
}

// ---------------------------------------------------------------------------
// Modem power-on (same sequence as ModemManager)
// ---------------------------------------------------------------------------

bool CellularMQTT::modemPowerOn() {
  Serial.println("[Cell] Powering on modem...");

  pinMode(MODEM_POWER_EN, OUTPUT);
  digitalWrite(MODEM_POWER_EN, HIGH);
  vTaskDelay(pdMS_TO_TICKS(500));

  pinMode(MODEM_RST, OUTPUT);
  digitalWrite(MODEM_RST, LOW);
  vTaskDelay(pdMS_TO_TICKS(200));
  digitalWrite(MODEM_RST, HIGH);
  vTaskDelay(pdMS_TO_TICKS(500));

  pinMode(MODEM_PWRKEY, OUTPUT);
  digitalWrite(MODEM_PWRKEY, HIGH);
  vTaskDelay(pdMS_TO_TICKS(100));
  digitalWrite(MODEM_PWRKEY, LOW);
  vTaskDelay(pdMS_TO_TICKS(1500));
  digitalWrite(MODEM_PWRKEY, HIGH);

  vTaskDelay(pdMS_TO_TICKS(5000));

  pinMode(MODEM_DTR, OUTPUT);
  digitalWrite(MODEM_DTR, LOW);

  MODEM_SERIAL.begin(MODEM_BAUD, SERIAL_8N1, MODEM_TX, MODEM_RX);
  vTaskDelay(pdMS_TO_TICKS(500));

  while (MODEM_SERIAL.available()) MODEM_SERIAL.read();

  for (int i = 0; i < 10; i++) {
    if (sendAT("AT", "OK", 1500)) {
      Serial.println("[Cell] AT responded OK");
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  Serial.println("[Cell] No AT response after power-on");
  return false;
}

// ---------------------------------------------------------------------------
// AT command helpers
// ---------------------------------------------------------------------------

bool CellularMQTT::sendAT(const char* cmd, const char* expect, uint32_t timeout_ms) {
  drainURCs();
  Serial.printf("[Cell] TX: %s\n", cmd);
  MODEM_SERIAL.println(cmd);
  bool ok = waitResponse(expect, timeout_ms, _atBuf, AT_BUF_SIZE);
  int len = strlen(_atBuf);
  while (len > 0 && (_atBuf[len-1] == '\r' || _atBuf[len-1] == '\n')) _atBuf[--len] = '\0';
  if (_atBuf[0]) {
    Serial.printf("[Cell] RX: %.120s [%s]\n", _atBuf, ok ? "OK" : "FAIL");
  }
  return ok;
}

bool CellularMQTT::waitResponse(const char* expect, uint32_t timeout_ms,
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
      if (buf && expect && strstr(buf, expect)) return true;
      if (buf && strstr(buf, "ERROR")) return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  if (buf && expect && strstr(buf, expect)) return true;
  return false;
}

bool CellularMQTT::waitPrompt(uint32_t timeout_ms) {
  unsigned long start = millis();
  while (millis() - start < timeout_ms) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (c == '>') return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  return false;
}

// ---------------------------------------------------------------------------
// URC handling
// ---------------------------------------------------------------------------

void CellularMQTT::drainURCs() {
  while (MODEM_SERIAL.available()) {
    char c = MODEM_SERIAL.read();

    if (_rxState == RX_WAIT_TOPIC) {
      if (_urcPos < _rxTopicLen && _urcPos < MQTT_TOPIC_MAX - 1) {
        _rxTopic[_urcPos] = c;
      }
      _urcPos++;
      if (_urcPos >= _rxTopicLen) {
        _rxTopic[min(_rxTopicLen, MQTT_TOPIC_MAX - 1)] = '\0';
        handleMqttRxTopic(_rxTopic, _rxTopicLen);
        _urcPos = 0;
        _rxState = RX_IDLE;
      }
      continue;
    }

    if (_rxState == RX_WAIT_PAYLOAD) {
      if (_urcPos < _rxPayloadLen && _urcPos < MQTT_PAYLOAD_MAX - 1) {
        _rxPayload[_urcPos] = c;
      }
      _urcPos++;
      if (_urcPos >= _rxPayloadLen) {
        _rxPayload[min(_rxPayloadLen, MQTT_PAYLOAD_MAX - 1)] = '\0';
        handleMqttRxPayload(_rxPayload, _rxPayloadLen);
        _urcPos = 0;
        _rxState = RX_IDLE;
      }
      continue;
    }

    if (c == '\n') {
      if (_urcPos > 0) {
        while (_urcPos > 0 && _urcBuf[_urcPos - 1] == '\r') _urcPos--;
        _urcBuf[_urcPos] = '\0';
        if (_urcPos > 0) processURCLine(_urcBuf);
      }
      _urcPos = 0;
    } else if (c != '\r' || _urcPos > 0) {
      if (_urcPos < URC_BUF_SIZE - 1) _urcBuf[_urcPos++] = c;
    }
  }
}

void CellularMQTT::processURCLine(const char* line) {
  if (strncmp(line, "+CMQTTRXSTART:", 14) == 0) {
    handleMqttRxStart(line);
    return;
  }
  if (strncmp(line, "+CMQTTRXTOPIC:", 14) == 0) {
    int client, tlen;
    if (sscanf(line, "+CMQTTRXTOPIC: %d,%d", &client, &tlen) == 2) {
      _rxTopicLen = tlen;
      _urcPos = 0;
      _rxState = RX_WAIT_TOPIC;
    }
    return;
  }
  if (strncmp(line, "+CMQTTRXPAYLOAD:", 16) == 0) {
    int client, plen;
    if (sscanf(line, "+CMQTTRXPAYLOAD: %d,%d", &client, &plen) == 2) {
      _rxPayloadLen = plen;
      _urcPos = 0;
      _rxState = RX_WAIT_PAYLOAD;
    }
    return;
  }
  if (strncmp(line, "+CMQTTRXEND:", 12) == 0) {
    handleMqttRxEnd();
    return;
  }
  if (strncmp(line, "+CMQTTCONNLOST:", 15) == 0) {
    handleMqttConnLost(line);
    return;
  }
}

// ---------------------------------------------------------------------------
// MQTT receive handlers
// ---------------------------------------------------------------------------

void CellularMQTT::handleMqttRxStart(const char* line) {
  int client, tlen, plen;
  if (sscanf(line, "+CMQTTRXSTART: %d,%d,%d", &client, &tlen, &plen) == 3) {
    _rxTopicLen = tlen;
    _rxPayloadLen = plen;
    _rxTopic[0] = '\0';
    _rxPayload[0] = '\0';
    Serial.printf("[Cell] MQTT RX start: topic_len=%d payload_len=%d\n", tlen, plen);
  }
}

void CellularMQTT::handleMqttRxTopic(const char* data, int len) {
  Serial.printf("[Cell] MQTT RX topic: %s\n", data);
}

void CellularMQTT::handleMqttRxPayload(const char* data, int len) {
  Serial.printf("[Cell] MQTT RX payload: %.80s\n", data);

  if (strstr(_rxTopic, "/cmd")) {
    MQTTCommand cmd;
    memset(&cmd, 0, sizeof(cmd));
    strncpy(cmd.cmd, data, MQTT_PAYLOAD_MAX - 1);
    if (xQueueSend(_cmdQueue, &cmd, 0) == pdTRUE) {
      _lastCmdTime = millis();
      Serial.printf("[Cell] Queued CLI command: %s\n", cmd.cmd);
    } else {
      Serial.println("[Cell] Command queue full, dropping");
    }
  } else if (strstr(_rxTopic, "/ota")) {
    requestOTA(data);
  }
}

void CellularMQTT::handleMqttRxEnd() {
  Serial.println("[Cell] MQTT RX end");
}

void CellularMQTT::handleMqttConnLost(const char* line) {
  int client, cause;
  sscanf(line, "+CMQTTCONNLOST: %d,%d", &client, &cause);
  Serial.printf("[Cell] MQTT connection lost (cause=%d)\n", cause);
  _state = CellState::RECONNECTING;
}

// ---------------------------------------------------------------------------
// APN resolution (reuses Meck's ApnDatabase)
// ---------------------------------------------------------------------------

void CellularMQTT::resolveAPN() {
  // 1. Check SD config
  File f = SD.open("/remote/apn.cfg", FILE_READ);
  if (f) {
    String line = f.readStringUntil('\n');
    f.close();
    line.trim();
    if (line.length() > 0) {
      strncpy(_apn, line.c_str(), sizeof(_apn) - 1);
      Serial.printf("[Cell] APN from config: %s\n", _apn);
      char cmd[80];
      snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", _apn);
      sendAT(cmd, "OK", 3000);
      return;
    }
  }

  // 2. Check modem's current APN
  if (sendAT("AT+CGDCONT?", "OK", 3000)) {
    char* p = strstr(_atBuf, "+CGDCONT:");
    if (p) {
      char* q1 = strchr(p, '"');
      if (q1) q1 = strchr(q1 + 1, '"');
      if (q1) q1 = strchr(q1 + 1, '"');
      if (q1) {
        q1++;
        char* q2 = strchr(q1, '"');
        if (q2 && q2 > q1) {
          int len = q2 - q1;
          if (len > 0 && len < (int)sizeof(_apn)) {
            memcpy(_apn, q1, len);
            _apn[len] = '\0';
            Serial.printf("[Cell] APN from network: %s\n", _apn);
            return;
          }
        }
      }
    }
  }

  // 3. Auto-detect from IMSI
  if (_imsi[0]) {
    const ApnEntry* entry = apnLookupFromIMSI(_imsi);
    if (entry) {
      strncpy(_apn, entry->apn, sizeof(_apn) - 1);
      Serial.printf("[Cell] APN auto-detected: %s (%s)\n", _apn, entry->carrier);
      char cmd[80];
      snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", _apn);
      sendAT(cmd, "OK", 3000);
      return;
    }
  }

  _apn[0] = '\0';
  Serial.println("[Cell] APN: none detected");
}

// ---------------------------------------------------------------------------
// Data connection — activate PDP context
// ---------------------------------------------------------------------------

bool CellularMQTT::activateData() {
  Serial.println("[Cell] Activating data connection...");

  if (!sendAT("AT+CGACT=1,1", "OK", 15000)) {
    Serial.println("[Cell] PDP activation failed, trying CGATT first...");
    sendAT("AT+CGATT=1", "OK", 30000);
    if (!sendAT("AT+CGACT=1,1", "OK", 15000)) {
      Serial.println("[Cell] PDP activation failed");
      return false;
    }
  }

  // Query IP address
  if (sendAT("AT+CGPADDR=1", "OK", 5000)) {
    char* p = strstr(_atBuf, "+CGPADDR:");
    if (p) {
      char* q = strchr(p, '"');
      if (q) {
        q++;
        char* e = strchr(q, '"');
        if (e && e > q) {
          int len = e - q;
          if (len < (int)sizeof(_ipAddr)) {
            memcpy(_ipAddr, q, len);
            _ipAddr[len] = '\0';
          }
        }
      }
    }
  }

  Serial.printf("[Cell] Data active, IP: %s\n", _ipAddr[0] ? _ipAddr : "unknown");
  return true;
}

// ---------------------------------------------------------------------------
// MQTT operations via AT commands
// ---------------------------------------------------------------------------

bool CellularMQTT::mqttStart() {
  if (!sendAT("AT+CMQTTSTART", "OK", 5000)) {
    if (!strstr(_atBuf, "+CMQTTSTART: 0")) {
      Serial.println("[Cell] MQTT start failed");
      return false;
    }
  }

  // Acquire client with SSL enabled (third param = 1 for SSL)
  char cmd[120];
  snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",1", _config.deviceId);
  if (!sendAT(cmd, "OK", 5000)) {
    Serial.println("[Cell] MQTT client acquire failed");
    return false;
  }

  // Configure TLS 1.2 (sslversion 4 = TLS 1.2)
  sendAT("AT+CSSLCFG=\"sslversion\",0,4", "OK", 3000);

  // Skip certificate verification (no CA cert loaded on device)
  sendAT("AT+CSSLCFG=\"authmode\",0,0", "OK", 3000);

  // Enable SNI — required for HiveMQ Cloud (shared IP, multiple clusters)
  sendAT("AT+CSSLCFG=\"enableSNI\",0,1", "OK", 3000);

  // Bind SSL config to MQTT session
  sendAT("AT+CMQTTSSLCFG=0,0", "OK", 3000);

  return true;
}

bool CellularMQTT::mqttConnect() {
  char cmd[256];
  snprintf(cmd, sizeof(cmd),
             "AT+CMQTTCONNECT=0,\"tcp://%s:%d\",120,1,\"%s\",\"%s\"",
           _config.broker, _config.port,
           _config.username, _config.password);

  Serial.printf("[Cell] TX: AT+CMQTTCONNECT=0,\"ssl://%s:%d\",...\n",
                _config.broker, _config.port);
                Serial.printf("[Cell] Full cmd (%d chars): %s\n", strlen(cmd), cmd);
  MODEM_SERIAL.println(cmd);

  // Wait for +CMQTTCONNECT URC (any result code, not just success)
  // Don't use waitResponse — it bails on "ERROR" before we see the code
  unsigned long start = millis();
  int pos = 0;
  _atBuf[0] = '\0';

  while (millis() - start < 30000) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (pos < AT_BUF_SIZE - 1) {
        _atBuf[pos++] = c;
        _atBuf[pos] = '\0';
      }
      // Check for the URC regardless of what else is in the buffer
      char* p = strstr(_atBuf, "+CMQTTCONNECT:");
      if (p) {
        // Give it a moment to complete the line
        vTaskDelay(pdMS_TO_TICKS(100));
        while (MODEM_SERIAL.available() && pos < AT_BUF_SIZE - 1) {
          _atBuf[pos++] = MODEM_SERIAL.read();
          _atBuf[pos] = '\0';
        }

        int client, result;
        if (sscanf(p, "+CMQTTCONNECT: %d,%d", &client, &result) == 2) {
          Serial.printf("[Cell] MQTT connect result: %d\n", result);
          if (result == 0) {
            Serial.println("[Cell] MQTT connected!");
            return true;
          }
        }
        Serial.printf("[Cell] MQTT connect failed (code from URC): %.80s\n", p);
        return false;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(50));
  }

  // Timeout — dump what we got
  int len = strlen(_atBuf);
  while (len > 0 && (_atBuf[len-1] == '\r' || _atBuf[len-1] == '\n')) _atBuf[--len] = '\0';
  Serial.printf("[Cell] MQTT connect timeout. Buffer: %.200s\n", _atBuf);
  return false;
}

bool CellularMQTT::mqttSubscribe(const char* topic) {
  int tlen = strlen(topic);
  char cmd[80];
  snprintf(cmd, sizeof(cmd), "AT+CMQTTSUB=0,%d,1", tlen);
  MODEM_SERIAL.println(cmd);

  if (!waitPrompt(5000)) {
    Serial.println("[Cell] No prompt for CMQTTSUB");
    return false;
  }

  MODEM_SERIAL.write((const uint8_t*)topic, tlen);
  return waitResponse("OK", 10000, _atBuf, AT_BUF_SIZE);
}

bool CellularMQTT::mqttPublish(const char* topic, const char* payload) {
  int tlen = strlen(topic);
  int plen = strlen(payload);

  // Step 1: Set topic
  char cmd[80];
  snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%d", tlen);
  MODEM_SERIAL.println(cmd);
  if (!waitPrompt(5000)) {
    _pubFailCount++;
    Serial.println("[Cell] No prompt for CMQTTTOPIC");
    return false;
  }
  MODEM_SERIAL.write((const uint8_t*)topic, tlen);
  if (!waitResponse("OK", 5000, _atBuf, AT_BUF_SIZE)) {
    _pubFailCount++;
    return false;
  }

  // Step 2: Set payload
  snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%d", plen);
  MODEM_SERIAL.println(cmd);
  if (!waitPrompt(5000)) {
    _pubFailCount++;
    return false;
  }
  MODEM_SERIAL.write((const uint8_t*)payload, plen);
  if (!waitResponse("OK", 5000, _atBuf, AT_BUF_SIZE)) {
    _pubFailCount++;
    return false;
  }

  // Step 3: Publish QoS 1, 60s timeout
  if (!sendAT("AT+CMQTTPUB=0,1,60", "OK", 15000)) {
    _pubFailCount++;
    Serial.printf("[Cell] Publish failed (%d consecutive)\n", _pubFailCount);
    return false;
  }

  // Success — reset failure counter
  _pubFailCount = 0;
  return true;
}

void CellularMQTT::mqttDisconnect() {
  sendAT("AT+CMQTTDISC=0,60", "OK", 5000);
  sendAT("AT+CMQTTREL=0", "OK", 3000);
  sendAT("AT+CMQTTSTOP", "OK", 5000);
}

// ---------------------------------------------------------------------------
// OTA — HTTP download via A7682E + ESP32 flash
// ---------------------------------------------------------------------------

int CellularMQTT::readRawBytes(uint8_t* dest, int count, uint32_t timeout_ms) {
  unsigned long start = millis();
  int received = 0;
  while (received < count && millis() - start < timeout_ms) {
    while (MODEM_SERIAL.available() && received < count) {
      dest[received++] = MODEM_SERIAL.read();
    }
    if (received < count) vTaskDelay(pdMS_TO_TICKS(5));
  }
  return received;
}

int CellularMQTT::httpGet(const char* url) {
  sendAT("AT+HTTPTERM", "OK", 2000);
  vTaskDelay(pdMS_TO_TICKS(500));

  if (!sendAT("AT+HTTPINIT", "OK", 5000)) {
    Serial.println("[OTA] HTTPINIT failed");
    return -1;
  }

  int urlLen = strlen(url);
  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+HTTPPARA=\"URL\",%d", urlLen);
  MODEM_SERIAL.println(cmd);
  if (!waitPrompt(5000)) {
    Serial.println("[OTA] No prompt for HTTPPARA URL");
    httpTerm();
    return -1;
  }
  MODEM_SERIAL.write((const uint8_t*)url, urlLen);
  if (!waitResponse("OK", 10000, _atBuf, AT_BUF_SIZE)) {
    Serial.println("[OTA] HTTPPARA URL failed");
    httpTerm();
    return -1;
  }

  if (strncmp(url, "https://", 8) == 0) {
    sendAT("AT+HTTPPARA=\"SSLCFG\",0", "OK", 3000);
  }

  sendAT("AT+HTTPPARA=\"REDIR\",1", "OK", 2000);

  MODEM_SERIAL.println("AT+HTTPACTION=0");

  unsigned long start = millis();
  int pos = 0;
  _atBuf[0] = '\0';

  while (millis() - start < 180000) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (pos < AT_BUF_SIZE - 1) {
        _atBuf[pos++] = c;
        _atBuf[pos] = '\0';
      }
      char* p = strstr(_atBuf, "+HTTPACTION:");
      if (p) {
        vTaskDelay(pdMS_TO_TICKS(100));
        while (MODEM_SERIAL.available() && pos < AT_BUF_SIZE - 1) {
          _atBuf[pos++] = MODEM_SERIAL.read();
          _atBuf[pos] = '\0';
        }

        int method, status, contentLen;
        if (sscanf(p, "+HTTPACTION: %d,%d,%d", &method, &status, &contentLen) == 3) {
          Serial.printf("[OTA] HTTP status=%d content_length=%d\n", status, contentLen);
          if (status == 200 && contentLen > 0) {
            return contentLen;
          }
          Serial.printf("[OTA] HTTP download failed (status %d)\n", status);
          httpTerm();
          return -1;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  Serial.println("[OTA] HTTP download timeout");
  httpTerm();
  return -1;
}

bool CellularMQTT::httpReadChunk(int offset, int len, uint8_t* dest, int* bytesRead) {
  *bytesRead = 0;

  char cmd[40];
  snprintf(cmd, sizeof(cmd), "AT+HTTPREAD=%d,%d", offset, len);
  MODEM_SERIAL.println(cmd);

  unsigned long start = millis();
  int pos = 0;
  _atBuf[0] = '\0';

  while (millis() - start < 10000) {
    while (MODEM_SERIAL.available()) {
      char c = MODEM_SERIAL.read();
      if (pos < AT_BUF_SIZE - 1) {
        _atBuf[pos++] = c;
        _atBuf[pos] = '\0';
      }

      char* p = strstr(_atBuf, "+HTTPREAD:");
      if (p) {
        char* nl = strchr(p, '\n');
        if (nl) {
          int actualLen = 0;
          sscanf(p, "+HTTPREAD: %d", &actualLen);
          if (actualLen <= 0 || actualLen > len) {
            Serial.printf("[OTA] Bad HTTPREAD len: %d\n", actualLen);
            return false;
          }

          int got = readRawBytes(dest, actualLen, 15000);
          if (got != actualLen) {
            Serial.printf("[OTA] Short read: got %d expected %d\n", got, actualLen);
            return false;
          }

          *bytesRead = actualLen;
          waitResponse("OK", 3000, _atBuf, AT_BUF_SIZE);
          return true;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  Serial.println("[OTA] HTTPREAD timeout");
  return false;
}

void CellularMQTT::httpTerm() {
  sendAT("AT+HTTPTERM", "OK", 3000);
}

void CellularMQTT::performOTA() {
  _otaPending = false;
  _state = CellState::OTA_IN_PROGRESS;

  Serial.printf("[OTA] URL: %s\n", _otaUrl);

  // Disconnect MQTT — modem can only do one thing at a time
  mqttDisconnect();
  vTaskDelay(pdMS_TO_TICKS(1000));

  int fileSize = httpGet(_otaUrl);
  if (fileSize <= 0) {
    Serial.println("[OTA] Download failed");
    httpTerm();
    _state = CellState::RECONNECTING;
    return;
  }

  Serial.printf("[OTA] Downloaded %d bytes, flashing...\n", fileSize);

  if (!Update.begin(fileSize)) {
    Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
    httpTerm();
    _state = CellState::RECONNECTING;
    return;
  }

  uint8_t* chunk = (uint8_t*)malloc(OTA_CHUNK_SIZE);
  if (!chunk) {
    Serial.println("[OTA] malloc failed");
    Update.abort();
    httpTerm();
    _state = CellState::RECONNECTING;
    return;
  }

  int offset = 0;
  int lastPct = -1;

  while (offset < fileSize) {
    int remaining = fileSize - offset;
    int toRead = (remaining < OTA_CHUNK_SIZE) ? remaining : OTA_CHUNK_SIZE;

    int bytesRead = 0;
    if (!httpReadChunk(offset, toRead, chunk, &bytesRead) || bytesRead == 0) {
      Serial.printf("[OTA] Read failed at offset %d\n", offset);
      free(chunk);
      Update.abort();
      httpTerm();
      _state = CellState::RECONNECTING;
      return;
    }

    size_t written = Update.write(chunk, bytesRead);
    if (written != (size_t)bytesRead) {
      Serial.printf("[OTA] Write failed: wrote %d of %d\n", written, bytesRead);
      free(chunk);
      Update.abort();
      httpTerm();
      _state = CellState::RECONNECTING;
      return;
    }

    offset += bytesRead;

    int pct = (offset * 100) / fileSize;
    if (pct / 10 != lastPct / 10) {
      Serial.printf("[OTA] Flash progress: %d%% (%d/%d)\n", pct, offset, fileSize);
      lastPct = pct;
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }

  free(chunk);
  httpTerm();

  if (!Update.end(true)) {
    Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
    _state = CellState::RECONNECTING;
    return;
  }

  Serial.println("[OTA] SUCCESS — rebooting in 3 seconds");
  vTaskDelay(pdMS_TO_TICKS(3000));
  ESP.restart();
}

// ---------------------------------------------------------------------------
// FreeRTOS Task
// ---------------------------------------------------------------------------

void CellularMQTT::taskEntry(void* param) {
  static_cast<CellularMQTT*>(param)->taskLoop();
}

void CellularMQTT::taskLoop() {
  Serial.printf("[Cell] Task started on core %d\n", xPortGetCoreID());

restart:
  _pubFailCount = 0;

  // ---- Phase 1: Power on ----
  _state = CellState::POWERING_ON;
  if (!modemPowerOn()) {
    Serial.println("[Cell] Power-on failed, retry in 30s");
    _state = CellState::ERROR;
    vTaskDelay(pdMS_TO_TICKS(30000));
    goto restart;
  }

  // ---- Phase 2: Initialize ----
  _state = CellState::INITIALIZING;
  sendAT("ATE0", "OK");

  if (sendAT("AT+GSN", "OK", 3000)) {
    char* p = _atBuf;
    while (*p && !isdigit(*p)) p++;
    int i = 0;
    while (isdigit(p[i]) && i < 19) { _imei[i] = p[i]; i++; }
    _imei[i] = '\0';
    Serial.printf("[Cell] IMEI: %s\n", _imei);
  }

  if (sendAT("AT+CIMI", "OK", 3000)) {
    char* p = _atBuf;
    while (*p && !isdigit(*p)) p++;
    int i = 0;
    while (isdigit(p[i]) && i < 19) { _imsi[i] = p[i]; i++; }
    _imsi[i] = '\0';
    Serial.printf("[Cell] IMSI: %s\n", _imsi);
  }

  sendAT("AT+CTZU=1", "OK");

  // ---- Phase 3: Network registration ----
  _state = CellState::REGISTERING;
  Serial.println("[Cell] Waiting for network...");

  {
    bool registered = false;
    for (int i = 0; i < 60; i++) {
      if (sendAT("AT+CREG?", "OK", 2000)) {
        char* p = strstr(_atBuf, "+CREG:");
        if (p) {
          int n, stat;
          if (sscanf(p, "+CREG: %d,%d", &n, &stat) == 2) {
            if (stat == 1 || stat == 5) { registered = true; break; }
          }
        }
      }
      vTaskDelay(pdMS_TO_TICKS(1000));
    }
    if (!registered) Serial.println("[Cell] Registration timeout — continuing");
  }

  // Operator name
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
      }
    }
  }

  resolveAPN();

  if (sendAT("AT+CSQ", "OK", 2000)) {
    char* p = strstr(_atBuf, "+CSQ:");
    if (p) { int csq, ber; if (sscanf(p, "+CSQ: %d,%d", &csq, &ber) >= 1) _csq = csq; }
  }

  Serial.printf("[Cell] Registered: oper=%s CSQ=%d APN=%s IMEI=%s\n",
                _operator, _csq, _apn[0] ? _apn : "(none)", _imei);

// Sync ESP32 system clock from modem network time
  for (int attempt = 0; attempt < 5; attempt++) {
    if (attempt > 0) vTaskDelay(pdMS_TO_TICKS(2000));
    if (sendAT("AT+CCLK?", "OK", 3000)) {
      char* p = strstr(_atBuf, "+CCLK:");
      if (p) {
        int yy=0, mo=0, dd=0, hh=0, mm=0, ss=0, tz=0;
        if (sscanf(p, "+CCLK: \"%d/%d/%d,%d:%d:%d", &yy, &mo, &dd, &hh, &mm, &ss) >= 6) {
          if (yy < 24 || yy > 50) continue;  // Not synced yet
          char* tzp = p + 7;
          while (*tzp && *tzp != '+' && *tzp != '-') tzp++;
          if (*tzp) tz = atoi(tzp);
          struct tm t = {};
          t.tm_year = yy + 100;
          t.tm_mon = mo - 1;
          t.tm_mday = dd;
          t.tm_hour = hh;
          t.tm_min = mm;
          t.tm_sec = ss;
          time_t epoch = mktime(&t);
          epoch -= (tz * 15 * 60);
          struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
          settimeofday(&tv, nullptr);
          Serial.printf("[Cell] Clock synced: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
                        yy+2000, mo, dd, hh, mm, ss);
          break;
        }
      }
    }
  }

  // ---- Phase 4: Activate data ----
  _state = CellState::DATA_ACTIVATING;
  if (!activateData()) {
    Serial.println("[Cell] Data activation failed, retry in 30s");
    _state = CellState::ERROR;
    vTaskDelay(pdMS_TO_TICKS(30000));
    goto restart;
  }

  // ---- Phase 5: MQTT connect ----
  _state = CellState::MQTT_STARTING;
  if (!mqttStart()) {
    _state = CellState::ERROR;
    vTaskDelay(pdMS_TO_TICKS(30000));
    goto restart;
  }

  _state = CellState::MQTT_CONNECTING;
  if (!mqttConnect()) {
    mqttDisconnect();
    _state = CellState::ERROR;
    vTaskDelay(pdMS_TO_TICKS(30000));
    goto restart;
  }

// Allow MQTT session to stabilise before subscribing
  vTaskDelay(pdMS_TO_TICKS(2000));

  // Subscribe with retry — the modem sometimes misses the first prompt
  for (int i = 0; i < 3; i++) {
    if (mqttSubscribe(_topicCmd)) break;
    Serial.printf("[Cell] Subscribe retry %d for cmd topic\n", i + 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
  for (int i = 0; i < 3; i++) {
    if (mqttSubscribe(_topicOta)) break;
    Serial.printf("[Cell] Subscribe retry %d for ota topic\n", i + 1);
    vTaskDelay(pdMS_TO_TICKS(1000));
  }

  _state = CellState::CONNECTED;
  _reconnectDelay = MQTT_RECONNECT_MIN;
  Serial.println("[Cell] MQTT connected and subscribed — ready");

  mqttPublish(_topicTelem, "{\"event\":\"boot\",\"state\":\"connected\"}");

  // ---- Phase 6: Main loop ----
  unsigned long lastCSQ = 0;
  unsigned long lastTelem = 0;

  while (true) {
    // Check for pending OTA request
    if (_otaPending && _state == CellState::CONNECTED) {
      performOTA();
      continue;
    }

    drainURCs();

    // Health check: too many consecutive publish failures = silent disconnect
    if (_pubFailCount >= MQTT_PUB_FAIL_MAX && _state == CellState::CONNECTED) {
      Serial.printf("[Cell] %d consecutive publish failures — forcing reconnect\n", _pubFailCount);
      _state = CellState::RECONNECTING;
    }

    // Reconnect logic
    if (_state == CellState::RECONNECTING) {
      Serial.printf("[Cell] Reconnecting in %lums...\n", _reconnectDelay);
      vTaskDelay(pdMS_TO_TICKS(_reconnectDelay));
      _reconnectDelay = min(_reconnectDelay * 2, (uint32_t)MQTT_RECONNECT_MAX);

      mqttDisconnect();
      vTaskDelay(pdMS_TO_TICKS(2000));

      // Check data is still active
      if (!sendAT("AT+CGACT?", "OK", 5000) || !strstr(_atBuf, ",1")) {
        if (!activateData()) {
          vTaskDelay(pdMS_TO_TICKS(10000));
          goto restart;
        }
      }

      if (!mqttStart() || !mqttConnect()) {
        continue;  // Retry with backoff
      }

      mqttSubscribe(_topicCmd);
      mqttSubscribe(_topicOta);
      _state = CellState::CONNECTED;
      _reconnectDelay = MQTT_RECONNECT_MIN;
      _pubFailCount = 0;
      Serial.println("[Cell] Reconnected");
    }

    // Publish queued responses
    if (_state == CellState::CONNECTED) {
      MQTTResponse rsp;
      while (xQueueReceive(_rspQueue, &rsp, 0) == pdTRUE) {
        mqttPublish(rsp.topic, rsp.payload);
      }
    }

    // Periodic CSQ poll
    if (millis() - lastCSQ > 60000) {
      if (sendAT("AT+CSQ", "OK", 2000)) {
        char* p = strstr(_atBuf, "+CSQ:");
        if (p) { int csq, ber; if (sscanf(p, "+CSQ: %d,%d", &csq, &ber) >= 1) _csq = csq; }
      }
      lastCSQ = millis();
    }

    // Periodic telemetry publish
    if (_state == CellState::CONNECTED && millis() - lastTelem > TELEMETRY_INTERVAL) {
      TelemetryData td;
      if (xSemaphoreTake(_telemetryMutex, pdMS_TO_TICKS(50))) {
        memcpy(&td, &_telemetry, sizeof(td));
        xSemaphoreGive(_telemetryMutex);
      }

      char json[400];
      snprintf(json, sizeof(json),
               "{\"uptime\":%lu,\"batt_mv\":%d,\"batt_pct\":%d,\"temp\":%.1f,"
               "\"csq\":%d,\"bars\":%d,\"neighbors\":%d,"
               "\"freq\":%.3f,\"bw\":%.1f,\"sf\":%d,\"cr\":%d,\"tx\":%d,"
               "\"name\":\"%s\",\"ip\":\"%s\",\"oper\":\"%s\",\"apn\":\"%s\","
               "\"heap\":%d}",
               td.uptime_secs, td.battery_mv, td.battery_pct,
               td.temperature / 10.0f,
               _csq, getSignalBars(), td.neighbor_count,
               td.freq, td.bw, td.sf, td.cr, td.tx_power,
               td.node_name, _ipAddr, _operator, _apn,
               ESP.getFreeHeap());

      mqttPublish(_topicTelem, json);
      lastTelem = millis();
    }

    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

#endif // HAS_4G_MODEM