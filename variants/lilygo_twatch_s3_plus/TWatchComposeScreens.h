#pragma once
// =============================================================================
// TWatchComposeScreens — touch keyboard compose system for the LilyGo
// T-Watch S3 Plus (ST7789 240x240, capacitive touch, companion_radio ui-new).
//
// Three UIScreen subclasses, compiled only when TWATCH_COMPOSE_ENABLED is
// defined (companion build only). All drawing and hit-testing is in the
// LGFXDisplay virtual coordinate space, which is 120x120 on this build
// (240px panel / UI_ZOOM=2). getTouch() returns coords already divided by
// UI_ZOOM, so a tap coordinate is directly comparable to draw coordinates.
//
//   TWatchChannelPicker  — long-press on the home screen opens this. Tap the
//                          top half to move the highlight up, the bottom half
//                          to move it down; long-press selects the highlighted
//                          channel; the back arrow (top-left) returns home.
//   TWatchChannelScreen  — shows the last TW_INBOX_SIZE incoming messages for
//                          the selected channel (live, in-RAM, no history).
//                          Tap the compose bar to open the keyboard; back
//                          arrow returns home.
//   TWatchKeyboardScreen — on-screen QWERTY. Bottom-left mode key cycles
//                          lower -> UPPER -> SYM. Bottom row is mode | space |
//                          enter | backspace. Enter sends on the channel; back
//                          arrow returns home.
//
// Each screen reads getTouch() itself in poll() and does its own release-edge
// detection, so actions fire on touch release (finger up) — this keeps the
// held finger from carrying into the next screen after a transition. Send/exit
// are delegated to UITask via the consume/flag pattern, so this header has no
// dependency on MyMesh / BaseChatMesh.
// =============================================================================

#ifdef TWATCH_COMPOSE_ENABLED

#include <Arduino.h>
#include <string.h>
#include <helpers/ui/LGFXDisplay.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>

// ---- Tunables ---------------------------------------------------------------
#define TW_LONG_PRESS_MS   600    // hold to select a channel in the picker
#define TW_OUT_BUF_LEN     134    // MeshCore per-channel msg cap (~133) + NUL
#define TW_INBOX_SIZE      5      // messages kept/shown on the channel screen
#define TW_INBOX_TEXT_LEN  96
#define TW_TICKER_MS_PER_PX 20    // channel-screen ticker scroll speed (ms per pixel)
#define TW_CH_NAME_LEN     32
#define TW_PICKER_MAX      20     // matches MAX_GROUP_CHANNELS

// =============================================================================
// TWatchChannelPicker
// =============================================================================
class TWatchChannelPicker : public UIScreen {
  DisplayDriver* _display;

  struct Entry { uint8_t idx; char name[TW_CH_NAME_LEN]; };
  Entry    _channels[TW_PICKER_MAX];
  uint8_t  _numChannels;
  uint8_t  _highlighted;
  uint8_t  _scrollTop;

  // touch edge state
  bool          _touchDown;
  int           _downX, _downY;
  unsigned long _downAt;

  // cross-screen flags (UITask polls these)
  bool _confirmed;
  bool _wantsExit;

  static const int HEADER_H = 14;
  static const int ROW_H    = 14;

  int visibleRows() const {
    int h = _display ? _display->height() : 120;
    int rows = (h - HEADER_H) / ROW_H;
    return rows < 1 ? 1 : rows;
  }
  void ensureVisible() {
    int vis = visibleRows();
    if (_highlighted < _scrollTop) _scrollTop = _highlighted;
    else if (_highlighted >= _scrollTop + vis) _scrollTop = (uint8_t)(_highlighted - vis + 1);
  }
  void moveUp() {
    if (_numChannels == 0) return;
    _highlighted = (_highlighted == 0) ? (uint8_t)(_numChannels - 1) : (uint8_t)(_highlighted - 1);
    ensureVisible();
  }
  void moveDown() {
    if (_numChannels == 0) return;
    _highlighted = (uint8_t)((_highlighted + 1) % _numChannels);
    ensureVisible();
  }

public:
  TWatchChannelPicker(DisplayDriver* display)
    : _display(display), _numChannels(0), _highlighted(0), _scrollTop(0),
      _touchDown(false), _downX(0), _downY(0), _downAt(0),
      _confirmed(false), _wantsExit(false) {
    memset(_channels, 0, sizeof(_channels));
  }

