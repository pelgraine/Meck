#pragma once

// =============================================================================
// SMSScreen - SMS messaging UI for T-Deck Pro (4G variant)
//
// Sub-views:
//   INBOX        — list of conversations (names resolved via SMSContacts)
//   CONVERSATION — messages for a selected contact, scrollable
//   COMPOSE      — text input for new SMS
//   CONTACTS     — browsable contacts list, pick to compose
//   EDIT_CONTACT — add or edit a contact name for a phone number
//
// Navigation mirrors ChannelScreen conventions:
//   W/S: scroll     Enter: select/send    C: compose new/reply
//   Q: back         Sh+Del: cancel compose
//   D: contacts (from inbox)
//   A: add/edit contact (from conversation)
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef SMS_SCREEN_H
#define SMS_SCREEN_H

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <time.h>
#include "ModemManager.h"
#include "SMSStore.h"
#include "SMSContacts.h"

// Limits
#define SMS_INBOX_PAGE_SIZE     4
#define SMS_MSG_PAGE_SIZE      30
#define SMS_COMPOSE_MAX       160

class UITask;   // forward declaration

class SMSScreen : public UIScreen {
public:
  enum SubView { INBOX, CONVERSATION, COMPOSE, CONTACTS, EDIT_CONTACT };

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

  // Contacts list state
  int _contactsCursor;
  int _contactsScrollTop;

  // Edit contact state
  char _editPhone[SMS_PHONE_LEN];
  char _editNameBuf[SMS_CONTACT_NAME_LEN];
  int  _editNamePos;
  bool _editIsNew;              // true = adding new, false = editing existing
  SubView _editReturnView;      // where to return after save/cancel

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
    // Scroll to bottom (newest messages are at end now, chat-style)
    _msgScrollPos = (_msgCount > 3) ? _msgCount - 3 : 0;
  }

