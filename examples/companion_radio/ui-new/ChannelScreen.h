#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include <Packet.h>
#include "EmojiSprites.h"

// SD card message persistence
#if defined(HAS_SDCARD) && defined(ESP32)
  #include <SD.h>
#endif

// Maximum messages to store in history
#define CHANNEL_MSG_HISTORY_SIZE 300
#define CHANNEL_MSG_TEXT_LEN 160
#define MSG_PATH_MAX 20  // Max repeater hops stored per message

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 20
#endif

// ---------------------------------------------------------------------------
// On-disk format for message persistence (SD card)
// ---------------------------------------------------------------------------
#define MSG_FILE_MAGIC   0x4D434853  // "MCHS" - MeshCore History Store
#define MSG_FILE_VERSION 3           // v3: MSG_PATH_MAX=20, reserved→snr field
#define MSG_FILE_PATH    "/meshcore/messages.bin"

struct __attribute__((packed)) MsgFileHeader {
  uint32_t magic;
  uint16_t version;
  uint16_t capacity;
  uint16_t count;
  int16_t  newestIdx;
  // 12 bytes total
};

struct __attribute__((packed)) MsgFileRecord {
  uint32_t timestamp;
  uint8_t  path_len;
  uint8_t  channel_idx;
  uint8_t  valid;
  int8_t   snr;          // Receive SNR × 4 (was reserved; 0 = unknown)
  uint8_t  path[MSG_PATH_MAX];  // Repeater hop hashes (first byte of pub key)
  char     text[CHANNEL_MSG_TEXT_LEN];
  // 188 bytes total
};

class UITask;  // Forward declaration
class MyMesh;  // Forward declaration
extern MyMesh the_mesh;

class ChannelScreen : public UIScreen {
public:
  struct ChannelMessage {
    uint32_t timestamp;
    uint8_t path_len;
    uint8_t channel_idx;  // Which channel this message belongs to
    int8_t  snr;          // Receive SNR × 4 (0 if locally sent or unknown)
    uint32_t dm_peer_hash; // DM peer name hash (for conversation filtering)
    uint8_t path[MSG_PATH_MAX];  // Repeater hop hashes
    char text[CHANNEL_MSG_TEXT_LEN];
    bool valid;
  };

  // Simple hash for DM peer matching
  static uint32_t peerHash(const char* s) {
    uint32_t h = 5381;
    while (*s) { h = ((h << 5) + h) ^ (uint8_t)*s++; }
    return h;
  }

private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  
  ChannelMessage _messages[CHANNEL_MSG_HISTORY_SIZE];
  int _msgCount;      // Total messages stored
  int _newestIdx;     // Index of newest message (circular buffer)
  int _scrollPos;     // Current scroll position (0 = newest)
  int _msgsPerPage;   // Messages that fit on screen
  uint8_t _viewChannelIdx;  // Which channel we're currently viewing
  bool _sdReady;      // SD card is available for persistence
  bool _showPathOverlay;  // Show path detail overlay for last received msg
  int _pathScrollPos;     // Scroll offset within path overlay hop list
  int _pathHopsVisible;   // Hops that fit on screen (set during render)

  // Reply select mode — press R to pick a message and reply with @mention
  bool _replySelectMode;       // True when user is picking a message to reply to
  int  _replySelectPos;        // Index into chronological channelMsgs[] (0=oldest)
  int  _replyChannelMsgCount;  // Cached count from last render (for input bounds)

  // DM tab (channel_idx == 0xFF) two-level view:
  //   Inbox mode: list of contacts you have DMs from
  //   Conversation mode: messages filtered to one contact
  bool _dmInboxMode;           // true = showing inbox list, false = conversation
  int  _dmInboxScroll;         // Scroll position in inbox list
  char _dmFilterName[32];      // Selected contact name for conversation view
  int  _dmContactIdx;          // Contact index for conversation (-1 if unknown)
  uint8_t _dmContactPerms;     // Last login permissions for this contact (0=none/guest)
  const uint8_t* _dmUnreadPtr; // Pointer to per-contact DM unread array (from UITask)

  // Helper: does a message belong to the current view?
  bool msgMatchesView(const ChannelMessage& msg) const {
    if (!msg.valid) return false;
    if (_viewChannelIdx != 0xFF) {
      return msg.channel_idx == _viewChannelIdx;
    }
    // DM tab in conversation mode: filter by peer hash
    if (!_dmInboxMode && _dmFilterName[0] != '\0') {
      if (msg.channel_idx != 0xFF) return false;
      return msg.dm_peer_hash == peerHash(_dmFilterName);
    }
    // Inbox mode or no filter — match all DMs
    return msg.channel_idx == 0xFF;
  }

