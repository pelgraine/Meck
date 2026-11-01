#pragma once

// =============================================================================
// SMSScreen - SMS & Phone UI for T-Deck Pro (4G variant)
//
// Sub-views:
//   APP_MENU      — landing screen: choose Phone or SMS Inbox
//   INBOX         — list of conversations (names resolved via SMSContacts)
//   CONVERSATION  — messages for a selected contact, scrollable
//   COMPOSE       — text input for new SMS
//   CONTACTS      — browsable contacts list, pick to compose or call
//   EDIT_CONTACT  — add or edit a contact name for a phone number
//   PHONE_DIALER  — enter arbitrary phone number and call
//   DIALING_OUT   — outgoing call in progress (waiting for answer)
//   INCOMING_CALL — incoming call ringing (answer or reject)
//   IN_CALL       — active voice call with timer, DTMF, volume
//
// Navigation mirrors ChannelScreen conventions:
//   W/S: scroll     Enter: select/send    C: compose new/reply
//   Sh+Del: back      Sh+Del: cancel compose
//   D: contacts (from inbox)
//   A: add/edit contact (from conversation)
//   F: call (from conversation, contacts, or phone dialer)
//
// Guard: HAS_4G_MODEM
// =============================================================================

#ifdef HAS_4G_MODEM

#ifndef SMS_SCREEN_H
#define SMS_SCREEN_H

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include <time.h>
#include "ModemManager.h"
#include "SMSStore.h"
#include "SMSContacts.h"
#include "../NodePrefs.h"
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
  #include "ui2/Ui2RichText.h"
#endif

// Limits
#define SMS_INBOX_PAGE_SIZE     4
#define SMS_MSG_PAGE_SIZE      30
#define SMS_COMPOSE_MAX       160

class UITask;   // forward declaration

class SMSScreen : public UIScreen {
public:
  enum SubView { APP_MENU, INBOX, CONVERSATION, COMPOSE, CONTACTS, EDIT_CONTACT, PHONE_DIALER,
                 DIALING_OUT, INCOMING_CALL, IN_CALL, CALL_LOG, CONTACT_PAGE };

private:
  UITask* _task;
  NodePrefs* _prefs;
  SubView _view;

  // App menu state
  int _menuCursor;  // 0 = Dial, 1 = SMS Inbox, 2 = Call Log

  // Call log state
  CallLogRecord _callLog[SMS_CALLLOG_MAX];
  int  _callLogCount;
  int  _callLogCursor;
  int  _callLogScrollTop;
  int  _unseenMissed;      // Missed calls not yet viewed in the log (menu badge)
  bool _callWasIncoming;   // Direction of the active/last call, for log entries

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

  // Phone input state (for new conversation and phone dialer)
  char _phoneInputBuf[SMS_PHONE_LEN];
  int  _phoneInputPos;
  bool _enteringPhone;

  // Contacts list state
  int _contactsCursor;
  int _contactsScrollTop;
  SubView _contactsReturnView;  // where Q returns to from the contacts list

  // Contact page (view / add / edit a single contact)
  char _cpName[SMS_CONTACT_NAME_LEN];
  char _cpPhone[SMS_PHONE_LEN];
  char _cpOrigPhone[SMS_PHONE_LEN];  // key before an edit, for rename
  int  _cpCursor;               // 0 = Name, 1 = Number, 2 = Save
  bool _cpIsNew;
  bool _cpEditing;              // true = typing into the selected field
  char _cpEditBuf[SMS_CONTACT_NAME_LEN];
  int  _cpEditPos;
  bool _cpNeedNumber;           // set when Save is pressed with no number

  // Edit contact state
  char _editPhone[SMS_PHONE_LEN];
  char _editNameBuf[SMS_CONTACT_NAME_LEN];
  int  _editNamePos;
  bool _editIsNew;              // true = adding new, false = editing existing
  SubView _editReturnView;      // where to return after save/cancel

  // Voice call UI state
  char _callPhone[SMS_PHONE_LEN];   // Phone number for active call
  SubView _callReturnView;          // View to return to after call ends
  unsigned long _callConnectTime;   // millis() when call connected (UI timer)
  uint8_t _callVolume;              // Current speaker volume (0-5)
  uint8_t _callDotAnim;            // Animation frame for dialing dots

  // Refresh debounce
  bool _needsRefresh;
  unsigned long _lastRefresh;
  static const unsigned long REFRESH_INTERVAL = 600;

  // SD ready flag
  bool _sdReady;

  // Reload helpers
  void refreshCallLog() {
    _callLogCount = smsStore.loadCallLog(_callLog, SMS_CALLLOG_MAX);
    _unseenMissed = 0;
    for (int i = 0; i < _callLogCount; i++) {
      if (_callLog[i].type == CALL_LOG_MISSED && _callLog[i].seen == 0) _unseenMissed++;
    }
  }

  void refreshInbox() {
    _convCount = smsStore.loadConversations(_conversations, SMS_MAX_CONVERSATIONS);
  }

  void refreshConversation() {
    _msgCount = smsStore.loadMessages(_activePhone, _msgs, SMS_MSG_PAGE_SIZE);
    // Scroll to bottom (newest messages are at end now, chat-style)
    _msgScrollPos = (_msgCount > 3) ? _msgCount - 3 : 0;
  }

public:
  SMSScreen(UITask* task, NodePrefs* prefs = nullptr)
    : _task(task), _prefs(prefs), _view(APP_MENU)
    , _menuCursor(0)
    , _convCount(0), _inboxCursor(0), _inboxScrollTop(0)
    , _msgCount(0), _msgScrollPos(0)
    , _composePos(0), _composeNewConversation(false)
    , _phoneInputPos(0), _enteringPhone(false)
    , _contactsCursor(0), _contactsScrollTop(0), _contactsReturnView(INBOX)
    , _cpCursor(0), _cpIsNew(false), _cpEditing(false), _cpEditPos(0)
    , _cpNeedNumber(false)
    , _editNamePos(0), _editIsNew(false), _editReturnView(INBOX)
    , _callReturnView(APP_MENU), _callConnectTime(0), _callVolume(3), _callDotAnim(0)
    , _callLogCount(0), _callLogCursor(0), _callLogScrollTop(0), _unseenMissed(0)
    , _callWasIncoming(false)
    , _needsRefresh(false), _lastRefresh(0)
    , _sdReady(false)
  {
    memset(_composeBuf, 0, sizeof(_composeBuf));
    memset(_composePhone, 0, sizeof(_composePhone));
    memset(_phoneInputBuf, 0, sizeof(_phoneInputBuf));
    memset(_activePhone, 0, sizeof(_activePhone));
    memset(_cpName, 0, sizeof(_cpName));
    memset(_cpPhone, 0, sizeof(_cpPhone));
    memset(_cpOrigPhone, 0, sizeof(_cpOrigPhone));
    memset(_cpEditBuf, 0, sizeof(_cpEditBuf));
    memset(_editPhone, 0, sizeof(_editPhone));
    memset(_editNameBuf, 0, sizeof(_editNameBuf));
    memset(_callPhone, 0, sizeof(_callPhone));
  }

  void setSDReady(bool ready) {
    _sdReady = ready;
    if (_sdReady) {
      // Load unread/unseen state now so the lock screen shows the right
      // counts before the phone screen has ever been opened
      refreshInbox();
      refreshCallLog();
    }
  }

  // Live counts for the lock screen badge. Both clear only when the relevant
  // conversation or the call log is actually opened.
  int getUnreadSmsCount() const {
    int n = 0;
    for (int i = 0; i < _convCount; i++) n += _conversations[i].unreadCount;
    return n;
  }
  int getUnseenMissedCount() const { return _unseenMissed; }

  void activate() {
    _view = APP_MENU;
    _menuCursor = 0;
    if (_sdReady) {
      refreshInbox();
      refreshCallLog();
    }
  }

  SubView getSubView() const { return _view; }
  bool isComposing() const { return _view == COMPOSE; }
  bool isEnteringPhone() const { return _enteringPhone || _view == PHONE_DIALER; }
  bool isInCallView() const {
    return _view == DIALING_OUT || _view == INCOMING_CALL || _view == IN_CALL;
  }

  // Transition to dialing screen — used by all dial callsites
  void startCall(const char* phone) {
    strncpy(_callPhone, phone, SMS_PHONE_LEN - 1);
    _callPhone[SMS_PHONE_LEN - 1] = '\0';
    _callReturnView = _view;
    _callConnectTime = 0;
    _callVolume = 3;
    _callDotAnim = 0;
    _callWasIncoming = false;
    _view = DIALING_OUT;
    modemManager.dialCall(phone);
  }

  // Handle call events from modem (incoming, connected, ended, etc.)
  void onCallEvent(const CallEvent& evt) {
    switch (evt.type) {
      case CallEventType::INCOMING:
        Serial.printf("[SMSScreen] Incoming call from %s\n", evt.phone);
        strncpy(_callPhone, evt.phone, SMS_PHONE_LEN - 1);
        _callPhone[SMS_PHONE_LEN - 1] = '\0';
        _callConnectTime = 0;
        _callVolume = 3;
        _callWasIncoming = true;
        if (!isInCallView()) {
          _callReturnView = _view;
        }
        _view = INCOMING_CALL;
        _needsRefresh = true;
        break;

      case CallEventType::CONNECTED:
        Serial.printf("[SMSScreen] Call connected: %s\n", evt.phone);
        _callConnectTime = millis();
        _view = IN_CALL;
        _needsRefresh = true;
        break;

      case CallEventType::ENDED:
        Serial.printf("[SMSScreen] Call ended (%lus)\n", (unsigned long)evt.duration);
        if (_sdReady) {
          smsStore.appendCallLog(_callWasIncoming ? CALL_LOG_INCOMING : CALL_LOG_OUTGOING,
                                 evt.phone, evt.duration, (uint32_t)time(nullptr));
        }
        if (_view == IN_CALL || _view == DIALING_OUT) {
          // Remote hangup or network drop — return to previous view
          // "Call Ended" alert is shown by main.cpp via showAlert()
          _view = _callReturnView;
        }
        _callPhone[0] = '\0';
        _callConnectTime = 0;
        if (_view == CALL_LOG && _sdReady) refreshCallLog();
        _needsRefresh = true;
        break;

      case CallEventType::MISSED:
        Serial.printf("[SMSScreen] Missed call from %s\n", evt.phone);
        if (_sdReady) {
          smsStore.appendCallLog(CALL_LOG_MISSED, evt.phone, 0, (uint32_t)time(nullptr));
        }
        _view = _callReturnView;
        _callPhone[0] = '\0';
        _callConnectTime = 0;
        if (_sdReady) refreshCallLog();
        _needsRefresh = true;
        break;

      case CallEventType::BUSY:
        Serial.printf("[SMSScreen] Busy: %s\n", evt.phone);
        if (_sdReady) {
          smsStore.appendCallLog(CALL_LOG_OUTGOING, evt.phone, 0, (uint32_t)time(nullptr));
        }
        _view = _callReturnView;
        _callPhone[0] = '\0';
        if (_view == CALL_LOG && _sdReady) refreshCallLog();
        _needsRefresh = true;
        break;

      case CallEventType::NO_ANSWER:
        Serial.printf("[SMSScreen] No answer: %s\n", evt.phone);
        if (_sdReady) {
          smsStore.appendCallLog(CALL_LOG_OUTGOING, evt.phone, 0, (uint32_t)time(nullptr));
        }
        _view = _callReturnView;
        _callPhone[0] = '\0';
        if (_view == CALL_LOG && _sdReady) refreshCallLog();
        _needsRefresh = true;
        break;

      case CallEventType::DIAL_FAILED:
        Serial.printf("[SMSScreen] Dial failed: %s\n", evt.phone);
        _view = _callReturnView;
        _callPhone[0] = '\0';
        _needsRefresh = true;
        break;
    }
  }

