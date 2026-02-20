#pragma once

// =============================================================================
// ModemManager - A7682E 4G Modem Driver for T-Deck Pro (V1.1 4G variant)
//
// Runs AT commands on a dedicated FreeRTOS task (Core 0, priority 1) to never
// block the mesh radio loop.  Communicates with main loop via lock-free queues.
//
// Guard: HAS_4G_MODEM (defined only for the 4G build environment)
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef MODEM_MANAGER_H
#define MODEM_MANAGER_H

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include "variant.h"

// ---------------------------------------------------------------------------
// Modem pins (from variant.h, always defined for reference)
//   MODEM_POWER_EN  41   Board 6609 enable
//   MODEM_PWRKEY    40   Power key toggle
//   MODEM_RST        9   Reset (shared with I2S BCLK on audio board)
//   MODEM_RI         7   Ring indicator (shared with I2S DOUT on audio)
//   MODEM_DTR        8   Data terminal ready (shared with I2S LRC on audio)
//   MODEM_RX        10   UART RX (shared with PIN_PERF_POWERON)
//   MODEM_TX        11   UART TX
// ---------------------------------------------------------------------------

// SMS field limits
#define SMS_PHONE_LEN    20
#define SMS_BODY_LEN    161   // 160 chars + null

// Task configuration
#define MODEM_TASK_PRIORITY   1       // Below mesh (default loop = priority 1 on core 1)
#define MODEM_TASK_STACK_SIZE 4096
#define MODEM_TASK_CORE       0       // Run on core 0 (mesh runs on core 1)

// Queue sizes
#define MODEM_SEND_QUEUE_SIZE  4
#define MODEM_RECV_QUEUE_SIZE  8

// Modem state machine
enum class ModemState {
  OFF,
  POWERING_ON,
  INITIALIZING,
  REGISTERING,
  READY,
  ERROR,
  SENDING_SMS
};

// Outgoing SMS (queued from main loop to modem task)
struct SMSOutgoing {
  char phone[SMS_PHONE_LEN];
  char body[SMS_BODY_LEN];
};

// Incoming SMS (queued from modem task to main loop)
struct SMSIncoming {
  char phone[SMS_PHONE_LEN];
  char body[SMS_BODY_LEN];
  uint32_t timestamp;   // epoch seconds (from modem RTC or millis-based)
};

class ModemManager {
public:
  void begin();
  void shutdown();

  // Non-blocking: queue an SMS for sending (returns false if queue full)
  bool sendSMS(const char* phone, const char* body);

  // Non-blocking: poll for received SMS (returns true if one was dequeued)
  bool recvSMS(SMSIncoming& out);

  // State queries (lock-free reads)
  ModemState getState() const { return _state; }
  int  getSignalBars() const;       // 0-5
  int  getCSQ() const { return _csq; }
  bool isReady() const { return _state == ModemState::READY; }
  const char* getOperator() const { return _operator; }

  static const char* stateToString(ModemState s);

  // Persistent enable/disable config (SD file /sms/modem.cfg)
  static bool loadEnabledConfig();               // returns true if enabled (default)
  static void saveEnabledConfig(bool enabled);

private:
  volatile ModemState _state = ModemState::OFF;
  volatile int _csq = 99;    // 99 = unknown
  char _operator[24] = {0};

  TaskHandle_t _taskHandle = nullptr;
  QueueHandle_t _sendQueue = nullptr;
  QueueHandle_t _recvQueue = nullptr;
  SemaphoreHandle_t _uartMutex = nullptr;

  // UART AT command helpers (called only from modem task)
  bool modemPowerOn();
  bool sendAT(const char* cmd, const char* expect, uint32_t timeout_ms = 2000);
  bool waitResponse(const char* expect, uint32_t timeout_ms, char* buf = nullptr, size_t bufLen = 0);
  void pollCSQ();
  void pollIncomingSMS();
  bool doSendSMS(const char* phone, const char* body);

  // FreeRTOS task
  static void taskEntry(void* param);
  void taskLoop();
};

// Global singleton
extern ModemManager modemManager;

#endif // MODEM_MANAGER_H
#endif // HAS_4G_MODEM