  // Per-channel unread message counts (standalone mode)
  // Index 0..MAX_GROUP_CHANNELS-1 for channel messages
  // Index MAX_GROUP_CHANNELS for DMs (channel_idx == 0xFF)
  int _unread[MAX_GROUP_CHANNELS + 1];
  
public:
  ChannelScreen(UITask* task, mesh::RTCClock* rtc) 
    : _task(task), _rtc(rtc), _msgCount(0), _newestIdx(-1), _scrollPos(0), 
      _msgsPerPage(6), _viewChannelIdx(0), _sdReady(false), _showPathOverlay(false), _pathScrollPos(0), _pathHopsVisible(20),
      _replySelectMode(false), _replySelectPos(-1), _replyChannelMsgCount(0),
      _dmInboxMode(true), _dmInboxScroll(0), _dmContactIdx(-1), _dmContactPerms(0), _dmUnreadPtr(nullptr) {
    _dmFilterName[0] = '\0';
    // Initialize all messages as invalid
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      _messages[i].valid = false;
      _messages[i].dm_peer_hash = 0;
      memset(_messages[i].path, 0, MSG_PATH_MAX);
    }
    // Initialize unread counts
    memset(_unread, 0, sizeof(_unread));
  }

  void setSDReady(bool ready) { _sdReady = ready; }

  // Add a new message to the history
  // peer_name: for DMs, the contact this message belongs to (sender for received, recipient for sent)
  void addMessage(uint8_t channel_idx, uint8_t path_len, const char* sender, const char* text,
                  const uint8_t* path_bytes = nullptr, int8_t snr = 0, const char* peer_name = nullptr) {
    // Move to next slot in circular buffer
    _newestIdx = (_newestIdx + 1) % CHANNEL_MSG_HISTORY_SIZE;
    
    ChannelMessage* msg = &_messages[_newestIdx];
    msg->timestamp = _rtc->getCurrentTime();
    msg->path_len = path_len;
    msg->channel_idx = channel_idx;
    msg->snr = snr;
    msg->valid = true;
    
    // Set DM peer hash for conversation filtering
    if (channel_idx == 0xFF) {
      msg->dm_peer_hash = peerHash(peer_name ? peer_name : sender);
    } else {
      msg->dm_peer_hash = 0;
    }
    
    // Store path hop hashes
    memset(msg->path, 0, MSG_PATH_MAX);
    if (path_bytes && path_len > 0 && path_len != 0xFF) {
      int n = mesh::Packet::getPathByteLenFor(path_len);
      if (n > MSG_PATH_MAX) n = MSG_PATH_MAX;
      memcpy(msg->path, path_bytes, n);
    }
    
    // Sanitize emoji: replace UTF-8 emoji sequences with single-byte escape codes
    // The text already contains "Sender: message" format
    emojiSanitize(text, msg->text, CHANNEL_MSG_TEXT_LEN);
    
    if (_msgCount < CHANNEL_MSG_HISTORY_SIZE) {
      _msgCount++;
    }
    
    // Reset scroll to show newest message
    _scrollPos = 0;
    _showPathOverlay = false;  // Dismiss overlay on new message
    _pathScrollPos = 0;
    _replySelectMode = false;  // Dismiss reply select on new message
    _replySelectPos = -1;

    // Track unread count for this channel (only for received messages, not sent)
    // path_len == 0 means locally sent
    if (path_len != 0) {
      int unreadSlot = (channel_idx == 0xFF) ? MAX_GROUP_CHANNELS : channel_idx;
      if (unreadSlot >= 0 && unreadSlot <= MAX_GROUP_CHANNELS) {
        _unread[unreadSlot]++;
      }
    }

    // Persist to SD card
    saveToSD();
  }

  // Get count of messages for the currently viewed channel
  int getMessageCountForChannel() const {
    int count = 0;
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      if (msgMatchesView(_messages[i])) {
        count++;
      }
    }
    return count;
  }

  int getMessageCount() const { return _msgCount; }
  
  uint8_t getViewChannelIdx() const { return _viewChannelIdx; }
  void setViewChannelIdx(uint8_t idx) {
    _viewChannelIdx = idx;
    _scrollPos = 0;
    _showPathOverlay = false;
    _pathScrollPos = 0;
    // Reset DM inbox state when entering DM tab
    if (idx == 0xFF) {
      _dmInboxMode = true;
      _dmInboxScroll = 0;
      _dmFilterName[0] = '\0';
      _dmContactIdx = -1;
      _dmContactPerms = 0;
    }
    markChannelRead(idx);
  }
  bool isDMTab() const { return _viewChannelIdx == 0xFF; }
  bool isDMInboxMode() const { return _viewChannelIdx == 0xFF && _dmInboxMode; }
  bool isDMConversation() const { return _viewChannelIdx == 0xFF && !_dmInboxMode; }
  const char* getDMFilterName() const { return _dmFilterName; }

  // Open a specific contact's DM conversation directly (skipping inbox)
  void openConversation(const char* contactName, int contactIdx = -1, uint8_t perms = 0) {
    strncpy(_dmFilterName, contactName, sizeof(_dmFilterName) - 1);
    _dmFilterName[sizeof(_dmFilterName) - 1] = '\0';
    _dmInboxMode = false;
    _dmContactIdx = contactIdx;
    _dmContactPerms = perms;
    _scrollPos = 0;
  }

  int getDMContactIdx() const { return _dmContactIdx; }
  uint8_t getDMContactPerms() const { return _dmContactPerms; }
  void setDMContactPerms(uint8_t p) { _dmContactPerms = p; }
  bool isShowingPathOverlay() const { return _showPathOverlay; }
  void dismissPathOverlay() { _showPathOverlay = false; _pathScrollPos = 0; }

  // Set pointer to per-contact DM unread array (called by UITask after allocation)
  void setDMUnreadPtr(const uint8_t* ptr) { _dmUnreadPtr = ptr; }

  // Subtract a specific amount from the DM unread slot (used by per-contact clearing)
  void subtractDMUnread(int count) {
    int slot = MAX_GROUP_CHANNELS;  // DM slot
    _unread[slot] -= count;
    if (_unread[slot] < 0) _unread[slot] = 0;
  }

  // --- Reply select mode (R key → pick a message → Enter to @mention reply) ---
  bool isReplySelectMode() const { return _replySelectMode; }
  void exitReplySelect() { _replySelectMode = false; _replySelectPos = -1; }

  // Extract sender name from a "Sender: message" formatted text.
  // Returns true if a sender was found, fills senderBuf (null-terminated).
  static bool extractSenderName(const char* msgText, char* senderBuf, int bufLen) {
    const char* colon = strstr(msgText, ": ");
    if (!colon || colon == msgText) return false;
    int nameLen = colon - msgText;
    if (nameLen >= bufLen) nameLen = bufLen - 1;
    memcpy(senderBuf, msgText, nameLen);
    senderBuf[nameLen] = '\0';
    return true;
  }

  // Get the sender name of the currently selected message in reply select mode.
  // Returns true and fills senderBuf if valid selection exists.
  bool getReplySelectSender(char* senderBuf, int bufLen) {
    if (!_replySelectMode || _replySelectPos < 0) return false;

    // Rebuild the channel message list (same logic as render)
    static int rsMsgs[CHANNEL_MSG_HISTORY_SIZE];
    int count = 0;
    for (int i = 0; i < _msgCount && count < CHANNEL_MSG_HISTORY_SIZE; i++) {
      int idx = _newestIdx - i;
      while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
      idx = idx % CHANNEL_MSG_HISTORY_SIZE;
      if (_messages[idx].valid && msgMatchesView(_messages[idx])) {
        rsMsgs[count++] = idx;
      }
    }
    // Reverse to chronological (oldest first)
    for (int l = 0, r = count - 1; l < r; l++, r--) {
      int t = rsMsgs[l]; rsMsgs[l] = rsMsgs[r]; rsMsgs[r] = t;
    }

    if (_replySelectPos >= count) return false;
    int idx = rsMsgs[_replySelectPos];
    return extractSenderName(_messages[idx].text, senderBuf, bufLen);
  }

  // Get the ChannelMessage pointer for the currently selected reply message.
  ChannelMessage* getReplySelectMsg() {
    if (!_replySelectMode || _replySelectPos < 0) return nullptr;

    static int rsMsgs[CHANNEL_MSG_HISTORY_SIZE];
    int count = 0;
    for (int i = 0; i < _msgCount && count < CHANNEL_MSG_HISTORY_SIZE; i++) {
      int idx = _newestIdx - i;
      while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
      idx = idx % CHANNEL_MSG_HISTORY_SIZE;
      if (_messages[idx].valid && msgMatchesView(_messages[idx])) {
        rsMsgs[count++] = idx;
      }
    }
    for (int l = 0, r = count - 1; l < r; l++, r--) {
      int t = rsMsgs[l]; rsMsgs[l] = rsMsgs[r]; rsMsgs[r] = t;
    }

    if (_replySelectPos >= count) return nullptr;
    return &_messages[rsMsgs[_replySelectPos]];
  }

  // --- Unread message tracking (standalone mode) ---

  // Mark all messages for a channel as read
  void markChannelRead(uint8_t channel_idx) {
    int slot = (channel_idx == 0xFF) ? MAX_GROUP_CHANNELS : channel_idx;
    if (slot >= 0 && slot <= MAX_GROUP_CHANNELS) {
      _unread[slot] = 0;
    }
  }

  // Get unread count for a specific channel
  int getUnreadForChannel(uint8_t channel_idx) const {
    int slot = (channel_idx == 0xFF) ? MAX_GROUP_CHANNELS : channel_idx;
    if (slot >= 0 && slot <= MAX_GROUP_CHANNELS) {
      return _unread[slot];
    }
    return 0;
  }

  // Get total unread across all channels
  int getTotalUnread() const {
    int total = 0;
    for (int i = 0; i <= MAX_GROUP_CHANNELS; i++) {
      total += _unread[i];
    }
    return total;
  }

  // Find the newest RECEIVED message for the current channel
  // (path_len != 0 means received, path_len 0 = locally sent)
  ChannelMessage* getNewestReceivedMsg() {
    for (int i = 0; i < _msgCount; i++) {
      int idx = _newestIdx - i;
      while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
      idx = idx % CHANNEL_MSG_HISTORY_SIZE;
      if (msgMatchesView(_messages[idx])
          && _messages[idx].path_len != 0) {
        return &_messages[idx];
      }
    }
    return nullptr;
  }

  // Format the path of the newest received message as paste-ready text
  // Output: comma-separated hex prefixes e.g. "30, 3b, 9b, 05, e8, 36"
  // Returns length written (0 if no path available)
  int formatPathAsText(char* buf, int bufLen) {
    ChannelMessage* msg = getNewestReceivedMsg();
    if (!msg || msg->path_len == 0 || msg->path_len == 0xFF) return 0;
    
    int pos = 0;
    uint8_t hopCount = msg->path_len & 63;
    uint8_t bytesPerHop = (msg->path_len >> 6) + 1;
    
    for (int h = 0; h < hopCount && pos < bufLen - 1; h++) {
      if (h > 0) pos += snprintf(buf + pos, bufLen - pos, ", ");
      int offset = h * bytesPerHop;
      for (int b = 0; b < bytesPerHop && pos < bufLen - 1; b++) {
        pos += snprintf(buf + pos, bufLen - pos, "%02x", msg->path[offset + b]);
      }
    }
    
    return pos;
  }

  // -----------------------------------------------------------------------
  // SD card persistence
  // -----------------------------------------------------------------------

  // Save the entire message buffer to SD card.
  // File: /meshcore/messages.bin  (~50 KB for 300 messages)
  void saveToSD() {
#if defined(HAS_SDCARD) && defined(ESP32)
    if (!_sdReady) return;

    // Ensure directory exists
    if (!SD.exists("/meshcore")) {
      SD.mkdir("/meshcore");
    }

    File f = SD.open(MSG_FILE_PATH, "w", true);
    if (!f) {
      Serial.println("ChannelScreen: SD save failed - can't open file");
      return;
    }

    // Write header
    MsgFileHeader hdr;
    hdr.magic    = MSG_FILE_MAGIC;
    hdr.version  = MSG_FILE_VERSION;
    hdr.capacity = CHANNEL_MSG_HISTORY_SIZE;
    hdr.count    = (uint16_t)_msgCount;
    hdr.newestIdx = (int16_t)_newestIdx;
    f.write((uint8_t*)&hdr, sizeof(hdr));

    // Write all message slots (including invalid ones - preserves circular buffer layout)
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      MsgFileRecord rec;
      rec.timestamp   = _messages[i].timestamp;
      rec.path_len    = _messages[i].path_len;
      rec.channel_idx = _messages[i].channel_idx;
      rec.valid       = _messages[i].valid ? 1 : 0;
      rec.snr         = _messages[i].snr;
      memcpy(rec.path, _messages[i].path, MSG_PATH_MAX);
      memcpy(rec.text, _messages[i].text, CHANNEL_MSG_TEXT_LEN);
      f.write((uint8_t*)&rec, sizeof(rec));
    }

    f.close();
    digitalWrite(SDCARD_CS, HIGH);  // Release SD CS