  // Called from main loop when an SMS arrives (saves to store + refreshes)
  void onIncomingSMS(const char* phone, const char* body, uint32_t timestamp) {
    if (_sdReady) {
      smsStore.saveMessage(phone, body, false, timestamp);
    }
    if (_view == CONVERSATION && strcmp(_activePhone, phone) == 0) {
      smsStore.markConversationRead(_activePhone);
      refreshConversation();
    }
    // Refresh regardless of the current view so the unread total stays
    // correct for the lock screen badge
    if (_sdReady) refreshInbox();
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
      display.setTextSize(_prefs->smallTextSize());
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

#ifdef MECK_UI_V2
  // ===========================================================================
  // UI v2 (designs B10-B12 and the list pattern). Behaviour and keys are the
  // classic ones; only the drawing differs.
  // ===========================================================================

  static Ui2Icon v2SignalIcon() {
    int bars = modemManager.getSignalBars();   // 0-5
    if (bars > 4) bars = 4;
    static const Ui2Icon icons[5] = { UI2_IC_SIG0, UI2_IC_SIG1, UI2_IC_SIG2, UI2_IC_SIG3, UI2_IC_SIG4 };
    return icons[bars < 0 ? 0 : bars];
  }

  // Status header and title bar with the cellular signal as an icon chip
  void v2Frame(Ui2Canvas& c, const char* title, const char* right = nullptr) {
    Ui2Chrome::statusHeader(c);
    Ui2Chrome::titleBar(c, title, right, nullptr, v2SignalIcon());
  }

  // Modem state banner above the footer while the modem is not ready
  void v2ModemBanner(Ui2Canvas& c) {
    using namespace ui2;
    ModemState ms = modemManager.getState();
    if (ms == ModemState::READY || ms == ModemState::SENDING_SMS) return;
    char buf[48];
    snprintf(buf, sizeof(buf), MECK_TR("Modem: %s", "Modem : %s"), ModemManager::stateToString(ms));
    const int16_t y = FOOTER_Y - 26;
    c.fill(PAD, y, SCREEN_W - 2 * PAD, 20, false);
    c.rect(PAD, y, SCREEN_W - 2 * PAD, 20);
    c.icon(PAD + 6, y + 4, UI2_IC_SIM, 12);
    c.textFit(PAD + 24, Ui2Canvas::boxBaseline(UI2_F_SMALL, y, 20), SCREEN_W - 2 * PAD - 30, buf, UI2_F_SMALL);
  }

  static void v2Badge(char* buf, size_t len, int n) {
    if (n > 99) snprintf(buf, len, "99+"); else snprintf(buf, len, "%d", n);
  }

  int renderAppMenuV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    v2Frame(c, MECK_TR("Phone & SMS", "T\xC3\xA9l\xC3\xA9phone et SMS"));
    int unread = 0;
    for (int i = 0; i < _convCount; i++) unread += _conversations[i].unreadCount;
    char bMissed[6], bUnread[6];
    v2Badge(bMissed, sizeof(bMissed), _unseenMissed);
    v2Badge(bUnread, sizeof(bUnread), unread);
    struct Item { Ui2Icon icon; const char* title; const char* badge; };
    const Item items[4] = {
      { UI2_IC_PHONE,   MECK_TR("Dial", "Composer"), nullptr },
      { UI2_IC_CLOCK,   MECK_TR("Call log", "Journal d'appels"), _unseenMissed > 0 ? bMissed : nullptr },
      { UI2_IC_CONTACT, "Contacts", nullptr },
      { UI2_IC_SMS,     MECK_TR("SMS inbox", "SMS re\xC3\xA7us"), unread > 0 ? bUnread : nullptr },
    };
    for (int i = 0; i < 4; i++) {
      Ui2Row r;
      r.icon = items[i].icon;
      r.title = items[i].title;
      r.badge = items[i].badge;
      r.chevron = true;
      r.selected = (i == _menuCursor);
      Ui2List::drawRow(c, BODY_Y + i * ROW_H, r);
    }
    v2ModemBanner(c);
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Open", "Ouvrir")), ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
                            ui2Key("Q", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 3);
    ModemState ms = modemManager.getState();
    return (ms != ModemState::READY && ms != ModemState::SENDING_SMS) ? 1000 : 5000;
  }

