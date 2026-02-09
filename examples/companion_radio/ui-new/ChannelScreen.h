#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include "EmojiSprites.h"

// Maximum messages to store in history
#define CHANNEL_MSG_HISTORY_SIZE 20
#define CHANNEL_MSG_TEXT_LEN 160

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 20
#endif

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
  
public:
  ChannelScreen(UITask* task, mesh::RTCClock* rtc) 
    : _task(task), _rtc(rtc), _msgCount(0), _newestIdx(-1), _scrollPos(0), 
      _msgsPerPage(3), _viewChannelIdx(0) {
    // Initialize all messages as invalid
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      _messages[i].valid = false;
    }
  }

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
      
      // Display messages from scroll position
      int msgsDrawn = 0;
      for (int i = _scrollPos; i < numChannelMsgs && y + lineHeight <= maxY; i++) {
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
        
        // Track position in pixels for tight emoji placement
        int lineW = display.width();
        int px = display.getTextWidth(tmp);  // Pixel X after timestamp
        
        // Cursor already positioned after timestamp print - don't reset it
        
        while (pos < textLen && linesForThisMsg < maxLinesPerMsg && y + lineHeight <= maxY) {
          uint8_t b = (uint8_t)msg->text[pos];
          
          if (isEmojiEscape(b)) {
            // Check if emoji fits on this line
            if (px + EMOJI_SM_W > lineW) {
              px = 0;
              linesForThisMsg++;
              y += lineHeight;
              if (linesForThisMsg >= maxLinesPerMsg || y + lineHeight > maxY) break;
              display.setCursor(0, y);
            }
            
            const uint8_t* sprite = getEmojiSpriteSm(b);
            if (sprite) {
              display.drawXbm(px, y - 1, sprite, EMOJI_SM_W, EMOJI_SM_H);
            }
            pos++;
            px += EMOJI_SM_W + 1;  // sprite width + 1px gap
            display.setCursor(px, y);
          } else {
            charStr[0] = (char)b;
            int cw = display.getTextWidth(charStr);
            if (px + cw > lineW) {
              px = 0;
              linesForThisMsg++;
              y += lineHeight;
              if (linesForThisMsg < maxLinesPerMsg && y + lineHeight <= maxY) {
                display.setCursor(0, y);
              } else break;
            }
            display.print(charStr);
            px += cw;
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