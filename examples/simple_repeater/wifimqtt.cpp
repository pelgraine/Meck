#ifdef MECK_WIFI_REMOTE

#include "target.h"
#include "WiFiMQTT.h"
#include <esp_mac.h>
#include <Update.h>
#include <HTTPClient.h>

WiFiMQTT wifiMQTT;

#define WIFI_CONFIG_FILE  "/remote/wifi.cfg"
#define MQTT_CONFIG_FILE  "/remote/mqtt.cfg"

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void WiFiMQTT::begin() {
  Serial.println("[WiFi] begin()");

  _state = WiFiMQTTState::OFF;
  _cmdHead = _cmdTail = 0;
  _rspHead = _rspTail = 0;
  _activeNetwork = 0;

  if (!loadConfig(_config)) {
    Serial.println("[WiFi] ERROR: Missing config files — cannot start");
    _state = WiFiMQTTState::ERROR;
    return;
  }

  Serial.printf("[WiFi] Config: %d network(s), broker=%s:%d id=%s\n",
                _config.networkCount, _config.broker, _config.port, _config.deviceId);
  for (int i = 0; i < _config.networkCount; i++) {
    Serial.printf("[WiFi]   %d: %s\n", i + 1, _config.networks[i].ssid);
  }

  snprintf(_topicCmd,   sizeof(_topicCmd),   "meck/%s/cmd",       _config.deviceId);
  snprintf(_topicRsp,   sizeof(_topicRsp),   "meck/%s/rsp",       _config.deviceId);
  snprintf(_topicTelem, sizeof(_topicTelem), "meck/%s/telemetry",  _config.deviceId);
  snprintf(_topicOta,   sizeof(_topicOta),   "meck/%s/ota",        _config.deviceId);

  // Configure TLS — skip server cert verification (same as cellular)
  _wifiClient.setInsecure();

  _mqttClient.setClient(_wifiClient);
  _mqttClient.setServer(_config.broker, _config.port);
  _mqttClient.setCallback(mqttCallback);
  _mqttClient.setBufferSize(MQTT_PAYLOAD_MAX + MQTT_TOPIC_MAX);

  _state = WiFiMQTTState::WIFI_CONNECTING;
}

void WiFiMQTT::loop() {
  if (_state == WiFiMQTTState::OFF || _state == WiFiMQTTState::ERROR) return;

  // Check for pending OTA
  if (_otaPending && _state == WiFiMQTTState::CONNECTED) {
    performOTA();
    return;
  }

  // WiFi connection management
  if (WiFi.status() != WL_CONNECTED) {
    if (_state == WiFiMQTTState::CONNECTED || _state == WiFiMQTTState::MQTT_CONNECTING) {
      Serial.println("[WiFi] Connection lost");
      _state = WiFiMQTTState::WIFI_CONNECTING;
    }
    if (millis() - _lastWifiAttempt > WIFI_RECONNECT_MS) {
      connectWiFi();
      _lastWifiAttempt = millis();
    }
    return;
  }

  // WiFi is up — check MQTT
  if (!_mqttClient.connected()) {
    if (_state == WiFiMQTTState::CONNECTED) {
      Serial.println("[WiFi] MQTT disconnected");
    }
    _state = WiFiMQTTState::MQTT_CONNECTING;
    if (millis() - _lastMqttAttempt > MQTT_RECONNECT_MS) {
      connectMQTT();
      _lastMqttAttempt = millis();
    }
    return;
  }

  // Connected — run MQTT loop
  _mqttClient.loop();

  // Publish queued responses
  publishQueuedResponses();

  // Periodic RSSI
  if (millis() - _lastRSSI > 30000) {
    _rssi = WiFi.RSSI();
    _lastRSSI = millis();
  }

  // Periodic telemetry
  if (millis() - _lastTelem > TELEMETRY_INTERVAL) {
    publishTelemetry();
    _lastTelem = millis();
  }
}

