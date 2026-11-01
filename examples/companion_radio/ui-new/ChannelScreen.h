#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include "MeckGeo.h"
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include <Packet.h>
#include "EmojiSprites.h"
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
  #include "ui2/Ui2RichText.h"
#endif

// SD card message persistence
#if defined(HAS_SDCARD) && defined(ESP32)
  #include <SD.h>
#endif

// DM send status codes (session only). Guarded so AbstractUITask.h can carry
// the same definitions without a clash.
#ifndef DM_SEND_NONE
#define DM_SEND_NONE       0
#define DM_SEND_SENDING    1
#define DM_SEND_DELIVERED  2
#define DM_SEND_FAILED     3
#endif

// Maximum messages to store in history
#ifndef CHANNEL_MSG_HISTORY_SIZE
#define CHANNEL_MSG_HISTORY_SIZE 300
#endif
#ifndef CHANNEL_MSG_TEXT_LEN
#define CHANNEL_MSG_TEXT_LEN 160
#endif
#define MSG_PATH_MAX 20  // Max repeater hops stored per message

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 20
#endif

// ---------------------------------------------------------------------------
// On-disk format for message persistence (SD card)
// ---------------------------------------------------------------------------
#define MSG_FILE_MAGIC   0x4D434853  // "MCHS" - MeshCore History Store
#define MSG_FILE_VERSION 4           // v4: added dm_peer_hash for DM persistence
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
  uint8_t  valid;        // bit 0: slot in use; bits 4-5: final DM send status
                         // (DM_SEND_DELIVERED / DM_SEND_FAILED, 0 = none). Older
                         // firmware treats any non-zero value as valid.
  int8_t   snr;          // Receive SNR x 4 (was reserved; 0 = unknown)
  uint32_t dm_peer_hash; // DM peer name hash (v4+, for conversation filtering)
  uint8_t  path[MSG_PATH_MAX];  // Repeater hop hashes (first byte of pub key)
  char     text[CHANNEL_MSG_TEXT_LEN];
  // 192 bytes total
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
    bool has_fp;        // sent bubble carries a 12-byte send fingerprint in path[] ("Heard by")
    uint8_t scope_idx;  // Region scope index for display (session only, 0xFF = unscoped). Not persisted.
    // --- DM send status (session only). Not persisted to SD. ---
    uint32_t send_ref;   // Tracked-send handle from MyMesh (0 = untracked)
    uint8_t dm_status;   // DM_SEND_NONE / SENDING / DELIVERED / FAILED
    uint8_t dm_attempt;  // attempts sent so far (for "Sending x/N")
    uint8_t dm_total;    // planned attempts (3 or 5)
    uint8_t heard;       // sent channel message: repeats heard so far (session only)
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
  int  _pathMsgIdx = -1;       // _messages[] slot shown in the path view (-1: newest received)

  // Message menu (UI v2): R selects a message, Enter opens the menu for it
  bool _menuOpen = false;
  int  _menuSel = 0;
  int  _menuMsgIdx = -1;       // _messages[] slot the menu is for
  uint8_t _menuItems[7];
  int  _menuCount = 0;
  bool _showHeardBy = false;   // "Repeated by" view for a sent channel message
  int  _heardMsgIdx = -1;

  // DM tab (channel_idx == 0xFF) two-level view:
  //   Inbox mode: list of contacts you have DMs from
  //   Conversation mode: messages filtered to one contact
  bool _dmInboxMode;           // true = showing inbox list, false = conversation
  int  _dmInboxScroll;         // Scroll position in inbox list
  char _dmFilterName[32];      // Selected contact name for conversation view
  int  _dmContactIdx;          // Contact index for conversation (-1 if unknown)

  // --- Canned messages overlay (opened by a board trigger, e.g. the Max
  // speech-bubble capacitive pad; see main.cpp) ---
  bool _cannedOpen;
  uint8_t _cannedMap[CANNED_MSG_SLOTS];   // visible row -> prefs slot index
  uint8_t _cannedCount;
  int _cannedPendingSlot;                 // tapped slot awaiting main-loop send; -1 = none
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
      _dmInboxMode(true), _dmInboxScroll(0), _dmContactIdx(-1), _dmContactPerms(0), _dmUnreadPtr(nullptr),
      _cannedOpen(false), _cannedCount(0), _cannedPendingSlot(-1) {
    _dmFilterName[0] = '\0';
    // Initialize all messages as invalid
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      _messages[i].valid = false;
      _messages[i].dm_peer_hash = 0;
      memset(_messages[i].path, 0, MSG_PATH_MAX);
      _messages[i].scope_idx = 0xFF;
      _messages[i].send_ref = 0;
      _messages[i].dm_status = DM_SEND_NONE;
      _messages[i].dm_attempt = 0;
      _messages[i].dm_total = 0;
    }
    // Initialize unread counts
    memset(_unread, 0, sizeof(_unread));
  }

  void setSDReady(bool ready) { _sdReady = ready; }

  // Add a new message to the history
  // peer_name: for DMs, the contact this message belongs to (sender for received, recipient for sent)
  // suppressUnread: if true, do not increment the unread counter for this message
  void addMessage(uint8_t channel_idx, uint8_t path_len, const char* sender, const char* text,
                  const uint8_t* path_bytes = nullptr, int8_t snr = 0, const char* peer_name = nullptr,
                  bool suppressUnread = false, uint8_t scope_idx = 0xFF,
                  uint32_t send_ref = 0, uint8_t send_total = 0,
                  const uint8_t* sent_fp = nullptr) {
    // Move to next slot in circular buffer
    _newestIdx = (_newestIdx + 1) % CHANNEL_MSG_HISTORY_SIZE;
    
    ChannelMessage* msg = &_messages[_newestIdx];
    msg->timestamp = _rtc->getCurrentTime();
    msg->path_len = path_len;
    msg->channel_idx = channel_idx;
    msg->snr = snr;
    msg->valid = true;
    msg->scope_idx = scope_idx;
    // Tracked send: attempt 1 has already been transmitted by the time the
    // local echo is added, so the initial state is "Sending 1/N"
    msg->send_ref = send_ref;
    msg->dm_status = send_ref ? DM_SEND_SENDING : DM_SEND_NONE;
    msg->dm_attempt = send_ref ? 1 : 0;
    msg->dm_total = send_total;
    msg->heard = 0;

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

    // Sent bubble: stash the 12-byte send fingerprint in the otherwise-unused
    // path buffer so the "Heard by" overlay can match it to the repeat track.
    msg->has_fp = false;
    if (sent_fp && path_len == 0) {
      memcpy(msg->path, sent_fp, 12);  // SENT_FINGERPRINT_SIZE
      msg->has_fp = true;
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
    // path_len == 0 means locally sent.
    // suppressUnread: per-channel notification preference says not to count this message.
    if (path_len != 0 && !suppressUnread) {
      int unreadSlot = (channel_idx == 0xFF) ? MAX_GROUP_CHANNELS : channel_idx;
      if (unreadSlot >= 0 && unreadSlot <= MAX_GROUP_CHANNELS) {
        _unread[unreadSlot]++;
      }
    }

    // Persist to SD card: only this slot and the header
    saveRecordToSD(_newestIdx);
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

  // Read-only access to stored messages for a given channel, by recency.
  // n = 0 returns the newest message on that channel, n = 1 the next newest,
  // and so on. Returns nullptr when fewer than n + 1 messages exist.
  const ChannelMessage* getChannelMsgByRecency(uint8_t channel_idx, int n) const {
    if (n < 0 || _msgCount == 0) return nullptr;
    int seen = 0;
    for (int i = 0; i < _msgCount; i++) {
      int idx = _newestIdx - i;
      while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
      const ChannelMessage& m = _messages[idx];
      if (!m.valid || m.channel_idx != channel_idx) continue;
      if (seen == n) return &m;
      seen++;
    }
    return nullptr;
  }
  
  uint8_t getViewChannelIdx() const { return _viewChannelIdx; }
  void setViewChannelIdx(uint8_t idx) {
    _viewChannelIdx = idx;
    _scrollPos = 0;
    _showPathOverlay = false;
    _pathScrollPos = 0;
    _cannedOpen = false;
    _cannedPendingSlot = -1;
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
  void dismissPathOverlay() { _showPathOverlay = false; _pathScrollPos = 0; _pathMsgIdx = -1; }

  // --- Canned messages overlay ---
  bool isCannedOpen() const { return _cannedOpen; }
  // Build the list of non-empty slots and open the overlay.
  // Returns false (and stays closed) when every slot is empty.
  bool openCannedList() {
    _cannedCount = 0;
    const NodePrefs* np = the_mesh.getNodePrefs();
    for (uint8_t i = 0; i < CANNED_MSG_SLOTS; i++) {
      if (np->canned_msgs[i][0]) _cannedMap[_cannedCount++] = i;
    }
    if (_cannedCount == 0) return false;
    _cannedOpen = true;
    return true;
  }
  void closeCannedList() { _cannedOpen = false; }
  // Virtual-coordinate tap while the overlay is open: a row tap queues that
  // slot for the main loop to send; any other tap just closes the overlay.
  // Geometry must match the render block (headerH + smallLineH rows).
  void cannedTapAt(int vx, int vy) {
    (void)vx;
    const int headerH = 14;
    int lineH = the_mesh.getNodePrefs()->smallLineH();
    if (vy >= headerH) {
      int row = (vy - headerH) / lineH;
      if (row >= 0 && row < (int)_cannedCount) {
        _cannedPendingSlot = _cannedMap[row];
      }
    }
    _cannedOpen = false;
  }
  // Consumed by the main loop: returns the tapped slot once, then -1.
  int consumeCannedSend() {
    int s = _cannedPendingSlot;
    _cannedPendingSlot = -1;
    return s;
  }

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
  void exitReplySelect() { _replySelectMode = false; _replySelectPos = -1; _menuOpen = false; }

  // --- Message menu (UI v2) -------------------------------------------------
  // R selects a message (as for a reply), Enter opens this menu for it. The
  // actions that open the composer or send run in main.cpp
  // (meckRunMessageAction); the views (path, repeats) are shown here.
  enum { MSG_ACT_REPLY = 1, MSG_ACT_COPY, MSG_ACT_PATH, MSG_ACT_HEARD, MSG_ACT_RESEND, MSG_ACT_MAP };

  // Sent by this device: same rule as the chat bubbles
  bool msgIsOwn(const ChannelMessage& m) {
    if (m.dm_status != DM_SEND_NONE) return true;
    char sender[32];
    return m.path_len == 0 && m.snr == 0 && extractSenderName(m.text, sender, sizeof(sender)) &&
           strcmp(sender, the_mesh.getNodePrefs()->node_name) == 0;
  }

  bool isMenuOpen() const { return _menuOpen; }
  bool isShowingHeardBy() const { return _showHeardBy; }

  // Open the menu for the selected message; false if nothing is selected
  bool openMenu() {
    ChannelMessage* m = getReplySelectMsg();
    if (!m) return false;
    _menuMsgIdx = (int)(m - _messages);
    bool own = msgIsOwn(*m);
    bool dm = (m->channel_idx == 0xFF);
    _menuCount = 0;
    if (!own) _menuItems[_menuCount++] = MSG_ACT_REPLY;
    _menuItems[_menuCount++] = MSG_ACT_COPY;
    if (!own && m->path_len != 0) _menuItems[_menuCount++] = MSG_ACT_PATH;
    if (own && !dm && (m->has_fp || m->heard > 0)) _menuItems[_menuCount++] = MSG_ACT_HEARD;
    if (own) _menuItems[_menuCount++] = MSG_ACT_RESEND;
#if HAS_GPS
    MeckGeoPoint g;
    if (meckGeoFind(msgBody(m), g)) _menuItems[_menuCount++] = MSG_ACT_MAP;   // a location in the text
#endif
    _menuSel = 0;
    _menuOpen = true;
    return true;
  }

  // The highlighted action and its message (nullptr when the menu is closed)
  int menuAction() const { return _menuOpen ? _menuItems[_menuSel] : 0; }
  ChannelMessage* menuMsg() {
    if (_menuMsgIdx < 0 || _menuMsgIdx >= CHANNEL_MSG_HISTORY_SIZE || !_messages[_menuMsgIdx].valid) return nullptr;
    return &_messages[_menuMsgIdx];
  }
  // Message text without the "Sender: " prefix
  static const char* msgBody(const ChannelMessage* m) {
    const char* colon = strstr(m->text, ": ");
    return colon ? colon + 2 : m->text;
  }

  // Views opened from the menu; closing them returns to the selection
  void showPathForMenuMsg() {
    if (!menuMsg()) return;
    _pathMsgIdx = _menuMsgIdx;
    _menuOpen = false;
    _showPathOverlay = true;
    _pathScrollPos = 0;
  }
  void showHeardForMenuMsg() {
    if (!menuMsg()) return;
    _heardMsgIdx = _menuMsgIdx;
    _menuOpen = false;
    _showHeardBy = true;
  }

  static const char* menuLabel(int act) {
    switch (act) {
      case MSG_ACT_REPLY:  return MECK_TR("Reply", "R\xC3\xA9pondre");
      case MSG_ACT_COPY:   return MECK_TR("Copy to new message", "Copier dans un message");
      case MSG_ACT_PATH:   return MECK_TR("Path", "Chemin");
      case MSG_ACT_HEARD:  return MECK_TR("Repeated by", "R\xC3\xA9p\xC3\xA9t\xC3\xA9 par");
      case MSG_ACT_RESEND: return MECK_TR("Send again", "Renvoyer");
      case MSG_ACT_MAP:    return MECK_TR("Show on map", "Voir sur la carte");
    }
    return "";
  }

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

  // Mark all channels + DMs as read (companion app connected)
  void markAllRead() {
    memset(_unread, 0, sizeof(_unread));
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

  // Message shown in the path view: the one picked from the message menu
  // (or with V in select mode), else the newest received message
  ChannelMessage* pathViewMsg() {
    if (_pathMsgIdx >= 0 && _pathMsgIdx < CHANNEL_MSG_HISTORY_SIZE && _messages[_pathMsgIdx].valid)
      return &_messages[_pathMsgIdx];
    return getNewestReceivedMsg();
  }

  // Format the path of the newest received message as paste-ready text
  // Output: comma-separated hex prefixes e.g. "30, 3b, 9b, 05, e8, 36"
  // Returns length written (0 if no path available)
  int formatPathAsText(char* buf, int bufLen) {
    ChannelMessage* msg = pathViewMsg();
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
  // Update the send status of a tracked sent DM (from MyMesh via UITask).
  // Returns true if a message with this send_ref was found and updated.
  bool setSendStatus(uint32_t send_ref, uint8_t status, uint8_t attempt, uint8_t total) {
    if (send_ref == 0) return false;
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      ChannelMessage* m = &_messages[i];
      if (!m->valid || m->send_ref != send_ref) continue;
      m->dm_status = status;
      m->dm_attempt = attempt;
      m->dm_total = total;
      if (status == DM_SEND_DELIVERED || status == DM_SEND_FAILED) saveRecordToSD(i);   // keep it across restarts
      return true;
    }
    return false;
  }

  // Per-channel history deletion
  // -----------------------------------------------------------------------

  // Clear all stored messages for a specific channel (or all DMs if 0xFF).
  // Invalidates matching slots in the circular buffer and persists to SD.
  // Does NOT alter _newestIdx -- gaps are naturally overwritten as new
  // messages arrive.
  int clearHistoryForChannel(uint8_t channel_idx) {
    int cleared = 0;
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      if (_messages[i].valid && _messages[i].channel_idx == channel_idx) {
        _messages[i].valid = false;
        cleared++;
      }
    }
    if (cleared > 0) {
      // Reset unread counter for the cleared channel
      markChannelRead(channel_idx);
      // Reset scroll if we're viewing the cleared channel
      if (_viewChannelIdx == channel_idx) {
        _scrollPos = 0;
      }
      // Reset DM inbox state if clearing DMs
      if (channel_idx == 0xFF) {
        _dmInboxScroll = 0;
        _dmFilterName[0] = '\0';
        _dmInboxMode = true;
      }
      saveToSD();
      Serial.printf("ChannelScreen: Cleared %d messages for channel %d\n",
                    cleared, (int)channel_idx);
    }
    return cleared;
  }

  // -----------------------------------------------------------------------
  // SD card persistence
  // -----------------------------------------------------------------------

  void fillHeader(MsgFileHeader& hdr) const {
    hdr.magic    = MSG_FILE_MAGIC;
    hdr.version  = MSG_FILE_VERSION;
    hdr.capacity = CHANNEL_MSG_HISTORY_SIZE;
    hdr.count    = (uint16_t)_msgCount;
    hdr.newestIdx = (int16_t)_newestIdx;
  }

  void fillRecord(int i, MsgFileRecord& rec) const {
    rec.timestamp   = _messages[i].timestamp;
    rec.path_len    = _messages[i].path_len;
    rec.channel_idx = _messages[i].channel_idx;
    uint8_t st = _messages[i].dm_status;
    if (st != DM_SEND_DELIVERED && st != DM_SEND_FAILED) st = 0;   // in flight: unknown after a restart
    rec.valid       = _messages[i].valid ? (uint8_t)(1 | (st << 4)) : 0;
    rec.snr         = _messages[i].snr;
    rec.dm_peer_hash = _messages[i].dm_peer_hash;
    memcpy(rec.path, _messages[i].path, MSG_PATH_MAX);
    memcpy(rec.text, _messages[i].text, CHANNEL_MSG_TEXT_LEN);
  }

  void saveNewestRecordToSD() { if (_newestIdx >= 0) saveRecordToSD(_newestIdx); }   // debug "uigo histsave"

  // Save one message slot (and the header) in place: about 200 bytes instead
  // of rewriting the whole ~58 KB file for every new message. Falls back to a
  // full save when the file is missing or not the expected size.
  void saveRecordToSD(int idx) {
#if defined(HAS_SDCARD) && defined(ESP32)
    if (!_sdReady) return;
    const size_t fullSize = sizeof(MsgFileHeader) + (size_t)CHANNEL_MSG_HISTORY_SIZE * sizeof(MsgFileRecord);
    File f = SD.open(MSG_FILE_PATH, "r+");
    if (!f || f.size() != fullSize) {
      if (f) f.close();
      Serial.println("ChannelScreen: history file missing or another size, saving all");
      saveToSD();
      return;
    }
    MsgFileHeader hdr;
    fillHeader(hdr);
    MsgFileRecord rec;
    fillRecord(idx, rec);
    bool ok = f.seek(0) && f.write((uint8_t*)&hdr, sizeof(hdr)) == sizeof(hdr) &&
              f.seek(sizeof(MsgFileHeader) + (size_t)idx * sizeof(MsgFileRecord)) &&
              f.write((uint8_t*)&rec, sizeof(rec)) == sizeof(rec);
    f.close();
    digitalWrite(SDCARD_CS, HIGH);  // Release SD CS
    if (!ok) {
      Serial.println("ChannelScreen: SD record write failed, saving all");
      saveToSD();
    }
#endif
  }

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
    fillHeader(hdr);
    f.write((uint8_t*)&hdr, sizeof(hdr));

    // Write all message slots (including invalid ones - preserves circular buffer layout)
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      MsgFileRecord rec;
      fillRecord(i, rec);
      f.write((uint8_t*)&rec, sizeof(rec));
    }

    f.close();
    digitalWrite(SDCARD_CS, HIGH);  // Release SD CS
#endif
  }


  // Settings > Experimental Features > Delete all contacts: drop every direct
  // message and save, keeping channel messages. The file keeps the circular-
  // buffer layout, so DM slots are marked empty in place. Returns how many
  // were removed, or -1 if the SD card is not ready (nothing could be saved).
  int purgeDMs() {
#if defined(HAS_SDCARD) && defined(ESP32)
    if (!_sdReady) return -1;
    int removed = 0;
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      if (_messages[i].valid && _messages[i].channel_idx == 0xFF) {
        _messages[i].valid = false;
        removed++;
      }
    }
    _unread[MAX_GROUP_CHANNELS] = 0;  // DM unread slot
    saveToSD();
    return removed;
#else
    return -1;
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
      _messages[i].valid       = (rec.valid & 1) != 0;
      _messages[i].snr         = rec.snr;
      _messages[i].dm_peer_hash = rec.dm_peer_hash;
      memcpy(_messages[i].path, rec.path, MSG_PATH_MAX);
      memcpy(_messages[i].text, rec.text, CHANNEL_MSG_TEXT_LEN);
      _messages[i].scope_idx = 0xFF;  // region scope is session-only, not stored on SD
      // Only the final DM send status is stored on SD (in rec.valid)
      _messages[i].send_ref = 0;
      _messages[i].dm_status = (rec.valid >> 4) & 3;
      _messages[i].has_fp = false;
      _messages[i].heard = 0;
      _messages[i].dm_attempt = 0;
      _messages[i].dm_total = 0;
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

  // One row of the DM inbox: a conversation partner and its messages
  #define DM_INBOX_MAX 20
    struct DMInboxEntry {
      uint32_t hash;
      char name[32];
      int msgCount;
      int unreadCount;
      uint32_t newestTs;
    };

  // Collect the DM conversations (grouped by peer, newest first) with
  // their message and unread counts. Returns the number of entries.
  int collectDMInbox(DMInboxEntry* inbox) {
    int inboxCount = 0;

    // Scan all DMs and group by peer hash
    for (int i = 0; i < _msgCount && i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      int idx = _newestIdx - i;
      while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
      idx = idx % CHANNEL_MSG_HISTORY_SIZE;
      if (!_messages[idx].valid || _messages[idx].channel_idx != 0xFF) continue;
      if (_messages[idx].dm_peer_hash == 0) continue;

      uint32_t h = _messages[idx].dm_peer_hash;

      // Find existing entry by hash
      int found = -1;
      for (int j = 0; j < inboxCount; j++) {
        if (inbox[j].hash == h) { found = j; break; }
      }
      if (found < 0 && inboxCount < DM_INBOX_MAX) {
        found = inboxCount++;
        inbox[found].hash = h;
        inbox[found].name[0] = '\0';
        inbox[found].msgCount = 0;
        inbox[found].unreadCount = 0;
        inbox[found].newestTs = 0;

        // Look up name from contacts by matching peer hash
        uint32_t numC = the_mesh.getNumContacts();
        ContactInfo ci;
        for (uint32_t c = 0; c < numC; c++) {
          if (the_mesh.getContactByIdx(c, ci) && peerHash(ci.name) == h) {
            strncpy(inbox[found].name, ci.name, 31);
            inbox[found].name[31] = '\0';
            break;
          }
        }
        // Fallback: extract from text if contact not found
        if (inbox[found].name[0] == '\0') {
          extractSenderName(_messages[idx].text, inbox[found].name, sizeof(inbox[found].name));
        }
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
          if (the_mesh.getContactByIdx(c, ci) && peerHash(ci.name) == inbox[e].hash) {
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

    return inboxCount;
  }

#ifdef MECK_UI_V2
  // ===========================================================================
  // UI v2 conversation (design B5)
  // ===========================================================================

  // Short local time ("19:58") or age ("14m") of a message
  void v2MsgTime(uint32_t ts, char* buf, size_t len) {
    uint32_t now = _rtc->getCurrentTime();
    if (ts > 1700000000 && now > 1700000000) {
      int32_t local = (int32_t)ts + (int32_t)the_mesh.getNodePrefs()->utc_offset_hours * 3600;
      int h = (local / 3600) % 24; if (h < 0) h += 24;
      int m = (local / 60) % 60;   if (m < 0) m += 60;
      snprintf(buf, len, "%02d:%02d", h, m);
    } else {
      Ui2List::formatAge(buf, len, (int32_t)(now - ts));
    }
  }

  // One bubble, laid out but not yet drawn
  struct V2Bubble {
    int msgIdx;          // index into _messages
    int chronoIdx;       // index into the chronological list (reply select)
    bool own;
    int16_t h, w;
    Ui2RtLine lines[6];
    uint8_t nLines;
    bool more;
    const char* body;    // message text after "Sender: "
    int bodyLen;
    char sender[32];
    char alt[64];        // shown instead of a raw location share ("[LOC]...")
  };

  static const int16_t V2_BUBBLE_MAXW = 196;
  static const int16_t V2_TEXT_PAD = 6;

  // Header text of a sent message with no DM send status: a channel message
  // "sent 12:30" (with the repeats heard back "sent 12:30 · heard 2x"), or a
  // DM from history saved before send status was kept: just the time
  static void v2SentMeta(const ChannelMessage* m, const char* t, char* out, size_t len) {
    if (m->channel_idx == 0xFF)      // DM without a known send status (older history)
      snprintf(out, len, "%s", t);
    else if (m->heard > 0)
      snprintf(out, len, MECK_TR("sent %s \xC2\xB7 heard %ux", "envoy\xC3\xA9 %s \xC2\xB7 r\xC3\xA9p\xC3\xA9t\xC3\xA9 %ux"), t, m->heard);
    else
      snprintf(out, len, MECK_TR("sent %s", "envoy\xC3\xA9 %s"), t);
  }

  void v2Layout(V2Bubble& bb, int idx, int chrono) {
    ChannelMessage* m = &_messages[idx];
    bb.msgIdx = idx;
    bb.chronoIdx = chrono;
    bb.sender[0] = 0;
    bb.body = m->text;
    if (extractSenderName(m->text, bb.sender, sizeof(bb.sender))) {
      const char* colon = strstr(m->text, ": ");
      bb.body = colon ? colon + 2 : m->text;
    }
    MeckGeoPoint g;
    if (meckGeoIsShare(bb.body, g)) {
      // "[LOC]50.012345,14.654321" reads as a location line; the message
      // menu opens it on the map
      if (g.waypoint && g.label[0])
        snprintf(bb.alt, sizeof(bb.alt), MECK_TR("Waypoint %s (%.5f, %.5f)", "Point %s (%.5f, %.5f)"), g.label, g.lat, g.lon);
      else
        snprintf(bb.alt, sizeof(bb.alt), MECK_TR("Location %.5f, %.5f", "Position %.5f, %.5f"), g.lat, g.lon);
      bb.body = bb.alt;
    }
    bb.bodyLen = strlen(bb.body);
    bb.own = (m->path_len == 0 && m->snr == 0 && strcmp(bb.sender, the_mesh.getNodePrefs()->node_name) == 0)
             || m->dm_status != DM_SEND_NONE;
    const int16_t textMax = V2_BUBBLE_MAXW - 2 - 2 * V2_TEXT_PAD;
    bb.nLines = ui2RtWrap(bb.body, bb.bodyLen, textMax, UI2_F_BODY, bb.lines, 6, &bb.more);
    if (bb.nLines == 0) { bb.lines[0] = {0, 0, 0}; bb.nLines = 1; }
    int16_t cw = 0;
    for (int k = 0; k < bb.nLines; k++) if (bb.lines[k].w > cw) cw = bb.lines[k].w;
    if (bb.more) cw = textMax;
    int16_t hw = bb.own ? 110 : Ui2Canvas::textWidth(bb.sender, UI2_F_LABEL);
    if (bb.own && m->dm_status == DM_SEND_NONE) {
      // Sent channel message: the header tab holds "You", "sent 12:30 ·
      // heard 2x" and the check icon; widen the bubble so all of it fits
      if (m->has_fp) {
        uint8_t bph = 0, srcs = 0;
        uint8_t n = the_mesh.getHeardBy(m->path, 12, bph, srcs, nullptr, nullptr, 0);
        if (n > m->heard) m->heard = n;   // the sent-track forgets after 10 min
      }
      if (m->heard > 0) {
        char meta[48], t[8];
        v2MsgTime(m->timestamp, t, sizeof(t));
        v2SentMeta(m, t, meta, sizeof(meta));
        int16_t need = Ui2Canvas::textWidth(MECK_TR("You", "Vous"), UI2_F_CHIP) + 6 +
                       Ui2Canvas::textWidth(meta, UI2_F_META) + 15;
        if (need > hw) hw = need;
      }
    }
    if (hw > cw) cw = hw;
    if (!bb.own && cw < 84) cw = 84;      // room for the time / hops line
    bb.w = cw + 2 + 2 * V2_TEXT_PAD;
    if (bb.w > V2_BUBBLE_MAXW) bb.w = V2_BUBBLE_MAXW;
    bb.h = bb.own ? (14 + 2 + bb.nLines * 15 + 3 + 2) : (2 + 15 + bb.nLines * 15 + 15 + 2);
  }

  void v2DrawBubble(Ui2Canvas& c, const V2Bubble& bb, int16_t y, bool sel) {
    using namespace ui2;
    ChannelMessage* m = &_messages[bb.msgIdx];
    const int16_t x = bb.own ? (SCREEN_W - PAD - bb.w) : PAD;
    const int16_t textMax = bb.w - 2 - 2 * V2_TEXT_PAD;
    c.fill(x, y, bb.w, bb.h, false);
    c.roundRect(x, y, bb.w, bb.h, 3, true);
    if (sel) c.roundRect(x + 1, y + 1, bb.w - 2, bb.h - 2, 2, true);
    char meta[40], t[8];
    v2MsgTime(m->timestamp, t, sizeof(t));
    int16_t ty;
    if (bb.own) {
      // Filled header tab: "You", delivery state, state icon
      c.fill(x + 1, y + 1, bb.w - 2, 14, true);
      c.text(x + V2_TEXT_PAD, Ui2Canvas::boxBaseline(UI2_F_CHIP, y + 1, 14), MECK_TR("You", "Vous"), UI2_F_CHIP, false);
      Ui2Icon ic = UI2_NO_ICON;
      if (m->dm_status == DM_SEND_SENDING) {
        snprintf(meta, sizeof(meta), MECK_TR("sending %d/%d", "envoi %d/%d"), m->dm_attempt, m->dm_total);
        ic = UI2_IC_CLOCK;
      } else if (m->dm_status == DM_SEND_DELIVERED) {
        snprintf(meta, sizeof(meta), MECK_TR("delivered %s", "distribu\xC3\xA9 %s"), t);
        ic = UI2_IC_CHECK;
      } else if (m->dm_status == DM_SEND_FAILED) {
        snprintf(meta, sizeof(meta), "%s", MECK_TR("failed", "\xC3\xA9" "chec"));
        ic = UI2_IC_WARN;
      } else {
        // Channel message: repeats heard back (m->heard, set in v2Layout)
        v2SentMeta(m, t, meta, sizeof(meta));
        if (m->heard > 0) ic = UI2_IC_CHECK;
      }
      int16_t rx = x + bb.w - V2_TEXT_PAD;
      if (ic != UI2_NO_ICON) { rx -= 12; c.icon(rx, y + 2, ic, 12, false); rx -= 3; }
      int16_t mw = Ui2Canvas::textWidth(meta, UI2_F_META);
      int16_t lx = x + V2_TEXT_PAD + Ui2Canvas::textWidth(MECK_TR("You", "Vous"), UI2_F_CHIP) + 6;
      if (rx - mw < lx) mw = rx - lx;
      c.textFit(rx - mw, Ui2Canvas::boxBaseline(UI2_F_META, y + 1, 14), mw, meta, UI2_F_META, false, UI2_RIGHT);
      ty = y + 16;
    } else {
      // Sender line (inverted when the bubble is selected for a reply)
      if (sel) c.fill(x + 2, y + 2, bb.w - 4, 15, true);
      c.textFit(x + V2_TEXT_PAD, Ui2Canvas::lineBaseline(UI2_F_LABEL, y + 2, 15), textMax,
                bb.sender[0] ? bb.sender : "?", UI2_F_LABEL, !sel);
      ty = y + 17;
    }
    for (int k = 0; k < bb.nLines; k++) {
      int16_t base = Ui2Canvas::lineBaseline(UI2_F_BODY, ty + k * 15, 15);
      const Ui2RtLine& L = bb.lines[k];
      if (k == bb.nLines - 1 && bb.more) {
        // Last line with text remaining: cut to leave room for "..."
        int16_t ellW = Ui2Canvas::textWidth("\xE2\x80\xA6", UI2_F_BODY);
        int b = L.a; int16_t w = 0;
        while (b < L.b) {
          int16_t tw; int cl = ui2RtToken(bb.body, b, L.b, UI2_F_BODY, &tw);
          if (w + tw + ellW > textMax) break;
          w += tw; b += cl;
        }
        ui2RtDraw(c, x + V2_TEXT_PAD, base, bb.body, L.a, b, UI2_F_BODY);
        c.text(x + V2_TEXT_PAD + w, base, "\xE2\x80\xA6", UI2_F_BODY);
      } else {
        ui2RtDraw(c, x + V2_TEXT_PAD, base, bb.body, L.a, L.b, UI2_F_BODY);
      }
    }
    if (!bb.own) {
      int hops = (m->path_len == 0xFF) ? 0 : (m->path_len & 63);
      char h[16];
      if (m->path_len == 0xFF || hops == 0) snprintf(h, sizeof(h), "%s", MECK_TR("direct", "direct"));
      else if (hops == 1) snprintf(h, sizeof(h), "%s", MECK_TR("1 hop", "1 saut"));
      else snprintf(h, sizeof(h), MECK_TR("%d hops", "%d sauts"), hops);
      snprintf(meta, sizeof(meta), "%s \xC2\xB7 %s", t, h);
      c.textFit(x + V2_TEXT_PAD, Ui2Canvas::lineBaseline(UI2_F_META, ty + bb.nLines * 15 + 1, 12), textMax, meta, UI2_F_META);
    }
  }

  int renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);

    char title[48];
    ChannelDetails channel;
    if (_viewChannelIdx == 0xFF) {
      if (_dmInboxMode) snprintf(title, sizeof(title), "%s", MECK_TR("Direct messages", "Messages priv\xC3\xA9s"));
      else snprintf(title, sizeof(title), "%s", _dmFilterName);
    } else if (the_mesh.getChannel(_viewChannelIdx, channel)) {
      snprintf(title, sizeof(title), "%s", channel.name);
    } else {
      snprintf(title, sizeof(title), MECK_TR("Channel %d", "Canal %d"), _viewChannelIdx);
    }

    // --- Canned messages: a list of the filled slots, tap a row to send ---
    if (_cannedOpen) {
      Ui2Chrome::titleBar(c, title, nullptr, MECK_TR("Quick reply", "R\xC3\xA9ponse rapide"));
      for (uint8_t r = 0; r < _cannedCount && r < ROWS_PER_PAGE; r++) {
        char num[4];
        snprintf(num, sizeof(num), "%u", (unsigned)(_cannedMap[r] + 1));
        Ui2Row row;
        row.title = the_mesh.getNodePrefs()->canned_msgs[_cannedMap[r]];
        row.badge = num;
        Ui2List::drawRow(c, BODY_Y + r * ROW_H, row);
      }
      const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Close", "Fermer")) };
      Ui2Chrome::footer(c, keys, 1);
      return 5000;
    }

    // --- DM inbox: one row per conversation ---
    if (_viewChannelIdx == 0xFF && _dmInboxMode) {
      DMInboxEntry inbox[DM_INBOX_MAX];
      int inboxCount = collectDMInbox(inbox);
      if (_dmInboxScroll >= inboxCount) _dmInboxScroll = inboxCount > 0 ? inboxCount - 1 : 0;
      char right[16];
      Ui2List::pageLabel(right, sizeof(right), _dmInboxScroll, inboxCount, ROWS_PER_PAGE);
      Ui2Chrome::titleBar(c, title, right);
      if (inboxCount == 0) {
        Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_MSG, MECK_TR("No direct messages", "Aucun message priv\xC3\xA9"));
      } else {
        const uint32_t now = _rtc->getCurrentTime();
        const int start = Ui2List::pageStart(_dmInboxScroll, ROWS_PER_PAGE);
        for (int i = start; i < inboxCount && i < start + ROWS_PER_PAGE; i++) {
          char sub[24], age[8], badge[6];
          Ui2Row r;
          r.icon = UI2_IC_CONTACT;
          r.title = inbox[i].name;
          snprintf(sub, sizeof(sub), MECK_TR("%d messages", "%d messages"), inbox[i].msgCount);
          r.sub = sub;
          Ui2List::formatAge(age, sizeof(age), (int32_t)(now - inbox[i].newestTs));
          r.right = age;
          if (inbox[i].unreadCount > 0) { snprintf(badge, sizeof(badge), "%d", inbox[i].unreadCount); r.badge = badge; }
          r.rightMax = 40;
          r.selected = (i == _dmInboxScroll);
          Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
        }
      }
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Open", "Ouvrir")), ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
                              ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 3);
      return 5000;
    }

    // --- Conversation: bubbles, newest at the bottom ---
    static int chrono[CHANNEL_MSG_HISTORY_SIZE];
    int n = 0;
    for (int i = 0; i < _msgCount && n < CHANNEL_MSG_HISTORY_SIZE; i++) {
      int idx = _newestIdx - i;
      while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
      idx %= CHANNEL_MSG_HISTORY_SIZE;
      if (msgMatchesView(_messages[idx])) chrono[n++] = idx;
    }
    for (int l = 0, r = n - 1; l < r; l++, r--) { int t = chrono[l]; chrono[l] = chrono[r]; chrono[r] = t; }
    _replyChannelMsgCount = n;
    _msgsPerPage = 1;   // bubbles have variable height: scroll one message at a time

    char right[12];
    snprintf(right, sizeof(right), "%d", n);
    Ui2Chrome::titleBar(c, title, n ? right : nullptr,
                        _replySelectMode ? MECK_TR("Select", "Choix") : nullptr, UI2_NO_ICON, _replySelectMode);

    if (n == 0) {
      if (_viewChannelIdx == 0xFF) {
        char line[48];
        snprintf(line, sizeof(line), MECK_TR("No messages from %s", "Aucun message de %s"), _dmFilterName);
        Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_MSG, line);
      } else {
        Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_MSG, MECK_TR("No messages yet", "Aucun message"),
                            MECK_TR("Enter writes the first one.", "Entr\xC3\xA9" "e pour \xC3\xA9" "crire le premier."));
      }
    } else {
      if (_scrollPos > n - 1) _scrollPos = n - 1;
      if (_scrollPos < 0) _scrollPos = 0;
      // Keep the reply selection on screen: it never sits below the bottom bubble
      if (_replySelectMode && _replySelectPos >= 0 && _replySelectPos > n - 1 - _scrollPos) _scrollPos = n - 1 - _replySelectPos;

      static V2Bubble bubbles[8];
      int16_t ys[8];
      int count = 0;
      for (int pass = 0; pass < 2; pass++) {
        count = 0;
        int16_t y = FOOTER_Y - 6;
        for (int k = n - 1 - _scrollPos; k >= 0 && count < 8; k--) {
          V2Bubble& bb = bubbles[count];
          v2Layout(bb, chrono[k], k);
          if (y - bb.h < BODY_Y + 4 && count > 0) break;
          y -= bb.h;
          ys[count++] = y;
          y -= 6;
        }
        // Selection above the top bubble: anchor it at the bottom and lay out again
        if (_replySelectMode && _replySelectPos >= 0 && count > 0 &&
            _replySelectPos < bubbles[count - 1].chronoIdx && pass == 0) {
          _scrollPos = n - 1 - _replySelectPos;
          continue;
        }
        break;
      }
      for (int b = count - 1; b >= 0; b--) {
        int16_t y = ys[b] < BODY_Y + 2 ? BODY_Y + 2 : ys[b];
        v2DrawBubble(c, bubbles[b], y, _replySelectMode && bubbles[b].chronoIdx == _replySelectPos);
      }
      // The title bar is drawn again over a bubble that does not fully fit
      if (count > 0 && ys[count - 1] < BODY_Y + 2) {
        c.fill(0, TITLE_Y, SCREEN_W, TITLE_H, false);
        Ui2Chrome::titleBar(c, title, right, _replySelectMode ? MECK_TR("Select", "Choix") : nullptr, UI2_NO_ICON, _replySelectMode);
      }
    }

    if (_replySelectMode) {
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Options", "Options")), ui2Key("W/S", MECK_TR("Select", "Choisir"), true),
                              ui2Key("Q", MECK_TR("Cancel", "Annuler")) };
      Ui2Chrome::footer(c, keys, 3);
    } else if (_viewChannelIdx == 0xFF && _dmContactPerms > 0) {
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Reply", "R\xC3\xA9pondre")), ui2Key("R", MECK_TR("Select", "Choisir")),
                              ui2Key("L", "Admin"), ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 4);
    } else if (_viewChannelIdx == 0xFF) {
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Reply", "R\xC3\xA9pondre")), ui2Key("R", MECK_TR("Select", "Choisir")),
                              ui2Key("W/S", MECK_TR("Scroll", "D\xC3\xA9" "filer"), true), ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 4);
    } else {
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Write", "\xC3\x89" "crire")), ui2Key("R", MECK_TR("Select", "Choisir")),
                              ui2Key("W/S", MECK_TR("Scroll", "D\xC3\xA9" "filer"), true), ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 4);
    }
    return 5000;
  }

  // Canned overlay tap in physical pixels: a row tap queues that slot for
  // the main loop to send; any other tap just closes the overlay.
  // UI v2 path view for the newest received message: its text, age, route,
  // region and SNR as label/value lines, then one row per hop with the
  // contact it resolves to (repeaters first)
  int renderPathV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    ChannelMessage* msg = pathViewMsg();
    uint8_t plen = msg ? msg->path_len : 0;
    int hops = (msg && plen != 0xFF) ? (plen & 63) : 0;
    int bph = (plen >> 6) + 1;
    char right[16] = "";
    if (hops > 0) snprintf(right, sizeof(right), MECK_TR("%d hop%s", "%d saut%s"), hops, hops == 1 ? "" : "s");
    Ui2Chrome::titleBar(c, MECK_TR("Message path", "Chemin du message"), right[0] ? right : nullptr);
    if (!msg) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_TRACE, MECK_TR("No received messages", "Aucun message re\xC3\xA7u"), nullptr);
      const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 1);
      return 5000;
    }

    int16_t y = BODY_Y + 4;
    // The message, up to two lines
    Ui2RtLine lines[2];
    bool more = false;
    int len = strlen(msg->text);
    int n = ui2RtWrap(msg->text, len, SCREEN_W - 2 * PAD, UI2_F_BODY, lines, 2, &more);
    for (int i = 0; i < n; i++) {
      ui2RtDraw(c, PAD, y + 13, msg->text, lines[i].a, lines[i].b, UI2_F_BODY);
      y += 16;
    }
    y += 4;
    char age[12], val[40];
    Ui2List::formatAge(age, sizeof(age), (int32_t)(_rtc->getCurrentTime() - msg->timestamp));
    Ui2List::kvRow(c, y, MECK_TR("Received", "Re\xC3\xA7u"), age);  y += 20;
    if (plen == 0xFF) snprintf(val, sizeof(val), "%s", MECK_TR("Direct", "Directe"));
    else if (plen == 0) snprintf(val, sizeof(val), "%s", MECK_TR("Local / sent", "Locale / envoy\xC3\xA9"));
    else snprintf(val, sizeof(val), MECK_TR("Flood \xC2\xB7 %d-byte hashes", "Flood \xC2\xB7 hash %d o"), bph);
    Ui2List::kvRow(c, y, "Route", val);  y += 20;
    const char* rgn = the_mesh.getScopeName(msg->scope_idx);
    if (rgn) { Ui2List::kvRow(c, y, MECK_TR("Region", "R\xC3\xA9gion"), rgn); y += 20; }
    if (msg->snr != 0) {
      snprintf(val, sizeof(val), "%.2f dB", msg->snr / 4.0f);
      Ui2List::kvRow(c, y, "SNR", val);
      y += 20;
    }

    if (hops > 0) {
      y += 4;
      c.hline(0, y, SCREEN_W);
      const int16_t rowH = 32;
      int visible = (FOOTER_Y - y - 1) / rowH;
      if (visible < 1) visible = 1;
      _pathHopsVisible = visible;
      if (_pathScrollPos > hops - visible) _pathScrollPos = hops - visible;
      if (_pathScrollPos < 0) _pathScrollPos = 0;
      for (int h = _pathScrollPos; h < hops && h < _pathScrollPos + visible; h++) {
        const uint8_t* hb = &msg->path[h * bph];
        char hex[8], name[33] = "", sub[16];
        if (bph == 1) snprintf(hex, sizeof(hex), "%02X", hb[0]);
        else if (bph == 2) snprintf(hex, sizeof(hex), "%02X%02X", hb[0], hb[1]);
        else snprintf(hex, sizeof(hex), "%02X%02X%02X", hb[0], hb[1], hb[2]);
        // Resolve the hash: repeaters first, then any contact
        uint8_t type = 0;
        ContactInfo ci;
        int numContacts = the_mesh.getNumContacts();
        for (int pass = 0; pass < 2 && !name[0]; pass++) {
          for (int k = 0; k < numContacts; k++) {
            if (the_mesh.getContactByIdx(k, ci) && memcmp(ci.id.pub_key, hb, bph) == 0 &&
                (pass == 1 || ci.type == ADV_TYPE_REPEATER)) {
              snprintf(name, sizeof(name), "%s", ci.name);
              type = ci.type;
              break;
            }
          }
        }
        snprintf(sub, sizeof(sub), MECK_TR("Hop %d", "Saut %d"), h + 1);
        Ui2Row r;
        r.icon = name[0] ? ui2NodeIcon(type) : UI2_IC_REPEATER;
        r.title = name[0] ? name : MECK_TR("Unknown node", "N\xC5\x93ud inconnu");
        r.sub = sub;
        r.right = hex;
        r.height = rowH;
        Ui2List::drawRow(c, y + 1 + (h - _pathScrollPos) * rowH, r);
      }
    }
    const bool scroll = hops > _pathHopsVisible;
    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Copy", "Copier")),
      ui2Key("W/S", MECK_TR("Scroll", "D\xC3\xA9" "filer"), true),
      ui2Key("Q", MECK_TR("Back", "Retour")),
    };
    if (scroll) Ui2Chrome::footer(c, keys, 3);
    else { const Ui2Key k2[] = { keys[0], keys[2] }; Ui2Chrome::footer(c, k2, 2); }
    return 5000;
  }

  void cannedTapAtPx(int py) {
    int row = Ui2List::rowAt(py, ui2::BODY_Y, ui2::ROW_H, ui2::ROWS_PER_PAGE);
    if (row >= 0 && row < (int)_cannedCount) _cannedPendingSlot = _cannedMap[row];
    _cannedOpen = false;
  }
