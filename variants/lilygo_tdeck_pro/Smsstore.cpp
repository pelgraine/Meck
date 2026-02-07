#ifdef HAS_4G_MODEM

#include "SMSStore.h"
#include <Mesh.h>

SMSStore smsStore;

SMSStore::SMSStore() : _initialized(false) {}

bool SMSStore::begin() {
  if (!ensureDir()) {
    MESH_DEBUG_PRINTLN("[SMSStore] failed to create /sms directory");
    return false;
  }
  _initialized = true;
  MESH_DEBUG_PRINTLN("[SMSStore] initialized");
  return true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool SMSStore::saveSentMessage(const char* phone, const char* body, uint32_t timestamp) {
  if (!_initialized) return false;
  if (!appendMessage(phone, 'S', timestamp, body)) return false;

  // Update index with preview
  char preview[SMS_PREVIEW_LEN];
  snprintf(preview, sizeof(preview), "You: %s", body);
  updateIndex(phone, timestamp, preview);
  return true;
}

bool SMSStore::saveReceivedMessage(const char* phone, const char* body, uint32_t timestamp) {
  if (!_initialized) return false;
  if (!appendMessage(phone, 'R', timestamp, body)) return false;
  updateIndex(phone, timestamp, body);
  return true;
}

int SMSStore::loadConversations(SMSConversation* out, int maxCount) {
  if (!_initialized) return 0;

  File f = SD.open(SMS_INDEX_FILE, FILE_READ);
  if (!f) return 0;

  int count = 0;
  char line[256];

  while (f.available() && count < maxCount) {
    // Read one line
    int pos = 0;
    while (f.available() && pos < (int)sizeof(line) - 1) {
      char c = f.read();
      if (c == '\n') break;
      if (c != '\r') line[pos++] = c;
    }
    line[pos] = '\0';
    if (pos == 0) continue;

    // Parse: phone|timestamp|preview
    char* p1 = strchr(line, '|');
    if (!p1) continue;
    *p1 = '\0';
    char* p2 = strchr(p1 + 1, '|');
    if (!p2) continue;
    *p2 = '\0';

    SMSConversation& conv = out[count];
    memset(&conv, 0, sizeof(conv));
    strncpy(conv.phone, line, SMS_PHONE_LEN - 1);
    conv.lastTimestamp = strtoul(p1 + 1, nullptr, 10);
    strncpy(conv.preview, p2 + 1, SMS_PREVIEW_LEN - 1);
    conv.unreadCount = 0;  // TODO: track unread state
    conv.valid = true;
    count++;
  }

  f.close();

  // Sort by timestamp descending (simple bubble sort — small list)
  for (int i = 0; i < count - 1; i++) {
    for (int j = 0; j < count - i - 1; j++) {
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
  if (!_initialized) return 0;

  char path[48];
  getLogPath(phone, path, sizeof(path));

  File f = SD.open(path, FILE_READ);
  if (!f) return 0;

  // First pass: count total lines to know where to start for "newest N"
  int totalLines = 0;
  while (f.available()) {
    char c = f.read();
    if (c == '\n') totalLines++;
  }
  f.seek(0);

  // Skip older messages if more than maxCount
  int skipLines = (totalLines > maxCount) ? (totalLines - maxCount) : 0;
  int skipped = 0;
  while (f.available() && skipped < skipLines) {
    char c = f.read();
    if (c == '\n') skipped++;
  }

  // Read the messages
  int count = 0;
  char line[256];

  while (f.available() && count < maxCount) {
    int pos = 0;
    while (f.available() && pos < (int)sizeof(line) - 1) {
      char c = f.read();
      if (c == '\n') break;
      if (c != '\r') line[pos++] = c;
    }
    line[pos] = '\0';
    if (pos == 0) continue;

    // Parse: direction|timestamp|body
    if (pos < 4) continue;  // minimum: "S|0|x"
    
    char* p1 = strchr(line, '|');
    if (!p1) continue;
    *p1 = '\0';
    char* p2 = strchr(p1 + 1, '|');
    if (!p2) continue;
    *p2 = '\0';

    SMSMessage& msg = out[count];
    memset(&msg, 0, sizeof(msg));
    msg.isSent = (line[0] == 'S');
    msg.timestamp = strtoul(p1 + 1, nullptr, 10);
    strncpy(msg.body, p2 + 1, SMS_BODY_LEN - 1);
    msg.valid = true;
    count++;
  }

  f.close();

  // Messages are in chronological order (oldest first from file).
  // Reverse to get newest first for display.
  for (int i = 0; i < count / 2; i++) {
    SMSMessage tmp = out[i];
    out[i] = out[count - 1 - i];
    out[count - 1 - i] = tmp;
  }

  return count;
}

bool SMSStore::deleteConversation(const char* phone) {
  if (!_initialized) return false;

  // Delete the log file
  char path[48];
  getLogPath(phone, path, sizeof(path));
  SD.remove(path);

  // Rewrite index without this phone number
  SMSConversation convs[SMS_MAX_CONVERSATIONS];
  int count = loadConversations(convs, SMS_MAX_CONVERSATIONS);

  SD.remove(SMS_INDEX_FILE);
  File f = SD.open(SMS_INDEX_FILE, FILE_WRITE);
  if (!f) return false;

  for (int i = 0; i < count; i++) {
    if (strcmp(convs[i].phone, phone) != 0) {
      f.printf("%s|%lu|%s\n", convs[i].phone, convs[i].lastTimestamp, convs[i].preview);
    }
  }
  f.close();
  return true;
}

int SMSStore::getConversationCount() {
  if (!_initialized) return 0;
  SMSConversation convs[SMS_MAX_CONVERSATIONS];
  return loadConversations(convs, SMS_MAX_CONVERSATIONS);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

bool SMSStore::ensureDir() {
  if (!SD.exists(SMS_DIR)) {
    return SD.mkdir(SMS_DIR);
  }
  return true;
}

uint32_t SMSStore::phoneHash(const char* phone) {
  // Simple DJB2 hash
  uint32_t hash = 5381;
  while (*phone) {
    // Skip non-digit chars for consistency (+1234 vs 1234)
    if (*phone >= '0' && *phone <= '9') {
      hash = ((hash << 5) + hash) + *phone;
    }
    phone++;
  }
  return hash;
}

void SMSStore::getLogPath(const char* phone, char* pathBuf, size_t pathLen) {
  uint32_t h = phoneHash(phone);
  snprintf(pathBuf, pathLen, "%s/%08lx.log", SMS_DIR, (unsigned long)h);
}

bool SMSStore::appendMessage(const char* phone, char direction, uint32_t timestamp, const char* body) {
  char path[48];
  getLogPath(phone, path, sizeof(path));

  File f = SD.open(path, FILE_APPEND);
  if (!f) {
    // Try creating
    f = SD.open(path, FILE_WRITE);
    if (!f) return false;
  }

  // Sanitize body: replace | and newlines with spaces
  char safebody[SMS_BODY_LEN];
  int j = 0;
  for (int i = 0; body[i] && j < SMS_BODY_LEN - 1; i++) {
    char c = body[i];
    if (c == '|' || c == '\n' || c == '\r') c = ' ';
    safebody[j++] = c;
  }
  safebody[j] = '\0';

  f.printf("%c|%lu|%s\n", direction, (unsigned long)timestamp, safebody);
  f.close();
  return true;
}

void SMSStore::updateIndex(const char* phone, uint32_t timestamp, const char* preview) {
  // Read existing index
  SMSConversation convs[SMS_MAX_CONVERSATIONS];
  int count = loadConversations(convs, SMS_MAX_CONVERSATIONS);

  // Find existing entry for this phone, or use a new slot
  int found = -1;
  for (int i = 0; i < count; i++) {
    if (strcmp(convs[i].phone, phone) == 0) {
      found = i;
      break;
    }
  }

  if (found >= 0) {
    convs[found].lastTimestamp = timestamp;
    strncpy(convs[found].preview, preview, SMS_PREVIEW_LEN - 1);
    convs[found].preview[SMS_PREVIEW_LEN - 1] = '\0';
  } else if (count < SMS_MAX_CONVERSATIONS) {
    SMSConversation& c = convs[count];
    memset(&c, 0, sizeof(c));
    strncpy(c.phone, phone, SMS_PHONE_LEN - 1);
    c.lastTimestamp = timestamp;
    strncpy(c.preview, preview, SMS_PREVIEW_LEN - 1);
    c.valid = true;
    count++;
  }
  // else: at max conversations, oldest would need to be evicted (TODO)

  // Rewrite index
  SD.remove(SMS_INDEX_FILE);
  File f = SD.open(SMS_INDEX_FILE, FILE_WRITE);
  if (!f) return;

  for (int i = 0; i < count; i++) {
    if (convs[i].valid) {
      f.printf("%s|%lu|%s\n", convs[i].phone, 
               (unsigned long)convs[i].lastTimestamp, convs[i].preview);
    }
  }
  f.close();
}

#endif // HAS_4G_MODEM