#pragma once

// =============================================================================
// SMSScreen - SMS messaging UI for T-Deck Pro (4G variant)
//
// Three sub-views, all managed internally:
//   INBOX        — list of conversations (phone numbers + preview)
//   CONVERSATION — messages for a selected contact, scrollable
//   COMPOSE      — text input for new SMS (reuses keyboard compose pattern)
//
// Navigation mirrors ChannelScreen conventions:
//   W/S: scroll     A/D: switch conversations    C: compose
//   Q: back         Enter: select / send          Sh+Del: cancel compose
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "ModemManager.h"
#include "SMSStore.h"

// Limits
#define SMS_INBOX_PAGE_SIZE     4
#define SMS_MSG_PAGE_SIZE      30
#define SMS_COMPOSE_MAX       160

class UITask;   // forward
class SMSScreen : public UIScreen {
public:
  enum SubView { INBOX, CONVERSATION, COMPOSE };

private:
  UITask* _task;
  SubView _view;

  // Inbox state
  SMSConversation _conversations[SMS_MAX_CONVERSATIONS];
  int  _convCount;
  int  _inboxCursor;       // highlighted row
  int  _inboxScrollTop;    // first visible row

  // Conversation state
  char  _activePhone[SMS_PHONE_LEN];
  SMSMessage _msgs[SMS_MSG_PAGE_SIZE];
  int  _msgCount;
  int  _msgScrollPos;      // 0 = newest

  // Compose state
  char _composeBuf[SMS_COMPOSE_MAX + 1];
  int  _composePos;
  char _composePhone[SMS_PHONE_LEN];   // destination phone number
  bool _composeNewConversation;        // true = user is entering a new number

  // Phone input state (for new conversation)
  char _phoneInputBuf[SMS_PHONE_LEN];
  int  _phoneInputPos;

  // Refresh debounce (same pattern as mesh compose)
  bool _needsRefresh;
  unsigned long _lastRefresh;
  static const unsigned long REFRESH_INTERVAL = 600;

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
    , _phoneInputPos(0)
    , _needsRefresh(false), _lastRefresh(0)
  {
    memset(_conversations, 0, sizeof(_conversations));
    memset(_msgs, 0, sizeof(_msgs));
    memset(_composeBuf, 0, sizeof(_composeBuf));
    memset(_activePhone, 0, sizeof(_activePhone));
    memset(_composePhone, 0, sizeof(_composePhone));
    memset(_phoneInputBuf, 0, sizeof(_phoneInputBuf));
  }

  SubView getView() const { return _view; }
  bool isComposing() const { return _view == COMPOSE; }

  // Called when entering the SMS screen from home
  void activate() {
    _view = INBOX;
    refreshInbox();
    _inboxCursor = 0;
    _inboxScrollTop = 0;
  }

  // Called when a new SMS is received (from main loop polling)
  void onSMSReceived(const SMSIncoming& sms) {
    // If we're viewing this conversation, refresh it
    if (_view == CONVERSATION && strcmp(_activePhone, sms.phone) == 0) {
      refreshConversation();
    }
    // Always refresh inbox preview
    if (_view == INBOX) {
      refreshInbox();
    }
    _needsRefresh = true;
  }

  // ---- Debounced refresh check (called from main loop) ----
  bool checkRefreshNeeded() {
    if (_needsRefresh && (millis() - _lastRefresh) >= REFRESH_INTERVAL) {
      _needsRefresh = false;
      _lastRefresh = millis();
      return true;
    }
    return false;
  }

  // =========================================================================
  // SIGNAL STRENGTH INDICATOR (graphical ascending bars)
  // =========================================================================

  // Draws a 4-bar ascending signal icon, bottom-aligned at (x, baselineY).
  // Returns the total width consumed so caller can position things to the left.
  //
  //  Layout (4 bars, each 2px wide, 1px gap):
  //
  //          ██       bar 4: 8px tall  (CSQ >= 20)
  //       ██ ██       bar 3: 6px tall  (CSQ >= 15)
  //    ██ ██ ██       bar 2: 4px tall  (CSQ >= 10)
  //  ██ ██ ██ ██      bar 1: 2px tall  (CSQ >= 5)
  //  ↑              ↑
  //  x              x + 14
  //
  // Active bars: fillRect (solid).  Inactive bars: drawRect (outline).
  // Modem not ready: all bars outline + status text.