#endif
  }

  // Load message buffer from SD card.  Returns true if messages were loaded.
  bool loadFromSD() {
#if defined(HAS_SDCARD) && defined(ESP32)
    if (!_sdReady) return false;

    if (!SD.exists(MSG_FILE_PATH)) {
      Serial.println("ChannelScreen: No saved messages on SD");
      return false;
    }

    File f = SD.open(MSG_FILE_PATH, "r");
    if (!f) {
      Serial.println("ChannelScreen: SD load failed - can't open file");
      return false;
    }

    // Read and validate header
    MsgFileHeader hdr;
    if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr)) {
      Serial.println("ChannelScreen: SD load failed - short header");
      f.close();
      return false;
    }

    if (hdr.magic != MSG_FILE_MAGIC) {
      Serial.printf("ChannelScreen: SD load failed - bad magic 0x%08X\n", hdr.magic);
      f.close();
      return false;
    }

    if (hdr.version != MSG_FILE_VERSION) {
      Serial.printf("ChannelScreen: SD load failed - version %d (expected %d)\n",
                    hdr.version, MSG_FILE_VERSION);
      f.close();
      return false;
    }

    if (hdr.capacity != CHANNEL_MSG_HISTORY_SIZE) {
      Serial.printf("ChannelScreen: SD load failed - capacity %d (expected %d)\n",
                    hdr.capacity, CHANNEL_MSG_HISTORY_SIZE);
      f.close();
      return false;
    }

    // Read message records
    int loaded = 0;
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      MsgFileRecord rec;
      if (f.read((uint8_t*)&rec, sizeof(rec)) != sizeof(rec)) {
        Serial.printf("ChannelScreen: SD load - short read at record %d\n", i);
        break;
      }
      _messages[i].timestamp   = rec.timestamp;
      _messages[i].path_len    = rec.path_len;
      _messages[i].channel_idx = rec.channel_idx;
      _messages[i].valid       = (rec.valid != 0);
      _messages[i].snr         = rec.snr;
      memcpy(_messages[i].path, rec.path, MSG_PATH_MAX);
      memcpy(_messages[i].text, rec.text, CHANNEL_MSG_TEXT_LEN);
      if (_messages[i].valid) loaded++;
    }

    _msgCount   = (int)hdr.count;
    _newestIdx  = (int)hdr.newestIdx;
    _scrollPos  = 0;

    // Sanity-check restored state
    if (_newestIdx < -1 || _newestIdx >= CHANNEL_MSG_HISTORY_SIZE) _newestIdx = -1;
    if (_msgCount < 0 || _msgCount > CHANNEL_MSG_HISTORY_SIZE) _msgCount = loaded;

    f.close();
    digitalWrite(SDCARD_CS, HIGH);  // Release SD CS
    Serial.printf("ChannelScreen: Loaded %d messages from SD (count=%d, newest=%d)\n",
                  loaded, _msgCount, _newestIdx);
    return loaded > 0;
#else
    return false;