bool WiFiMQTT::recvCommand(MQTTCommand& out) {
  if (_cmdHead == _cmdTail) return false;
  memcpy(&out, &_cmdBuf[_cmdTail], sizeof(MQTTCommand));
  _cmdTail = (_cmdTail + 1) % CMD_QUEUE_SIZE;
  return true;
}

bool WiFiMQTT::sendResponse(const char* topic, const char* payload) {
  int next = (_rspHead + 1) % RSP_QUEUE_SIZE;
  if (next == _rspTail) return false;  // Full
  memset(&_rspBuf[_rspHead], 0, sizeof(MQTTResponse));
  strncpy(_rspBuf[_rspHead].topic, topic, MQTT_TOPIC_MAX - 1);
  strncpy(_rspBuf[_rspHead].payload, payload, MQTT_PAYLOAD_MAX - 1);
  _rspHead = next;
  return true;
}

void WiFiMQTT::updateTelemetry(const TelemetryData& data) {
  memcpy(&_telemetry, &data, sizeof(data));
}

void WiFiMQTT::requestOTA(const char* url) {
  if (_state == WiFiMQTTState::OTA_IN_PROGRESS) return;
  strncpy(_otaUrl, url, sizeof(_otaUrl) - 1);
  _otaUrl[sizeof(_otaUrl) - 1] = '\0';
  _otaPending = true;
  Serial.printf("[OTA] Requested: %s\n", url);
}

int WiFiMQTT::getSignalBars() const {
  if (_rssi == 0) return 0;
  if (_rssi > -50) return 5;
  if (_rssi > -60) return 4;
  if (_rssi > -70) return 3;
  if (_rssi > -80) return 2;
  return 1;
}

const char* WiFiMQTT::stateString() const {
  switch (_state) {
    case WiFiMQTTState::OFF:              return "OFF";
    case WiFiMQTTState::WIFI_CONNECTING:  return "WiFi...";
    case WiFiMQTTState::WIFI_CONNECTED:   return "WiFi OK";
    case WiFiMQTTState::MQTT_CONNECTING:  return "MQTT...";
    case WiFiMQTTState::CONNECTED:        return "CONNECTED";
    case WiFiMQTTState::OTA_IN_PROGRESS:  return "OTA";
    case WiFiMQTTState::ERROR:            return "ERROR";
    default:                              return "???";
  }
}

// ---------------------------------------------------------------------------
// Config files
//
// /remote/wifi.cfg — SSID/password pairs, two lines each:
//   HomeNetwork
//   HomePassword
//   BackupNetwork
//   BackupPassword
//
// /remote/mqtt.cfg — same format as cellular variant
// ---------------------------------------------------------------------------