  int renderCallLogV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _callLogCursor, _callLogCount, ROWS_PER_PAGE);
    v2Frame(c, MECK_TR("Call log", "Journal d'appels"), right);
    if (_callLogCount == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_PHONE, MECK_TR("No calls", "Aucun appel"));
    } else {
      const int start = Ui2List::pageStart(_callLogCursor, ROWS_PER_PAGE);
      for (int i = start; i < _callLogCount && i < start + ROWS_PER_PAGE; i++) {
        CallLogRecord& e = _callLog[i];
        char name[SMS_CONTACT_NAME_LEN], sub[32], hm[8], dm[8];
        if (e.phone[0]) smsContacts.displayName(e.phone, name, sizeof(name));
        else snprintf(name, sizeof(name), "%s", MECK_TR("Unknown", "Inconnu"));
        const bool missed = e.type == CALL_LOG_MISSED;
        const char* typeStr = missed ? MECK_TR("Missed", "Manqu\xC3\xA9")
                            : (e.type == CALL_LOG_INCOMING) ? MECK_TR("Incoming", "Re\xC3\xA7u") : MECK_TR("Outgoing", "\xC3\x89mis");
        if (!missed && e.duration > 0) snprintf(sub, sizeof(sub), "%s \xC2\xB7 %lu:%02lu", typeStr,
                                                (unsigned long)(e.duration / 60), (unsigned long)(e.duration % 60));
        else snprintf(sub, sizeof(sub), "%s", typeStr);
        int32_t local = (int32_t)e.timestamp + ((int32_t)_prefs->utc_offset_hours * 3600);
        time_t lt = (time_t)local;
        struct tm tmv;
        gmtime_r(&lt, &tmv);
        snprintf(hm, sizeof(hm), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
        snprintf(dm, sizeof(dm), "%02d/%02d", tmv.tm_mday, tmv.tm_mon + 1);
        Ui2Row r;
        r.icon = missed ? UI2_IC_RING : (e.type == CALL_LOG_INCOMING) ? UI2_IC_PHONE : UI2_IC_SEND;
        r.title = name;
        r.sub = sub;
        r.right = hm;
        r.right2 = dm;
        r.dot = missed && e.seen == 0;
        r.selected = (i == _callLogCursor);
        Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
      }
    }
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Call", "Appeler")), ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
                            ui2Key("D", MECK_TR("Delete", "Suppr.")), ui2Key("Q", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 4);
    return 5000;
  }

  int renderInboxV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    char right[16];
    if (_inboxCursor >= _convCount) _inboxCursor = _convCount > 0 ? _convCount - 1 : 0;
    Ui2List::pageLabel(right, sizeof(right), _inboxCursor, _convCount, ROWS_PER_PAGE);
    v2Frame(c, MECK_TR("SMS inbox", "SMS re\xC3\xA7us"), right);
    if (_convCount == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y - 28, UI2_IC_SMS, MECK_TR("No conversations", "Aucune conversation"),
                          nullptr, "C", MECK_TR("New SMS", "Nouveau SMS"));
    } else {
      const int start = Ui2List::pageStart(_inboxCursor, ROWS_PER_PAGE);
      for (int i = start; i < _convCount && i < start + ROWS_PER_PAGE; i++) {
        SMSConversation& cv = _conversations[i];
        char name[SMS_CONTACT_NAME_LEN], badge[6], age[8];
        smsContacts.displayName(cv.phone, name, sizeof(name));
        Ui2Row r;
        r.icon = UI2_IC_SMS;
        r.title = name;
        r.sub = cv.preview;
        time_t now = time(nullptr);
        if (now > 1700000000 && cv.lastTimestamp > 1700000000) {
          Ui2List::formatAge(age, sizeof(age), (int32_t)(now - cv.lastTimestamp));
          r.right = age;
        }
        if (cv.unreadCount > 0) { v2Badge(badge, sizeof(badge), cv.unreadCount); r.badge = badge; }
        r.rightMax = 40;
        r.selected = (i == _inboxCursor);
        Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
      }
    }
    v2ModemBanner(c);
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Open", "Ouvrir")), ui2Key("C", MECK_TR("New", "Nouveau")),
                            ui2Key("D", "Contacts"), ui2Key("Q", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 4);
    return 5000;
  }

  int renderContactsV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    int cnt = smsContacts.count();
    if (_contactsCursor >= cnt) _contactsCursor = cnt - 1;
    if (_contactsCursor < 0) _contactsCursor = 0;
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _contactsCursor, cnt, ROWS_PER_PAGE);
    v2Frame(c, MECK_TR("SMS contacts", "Contacts SMS"), right);
    if (cnt == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_CONTACT, MECK_TR("No contacts saved", "Aucun contact enregistr\xC3\xA9"),
                          nullptr, "A", MECK_TR("Add one", "En ajouter un"));
    } else {
      const int start = Ui2List::pageStart(_contactsCursor, ROWS_PER_PAGE);
      for (int i = start; i < cnt && i < start + ROWS_PER_PAGE; i++) {
        const SMSContact& ct = smsContacts.get(i);
        Ui2Row r;
        r.icon = UI2_IC_CONTACT;
        r.title = ct.name;
        r.sub = ct.phone;
        r.selected = (i == _contactsCursor);
        Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
      }
    }
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Open", "Ouvrir")), ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
                            ui2Key("A", MECK_TR("Add", "Ajouter")), ui2Key("Q", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 4);
    return 5000;
  }

  int renderContactPageV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    v2Frame(c, _cpIsNew ? MECK_TR("New contact", "Nouveau contact") : (_cpName[0] ? _cpName : "Contact"));
    char nameVal[SMS_CONTACT_NAME_LEN + 2], phoneVal[SMS_PHONE_LEN + 2];
    snprintf(nameVal, sizeof(nameVal), "%s%s", (_cpEditing && _cpCursor == 0) ? _cpEditBuf : _cpName,
             (_cpEditing && _cpCursor == 0) ? "_" : "");
    snprintf(phoneVal, sizeof(phoneVal), "%s%s", (_cpEditing && _cpCursor == 1) ? _cpEditBuf : _cpPhone,
             (_cpEditing && _cpCursor == 1) ? "_" : "");
    Ui2Row r0; r0.icon = UI2_IC_CONTACT; r0.title = MECK_TR("Name", "Nom"); r0.sub = nameVal;
    r0.selected = _cpCursor == 0; r0.pending = _cpEditing && _cpCursor == 0;
    Ui2Row r1; r1.icon = UI2_IC_PHONE; r1.title = MECK_TR("Number", "Num\xC3\xA9ro"); r1.sub = phoneVal;
    r1.selected = _cpCursor == 1; r1.pending = _cpEditing && _cpCursor == 1;
    Ui2Row r2; r2.icon = UI2_IC_CHECK; r2.title = MECK_TR("Save", "Enregistrer"); r2.selected = _cpCursor == 2;
    Ui2List::drawRow(c, BODY_Y, r0);
    Ui2List::drawRow(c, BODY_Y + ROW_H, r1);
    Ui2List::drawRow(c, BODY_Y + 2 * ROW_H, r2);
    if (_cpNeedNumber) {
      const int16_t y = BODY_Y + 3 * ROW_H + 8;
      c.icon(PAD, y, UI2_IC_WARN, 16);
      c.textFit(PAD + 22, Ui2Canvas::boxBaseline(UI2_F_BODY, y, 16), SCREEN_W - 2 * PAD - 22,
                MECK_TR("A number is required", "Un num\xC3\xA9ro est requis"), UI2_F_BODY);
    }
    if (_cpEditing) {
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Done", "OK")), ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")) };
      Ui2Chrome::footer(c, keys, 2);
    } else if (_cpIsNew) {
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Select", "Choisir")), ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
                              ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 3);
    } else {
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Select", "Choisir")), ui2Key("F", MECK_TR("Call", "Appeler")),
                              ui2Key("D", MECK_TR("Delete", "Suppr.")), ui2Key("Q", MECK_TR("Back", "Retour")) };
      Ui2Chrome::footer(c, keys, 4);
    }
    return 5000;
  }

  // --- Conversation: received bubbles left, sent ones right (pattern B5) ---
  int renderConversationV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    char title[SMS_CONTACT_NAME_LEN];
    smsContacts.displayName(_activePhone, title, sizeof(title));
    v2Frame(c, title);
    if (_msgCount == 0) {
      Ui2List::emptyState(c, BODY_Y, FOOTER_Y, UI2_IC_SMS, MECK_TR("No messages", "Aucun message"), nullptr,
                          "C", MECK_TR("Write", "\xC3\x89" "crire"));
    } else {
      int16_t y = BODY_Y + 6;
      const int16_t maxW = 196, pad = 6, textMax = maxW - 2 - 2 * pad;
      for (int i = _msgScrollPos; i < _msgCount; i++) {
        SMSMessage& m = _msgs[i];
        if (!m.valid) continue;
        Ui2RtLine lines[6];
        bool more = false;
        int len = strlen(m.body);
        int n = ui2RtWrap(m.body, len, textMax, UI2_F_BODY, lines, 6, &more);
        if (n == 0) { lines[0] = {0, 0, 0}; n = 1; }
        int16_t cw = 0;
        for (int k = 0; k < n; k++) if (lines[k].w > cw) cw = lines[k].w;
        if (more) cw = textMax;
        if (cw < 90) cw = 90;
        const int16_t w = cw + 2 + 2 * pad;
        const int16_t h = 14 + 2 + n * 15 + 3 + 2;
        if (y + h > FOOTER_Y - 4 && i > _msgScrollPos) break;
        const int16_t x = m.isSent ? SCREEN_W - PAD - w : PAD;
        // Header tab: filled for sent messages, outlined for received ones
        char meta[24], t[8];
        time_t now = time(nullptr);
        if (now > 1700000000 && m.timestamp > 1700000000) {
          int32_t local = (int32_t)m.timestamp + (int32_t)_prefs->utc_offset_hours * 3600;
          int hh = (local / 3600) % 24; if (hh < 0) hh += 24;
          int mm = (local / 60) % 60;   if (mm < 0) mm += 60;
          snprintf(t, sizeof(t), "%02d:%02d", hh, mm);
        } else {
          snprintf(t, sizeof(t), "--:--");
        }
        c.roundRect(x, y, w, h, 3, true);
        if (m.isSent) {
          c.fill(x + 1, y + 1, w - 2, 14, true);
          c.text(x + pad, Ui2Canvas::boxBaseline(UI2_F_CHIP, y + 1, 14), MECK_TR("You", "Vous"), UI2_F_CHIP, false);
          snprintf(meta, sizeof(meta), MECK_TR("sent %s", "envoy\xC3\xA9 %s"), t);
          c.textFit(x + 40, Ui2Canvas::boxBaseline(UI2_F_META, y + 1, 14), w - 40 - pad, meta, UI2_F_META, false, UI2_RIGHT);
        } else {
          c.hline(x + 1, y + 15, w - 2);
          c.textFit(x + pad, Ui2Canvas::boxBaseline(UI2_F_META, y + 1, 14), w - 2 * pad, t, UI2_F_META);
        }
        for (int k = 0; k < n; k++) {
          int16_t base = Ui2Canvas::lineBaseline(UI2_F_BODY, y + 16 + k * 15, 15);
          if (k == n - 1 && more) {
            c.textFit(x + pad, base, textMax, m.body + lines[k].a, UI2_F_BODY, true, UI2_LEFT, len - lines[k].a);
          } else {
            ui2RtDraw(c, x + pad, base, m.body, lines[k].a, lines[k].b, UI2_F_BODY);
          }
        }
        y += h + 6;
      }
    }
    const Ui2Key keys[] = { ui2Key("C", MECK_TR("Reply", "R\xC3\xA9pondre")), ui2Key("W/S", MECK_TR("Scroll", "D\xC3\xA9" "filer"), true),
                            ui2Key("A", MECK_TR("Add contact", "+Contact")), ui2Key("Q", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 4);
    return 5000;
  }

  // --- Compose SMS: recipient number entry, then the message box ---
  int renderComposeV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    if (_enteringPhone) {
      v2Frame(c, MECK_TR("New SMS", "Nouveau SMS"));
      c.text(PAD, BODY_Y + 22, MECK_TR("To", "\xC3\x80"), UI2_F_LABEL);
      const int16_t fy = BODY_Y + 30;
      c.roundRect(PAD, fy, SCREEN_W - 2 * PAD, 40, 3);
      int16_t ex = c.textFit(PAD + 8, Ui2Canvas::boxBaseline(UI2_F_HEAD18, fy, 40), SCREEN_W - 2 * PAD - 24,
                             _phoneInputBuf, UI2_F_HEAD18);
      c.fill(PAD + 8 + ex + 2, fy + 8, 2, 24, true);
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Next", "Suivant")), ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")) };
      Ui2Chrome::footer(c, keys, 2);
      return 2000;
    }
    char name[SMS_CONTACT_NAME_LEN], title[SMS_CONTACT_NAME_LEN + 8];
    smsContacts.displayName(_composePhone, name, sizeof(name));
    snprintf(title, sizeof(title), MECK_TR("To \xC2\xB7 %s", "\xC3\x80 \xC2\xB7 %s"), name);
    v2Frame(c, title);
    const int16_t boxY = BODY_Y + 6, barY = FOOTER_Y - 6 - 14, boxH = barY - 6 - boxY;
    const int16_t textW = SCREEN_W - 2 * PAD - 20;
    const int maxVisible = (boxH - 10) / 15;
    c.roundRect(PAD, boxY, SCREEN_W - 2 * PAD, boxH, 3);
    static Ui2RtLine lines[24];
    int n = ui2RtWrap(_composeBuf, _composePos, textW, UI2_F_BODY, lines, 24, nullptr);
    int first = n > maxVisible ? n - maxVisible : 0;
    int16_t cx = PAD + 6, cy = boxY + 5;
    for (int k = first; k < n; k++) {
      cy = boxY + 5 + (k - first) * 15;
      cx = ui2RtDraw(c, PAD + 6, Ui2Canvas::lineBaseline(UI2_F_BODY, cy, 15), _composeBuf, lines[k].a, lines[k].b, UI2_F_BODY);
    }
    if (_composePos > 0 && _composeBuf[_composePos - 1] == ' ' && n > 0) cx += Ui2Canvas::textWidth(" ", UI2_F_BODY);
    c.fill(cx + 1, cy + 1, 6, 13, true);
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%d / %d", _composePos, SMS_COMPOSE_MAX);
    int16_t cw = Ui2Canvas::textWidth(cnt, UI2_F_LABEL) + 8;
    bool nearFull = _composePos >= SMS_COMPOSE_MAX - 10;
    if (nearFull) c.fillRoundRect(SCREEN_W - PAD - cw, barY, cw, 14, 3, true);
    c.text(SCREEN_W - PAD - cw + 4, Ui2Canvas::boxBaseline(UI2_F_LABEL, barY, 14), cnt, UI2_F_LABEL, !nearFull);
    c.segBar(PAD, barY + 4, SCREEN_W - 2 * PAD - cw - 6, 6, (_composePos * 20 + SMS_COMPOSE_MAX - 1) / SMS_COMPOSE_MAX, 20);
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Send", "Envoyer")), ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")) };
    Ui2Chrome::footer(c, keys, 2);
    return 2000;
  }

  // --- Dialer (design B10): number field, 3 x 5 keypad ---
  static const int16_t V2_KP_TOP = ui2::BODY_Y + 48 + 6;
  static const int16_t V2_KP_GAP = 4;
  static int16_t v2KpRowH() { return (ui2::FOOTER_Y - 6 - V2_KP_TOP - (NUMPAD_ROWS - 1) * V2_KP_GAP) / NUMPAD_ROWS; }
  static int16_t v2KpColW() { return (ui2::SCREEN_W - 2 * ui2::PAD - (NUMPAD_COLS - 1) * V2_KP_GAP) / NUMPAD_COLS; }

  int renderPhoneDialerV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    const char* op = modemManager.getOperator();
    Ui2Chrome::titleBar(c, MECK_TR("Dialer", "Num\xC3\xA9roteur"), op[0] ? op : nullptr, nullptr, v2SignalIcon());

    // Number, right-aligned; when too long the start is cut (last digits stay)
    const int16_t fy = BODY_Y;
    const char* num = _phoneInputBuf;
    while (*num && Ui2Canvas::textWidth(num, UI2_F_HEAD18) > SCREEN_W - 2 * PAD - 6) num++;
    int16_t nw = Ui2Canvas::textWidth(num, UI2_F_HEAD18);
    c.text(SCREEN_W - PAD - 4 - nw, Ui2Canvas::boxBaseline(UI2_F_HEAD18, fy, 48), num, UI2_F_HEAD18);
    c.fill(SCREEN_W - PAD - 2, fy + 12, 2, 24, true);
    c.hline(0, fy + 47, SCREEN_W);

    static const char* letters[4][3] = { {"", "ABC", "DEF"}, {"GHI", "JKL", "MNO"}, {"PQRS", "TUV", "WXYZ"}, {"", "+", ""} };
    const int16_t rowH = v2KpRowH(), colW = v2KpColW();
    for (int row = 0; row < NUMPAD_ROWS; row++) {
      for (int col = 0; col < NUMPAD_COLS; col++) {
        const int16_t x = PAD + col * (colW + V2_KP_GAP), y = V2_KP_TOP + row * (rowH + V2_KP_GAP);
        const bool call = (row == 4 && col == 2);
        const bool callActive = call && _phoneInputPos > 0;
        if (callActive) c.fillRoundRect(x, y, colW, rowH, 3, true);
        else c.roundRect(x, y, colW, rowH, 3, true);
        if (row < 4) {
          const char* d = numpadLabel(row, col);
          const char* l = letters[row][col];
          int16_t dw = Ui2Canvas::textWidth(d, UI2_F_HEAD18), lw = l[0] ? Ui2Canvas::textWidth(l, UI2_F_KEY) + 5 : 0;
          int16_t sx = x + (colW - dw - lw) / 2, base = Ui2Canvas::boxBaseline(UI2_F_HEAD18, y, rowH);
          c.text(sx, base, d, UI2_F_HEAD18);
          if (l[0]) c.text(sx + dw + 5, base, l, UI2_F_KEY);
        } else {
          // Bottom row: +, delete, call
          const Ui2Icon ic = (col == 1) ? UI2_IC_LEFT : (col == 2) ? UI2_IC_PHONE : UI2_IC_PLUS;
          c.icon(x + (colW - 16) / 2, y + (rowH - 16) / 2, ic, 16, !callActive);
        }
      }
    }
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Call", "Appeler")), ui2Key("Bksp", MECK_TR("Delete", "Effacer")),
                            ui2Key("Sh+Del", MECK_TR("Back", "Retour")) };
    Ui2Chrome::footer(c, keys, 3);
    return 2000;
  }

  // Shared layout of the dialing / incoming screens (design B11)
  void v2CallHeader(Ui2Canvas& c, Ui2Icon icon, const char* label) {
    using namespace ui2;
    Ui2Chrome::statusHeader(c);
    char name[SMS_CONTACT_NAME_LEN];
    smsContacts.displayName(_callPhone, name, sizeof(name));
    int16_t y = HDR_H + 20;
    c.icon((SCREEN_W - 32) / 2, y, icon, 32);
    y += 32 + 8;
    int16_t lw = Ui2Canvas::textWidth(label, UI2_F_CHIP) + (int16_t)strlen(label);
    c.textTracked((SCREEN_W - lw) / 2, y + 10, label, UI2_F_CHIP, 1);
    y += 18;
    c.wrap(PAD, Ui2Canvas::lineBaseline(UI2_F_HEAD18, y, 22), SCREEN_W - 2 * PAD, 22, 2, name, UI2_F_HEAD18,
           true, true, nullptr, UI2_CENTER);
    if (strcmp(name, _callPhone) != 0) {
      int lines = c.wrap(PAD, 0, SCREEN_W - 2 * PAD, 22, 2, name, UI2_F_HEAD18, true, false);
      c.textFit(PAD, Ui2Canvas::lineBaseline(UI2_F_LABEL, y + lines * 22 + 2, 15), SCREEN_W - 2 * PAD, _callPhone,
                UI2_F_LABEL, true, UI2_CENTER);
    }
  }

  // Big action button at the bottom of the call screens
  void v2CallButton(Ui2Canvas& c, int16_t x, int16_t w, Ui2Icon icon, const char* label, bool filled) {
    using namespace ui2;
    const int16_t y = FOOTER_Y - 8 - 48;
    if (filled) c.fillRoundRect(x, y, w, 48, 3, true);
    else { c.roundRect(x, y, w, 48, 3, true); c.roundRect(x + 1, y + 1, w - 2, 46, 2, true); }
    int16_t tw = Ui2Canvas::textWidth(label, UI2_F_TITLE);
    int16_t sx = x + (w - 16 - 6 - tw) / 2;
    c.icon(sx, y + 16, icon, 16, !filled);
    c.text(sx + 22, Ui2Canvas::boxBaseline(UI2_F_TITLE, y, 48), label, UI2_F_TITLE, !filled);
  }

  int renderDialingOutV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    v2CallHeader(c, UI2_IC_PHONE, MECK_TR("CALLING", "APPEL EN COURS"));
    v2CallButton(c, PAD, SCREEN_W - 2 * PAD, UI2_IC_ENDCALL, MECK_TR("Hang up", "Raccrocher"), true);
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Hang up", "Raccrocher")) };
    Ui2Chrome::footer(c, keys, 1);
    return 2000;
  }

  int renderIncomingCallV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    v2CallHeader(c, UI2_IC_RING, MECK_TR("INCOMING CALL", "APPEL ENTRANT"));
    const int16_t w = (SCREEN_W - 2 * PAD - 8) / 2;
    v2CallButton(c, PAD, w, UI2_IC_PHONE, MECK_TR("Answer", "R\xC3\xA9pondre"), true);
    v2CallButton(c, PAD + w + 8, w, UI2_IC_ENDCALL, MECK_TR("Reject", "Rejeter"), false);
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Answer", "R\xC3\xA9pondre")), ui2Key("Sh+Del", MECK_TR("Reject", "Rejeter")) };
    Ui2Chrome::footer(c, keys, 2);
    return 2000;
  }

  // Active call (design B12): the timer counts minutes and redraws once a minute
  int renderInCallV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    char name[SMS_CONTACT_NAME_LEN];
    smsContacts.displayName(_callPhone, name, sizeof(name));
    int16_t y = HDR_H + 16;
    c.textFit(PAD, Ui2Canvas::lineBaseline(UI2_F_HEAD16, y, 20), SCREEN_W - 2 * PAD, name, UI2_F_HEAD16, true, UI2_CENTER);
    y += 20;
    if (strcmp(name, _callPhone) != 0) {
      c.textFit(PAD, Ui2Canvas::lineBaseline(UI2_F_LABEL, y, 15), SCREEN_W - 2 * PAD, _callPhone, UI2_F_LABEL, true, UI2_CENTER);
    }
    y += 24;
    unsigned long secs = _callConnectTime ? (millis() - _callConnectTime) / 1000 : 0;
    char mins[8];
    snprintf(mins, sizeof(mins), "%lu", secs / 60);
    int16_t nw = Ui2Canvas::textWidth(mins, UI2_F_NUM40), uw = Ui2Canvas::textWidth("min", UI2_F_TITLE);
    int16_t sx = (SCREEN_W - nw - 6 - uw) / 2;
    c.text(sx, y + 40, mins, UI2_F_NUM40);
    c.text(sx + nw + 6, y + 40, "min", UI2_F_TITLE);
    y += 56;
    // Volume as a 5-segment bar (W/S)
    c.textFit(PAD, Ui2Canvas::lineBaseline(UI2_F_META, y, 12), 60, MECK_TR("Volume", "Volume"), UI2_F_META);
    c.segBar(PAD + 64, y + 2, SCREEN_W - 2 * PAD - 64, 8, _callVolume, 5, 2);
    v2CallButton(c, PAD, SCREEN_W - 2 * PAD, UI2_IC_ENDCALL, MECK_TR("End call", "Raccrocher"), true);
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("End", "Fin")), ui2Key("W/S", MECK_TR("Volume", "Volume")),
                            ui2Key("0-9", "DTMF") };
    Ui2Chrome::footer(c, keys, 3);
    // Next redraw at the next whole minute of the call
    return (int)(60000 - ((millis() - _callConnectTime) % 60000)) + 50;
  }

  int renderEditContactV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    v2Frame(c, _editIsNew ? MECK_TR("Add contact", "Ajouter un contact") : MECK_TR("Edit contact", "Modifier le contact"));
    char nameVal[SMS_CONTACT_NAME_LEN + 2];
    snprintf(nameVal, sizeof(nameVal), "%s_", _editNameBuf);
    Ui2Row r0; r0.icon = UI2_IC_PHONE; r0.title = MECK_TR("Number", "Num\xC3\xA9ro"); r0.sub = _editPhone;
    Ui2Row r1; r1.icon = UI2_IC_CONTACT; r1.title = MECK_TR("Name", "Nom"); r1.sub = nameVal; r1.selected = true; r1.pending = true;
    Ui2List::drawRow(c, BODY_Y, r0);
    Ui2List::drawRow(c, BODY_Y + ROW_H, r1);
    const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Save", "Enregistrer")), ui2Key("Sh+Del", MECK_TR("Cancel", "Annuler")) };
    Ui2Chrome::footer(c, keys, 2);
    return 2000;
  }