  // Called by UITask before showing the screen.
  void beginChannelSelect() {
    _numChannels = 0; _highlighted = 0; _scrollTop = 0;
    _touchDown = false; _confirmed = false; _wantsExit = false;
  }
  void addChannel(uint8_t idx, const char* name) {
    if (_numChannels >= TW_PICKER_MAX) return;
    _channels[_numChannels].idx = idx;
    strncpy(_channels[_numChannels].name, name ? name : "", TW_CH_NAME_LEN - 1);
    _channels[_numChannels].name[TW_CH_NAME_LEN - 1] = 0;
    _numChannels++;
  }

  // UITask bridges
  bool isConfirmed() const { return _confirmed; }
  void acknowledgeConfirm() { _confirmed = false; }
  uint8_t getSelectedChannelIdx() const { return _channels[_highlighted].idx; }
  const char* getSelectedChannelName() const { return _channels[_highlighted].name; }
  bool wantsExit() const { return _wantsExit; }
  void acknowledgeExit() { _wantsExit = false; }

  bool handleInput(char c) override { return false; }   // touch handled in poll()

  void poll() override {
    if (!_display) return;
    int x, y;
    bool now = ((LGFXDisplay*)_display)->getTouch(&x, &y);
    if (now && !_touchDown) {
      _touchDown = true; _downX = x; _downY = y; _downAt = millis();
    } else if (!now && _touchDown) {
      _touchDown = false;
      unsigned long held = millis() - _downAt;
      if (_downX < 20 && _downY < HEADER_H) { _wantsExit = true; return; }   // back arrow
      if (held >= TW_LONG_PRESS_MS) {
        if (_numChannels > 0) _confirmed = true;                             // select highlighted
      } else {
        if (_downY < _display->height() / 2) moveUp(); else moveDown();      // tap zones
      }
    }
  }

  int render(DisplayDriver& display) override {
    const int W = display.width();
    display.setTextSize(1);

    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, 0);
    display.print("<");
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(W / 2, 0, "CHANNELS");
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, HEADER_H - 2, W, 1);

    if (_numChannels == 0) {
      display.setCursor(2, HEADER_H + 2);
      display.print("(no channels)");
      return 500;
    }

    int vis = visibleRows();
    int y = HEADER_H;
    for (int i = 0; i < vis; i++) {
      int ci = _scrollTop + i;
      if (ci >= _numChannels) break;
      if (ci == _highlighted) {
        display.setColor(DisplayDriver::GREEN);
        display.fillRect(0, y, W, ROW_H);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.drawTextEllipsized(3, y + 2, W - 6, _channels[ci].name);
      y += ROW_H;
    }
    return 500;
  }
};

// =============================================================================
// TWatchChannelScreen
// =============================================================================
class TWatchChannelScreen : public UIScreen {
  DisplayDriver* _display;
  uint8_t _channelIdx;
  char    _channelName[TW_CH_NAME_LEN];

  struct InboxEntry { char text[TW_INBOX_TEXT_LEN]; bool valid; bool isSent; };
  InboxEntry _inbox[TW_INBOX_SIZE];
  uint8_t    _inboxNewest;
  uint8_t    _inboxCount;

  bool _touchDown;
  int  _downX, _downY;

  bool _wantsCompose;
  bool _wantsExit;

  int           _selectedSlot;    // ring slot shown as ticker, -1 = none
  unsigned long _tickerStartMs;

  static const int HEADER_H      = 14;
  static const int COMPOSE_BAR_H = 18;
  static const int MSG_LINE_H    = 11;
  static const int MSG_TOP       = HEADER_H + 2;

  void pushEntry(const char* text, bool sent) {
    _inboxNewest = (_inboxCount == 0) ? 0 : (uint8_t)((_inboxNewest + 1) % TW_INBOX_SIZE);
    InboxEntry& e = _inbox[_inboxNewest];
    if (text) { strncpy(e.text, text, TW_INBOX_TEXT_LEN - 1); e.text[TW_INBOX_TEXT_LEN - 1] = 0; }
    else      { e.text[0] = 0; }
    e.valid = true;
    e.isSent = sent;
    if (_inboxCount < TW_INBOX_SIZE) _inboxCount++;
    _selectedSlot = -1;   // a new message dismisses any open ticker
  }

