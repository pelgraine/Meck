#pragma once

// =============================================================================
// SMSScreen - SMS messaging UI for T-Deck Pro (4G variant)
//
// Three sub-views:
//   INBOX        — list of conversations, sorted by most recent
//   CONVERSATION — messages for a selected contact, scrollable
//   COMPOSE      — text input for new SMS
//
// Navigation mirrors ChannelScreen conventions:
//   W/S: scroll     Enter: select/send    C: compose new/reply
//   Q: back         Sh+Del: cancel compose
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef SMS_SCREEN_H
#define SMS_SCREEN_H

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "ModemManager.h"
#include "SMSStore.h"

// Limits
#define SMS_INBOX_PAGE_SIZE     4
#define SMS_MSG_PAGE_SIZE      30
#define SMS_COMPOSE_MAX       160

class UITask;   // forward declaration

class SMSScreen : public UIScreen {
public:
  enum SubView { INBOX, CONVERSATION, COMPOSE };

private:
  UITask* _task;
  SubView _view;

  // Inbox state
  SMSConversation _conversations[SMS_MAX_CONVERSATIONS];
  int  _convCount;
  int  _inboxCursor;
  int  _inboxScrollTop;

  // Conversation state
  char  _activePhone[SMS_PHONE_LEN];
  SMSMessage _msgs[SMS_MSG_PAGE_SIZE];
  int  _msgCount;
  int  _msgScrollPos;

  // Compose state
  char _composeBuf[SMS_COMPOSE_MAX + 1];
  int  _composePos;
  char _composePhone[SMS_PHONE_LEN];
  bool _composeNewConversation;

  // Phone input state (for new conversation)
  char _phoneInputBuf[SMS_PHONE_LEN];
  int  _phoneInputPos;
  bool _enteringPhone;

  // Refresh debounce
  bool _needsRefresh;
  unsigned long _lastRefresh;
  static const unsigned long REFRESH_INTERVAL = 600;

  // SD ready flag
  bool _sdReady;

  // Reload helpers
  void refreshInbox() {
    _convCount = smsStore.loadConversations(_conversations, SMS_MAX_CONVERSATIONS);
  }

  void refreshConversation() {
    _msgCount = smsStore.loadMessages(_activePhone, _msgs, SMS_MSG_PAGE_SIZE);
    _msgScrollPos = 0;
  }

public:
  SMSScreen(UITask* task)
    : _task(task), _view(INBOX)
    , _convCount(0), _inboxCursor(0), _inboxScrollTop(0)
    , _msgCount(0), _msgScrollPos(0)
    , _composePos(0), _composeNewConversation(false)
    , _phoneInputPos(0), _enteringPhone(false)
    , _needsRefresh(false), _lastRefresh(0)
    , _sdReady(false)
  {
    memset(_composeBuf, 0, sizeof(_composeBuf));
    memset(_composePhone, 0, sizeof(_composePhone));
    memset(_phoneInputBuf, 0, sizeof(_phoneInputBuf));
    memset(_activePhone, 0, sizeof(_activePhone));
  }

  void setSDReady(bool ready) { _sdReady = ready; }

  void activate() {
    _view = INBOX;
    _inboxCursor = 0;
    _inboxScrollTop = 0;
    if (_sdReady) refreshInbox();
  }

  SubView getSubView() const { return _view; }
  bool isComposing() const { return _view == COMPOSE; }
  bool isEnteringPhone() const { return _enteringPhone; }

  // Called from main loop when an SMS arrives (saves to store + refreshes)
  void onIncomingSMS(const char* phone, const char* body, uint32_t timestamp) {
    if (_sdReady) {
      smsStore.saveMessage(phone, body, false, timestamp);
    }
    // If we're viewing this conversation, refresh it
    if (_view == CONVERSATION && strcmp(_activePhone, phone) == 0) {
      refreshConversation();
    }
    // If on inbox, refresh the list
    if (_view == INBOX) {
      refreshInbox();
    }
    _needsRefresh = true;
  }