#endif

  int render(DisplayDriver& display) override {
    _lastRefresh = millis();
#ifdef MECK_UI_V2
    switch (_view) {
      case APP_MENU:      return renderAppMenuV2(display);
      case CALL_LOG:      return renderCallLogV2(display);
      case INBOX:         return renderInboxV2(display);
      case CONTACTS:      return renderContactsV2(display);
      case CONTACT_PAGE:  return renderContactPageV2(display);
      case EDIT_CONTACT:  return renderEditContactV2(display);
      case CONVERSATION:  return renderConversationV2(display);
      case COMPOSE:       return renderComposeV2(display);
      case PHONE_DIALER:  return renderPhoneDialerV2(display);
      case DIALING_OUT:   return renderDialingOutV2(display);
      case INCOMING_CALL: return renderIncomingCallV2(display);
      case IN_CALL:       return renderInCallV2(display);
    }
#endif

    switch (_view) {
      case APP_MENU:      return renderAppMenu(display);
      case CALL_LOG:      return renderCallLog(display);
      case CONTACT_PAGE:  return renderContactPage(display);
      case INBOX:         return renderInbox(display);
      case CONVERSATION:  return renderConversation(display);
      case COMPOSE:       return renderCompose(display);
      case CONTACTS:      return renderContacts(display);
      case EDIT_CONTACT:  return renderEditContact(display);
      case PHONE_DIALER:  return renderPhoneDialer(display);
      case DIALING_OUT:   return renderDialingOut(display);
      case INCOMING_CALL: return renderIncomingCall(display);
      case IN_CALL:       return renderInCall(display);
    }
    return 1000;
  }

  // ---- App menu (landing screen) ----
  int renderAppMenu(DisplayDriver& display) {
    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("Phone & SMS", "T\xC3\xA9l\xC3\xA9phone et SMS"));

    // Signal strength at top-right
    renderSignalIndicator(display, display.width() - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    // Menu items
    display.setTextSize(1);
    int y = 24;
    int lineHeight = 16;

    // Item 0: Phone
    display.setCursor(4, y);
    display.setColor(_menuCursor == 0 ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    if (_menuCursor == 0) display.print("> ");
    else display.print("  ");
    display.print(MECK_TR("Dial", "Composer"));

    y += lineHeight;

    // Item 1: Call Log
    display.setCursor(4, y);
    display.setColor(_menuCursor == 1 ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    if (_menuCursor == 1) display.print("> ");
    else display.print("  ");
    display.print(MECK_TR("Call Log", "Journal d'appels"));

    // Show unseen missed-call count (hidden when there are none)
    if (_unseenMissed > 0) {
      char countHint[12];
      snprintf(countHint, sizeof(countHint), " [%d]", _unseenMissed);
      display.setColor(DisplayDriver::LIGHT);
      display.print(countHint);
    }

    y += lineHeight;

    // Item 2: Contacts
    display.setCursor(4, y);
    display.setColor(_menuCursor == 2 ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    if (_menuCursor == 2) display.print("> ");
    else display.print("  ");
    display.print("Contacts");

    y += lineHeight;

    // Item 3: SMS Inbox
    display.setCursor(4, y);
    display.setColor(_menuCursor == 3 ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    if (_menuCursor == 3) display.print("> ");
    else display.print("  ");
    display.print(MECK_TR("SMS Inbox", "SMS re\xC3\xA7us"));

    // Show unread count hint (hidden when there are none)
    int unread = 0;
    for (int i = 0; i < _convCount; i++) unread += _conversations[i].unreadCount;
    if (unread > 0) {
      char countHint[12];
      snprintf(countHint, sizeof(countHint), " [%d]", unread);
      display.setColor(DisplayDriver::LIGHT);
      display.print(countHint);
    }

    // Modem status indicator 
    ModemState ms = modemManager.getState();
    display.setTextSize(_prefs->smallTextSize());
    display.setCursor(4, y + lineHeight + 8);
    if (ms == ModemState::OFF || ms == ModemState::POWERING_ON || 
        ms == ModemState::INITIALIZING) {
      display.setColor(DisplayDriver::YELLOW);
      display.print(MECK_TR("Please wait...", "Veuillez patienter..."));
    } else if (ms == ModemState::ERROR) {
      display.setColor(DisplayDriver::YELLOW);
      char statBuf[40];
      snprintf(statBuf, sizeof(statBuf), MECK_TR("Modem: %s", "Modem : %s"), ModemManager::stateToString(ms));
      display.print(statBuf);
    } else if (ms == ModemState::REGISTERING || ms == ModemState::READY || 
               ms == ModemState::SENDING_SMS) {
      display.setColor(DisplayDriver::GREEN);
      display.print(MECK_TR("Ready!", "Pr\xC3\xAAt !"));
    }
    display.setTextSize(1);

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Q:Back", "Q:Retour"));
    const char* rt = MECK_TR("Ent:Open", "Ent:Ouvrir");
    display.setCursor(display.width() - display.getTextWidth(rt) - 6, footerY);
    display.print(rt);

    if (ms != ModemState::READY && ms != ModemState::SENDING_SMS) {
      return 1000;
    }
    return 5000;
  }

  // ---- Phone dialer with touch numpad ----

  // Numpad layout constants — computed dynamically from display dimensions
  static const int NUMPAD_ROWS   = 5;
  static const int NUMPAD_COLS   = 3;

  // Button labels: [row][col]
  const char* numpadLabel(int row, int col) const {
    static const char* labels[5][3] = {
      {"1", "2", "3"},
      {"4", "5", "6"},
      {"7", "8", "9"},
      {"*", "0", "#"},
      {"+", "DEL", "CALL"}
    };
    static const char* labels_fr[5][3] = {
      {"1", "2", "3"},
      {"4", "5", "6"},
      {"7", "8", "9"},
      {"*", "0", "#"},
      {"+", "EFF", "APPEL"}
    };
    return MECK_TR(labels[row][col], labels_fr[row][col]);
  }

  // Button character values: '\b' = backspace, '\r' = call
  char numpadChar(int row, int col) const {
    static const char chars[5][3] = {
      {'1', '2', '3'},
      {'4', '5', '6'},
      {'7', '8', '9'},
      {'*', '0', '#'},
      {'+', '\b', '\r'}
    };
    return chars[row][col];
  }

  int renderPhoneDialer(DisplayDriver& display) {
    int W = display.width();
    int H = display.height();

    // Layout regions (dynamic based on display size)
    int headerH    = 12;
    int phoneFieldH = 14;
    int footerH    = 12;
    int numpadTop  = headerH + phoneFieldH;
    int numpadH    = H - numpadTop - footerH;
    int rowH       = numpadH / NUMPAD_ROWS;
    int colW       = W / NUMPAD_COLS;

    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("Dial Number", "Composer un num\xC3\xA9ro"));

    // Signal strength at top-right
    renderSignalIndicator(display, W - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, headerH - 1, W, 1);

    // Phone number field
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(2, headerH + 2);
    if (_phoneInputPos > 0) {
      display.print(_phoneInputBuf);
    }
    display.print("_");

    // Separator above numpad
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, numpadTop - 1, W, 1);

    // Draw numpad grid
    for (int row = 0; row < NUMPAD_ROWS; row++) {
      int y = numpadTop + row * rowH;

      // Row separator
      if (row > 0) {
        display.setColor(DisplayDriver::DARK);
        display.drawRect(0, y, W, 1);
      }

      for (int col = 0; col < NUMPAD_COLS; col++) {
        int x = col * colW;

        // Column separator
        if (col > 0) {
          display.setColor(DisplayDriver::DARK);
          display.drawRect(x, y, 1, rowH);
        }

        // Button label - centered in cell
        const char* label = numpadLabel(row, col);
        bool isAction = (row == 4);  // Bottom row has action buttons

        if (isAction) {
          display.setTextSize(_prefs->smallTextSize());
          if (col == 2 && _phoneInputPos > 0) {
            display.setColor(DisplayDriver::GREEN);  // CALL
          } else if (col == 1) {
            display.setColor(DisplayDriver::YELLOW);  // DEL
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
        } else {
          display.setTextSize(1);
          display.setColor(DisplayDriver::LIGHT);
        }

        uint16_t textW = display.getTextWidth(label);
        int textH = isAction ? 7 : 8;
        int cx = x + (colW - textW) / 2;
        int cy = y + (rowH - textH) / 2;
        display.setCursor(cx, cy);
        display.print(label);
      }
    }

    // Footer
    display.setTextSize(1);
    int footerY = H - footerH;
    display.drawRect(0, footerY - 1, W, 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Sh+Del:Bk", "S+D:Ret"));
    if (_phoneInputPos > 0) {
      const char* rt = MECK_TR("Ent:Call", "Ent:Appel");
      display.setCursor(W - display.getTextWidth(rt) - 2, footerY);
      display.print(rt);
    } else {
      // Hint: letter keys type numbers directly
      display.setColor(DisplayDriver::DARK);
      const char* hint = "W-C=1-9";
      display.setCursor(W - display.getTextWidth(hint) - 2, footerY);
      display.print(hint);
    }

    return 2000;
  }

  // ---- Inbox ----
  // ---- Call log ----
  int renderCallLog(DisplayDriver& display) {
    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("Call Log", "Journal d'appels"));

    // Signal strength at top-right
    renderSignalIndicator(display, display.width() - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    if (_callLogCount == 0) {
      display.setTextSize(_prefs->smallTextSize());
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 20);
      display.print(MECK_TR("No calls", "Aucun appel"));
      display.setTextSize(1);
    } else {
      display.setTextSize(_prefs->smallTextSize());
      int lineHeight = _prefs->smallLineH() + 1;
      int y = 14;

      int visibleCount = (display.height() - 14 - 14) / (lineHeight * 2 + 2);
      if (visibleCount < 1) visibleCount = 1;

      // Adjust scroll to keep cursor visible
      if (_callLogCursor < _callLogScrollTop) _callLogScrollTop = _callLogCursor;
      if (_callLogCursor >= _callLogScrollTop + visibleCount) {
        _callLogScrollTop = _callLogCursor - visibleCount + 1;
      }

      for (int vi = 0; vi < visibleCount && (_callLogScrollTop + vi) < _callLogCount; vi++) {
        int idx = _callLogScrollTop + vi;
        CallLogRecord& e = _callLog[idx];

        bool selected = (idx == _callLogCursor);

        // Resolve contact name (shows name if saved, phone otherwise)
        char dispName[SMS_CONTACT_NAME_LEN];
        if (e.phone[0]) {
          smsContacts.displayName(e.phone, dispName, sizeof(dispName));
        } else {
          strncpy(dispName, MECK_TR("Unknown", "Inconnu"), sizeof(dispName) - 1);
          dispName[sizeof(dispName) - 1] = '\0';
        }

        display.setCursor(0, y);
        display.setColor(selected ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        if (selected) display.print("> ");
        display.print(dispName);
        y += lineHeight;

        // Detail line: type, duration for connected calls, local date/time
        const char* typeStr = (e.type == CALL_LOG_MISSED) ? MECK_TR("Missed", "Manqu\xC3\xA9")
                            : (e.type == CALL_LOG_INCOMING) ? MECK_TR("In", "Re\xC3\xA7u") : MECK_TR("Out", "\xC3\x89mis");
        int32_t local = (int32_t)e.timestamp + ((int32_t)_prefs->utc_offset_hours * 3600);
        time_t lt = (time_t)local;
        struct tm tmv;
        gmtime_r(&lt, &tmv);
        char detail[36];
        if (e.type != CALL_LOG_MISSED && e.duration > 0) {
          snprintf(detail, sizeof(detail), "%s %lu:%02lu  %02d/%02d %02d:%02d",
                   typeStr, (unsigned long)(e.duration / 60), (unsigned long)(e.duration % 60),
                   tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_hour, tmv.tm_min);
        } else {
          snprintf(detail, sizeof(detail), "%s  %02d/%02d %02d:%02d",
                   typeStr, tmv.tm_mday, tmv.tm_mon + 1, tmv.tm_hour, tmv.tm_min);
        }
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(12, y);
        display.print(detail);
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
    display.print(MECK_TR("Q:Back", "Q:Ret"));
    const char* mid = MECK_TR("D:Del", "D:Suppr");
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);
    const char* rt = MECK_TR("Ent:Dial", "Ent:Appel");
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

    return 5000;
  }

  bool handleCallLogInput(char c) {
    switch (c) {
      case 'w': case 'W':
        if (_callLogCursor > 0) _callLogCursor--;
        return true;

      case 's': case 'S':
        if (_callLogCursor < _callLogCount - 1) _callLogCursor++;
        return true;

      case '\r':  // Enter - dial selected entry
        if (_callLogCount > 0 && _callLogCursor < _callLogCount
            && _callLog[_callLogCursor].phone[0]) {
          startCall(_callLog[_callLogCursor].phone);
        }
        return true;

      case 'd': case 'D':  // Delete selected entry
        if (_callLogCount > 0 && _callLogCursor < _callLogCount && _sdReady) {
          smsStore.deleteCallLogEntry(_callLogCursor);
          refreshCallLog();
          if (_callLogCursor >= _callLogCount && _callLogCursor > 0) _callLogCursor--;
        }
        return true;

      case 'q':
      case KEY_CANCEL:  // Back to app menu, on the Call log item
        _view = APP_MENU;
        _menuCursor = 1;
        return true;

      default:
        return false;
    }
  }

  int renderInbox(DisplayDriver& display) {
    ModemState ms = modemManager.getState();

    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("SMS Inbox", "SMS re\xC3\xA7us"));

    // Signal strength at top-right
    renderSignalIndicator(display, display.width() - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    if (_convCount == 0) {
      display.setTextSize(_prefs->smallTextSize());
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 20);
      display.print(MECK_TR("No conversations", "Aucune conversation"));
      display.setCursor(0, 32);
      display.print(MECK_TR("Press C for new SMS", "C pour un nouveau SMS"));

      if (ms != ModemState::READY) {
        display.setCursor(0, 48);
        display.setColor(DisplayDriver::YELLOW);
        char statBuf[40];
        snprintf(statBuf, sizeof(statBuf), MECK_TR("Modem: %s", "Modem : %s"), ModemManager::stateToString(ms));
        display.print(statBuf);
      }
      display.setTextSize(1);
    } else {
      display.setTextSize(_prefs->smallTextSize());
      int lineHeight = _prefs->smallLineH() + 1;
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
    display.print(MECK_TR("Q:Back", "Q:Ret"));
    const char* mid = "D:Contacts";
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);
    const char* rt = MECK_TR("C:New", "C:\xC3\x89" "crire");
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
      display.setTextSize(_prefs->smallTextSize());
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 25);
      display.print(MECK_TR("No messages", "Aucun message"));
      display.setTextSize(1);
    } else {
      display.setTextSize(_prefs->smallTextSize());
      int lineHeight = _prefs->smallLineH() + 1;
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
          else if (age < 3600)  snprintf(timeStr, sizeof(timeStr), MECK_TR("%lum", "%lumin"), (unsigned long)(age / 60));
          else if (age < 86400) snprintf(timeStr, sizeof(timeStr), "%luh", (unsigned long)(age / 3600));
          else                  snprintf(timeStr, sizeof(timeStr), MECK_TR("%lud", "%luj"), (unsigned long)(age / 86400));
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
    display.print(MECK_TR("Q:Bk A:Add Contact", "Q:Ret A:+Contact"));
    const char* rt = MECK_TR("C:Reply", "C:R\xC3\xA9p.");
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
      display.print(MECK_TR("To: ", "\xC3\x80 : "));
      display.setColor(DisplayDriver::LIGHT);
      display.print(_phoneInputBuf);
      display.print("_");
    } else {
      // Show contact name if available
      char dispName[SMS_CONTACT_NAME_LEN];
      smsContacts.displayName(_composePhone, dispName, sizeof(dispName));
      char toLabel[40];
      snprintf(toLabel, sizeof(toLabel), MECK_TR("To: %s", "\xC3\x80 : %s"), dispName);
      display.print(toLabel);
    }

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    if (!_enteringPhone) {
      // Message body
      display.setCursor(0, 14);
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(_prefs->smallTextSize());

      uint16_t testWidth = display.getTextWidth("MMMMMMMMMM");
      int charsPerLine = (testWidth > 0) ? (display.width() * 10) / testWidth : 20;
      if (charsPerLine < 12) charsPerLine = 12;

      int composeLH = _prefs->smallLineH() + 1;
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
          y += composeLH;
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
      display.print(MECK_TR("Phone#", "Num\xC3\xA9ro"));
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
  // ---- Contact page: view / add / edit one contact ----
  void openContactPage(const char* name, const char* phone, bool isNew) {
    strncpy(_cpName, name, SMS_CONTACT_NAME_LEN - 1);
    _cpName[SMS_CONTACT_NAME_LEN - 1] = '\0';
    strncpy(_cpPhone, phone, SMS_PHONE_LEN - 1);
    _cpPhone[SMS_PHONE_LEN - 1] = '\0';
    strncpy(_cpOrigPhone, phone, SMS_PHONE_LEN - 1);
    _cpOrigPhone[SMS_PHONE_LEN - 1] = '\0';
    _cpIsNew = isNew;
    _cpCursor = 0;
    _cpEditing = false;
    _cpEditPos = 0;
    _cpEditBuf[0] = '\0';
    _cpNeedNumber = false;
    _view = CONTACT_PAGE;
  }

  int renderContactPage(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(_cpIsNew ? MECK_TR("New Contact", "Nouveau contact") : "Contact");

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    display.setTextSize(1);
    int lineHeight = 16;
    int y = 24;
    char rowBuf[SMS_CONTACT_NAME_LEN + 16];

    // Row 0: Name
    display.setCursor(4, y);
    display.setColor(_cpCursor == 0 ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    display.print(_cpCursor == 0 ? "> " : "  ");
    if (_cpEditing && _cpCursor == 0) {
      snprintf(rowBuf, sizeof(rowBuf), MECK_TR("Name: %s_", "Nom : %s_"), _cpEditBuf);
    } else {
      snprintf(rowBuf, sizeof(rowBuf), MECK_TR("Name: %s", "Nom : %s"), _cpName);
    }
    display.print(rowBuf);
    y += lineHeight;

    // Row 1: Number
    display.setCursor(4, y);
    display.setColor(_cpCursor == 1 ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    display.print(_cpCursor == 1 ? "> " : "  ");
    if (_cpEditing && _cpCursor == 1) {
      snprintf(rowBuf, sizeof(rowBuf), MECK_TR("Number: %s_", "Num\xC3\xA9ro : %s_"), _cpEditBuf);
    } else {
      snprintf(rowBuf, sizeof(rowBuf), MECK_TR("Number: %s", "Num\xC3\xA9ro : %s"), _cpPhone);
    }
    display.print(rowBuf);
    y += lineHeight;

    // Row 2: Save
    display.setCursor(4, y);
    display.setColor(_cpCursor == 2 ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
    display.print(_cpCursor == 2 ? "> " : "  ");
    display.print(MECK_TR("Save", "Enregistrer"));
    y += lineHeight;

    if (_cpNeedNumber) {
      display.setTextSize(_prefs->smallTextSize());
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(4, y + 4);
      display.print(MECK_TR("A number is required", "Un num\xC3\xA9ro est requis"));
      display.setTextSize(1);
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    if (_cpEditing) {
      display.print(MECK_TR("Sh+Del:Cancel", "Sh+Del:Annuler"));
      const char* rt = MECK_TR("Ent:Done", "Ent:OK");
      display.setCursor(display.width() - display.getTextWidth(rt) - 6, footerY);
      display.print(rt);
    } else {
      display.print(MECK_TR("Q:Back", "Q:Ret"));
      if (!_cpIsNew) {
        const char* mid = MECK_TR("F:Dial D:Del", "F:Appel D:Sup");
        display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
        display.print(mid);
      }
      const char* rt = MECK_TR("Ent:Sel", "Ent:S\xC3\xA9l");
      display.setCursor(display.width() - display.getTextWidth(rt) - 6, footerY);
      display.print(rt);
    }

    return 5000;
  }

  bool handleContactPageInput(char c) {
    // --- Field editing ---
    if (_cpEditing) {
      if (c == '\r') {
        _cpEditBuf[_cpEditPos] = '\0';
        if (_cpCursor == 0) {
          strncpy(_cpName, _cpEditBuf, SMS_CONTACT_NAME_LEN - 1);
          _cpName[SMS_CONTACT_NAME_LEN - 1] = '\0';
        } else {
          strncpy(_cpPhone, _cpEditBuf, SMS_PHONE_LEN - 1);
          _cpPhone[SMS_PHONE_LEN - 1] = '\0';
        }
        _cpEditing = false;
        return true;
      }
      if (c == 0x18 || c == KEY_CANCEL) {  // Shift+Backspace -- abandon this field edit
        _cpEditing = false;
        return true;
      }
      if (c == '\b') {
        if (_cpEditPos > 0) {
          _cpEditPos--;
          _cpEditBuf[_cpEditPos] = '\0';
        }
        return true;
      }
      int limit = (_cpCursor == 0) ? SMS_CONTACT_NAME_LEN - 1 : SMS_PHONE_LEN - 1;
      if (c >= 32 && c < 127 && _cpEditPos < limit) {
        _cpEditBuf[_cpEditPos++] = c;
        _cpEditBuf[_cpEditPos] = '\0';
      }
      return true;  // consume all keys while editing
    }

    // --- Navigation ---
    switch (c) {
      case 'w': case 'W':
        if (_cpCursor > 0) _cpCursor--;
        _cpNeedNumber = false;
        return true;

      case 's': case 'S':
        if (_cpCursor < 2) _cpCursor++;
        _cpNeedNumber = false;
        return true;

      case '\r':
        if (_cpCursor == 2) {
          saveContactPage();
        } else {
          const char* src = (_cpCursor == 0) ? _cpName : _cpPhone;
          strncpy(_cpEditBuf, src, sizeof(_cpEditBuf) - 1);
          _cpEditBuf[sizeof(_cpEditBuf) - 1] = '\0';
          _cpEditPos = strlen(_cpEditBuf);
          _cpEditing = true;
          _cpNeedNumber = false;
        }
        return true;

      case 'f': case 'F':  // Dial this contact
        if (!_cpIsNew && _cpPhone[0]) startCall(_cpPhone);
        return true;

      case 'd': case 'D':  // Delete this contact
        if (!_cpIsNew && _cpOrigPhone[0]) {
          smsContacts.remove(_cpOrigPhone);
          Serial.printf("[SMSContacts] Deleted: %s\n", _cpOrigPhone);
          returnToContacts();
        }
        return true;

      case 'q':
      case KEY_CANCEL:  // Back without saving
        returnToContacts();
        return true;

      default:
        return false;
    }
  }

  void saveContactPage() {
    if (_cpPhone[0] == '\0') {
      _cpNeedNumber = true;   // number is the storage key, so it cannot be blank
      return;
    }
    // Renaming the number is a remove-then-add, since contacts are keyed on it
    if (!_cpIsNew && _cpOrigPhone[0] && strcmp(_cpOrigPhone, _cpPhone) != 0) {
      smsContacts.remove(_cpOrigPhone);
    }
    smsContacts.set(_cpPhone, _cpName);
    Serial.printf("[SMSContacts] Saved: %s = %s\n", _cpPhone, _cpName);
    returnToContacts();
  }

  void returnToContacts() {
    int cnt = smsContacts.count();
    if (_contactsCursor >= cnt) _contactsCursor = (cnt > 0) ? cnt - 1 : 0;
    _view = CONTACTS;
  }

  int renderContacts(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("SMS Contacts", "Contacts SMS"));

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    int cnt = smsContacts.count();

    if (cnt == 0) {
      display.setTextSize(_prefs->smallTextSize());
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, 25);
      display.print(MECK_TR("No contacts saved", "Aucun contact enregistr\xC3\xA9"));
      display.setCursor(0, 37);
      display.print(MECK_TR("Press A to add one", "A pour en ajouter un"));
      display.setTextSize(1);
    } else {
      display.setTextSize(_prefs->smallTextSize());
      int lineHeight = _prefs->smallLineH() + 1;
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
    display.print(MECK_TR("Q:Back", "Q:Ret"));
    const char* mid = MECK_TR("A:Add", "A:Ajouter");
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);
    const char* rt = MECK_TR("Ent:Open", "Ent:Ouvrir");
    display.setCursor(display.width() - display.getTextWidth(rt) - 6, footerY);
    display.print(rt);

    return 5000;
  }

  // ---- Edit contact ----
  int renderEditContact(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(_editIsNew ? MECK_TR("Add Contact", "Ajouter un contact") : MECK_TR("Edit Contact", "Modifier le contact"));

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    // Phone number (read-only)
    display.setTextSize(_prefs->smallTextSize());
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, 16);
    display.print(MECK_TR("Phone: ", "Num\xC3\xA9ro : "));
    display.print(_editPhone);

    // Name input
    display.setCursor(0, 30);
    display.setColor(DisplayDriver::YELLOW);
    display.print(MECK_TR("Name: ", "Nom : "));
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
    const char* rt = MECK_TR("Ent:Save", "Ent:Enreg.");
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

    return 2000;
  }

  // ---- Dialing out (waiting for remote answer) ----
  int renderDialingOut(DisplayDriver& display) {
    int W = display.width();
    int H = display.height();

    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("Calling", "Appel"));

    renderSignalIndicator(display, W - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, W, 1);

    // Contact name (left-aligned)
    char dispName[SMS_CONTACT_NAME_LEN];
    smsContacts.displayName(_callPhone, dispName, sizeof(dispName));

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(4, 20);
    display.print(dispName);

    // Phone number below name (smaller, dimmer)
    display.setTextSize(_prefs->smallTextSize());
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(4, 36);
    display.print(_callPhone);

    // Animated dots (centered)
    _callDotAnim = (_callDotAnim + 1) % 4;
    char dots[4] = {0};
    for (int i = 0; i < (int)_callDotAnim; i++) dots[i] = '.';

    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    const char* dialLabel = MECK_TR("Dialing", "Num\xC3\xA9rotation");
    uint16_t dialW = display.getTextWidth(dialLabel);
    display.setCursor((W - dialW) / 2 - 6, H / 2 + 4);
    display.print(dialLabel);
    display.print(dots);

    // Footer
    display.setTextSize(1);
    int footerY = H - 12;
    display.drawRect(0, footerY - 2, W, 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Ent/Sh+Del:Hang up", "Ent/Sh+Del:Raccrocher"));

    return 800;  // Fast refresh for dot animation
  }

  // ---- Incoming call (ringing) ----
  int renderIncomingCall(DisplayDriver& display) {
    int W = display.width();
    int H = display.height();

    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("Incoming Call", "Appel entrant"));

    renderSignalIndicator(display, W - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, W, 1);

    // Caller name (left-aligned)
    char dispName[SMS_CONTACT_NAME_LEN];
    smsContacts.displayName(_callPhone, dispName, sizeof(dispName));

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(4, 20);
    display.print(dispName);

    // Phone number below name (smaller, dimmer)
    display.setTextSize(_prefs->smallTextSize());
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(4, 36);
    display.print(_callPhone);

    // Ringing indicator (centered)
    _callDotAnim = (_callDotAnim + 1) % 4;
    char dots[4] = {0};
    for (int i = 0; i < (int)_callDotAnim; i++) dots[i] = '.';

    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    const char* ringLabel = MECK_TR("Ringing", "Sonnerie");
    uint16_t ringW = display.getTextWidth(ringLabel);
    display.setCursor((W - ringW) / 2 - 6, H / 2 + 4);
    display.print(ringLabel);
    display.print(dots);

    // Footer
    display.setTextSize(1);
    int footerY = H - 12;
    display.drawRect(0, footerY - 2, W, 1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Ent:Answer", "Ent:R\xC3\xA9pondre"));
    const char* rt = MECK_TR("Sh+Del:Reject", "Sh+Del:Rejet");
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(W - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

    return 800;  // Fast refresh for ring animation
  }

  // ---- In call (active voice call) ----
  int renderInCall(DisplayDriver& display) {
    int W = display.width();
    int H = display.height();

    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print(MECK_TR("In Call", "En appel"));

    renderSignalIndicator(display, W - 2, 0);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, W, 1);

    // Contact name (left-aligned)
    char dispName[SMS_CONTACT_NAME_LEN];
    smsContacts.displayName(_callPhone, dispName, sizeof(dispName));

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(4, 20);
    display.print(dispName);

    // Phone number below name (smaller, dimmer)
    display.setTextSize(_prefs->smallTextSize());
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(4, 36);
    display.print(_callPhone);

    // Call timer (centered)
    unsigned long elapsed = 0;
    if (_callConnectTime > 0) {
      elapsed = (millis() - _callConnectTime) / 1000;
    }
    char timeBuf[12];
    snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu", elapsed / 60, elapsed % 60);

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    uint16_t timerW = display.getTextWidth(timeBuf);
    display.setCursor((W - timerW) / 2, H / 2 + 4);
    display.print(timeBuf);

    // Volume (left-aligned)
    display.setTextSize(_prefs->smallTextSize());
    display.setColor(DisplayDriver::LIGHT);
    char volLabel[12];
    snprintf(volLabel, sizeof(volLabel), "Vol: %d/5", _callVolume);
    display.setCursor(4, H / 2 + 28);
    display.print(volLabel);
    display.setTextSize(1);

    // Footer
    display.setTextSize(1);
    int footerY = H - 12;
    display.drawRect(0, footerY - 2, W, 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print(MECK_TR("Ent:Hang  W/S:Vol 0-9:DTMF", "Ent:Racc.  W/S:Vol 0-9:DTMF"));

    return 1000;  // 1s refresh for timer
  }

  // =========================================================================
  // INPUT HANDLING
  // =========================================================================

  bool handleInput(char c) override {
    switch (_view) {
      case APP_MENU:      return handleAppMenuInput(c);
      case CALL_LOG:      return handleCallLogInput(c);
      case CONTACT_PAGE:  return handleContactPageInput(c);
      case INBOX:         return handleInboxInput(c);
      case CONVERSATION:  return handleConversationInput(c);
      case COMPOSE:       return handleComposeInput(c);
      case CONTACTS:      return handleContactsInput(c);
      case EDIT_CONTACT:  return handleEditContactInput(c);
      case PHONE_DIALER:  return handlePhoneDialerInput(c);
      case DIALING_OUT:   return handleDialingOutInput(c);
      case INCOMING_CALL: return handleIncomingCallInput(c);
      case IN_CALL:       return handleInCallInput(c);
    }
    return false;
  }

  // ---- App menu input ----
  bool handleAppMenuInput(char c) {
    switch (c) {
      case 'w': case 'W':
        if (_menuCursor > 0) _menuCursor--;
        return true;

      case 's': case 'S':
        if (_menuCursor < 3) _menuCursor++;
        return true;

      case '\r':  // Enter - select menu item
        if (_menuCursor == 0) {
          // Phone dialer
          _phoneInputBuf[0] = '\0';
          _phoneInputPos = 0;
          _view = PHONE_DIALER;
        } else if (_menuCursor == 1) {
          // Call Log
          if (_sdReady) {
            smsStore.markMissedSeen();
            refreshCallLog();
          }
          _callLogCursor = 0;
          _callLogScrollTop = 0;
          _view = CALL_LOG;
        } else if (_menuCursor == 2) {
          // Contacts
          _contactsCursor = 0;
          _contactsScrollTop = 0;
          _contactsReturnView = APP_MENU;
          _view = CONTACTS;
        } else {
          // SMS Inbox
          if (_sdReady) refreshInbox();
          _inboxCursor = 0;
          _inboxScrollTop = 0;
          _view = INBOX;
        }
        return true;

      case KEY_CANCEL:  // Back to home (handled by main.cpp)
        return false;

      default:
        return false;
    }
  }

  // ---- Phone dialer input (keyboard) ----
  //
  // Three ways to enter digits:
  //   1. Touch the on-screen numpad
  //   2. Sym+key (normal keyboard number entry)
  //   3. Just press the letter key — the dialer maps it automatically
  //      using the silk-screened number labels on the keyboard:
  //        w=1 e=2 r=3 | s=4 d=5 f=6 | z=7 x=8 c=9
  //        q=# a=* o=+ | 0=mic key
  // The same mapping applies to the SMS recipient number, so no field that
  // takes only a phone number needs Sym (it is one-shot, one Sym per digit).

  // Bare Mic key (KB_KEY_MIC); its silk-screen label is 0
  static const char DIAL_KEY_MIC = 0x02;

  // Map a letter key to its dialer equivalent (0 = no mapping)
  char dialerKeyMap(char c) {
    switch (c) {
      case 'w': return '1';  case 'e': return '2';  case 'r': return '3';
      case 's': return '4';  case 'd': return '5';  case 'f': return '6';
      case 'z': return '7';  case 'x': return '8';  case 'c': return '9';
      case DIAL_KEY_MIC: return '0';
      case 'q': return '#';  case 'a': return '*';  case 'o': return '+';
      default:  return 0;
    }
  }

  bool handlePhoneDialerInput(char c) {
    switch (c) {
      case '\r':  // Enter - place call
        if (_phoneInputPos > 0) {
          _phoneInputBuf[_phoneInputPos] = '\0';
          startCall(_phoneInputBuf);
        }
        return true;

      case '\b':  // Backspace
        if (_phoneInputPos > 0) {
          _phoneInputPos--;
          _phoneInputBuf[_phoneInputPos] = '\0';
        }
        return true;

      case KEY_CANCEL:  // Back to app menu
        _phoneInputBuf[0] = '\0';
        _phoneInputPos = 0;
        _view = APP_MENU;
        return true;

      default:
        // Accept phone number characters directly (from sym+key)
        if ((c >= '0' && c <= '9') || c == '+' || c == '*' || c == '#') {
          if (_phoneInputPos < SMS_PHONE_LEN - 1) {
            _phoneInputBuf[_phoneInputPos++] = c;
            _phoneInputBuf[_phoneInputPos] = '\0';
          }
          return true;
        }
        // Map plain letter keys to their silk-screened number equivalents
        char mapped = dialerKeyMap(c);
        if (mapped) {
          if (_phoneInputPos < SMS_PHONE_LEN - 1) {
            _phoneInputBuf[_phoneInputPos++] = mapped;
            _phoneInputBuf[_phoneInputPos] = '\0';
          }
          return true;
        }
        return true;
    }
  }

  // ---- Touch numpad input (called from main loop when touch detected) ----

  // Process a touch event at PHYSICAL display coordinates (px, py).
  // The touch controller reports 240x320; display draws in virtual coords.
  // Returns true if the touch was consumed (a button was pressed).
  // Caller should call forceRefresh() after this returns true.
  //
  // dispW/dispH: virtual display dimensions (display.width()/height())
  // physW/physH: physical touch panel dimensions (240/320)
  bool handleTouch(int16_t px, int16_t py, int dispW = 128, int dispH = 128,
                   int physW = 240, int physH = 320) {
    // App menu: first tap highlights a row, tapping the same row again opens it
    if (_view == APP_MENU) {
#ifdef MECK_UI_V2
      // v2 menu: 40 px rows from the top of the content area, physical pixels
      int my = py;
      const int menuTop   = ui2::BODY_Y;
      const int menuLineH = ui2::ROW_H;
#else
      int my = (int)py * dispH / physH;
      const int menuTop   = 24;   // must match renderAppMenu
      const int menuLineH = 16;
#endif
      for (int i = 0; i < 4; i++) {
        if (my >= menuTop + i * menuLineH && my < menuTop + (i + 1) * menuLineH) {
          if (_menuCursor == i) {
            handleAppMenuInput(0x0D);   // second tap == Enter
          } else {
            _menuCursor = i;
          }
          _needsRefresh = true;
          return true;
        }
      }
      return false;
    }

    if (_view != PHONE_DIALER) return false;

#ifdef MECK_UI_V2
    // v2 keypad: physical-pixel grid (must match renderPhoneDialerV2)
    if (px < ui2::PAD || py < V2_KP_TOP) return false;
    int col = (px - ui2::PAD) / (v2KpColW() + V2_KP_GAP);
    int row = (py - V2_KP_TOP) / (v2KpRowH() + V2_KP_GAP);
    if (col >= NUMPAD_COLS || row >= NUMPAD_ROWS) return false;
    int x = px, y = py;
    (void)dispW; (void)dispH; (void)physW; (void)physH;
#else
    // Map physical touch coordinates to virtual display coordinates
    int x = (int)px * dispW / physW;
    int y = (int)py * dispH / physH;

    // Compute layout (must match renderPhoneDialer)
    int headerH    = 12;
    int phoneFieldH = 14;
    int footerH    = 12;
    int numpadTop  = headerH + phoneFieldH;
    int numpadH    = dispH - numpadTop - footerH;
    int rowH       = numpadH / NUMPAD_ROWS;
    int colW       = dispW / NUMPAD_COLS;

    // Check bounds: must be within numpad grid
    if (y < numpadTop || y >= numpadTop + NUMPAD_ROWS * rowH) return false;
    if (x < 0 || x >= NUMPAD_COLS * colW) return false;

    // Map coordinates to grid cell
    int col = x / colW;
    int row = (y - numpadTop) / rowH;

    if (col < 0 || col >= NUMPAD_COLS || row < 0 || row >= NUMPAD_ROWS) return false;
#endif

    char c = numpadChar(row, col);
    bool changed = false;

    if (c == '\r') {
      // CALL button
      if (_phoneInputPos > 0) {
        _phoneInputBuf[_phoneInputPos] = '\0';
        startCall(_phoneInputBuf);
        changed = true;
      }
    } else if (c == '\b') {
      // DEL button
      if (_phoneInputPos > 0) {
        _phoneInputPos--;
        _phoneInputBuf[_phoneInputPos] = '\0';
        changed = true;
      }
    } else {
      // Digit/symbol button
      if (_phoneInputPos < SMS_PHONE_LEN - 1) {
        _phoneInputBuf[_phoneInputPos++] = c;
        _phoneInputBuf[_phoneInputPos] = '\0';
        changed = true;
      }
    }

    Serial.printf("[Touch] Numpad: phys=(%d,%d) virt=(%d,%d) row=%d col=%d btn=%s %s\n",
                  px, py, x, y, row, col, numpadLabel(row, col),
                  changed ? "OK" : "NOOP");
    return changed;
  }

  // clearTouch() is a no-op with time-based debounce, kept for API compat
  void clearTouch() { }

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
          smsStore.markConversationRead(_activePhone);
          _conversations[_inboxCursor].unreadCount = 0;
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
        _contactsReturnView = INBOX;
        _view = CONTACTS;
        return true;

      case 'q':
      case KEY_CANCEL:  // Back to app menu, on the SMS inbox item
        _view = APP_MENU;
        _menuCursor = 3;
        return true;

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

      case 'f': case 'F':  // Call this number
        if (_activePhone[0] != '\0') {
          startCall(_activePhone);
        }
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

      case 'q':
      case KEY_CANCEL:  // Back to inbox
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

      case 0x18:
      case KEY_CANCEL:  // Shift+Backspace (cancel)
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

  // ---- Phone number input (for compose) ----
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

      case 0x18:
      case KEY_CANCEL:  // Shift+Backspace (cancel)
        _phoneInputBuf[0] = '\0';
        _phoneInputPos = 0;
        refreshInbox();
        _view = INBOX;
        _enteringPhone = false;
        return true;

      default:
        if (char mapped = dialerKeyMap(c)) c = mapped;
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

      case '\r':  // Enter - open the contact page
        if (cnt > 0 && _contactsCursor < cnt) {
          const SMSContact& ct = smsContacts.get(_contactsCursor);
          openContactPage(ct.name, ct.phone, false);
        }
        return true;

      case 'a': case 'A':  // Add a new contact
        openContactPage("", "", true);
        return true;

      case 'm': case 'M':  // Compose an SMS to the selected contact
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

      case 'f': case 'F':  // Call selected contact
        if (cnt > 0 && _contactsCursor < cnt) {
          const SMSContact& ct = smsContacts.get(_contactsCursor);
          startCall(ct.phone);
        }
        return true;

      case 'q':
      case KEY_CANCEL:  // Back to wherever the list was opened from
        if (_contactsReturnView == INBOX) {
          refreshInbox();
          _view = INBOX;
        } else {
          _view = APP_MENU;
          _menuCursor = 2;
        }
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

      case 0x18:
      case KEY_CANCEL:  // Shift+Backspace (cancel without saving)
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

  // ---- Dialing out input (Enter or Shift+Del to cancel/hang up) ----
  bool handleDialingOutInput(char c) {
    switch (c) {
      case '\r':  // Enter - hang up
      case KEY_CANCEL:
        modemManager.hangupCall();
        _view = _callReturnView;
        _callPhone[0] = '\0';
        return true;

      default:
        return true;  // Absorb all other keys
    }
  }

  // ---- Incoming call input (Enter to answer, Shift+Del to reject) ----
  bool handleIncomingCallInput(char c) {
    switch (c) {
      case '\r':  // Enter - answer call
        modemManager.answerCall();
        return true;

      case KEY_CANCEL:  // Reject call
        modemManager.hangupCall();
        _view = _callReturnView;
        _callPhone[0] = '\0';
        return true;

      default:
        return true;  // Absorb all other keys
    }
  }

  // ---- In call input (hangup, volume, DTMF) ----
  bool handleInCallInput(char c) {
    switch (c) {
      case '\r':  // Enter - hang up
      case KEY_CANCEL:
        modemManager.hangupCall();
        _view = _callReturnView;
        _callPhone[0] = '\0';
        _callConnectTime = 0;
        return true;

      case 'w': case 'W':  // Volume up
        if (_callVolume < 5) {
          _callVolume++;
          modemManager.setCallVolume(_callVolume);
        }
        return true;

      case 's': case 'S':  // Volume down
        if (_callVolume > 0) {
          _callVolume--;
          modemManager.setCallVolume(_callVolume);
        }
        return true;

      default:
        // DTMF tones: 0-9, *, #
        if ((c >= '0' && c <= '9') || c == '*' || c == '#') {
          modemManager.sendDTMF(c);
        }
        return true;  // Absorb all keys during call
    }
  }
};

#endif // SMS_SCREEN_H
#endif // HAS_4G_MODEM