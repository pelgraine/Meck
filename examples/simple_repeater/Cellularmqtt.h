#pragma once

// =============================================================================
// CellularMQTT — A7682E Modem + MQTT via native AT commands
//
// Stripped-down modem driver for the remote repeater use case. No SMS, no
// voice calls. Just: power on → register → activate data → MQTT connect.
//
// Uses the A7682E's built-in MQTT client with TLS (AT+CMQTT* commands).
// The UART stays in AT command mode permanently — no PPP needed.
//
// Runs on a FreeRTOS task (Core 0) to avoid blocking the mesh radio loop.
// Commands from the MQTT dashboard arrive as queued strings for the main
// loop to process via CommonCLI; responses are queued back for publishing.
//
// Guard: HAS_4G_MODEM (variant.h provides pin definitions)
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

// Queue sizes
#define CMD_QUEUE_SIZE         4   // MQTT → main loop (CLI commands)
#define RSP_QUEUE_SIZE         4   // main loop → MQTT (CLI responses)

// Telemetry interval (ms)
#define TELEMETRY_INTERVAL   60000   // 60 seconds

// Task configuration
#define CELL_TASK_PRIORITY     1
#define CELL_TASK_STACK_SIZE   8192   // MQTT + TLS AT commands need headroom
#define CELL_TASK_CORE         0

// Reconnect timing
#define MQTT_RECONNECT_MIN   5000    // 5 seconds
#define MQTT_RECONNECT_MAX   300000  // 5 minutes (exponential backoff cap)

// Consecutive publish failures before forced reconnect
#define MQTT_PUB_FAIL_MAX    5

// ---------------------------------------------------------------------------
// State machine
// ---------------------------------------------------------------------------
enum class CellState : uint8_t {
  OFF,
  POWERING_ON,
  INITIALIZING,
  REGISTERING,
  DATA_ACTIVATING,    // PDP context
  MQTT_STARTING,      // AT+CMQTTSTART
  MQTT_CONNECTING,    // AT+CMQTTCONNECT
  CONNECTED,          // MQTT up, subscribed, publishing telemetry
  RECONNECTING,       // Link lost, attempting reconnect
  ERROR
};

// ---------------------------------------------------------------------------
// Queue message types
// ---------------------------------------------------------------------------

// Incoming CLI command (from MQTT subscription → main loop)
struct MQTTCommand {
  char cmd[MQTT_PAYLOAD_MAX];
};

// Outgoing CLI response (from main loop → MQTT publish)
struct MQTTResponse {
  char topic[MQTT_TOPIC_MAX];
  char payload[MQTT_PAYLOAD_MAX];
};

// ---------------------------------------------------------------------------
// MQTT config (loaded from SD: /remote/mqtt.cfg)
// ---------------------------------------------------------------------------
struct MQTTConfig {
  char broker[80];         // e.g. "broker.hivemq.cloud"
  uint16_t port;           // e.g. 8883
  char username[40];
  char password[40];
  char deviceId[MQTT_CLIENT_ID_MAX];  // Auto-generated from MAC if empty
};

// ---------------------------------------------------------------------------
// Telemetry snapshot (filled by main loop, published by modem task)
// ---------------------------------------------------------------------------
struct TelemetryData {
  uint32_t uptime_secs;
  uint16_t battery_mv;
  uint8_t  battery_pct;
  int16_t  temperature;      // 0.1°C from BQ27220
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

  // --- Telemetry (set by main loop, published by modem task) ---
  void updateTelemetry(const TelemetryData& data);

  // --- State queries (lock-free reads from main loop) ---
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

  // Load config from SD card
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

  // Topic strings (built from device ID)
  char _topicCmd[MQTT_TOPIC_MAX] = {0};
  char _topicRsp[MQTT_TOPIC_MAX] = {0};
  char _topicTelem[MQTT_TOPIC_MAX] = {0};
  char _topicOta[MQTT_TOPIC_MAX] = {0};

  TaskHandle_t _taskHandle = nullptr;
  QueueHandle_t _cmdQueue = nullptr;
  QueueHandle_t _rspQueue = nullptr;
  SemaphoreHandle_t _uartMutex = nullptr;

  // Publish failure counter for health monitoring
  uint8_t _pubFailCount = 0;

  // AT response buffer
  static const int AT_BUF_SIZE = 512;
  char _atBuf[AT_BUF_SIZE];

  // URC accumulation
  static const int URC_BUF_SIZE = 600;
  char _urcBuf[URC_BUF_SIZE];
  int _urcPos = 0;

  // MQTT receive state machine (multi-line URC parsing)
  enum MqttRxState { RX_IDLE, RX_WAIT_TOPIC, RX_WAIT_PAYLOAD };
  MqttRxState _rxState = RX_IDLE;
  int _rxTopicLen = 0;
  int _rxPayloadLen = 0;
  char _rxTopic[MQTT_TOPIC_MAX];
  char _rxPayload[MQTT_PAYLOAD_MAX];

  // Reconnect backoff
  uint32_t _reconnectDelay = MQTT_RECONNECT_MIN;

  // --- Modem UART helpers (modem task only) ---
  bool modemPowerOn();
  bool sendAT(const char* cmd, const char* expect, uint32_t timeout_ms = 2000);
  bool waitResponse(const char* expect, uint32_t timeout_ms, char* buf = nullptr, size_t bufLen = 0);
  bool waitPrompt(uint32_t timeout_ms = 5000);
  void drainURCs();
  void processURCLine(const char* line);

  // --- Data connection ---
  void resolveAPN();
  bool activateData();

  // --- MQTT operations (modem task only) ---
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

  // --- Task ---
  static void taskEntry(void* param);
  void taskLoop();
};

extern CellularMQTT cellularMQTT;

#endif // CELLULAR_MQTT_H
#endif // HAS_4G_MODEM