#endif

#ifdef MECK_UI_V2
  // Name of the contact whose public key starts with hb[0..bph), repeaters
  // first; false when none matches
  static bool resolveHashName(const uint8_t* hb, int bph, char* name, size_t len) {
    ContactInfo ci;
    int numContacts = the_mesh.getNumContacts();
    for (int pass = 0; pass < 2; pass++) {
      for (int k = 0; k < numContacts; k++) {
        if (the_mesh.getContactByIdx(k, ci) && memcmp(ci.id.pub_key, hb, bph) == 0 &&
            (pass == 1 || ci.type == ADV_TYPE_REPEATER)) {
          snprintf(name, len, "%s", ci.name);
          return true;
        }
      }
    }
    return false;
  }

  // Message menu: the message, then one row per action
  int renderMenuV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    ChannelMessage* m = menuMsg();
    if (!m) {
      _menuOpen = false;
      Ui2Chrome::titleBar(c, MECK_TR("Message", "Message"));
      return 100;
    }
    bool own = msgIsOwn(*m);
    char sender[32] = "", t[8];
    extractSenderName(m->text, sender, sizeof(sender));
    v2MsgTime(m->timestamp, t, sizeof(t));
    Ui2Chrome::titleBar(c, own ? MECK_TR("Your message", "Votre message")
                               : (sender[0] ? sender : MECK_TR("Message", "Message")), t);

    // The message, up to three lines
    int16_t y = BODY_Y + 4;
    const char* body = msgBody(m);
    Ui2RtLine lines[3];
    bool more = false;
    int n = ui2RtWrap(body, strlen(body), SCREEN_W - 2 * PAD, UI2_F_BODY, lines, 3, &more);
    for (int i = 0; i < n; i++) {
      int16_t x = ui2RtDraw(c, PAD, y + 13, body, lines[i].a, lines[i].b, UI2_F_BODY);
      if (more && i == n - 1) c.text(x + 2, y + 13, "...", UI2_F_BODY);
      y += 16;
    }
    y += 6;
    c.hline(0, y, SCREEN_W);
    y += 1;

    bool dm = (m->channel_idx == 0xFF);
    for (int i = 0; i < _menuCount && y + ROW_H <= FOOTER_Y; i++) {
      Ui2Row r;
      switch (_menuItems[i]) {
        case MSG_ACT_REPLY:  r.icon = UI2_IC_MSG; break;
        case MSG_ACT_COPY:   r.icon = UI2_IC_EDIT; break;
        case MSG_ACT_PATH:   r.icon = UI2_IC_TRACE; break;
        case MSG_ACT_HEARD:  r.icon = UI2_IC_REPEATER; break;
        case MSG_ACT_RESEND: r.icon = UI2_IC_REFRESH; break;
        case MSG_ACT_MAP:    r.icon = UI2_IC_MAP; break;
      }
      r.title = menuLabel(_menuItems[i]);
      char sub[32] = "";
      if (_menuItems[i] == MSG_ACT_REPLY && !dm && sender[0]) {
        snprintf(sub, sizeof(sub), "@%s", sender);
        r.sub = sub;
      } else if (_menuItems[i] == MSG_ACT_HEARD && m->heard > 0) {
        snprintf(sub, sizeof(sub), MECK_TR("%ux", "%ux"), m->heard);
        r.right = sub;
      }
      r.selected = (i == _menuSel);
      r.chevron = (_menuItems[i] == MSG_ACT_PATH || _menuItems[i] == MSG_ACT_HEARD || _menuItems[i] == MSG_ACT_MAP);
      Ui2List::drawRow(c, y, r);
      y += ROW_H;
    }

    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Select", "Choisir")),
                            ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
                            ui2Key("Q", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 3);
    return 5000;
  }

  // Repeaters heard repeating a sent channel message, with their SNR. The
  // sources are kept for 10 minutes after sending; after that only the count.
  int renderHeardV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    ChannelMessage* m = (_heardMsgIdx >= 0 && _heardMsgIdx < CHANNEL_MSG_HISTORY_SIZE &&
                         _messages[_heardMsgIdx].valid) ? &_messages[_heardMsgIdx] : nullptr;
    uint8_t bph = 0, cnt = 0;
    uint8_t hashes[SENT_ECHO_MAX * 2];
    int8_t snrs[SENT_ECHO_MAX];
    if (m && m->has_fp) {
      uint8_t total = the_mesh.getHeardBy(m->path, 12, bph, cnt, hashes, snrs, SENT_ECHO_MAX);
      if (total > m->heard) m->heard = total;
    }
    char right[12] = "";
    if (m && m->heard > 0) snprintf(right, sizeof(right), "%ux", m->heard);
    Ui2Chrome::titleBar(c, MECK_TR("Repeated by", "R\xC3\xA9p\xC3\xA9t\xC3\xA9 par"), right[0] ? right : nullptr);

    if (cnt == 0 || bph == 0) {
      bool some = m && m->heard > 0;
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_REPEATER,
                          some ? MECK_TR("Repeats heard", "R\xC3\xA9p\xC3\xA9titions entendues")
                               : MECK_TR("No repeats heard yet", "Aucune r\xC3\xA9p\xC3\xA9tition"),
                          some ? MECK_TR("Which repeaters is kept for 10 minutes after sending.",
                                         "Le d\xC3\xA9tail est gard\xC3\xA9 10 minutes apr\xC3\xA8s l'envoi.")
                               : MECK_TR("Only repeaters this device can hear are counted.",
                                         "Seuls les r\xC3\xA9p\xC3\xA9teurs \xC3\xA0 port\xC3\xA9" "e sont compt\xC3\xA9s."));
    } else {
      int16_t y = BODY_Y;
      for (int e = 0; e < cnt && y + ROW_H <= FOOTER_Y; e++) {
        const uint8_t* hb = &hashes[e * bph];
        char hex[8], name[33] = "", snr[16];
        if (bph == 1) snprintf(hex, sizeof(hex), "%02X", hb[0]);
        else snprintf(hex, sizeof(hex), "%02X%02X", hb[0], hb[1]);
        bool known = resolveHashName(hb, bph, name, sizeof(name));
        snprintf(snr, sizeof(snr), "%.1f dB", snrs[e] / 4.0f);
        Ui2Row r;
        r.icon = UI2_IC_REPEATER;
        r.title = known ? name : hex;
        r.sub = known ? hex : MECK_TR("Unknown repeater", "R\xC3\xA9p\xC3\xA9teur inconnu");
        r.right = snr;
        Ui2List::drawRow(c, y, r);
        y += ROW_H;
      }
    }
    const Ui2Key keys[] = { ui2Key("Q", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 1);
    return 5000;
  }