  void drawMsgLine(DisplayDriver& display, int y, const char* text, bool sent) {
    const int W = display.width();
    const int maxW = W - 4;
    display.setColor(DisplayDriver::LIGHT);
    if (!sent) {
      display.drawTextEllipsized(2, y, maxW, text);   // incoming: left-aligned
      return;
    }
    if (display.getTextWidth(text) <= maxW) {          // sent: right-aligned
      display.drawTextRightAlign(W - 2, y, text);
      return;
    }
    char buf[TW_INBOX_TEXT_LEN + 4];
    strncpy(buf, text, sizeof(buf) - 4);
    buf[sizeof(buf) - 4] = 0;
    int ellW = display.getTextWidth("...");
    int len = (int)strlen(buf);
    while (len > 0 && display.getTextWidth(buf) > maxW - ellW) { buf[--len] = 0; }
    strcat(buf, "...");
    display.drawTextRightAlign(W - 2, y, buf);
  }

  void drawTicker(DisplayDriver& display, int y, const char* text, bool sent) {
    const int W = display.width();
    int textW = display.getTextWidth(text);
    display.setColor(DisplayDriver::LIGHT);
    if (textW <= W - 4) {   // fits -> nothing to scroll, keep alignment
      if (sent) display.drawTextRightAlign(W - 2, y, text);
      else      { display.setCursor(2, y); display.print(text); }
      return;
    }
    int period = textW + 24;   // full text width + trailing gap
    unsigned long elapsed = millis() - _tickerStartMs;
    int off = (int)((elapsed / TW_TICKER_MS_PER_PX) % (unsigned long)period);
    display.setCursor(2 - off, y);
    display.print(text);
    display.setCursor(2 - off + period, y);
    display.print(text);
  }

public:
  TWatchChannelScreen(DisplayDriver* display)
    : _display(display), _channelIdx(0),
      _inboxNewest(0), _inboxCount(0),
      _touchDown(false), _downX(0), _downY(0),
      _wantsCompose(false), _wantsExit(false),
      _selectedSlot(-1), _tickerStartMs(0) {
    _channelName[0] = 0;
    memset(_inbox, 0, sizeof(_inbox));
  }

  // Switch to a channel (called when a channel is selected in the picker).
  void activate(uint8_t idx, const char* name) {
    _channelIdx = idx;
    strncpy(_channelName, name ? name : "", TW_CH_NAME_LEN - 1);
    _channelName[TW_CH_NAME_LEN - 1] = 0;
    _inboxNewest = 0; _inboxCount = 0;
    memset(_inbox, 0, sizeof(_inbox));
    _touchDown = false; _wantsCompose = false; _wantsExit = false;
    _selectedSlot = -1;
  }

  uint8_t getChannelIdx() const { return _channelIdx; }
  const char* getChannelName() const { return _channelName; }

  // Fed by UITask::newMsg for every incoming message; kept only if it matches
  // this channel by name.
  void notifyMsg(const char* from, const char* text) {
    if (!from || strcmp(from, _channelName) != 0) return;
    pushEntry(text, false);
  }

  // Called by UITask after a message is sent on this channel.
  void addSentMsg(const char* text) {
    pushEntry(text, true);
  }

  bool wantsCompose() const { return _wantsCompose; }
  void acknowledgeCompose() { _wantsCompose = false; }
  bool wantsExit() const { return _wantsExit; }
  void acknowledgeExit() { _wantsExit = false; }

  bool handleInput(char c) override { return false; }

  void poll() override {
    if (!_display) return;
    int x, y;
    bool now = ((LGFXDisplay*)_display)->getTouch(&x, &y);
    if (now && !_touchDown) {
      _touchDown = true; _downX = x; _downY = y;
    } else if (!now && _touchDown) {
      _touchDown = false;
      int H = _display->height();
      if (_downX < 20 && _downY < HEADER_H) { _selectedSlot = -1; _wantsExit = true; return; }   // back arrow
      if (_downY >= H - COMPOSE_BAR_H) { _selectedSlot = -1; _wantsCompose = true; return; }      // compose bar
      // message area -> tap to open/close ticker
      if (_downY >= MSG_TOP && _downY < H - COMPOSE_BAR_H) {
        int visualRow = (_downY - MSG_TOP) / MSG_LINE_H;
        if (visualRow >= 0 && visualRow < _inboxCount) {
          int i = (_inboxCount - 1) - visualRow;
          int slot = (int)_inboxNewest - i;
          while (slot < 0) slot += TW_INBOX_SIZE;
          slot %= TW_INBOX_SIZE;
          if (_inbox[slot].valid) {
            if (_selectedSlot == slot) _selectedSlot = -1;
            else { _selectedSlot = slot; _tickerStartMs = millis(); }
          }
        }
      }
    }
  }

