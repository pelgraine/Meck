#pragma once

// =============================================================================
// ModemManager - A7682E 4G Modem Driver for T-Deck Pro (V1.1 4G variant)
//
// Runs entirely on a dedicated FreeRTOS task at lower priority than the mesh
// radio, ensuring LoRa/mesh operations are never blocked by modem AT commands.
//
// Guard: HAS_4G_MODEM (defined only for the 4G build environment)
// =============================================================================

#ifdef HAS_4G_MODEM

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

// ---------------------------------------------------------------------------
// Modem pins (V1.1 4G board)
// NOTE: MODEM_RX (GPIO 10) is shared with PIN_PERF_POWERON.
// After peripheral power-on (HIGH), we reconfigure GPIO 10 as UART RX.
// The UART idle state is HIGH, so peripherals remain powered — this is
// by LilyGo's design.
// ---------------------------------------------------------------------------
#ifndef MODEM_RX
  #define MODEM_RX       10
#endif
#ifndef MODEM_TX
  #define MODEM_TX       11
#endif
#ifndef MODEM_POWER_EN
  #define MODEM_POWER_EN 41
#endif
#ifndef MODEM_PWRKEY
  #define MODEM_PWRKEY   40
#endif
#ifndef MODEM_RST
  #define MODEM_RST       9
#endif
#ifndef MODEM_RI
  #define MODEM_RI        7
#endif
#ifndef MODEM_DTR
  #define MODEM_DTR       8
#endif

// Modem UART
#define MODEM_BAUD       115200
#define MODEM_SERIAL     Serial1

// Timing constants
#define MODEM_CMD_TIMEOUT_MS       5000
#define MODEM_REG_TIMEOUT_MS      60000
#define MODEM_PWRKEY_PULSE_MS      1500
#define MODEM_POWER_SETTLE_MS      3000

// Queue / task sizing
#define SMS_SEND_QUEUE_SIZE           4
#define SMS_RECV_QUEUE_SIZE           8
#define MODEM_TASK_STACK_SIZE      6144
#define MODEM_TASK_PRIORITY           1   // mesh loop() runs at priority 1 on core 1
#define MODEM_TASK_CORE               0   // keep modem on core 0, away from radio

// SMS field limits
#define SMS_PHONE_LEN    20
#define SMS_BODY_LEN    161   // 160 chars + null

// Status poll interval (don't poll too aggressively)
#define MODEM_STATUS_POLL_MS     30000

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

enum class ModemState : uint8_t {
  OFF,
  POWERING_ON,
  INITIALIZING,
  REGISTERING,
  READY,
  ERROR,
  SENDING_SMS
};

struct SMSOutgoing {
  char phone[SMS_PHONE_LEN];
  char body[SMS_BODY_LEN];
};

struct SMSIncoming {
  char phone[SMS_PHONE_LEN];
  char body[SMS_BODY_LEN];
  uint32_t timestamp;
};

// Callback for received SMS (fired from modem task — keep it fast / non-blocking)
typedef void (*SMSReceivedCallback)(const SMSIncoming& sms);

// ---------------------------------------------------------------------------
// ModemManager
// ---------------------------------------------------------------------------

class ModemManager {
public:
  ModemManager();

  /// Call from setup() AFTER board.begin() has driven PIN_PERF_POWERON HIGH.
  /// Reconfigures GPIO 10 as UART RX and spawns the modem background task.
  void begin();

  /// Gracefully power off modem and delete task.
  void shutdown();

  // ------ Thread-safe API (call from any task / loop) ------

  /// Queue an outgoing SMS.  Non-blocking.  Returns false if queue full.
  bool sendSMS(const char* phone, const char* body);

  /// Dequeue a received SMS.  Non-blocking.  Returns false if nothing waiting.
  bool recvSMS(SMSIncoming& out);

  /// Set callback fired on SMS receipt (runs in modem task context).
  void setReceivedCallback(SMSReceivedCallback cb) { _recvCallback = cb; }

  // ------ State queries (safe to read from any core) ------

  ModemState getState() const     { return _state; }
  bool       isReady() const      { return _state == ModemState::READY; }
  int        getSignalBars() const;           // 0-5 from CSQ
  int        getCSQ() const       { return _csq; }
  const char* getOperatorName() const { return _operator; }
  const char* getOwnNumber() const    { return _ownNumber; }
  bool       hasSIM() const       { return _hasSIM; }

  static const char* stateToString(ModemState s);

private:
  TaskHandle_t       _taskHandle;
  QueueHandle_t      _sendQueue;
  QueueHandle_t      _recvQueue;
  SemaphoreHandle_t  _uartMutex;

  volatile ModemState _state;
  volatile int   _csq;          // raw CSQ value 0-31, 99=unknown
  volatile bool  _hasSIM;
  char _operator[24];
  char _ownNumber[SMS_PHONE_LEN];

  SMSReceivedCallback _recvCallback;

  // ---- Internal (modem task only) ----
  bool modemPowerOn();
  bool modemInit();
  bool modemWaitRegistration();

  // AT command primitives
  bool sendAT(const char* cmd, const char* expect,
              char* respBuf, size_t respLen, uint32_t timeoutMs);
  bool sendAT(const char* cmd, const char* expect, uint32_t timeoutMs);
  void readLine(char* buf, size_t maxLen, uint32_t timeoutMs);
  void flushInput();

  // SMS operations
  bool doSendSMS(const SMSOutgoing& sms);
  void checkForNewSMS();
  bool readSMS(int index, SMSIncoming& out);
  bool deleteSMS(int index);
  void deleteAllReadSMS();

  // Periodic housekeeping
  void pollStatus();

  // FreeRTOS task
  static void taskEntry(void* param);
  void taskLoop();
};

extern ModemManager modemManager;

#endif // HAS_4G_MODEM