#endif

  int render(DisplayDriver& display) override {
#ifdef MECK_UI_V2
    if (_showHeardBy) return renderHeardV2(display);
    if (_menuOpen) return renderMenuV2(display);
    return _showPathOverlay ? renderPathV2(display) : renderV2(display);
#endif
    char tmp[40];
    
    // Header - show current channel name
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    
    // Get channel name
    ChannelDetails channel;
    if (_viewChannelIdx == 0xFF) {
      if (_dmInboxMode) {
        display.print(MECK_TR("Direct Messages", "Messages priv\xC3\xA9s"));
      } else {
        char hdr[40];
        snprintf(hdr, sizeof(hdr), MECK_TR("DM: %s", "MP : %s"), _dmFilterName);
        display.print(hdr);
      }
    } else if (the_mesh.getChannel(_viewChannelIdx, channel)) {
      display.print(channel.name);
    } else {
      sprintf(tmp, MECK_TR("Channel %d", "Canal %d"), _viewChannelIdx);
      display.print(tmp);
    }
    
    // Message count for this channel on right
    int channelMsgCount = getMessageCountForChannel();
    sprintf(tmp, "[%d]", channelMsgCount);
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);
    
    // Divider line
    display.drawRect(0, 11, display.width(), 1);

    // === Canned messages overlay: takes over the body while open ===
    // The channel/DM header above stays visible, showing the send target.
    if (_cannedOpen) {
      display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
      int lineH = the_mesh.getNodePrefs()->smallLineH();
      int y = 14;
      for (uint8_t r = 0; r < _cannedCount; r++) {
        display.setCursor(0, y);
        display.setColor(DisplayDriver::LIGHT);
        snprintf(tmp, sizeof(tmp), "%u: %.26s", (unsigned)(_cannedMap[r] + 1),
                 the_mesh.getNodePrefs()->canned_msgs[_cannedMap[r]]);
        display.print(tmp);
        y += lineH;
      }
#ifdef USE_EINK
      return 5000;
#else
      return 1000;
#endif
    }

    // === DM Inbox mode: show list of contacts with DMs ===
    if (_viewChannelIdx == 0xFF && _dmInboxMode) {
      DMInboxEntry inbox[DM_INBOX_MAX];
      int inboxCount = collectDMInbox(inbox);

      // Render inbox list
      display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
      int lineH = the_mesh.getNodePrefs()->smallLineH();
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
        display.print(MECK_TR("No direct messages", "Aucun message priv\xC3\xA9"));
        display.setCursor(0, y + lineH);
        display.print(MECK_TR("A/D: Switch channel", "A/D : changer de canal"));
      } else {
        int startIdx = max(0, min(_dmInboxScroll - maxVisible / 2,
                                  inboxCount - maxVisible));
        int endIdx = min(inboxCount, startIdx + maxVisible);

        for (int i = startIdx; i < endIdx && y + lineH <= maxY; i++) {
          bool selected = (i == _dmInboxScroll);

          if (selected) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), display.width(), lineH);
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
          else if (age < 3600) snprintf(ageStr, sizeof(ageStr), MECK_TR("%dm", "%dmin"), age / 60);
          else if (age < 86400) snprintf(ageStr, sizeof(ageStr), "%dh", age / 3600);
          else snprintf(ageStr, sizeof(ageStr), MECK_TR("%dd", "%dj"), age / 86400);

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
      display.setCursor(0, footerY);
      display.print(MECK_TR("Q:Bck", "Q:Ret"));
      const char* rtInbox = MECK_TR("Ent:Open", "Ent:Ouvrir");
      display.setCursor(display.width() - display.getTextWidth(rtInbox) - 6, footerY);
      display.print(rtInbox);