  int render(DisplayDriver& display) override {
    const int W = display.width();
    const int H = display.height();
    display.setTextSize(1);

    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, 0);
    display.print("<");
    display.setColor(DisplayDriver::GREEN);
    display.drawTextCentered(W / 2, 0, _channelName);
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, HEADER_H - 2, W, 1);

    const int areaBottom = H - COMPOSE_BAR_H - 1;

    if (_inboxCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(2, MSG_TOP);
      display.print("(no messages)");
    } else {
      int y = MSG_TOP;
      for (int i = _inboxCount - 1; i >= 0; i--) {   // oldest first (top) -> newest (bottom)
        int idx = (int)_inboxNewest - i;
        while (idx < 0) idx += TW_INBOX_SIZE;
        const InboxEntry& e = _inbox[idx];
        if (!e.valid) continue;
        if (y + MSG_LINE_H > areaBottom) break;
        if (idx == _selectedSlot) drawTicker(display, y, e.text, e.isSent);
        else                      drawMsgLine(display, y, e.text, e.isSent);
        y += MSG_LINE_H;
      }
    }

    // compose bar
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, H - COMPOSE_BAR_H, W, COMPOSE_BAR_H - 1);
    display.drawTextEllipsized(3, H - COMPOSE_BAR_H + 4, W - 6, "Tap to compose");
    return (_selectedSlot >= 0) ? 60 : 500;
  }
};

// =============================================================================
// TWatchKeyboardScreen
// =============================================================================
class TWatchKeyboardScreen : public UIScreen {
  DisplayDriver* _display;
  uint8_t  _channelIdx;

  char     _outBuf[TW_OUT_BUF_LEN];
  uint16_t _outLen;

  enum Mode { LOWER, UPPER, SYM };
  Mode _mode;

  bool _touchDown;
  int  _downX, _downY;

  bool _wantsSend;
  bool _wantsExit;

  static const int MAXLEN   = TW_OUT_BUF_LEN - 1;    // 133

  // layout geometry (120x120 virtual space)
  static const int TOPBAR_H = 15;
  static const int KEY_W    = 12;                    // 10 * 12 = 120 wide
  static const int KEY_H    = 26;
  static const int GRID_Y   = 15;                    // row0 top; rows at GRID_Y + row*KEY_H

  const char* const* layout() const {
    static const char* const lower[3] = { "qwertyuiop", "asdfghjkl'", "zxcvbnm,.?" };
    static const char* const upper[3] = { "QWERTYUIOP", "ASDFGHJKL\"", "ZXCVBNM<>/" };
    static const char* const syms[3]  = { "123!@#$%^&", "456()[]{}-", "7890+=_:;|" };
    return _mode == SYM ? syms : (_mode == UPPER ? upper : lower);
  }
  const char* modeLabel() const {
    return _mode == SYM ? "SYM" : (_mode == UPPER ? "A-Z" : "a-z");
  }

  void appendChar(char ch) {
    if (_outLen < MAXLEN) { _outBuf[_outLen++] = ch; _outBuf[_outLen] = 0; }
  }
  void backspace() {
    if (_outLen > 0) { _outLen--; _outBuf[_outLen] = 0; }
  }
  void cycleMode() {
    _mode = (_mode == LOWER) ? UPPER : (_mode == UPPER ? SYM : LOWER);
  }
  // bottom row zones: mode 0..24 | space 24..72 | enter 72..96 | backspace 96..120
  void handleBottomRow(int x) {
    if (x < 24)      cycleMode();
    else if (x < 72) appendChar(' ');
    else if (x < 96) { if (_outLen > 0) _wantsSend = true; }
    else             backspace();
  }

public:
  TWatchKeyboardScreen(DisplayDriver* display)
    : _display(display), _channelIdx(0), _outLen(0), _mode(LOWER),
      _touchDown(false), _downX(0), _downY(0),
      _wantsSend(false), _wantsExit(false) {
    _outBuf[0] = 0;
  }

  // Enter compose on the given channel (called when the compose bar is tapped).
  void activate(uint8_t idx, const char* name) {
    (void)name;
    _channelIdx = idx;
    _outLen = 0; _outBuf[0] = 0;
    _mode = LOWER;
    _touchDown = false; _wantsSend = false; _wantsExit = false;
  }