  // =========================================================================
  // Signal strength indicator (top-right corner)
  // =========================================================================
  int renderSignalIndicator(DisplayDriver& display, int rightX, int topY) {
    ModemState ms = modemManager.getState();
    int bars = modemManager.getSignalBars();
    int iconWidth = 16;

    // Draw signal bars (4 bars, increasing height)
    int barW = 3;
    int gap = 1;
    int startX = rightX - (4 * (barW + gap));
    for (int i = 0; i < 4; i++) {
      int barH = 2 + i * 2;  // 2, 4, 6, 8
      int x = startX + i * (barW + gap);
      int y = topY + (8 - barH);
      if (i < bars) {
        display.setColor(DisplayDriver::GREEN);
        display.fillRect(x, y, barW, barH);
      } else {
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(x, y, barW, barH);
      }
    }

    // Show modem state text if not ready
    if (ms != ModemState::READY && ms != ModemState::SENDING_SMS) {
      display.setTextSize(0);
      display.setColor(DisplayDriver::YELLOW);
      const char* label = ModemManager::stateToString(ms);
      uint16_t labelW = display.getTextWidth(label);
      display.setCursor(startX - labelW - 2, topY - 3);
      display.print(label);
      display.setTextSize(1);
      return iconWidth + labelW + 2;
    }

    return iconWidth;
  }

  // =========================================================================
  // RENDER
  // =========================================================================

  int render(DisplayDriver& display) override {
    _lastRefresh = millis();

    switch (_view) {
      case INBOX:        return renderInbox(display);
      case CONVERSATION: return renderConversation(display);
      case COMPOSE:      return renderCompose(display);
    }
    return 1000;
  }

  // ---- Inbox ----
  int renderInbox(DisplayDriver& display) {
    ModemState ms = modemManager.getState();

    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print("SMS Inbox");

    // Signal strength at top-right
    renderSignalIndicator(display, display.width() - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    if (_convCount == 0) {
      display.setTextSize(0);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 20);
      display.print("No conversations");
      display.setCursor(0, 32);
      display.print("Press C for new SMS");
      
      if (ms != ModemState::READY) {
        display.setCursor(0, 48);
        display.setColor(DisplayDriver::YELLOW);
        char statBuf[40];
        snprintf(statBuf, sizeof(statBuf), "Modem: %s", ModemManager::stateToString(ms));
        display.print(statBuf);
      }
      display.setTextSize(1);
    } else {
      display.setTextSize(0);
      int lineHeight = 10;
      int y = 14;

      int visibleCount = (display.height() - 14 - 14) / (lineHeight * 2 + 2);
      if (visibleCount < 1) visibleCount = 1;

      // Adjust scroll to keep cursor visible
      if (_inboxCursor < _inboxScrollTop) _inboxScrollTop = _inboxCursor;
      if (_inboxCursor >= _inboxScrollTop + visibleCount) {
        _inboxScrollTop = _inboxCursor - visibleCount + 1;
      }

      for (int vi = 0; vi < visibleCount && (_inboxScrollTop + vi) < _convCount; vi++) {
        int idx = _inboxScrollTop + vi;
        SMSConversation& c = _conversations[idx];
        if (!c.valid) continue;

        bool selected = (idx == _inboxCursor);

        // Phone number (highlighted if selected)
        display.setCursor(0, y);
        display.setColor(selected ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        if (selected) display.print("> ");
        display.print(c.phone);

        // Message count at right
        char countStr[8];
        snprintf(countStr, sizeof(countStr), "[%d]", c.messageCount);
        display.setCursor(display.width() - display.getTextWidth(countStr) - 2, y);
        display.print(countStr);

        y += lineHeight;

        // Preview (dimmer)
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(12, y);
        char prev[36];
        strncpy(prev, c.preview, 35);
        prev[35] = '\0';
        display.print(prev);
        y += lineHeight + 2;
      }
      display.setTextSize(1);
    }

    // Footer
    display.setTextSize(0);  // Must be set before setCursor/getTextWidth
    display.setColor(DisplayDriver::LIGHT);
    int footerY = display.height() - 10;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("Q:Back");
    const char* mid = "W/S:Scrll";
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);
    const char* rt = "C:New";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);
    display.setTextSize(1);

