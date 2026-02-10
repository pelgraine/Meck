#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include "EmojiSprites.h"

// SD card message persistence
#if defined(HAS_SDCARD) && defined(ESP32)
  #include <SD.h>
#endif

// Maximum messages to store in history
#define CHANNEL_MSG_HISTORY_SIZE 20
#define CHANNEL_MSG_TEXT_LEN 160

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 20
#endif

// ---------------------------------------------------------------------------
// On-disk format for message persistence (SD card)
// ---------------------------------------------------------------------------
#define MSG_FILE_MAGIC   0x4D434853  // "MCHS" - MeshCore History Store
#define MSG_FILE_VERSION 1
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
  uint8_t  reserved;
  char     text[CHANNEL_MSG_TEXT_LEN];
  // 168 bytes total
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
    char text[CHANNEL_MSG_TEXT_LEN];
    bool valid;
  };

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
  
public:
  ChannelScreen(UITask* task, mesh::RTCClock* rtc) 
    : _task(task), _rtc(rtc), _msgCount(0), _newestIdx(-1), _scrollPos(0), 
      _msgsPerPage(3), _viewChannelIdx(0), _sdReady(false) {
    // Initialize all messages as invalid
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      _messages[i].valid = false;
    }
  }

  void setSDReady(bool ready) { _sdReady = ready; }

  // Add a new message to the history
  void addMessage(uint8_t channel_idx, uint8_t path_len, const char* sender, const char* text) {
    // Move to next slot in circular buffer
    _newestIdx = (_newestIdx + 1) % CHANNEL_MSG_HISTORY_SIZE;
    
    ChannelMessage* msg = &_messages[_newestIdx];
    msg->timestamp = _rtc->getCurrentTime();
    msg->path_len = path_len;
    msg->channel_idx = channel_idx;
    msg->valid = true;
    
    // Sanitize emoji: replace UTF-8 emoji sequences with single-byte escape codes
    // The text already contains "Sender: message" format
    emojiSanitize(text, msg->text, CHANNEL_MSG_TEXT_LEN);
    
    if (_msgCount < CHANNEL_MSG_HISTORY_SIZE) {
      _msgCount++;
    }
    
    // Reset scroll to show newest message
    _scrollPos = 0;

    // Persist to SD card
    saveToSD();
  }

  // Get count of messages for the currently viewed channel
  int getMessageCountForChannel() const {
    int count = 0;
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      if (_messages[i].valid && _messages[i].channel_idx == _viewChannelIdx) {
        count++;
      }
    }
    return count;
  }

  int getMessageCount() const { return _msgCount; }
  
  uint8_t getViewChannelIdx() const { return _viewChannelIdx; }
  void setViewChannelIdx(uint8_t idx) { _viewChannelIdx = idx; _scrollPos = 0; }

  // -----------------------------------------------------------------------
  // SD card persistence
  // -----------------------------------------------------------------------

  // Save the entire message buffer to SD card.
  // File: /meshcore/messages.bin  (~3.4 KB for 20 messages)
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
      rec.reserved    = 0;
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
    if (the_mesh.getChannel(_viewChannelIdx, channel)) {
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
    
    if (channelMsgCount == 0) {
      display.setTextSize(0);  // Tiny font for body text
      display.setCursor(0, 20);
      display.setColor(DisplayDriver::LIGHT);
      display.print("No messages yet");
      display.setCursor(0, 30);
      display.print("A/D: Switch channel");
      display.setCursor(0, 40);
      display.print("C: Compose message");
      display.setTextSize(1);  // Restore for footer
    } else {
      display.setTextSize(0);  // Tiny font for message body
      int lineHeight = 9;   // 8px font + 1px spacing
      int headerHeight = 14;
      int footerHeight = 14;
      // Hard cutoff: no text may START at or beyond this y value
      // This ensures rendered glyphs (which extend lineHeight below y) stay above the footer
      int maxY = display.height() - footerHeight;
      
      int y = headerHeight;
      
      // Build list of messages for this channel (newest first)
      int channelMsgs[CHANNEL_MSG_HISTORY_SIZE];
      int numChannelMsgs = 0;
      
      for (int i = 0; i < _msgCount && numChannelMsgs < CHANNEL_MSG_HISTORY_SIZE; i++) {
        int idx = _newestIdx - i;
        while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
        idx = idx % CHANNEL_MSG_HISTORY_SIZE;
        
        if (_messages[idx].valid && _messages[idx].channel_idx == _viewChannelIdx) {
          channelMsgs[numChannelMsgs++] = idx;
        }
      }
      
      // Reverse to chronological order (oldest first, newest last at bottom)
      for (int l = 0, r = numChannelMsgs - 1; l < r; l++, r--) {
        int tmp = channelMsgs[l]; channelMsgs[l] = channelMsgs[r]; channelMsgs[r] = tmp;
      }
      
      // Calculate start index so newest messages appear at the bottom
      // scrollPos=0 shows the most recent messages, scrollPos++ scrolls up to older
      int startIdx = numChannelMsgs - _msgsPerPage - _scrollPos;
      if (startIdx < 0) startIdx = 0;
      
      // Display messages oldest-to-newest (top to bottom)
      int msgsDrawn = 0;
      for (int i = startIdx; i < numChannelMsgs && y + lineHeight <= maxY; i++) {
        int idx = channelMsgs[i];
        ChannelMessage* msg = &_messages[idx];
        
        // Time indicator with hop count - inline on same line as message start
        display.setCursor(0, y);
        display.setColor(DisplayDriver::YELLOW);
        
        uint32_t age = _rtc->getCurrentTime() - msg->timestamp;
        if (age < 60) {
          sprintf(tmp, "(%d) %ds ", msg->path_len == 0xFF ? 0 : msg->path_len, age);
        } else if (age < 3600) {
          sprintf(tmp, "(%d) %dm ", msg->path_len == 0xFF ? 0 : msg->path_len, age / 60);
        } else if (age < 86400) {
          sprintf(tmp, "(%d) %dh ", msg->path_len == 0xFF ? 0 : msg->path_len, age / 3600);
        } else {
          sprintf(tmp, "(%d) %dd ", msg->path_len == 0xFF ? 0 : msg->path_len, age / 86400);
        }
        display.print(tmp);
        // DO NOT advance y - message text continues on the same line
        
        // Message text with character wrapping and inline emoji support
        // (continues after timestamp on first line)
        display.setColor(DisplayDriver::LIGHT);
        
        int textLen = strlen(msg->text);
        int pos = 0;
        int linesForThisMsg = 0;
        int maxLinesPerMsg = 8;
        char charStr[2] = {0, 0};
        
        // Track position in pixels for emoji placement
        // Uses advance width (cursor movement) not bounding box for px tracking
        int lineW = display.width();
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
                wordW += display.getTextWidth(dblStr) - display.getTextWidth(charStr);
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
        
        // If we didn't end on a full line, still count it
        if (px > 0) {
          y += lineHeight;
        }
        
        y += 2;  // Small gap between messages
        msgsDrawn++;
        _msgsPerPage = msgsDrawn;
      }
      
      display.setTextSize(1);  // Restore for footer
    }
    
    // Footer with controls
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    
    // Left side: Q:Back A/D:Ch
    display.print("Q:Back A/D:Ch");
    
    // Right side: C:New
    const char* rightText = "C:New";
    display.setCursor(display.width() - display.getTextWidth(rightText) - 2, footerY);
    display.print(rightText);

#if AUTO_OFF_MILLIS == 0  // e-ink
    return 5000;
#else
    return 1000;
#endif
  }

  bool handleInput(char c) override {
    int channelMsgCount = getMessageCountForChannel();
    
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
    
    // A - previous channel
    if (c == 'a' || c == 'A') {
      if (_viewChannelIdx > 0) {
        _viewChannelIdx--;
      } else {
        // Wrap to last valid channel
        for (uint8_t i = MAX_GROUP_CHANNELS - 1; i > 0; i--) {
          ChannelDetails ch;
          if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
            _viewChannelIdx = i;
            break;
          }
        }
      }
      _scrollPos = 0;
      return true;
    }
    
    // D - next channel
    if (c == 'd' || c == 'D') {
      ChannelDetails ch;
      uint8_t nextIdx = _viewChannelIdx + 1;
      if (the_mesh.getChannel(nextIdx, ch) && ch.name[0] != '\0') {
        _viewChannelIdx = nextIdx;
      } else {
        _viewChannelIdx = 0;
      }
      _scrollPos = 0;
      return true;
    }
    
    return false;
  }
  
  // Reset scroll position to newest
  void resetScroll() {
    _scrollPos = 0;
  }
};