  uint8_t getChannelIdx() const { return _channelIdx; }
  bool consumeSendRequest(const char** textOut) {
    if (!_wantsSend) return false;
    _wantsSend = false;
    if (textOut) *textOut = _outBuf;
    return true;
  }
  void clearOutBuf() { _outLen = 0; _outBuf[0] = 0; }
  bool wantsExit() const { return _wantsExit; }
  void acknowledgeExit() { _wantsExit = false; }

  bool handleInput(char c) override { return false; }

  void poll() override {
    if (!_display) return;
    int x, y;
    bool now = ((LGFXDisplay*)_display)->getTouch(&x, &y);
    if (now && !_touchDown) {
      _touchDown = true; _downX = x; _downY = y;
    } else if (!now && _touchDown) {
      _touchDown = false;
      int x0 = _downX, y0 = _downY;
      if (x0 < 20 && y0 < TOPBAR_H) { _wantsExit = true; return; }   // back arrow
      if (y0 < GRID_Y) return;                                       // top bar (text) area
      int row = (y0 - GRID_Y) / KEY_H;
      if (row <= 2) {
        int col = x0 / KEY_W;
        char ch = layout()[row][col];
        if (ch) appendChar(ch);
      } else {
        handleBottomRow(x0);                                         // bottom row
      }
    }
  }

  int render(DisplayDriver& display) override {
    const int W = display.width();
    display.setTextSize(1);

    // ---- top bar: back arrow + composed text tail + cursor ----
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, 0);
    display.print("<");

    display.setColor(DisplayDriver::LIGHT);
    {
      const int avail = W - 16 - 4;   // room after the back arrow, minus cursor
      int len = _outLen;
      int fit = 0;
      for (int n = 1; n <= len; n++) {
        if (display.getTextWidth(_outBuf + (len - n)) > avail) break;
        fit = n;
      }
      char tail[TW_OUT_BUF_LEN + 2];
      strncpy(tail, _outBuf + (len - fit), sizeof(tail) - 2);
      tail[sizeof(tail) - 2] = 0;
      size_t tl = strlen(tail);
      tail[tl] = '_'; tail[tl + 1] = 0;
      display.setCursor(16, 2);
      display.print(tail);
    }

    // ---- key grid rows 0..2 ----
    const char* const* lay = layout();
    for (int row = 0; row < 3; row++) {
      int ry = GRID_Y + row * KEY_H;
      const char* r = lay[row];
      for (int col = 0; col < 10; col++) {
        int kx = col * KEY_W;
        display.setColor(DisplayDriver::GREEN);
        display.drawRect(kx, ry, KEY_W, KEY_H);
        char ch[2] = { r[col], 0 };
        display.setColor(DisplayDriver::LIGHT);
        int tw = display.getTextWidth(ch);
        display.setCursor(kx + (KEY_W - tw) / 2, ry + (KEY_H - 8) / 2);
        display.print(ch);
      }
    }

    // ---- bottom row: mode | space | enter | backspace ----
    int by = GRID_Y + 3 * KEY_H;

    display.setColor(DisplayDriver::GREEN);
    display.fillRect(0, by, 24, KEY_H);
    display.setColor(DisplayDriver::DARK);
    {
      const char* ml = modeLabel();
      int tw = display.getTextWidth(ml);
      display.setCursor((24 - tw) / 2, by + (KEY_H - 8) / 2);
      display.print(ml);
    }

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(24, by, 48, KEY_H);   // space

    display.setColor(DisplayDriver::GREEN);
    display.fillRect(72, by, 24, KEY_H);
    display.setColor(DisplayDriver::DARK);
    {
      const char* el = "OK";
      int tw = display.getTextWidth(el);
      display.setCursor(72 + (24 - tw) / 2, by + (KEY_H - 8) / 2);
      display.print(el);
    }

    display.setColor(DisplayDriver::ORANGE);
    display.fillRect(96, by, 24, KEY_H);
    display.setColor(DisplayDriver::DARK);
    {
      const char* bl = "DEL";
      int tw = display.getTextWidth(bl);
      display.setCursor(96 + (24 - tw) / 2, by + (KEY_H - 8) / 2);
      display.print(bl);
    }

    return 500;
  }
};

#endif // TWATCH_COMPOSE_ENABLED