  int renderSignalIndicator(DisplayDriver& display, int rightEdgeX, int topY) {
    const int numBars   = 4;
    const int barWidth  = 2;
    const int barGap    = 1;
    const int barStep   = barWidth + barGap;   // 3px per bar slot
    const int minHeight = 2;
    const int heightInc = 2;   // each bar 2px taller than previous
    const int maxHeight = minHeight + heightInc * (numBars - 1);  // 8px

    // Total icon width: 4 bars × 2px + 3 gaps × 1px = 11px
    int iconWidth = numBars * barWidth + (numBars - 1) * barGap;

    ModemState ms = modemManager.getState();
    int signalBars = 0;

    if (ms == ModemState::READY || ms == ModemState::SENDING_SMS) {
      signalBars = modemManager.getSignalBars();  // 0-5 mapped to 0-4 bars below
      display.setColor(DisplayDriver::GREEN);
    } else {
      display.setColor(DisplayDriver::LIGHT);
    }

    // Map 0-5 signal bars to our 0-4 bar icon
    int filledBars = signalBars;
    if (filledBars > numBars) filledBars = numBars;

    int iconX = rightEdgeX - iconWidth;
    int baseY = topY + maxHeight;  // bottom of tallest bar

    for (int i = 0; i < numBars; i++) {
      int barHeight = minHeight + (i * heightInc);
      int bx = iconX + i * barStep;
      int by = baseY - barHeight;

      if (i < filledBars) {
        display.fillRect(bx, by, barWidth, barHeight);
      } else {
        display.drawRect(bx, by, barWidth, barHeight);
      }
    }

    // If modem is not ready, draw a small status label to the left of the bars
    if (ms != ModemState::READY && ms != ModemState::SENDING_SMS) {
      display.setTextSize(0);
      const char* label = ModemManager::stateToString(ms);
      uint16_t labelW = display.getTextWidth(label);
      display.setCursor(iconX - labelW - 2, topY - 3);
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
    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print("SMS Inbox");

    // Signal strength icon at top-right (2px margin from edge)
    renderSignalIndicator(display, display.width() - 2, 0);

    display.drawRect(0, 11, display.width(), 1);

    if (_convCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 20);
      if (ms != ModemState::READY) {
        display.print("Modem: ");
        display.print(ModemManager::stateToString(ms));
      } else {
        display.print("No conversations");
      }
      display.setCursor(0, 35);
      display.print("C: New SMS");
      display.setCursor(0, 47);
      display.print("Q: Back to home");
    } else {
      int y = 14;
      int lineHeight = 11;
      int visibleRows = (display.height() - 14 - 14) / (lineHeight * 2);
      if (visibleRows < 1) visibleRows = 1;

      for (int i = _inboxScrollTop;
           i < _convCount && i < _inboxScrollTop + visibleRows;
           i++) {
        SMSConversation& c = _conversations[i];

        // Highlight cursor
        if (i == _inboxCursor) {
          display.setColor(DisplayDriver::YELLOW);
          display.setCursor(0, y);
          display.print("> ");
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.setCursor(0, y);
          display.print("  ");
        }

        // Phone number
        display.setColor(i == _inboxCursor ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        display.print(c.phone);
        y += lineHeight;

        // Preview (dimmer)
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(12, y);
        // Truncate preview to fit display width
        char prev[36];
        strncpy(prev, c.preview, 35);
        prev[35] = '\0';
        display.print(prev);
        y += lineHeight + 2;
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("Q:Back W/S:Scroll");
    const char* rt = "C:New";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

    return 5000;  // e-ink: refresh every 5s
  }

  // ---- Conversation view ----
  int renderConversation(DisplayDriver& display) {
    // Header: phone number
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(_activePhone);

    // Signal icon at top-right, message count to its left
    int sigWidth = renderSignalIndicator(display, display.width() - 2, 0);

    char countStr[12];
    snprintf(countStr, sizeof(countStr), "[%d]", _msgCount);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(display.width() - 2 - sigWidth - 3 - display.getTextWidth(countStr), 0);
    display.print(countStr);

    display.drawRect(0, 11, display.width(), 1);

    if (_msgCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 25);
      display.print("No messages");
    } else {
      int lineHeight = 10;
      int headerHeight = 14;
      int footerHeight = 14;

      // Calculate chars per line
      uint16_t testWidth = display.getTextWidth("MMMMMMMMMM");
      int charsPerLine = (testWidth > 0) ? (display.width() * 10) / testWidth : 20;
      if (charsPerLine < 12) charsPerLine = 12;
      if (charsPerLine > 40) charsPerLine = 40;

      int y = headerHeight;
      int msgsDrawn = 0;

      for (int i = _msgScrollPos;
           i < _msgCount && y < display.height() - footerHeight - lineHeight;
           i++) {
        SMSMessage& msg = _msgs[i];
        if (!msg.valid) continue;

        // Direction indicator + timestamp
        display.setCursor(0, y);
        display.setColor(msg.isSent ? DisplayDriver::BLUE : DisplayDriver::YELLOW);

        uint32_t age = (millis() / 1000) - msg.timestamp;
        char timeStr[16];
        if (age < 60)        snprintf(timeStr, sizeof(timeStr), "%lus", age);
        else if (age < 3600) snprintf(timeStr, sizeof(timeStr), "%lum", age / 60);
        else if (age < 86400)snprintf(timeStr, sizeof(timeStr), "%luh", age / 3600);
        else                 snprintf(timeStr, sizeof(timeStr), "%lud", age / 86400);

        char header[32];
        snprintf(header, sizeof(header), "%s %s",
                 msg.isSent ? ">>>" : "<<<", timeStr);
        display.print(header);
        y += lineHeight;

        // Message body with word wrap
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
        msgsDrawn++;
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("Q:Back W/S:Scroll");
    const char* rt = "C:Reply";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

    return 5000;
  }

  // ---- Compose ----
  int renderCompose(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    // Signal icon at top-right (visible even while composing)
    renderSignalIndicator(display, display.width() - 2, 0);

    if (_composeNewConversation && _composePhone[0] == '\0') {
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

    // Message body
    display.setCursor(0, 14);
    display.setColor(DisplayDriver::LIGHT);

    uint16_t testWidth = display.getTextWidth("MMMMMMMMMM");
    int charsPerLine = (testWidth > 0) ? (display.width() * 10) / testWidth : 20;
    if (charsPerLine < 12) charsPerLine = 12;
    if (charsPerLine > 40) charsPerLine = 40;

    int x = 0, y = 14;
    char cs[2] = {0, 0};

    for (int i = 0; i < _composePos; i++) {
      cs[0] = _composeBuf[i];
      display.print(cs);
      x++;
      if (x >= charsPerLine) {
        x = 0;
        y += 11;
        display.setCursor(0, y);
      }
    }
    display.print("_");

    // Status bar
    int statusY = display.height() - 12;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, statusY - 2, display.width(), 1);
    display.setCursor(0, statusY);
    display.setColor(DisplayDriver::YELLOW);

    char statusStr[40];
    snprintf(statusStr, sizeof(statusStr), "%d/%d Ent:Send", _composePos, SMS_COMPOSE_MAX);
    display.print(statusStr);
    const char* cancel = "Sh+Del:X";
    display.setCursor(display.width() - display.getTextWidth(cancel) - 2, statusY);
    display.print(cancel);

    return 5000;
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
    // W - cursor up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_inboxCursor > 0) {
        _inboxCursor--;
        if (_inboxCursor < _inboxScrollTop) _inboxScrollTop = _inboxCursor;
      }
      return true;
    }
    // S - cursor down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_inboxCursor < _convCount - 1) {
        _inboxCursor++;
        int visibleRows = 3;  // approximate
        if (_inboxCursor >= _inboxScrollTop + visibleRows) {
          _inboxScrollTop = _inboxCursor - visibleRows + 1;
        }
      }
      return true;
    }
    // Enter - open conversation
    if (c == '\r' || c == 13) {
      if (_convCount > 0 && _inboxCursor < _convCount) {
        strncpy(_activePhone, _conversations[_inboxCursor].phone, SMS_PHONE_LEN - 1);
        refreshConversation();
        _view = CONVERSATION;
        return true;
      }
    }
    // C - new SMS compose
    if (c == 'c' || c == 'C') {
      enterCompose(nullptr);  // no pre-set phone = prompt for number
      return true;
    }
    // Q - back to home (handled by caller detecting we returned false for 'q')
    if (c == 'q' || c == 'Q') {
      return false;  // signal to caller: leave SMS screen
    }
    return false;
  }

