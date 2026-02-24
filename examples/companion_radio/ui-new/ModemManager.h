#pragma once

// =============================================================================
// ModemManager - A7682E 4G Modem Driver for T-Deck Pro (V1.1 4G variant)
//
// Runs AT commands on a dedicated FreeRTOS task (Core 0, priority 1) to never
// block the mesh radio loop.  Communicates with main loop via lock-free queues.
//
// Supports: SMS send/receive, voice call dial/answer/hangup/DTMF
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
#define MODEM_TASK_STACK_SIZE 6144    // Increased for call handling
#define MODEM_TASK_CORE       0       // Run on core 0 (mesh runs on core 1)

// Queue sizes
#define MODEM_SEND_QUEUE_SIZE  4
#define MODEM_RECV_QUEUE_SIZE  8
#define MODEM_CALL_CMD_QUEUE_SIZE  4
#define MODEM_CALL_EVT_QUEUE_SIZE  4

// ---------------------------------------------------------------------------
// Modem state machine
// ---------------------------------------------------------------------------
enum class ModemState {
  OFF,
  POWERING_ON,
  INITIALIZING,
  REGISTERING,
  READY,
  ERROR,
  SENDING_SMS,
  // Voice call states
  DIALING,        // ATD sent, waiting for connect/carrier
  RINGING_IN,     // Incoming call detected (RING URC)
  IN_CALL         // Voice call active
};

// ---------------------------------------------------------------------------
// SMS structures (unchanged)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Voice call structures
// ---------------------------------------------------------------------------

// Commands from main loop → modem task
enum class CallCmd : uint8_t {
  DIAL,           // Initiate outgoing call
  ANSWER,         // Answer incoming call
  HANGUP,         // End active call or reject incoming
  DTMF,           // Send DTMF tone during call
  SET_VOLUME      // Set speaker volume
};

struct CallCommand {
  CallCmd cmd;
  char phone[SMS_PHONE_LEN];   // Used by DIAL
  char dtmf;                    // Used by DTMF (single digit: 0-9, *, #)
  uint8_t volume;               // Used by SET_VOLUME (0-5)
};

// Events from modem task → main loop
enum class CallEventType : uint8_t {
  INCOMING,       // Incoming call ringing (+CLIP parsed)
  CONNECTED,      // Call answered / outgoing connected
  ENDED,          // Call ended (local hangup, remote hangup, or no carrier)
  MISSED,         // Incoming call ended before answer
  BUSY,           // Outgoing call got busy signal
  NO_ANSWER,      // Outgoing call not answered
  DIAL_FAILED     // ATD command failed
};

struct CallEvent {
  CallEventType type;
  char phone[SMS_PHONE_LEN];   // Caller/callee number (from +CLIP or dial)
  uint32_t duration;            // Call duration in seconds (for ENDED)
};

// ---------------------------------------------------------------------------
// ModemManager class
// ---------------------------------------------------------------------------

class ModemManager {
public:
  void begin();
  void shutdown();

  // --- SMS API (unchanged) ---
  bool sendSMS(const char* phone, const char* body);
  bool recvSMS(SMSIncoming& out);

  // --- Voice Call API ---
  bool dialCall(const char* phone);          // Queue outgoing call
  bool answerCall();                          // Answer incoming call
  bool hangupCall();                          // End active / reject incoming
  bool sendDTMF(char digit);                  // Send DTMF during call
  bool setCallVolume(uint8_t level);          // Set volume 0-5
  bool pollCallEvent(CallEvent& out);         // Poll from main loop

  // --- State queries (lock-free reads) ---
  ModemState getState() const { return _state; }
  int  getSignalBars() const;       // 0-5
  int  getCSQ() const { return _csq; }
  bool isReady() const { return _state == ModemState::READY; }
  bool isInCall() const { return _state == ModemState::IN_CALL; }
  bool isRinging() const { return _state == ModemState::RINGING_IN; }
  bool isDialing() const { return _state == ModemState::DIALING; }
  bool isCallActive() const {
    return _state == ModemState::IN_CALL ||
           _state == ModemState::DIALING ||
           _state == ModemState::RINGING_IN;
  }
  const char* getOperator() const { return _operator; }
  const char* getCallPhone() const { return _callPhone; }
  uint32_t getCallStartTime() const { return _callStartTime; }

  static const char* stateToString(ModemState s);

  // Persistent enable/disable config (SD file /sms/modem.cfg)
  static bool loadEnabledConfig();
  static void saveEnabledConfig(bool enabled);

private:
  volatile ModemState _state = ModemState::OFF;
  volatile int _csq = 99;    // 99 = unknown
  char _operator[24] = {0};

  // Call state (written by modem task, read by main loop)
  char _callPhone[SMS_PHONE_LEN] = {0};  // Current call number
  volatile uint32_t _callStartTime = 0;    // millis() when call connected

  TaskHandle_t _taskHandle = nullptr;

  // SMS queues
  QueueHandle_t _sendQueue = nullptr;
  QueueHandle_t _recvQueue = nullptr;

  // Call queues
  QueueHandle_t _callCmdQueue = nullptr;   // main loop → modem task
  QueueHandle_t _callEvtQueue = nullptr;   // modem task → main loop

  SemaphoreHandle_t _uartMutex = nullptr;

  // URC line buffer (accumulated between AT commands)
  static const int URC_BUF_SIZE = 256;
  char _urcBuf[URC_BUF_SIZE];
  int  _urcPos = 0;

  // UART AT command helpers (called only from modem task)
  bool modemPowerOn();
  bool sendAT(const char* cmd, const char* expect, uint32_t timeout_ms = 2000);
  bool waitResponse(const char* expect, uint32_t timeout_ms, char* buf = nullptr, size_t bufLen = 0);
  void pollCSQ();
  void pollIncomingSMS();
  bool doSendSMS(const char* phone, const char* body);

  // URC (unsolicited result code) handling
  void drainURCs();                // Read available UART data, process complete lines
  void processURCLine(const char* line);  // Handle a single URC line

  // Call control (called from modem task)
  bool doDialCall(const char* phone);
  bool doAnswerCall();
  bool doHangup();
  bool doSendDTMF(char digit);
  bool doSetVolume(uint8_t level);
  void queueCallEvent(CallEventType type, const char* phone = nullptr, uint32_t duration = 0);

  // FreeRTOS task
  static void taskEntry(void* param);
  void taskLoop();
};

// Global singleton
extern ModemManager modemManager;

#endif // MODEM_MANAGER_H
#endif // HAS_4G_MODEM