bool WiFiMQTT::loadConfig(WiFiMQTTConfig& cfg) {
  memset(&cfg, 0, sizeof(cfg));

  // Determine filesystem: SD if available, otherwise SPIFFS
  // Heltec V4 and other headless boards have no SD slot — config lives in SPIFFS.
  // Upload config files via: pio run -t uploadfs (with data/ folder)
#if defined(HAS_SDCARD) || defined(SDCARD_CS)
  fs::FS& configFS = SD;
  Serial.println("[WiFi] Config source: SD card");
#else
  fs::FS& configFS = SPIFFS;
  Serial.println("[WiFi] Config source: SPIFFS");
#endif

  // WiFi config: read SSID/password pairs
  File wf = configFS.open(WIFI_CONFIG_FILE, FILE_READ);
  if (!wf) {
    Serial.printf("[WiFi] No %s\n", WIFI_CONFIG_FILE);
    return false;
  }

  cfg.networkCount = 0;
  while (wf.available() && cfg.networkCount < MAX_WIFI_NETWORKS) {
    String ssid = wf.readStringUntil('\n'); ssid.trim();
    if (ssid.length() == 0) break;
    String pass = wf.readStringUntil('\n'); pass.trim();
    strncpy(cfg.networks[cfg.networkCount].ssid, ssid.c_str(), sizeof(cfg.networks[0].ssid) - 1);
    strncpy(cfg.networks[cfg.networkCount].password, pass.c_str(), sizeof(cfg.networks[0].password) - 1);
    cfg.networkCount++;
  }
  wf.close();

  if (cfg.networkCount == 0) {
    Serial.println("[WiFi] No networks in wifi.cfg");
    return false;
  }

  // MQTT config: /remote/mqtt.cfg (same format as cellular)
  File mf = configFS.open(MQTT_CONFIG_FILE, FILE_READ);
  if (!mf) {
    Serial.printf("[WiFi] No %s\n", MQTT_CONFIG_FILE);
    return false;
  }
  String line;
  line = mf.readStringUntil('\n'); line.trim();
  strncpy(cfg.broker, line.c_str(), sizeof(cfg.broker) - 1);
  line = mf.readStringUntil('\n'); line.trim();
  cfg.port = line.length() > 0 ? line.toInt() : 8883;
  line = mf.readStringUntil('\n'); line.trim();
  strncpy(cfg.username, line.c_str(), sizeof(cfg.username) - 1);
  line = mf.readStringUntil('\n'); line.trim();
  strncpy(cfg.password, line.c_str(), sizeof(cfg.password) - 1);
  if (mf.available()) {
    line = mf.readStringUntil('\n'); line.trim();
    if (line.length() > 0) {
      strncpy(cfg.deviceId, line.c_str(), sizeof(cfg.deviceId) - 1);
    }
  }
  mf.close();

  // Auto-generate device ID if not provided
  if (cfg.deviceId[0] == '\0') {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    snprintf(cfg.deviceId, sizeof(cfg.deviceId), "meck-%02x%02x%02x%02x",
             mac[2], mac[3], mac[4], mac[5]);
  }

  return cfg.broker[0] != '\0';
}

// ---------------------------------------------------------------------------
// WiFi connection — tries each configured network in order
// ---------------------------------------------------------------------------