public:
  SMSScreen(UITask* task)
    : _task(task), _view(INBOX)
    , _convCount(0), _inboxCursor(0), _inboxScrollTop(0)
    , _msgCount(0), _msgScrollPos(0)
    , _composePos(0), _composeNewConversation(false)
    , _phoneInputPos(0), _enteringPhone(false)
    , _contactsCursor(0), _contactsScrollTop(0)
    , _editNamePos(0), _editIsNew(false), _editReturnView(INBOX)
    , _needsRefresh(false), _lastRefresh(0)
    , _sdReady(false)
  {
    memset(_composeBuf, 0, sizeof(_composeBuf));
    memset(_composePhone, 0, sizeof(_composePhone));
    memset(_phoneInputBuf, 0, sizeof(_phoneInputBuf));
    memset(_activePhone, 0, sizeof(_activePhone));
    memset(_editPhone, 0, sizeof(_editPhone));
    memset(_editNameBuf, 0, sizeof(_editNameBuf));
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
    if (_view == CONVERSATION && strcmp(_activePhone, phone) == 0) {
      refreshConversation();
    }
    if (_view == INBOX) {
      refreshInbox();
    }
    _needsRefresh = true;
  }

  // =========================================================================
  // Signal strength indicator (top-right corner)
  // =========================================================================

  int renderSignalIndicator(DisplayDriver& display, int startX, int topY) {
    ModemState ms = modemManager.getState();
    int bars = modemManager.getSignalBars();

    // Draw signal bars (4 bars, increasing height)
    int barWidth = 3;
    int barGap = 2;
    int maxBarH = 10;
    int totalWidth = 4 * barWidth + 3 * barGap;
    int x = startX - totalWidth;
    int iconWidth = totalWidth;

    for (int b = 0; b < 4; b++) {
      int barH = 3 + b * 2;
      int barY = topY + (maxBarH - barH);
      if (b < bars) {
        display.setColor(DisplayDriver::LIGHT);
      } else {
        display.setColor(DisplayDriver::DARK);
      }
      display.fillRect(x, barY, barWidth, barH);
      x += barWidth + barGap;
    }

    // Show modem state text if not ready
    if (ms != ModemState::READY && ms != ModemState::SENDING_SMS) {
      display.setTextSize(0);
      display.setColor(DisplayDriver::YELLOW);
      const char* label = ModemManager::stateToString(ms);
      uint16_t labelW = display.getTextWidth(label);
      display.setCursor(startX - totalWidth - labelW - 2, topY - 3);
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
      case CONTACTS:     return renderContacts(display);
      case EDIT_CONTACT: return renderEditContact(display);
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

        // Resolve contact name (shows name if saved, phone otherwise)
        char dispName[SMS_CONTACT_NAME_LEN];
        smsContacts.displayName(c.phone, dispName, sizeof(dispName));

        display.setCursor(0, y);
        display.setColor(selected ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        if (selected) display.print("> ");
        display.print(dispName);

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
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("Q:Back");
    const char* mid = "D:Contacts";
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);
    const char* rt = "C:New";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

    return 5000;
  }

  // ---- Conversation view ----
  int renderConversation(DisplayDriver& display) {
    // Header - show contact name if available, phone otherwise
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    char convTitle[SMS_CONTACT_NAME_LEN];
    smsContacts.displayName(_activePhone, convTitle, sizeof(convTitle));
    display.print(convTitle);

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

        // Time formatting (epoch-aware)
        char timeStr[16];
        time_t now = time(nullptr);
        bool haveEpoch = (now > 1700000000);        // system clock is set
        bool msgIsEpoch = (msg.timestamp > 1700000000); // msg has real timestamp

        if (haveEpoch && msgIsEpoch) {
          uint32_t age = (uint32_t)(now - msg.timestamp);
          if (age < 60)         snprintf(timeStr, sizeof(timeStr), "%lus", (unsigned long)age);
          else if (age < 3600)  snprintf(timeStr, sizeof(timeStr), "%lum", (unsigned long)(age / 60));
          else if (age < 86400) snprintf(timeStr, sizeof(timeStr), "%luh", (unsigned long)(age / 3600));
          else                  snprintf(timeStr, sizeof(timeStr), "%lud", (unsigned long)(age / 86400));
        } else {
          strncpy(timeStr, "---", sizeof(timeStr));
        }

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
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("Q:Bk A:Add Contact");
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

    if (_enteringPhone) {
      display.print("To: ");
      display.setColor(DisplayDriver::LIGHT);
      display.print(_phoneInputBuf);
      display.print("_");
    } else {
      // Show contact name if available
      char dispName[SMS_CONTACT_NAME_LEN];
      smsContacts.displayName(_composePhone, dispName, sizeof(dispName));
      char toLabel[40];
      snprintf(toLabel, sizeof(toLabel), "To: %s", dispName);
      display.print(toLabel);
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
    display.setTextSize(1);
    int statusY = display.height() - 12;
    display.drawRect(0, statusY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, statusY);

    if (_enteringPhone) {
      display.print("Phone#");
      const char* rt = "Ent S+D:X";
      display.setCursor(display.width() - display.getTextWidth(rt) - 2, statusY);
      display.print(rt);
    } else {
      char status[16];
      snprintf(status, sizeof(status), "%d/%d", _composePos, SMS_COMPOSE_MAX);
      display.print(status);
      const char* rt = "Ent S+D:X";
      display.setCursor(display.width() - display.getTextWidth(rt) - 2, statusY);
      display.print(rt);
    }

    return 2000;
  }

  // ---- Contacts list ----
  int renderContacts(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print("SMS Contacts");

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    int cnt = smsContacts.count();

    if (cnt == 0) {
      display.setTextSize(0);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 25);
      display.print("No contacts saved");
      display.setCursor(0, 37);
      display.print("Open a conversation");
      display.setCursor(0, 49);
      display.print("and press A to add");
      display.setTextSize(1);
    } else {
      display.setTextSize(0);
      int lineHeight = 10;
      int y = 14;

      int visibleCount = (display.height() - 14 - 14) / (lineHeight * 2 + 2);
      if (visibleCount < 1) visibleCount = 1;

      // Adjust scroll
      if (_contactsCursor >= cnt) _contactsCursor = cnt - 1;
      if (_contactsCursor < 0) _contactsCursor = 0;
      if (_contactsCursor < _contactsScrollTop) _contactsScrollTop = _contactsCursor;
      if (_contactsCursor >= _contactsScrollTop + visibleCount) {
        _contactsScrollTop = _contactsCursor - visibleCount + 1;
      }

      for (int vi = 0; vi < visibleCount && (_contactsScrollTop + vi) < cnt; vi++) {
        int idx = _contactsScrollTop + vi;
        const SMSContact& ct = smsContacts.get(idx);
        if (!ct.valid) continue;

        bool selected = (idx == _contactsCursor);

        // Name
        display.setCursor(0, y);
        display.setColor(selected ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        if (selected) display.print("> ");
        display.print(ct.name);
        y += lineHeight;

        // Phone (dimmer)
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(12, y);
        display.print(ct.phone);
        y += lineHeight + 2;
      }
      display.setTextSize(1);
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("Q:Back");
    const char* rt = "Ent:SMS";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

    return 5000;
  }

  // ---- Edit contact ----
  int renderEditContact(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(_editIsNew ? "Add Contact" : "Edit Contact");

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    // Phone number (read-only)
    display.setTextSize(0);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, 16);
    display.print("Phone: ");
    display.print(_editPhone);

    // Name input
    display.setCursor(0, 30);
    display.setColor(DisplayDriver::YELLOW);
    display.print("Name: ");
    display.setColor(DisplayDriver::LIGHT);
    display.print(_editNameBuf);
    display.print("_");

    display.setTextSize(1);

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("S+D:X");
    const char* rt = "Ent:Save";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

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
      case CONTACTS:     return handleContactsInput(c);
      case EDIT_CONTACT: return handleEditContactInput(c);
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

      case 'd': case 'D':  // Open contacts list
        _contactsCursor = 0;
        _contactsScrollTop = 0;
        _view = CONTACTS;
        return true;

      case 'q': case 'Q':  // Back to home (handled by main.cpp)
        return false;

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

      case 'a': case 'A': {  // Add/edit contact for this number
        strncpy(_editPhone, _activePhone, SMS_PHONE_LEN - 1);
        _editPhone[SMS_PHONE_LEN - 1] = '\0';
        _editReturnView = CONVERSATION;

        const char* existing = smsContacts.lookup(_activePhone);
        if (existing) {
          _editIsNew = false;
          strncpy(_editNameBuf, existing, SMS_CONTACT_NAME_LEN - 1);
          _editNameBuf[SMS_CONTACT_NAME_LEN - 1] = '\0';
          _editNamePos = strlen(_editNameBuf);
        } else {
          _editIsNew = true;
          _editNameBuf[0] = '\0';
          _editNamePos = 0;
        }
        _view = EDIT_CONTACT;
        return true;
      }

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

    switch (c) {
      case '\r': {  // Enter - send SMS
        if (_composePos > 0) {
          _composeBuf[_composePos] = '\0';
          bool queued = modemManager.sendSMS(_composePhone, _composeBuf);
          if (_sdReady) {
            uint32_t ts = (uint32_t)time(nullptr);
            smsStore.saveMessage(_composePhone, _composeBuf, true, ts);
          }
          Serial.printf("[SMS] %s to %s: %s\n",
                        queued ? "Queued" : "Queue full", _composePhone, _composeBuf);
        }
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

      case 0x18:  // Shift+Backspace (cancel)
        _composeBuf[0] = '\0';
        _composePos = 0;
        refreshInbox();
        _view = INBOX;
        return true;

      default:
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
      case '\r':  // Done entering phone, move to body
        if (_phoneInputPos > 0) {
          _phoneInputBuf[_phoneInputPos] = '\0';
          strncpy(_composePhone, _phoneInputBuf, SMS_PHONE_LEN - 1);
          _enteringPhone = false;
          _composeBuf[0] = '\0';
          _composePos = 0;
        }
        return true;

      case '\b':
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
        if (_phoneInputPos < SMS_PHONE_LEN - 1 &&
            ((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#')) {
          _phoneInputBuf[_phoneInputPos++] = c;
          _phoneInputBuf[_phoneInputPos] = '\0';
        }
        return true;
    }
  }

  // ---- Contacts list input ----
  bool handleContactsInput(char c) {
    int cnt = smsContacts.count();

    switch (c) {
      case 'w': case 'W':
        if (_contactsCursor > 0) _contactsCursor--;
        return true;

      case 's': case 'S':
        if (_contactsCursor < cnt - 1) _contactsCursor++;
        return true;

      case '\r':  // Enter - compose to selected contact
        if (cnt > 0 && _contactsCursor < cnt) {
          const SMSContact& ct = smsContacts.get(_contactsCursor);
          _composeNewConversation = true;
          _enteringPhone = false;
          strncpy(_composePhone, ct.phone, SMS_PHONE_LEN - 1);
          _composeBuf[0] = '\0';
          _composePos = 0;
          _view = COMPOSE;
        }
        return true;

      case 'q': case 'Q':  // Back to inbox
        refreshInbox();
        _view = INBOX;
        return true;

      default:
        return false;
    }
  }

  // ---- Edit contact input ----
  bool handleEditContactInput(char c) {
    switch (c) {
      case '\r':  // Enter - save contact
        if (_editNamePos > 0) {
          _editNameBuf[_editNamePos] = '\0';
          smsContacts.set(_editPhone, _editNameBuf);
          Serial.printf("[SMSContacts] Saved: %s = %s\n", _editPhone, _editNameBuf);
        }
        if (_editReturnView == CONVERSATION) {
          refreshConversation();
        } else {
          refreshInbox();
        }
        _view = _editReturnView;
        return true;

      case '\b':  // Backspace
        if (_editNamePos > 0) {
          _editNamePos--;
          _editNameBuf[_editNamePos] = '\0';
        }
        return true;

      case 0x18:  // Shift+Backspace (cancel without saving)
        if (_editReturnView == CONVERSATION) {
          refreshConversation();
        } else {
          refreshInbox();
        }
        _view = _editReturnView;
        return true;

      default:
        if (c >= 32 && c < 127 && _editNamePos < SMS_CONTACT_NAME_LEN - 1) {
          _editNameBuf[_editNamePos++] = c;
          _editNameBuf[_editNamePos] = '\0';
        }
        return true;
    }
  }
};

#endif // SMS_SCREEN_H
#endif // HAS_4G_MODEM