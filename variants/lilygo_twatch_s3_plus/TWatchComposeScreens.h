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
//   TWatchChannelScreen  -- shows the most recent messages (sent + received)
//                          for the selected channel, read live from the shared
//                          ChannelScreen history store. Tap a line to ticker-
//                          scroll it; tap the compose bar to open the keyboard;
//                          back arrow returns home.
//   TWatchKeyboardScreen — on-screen QWERTY. Bottom-left mode key cycles
//                          lower -> UPPER -> SYM. Bottom row is mode | space |
//                          enter | backspace. Enter sends on the channel; back
//                          arrow returns home.
//
// Each screen reads getTouch() itself in poll() and does its own release-edge
// detection, so actions fire on touch release (finger up) — this keeps the
// held finger from carrying into the next screen after a transition. Send/exit
// are delegated to UITask via the consume/flag pattern; message history and
// unread counts are read from the shared ChannelScreen store.
// =============================================================================

#ifdef TWATCH_COMPOSE_ENABLED

#include <Arduino.h>
#include <string.h>
#include <helpers/ui/LGFXDisplay.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "ChannelScreen.h"   // shared message history store (-I examples/companion_radio/ui-new)

// ---- Tunables ---------------------------------------------------------------
#define TW_LONG_PRESS_MS   600    // hold to select a channel in the picker
#define TW_OUT_BUF_LEN     134    // MeshCore per-channel msg cap (~133) + NUL
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
  ChannelScreen* _store;   // unread badge source

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
      _store(nullptr),
      _touchDown(false), _downX(0), _downY(0), _downAt(0),
      _confirmed(false), _wantsExit(false) {
    memset(_channels, 0, sizeof(_channels));
  }

  // Shared history store, used for the per-channel unread badges.
  void setStore(ChannelScreen* store) { _store = store; }

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
      int nameMaxW = W - 6;
      int unread = _store ? _store->getUnreadForChannel(_channels[ci].idx) : 0;
      if (unread > 0) {
        char cnt[8];
        snprintf(cnt, sizeof(cnt), "%d", unread > 99 ? 99 : unread);
        int cw = display.getTextWidth(cnt);
        display.setColor((ci == _highlighted) ? DisplayDriver::DARK : DisplayDriver::BLUE);
        display.drawTextRightAlign(W - 3, y + 2, cnt);
        display.setColor((ci == _highlighted) ? DisplayDriver::DARK : DisplayDriver::LIGHT);
        nameMaxW = W - 6 - cw - 4;
      }
      display.drawTextEllipsized(3, y + 2, nameMaxW, _channels[ci].name);
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
  ChannelScreen* _store;          // shared message history store
  uint8_t _channelIdx;
  char    _channelName[TW_CH_NAME_LEN];
  char    _selfPrefix[TW_CH_NAME_LEN + 2];   // "NodeName: " -- marks sent lines

  bool _touchDown;
  int  _downX, _downY;

  bool _wantsCompose;
  bool _wantsExit;

  int           _selectedN;       // recency index shown as ticker, -1 = none
  unsigned long _tickerStartMs;
  const void*   _lastNewest;      // newest store entry seen last render
  uint32_t      _lastNewestTs;    //   (a change dismisses the ticker)

  static const int HEADER_H      = 14;
  static const int COMPOSE_BAR_H = 18;
  static const int MSG_LINE_H    = 11;
  static const int MSG_TOP       = HEADER_H + 2;

  int rowsThatFit() const {
    int h = _display ? _display->height() : 120;
    int rows = (h - COMPOSE_BAR_H - 1 - MSG_TOP) / MSG_LINE_H;
    return rows < 1 ? 1 : rows;
  }

  // Store messages available for this channel, capped at what fits on screen.
  int visibleCount() const {
    if (!_store) return 0;
    int maxRows = rowsThatFit();
    int n = 0;
    while (n < maxRows && _store->getChannelMsgByRecency(_channelIdx, n)) n++;
    return n;
  }

  // Sent messages are stored with path_len 0 and a "NodeName: " prefix.
  bool isSent(const ChannelScreen::ChannelMessage* m) const {
    return m->path_len == 0 && _selfPrefix[0] != 0 &&
           strncmp(m->text, _selfPrefix, strlen(_selfPrefix)) == 0;
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
    char buf[CHANNEL_MSG_TEXT_LEN + 4];
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
    // Wrap off while the marquee draws: a long line must clip at the screen
    // edge, not wrap onto the rows below. Restored straight after.
    ((LGFXDisplay*)_display)->setTextWrap(false);
    display.setCursor(2 - off, y);
    display.print(text);
    display.setCursor(2 - off + period, y);
    display.print(text);
    ((LGFXDisplay*)_display)->setTextWrap(true);
  }