#ifdef USE_EINK
      return 5000;
#else
      return 1000;
#endif
    }
    
    // --- Path detail overlay ---
    if (_showPathOverlay) {
      display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
      int lineH = the_mesh.getNodePrefs()->smallLineH();
      int y = 14;
      
      ChannelMessage* msg = pathViewMsg();
      if (!msg) {
        display.setCursor(0, y);
        display.setColor(DisplayDriver::LIGHT);
        display.print(MECK_TR("No received messages", "Aucun message re\xC3\xA7u"));
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
        if (age < 60) sprintf(tmp, MECK_TR("Age: %ds", "\xC3\x82ge : %ds"), age);
        else if (age < 3600) sprintf(tmp, MECK_TR("Age: %dm", "\xC3\x82ge : %dmin"), age / 60);
        else if (age < 86400) sprintf(tmp, MECK_TR("Age: %dh", "\xC3\x82ge : %dh"), age / 3600);
        else sprintf(tmp, MECK_TR("Age: %dd", "\xC3\x82ge : %dj"), age / 86400);
        display.print(tmp);
        y += lineH;
        
        // Route type
        display.setCursor(0, y);
        uint8_t plen = msg->path_len;
        uint8_t hopCount = plen & 63;           // extract hop count from encoded path_len
        uint8_t bytesPerHop = (plen >> 6) + 1;  // 1, 2, or 3 bytes per hop
        if (plen == 0xFF) {
          display.setColor(DisplayDriver::LIGHT);
          display.print(MECK_TR("Route: Direct", "Route : directe"));
        } else if (plen == 0) {
          display.setColor(DisplayDriver::LIGHT);
          display.print(MECK_TR("Route: Local/Sent", "Route : locale/envoy\xC3\xA9"));
        } else {
          display.setColor(DisplayDriver::GREEN);
          sprintf(tmp, MECK_TR("Route: %d hop%s (%d-byte)", "Route : %d saut%s (%d o)"), hopCount, hopCount == 1 ? "" : "s", bytesPerHop);
          display.print(tmp);
        }
        y += lineH;

        // Region (scoped channel messages only; session, not persisted)
        const char* rgn = the_mesh.getScopeName(msg->scope_idx);
        if (rgn) {
          display.setCursor(0, y);
          display.setColor(DisplayDriver::YELLOW);
          sprintf(tmp, MECK_TR("Region: %s", "R\xC3\xA9gion : %s"), rgn);
          display.print(tmp);
          y += lineH;
        }

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
      display.print(MECK_TR("Q:Back", "Q:Ret"));
      // Show scroll hint if path is scrollable
      if (msg && (msg->path_len & 63) > _pathHopsVisible && msg->path_len != 0xFF) {
        const char* scrollHint = MECK_TR("W/S:Scrl", "W/S:D\xC3\xA9" "f.");
        int scrollW = display.getTextWidth(scrollHint);
        display.setCursor((display.width() - scrollW) / 2, footerY);
        display.print(scrollHint);
      }
      const char* copyHint = MECK_TR("Ent:Copy", "Ent:Copie");
      display.setCursor(display.width() - display.getTextWidth(copyHint) - 2, footerY);
      display.print(copyHint);

#ifdef USE_EINK
      return 5000;
#else
      return 1000;
#endif
    }
    
    if (channelMsgCount == 0) {
      display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());  // Tiny font for body text
      display.setCursor(0, 20);
      display.setColor(DisplayDriver::LIGHT);
      if (_viewChannelIdx == 0xFF) {
        char noMsg[48];
        snprintf(noMsg, sizeof(noMsg), MECK_TR("No messages from %s", "Aucun message de %s"), _dmFilterName);
        display.print(noMsg);
        display.setCursor(0, 30);
        display.print(MECK_TR("Q: Back to inbox", "Q : retour \xC3\xA0 la liste"));
        display.setCursor(0, 40);
        display.print(MECK_TR("Ent: Compose reply", "Ent : r\xC3\xA9pondre"));
      } else {
        display.print(MECK_TR("No messages yet", "Aucun message"));
        display.setCursor(0, 30);
        display.print(MECK_TR("A/D: Switch channel", "A/D : changer de canal"));
        display.setCursor(0, 40);
        display.print(MECK_TR("C: Compose message", "C : \xC3\xA9" "crire un message"));
      }
      display.setTextSize(1);  // Restore for footer
    } else if (_viewChannelIdx == 0xFF && _dmInboxMode) {
      // =================================================================
      // DM Inbox: list of contacts/rooms you have DM history with
      // =================================================================
      display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
      int lineHeight = the_mesh.getNodePrefs()->smallLineH();
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
        display.print(MECK_TR("No conversations", "Aucune conversation"));
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
            display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), display.width(), lineHeight);
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
          else if (age < 3600) snprintf(ageStr, sizeof(ageStr), MECK_TR("%dm", "%dmin"), age / 60);
          else if (age < 86400) snprintf(ageStr, sizeof(ageStr), "%dh", age / 3600);
          else snprintf(ageStr, sizeof(ageStr), MECK_TR("%dd", "%dj"), age / 86400);

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
      display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());  // Tiny font for message body
      int lineHeight = the_mesh.getNodePrefs()->smallLineH();   // 8px font + 1px spacing
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
          display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), contentW, maxFillH);
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
            sprintf(tmp, MECK_TR(">%dm ", ">%dmin "), age / 60);
          } else if (age < 86400) {
            sprintf(tmp, ">%dh ", age / 3600);
          } else {
            sprintf(tmp, MECK_TR(">%dd ", ">%dj "), age / 86400);
          }
        } else if (msg->dm_status == DM_SEND_SENDING) {
          // Tracked sent DM still in flight -- show the attempt counter
          sprintf(tmp, MECK_TR("Sending %d/%d ", "Envoi %d/%d "), msg->dm_attempt, msg->dm_total);
        } else if (msg->dm_status == DM_SEND_DELIVERED) {
          // Recipient's ack received
          sprintf(tmp, MECK_TR("Delivered ", "Distribu\xC3\xA9 "));
        } else if (msg->dm_status == DM_SEND_FAILED) {
          // All attempts exhausted without an ack from the recipient
          sprintf(tmp, MECK_TR("Failed ", "\xC3\x89" "chec "));
        } else {
          int hopsDisp = (msg->path_len == 0xFF) ? 0 : (msg->path_len & 63);
          // Byte mode: flood packets encode it in the upper bits of path_len.
          // The sentinels (0xFF direct-received, 0 locally-sent) do not encode it,
          // so fall back to this device's configured path hash size.
          int bphDisp = (msg->path_len == 0xFF || msg->path_len == 0)
                          ? (the_mesh.getNodePrefs()->path_hash_mode + 1)
                          : ((msg->path_len >> 6) + 1);
          if (age < 60) {
            sprintf(tmp, MECK_TR("(%dh)(%db) %ds ", "(%dh)(%do) %ds "), hopsDisp, bphDisp, age);
          } else if (age < 3600) {
            sprintf(tmp, MECK_TR("(%dh)(%db) %dm ", "(%dh)(%do) %dmin "), hopsDisp, bphDisp, age / 60);
          } else if (age < 86400) {
            sprintf(tmp, MECK_TR("(%dh)(%db) %dh ", "(%dh)(%do) %dh "), hopsDisp, bphDisp, age / 3600);
          } else {
            sprintf(tmp, MECK_TR("(%dh)(%db) %dd ", "(%dh)(%do) %dj "), hopsDisp, bphDisp, age / 86400);
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

          // --- UTF-8 lead byte detection ---
          // Must check BEFORE isEmojiEscape() because lead bytes 0xC2-0xCC
          // overlap the emoji escape range (0x80-0xCC). A byte >= 0xC2
          // followed by a continuation byte (0x80-0xBF) is always UTF-8.
          bool isUtf8 = (b >= 0xC2 && pos + 1 < textLen &&
                         ((uint8_t)msg->text[pos + 1] & 0xC0) == 0x80);

          // Word wrap: when starting a new text word, check if it fits
          if (b != ' ' && (isUtf8 || !isEmojiEscape(b)) && px > 0) {
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
              for (int j = pos; j < textLen; ) {
                uint8_t wb = (uint8_t)msg->text[j];
                if (wb == EMOJI_PAD_BYTE) { j++; continue; }
                if (wb == ' ') break;
                // Check for UTF-8 lead byte in word scan
                bool wbUtf8 = (wb >= 0xC2 && j + 1 < textLen &&
                               ((uint8_t)msg->text[j + 1] & 0xC0) == 0x80);
                if (!wbUtf8 && isEmojiEscape(wb)) break;
                if (wbUtf8) {
                  int clen = (wb < 0xE0) ? 2 : (wb < 0xF0) ? 3 : 4;
                  int actual = (j + clen <= textLen) ? clen : textLen - j;
                  char mbuf[5] = {0};
                  memcpy(mbuf, &msg->text[j], actual);
                  int charAdv = display.getTextWidth(mbuf);
                  if (charAdv < 1) charAdv = 1;
                  wordW += charAdv;
                  j += actual;
                } else {
                  charStr[0] = (char)wb;
                  dblStr[0] = dblStr[1] = (char)wb;
                  int charAdv = display.getTextWidth(dblStr) - display.getTextWidth(charStr);
                  if (charAdv < 1) charAdv = 1;
                  wordW += charAdv;
                  j++;
                }
              }
              if (px + wordW > lineW) {
                px = 0;
                linesForThisMsg++;
                y += lineHeight;
                if (linesForThisMsg >= maxLinesPerMsg || y + lineHeight > maxY) break;
              }
            }
          }
          
          // --- Render: UTF-8 multi-byte character ---
          if (isUtf8) {
            int clen = (b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4;
            int actual = (pos + clen <= textLen) ? clen : textLen - pos;
            char mbuf[5] = {0};
            memcpy(mbuf, &msg->text[pos], actual);
            int adv = display.getTextWidth(mbuf);
            if (adv < 1) adv = 1;
            if (px + adv > lineW) {
              px = 0;
              linesForThisMsg++;
              y += lineHeight;
              if (linesForThisMsg >= maxLinesPerMsg || y + lineHeight > maxY) break;
            }
            display.setCursor(px, y);
            display.print(mbuf);
            px += adv;
            pos += actual;
          } else if (isEmojiEscape(b)) {
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
            display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), contentW, maxFillH - usedH);
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
    
    // Left side: abbreviated controls
    if (_replySelectMode) {
      display.print(MECK_TR("W/S:Sel V:Pth Q:X", "W/S:S\xC3\xA9l V:Chem Q:X"));
      const char* rightText = MECK_TR("Ent:Reply", "Ent:R\xC3\xA9p");
      display.setCursor(display.width() - display.getTextWidth(rightText) - 2, footerY);
      display.print(rightText);
    } else if (_viewChannelIdx == 0xFF) {
      if (_dmContactPerms > 0) {
        display.print(MECK_TR("Q:Exit L:Admin", "Q:Quitter L:Admin"));
      } else {
        display.print(MECK_TR("Q:Exit", "Q:Quitter"));
      }
      const char* rightText = MECK_TR("Ent:Reply", "Ent:R\xC3\xA9p");
      display.setCursor(display.width() - display.getTextWidth(rightText) - 2, footerY);
      display.print(rightText);
    } else {
      display.print(MECK_TR("Q:Bck R:Rply", "Q:Ret R:R\xC3\xA9p"));
      const char* rightText = MECK_TR("Ent:New", "Ent:Nouveau");
      display.setCursor(display.width() - display.getTextWidth(rightText) - 2, footerY);
      display.print(rightText);
    }

#ifdef USE_EINK
    return 5000;
#else
    return 1000;
#endif
  }

  // Vertical-swipe scroll (watch). Pages the normal message view a screenful at
  // a time; keeps single-step in reply-select / path-overlay / DM-inbox so
  // precise selection isn't lost. Bounds clamping is handled by handleInput.
  void pageScroll(bool older) {
    char k = older ? 'w' : 's';
    bool normalView = !_replySelectMode && !_showPathOverlay && !_dmInboxMode;
    int steps = (normalView && _msgsPerPage > 1) ? _msgsPerPage : 1;
    for (int i = 0; i < steps; i++) handleInput(k);
  }

  bool handleInput(char c) override {
    // Canned overlay open: any key closes it (mirrors the path overlay)
    if (_cannedOpen) {
      _cannedOpen = false;
      return true;
    }
    // "Repeated by" view: Back or Enter returns to the selection
    if (_showHeardBy) {
      if (c == KEY_CANCEL || c == '\r' || c == 13) {
        _showHeardBy = false;
        _heardMsgIdx = -1;
      }
      return true;
    }
    // Message menu: W/S move, Back closes, Enter runs the action (main.cpp)
    if (_menuOpen) {
      if (c == KEY_CANCEL) { _menuOpen = false; return true; }
      if (c == 'w' || c == 'W' || c == 0xF2) { if (_menuSel > 0) _menuSel--; return true; }
      if (c == 's' || c == 'S' || c == 0xF1) { if (_menuSel < _menuCount - 1) _menuSel++; return true; }
      if (c == '\r' || c == 13) return false;
      return true;
    }
    // If overlay is showing, handle scroll and dismiss
    if (_showPathOverlay) {
      if (c == KEY_CANCEL || c == 'v' || c == 'V') {
        _showPathOverlay = false;
        _pathScrollPos = 0;
        _pathMsgIdx = -1;
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
        ChannelMessage* msg = pathViewMsg();
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
      // Shift+Del - exit reply select
      if (c == KEY_CANCEL) {
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
        ChannelMessage* selMsg = getReplySelectMsg();
        if (selMsg && selMsg->path_len != 0) {
          _pathMsgIdx = (int)(selMsg - _messages);
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
      // Enter - open conversation for selected entry
      if (c == '\r' || c == 13) {
        // Rebuild inbox by hash to find the selected entry
        uint32_t seenHash[DM_INBOX_MAX];
        int cur = 0;
        for (int i = 0; i < _msgCount; i++) {
          int idx = _newestIdx - i;
          while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
          idx = idx % CHANNEL_MSG_HISTORY_SIZE;
          if (!_messages[idx].valid || _messages[idx].channel_idx != 0xFF) continue;
          if (_messages[idx].dm_peer_hash == 0) continue;

          uint32_t h = _messages[idx].dm_peer_hash;
          bool dup = false;
          for (int k = 0; k < cur; k++) {
            if (seenHash[k] == h) { dup = true; break; }
          }
          if (dup) continue;
          if (cur < DM_INBOX_MAX) seenHash[cur] = h;

          if (cur == _dmInboxScroll) {
            // Found the selected entry — look up name from contacts
            _dmFilterName[0] = '\0';
            _dmContactIdx = -1;
            _dmContactPerms = 0;
            uint32_t numC = the_mesh.getNumContacts();
            ContactInfo ci;
            for (uint32_t c2 = 0; c2 < numC; c2++) {
              if (the_mesh.getContactByIdx(c2, ci) && peerHash(ci.name) == h) {
                strncpy(_dmFilterName, ci.name, sizeof(_dmFilterName) - 1);
                _dmFilterName[sizeof(_dmFilterName) - 1] = '\0';
                _dmContactIdx = (int)c2;
                break;
              }
            }
            // Fallback to text extraction if contact not found
            if (_dmFilterName[0] == '\0') {
              extractSenderName(_messages[idx].text, _dmFilterName, sizeof(_dmFilterName));
            }
            _dmInboxMode = false;
            _scrollPos = 0;
            return true;
          }
          cur++;
        }
        return true;
      }
      // Shift+Del - let main.cpp handle (back to home)
      if (c == KEY_CANCEL) {
        return false;
      }
      // A/D pass through to channel switching below
      if (c == 'a' || c == 'A' || c == 'd' || c == 'D') {
        // Fall through to channel switching
      } else {
        return true;  // Consume other keys
      }
    }

    // --- DM Conversation mode: Shift+Del goes back to inbox ---
    if (_viewChannelIdx == 0xFF && !_dmInboxMode) {
      if (c == KEY_CANCEL) {
        _dmInboxMode = true;
        _dmFilterName[0] = '\0';
        _scrollPos = 0;
        return true;
      }
    }

    int channelMsgCount = getMessageCountForChannel();

    // R - enter reply select mode (group channels only — DM tab uses Enter to reply)
    if (c == 'r' || c == 'R') {
#ifdef MECK_UI_V2
      // DM conversation: R selects a message for the message menu
      if (_viewChannelIdx == 0xFF && _dmInboxMode) return false;
#else
      if (_viewChannelIdx == 0xFF) return false;  // Not applicable on DM tab
#endif
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
        // Skip backwards over any empty/gap slots
        uint8_t prev = _viewChannelIdx - 1;
        bool found = false;
        while (true) {
          ChannelDetails ch;
          if (the_mesh.getChannel(prev, ch) && ch.name[0] != '\0') {
            _viewChannelIdx = prev;
            found = true;
            break;
          }
          if (prev == 0) break;
          prev--;
        }
        if (!found) {
          // No valid channel below → wrap to DM tab
          _viewChannelIdx = 0xFF;
          _dmInboxMode = true;
          _dmInboxScroll = 0;
          _dmFilterName[0] = '\0';
        }
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
        // Skip forward over any empty/gap slots
        bool found = false;
        for (uint8_t next = _viewChannelIdx + 1; next < MAX_GROUP_CHANNELS; next++) {
          ChannelDetails ch;
          if (the_mesh.getChannel(next, ch) && ch.name[0] != '\0') {
            _viewChannelIdx = next;
            found = true;
            break;
          }
        }
        if (!found) {
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