#endif
  }

  // -----------------------------------------------------------------------
  // Rendering
  // -----------------------------------------------------------------------

  int render(DisplayDriver& display) override {
    char tmp[40];
    
    // Header - show current channel name
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    
    // Get channel name
    ChannelDetails channel;
    if (_viewChannelIdx == 0xFF) {
      if (_dmInboxMode) {
        display.print("Direct Messages");
      } else {
        char hdr[40];
        snprintf(hdr, sizeof(hdr), "DM: %s", _dmFilterName);
        display.print(hdr);
      }
    } else if (the_mesh.getChannel(_viewChannelIdx, channel)) {
      display.print(channel.name);
    } else {
      sprintf(tmp, "Channel %d", _viewChannelIdx);
      display.print(tmp);
    }
    
    // Message count for this channel on right
    int channelMsgCount = getMessageCountForChannel();
    sprintf(tmp, "[%d]", channelMsgCount);
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);
    
    // Divider line
    display.drawRect(0, 11, display.width(), 1);

    // === DM Inbox mode: show list of contacts with DMs ===
    if (_viewChannelIdx == 0xFF && _dmInboxMode) {
      #define DM_INBOX_MAX 20
      struct DMInboxEntry {
        char name[32];
        int msgCount;
        int unreadCount;
        uint32_t newestTs;
      };
      DMInboxEntry inbox[DM_INBOX_MAX];
      int inboxCount = 0;

      // Scan all DMs and build unique sender list
      for (int i = 0; i < _msgCount && i < CHANNEL_MSG_HISTORY_SIZE; i++) {
        int idx = _newestIdx - i;
        while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
        idx = idx % CHANNEL_MSG_HISTORY_SIZE;
        if (!_messages[idx].valid || _messages[idx].channel_idx != 0xFF) continue;

        char sender[32];
        if (!extractSenderName(_messages[idx].text, sender, sizeof(sender))) continue;

        // Find existing entry
        int found = -1;
        for (int j = 0; j < inboxCount; j++) {
          if (strcmp(inbox[j].name, sender) == 0) { found = j; break; }
        }
        if (found < 0 && inboxCount < DM_INBOX_MAX) {
          found = inboxCount++;
          strncpy(inbox[found].name, sender, 31);
          inbox[found].name[31] = '\0';
          inbox[found].msgCount = 0;
          inbox[found].unreadCount = 0;
          inbox[found].newestTs = 0;
        }
        if (found >= 0) {
          inbox[found].msgCount++;
          if (_messages[idx].timestamp > inbox[found].newestTs)
            inbox[found].newestTs = _messages[idx].timestamp;
        }
      }

      // Look up unread counts from per-contact array
      if (_dmUnreadPtr) {
        for (int e = 0; e < inboxCount; e++) {
          uint32_t numC = the_mesh.getNumContacts();
          ContactInfo ci;
          for (uint32_t c = 0; c < numC; c++) {
            if (the_mesh.getContactByIdx(c, ci) && strcmp(ci.name, inbox[e].name) == 0) {
              inbox[e].unreadCount = _dmUnreadPtr[c];
              break;
            }
          }
        }
      }

      // Sort by newest timestamp descending (insertion sort)
      for (int i = 1; i < inboxCount; i++) {
        DMInboxEntry tmp2 = inbox[i];
        int j = i - 1;
        while (j >= 0 && inbox[j].newestTs < tmp2.newestTs) {
          inbox[j + 1] = inbox[j];
          j--;
        }
        inbox[j + 1] = tmp2;
      }

      // Render inbox list
      display.setTextSize(0);
      int lineH = 9;
      int headerH = 14;
      int footerH = 14;
      int maxY = display.height() - footerH;
      int y = headerH;
      int maxVisible = (maxY - headerH) / lineH;
      if (maxVisible < 3) maxVisible = 3;

      // Clamp scroll
      if (_dmInboxScroll >= inboxCount) _dmInboxScroll = inboxCount > 0 ? inboxCount - 1 : 0;

      if (inboxCount == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, y);
        display.print("No direct messages");
        display.setCursor(0, y + lineH);
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("DMs from contacts appear here");
#else
        display.print("A/D: Switch channel");
#endif
      } else {
        int startIdx = max(0, min(_dmInboxScroll - maxVisible / 2,
                                  inboxCount - maxVisible));
        int endIdx = min(inboxCount, startIdx + maxVisible);

        for (int i = startIdx; i < endIdx && y + lineH <= maxY; i++) {
          bool selected = (i == _dmInboxScroll);

          if (selected) {
            display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
            display.fillRect(0, y, display.width(), lineH);
#else
            display.fillRect(0, y + 5, display.width(), lineH);
#endif
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }

          display.setCursor(0, y);

          // Prefix: > for selected, unread indicator
          char prefix[6];
          if (inbox[i].unreadCount > 0) {
            snprintf(prefix, sizeof(prefix), "%s*%d", selected ? ">" : " ", inbox[i].unreadCount);
          } else {
            snprintf(prefix, sizeof(prefix), "%s ", selected ? ">" : " ");
          }
          display.print(prefix);

          // Name (truncated)
          char filteredName[32];
          display.translateUTF8ToBlocks(filteredName, inbox[i].name, sizeof(filteredName));

          // Right side: message count + age
          char ageStr[8];
          uint32_t age = _rtc->getCurrentTime() - inbox[i].newestTs;
          if (age < 60) snprintf(ageStr, sizeof(ageStr), "%ds", age);
          else if (age < 3600) snprintf(ageStr, sizeof(ageStr), "%dm", age / 60);
          else if (age < 86400) snprintf(ageStr, sizeof(ageStr), "%dh", age / 3600);
          else snprintf(ageStr, sizeof(ageStr), "%dd", age / 86400);

          char rightStr[16];
          snprintf(rightStr, sizeof(rightStr), "(%d) %s", inbox[i].msgCount, ageStr);
          int rightW = display.getTextWidth(rightStr) + 2;

          int nameX = display.getTextWidth(prefix) + 2;
          int nameMaxW = display.width() - nameX - rightW - 2;
          display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

          display.setCursor(display.width() - rightW, y);
          display.print(rightStr);

          y += lineH;
        }
      }

      // Footer
      display.setTextSize(1);
      int footerY = display.height() - 12;
      display.drawRect(0, footerY - 2, display.width(), 1);
      display.setColor(DisplayDriver::YELLOW);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.setCursor(0, footerY);
      display.print("Swipe:Nav");
      const char* rtInbox = "Hold:Open";
      display.setCursor(display.width() - display.getTextWidth(rtInbox) - 2, footerY);
      display.print(rtInbox);
#else
      display.setCursor(0, footerY);
      display.print("Q:Bck A/D:Ch");
      const char* rtInbox = "Ent:Open";
      display.setCursor(display.width() - display.getTextWidth(rtInbox) - 2, footerY);
      display.print(rtInbox);
#endif

#ifdef USE_EINK
      return 5000;
#else
      return 1000;
