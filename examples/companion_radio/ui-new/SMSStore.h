#pragma once

// =============================================================================
// SMSStore - SD card backed SMS message storage
//
// Stores sent and received messages in /sms/ on the SD card.
// Each conversation is a separate file named by phone number (sanitised).
// Messages are appended as fixed-size records for simple random access.
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef SMS_STORE_H
#define SMS_STORE_H

#include <Arduino.h>
#include <SD.h>

#define SMS_PHONE_LEN    20
#define SMS_BODY_LEN    161
#define SMS_MAX_CONVERSATIONS 20
#define SMS_DIR          "/sms"

// Fixed-size on-disk record (256 bytes, easy alignment)
struct SMSRecord {
  uint32_t timestamp;           // epoch seconds
  uint8_t  isSent;              // 1=sent, 0=received
  uint8_t  reserved[2];
  uint8_t  bodyLen;             // actual length of body
  char     phone[SMS_PHONE_LEN]; // 20
  char     body[SMS_BODY_LEN];   // 161
  uint8_t  padding[256 - 4 - 3 - 1 - SMS_PHONE_LEN - SMS_BODY_LEN];
};

// In-memory message for UI
struct SMSMessage {
  uint32_t timestamp;
  bool     isSent;
  bool     valid;
  char     phone[SMS_PHONE_LEN];
  char     body[SMS_BODY_LEN];
};

// Conversation summary for inbox view
struct SMSConversation {
  char     phone[SMS_PHONE_LEN];
  char     preview[40];         // last message preview
  uint32_t lastTimestamp;
  int      messageCount;
  int      unreadCount;
  bool     valid;
};

class SMSStore {
public:
  void begin();
  bool isReady() const { return _ready; }

  // Save a message (sent or received)
  bool saveMessage(const char* phone, const char* body, bool isSent, uint32_t timestamp);

  // Load conversation list (sorted by most recent)
  int loadConversations(SMSConversation* out, int maxCount);

  // Load messages for a specific phone number (chronological, oldest first)
  int loadMessages(const char* phone, SMSMessage* out, int maxCount);

  // Delete all messages for a phone number
  bool deleteConversation(const char* phone);

  // Get total message count for a phone number
  int getMessageCount(const char* phone);

private:
  bool _ready = false;

  // Convert phone number to safe filename
  void phoneToFilename(const char* phone, char* out, size_t outLen);
};

// Global singleton
extern SMSStore smsStore;

#endif // SMS_STORE_H
#endif // HAS_4G_MODEM