#pragma once

// =============================================================================
// SMSStore - SD Card storage for SMS conversations
//
// Storage layout on SD card:
//   /sms/
//     conversations.idx    — index file: one line per conversation
//     <hash>.log           — message log per conversation (append-only)
//
// Message log format (one line per message):
//   <direction>|<timestamp>|<body>\n
//   direction: 'S' = sent, 'R' = received
//
// Conversation index format (one line per contact):
//   <phone>|<timestamp_last>|<preview>\n
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#include <Arduino.h>
#include <SD.h>
#include "ModemManager.h"   // For SMS_PHONE_LEN, SMS_BODY_LEN

#define SMS_DIR              "/sms"
#define SMS_INDEX_FILE       "/sms/conversations.idx"
#define SMS_MAX_CONVERSATIONS  32
#define SMS_MAX_DISPLAY_MSGS   30    // max messages to load for display
#define SMS_PREVIEW_LEN        40

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct SMSConversation {
  char phone[SMS_PHONE_LEN];
  uint32_t lastTimestamp;
  char preview[SMS_PREVIEW_LEN];
  int unreadCount;
  bool valid;
};

struct SMSMessage {
  char body[SMS_BODY_LEN];
  uint32_t timestamp;
  bool isSent;         // true = we sent it, false = received
  bool valid;
};

// ---------------------------------------------------------------------------
// SMSStore
// ---------------------------------------------------------------------------

class SMSStore {
public:
  SMSStore();

  /// Initialize the store.  Call after SD card is mounted.
  bool begin();

  /// Store a sent message
  bool saveSentMessage(const char* phone, const char* body, uint32_t timestamp);

  /// Store a received message
  bool saveReceivedMessage(const char* phone, const char* body, uint32_t timestamp);

  /// Load conversation list (sorted by most recent first)
  int loadConversations(SMSConversation* out, int maxCount);

  /// Load messages for a specific phone number (newest first)
  /// Returns number of messages loaded.
  int loadMessages(const char* phone, SMSMessage* out, int maxCount);

  /// Delete an entire conversation
  bool deleteConversation(const char* phone);

  /// Get count of conversations
  int getConversationCount();

private:
  bool _initialized;

  // Ensure /sms directory exists
  bool ensureDir();

  // Get log filename for a phone number
  void getLogPath(const char* phone, char* pathBuf, size_t pathLen);

  // Simple hash of phone number for filename
  uint32_t phoneHash(const char* phone);

  // Append a message line to the conversation log
  bool appendMessage(const char* phone, char direction, uint32_t timestamp, const char* body);

  // Update the conversation index
  void updateIndex(const char* phone, uint32_t timestamp, const char* preview);

  // Rebuild index from log files (recovery)
  void rebuildIndex();
};

extern SMSStore smsStore;

#endif // HAS_4G_MODEM