#endif
    }
    
    // --- Path detail overlay ---
    if (_showPathOverlay) {
      display.setTextSize(0);
      int lineH = 9;
      int y = 14;
      
      ChannelMessage* msg = getNewestReceivedMsg();
      if (!msg) {
        display.setCursor(0, y);
        display.setColor(DisplayDriver::LIGHT);
        display.print("No received messages");
      } else {
        // Message preview (first ~30 chars)
        display.setCursor(0, y);
        display.setColor(DisplayDriver::LIGHT);
        char preview[32];
        strncpy(preview, msg->text, 31);
        preview[31] = '\0';
        display.print(preview);
        y += lineH;
        
        // Age
        uint32_t age = _rtc->getCurrentTime() - msg->timestamp;
        display.setCursor(0, y);
        display.setColor(DisplayDriver::YELLOW);
        if (age < 60) sprintf(tmp, "Age: %ds", age);
        else if (age < 3600) sprintf(tmp, "Age: %dm", age / 60);
        else if (age < 86400) sprintf(tmp, "Age: %dh", age / 3600);
        else sprintf(tmp, "Age: %dd", age / 86400);
        display.print(tmp);
        y += lineH;
        
        // Route type
        display.setCursor(0, y);
        uint8_t plen = msg->path_len;
        uint8_t hopCount = plen & 63;           // extract hop count from encoded path_len
        uint8_t bytesPerHop = (plen >> 6) + 1;  // 1, 2, or 3 bytes per hop
        if (plen == 0xFF) {
          display.setColor(DisplayDriver::LIGHT);
          display.print("Route: Direct");
        } else if (plen == 0) {
          display.setColor(DisplayDriver::LIGHT);
          display.print("Route: Local/Sent");
        } else {
          display.setColor(DisplayDriver::GREEN);
          sprintf(tmp, "Route: %d hop%s (%dB)", hopCount, hopCount == 1 ? "" : "s", bytesPerHop);
          display.print(tmp);
        }
        y += lineH;

        // SNR (if available — value is SNR×4)
        if (msg->snr != 0) {
          display.setCursor(0, y);
          display.setColor(DisplayDriver::YELLOW);
          int snr_whole = msg->snr / 4;
          int snr_frac = ((abs(msg->snr) % 4) * 10) / 4;
          sprintf(tmp, "SNR: %d.%ddB", snr_whole, snr_frac);
          display.print(tmp);
          y += lineH;
        }
        y += 2;
        
        // Show each hop resolved against contacts (scrollable)
        if (hopCount > 0 && plen != 0xFF) {
          int displayHops = hopCount;
          int footerReserve = 26;  // footer + divider
          int scrollBarW = 4;
          int maxY = display.height() - footerReserve;
          int hopAreaTop = y;
          
          // Calculate how many hops fit in the visible area
          int hopsVisible = (maxY - hopAreaTop) / lineH;
          if (hopsVisible < 1) hopsVisible = 1;
          _pathHopsVisible = hopsVisible;  // Cache for input handler
          bool needsScroll = displayHops > hopsVisible;
          
          // Clamp scroll position
          int maxScroll = displayHops - hopsVisible;
          if (maxScroll < 0) maxScroll = 0;
          if (_pathScrollPos > maxScroll) _pathScrollPos = maxScroll;
          
          // Available text width (narrower if scroll bar present)
          int textRight = needsScroll ? display.width() - scrollBarW - 2 : display.width();
          (void)textRight;  // reserved for future truncation
          
          int startHop = _pathScrollPos;
          int endHop = startHop + hopsVisible;
          if (endHop > displayHops) endHop = displayHops;
          
          for (int h = startHop; h < endHop && y + lineH <= maxY; h++) {
            int hopOffset = h * bytesPerHop;  // byte offset into path[]
            display.setCursor(0, y);
            display.setColor(DisplayDriver::LIGHT);
            sprintf(tmp, " %d: ", h + 1);
            display.print(tmp);
            
            // Show hex prefix (1, 2, or 3 bytes)
            display.setColor(DisplayDriver::LIGHT);
            if (bytesPerHop == 1) {
              sprintf(tmp, "%02X ", msg->path[hopOffset]);
            } else if (bytesPerHop == 2) {
              sprintf(tmp, "%02X%02X ", msg->path[hopOffset], msg->path[hopOffset + 1]);
            } else {
              sprintf(tmp, "%02X%02X%02X ", msg->path[hopOffset], msg->path[hopOffset + 1], msg->path[hopOffset + 2]);
            }
            display.print(tmp);
            
            // Try to resolve name: prefer repeaters, then any contact
            bool resolved = false;
            int numContacts = the_mesh.getNumContacts();
            ContactInfo contact;
            char filteredName[32];
            
            // First pass: repeaters only
            for (uint32_t ci = 0; ci < numContacts && !resolved; ci++) {
              if (the_mesh.getContactByIdx(ci, contact)) {
                if (memcmp(contact.id.pub_key, &msg->path[hopOffset], bytesPerHop) == 0
                    && contact.type == ADV_TYPE_REPEATER) {
                  display.setColor(DisplayDriver::GREEN);
                  display.translateUTF8ToBlocks(filteredName, contact.name, sizeof(filteredName));
                  display.print(filteredName);
                  resolved = true;
                }
              }
            }
            // Second pass: any contact type
            if (!resolved) {
              for (uint32_t ci = 0; ci < numContacts; ci++) {
                if (the_mesh.getContactByIdx(ci, contact)) {
                  if (memcmp(contact.id.pub_key, &msg->path[hopOffset], bytesPerHop) == 0) {
                    display.setColor(DisplayDriver::YELLOW);
                    display.translateUTF8ToBlocks(filteredName, contact.name, sizeof(filteredName));
                    display.print(filteredName);
                    resolved = true;
                    break;
                  }
                }
              }
            }
            // No name resolved - hex prefix already shown, add "?" marker
            if (!resolved) {
              display.setColor(DisplayDriver::LIGHT);
              display.print("?");
            }
            y += lineH;
          }
          
          // Scroll bar (right edge) when hops exceed visible area
          if (needsScroll) {
            int sbX = display.width() - scrollBarW;
            int sbTop = hopAreaTop;
            int sbHeight = maxY - hopAreaTop;
            
            // Outline
            display.setColor(DisplayDriver::LIGHT);
            display.drawRect(sbX, sbTop, scrollBarW, sbHeight);
            
            // Proportional thumb
            int thumbH = (hopsVisible * sbHeight) / displayHops;
            if (thumbH < 4) thumbH = 4;
            int thumbY = sbTop + (_pathScrollPos * (sbHeight - thumbH)) / maxScroll;
            display.setColor(DisplayDriver::GREEN);
            for (int ty = thumbY + 1; ty < thumbY + thumbH - 1; ty++)
              display.drawRect(sbX + 1, ty, scrollBarW - 2, 1);
          }
        }
      }
      
      // Overlay footer
      display.setTextSize(1);
      int footerY = display.height() - 12;
      display.drawRect(0, footerY - 2, display.width(), 1);
      display.setCursor(0, footerY);
      display.setColor(DisplayDriver::YELLOW);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.print("Back");
      const char* copyHint = "Tap:Dismiss";
#else
      display.print("Q:Back");
      // Show scroll hint if path is scrollable
      if (msg && (msg->path_len & 63) > _pathHopsVisible && msg->path_len != 0xFF) {
        const char* scrollHint = "W/S:Scrl";
        int scrollW = display.getTextWidth(scrollHint);
        display.setCursor((display.width() - scrollW) / 2, footerY);
        display.print(scrollHint);
      }
      const char* copyHint = "Ent:Copy";
#endif
      display.setCursor(display.width() - display.getTextWidth(copyHint) - 2, footerY);
      display.print(copyHint);

#ifdef USE_EINK
      return 5000;
#else
      return 1000;
#endif
    }
    
    if (channelMsgCount == 0) {
      display.setTextSize(0);  // Tiny font for body text
      display.setCursor(0, 20);
      display.setColor(DisplayDriver::LIGHT);
      if (_viewChannelIdx == 0xFF) {
        char noMsg[48];
        snprintf(noMsg, sizeof(noMsg), "No messages from %s", _dmFilterName);
        display.print(noMsg);
        display.setCursor(0, 30);
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Hold: Compose reply");
#else
        display.print("Q: Back to inbox");
        display.setCursor(0, 40);
        display.print("Ent: Compose reply");
#endif
      } else {
        display.print("No messages yet");
        display.setCursor(0, 30);
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Swipe: Switch channel");
        display.setCursor(0, 40);
        display.print("Long press: Compose");
#else
        display.print("A/D: Switch channel");
        display.setCursor(0, 40);
        display.print("C: Compose message");
#endif
      }
      display.setTextSize(1);  // Restore for footer
    } else if (_viewChannelIdx == 0xFF && _dmInboxMode) {
      // =================================================================
      // DM Inbox: list of contacts/rooms you have DM history with
      // =================================================================
      display.setTextSize(0);
      int lineHeight = 9;
      int headerHeight = 14;
      int footerHeight = 14;
      int maxY = display.height() - footerHeight;
      int y = headerHeight;

      // Scan all DM messages and collect unique senders
      #define DM_INBOX_MAX 16
      struct InboxEntry {
        char name[24];
        int count;
        uint32_t newest_ts;
      };
      static InboxEntry inbox[DM_INBOX_MAX];
      int inboxCount = 0;

      for (int i = 0; i < _msgCount; i++) {
        int idx = _newestIdx - i;
        while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
        idx = idx % CHANNEL_MSG_HISTORY_SIZE;
        if (!_messages[idx].valid || _messages[idx].channel_idx != 0xFF) continue;

        char sender[24];
        if (!extractSenderName(_messages[idx].text, sender, sizeof(sender))) continue;

        // Find or add sender in inbox
        bool found = false;
        for (int j = 0; j < inboxCount; j++) {
          if (strcmp(inbox[j].name, sender) == 0) {
            inbox[j].count++;
            if (_messages[idx].timestamp > inbox[j].newest_ts)
              inbox[j].newest_ts = _messages[idx].timestamp;
            found = true;
            break;
          }
        }
        if (!found && inboxCount < DM_INBOX_MAX) {
          strncpy(inbox[inboxCount].name, sender, 23);
          inbox[inboxCount].name[23] = '\0';
          inbox[inboxCount].count = 1;
          inbox[inboxCount].newest_ts = _messages[idx].timestamp;
          inboxCount++;
        }
      }

      // Sort by newest timestamp descending (most recent first)
      for (int i = 1; i < inboxCount; i++) {
        InboxEntry tmp2 = inbox[i];
        int j = i - 1;
        while (j >= 0 && inbox[j].newest_ts < tmp2.newest_ts) {
          inbox[j + 1] = inbox[j];
          j--;
        }
        inbox[j + 1] = tmp2;
      }

      if (inboxCount == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, y);
        display.print("No conversations");
      } else {
        // Clamp scroll
        if (_dmInboxScroll >= inboxCount) _dmInboxScroll = inboxCount - 1;
        if (_dmInboxScroll < 0) _dmInboxScroll = 0;

        int maxVisible = (maxY - headerHeight) / lineHeight;
        if (maxVisible < 3) maxVisible = 3;
        int startIdx = max(0, min(_dmInboxScroll - maxVisible / 2,
                                  inboxCount - maxVisible));
        int endIdx = min(inboxCount, startIdx + maxVisible);

        uint32_t now = _rtc->getCurrentTime();
        for (int i = startIdx; i < endIdx && y + lineHeight <= maxY; i++) {
          bool selected = (i == _dmInboxScroll);

          if (selected) {
            display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
            display.fillRect(0, y, display.width(), lineHeight);
#else
            display.fillRect(0, y + 5, display.width(), lineHeight);
#endif
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }

          display.setCursor(0, y);
          display.print(selected ? ">" : " ");

          // Name (ellipsized)
          char filteredName[24];
          display.translateUTF8ToBlocks(filteredName, inbox[i].name, sizeof(filteredName));

          // Right side: message count + age
          char ageStr[8];
          uint32_t age = now - inbox[i].newest_ts;
          if (age < 60) snprintf(ageStr, sizeof(ageStr), "%ds", age);
          else if (age < 3600) snprintf(ageStr, sizeof(ageStr), "%dm", age / 60);
          else if (age < 86400) snprintf(ageStr, sizeof(ageStr), "%dh", age / 3600);
          else snprintf(ageStr, sizeof(ageStr), "%dd", age / 86400);

          char rightStr[16];
          snprintf(rightStr, sizeof(rightStr), "[%d] %s", inbox[i].count, ageStr);
          int rightW = display.getTextWidth(rightStr) + 2;

          int nameX = display.getTextWidth(">") + 2;
          int nameMaxW = display.width() - nameX - rightW - 2;
          display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

          display.setCursor(display.width() - rightW, y);
          display.print(rightStr);

          y += lineHeight;
        }
      }
      display.setTextSize(1);
    } else {
      display.setTextSize(0);  // Tiny font for message body
      int lineHeight = 9;   // 8px font + 1px spacing
      int headerHeight = 14;
      int footerHeight = 14;
      int scrollBarW = 4;   // Width of scroll indicator on right edge
      // Hard cutoff: no text may START at or beyond this y value
      // This ensures rendered glyphs (which extend lineHeight below y) stay above the footer
      int maxY = display.height() - footerHeight;
      
      int y = headerHeight;
      
      // Build list of messages for this channel (newest first)
      // Static to avoid 1200-byte stack allocation every render cycle
      static int channelMsgs[CHANNEL_MSG_HISTORY_SIZE];
      int numChannelMsgs = 0;
      
      for (int i = 0; i < _msgCount && numChannelMsgs < CHANNEL_MSG_HISTORY_SIZE; i++) {
        int idx = _newestIdx - i;
        while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
        idx = idx % CHANNEL_MSG_HISTORY_SIZE;
        
        if (msgMatchesView(_messages[idx])) {
          channelMsgs[numChannelMsgs++] = idx;
        }
      }
      
      // Reverse to chronological order (oldest first, newest last at bottom)
      for (int l = 0, r = numChannelMsgs - 1; l < r; l++, r--) {
        int tmp = channelMsgs[l]; channelMsgs[l] = channelMsgs[r]; channelMsgs[r] = tmp;
      }
      
      // Cache for reply select input bounds
      _replyChannelMsgCount = numChannelMsgs;
      
      // Clamp scroll position to valid range
      int maxScroll = numChannelMsgs > _msgsPerPage ? numChannelMsgs - _msgsPerPage : 0;
      if (_scrollPos > maxScroll) _scrollPos = maxScroll;
      
      // Calculate start index so newest messages appear at the bottom
      // scrollPos=0 shows the most recent messages, scrollPos++ scrolls up to older
      int startIdx = numChannelMsgs - _msgsPerPage - _scrollPos;
      if (startIdx < 0) startIdx = 0;
      
      // Display messages oldest-to-newest (top to bottom)
      int msgsDrawn = 0;
      bool screenFull = false;
      bool lastMsgTruncated = false;  // Did the last message get clipped by footer?
      for (int i = startIdx; i < numChannelMsgs && y + lineHeight <= maxY; i++) {
        int idx = channelMsgs[i];
        ChannelMessage* msg = &_messages[idx];
        
        // Reply select: is this the currently selected message?
        bool isSelected = (_replySelectMode && i == _replySelectPos);
        
        // Highlight: single fillRect for the entire message area, then
        // draw DARK text on top (same pattern as web reader bookmarks).
        // Because message height depends on word-wrap, we fill a generous
        // area up-front and erase the excess after rendering.
        int yStart = y;
        int contentW = display.width();
        int maxLinesPerMsg = 8;
        if (isSelected) {
          int maxFillH = (maxLinesPerMsg + 1) * lineHeight + 2;
          int availH = maxY - y;
          if (maxFillH > availH) maxFillH = availH;
          display.setColor(DisplayDriver::LIGHT);
          #if defined(LilyGo_T5S3_EPaper_Pro)
          display.fillRect(0, y, contentW, maxFillH);
#else
          display.fillRect(0, y + 5, contentW, maxFillH);
#endif
        }
        
        // Time indicator with hop count - inline on same line as message start
        display.setCursor(0, y);
        display.setColor(isSelected ? DisplayDriver::DARK : DisplayDriver::YELLOW);
        
        uint32_t age = _rtc->getCurrentTime() - msg->timestamp;
        if (isSelected) {
          // Show > marker for selected message, replacing the hop count
          if (age < 60) {
            sprintf(tmp, ">%ds ", age);
          } else if (age < 3600) {
            sprintf(tmp, ">%dm ", age / 60);
          } else if (age < 86400) {
            sprintf(tmp, ">%dh ", age / 3600);
          } else {
            sprintf(tmp, ">%dd ", age / 86400);
          }
        } else {
          if (age < 60) {
            sprintf(tmp, "(%d) %ds ", msg->path_len == 0xFF ? 0 : (msg->path_len & 63), age);
          } else if (age < 3600) {
            sprintf(tmp, "(%d) %dm ", msg->path_len == 0xFF ? 0 : (msg->path_len & 63), age / 60);
          } else if (age < 86400) {
            sprintf(tmp, "(%d) %dh ", msg->path_len == 0xFF ? 0 : (msg->path_len & 63), age / 3600);
          } else {
            sprintf(tmp, "(%d) %dd ", msg->path_len == 0xFF ? 0 : (msg->path_len & 63), age / 86400);
          }
        }
        display.print(tmp);
        // DO NOT advance y - message text continues on the same line
        
        // Message text with character wrapping and inline emoji support
        // (continues after timestamp on first line)
        display.setColor(isSelected ? DisplayDriver::DARK : DisplayDriver::LIGHT);
        
        int textLen = strlen(msg->text);
        int pos = 0;
        int linesForThisMsg = 0;
        char charStr[2] = {0, 0};
        
        // Track position in pixels for emoji placement
        // Uses advance width (cursor movement) not bounding box for px tracking
        int lineW = display.width() - scrollBarW - 1;  // Reserve space for scroll bar
        int px = display.getTextWidth(tmp);  // Pixel X after timestamp
        char dblStr[3] = {0, 0, 0};
        
        while (pos < textLen && linesForThisMsg < maxLinesPerMsg && y + lineHeight <= maxY) {
          uint8_t b = (uint8_t)msg->text[pos];
          
          if (b == EMOJI_PAD_BYTE) { pos++; continue; }
          
          // Word wrap: when starting a new text word, check if it fits
          if (b != ' ' && !isEmojiEscape(b) && px > 0) {
            bool boundary = (pos == 0);
            if (!boundary) {
              for (int bp = pos - 1; bp >= 0; bp--) {
                uint8_t pb = (uint8_t)msg->text[bp];
                if (pb == EMOJI_PAD_BYTE) continue;
                boundary = (pb == ' ' || isEmojiEscape(pb));
                break;
              }
            }
            if (boundary) {
              int wordW = 0;
              for (int j = pos; j < textLen; j++) {
                uint8_t wb = (uint8_t)msg->text[j];
                if (wb == EMOJI_PAD_BYTE) continue;
                if (wb == ' ' || isEmojiEscape(wb)) break;
                charStr[0] = (char)wb;
                dblStr[0] = dblStr[1] = (char)wb;
                int charAdv = display.getTextWidth(dblStr) - display.getTextWidth(charStr);
                if (charAdv < 1) charAdv = 1;
                wordW += charAdv;
              }
              if (px + wordW > lineW) {
                px = 0;
                linesForThisMsg++;
                y += lineHeight;
                if (linesForThisMsg >= maxLinesPerMsg || y + lineHeight > maxY) break;
              }
            }
          }
          
          if (isEmojiEscape(b)) {
            if (px + EMOJI_SM_W > lineW) {
              px = 0;
              linesForThisMsg++;
              y += lineHeight;
              if (linesForThisMsg >= maxLinesPerMsg || y + lineHeight > maxY) break;
            }
            const uint8_t* sprite = getEmojiSpriteSm(b);
            if (sprite) {
              display.drawXbm(px, y, sprite, EMOJI_SM_W, EMOJI_SM_H);
            }
            pos++;
            px += EMOJI_SM_W + 1;
            display.setCursor(px, y);
          } else if (b == ' ') {
            charStr[0] = ' ';
            dblStr[0] = dblStr[1] = ' ';
            int adv = display.getTextWidth(dblStr) - display.getTextWidth(charStr);
            if (adv < 1) adv = 1;  // Minimum advance (rounding fix for proportional fonts)
            if (px + adv > lineW) {
              px = 0;
              linesForThisMsg++;
              y += lineHeight;
              if (linesForThisMsg < maxLinesPerMsg && y + lineHeight <= maxY) {
                // skip trailing space at wrap
              } else break;
            } else {
              display.setCursor(px, y);
              display.print(charStr);
              px += adv;
            }
            pos++;
          } else {
            charStr[0] = (char)b;
            dblStr[0] = dblStr[1] = (char)b;
            int adv = display.getTextWidth(dblStr) - display.getTextWidth(charStr);
            if (adv < 1) adv = 1;  // Minimum advance (rounding fix for proportional fonts)
            if (px + adv > lineW) {
              px = 0;
              linesForThisMsg++;
              y += lineHeight;
              if (linesForThisMsg < maxLinesPerMsg && y + lineHeight <= maxY) {
                // continue to print below
              } else break;
            }
            display.setCursor(px, y);
            display.print(charStr);
            px += adv;
            pos++;
          }
        }
        
        // Check if this message was clipped (not all text rendered)
        lastMsgTruncated = (pos < textLen);
        
        // If we didn't end on a full line, still count it
        if (px > 0) {
          y += lineHeight;
        }
        
        y += 2;  // Small gap between messages
        
        // Erase excess highlight below the actual message.
        // The upfront fillRect covered a max area; restore the unused
        // portion back to background so subsequent messages render cleanly.
        if (isSelected) {
          int usedH = y - yStart;
          int maxFillH = (maxLinesPerMsg + 1) * lineHeight + 2;
          int availH = maxY - yStart;
          if (maxFillH > availH) maxFillH = availH;
          if (usedH < maxFillH) {
            display.setColor(DisplayDriver::DARK);
#if defined(LilyGo_T5S3_EPaper_Pro)
            display.fillRect(0, y, contentW, maxFillH - usedH);
#else
            display.fillRect(0, y + 5, contentW, maxFillH - usedH);
#endif
          }
        }
        
        msgsDrawn++;
        if (y + lineHeight > maxY) screenFull = true;
      }
      
      // Only update _msgsPerPage when at the bottom (scrollPos==0) and the
      // screen actually filled up.  While scrolled, freezing _msgsPerPage
      // prevents a feedback loop where variable-height messages cause
      // msgsPerPage to oscillate, shifting startIdx every render (flicker).
      if (screenFull && msgsDrawn > 0 && _scrollPos == 0) {
        // If the last message was truncated (text clipped by footer), exclude it
        // from the page count so next render starts one message later and the
        // bottom message fits completely.
        int effectiveDrawn = lastMsgTruncated ? msgsDrawn - 1 : msgsDrawn;
        if (effectiveDrawn < 1) effectiveDrawn = 1;
        _msgsPerPage = effectiveDrawn;
      }
      
      // --- Scroll bar (emoji picker style) ---
      int sbX = display.width() - scrollBarW;
      int sbTop = headerHeight;
      int sbHeight = maxY - headerHeight;
      
      // Draw track outline
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(sbX, sbTop, scrollBarW, sbHeight);
      
      if (channelMsgCount > _msgsPerPage) {
        // Scrollable: draw proportional thumb
        int maxScroll = channelMsgCount - _msgsPerPage;
        if (maxScroll < 1) maxScroll = 1;
        int thumbH = (_msgsPerPage * sbHeight) / channelMsgCount;
        if (thumbH < 4) thumbH = 4;
        // _scrollPos=0 is newest (bottom), so invert for thumb position
        int thumbY = sbTop + ((maxScroll - _scrollPos) * (sbHeight - thumbH)) / maxScroll;
        for (int ty = thumbY + 1; ty < thumbY + thumbH - 1; ty++)
          display.drawRect(sbX + 1, ty, scrollBarW - 2, 1);
      } else {
        // All messages fit: fill entire track
        for (int ty = sbTop + 1; ty < sbTop + sbHeight - 1; ty++)
          display.drawRect(sbX + 1, ty, scrollBarW - 2, 1);
      }
      
      display.setTextSize(1);  // Restore for footer
    }
    
    // Footer with controls
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.setCursor(0, footerY);
    if (_viewChannelIdx == 0xFF) {
      display.print("Swipe:Scroll");
      const char* rtCh = "Hold:Reply";
      display.setCursor(display.width() - display.getTextWidth(rtCh) - 2, footerY);
      display.print(rtCh);
    } else {
      display.print("Swipe:Ch/Scroll");
      const char* midCh = "Tap:Path";
      display.setCursor((display.width() - display.getTextWidth(midCh)) / 2, footerY);
      display.print(midCh);
      const char* rtCh = "Hold:Compose";
      display.setCursor(display.width() - display.getTextWidth(rtCh) - 2, footerY);
      display.print(rtCh);
    }