  // ---- Conversation input ----
  bool handleConversationInput(char c) {
    // W - scroll up (older)
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_msgScrollPos + 3 < _msgCount) {
        _msgScrollPos++;
        return true;
      }
    }
    // S - scroll down (newer)
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_msgScrollPos > 0) {
        _msgScrollPos--;
        return true;
      }
    }
    // C - reply to this conversation
    if (c == 'c' || c == 'C') {
      enterCompose(_activePhone);
      return true;
    }
    // Q - back to inbox
    if (c == 'q' || c == 'Q') {
      refreshInbox();
      _view = INBOX;
      return true;
    }
    return false;
  }

  // ---- Compose input ----
  bool handleComposeInput(char c) {
    // Phone number entry mode
    if (_composeNewConversation && _composePhone[0] == '\0') {
      return handlePhoneInput(c);
    }

    // Enter - send
    if (c == '\r') {
      if (_composePos > 0) {
        doSend();
      }
      return true;
    }

    // Backspace
    if (c == '\b') {
      // Cancel is handled externally via shift+backspace detection.
      // Regular backspace:
      if (_composePos > 0) {
        _composePos--;
        _composeBuf[_composePos] = '\0';
        _needsRefresh = true;
      }
      return true;
    }

    // Regular character
    if (c >= 32 && c < 127 && _composePos < SMS_COMPOSE_MAX) {
      _composeBuf[_composePos++] = c;
      _composeBuf[_composePos] = '\0';
      _needsRefresh = true;
      return true;
    }

    return false;
  }

  // Phone number input sub-handler
  bool handlePhoneInput(char c) {
    if (c == '\r') {
      // Confirm phone number
      if (_phoneInputPos > 0) {
        strncpy(_composePhone, _phoneInputBuf, SMS_PHONE_LEN - 1);
        _composePhone[SMS_PHONE_LEN - 1] = '\0';
        _needsRefresh = true;
      }
      return true;
    }
    if (c == '\b') {
      if (_phoneInputPos > 0) {
        _phoneInputPos--;
        _phoneInputBuf[_phoneInputPos] = '\0';
        _needsRefresh = true;
      }
      return true;
    }
    if (c == 'q' || c == 'Q') {
      // Cancel — back to inbox
      _view = INBOX;
      refreshInbox();
      return true;
    }
    // Accept digits, +, *, #
    if ((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#') {
      if (_phoneInputPos < SMS_PHONE_LEN - 1) {
        _phoneInputBuf[_phoneInputPos++] = c;
        _phoneInputBuf[_phoneInputPos] = '\0';
        _needsRefresh = true;
      }
      return true;
    }
    return false;
  }

  // ---- Cancel compose (called externally on Shift+Backspace) ----
  void cancelCompose() {
    _composeBuf[0] = '\0';
    _composePos = 0;
    if (_activePhone[0] != '\0') {
      _view = CONVERSATION;
    } else {
      _view = INBOX;
      refreshInbox();
    }
  }

  // ---- Enter compose mode ----
  void enterCompose(const char* phone) {
    _composeBuf[0] = '\0';
    _composePos = 0;
    _view = COMPOSE;

    if (phone && phone[0] != '\0') {
      strncpy(_composePhone, phone, SMS_PHONE_LEN - 1);
      _composePhone[SMS_PHONE_LEN - 1] = '\0';
      _composeNewConversation = false;
    } else {
      _composePhone[0] = '\0';
      _phoneInputBuf[0] = '\0';
      _phoneInputPos = 0;
      _composeNewConversation = true;
    }
  }

private:
  void doSend() {
    if (_composePos == 0 || _composePhone[0] == '\0') return;

    if (modemManager.sendSMS(_composePhone, _composeBuf)) {
      // Save to SD
      uint32_t now = millis() / 1000;  // TODO: use RTC
      smsStore.saveSentMessage(_composePhone, _composeBuf, now);

      // Go back to conversation view
      strncpy(_activePhone, _composePhone, SMS_PHONE_LEN - 1);
      refreshConversation();
      _view = CONVERSATION;
    } else {
      // Queue was full — stay in compose so user can retry
      // The UITask can show an alert separately
    }

    _composeBuf[0] = '\0';
    _composePos = 0;
  }
};

#endif // HAS_4G_MODEM