public:
  TWatchChannelScreen(DisplayDriver* display)
    : _display(display), _store(nullptr), _channelIdx(0),
      _touchDown(false), _downX(0), _downY(0),
      _wantsCompose(false), _wantsExit(false),
      _selectedN(-1), _tickerStartMs(0),
      _lastNewest(nullptr), _lastNewestTs(0) {
    _channelName[0] = 0;
    _selfPrefix[0] = 0;
  }

  // Shared history store this screen renders from.
  void setStore(ChannelScreen* store) { _store = store; }

  // Own node name, used to right-align sent lines ("NodeName: text").
  void setSelfName(const char* name) {
    if (!name || !name[0]) { _selfPrefix[0] = 0; return; }
    snprintf(_selfPrefix, sizeof(_selfPrefix), "%s: ", name);
  }

  // Switch to a channel (called when a channel is selected in the picker).
  // Opening a channel marks its messages as read.
  void activate(uint8_t idx, const char* name) {
    _channelIdx = idx;
    strncpy(_channelName, name ? name : "", TW_CH_NAME_LEN - 1);
    _channelName[TW_CH_NAME_LEN - 1] = 0;
    _touchDown = false; _wantsCompose = false; _wantsExit = false;
    _selectedN = -1;
    _lastNewest = nullptr; _lastNewestTs = 0;
    if (_store) _store->markChannelRead(_channelIdx);
  }

  uint8_t getChannelIdx() const { return _channelIdx; }
  const char* getChannelName() const { return _channelName; }

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
      if (_downX < 20 && _downY < HEADER_H) { _selectedN = -1; _wantsExit = true; return; }   // back arrow
      if (_downY >= H - COMPOSE_BAR_H) { _selectedN = -1; _wantsCompose = true; return; }      // compose bar
      // message area -> tap to open/close ticker
      if (_downY >= MSG_TOP && _downY < H - COMPOSE_BAR_H) {
        int visualRow = (_downY - MSG_TOP) / MSG_LINE_H;
        int count = visibleCount();
        if (visualRow >= 0 && visualRow < count) {
          int n = (count - 1) - visualRow;   // top row = oldest visible
          if (_selectedN == n) _selectedN = -1;
          else { _selectedN = n; _tickerStartMs = millis(); }
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

    // A new message (sent or received) shifts the rows, so dismiss any open
    // ticker when the newest store entry for this channel changes.
    const ChannelScreen::ChannelMessage* newest =
        _store ? _store->getChannelMsgByRecency(_channelIdx, 0) : nullptr;
    if (newest != _lastNewest || (newest && newest->timestamp != _lastNewestTs)) {
      _lastNewest = newest;
      _lastNewestTs = newest ? newest->timestamp : 0;
      _selectedN = -1;
    }

    int count = visibleCount();
    if (count == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(2, MSG_TOP);
      display.print("(no messages)");
    } else {
      int y = MSG_TOP;
      for (int n = count - 1; n >= 0; n--) {   // oldest visible (top) -> newest (bottom)
        const ChannelScreen::ChannelMessage* m = _store->getChannelMsgByRecency(_channelIdx, n);
        if (!m) continue;
        bool sent = isSent(m);
        if (n == _selectedN) drawTicker(display, y, m->text, sent);
        else                 drawMsgLine(display, y, m->text, sent);
        y += MSG_LINE_H;
      }
    }

    // compose bar
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, H - COMPOSE_BAR_H, W, COMPOSE_BAR_H - 1);
    display.drawTextEllipsized(3, H - COMPOSE_BAR_H + 4, W - 6, "Tap to compose");
    return (_selectedN >= 0) ? 60 : 500;
  }
};