bool WiFiMQTT::connectWiFi() {
  WiFi.mode(WIFI_STA);

  for (int n = 0; n < _config.networkCount; n++) {
    Serial.printf("[WiFi] Trying %s (%d/%d)...\n",
                  _config.networks[n].ssid, n + 1, _config.networkCount);
    WiFi.begin(_config.networks[n].ssid, _config.networks[n].password);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      snprintf(_ipAddr, sizeof(_ipAddr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      _rssi = WiFi.RSSI();
      _activeNetwork = n;
      Serial.printf("[WiFi] Connected to %s — IP: %s RSSI: %d\n",
                    _config.networks[n].ssid, _ipAddr, _rssi);
        if (WiFi.status() == WL_CONNECTED) {
      IPAddress ip = WiFi.localIP();
      snprintf(_ipAddr, sizeof(_ipAddr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
      _rssi = WiFi.RSSI();
      _activeNetwork = n;
      Serial.printf("[WiFi] Connected to %s — IP: %s RSSI: %d\n",
                    _config.networks[n].ssid, _ipAddr, _rssi);

      // Sync clock via NTP
      configTime(0, 0, "pool.ntp.org", "time.google.com");
      Serial.print("[WiFi] NTP sync...");
      int tries = 0;
      while (time(nullptr) < 1700000000 && tries < 20) {
        delay(500);
        tries++;
      }
      time_t now = time(nullptr);
      if (now > 1700000000) {
        extern AutoDiscoverRTCClock rtc_clock;
        rtc_clock.setCurrentTime((uint32_t)now);
        Serial.printf(" OK (%lu)\n", (unsigned long)now);
      } else {
        Serial.println(" timeout");
      }

      _state = WiFiMQTTState::WIFI_CONNECTED;
      return true;
    }
    }

    WiFi.disconnect();
    delay(500);
  }

  Serial.println("[WiFi] All networks failed");
  return false;
}

// ---------------------------------------------------------------------------
// MQTT connection
// ---------------------------------------------------------------------------

bool WiFiMQTT::connectMQTT() {
  Serial.printf("[WiFi] MQTT connecting to %s:%d...\n", _config.broker, _config.port);

  char clientId[48];
  snprintf(clientId, sizeof(clientId), "%s-%lu", _config.deviceId, millis() & 0xFFFF);

  if (_mqttClient.connect(clientId, _config.username, _config.password)) {
    Serial.println("[WiFi] MQTT connected!");

    _mqttClient.subscribe(_topicCmd, 1);
    _mqttClient.subscribe(_topicOta, 1);

    _state = WiFiMQTTState::CONNECTED;

    // Publish boot event
    _mqttClient.publish(_topicTelem, "{\"event\":\"boot\",\"state\":\"connected\"}", true);
    return true;
  }

  Serial.printf("[WiFi] MQTT connect failed, rc=%d\n", _mqttClient.state());
  return false;
}

// ---------------------------------------------------------------------------
// MQTT message callback
// ---------------------------------------------------------------------------

void WiFiMQTT::mqttCallback(char* topic, byte* payload, unsigned int length) {
  wifiMQTT.onMessage(topic, payload, length);
}

void WiFiMQTT::onMessage(char* topic, byte* payload, unsigned int length) {
  char buf[MQTT_PAYLOAD_MAX];
  int len = (length < MQTT_PAYLOAD_MAX - 1) ? length : MQTT_PAYLOAD_MAX - 1;
  memcpy(buf, payload, len);
  buf[len] = '\0';

  Serial.printf("[WiFi] RX [%s]: %.80s\n", topic, buf);

  if (strstr(topic, "/cmd")) {
    int next = (_cmdHead + 1) % CMD_QUEUE_SIZE;
    if (next != _cmdTail) {
      memset(&_cmdBuf[_cmdHead], 0, sizeof(MQTTCommand));
      strncpy(_cmdBuf[_cmdHead].cmd, buf, MQTT_PAYLOAD_MAX - 1);
      _cmdHead = next;
      Serial.printf("[WiFi] Queued CLI: %s\n", buf);
    } else {
      Serial.println("[WiFi] Command queue full");
    }
  } else if (strstr(topic, "/ota")) {
    requestOTA(buf);
  }
}

// ---------------------------------------------------------------------------
// Publish helpers
// ---------------------------------------------------------------------------

void WiFiMQTT::publishQueuedResponses() {
  while (_rspHead != _rspTail) {
    _mqttClient.publish(_rspBuf[_rspTail].topic, _rspBuf[_rspTail].payload);
    _rspTail = (_rspTail + 1) % RSP_QUEUE_SIZE;
  }
}

void WiFiMQTT::publishTelemetry() {
  _rssi = WiFi.RSSI();

  static const char* loopLabels[] = { "off", "minimal", "moderate", "strict" };
  const char* loopStr = _telemetry.loop_detect <= 3 ? loopLabels[_telemetry.loop_detect] : "off";

  char json[512];
  snprintf(json, sizeof(json),
           "{\"uptime\":%lu,\"batt_mv\":%d,\"batt_pct\":%d,\"temp\":%.1f,"
           "\"rssi\":%d,\"bars\":%d,\"neighbors\":%d,"
           "\"freq\":%.3f,\"bw\":%.1f,\"sf\":%d,\"cr\":%d,\"tx\":%d,"
           "\"name\":\"%s\",\"ip\":\"%s\",\"ssid\":\"%s\","
           "\"heap\":%d,"
           "\"loop_detect\":\"%s\",\"path_hash_mode\":%d,\"flood_max\":%d}",
           _telemetry.uptime_secs, _telemetry.battery_mv, _telemetry.battery_pct,
           _telemetry.temperature / 10.0f,
           _rssi, getSignalBars(), _telemetry.neighbor_count,
           _telemetry.freq, _telemetry.bw, _telemetry.sf, _telemetry.cr, _telemetry.tx_power,
           _telemetry.node_name, _ipAddr, _config.networks[_activeNetwork].ssid,
           ESP.getFreeHeap(),
           loopStr, _telemetry.path_hash_mode, _telemetry.flood_max);

  _mqttClient.publish(_topicTelem, json);
}

// ---------------------------------------------------------------------------
// OTA — HTTP download over WiFi + ESP32 flash
// ---------------------------------------------------------------------------

void WiFiMQTT::performOTA() {
  _otaPending = false;
  _state = WiFiMQTTState::OTA_IN_PROGRESS;

  Serial.printf("[OTA] URL: %s\n", _otaUrl);

  _mqttClient.publish(_topicRsp, "OTA: Starting download...");
  _mqttClient.loop();

  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(180000);

  if (!http.begin(_wifiClient, _otaUrl)) {
    Serial.println("[OTA] HTTP begin failed");
    _mqttClient.publish(_topicRsp, "OTA: HTTP begin failed");
    _state = WiFiMQTTState::CONNECTED;
    return;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("[OTA] HTTP error: %d\n", httpCode);
    char msg[60];
    snprintf(msg, sizeof(msg), "OTA: HTTP error %d", httpCode);
    _mqttClient.publish(_topicRsp, msg);
    http.end();
    _state = WiFiMQTTState::CONNECTED;
    return;
  }

  int fileSize = http.getSize();
  if (fileSize <= 0) {
    Serial.println("[OTA] Unknown content length");
    _mqttClient.publish(_topicRsp, "OTA: Unknown file size");
    http.end();
    _state = WiFiMQTTState::CONNECTED;
    return;
  }

  Serial.printf("[OTA] File size: %d bytes\n", fileSize);

  if (!Update.begin(fileSize)) {
    Serial.printf("[OTA] Update.begin failed: %s\n", Update.errorString());
    _mqttClient.publish(_topicRsp, "OTA: Flash init failed");
    http.end();
    _state = WiFiMQTTState::CONNECTED;
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[1024];
  int offset = 0;
  int lastPct = -1;

  while (offset < fileSize) {
    int avail = stream->available();
    if (avail <= 0) {
      if (!stream->connected()) break;
      delay(10);
      continue;
    }

    int toRead = (avail < (int)sizeof(buf)) ? avail : sizeof(buf);
    int got = stream->readBytes(buf, toRead);
    if (got <= 0) break;

    size_t written = Update.write(buf, got);
    if (written != (size_t)got) {
      Serial.printf("[OTA] Write failed: %d of %d\n", written, got);
      break;
    }

    offset += got;

    int pct = (offset * 100) / fileSize;
    if (pct / 10 != lastPct / 10) {
      Serial.printf("[OTA] Progress: %d%% (%d/%d)\n", pct, offset, fileSize);
      char msg[60];
      snprintf(msg, sizeof(msg), "OTA: Flashing %d%%", pct);
      _mqttClient.publish(_topicRsp, msg);
      _mqttClient.loop();
      lastPct = pct;
    }

    delay(1);
  }

  http.end();

  if (offset < fileSize) {
    Serial.printf("[OTA] Incomplete: %d of %d\n", offset, fileSize);
    Update.abort();
    _mqttClient.publish(_topicRsp, "OTA: Download incomplete");
    _state = WiFiMQTTState::CONNECTED;
    return;
  }

  if (!Update.end(true)) {
    Serial.printf("[OTA] Update.end failed: %s\n", Update.errorString());
    _mqttClient.publish(_topicRsp, "OTA: Verification failed");
    _state = WiFiMQTTState::CONNECTED;
    return;
  }

  Serial.println("[OTA] SUCCESS — rebooting in 3 seconds");
  _mqttClient.publish(_topicRsp, "OTA: Success! Rebooting...");
  _mqttClient.loop();
  delay(3000);
  ESP.restart();
}

#endif // MECK_WIFI_REMOTE