#else
    // Left side: abbreviated controls
    if (_replySelectMode) {
      display.print("W/S:Sel V:Pth Q:X");
      const char* rightText = "Ent:Reply";
      display.setCursor(display.width() - display.getTextWidth(rightText) - 2, footerY);
      display.print(rightText);
    } else if (_viewChannelIdx == 0xFF) {
      if (_dmContactPerms > 0) {
        display.print("Q:Exit L:Admin");
      } else {
        display.print("Q:Exit");
      }
      const char* rightText = "Ent:Reply";
      display.setCursor(display.width() - display.getTextWidth(rightText) - 2, footerY);
      display.print(rightText);
    } else {
      display.print("Q:Bck A/D:Ch R:Rply");
      const char* rightText = "Ent:New";
      display.setCursor(display.width() - display.getTextWidth(rightText) - 2, footerY);
      display.print(rightText);
    }
#endif

#ifdef USE_EINK
    return 5000;
#else
    return 1000;
#endif
  }

  bool handleInput(char c) override {
    // If overlay is showing, handle scroll and dismiss
    if (_showPathOverlay) {
      if (c == 'q' || c == 'Q' || c == '\b' || c == 'v' || c == 'V') {
        _showPathOverlay = false;
        _pathScrollPos = 0;
        return true;
      }
      if (c == '\r' || c == 13) {
        return false;  // Let main.cpp handle Enter for copy-to-compose
      }
      // W - scroll up in hop list
      if (c == 'w' || c == 'W' || c == 0xF2) {
        if (_pathScrollPos > 0) {
          _pathScrollPos--;
        }
        return true;
      }
      // S - scroll down in hop list
      if (c == 's' || c == 'S' || c == 0xF1) {
        ChannelMessage* msg = getNewestReceivedMsg();
        if (msg && msg->path_len > 0 && msg->path_len != 0xFF) {
          int totalHops = msg->path_len & 63;
          if (_pathScrollPos < totalHops - _pathHopsVisible) {
            _pathScrollPos++;
          }
        }
        return true;
      }
      return true;  // Consume all other keys while overlay is up
    }

    // --- Reply select mode ---
    if (_replySelectMode) {
      // Q - exit reply select
      if (c == 'q' || c == 'Q' || c == '\b') {
        _replySelectMode = false;
        _replySelectPos = -1;
        return true;
      }
      // W - select older message (lower index in chronological order)
      if (c == 'w' || c == 'W' || c == 0xF2) {
        if (_replySelectPos > 0) {
          _replySelectPos--;
          // Auto-scroll to keep selection visible
          int startIdx = _replyChannelMsgCount - _msgsPerPage - _scrollPos;
          if (startIdx < 0) startIdx = 0;
          if (_replySelectPos < startIdx) {
            _scrollPos++;
          }
        }
        return true;
      }
      // S - select newer message (higher index in chronological order)
      if (c == 's' || c == 'S' || c == 0xF1) {
        if (_replySelectPos < _replyChannelMsgCount - 1) {
          _replySelectPos++;
          // Auto-scroll to keep selection visible
          int endIdx = _replyChannelMsgCount - _scrollPos;
          if (_replySelectPos >= endIdx) {
            if (_scrollPos > 0) _scrollPos--;
          }
        }
        return true;
      }
      // V - view path for the SELECTED message (not just newest received)
      if (c == 'v' || c == 'V') {
        // Path overlay will use getNewestReceivedMsg() — for v1 this is fine.
        // The user can see the selected message's hop count in the > marker.
        ChannelMessage* selMsg = getReplySelectMsg();
        if (selMsg && selMsg->path_len != 0) {
          _showPathOverlay = true;
          _pathScrollPos = 0;
        }
        return true;
      }
      // Enter - let main.cpp handle (enters compose with @mention)
      if (c == '\r' || c == 13) {
        return false;
      }
      return true;  // Consume all other keys in reply select
    }

    // --- DM Inbox mode (two-level DM view) ---
    if (_viewChannelIdx == 0xFF && _dmInboxMode) {
      // W - scroll up in inbox
      if (c == 'w' || c == 'W' || c == 0xF2) {
        if (_dmInboxScroll > 0) { _dmInboxScroll--; return true; }
        return false;
      }
      // S - scroll down in inbox
      if (c == 's' || c == 'S' || c == 0xF1) {
        _dmInboxScroll++;  // Clamped during render
        return true;
      }
      // Enter - open conversation for selected sender
      if (c == '\r' || c == 13) {
        // Rebuild inbox to find the selected sender name
        int cur = 0;
        for (int i = 0; i < _msgCount; i++) {
          int idx = _newestIdx - i;
          while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
          idx = idx % CHANNEL_MSG_HISTORY_SIZE;
          if (!_messages[idx].valid || _messages[idx].channel_idx != 0xFF) continue;
          char sender[24];
          if (!extractSenderName(_messages[idx].text, sender, sizeof(sender))) continue;
          // Check if we've seen this sender already
          bool dup = false;
          for (int k = 0; k < i; k++) {
            int ki = _newestIdx - k;
            while (ki < 0) ki += CHANNEL_MSG_HISTORY_SIZE;
            ki = ki % CHANNEL_MSG_HISTORY_SIZE;
            if (!_messages[ki].valid || _messages[ki].channel_idx != 0xFF) continue;
            char prev[24];
            if (extractSenderName(_messages[ki].text, prev, sizeof(prev))
                && strcmp(prev, sender) == 0) { dup = true; break; }
          }
          if (dup) continue;
          if (cur == _dmInboxScroll) {
            strncpy(_dmFilterName, sender, sizeof(_dmFilterName) - 1);
            _dmFilterName[sizeof(_dmFilterName) - 1] = '\0';
            _dmInboxMode = false;
            _scrollPos = 0;
            // Look up contact index
            _dmContactIdx = -1;
            _dmContactPerms = 0;
            uint32_t numC = the_mesh.getNumContacts();
            ContactInfo ci;
            for (uint32_t c = 0; c < numC; c++) {
              if (the_mesh.getContactByIdx(c, ci) && strcmp(ci.name, sender) == 0) {
                _dmContactIdx = (int)c;
                break;
              }
            }
            return true;
          }
          cur++;
        }
        return true;
      }
      // Q - let main.cpp handle (back to home)
      if (c == 'q' || c == 'Q' || c == '\b') {
        return false;
      }
      // A/D pass through to channel switching below
      if (c == 'a' || c == 'A' || c == 'd' || c == 'D') {
        // Fall through to channel switching
      } else {
        return true;  // Consume other keys
      }
    }

    // --- DM Conversation mode: Q goes back to inbox ---
    if (_viewChannelIdx == 0xFF && !_dmInboxMode) {
      if (c == 'q' || c == 'Q' || c == '\b') {
        _dmInboxMode = true;
        _dmFilterName[0] = '\0';
        _scrollPos = 0;
        return true;
      }
    }

    int channelMsgCount = getMessageCountForChannel();

    // R - enter reply select mode (group channels only — DM tab uses Enter to reply)
    if (c == 'r' || c == 'R') {
      if (_viewChannelIdx == 0xFF) return false;  // Not applicable on DM tab
      if (channelMsgCount > 0) {
        _replySelectMode = true;
        // Start with newest message selected
        _replySelectPos = _replyChannelMsgCount > 0
                            ? _replyChannelMsgCount - 1 : channelMsgCount - 1;
        return true;
      }
      return false;
    }
    
    // V - show path detail for last received message
    if (c == 'v' || c == 'V') {
      if (getNewestReceivedMsg() != nullptr) {
        _showPathOverlay = true;
        _pathScrollPos = 0;
        return true;
      }
      return false;  // No received messages to show
    }
    
    // W or KEY_PREV - scroll up (older messages)
    if (c == 0xF2 || c == 'w' || c == 'W') {
      if (_scrollPos + _msgsPerPage < channelMsgCount) {
        _scrollPos++;
        return true;
      }
    }
    
    // S or KEY_NEXT - scroll down (newer messages)
    if (c == 0xF1 || c == 's' || c == 'S') {
      if (_scrollPos > 0) {
        _scrollPos--;
        return true;
      }
    }
    
    // A - previous channel (includes DM tab at 0xFF)
    if (c == 'a' || c == 'A') {
      _replySelectMode = false;
      _replySelectPos = -1;
      if (_viewChannelIdx == 0xFF) {
        // DM tab → go to last valid group channel
        for (uint8_t i = MAX_GROUP_CHANNELS - 1; i > 0; i--) {
          ChannelDetails ch;
          if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
            _viewChannelIdx = i;
            break;
          }
        }
      } else if (_viewChannelIdx > 0) {
        _viewChannelIdx--;
      } else {
        // Channel 0 → wrap to DM tab
        _viewChannelIdx = 0xFF;
        _dmInboxMode = true;
        _dmInboxScroll = 0;
        _dmFilterName[0] = '\0';
      }
      _scrollPos = 0;
      markChannelRead(_viewChannelIdx);
      return true;
    }
    
    // D - next channel (includes DM tab at 0xFF)
    if (c == 'd' || c == 'D') {
      _replySelectMode = false;
      _replySelectPos = -1;
      if (_viewChannelIdx == 0xFF) {
        // DM tab → wrap to channel 0
        _viewChannelIdx = 0;
      } else {
        ChannelDetails ch;
        uint8_t nextIdx = _viewChannelIdx + 1;
        if (the_mesh.getChannel(nextIdx, ch) && ch.name[0] != '\0') {
          _viewChannelIdx = nextIdx;
        } else {
          // Past last channel → go to DM tab
          _viewChannelIdx = 0xFF;
          _dmInboxMode = true;
          _dmInboxScroll = 0;
          _dmFilterName[0] = '\0';
        }
      }
      _scrollPos = 0;
      markChannelRead(_viewChannelIdx);
      return true;
    }
    
    return false;
  }
  
  // Reset scroll position to newest
  void resetScroll() {
    _scrollPos = 0;
  }
};