// =============================================================================
// TWatchKeyboardScreen
// =============================================================================
class TWatchKeyboardScreen : public UIScreen {
  DisplayDriver* _display;
  uint8_t  _channelIdx;

public:
  enum Purpose { TWKB_CHANNEL, TWKB_DM, TWKB_ADMIN_PASSWORD, TWKB_ADMIN_CLI, TWKB_PATH };

private:
  Purpose  _purpose;
  int      _contextIdx;    // channel idx (channel) or contact idx (DM/admin)
  bool     _mask;          // render composed text as '*' (admin password)
  unsigned long _lastCharAt;   // when the last char was typed (for reveal window)

  static const unsigned long PW_REVEAL_MS = 2000;   // show last char this long before masking

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
    if (_outLen < MAXLEN) { _outBuf[_outLen++] = ch; _outBuf[_outLen] = 0; _lastCharAt = millis(); }
  }
  void backspace() {
    if (_outLen > 0) { _outLen--; _outBuf[_outLen] = 0; _lastCharAt = 0; }
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
    : _display(display), _channelIdx(0),
      _purpose(TWKB_CHANNEL), _contextIdx(0), _mask(false), _lastCharAt(0),
      _outLen(0), _mode(LOWER),
      _touchDown(false), _downX(0), _downY(0),
      _wantsSend(false), _wantsExit(false) {
    _outBuf[0] = 0;
  }

  // Enter compose on the given channel (called when the compose bar is tapped).
  void activate(uint8_t idx, const char* name) {
    (void)name;
    _channelIdx = idx;
    _purpose = TWKB_CHANNEL;
    _contextIdx = idx;
    _mask = false;
    _outLen = 0; _outBuf[0] = 0;
    _mode = LOWER;
    _touchDown = false; _wantsSend = false; _wantsExit = false;
  }

  // Enter text for a non-channel purpose (DM / admin password / admin CLI).
  void activateFor(Purpose purpose, int contextIdx) {
    _purpose = purpose;
    _contextIdx = contextIdx;
    _channelIdx = 0;
    _mask = (purpose == TWKB_ADMIN_PASSWORD);
    _outLen = 0; _outBuf[0] = 0;
    _mode = LOWER;
    _touchDown = false; _wantsSend = false; _wantsExit = false;
  }

  Purpose getPurpose() const { return _purpose; }
  int getContextIdx() const { return _contextIdx; }

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
      // Mask the composed text with '*' for the admin password purpose.
      char masked[TW_OUT_BUF_LEN];
      const char* src = _outBuf;
      if (_mask) {
        int m = (_outLen < TW_OUT_BUF_LEN - 1) ? _outLen : TW_OUT_BUF_LEN - 1;
        for (int i = 0; i < m; i++) masked[i] = '*';
        // Reveal the most recently typed char briefly so it can be read.
        if (m > 0 && (millis() - _lastCharAt < PW_REVEAL_MS)) masked[m - 1] = _outBuf[m - 1];
        masked[m] = 0;
        src = masked;
      }
      const int avail = W - 16 - 4;   // room after the back arrow, minus cursor
      int len = _outLen;
      int fit = 0;
      for (int n = 1; n <= len; n++) {
        if (display.getTextWidth(src + (len - n)) > avail) break;
        fit = n;
      }
      char tail[TW_OUT_BUF_LEN + 2];
      strncpy(tail, src + (len - fit), sizeof(tail) - 2);
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