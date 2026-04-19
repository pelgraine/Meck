#pragma once

// =============================================================================
// WiFiMQTT — WiFi + MQTT for audio variant remote repeater
//
// Same interface as CellularMQTT but uses ESP32 native WiFi + PubSubClient.
// No modem, no AT commands, no FreeRTOS task — runs in the main loop.
//
// Supports multiple WiFi networks in wifi.cfg (SSID/password pairs).
// Tries each in order on connect/reconnect.
//
// Guard: MECK_WIFI_REMOTE (set in platformio env build_flags)
// =============================================================================

#ifdef MECK_WIFI_REMOTE

#ifndef WIFI_MQTT_H
#define WIFI_MQTT_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#if defined(HAS_SDCARD) || defined(SDCARD_CS)
#include <SD.h>
#endif
#include <SPIFFS.h>

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define MQTT_TOPIC_MAX        80
#define MQTT_PAYLOAD_MAX     512
#define MQTT_CLIENT_ID_MAX    32

#define CMD_QUEUE_SIZE         8
#define RSP_QUEUE_SIZE         8

#define MAX_WIFI_NETWORKS      4

#define TELEMETRY_INTERVAL   60000   // 60 seconds
#define WIFI_RECONNECT_MS    10000   // 10 seconds between WiFi reconnect attempts
#define MQTT_RECONNECT_MS    5000    // 5 seconds between MQTT reconnect attempts

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class WiFiMQTTState : uint8_t {
  OFF,
  WIFI_CONNECTING,
  WIFI_CONNECTED,
  MQTT_CONNECTING,
  CONNECTED,
  OTA_IN_PROGRESS,
  ERROR
};

// ---------------------------------------------------------------------------
// Queue message types (same as CellularMQTT for compatibility)
// ---------------------------------------------------------------------------

struct MQTTCommand {
  char cmd[MQTT_PAYLOAD_MAX];
};

struct MQTTResponse {
  char topic[MQTT_TOPIC_MAX];
  char payload[MQTT_PAYLOAD_MAX];
};

// ---------------------------------------------------------------------------
// Config (loaded from SD)
// ---------------------------------------------------------------------------
struct WiFiNetwork {
  char ssid[40];
  char password[64];
};

struct WiFiMQTTConfig {
  WiFiNetwork networks[MAX_WIFI_NETWORKS];
  int networkCount;
  char broker[80];
  uint16_t port;             // 8883 for MQTT TLS
  char username[40];
  char password[40];
  char deviceId[MQTT_CLIENT_ID_MAX];
};

// ---------------------------------------------------------------------------
// Telemetry snapshot
// ---------------------------------------------------------------------------
struct TelemetryData {
  uint32_t uptime_secs;
  uint16_t battery_mv;
  uint8_t  battery_pct;
  int16_t  temperature;
  int      rssi;
  uint8_t  neighbor_count;
  float    freq;
  float    bw;
  uint8_t  sf;
  uint8_t  cr;
  uint8_t  tx_power;
  char     node_name[32];
  bool     mqtt_connected;
  // Routing settings (v1.8+)
  uint8_t  loop_detect;      // 0=off, 1=minimal, 2=moderate, 3=strict
  uint8_t  path_hash_mode;   // 0=1-byte, 1=2-byte, 2=3-byte
  uint8_t  flood_max;        // max flood hop count (0-64)
};

// ---------------------------------------------------------------------------
// WiFiMQTT class
// ---------------------------------------------------------------------------
class WiFiMQTT {
public:
  void begin();
  void loop();    // Call from main loop — handles WiFi, MQTT, publish/subscribe

  // --- Queue API (called from main loop) ---
  bool recvCommand(MQTTCommand& out);
  bool sendResponse(const char* topic, const char* payload);

  // --- Telemetry ---
  void updateTelemetry(const TelemetryData& data);

  // --- OTA ---
  void requestOTA(const char* url);
  bool isOTAInProgress() const { return _state == WiFiMQTTState::OTA_IN_PROGRESS; }

  // --- State queries ---
  WiFiMQTTState getState() const { return _state; }
  bool isConnected() const { return _state == WiFiMQTTState::CONNECTED; }
  int  getRSSI() const { return _rssi; }
  int  getSignalBars() const;
  const char* getSSID() const { return _config.networks[_activeNetwork].ssid; }
  const char* getIPAddress() const { return _ipAddr; }
  const char* getBroker() const { return _config.broker; }
  const char* getRspTopic() const { return _topicRsp; }
  const char* stateString() const;

  static bool loadConfig(WiFiMQTTConfig& cfg);

private:
  WiFiMQTTState _state = WiFiMQTTState::OFF;
  int _rssi = 0;
  int _activeNetwork = 0;

  char _ipAddr[20] = {0};
  WiFiMQTTConfig _config = {};
  TelemetryData _telemetry = {};

  // Topic strings
  char _topicCmd[MQTT_TOPIC_MAX] = {0};
  char _topicRsp[MQTT_TOPIC_MAX] = {0};
  char _topicTelem[MQTT_TOPIC_MAX] = {0};
  char _topicOta[MQTT_TOPIC_MAX] = {0};

  // Command/response ring buffers (no FreeRTOS queues needed — single-threaded)
  MQTTCommand _cmdBuf[CMD_QUEUE_SIZE];
  int _cmdHead = 0, _cmdTail = 0;

  MQTTResponse _rspBuf[RSP_QUEUE_SIZE];
  int _rspHead = 0, _rspTail = 0;

  // MQTT client stack
  WiFiClientSecure _wifiClient;
  PubSubClient _mqttClient;

  // Timers
  unsigned long _lastWifiAttempt = 0;
  unsigned long _lastMqttAttempt = 0;
  unsigned long _lastTelem = 0;
  unsigned long _lastRSSI = 0;

  // OTA state
  bool _otaPending = false;
  char _otaUrl[256] = {0};

  // --- Internal ---
  bool connectWiFi();
  bool connectMQTT();
  void publishTelemetry();
  void publishQueuedResponses();
  void performOTA();

  // PubSubClient callback (static → instance)
  static void mqttCallback(char* topic, byte* payload, unsigned int length);
  void onMessage(char* topic, byte* payload, unsigned int length);
};

extern WiFiMQTT wifiMQTT;

#endif // WIFI_MQTT_H
#endif // MECK_WIFI_REMOTE