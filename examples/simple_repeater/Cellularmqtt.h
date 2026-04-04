#pragma once

// =============================================================================
// CellularMQTT — A7682E Modem + MQTT via native AT commands
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef CELLULAR_MQTT_H
#define CELLULAR_MQTT_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "variant.h"
#include "ApnDatabase.h"

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define MQTT_TOPIC_MAX        80
#define MQTT_PAYLOAD_MAX     512
#define MQTT_CLIENT_ID_MAX    32

#define CMD_QUEUE_SIZE         4
#define RSP_QUEUE_SIZE         4

#define TELEMETRY_INTERVAL   60000

#define CELL_TASK_PRIORITY     1
#define CELL_TASK_STACK_SIZE   8192
#define CELL_TASK_CORE         0

#define MQTT_RECONNECT_MIN   5000
#define MQTT_RECONNECT_MAX   300000

#define MQTT_PUB_FAIL_MAX    5

#define OTA_CHUNK_SIZE       1024

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class CellState : uint8_t {
  OFF,
  POWERING_ON,
  INITIALIZING,
  REGISTERING,
  DATA_ACTIVATING,
  MQTT_STARTING,
  MQTT_CONNECTING,
  CONNECTED,
  RECONNECTING,
  OTA_IN_PROGRESS,
  ERROR
};

// ---------------------------------------------------------------------------
// Queue message types
// ---------------------------------------------------------------------------

struct MQTTCommand {
  char cmd[MQTT_PAYLOAD_MAX];
};

struct MQTTResponse {
  char topic[MQTT_TOPIC_MAX];
  char payload[MQTT_PAYLOAD_MAX];
};

// ---------------------------------------------------------------------------
// MQTT config (loaded from SD: /remote/mqtt.cfg)
// ---------------------------------------------------------------------------
struct MQTTConfig {
  char broker[80];
  uint16_t port;
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
  int      csq;
  uint8_t  neighbor_count;
  float    freq;
  float    bw;
  uint8_t  sf;
  uint8_t  cr;
  uint8_t  tx_power;
  char     node_name[32];
  char     apn[40];
  char     oper[24];
  bool     mqtt_connected;
};

// ---------------------------------------------------------------------------
// CellularMQTT class
// ---------------------------------------------------------------------------
class CellularMQTT {
public:
  void begin();
  void stop();

  // --- Queue API (called from main loop) ---
  bool recvCommand(MQTTCommand& out);
  bool sendResponse(const char* topic, const char* payload);

  // --- Telemetry ---
  void updateTelemetry(const TelemetryData& data);

  // --- OTA ---
  void requestOTA(const char* url);
  bool isOTAInProgress() const { return _state == CellState::OTA_IN_PROGRESS; }

  // --- State queries ---
  CellState getState() const { return _state; }
  bool isConnected() const { return _state == CellState::CONNECTED; }
  int  getCSQ() const { return _csq; }
  int  getSignalBars() const;
  const char* getOperator() const { return _operator; }
  const char* getIPAddress() const { return _ipAddr; }
  const char* getBroker() const { return _config.broker; }
  const char* getAPN() const { return _apn; }
  const char* getRspTopic() const { return _topicRsp; }
  const char* stateString() const;
  uint32_t getLastCmdTime() const { return _lastCmdTime; }

  static bool loadConfig(MQTTConfig& cfg);

private:
  volatile CellState _state = CellState::OFF;
  volatile int _csq = 99;
  volatile uint32_t _lastCmdTime = 0;

  char _operator[24] = {0};
  char _ipAddr[20] = {0};
  char _imei[20] = {0};
  char _imsi[20] = {0};
  char _apn[64] = {0};

  MQTTConfig _config = {};
  TelemetryData _telemetry = {};
  SemaphoreHandle_t _telemetryMutex = nullptr;

  char _topicCmd[MQTT_TOPIC_MAX] = {0};
  char _topicRsp[MQTT_TOPIC_MAX] = {0};
  char _topicTelem[MQTT_TOPIC_MAX] = {0};
  char _topicOta[MQTT_TOPIC_MAX] = {0};

  TaskHandle_t _taskHandle = nullptr;
  QueueHandle_t _cmdQueue = nullptr;
  QueueHandle_t _rspQueue = nullptr;
  SemaphoreHandle_t _uartMutex = nullptr;

  uint8_t _pubFailCount = 0;

  static const int AT_BUF_SIZE = 512;
  char _atBuf[AT_BUF_SIZE];

  static const int URC_BUF_SIZE = 600;
  char _urcBuf[URC_BUF_SIZE];
  int _urcPos = 0;

  enum MqttRxState { RX_IDLE, RX_WAIT_TOPIC, RX_WAIT_PAYLOAD };
  MqttRxState _rxState = RX_IDLE;
  int _rxTopicLen = 0;
  int _rxPayloadLen = 0;
  char _rxTopic[MQTT_TOPIC_MAX];
  char _rxPayload[MQTT_PAYLOAD_MAX];

  uint32_t _reconnectDelay = MQTT_RECONNECT_MIN;

  // OTA state
  volatile bool _otaPending = false;
  char _otaUrl[256] = {0};

  // --- Modem UART helpers ---
  bool modemPowerOn();
  bool sendAT(const char* cmd, const char* expect, uint32_t timeout_ms = 2000);
  bool waitResponse(const char* expect, uint32_t timeout_ms, char* buf = nullptr, size_t bufLen = 0);
  bool waitPrompt(uint32_t timeout_ms = 5000);
  void drainURCs();
  void processURCLine(const char* line);

  // --- Data connection ---
  void resolveAPN();
  bool activateData();

  // --- MQTT operations ---
  bool mqttStart();
  bool mqttConnect();
  bool mqttSubscribe(const char* topic);
  bool mqttPublish(const char* topic, const char* payload);
  void mqttDisconnect();

  // --- URC handlers ---
  void handleMqttRxStart(const char* line);
  void handleMqttRxTopic(const char* data, int len);
  void handleMqttRxPayload(const char* data, int len);
  void handleMqttRxEnd();
  void handleMqttConnLost(const char* line);

  // --- OTA operations (modem task only) ---
  void performOTA();
  int  httpGet(const char* url);
  bool httpReadChunk(int offset, int len, uint8_t* dest, int* bytesRead);
  void httpTerm();
  int  readRawBytes(uint8_t* dest, int count, uint32_t timeout_ms);

  // --- Task ---
  static void taskEntry(void* param);
  void taskLoop();
};

extern CellularMQTT cellularMQTT;

#endif // CELLULAR_MQTT_H
#endif // HAS_4G_MODEM