#ifdef HAS_4G_MODEM

#include "SMSStore.h"
#include <Mesh.h>   // For MESH_DEBUG_PRINTLN
#include "target.h" // For SDCARD_CS macro

// Global singleton
SMSStore smsStore;

void SMSStore::begin() {
  // Ensure SMS directory exists
  if (!SD.exists(SMS_DIR)) {
    SD.mkdir(SMS_DIR);
    MESH_DEBUG_PRINTLN("[SMSStore] created %s", SMS_DIR);
  }
  _ready = true;
  MESH_DEBUG_PRINTLN("[SMSStore] ready");
}

void SMSStore::phoneToFilename(const char* phone, char* out, size_t outLen) {
  // Convert phone number to safe filename: strip non-alphanumeric, prefix with dir
  // e.g. "+1234567890" -> "/sms/p1234567890.sms"
  char safe[SMS_PHONE_LEN];
  int j = 0;
  for (int i = 0; phone[i] && j < SMS_PHONE_LEN - 1; i++) {
    char c = phone[i];
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      safe[j++] = c;
    }
  }
  safe[j] = '\0';
  snprintf(out, outLen, "%s/p%s.sms", SMS_DIR, safe);
}

bool SMSStore::saveMessage(const char* phone, const char* body, bool isSent, uint32_t timestamp) {
  if (!_ready) return false;

  char filepath[64];
  phoneToFilename(phone, filepath, sizeof(filepath));

  // Build record
  SMSRecord rec;
  memset(&rec, 0, sizeof(rec));
  rec.timestamp = timestamp;
  rec.isSent = isSent ? 1 : 0;
  rec.bodyLen = strlen(body);
  if (rec.bodyLen >= SMS_BODY_LEN) rec.bodyLen = SMS_BODY_LEN - 1;
  strncpy(rec.phone, phone, SMS_PHONE_LEN - 1);
  strncpy(rec.body, body, SMS_BODY_LEN - 1);

  // Append to file
  File f = SD.open(filepath, FILE_APPEND);
  if (!f) {
    // Try creating
    f = SD.open(filepath, FILE_WRITE);
    if (!f) {
      MESH_DEBUG_PRINTLN("[SMSStore] can't open %s", filepath);
      return false;
    }
  }

  size_t written = f.write((uint8_t*)&rec, sizeof(rec));
  f.close();

  // Release SD CS
  digitalWrite(SDCARD_CS, HIGH);

  return written == sizeof(rec);
}

int SMSStore::loadConversations(SMSConversation* out, int maxCount) {
  if (!_ready) return 0;

  File dir = SD.open(SMS_DIR);
  if (!dir || !dir.isDirectory()) return 0;

  int count = 0;
  File entry;
  while ((entry = dir.openNextFile()) && count < maxCount) {
    const char* name = entry.name();
    // Only process .sms files
    if (!strstr(name, ".sms")) { entry.close(); continue; }

    size_t fileSize = entry.size();
    if (fileSize < sizeof(SMSRecord)) { entry.close(); continue; }

    int numRecords = fileSize / sizeof(SMSRecord);

    // Read the last record for preview
    SMSRecord lastRec;
    entry.seek(fileSize - sizeof(SMSRecord));
    if (entry.read((uint8_t*)&lastRec, sizeof(SMSRecord)) != sizeof(SMSRecord)) {
      entry.close();
      continue;
    }

    SMSConversation& conv = out[count];
    memset(&conv, 0, sizeof(SMSConversation));
    strncpy(conv.phone, lastRec.phone, SMS_PHONE_LEN - 1);
    strncpy(conv.preview, lastRec.body, 39);
    conv.preview[39] = '\0';
    conv.lastTimestamp = lastRec.timestamp;
    conv.messageCount = numRecords;
    conv.unreadCount = 0;  // TODO: track read state
    conv.valid = true;

    count++;
    entry.close();
  }
  dir.close();

  // Release SD CS
  digitalWrite(SDCARD_CS, HIGH);

  // Sort by most recent (simple bubble sort, small N)
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - 1 - i; j++) {
      if (out[j].lastTimestamp < out[j + 1].lastTimestamp) {
        SMSConversation tmp = out[j];
        out[j] = out[j + 1];
        out[j + 1] = tmp;
      }
    }
  }

  return count;
}

int SMSStore::loadMessages(const char* phone, SMSMessage* out, int maxCount) {
  if (!_ready) return 0;

  char filepath[64];
  phoneToFilename(phone, filepath, sizeof(filepath));

  File f = SD.open(filepath, FILE_READ);
  if (!f) return 0;

  size_t fileSize = f.size();
  int numRecords = fileSize / sizeof(SMSRecord);

  // Load from end of file (most recent N messages), in chronological order
  int startIdx = numRecords > maxCount ? numRecords - maxCount : 0;

  // Read chronologically (oldest first) for chat-style display
  SMSRecord rec;
  int outIdx = 0;
  for (int i = startIdx; i < numRecords && outIdx < maxCount; i++) {
    f.seek(i * sizeof(SMSRecord));
    if (f.read((uint8_t*)&rec, sizeof(SMSRecord)) != sizeof(SMSRecord)) continue;

    out[outIdx].timestamp = rec.timestamp;
    out[outIdx].isSent = rec.isSent != 0;
    out[outIdx].valid = true;
    strncpy(out[outIdx].phone, rec.phone, SMS_PHONE_LEN - 1);
    strncpy(out[outIdx].body, rec.body, SMS_BODY_LEN - 1);
    outIdx++;
  }

  f.close();

  digitalWrite(SDCARD_CS, HIGH);

  return outIdx;
}

bool SMSStore::deleteConversation(const char* phone) {
  if (!_ready) return false;

  char filepath[64];
  phoneToFilename(phone, filepath, sizeof(filepath));

  bool ok = SD.remove(filepath);

  digitalWrite(SDCARD_CS, HIGH);

  return ok;
}

int SMSStore::getMessageCount(const char* phone) {
  if (!_ready) return 0;

  char filepath[64];
  phoneToFilename(phone, filepath, sizeof(filepath));

  File f = SD.open(filepath, FILE_READ);
  if (!f) return 0;

  int count = f.size() / sizeof(SMSRecord);
  f.close();

  digitalWrite(SDCARD_CS, HIGH);

  return count;
}

#endif // HAS_4G_MODEM