    return 5000;
  }

  // ---- Conversation view ----
  int renderConversation(DisplayDriver& display) {
    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(_activePhone);

    // Signal icon
    renderSignalIndicator(display, display.width() - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    if (_msgCount == 0) {
      display.setTextSize(0);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 25);
      display.print("No messages");
      display.setTextSize(1);
    } else {
      display.setTextSize(0);
      int lineHeight = 10;
      int headerHeight = 14;
      int footerHeight = 14;

      // Estimate chars per line
      uint16_t testWidth = display.getTextWidth("MMMMMMMMMM");
      int charsPerLine = (testWidth > 0) ? (display.width() * 10) / testWidth : 20;
      if (charsPerLine < 12) charsPerLine = 12;
      if (charsPerLine > 40) charsPerLine = 40;

      int y = headerHeight;
      for (int i = _msgScrollPos;
           i < _msgCount && y < display.height() - footerHeight - lineHeight;
           i++) {
        SMSMessage& msg = _msgs[i];
        if (!msg.valid) continue;

        // Direction indicator
        display.setCursor(0, y);
        display.setColor(msg.isSent ? DisplayDriver::BLUE : DisplayDriver::YELLOW);

        // Time formatting
        uint32_t now = millis() / 1000;
        uint32_t age = (now > msg.timestamp) ? (now - msg.timestamp) : 0;
        char timeStr[16];
        if (age < 60)         snprintf(timeStr, sizeof(timeStr), "%lus", (unsigned long)age);
        else if (age < 3600)  snprintf(timeStr, sizeof(timeStr), "%lum", (unsigned long)(age / 60));
        else if (age < 86400) snprintf(timeStr, sizeof(timeStr), "%luh", (unsigned long)(age / 3600));
        else                  snprintf(timeStr, sizeof(timeStr), "%lud", (unsigned long)(age / 86400));

        char header[32];
        snprintf(header, sizeof(header), "%s %s",
                 msg.isSent ? ">>>" : "<<<", timeStr);
        display.print(header);
        y += lineHeight;

        // Message body with simple word wrap
        display.setColor(DisplayDriver::LIGHT);
        int textLen = strlen(msg.body);
        int pos = 0;
        int linesForMsg = 0;
        int maxLines = 4;
        int x = 0;
        char cs[2] = {0, 0};

        display.setCursor(0, y);
        while (pos < textLen && linesForMsg < maxLines &&
               y < display.height() - footerHeight - 2) {
          cs[0] = msg.body[pos++];
          display.print(cs);
          x++;
          if (x >= charsPerLine) {
            x = 0;
            linesForMsg++;
            y += lineHeight;
            if (linesForMsg < maxLines && y < display.height() - footerHeight - 2) {
              display.setCursor(0, y);
            }
          }
        }
        if (x > 0) y += lineHeight;
        y += 2;
      }
      display.setTextSize(1);
    }

    // Footer
    display.setTextSize(0);  // Must be set before setCursor/getTextWidth
    display.setColor(DisplayDriver::LIGHT);
    int footerY = display.height() - 10;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("Q:Back");
    const char* mid = "W/S:Scrll";
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);
    const char* rt = "C:Reply";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);
    display.setTextSize(1);

    return 5000;
  }

  // ---- Compose ----
  int renderCompose(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    if (_enteringPhone) {
      // Phone number input mode
      display.print("To: ");
      display.setColor(DisplayDriver::LIGHT);
      display.print(_phoneInputBuf);
      display.print("_");
    } else {
      char header[40];
      snprintf(header, sizeof(header), "To: %s", _composePhone);
      display.print(header);
    }

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    if (!_enteringPhone) {
      // Message body
      display.setCursor(0, 14);
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(0);

      uint16_t testWidth = display.getTextWidth("MMMMMMMMMM");
      int charsPerLine = (testWidth > 0) ? (display.width() * 10) / testWidth : 20;
      if (charsPerLine < 12) charsPerLine = 12;

      int y = 14;
      int x = 0;
      char cs[2] = {0, 0};
      for (int i = 0; i < _composePos; i++) {
        cs[0] = _composeBuf[i];
        display.setCursor(x * (display.width() / charsPerLine), y);
        display.print(cs);
        x++;
        if (x >= charsPerLine) {
          x = 0;
          y += 10;
        }
      }

      // Cursor
      display.setCursor(x * (display.width() / charsPerLine), y);
      display.print("_");
      display.setTextSize(1);
    }

    // Status bar
    display.setTextSize(0);  // Must be set before setCursor/getTextWidth
    display.setColor(DisplayDriver::LIGHT);
    int statusY = display.height() - 10;
    display.drawRect(0, statusY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, statusY);

    if (_enteringPhone) {
      display.print("Phone# then Ent");
      const char* rt = "S+D:X";
      display.setCursor(display.width() - display.getTextWidth(rt) - 2, statusY);
      display.print(rt);
    } else {
      char status[30];
      snprintf(status, sizeof(status), "%d/%d", _composePos, SMS_COMPOSE_MAX);
      display.print(status);
      const char* rt = "Ent:Snd S+D:X";
      display.setCursor(display.width() - display.getTextWidth(rt) - 2, statusY);
      display.print(rt);
    }
    display.setTextSize(1);

    return 2000;
  }

  // =========================================================================
  // INPUT HANDLING
  // =========================================================================

  bool handleInput(char c) override {
    switch (_view) {
      case INBOX:        return handleInboxInput(c);
      case CONVERSATION: return handleConversationInput(c);
      case COMPOSE:      return handleComposeInput(c);
    }
    return false;
  }

  // ---- Inbox input ----
  bool handleInboxInput(char c) {
    switch (c) {
      case 'w': case 'W':
        if (_inboxCursor > 0) _inboxCursor--;
        return true;

      case 's': case 'S':
        if (_inboxCursor < _convCount - 1) _inboxCursor++;
        return true;

      case '\r':  // Enter - open conversation
        if (_convCount > 0 && _inboxCursor < _convCount) {
          strncpy(_activePhone, _conversations[_inboxCursor].phone, SMS_PHONE_LEN - 1);
          refreshConversation();
          _view = CONVERSATION;
        }
        return true;

      case 'c': case 'C':  // New conversation
        _composeNewConversation = true;
        _enteringPhone = true;
        _phoneInputBuf[0] = '\0';
        _phoneInputPos = 0;
        _composeBuf[0] = '\0';
        _composePos = 0;
        _view = COMPOSE;
        return true;

      case 'q': case 'Q':  // Back to home (handled by main.cpp)
        return false;       // Let main.cpp handle navigation

      default:
        return false;
    }
  }

  // ---- Conversation input ----
  bool handleConversationInput(char c) {
    switch (c) {
      case 'w': case 'W':
        if (_msgScrollPos > 0) _msgScrollPos--;
        return true;

      case 's': case 'S':
        if (_msgScrollPos < _msgCount - 1) _msgScrollPos++;
        return true;

      case 'c': case 'C':  // Reply to this conversation
        _composeNewConversation = false;
        _enteringPhone = false;
        strncpy(_composePhone, _activePhone, SMS_PHONE_LEN - 1);
        _composeBuf[0] = '\0';
        _composePos = 0;
        _view = COMPOSE;
        return true;

      case 'q': case 'Q':  // Back to inbox
        refreshInbox();
        _view = INBOX;
        return true;

      default:
        return false;
    }
  }

  // ---- Compose input ----
  bool handleComposeInput(char c) {
    if (_enteringPhone) {
      return handlePhoneInput(c);
    }

    // Message body input
    switch (c) {
      case '\r': {  // Enter - send SMS
        if (_composePos > 0) {
          _composeBuf[_composePos] = '\0';

          // Queue for sending via modem
          bool queued = modemManager.sendSMS(_composePhone, _composeBuf);

          // Save to store (as sent)
          if (_sdReady) {
            uint32_t ts = millis() / 1000;
            smsStore.saveMessage(_composePhone, _composeBuf, true, ts);
          }

          Serial.printf("[SMS] %s to %s: %s\n",
                        queued ? "Queued" : "Queue full", _composePhone, _composeBuf);
        }

        // Return to inbox
        _composeBuf[0] = '\0';
        _composePos = 0;
        refreshInbox();
        _view = INBOX;
        return true;
      }

      case '\b':  // Backspace
        if (_composePos > 0) {
          _composePos--;
          _composeBuf[_composePos] = '\0';
        }
        return true;

      case 0x18:  // Shift+Backspace (cancel) — same as mesh compose
        _composeBuf[0] = '\0';
        _composePos = 0;
        refreshInbox();
        _view = INBOX;
        return true;

      default:
        // Printable character
        if (c >= 32 && c < 127 && _composePos < SMS_COMPOSE_MAX) {
          _composeBuf[_composePos++] = c;
          _composeBuf[_composePos] = '\0';
        }
        return true;
    }
  }

  // ---- Phone number input ----
  bool handlePhoneInput(char c) {
    switch (c) {
      case '\r':  // Enter - done entering phone, move to body
        if (_phoneInputPos > 0) {
          _phoneInputBuf[_phoneInputPos] = '\0';
          strncpy(_composePhone, _phoneInputBuf, SMS_PHONE_LEN - 1);
          _enteringPhone = false;
          _composeBuf[0] = '\0';
          _composePos = 0;
        }
        return true;

      case '\b':  // Backspace
        if (_phoneInputPos > 0) {
          _phoneInputPos--;
          _phoneInputBuf[_phoneInputPos] = '\0';
        }
        return true;

      case 0x18:  // Shift+Backspace (cancel)
        _phoneInputBuf[0] = '\0';
        _phoneInputPos = 0;
        refreshInbox();
        _view = INBOX;
        _enteringPhone = false;
        return true;

      default:
        // Accept digits, +, *, # for phone numbers
        if (_phoneInputPos < SMS_PHONE_LEN - 1 &&
            ((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#')) {
          _phoneInputBuf[_phoneInputPos++] = c;
          _phoneInputBuf[_phoneInputPos] = '\0';
        }
        return true;
    }
  }
};

#endif // SMS_SCREEN_H
#endif // HAS_4G_MODEM