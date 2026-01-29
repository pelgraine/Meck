#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>

// Maximum messages to store in history
#define CHANNEL_MSG_HISTORY_SIZE 20
#define CHANNEL_MSG_TEXT_LEN 160

class UITask;  // Forward declaration

class ChannelScreen : public UIScreen {
public:
  struct ChannelMessage {
    uint32_t timestamp;
    uint8_t path_len;
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
  
public:
  ChannelScreen(UITask* task, mesh::RTCClock* rtc) 
    : _task(task), _rtc(rtc), _msgCount(0), _newestIdx(-1), _scrollPos(0), _msgsPerPage(3) {
    // Initialize all messages as invalid
    for (int i = 0; i < CHANNEL_MSG_HISTORY_SIZE; i++) {
      _messages[i].valid = false;
    }
  }

  // Add a new message to the history
  void addMessage(uint8_t path_len, const char* sender, const char* text) {
    // Move to next slot in circular buffer
    _newestIdx = (_newestIdx + 1) % CHANNEL_MSG_HISTORY_SIZE;
    
    ChannelMessage* msg = &_messages[_newestIdx];
    msg->timestamp = _rtc->getCurrentTime();
    msg->path_len = path_len;
    msg->valid = true;
    
    // The text already contains "Sender: message" format, just store it
    strncpy(msg->text, text, CHANNEL_MSG_TEXT_LEN - 1);
    msg->text[CHANNEL_MSG_TEXT_LEN - 1] = '\0';
    
    if (_msgCount < CHANNEL_MSG_HISTORY_SIZE) {
      _msgCount++;
    }
    
    // Reset scroll to show newest message
    _scrollPos = 0;
  }

  int getMessageCount() const { return _msgCount; }

  int render(DisplayDriver& display) override {
    char tmp[32];
    
    // Header
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.print("Public Channel");
    
    // Message count on right
    sprintf(tmp, "[%d]", _msgCount);
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);
    
    // Divider line
    display.drawRect(0, 11, display.width(), 1);
    
    if (_msgCount == 0) {
      display.setCursor(0, 25);
      display.setColor(DisplayDriver::LIGHT);
      display.print("No messages yet");
      display.setCursor(0, 40);
      display.print("Press C to compose");
    } else {
      int lineHeight = 10;
      int headerHeight = 14;
      int footerHeight = 14;
      int availableHeight = display.height() - headerHeight - footerHeight;
      
      // Calculate chars per line based on display width
      int charsPerLine = display.width() / 6;
      
      int y = headerHeight;
      
      // Display messages from scroll position
      int msgsDrawn = 0;
      for (int i = 0; i + _scrollPos < _msgCount && y < display.height() - footerHeight - lineHeight; i++) {
        // Calculate index in circular buffer
        int idx = _newestIdx - _scrollPos - i;
        while (idx < 0) idx += CHANNEL_MSG_HISTORY_SIZE;
        idx = idx % CHANNEL_MSG_HISTORY_SIZE;
        
        if (!_messages[idx].valid) continue;
        
        ChannelMessage* msg = &_messages[idx];
        
        // Time indicator with hop count
        display.setCursor(0, y);
        display.setColor(DisplayDriver::YELLOW);
        
        uint32_t age = _rtc->getCurrentTime() - msg->timestamp;
        if (age < 60) {
          sprintf(tmp, "(%d) %ds", msg->path_len == 0xFF ? 0 : msg->path_len, age);
        } else if (age < 3600) {
          sprintf(tmp, "(%d) %dm", msg->path_len == 0xFF ? 0 : msg->path_len, age / 60);
        } else if (age < 86400) {
          sprintf(tmp, "(%d) %dh", msg->path_len == 0xFF ? 0 : msg->path_len, age / 3600);
        } else {
          sprintf(tmp, "(%d) %dd", msg->path_len == 0xFF ? 0 : msg->path_len, age / 86400);
        }
        display.print(tmp);
        y += lineHeight;
        
        // Message text with word wrap - the text already contains "Sender: message"
        display.setColor(DisplayDriver::LIGHT);
        
        int textLen = strlen(msg->text);
        int pos = 0;
        int linesForThisMsg = 0;
        int maxLinesPerMsg = 3;  // Allow up to 3 lines per message
        
        while (pos < textLen && linesForThisMsg < maxLinesPerMsg && y < display.height() - footerHeight - 2) {
          display.setCursor(0, y);
          
          // Find how much text fits on this line
          int lineEnd = pos + charsPerLine;
          if (lineEnd >= textLen) {
            lineEnd = textLen;
          } else {
            // Try to break at a space
            int lastSpace = -1;
            for (int j = pos; j < lineEnd && j < textLen; j++) {
              if (msg->text[j] == ' ') lastSpace = j;
            }
            if (lastSpace > pos) lineEnd = lastSpace;
          }
          
          // Print this line segment
          char lineBuf[42];
          int lineLen = lineEnd - pos;
          if (lineLen > 40) lineLen = 40;
          strncpy(lineBuf, msg->text + pos, lineLen);
          lineBuf[lineLen] = '\0';
          display.print(lineBuf);
          
          pos = lineEnd;
          // Skip space at start of next line
          while (pos < textLen && msg->text[pos] == ' ') pos++;
          
          y += lineHeight;
          linesForThisMsg++;
        }
        
        // If we truncated, show ellipsis indicator
        if (pos < textLen && linesForThisMsg >= maxLinesPerMsg) {
          // Message was truncated - could add "..." but space is tight
        }
        
        y += 2;  // Small gap between messages
        msgsDrawn++;
        _msgsPerPage = msgsDrawn;
      }
    }
    
    // Footer with scroll indicator and controls
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    
    // Left side: Q:Exit
    display.print("Q:Exit");
    
    // Middle: scroll position indicator
    if (_msgCount > _msgsPerPage) {
      int endMsg = _scrollPos + _msgsPerPage;
      if (endMsg > _msgCount) endMsg = _msgCount;
      sprintf(tmp, "%d-%d/%d", _scrollPos + 1, endMsg, _msgCount);
      // Center it roughly
      int midX = display.width() / 2 - 15;
      display.setCursor(midX, footerY);
      display.print(tmp);
    }
    
    // Right side: controls
    sprintf(tmp, "W/S:Scrl C:New");
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, footerY);
    display.print(tmp);

#if AUTO_OFF_MILLIS == 0  // e-ink
    return 5000;  // Refresh every 5s
#else
    return 1000;  // Refresh every 1s for time updates
#endif
  }

  bool handleInput(char c) override {
    // KEY_PREV (0xF2) or 'w' - scroll up (older messages)
    if (c == 0xF2 || c == 'w' || c == 'W') {
      if (_scrollPos + _msgsPerPage < _msgCount) {
        _scrollPos++;
        return true;
      }
    }
    
    // KEY_NEXT (0xF1) or 's' - scroll down (newer messages)
    if (c == 0xF1 || c == 's' || c == 'S') {
      if (_scrollPos > 0) {
        _scrollPos--;
        return true;
      }
    }
    
    // KEY_ENTER or 'c' - compose (handled by main.cpp keyboard handler)
    // 'q' - go back (handled by main.cpp keyboard handler)
    
    return false;
  }
  
  // Reset scroll position to newest
  void resetScroll() {
    _scrollPos = 0;
  }
}; 