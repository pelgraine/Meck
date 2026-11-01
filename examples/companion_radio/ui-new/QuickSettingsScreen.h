#pragma once

// =============================================================================
// QuickSettingsScreen -- UI v2 switchboard for radios, sounds and lights
// (docs/ui2-quick-settings.md)
//
// One 2 x 4 grid of tiles in two views: "Now" switches things on and off,
// "When locked" picks what keeps running while the lock screen is up. It
// opens over any screen (Alt+Space, a swipe down from the status header,
// the heart pad held) and Q goes back to that screen.
//
// The tiles' state and actions live in main.cpp (meckQs*), next to the
// radio code they call; this file only draws and takes input.
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include "ui2/Ui2Chrome.h"
#include "ui2/Ui2List.h"

// One per tile: the eight of the Now view, then the eight of When locked
enum QsItem : uint8_t {
  QS_BLE, QS_WIFI, QS_GPS, QS_MODEM, QS_TONES, QS_VIBRATE, QS_LIGHT, QS_KEYS,
  QS_L_BLE, QS_L_WIFI, QS_L_GPS, QS_L_MODEM, QS_L_TONES, QS_L_VIBRATE, QS_L_SAVING, QS_L_USB,
  QS_ITEM_COUNT
};

// A tile's state, filled in by meckQsState()
struct QsTileState {
  bool available = true;   // the feature is in this build
  bool on = false;         // the tile's switch
  bool pending = false;    // switching (WiFi joining, modem starting)
  char line[28] = "";      // state line under the label
};

void meckQsState(QsItem item, QsTileState& st);
void meckQsSwitch(QsItem item);
// What locking does with the current settings ("Locked: sleeps · ~17 mA")
void meckQsLockVerdict(char* buf, size_t len);

class QuickSettingsScreen : public UIScreen {
public:
  static const int TILES = 8;
  // Tiles are the home tiles' width, 4 px lower, so the title bar and the
  // lock verdict line fit
  static const int16_t TILE_H = 52;
  static const int16_t TILE_Y0 = 52;
  static const int16_t TILE_GAP_Y = 4;
  static const int16_t VERDICT_Y = TILE_Y0 + 4 * TILE_H + 3 * TILE_GAP_Y + 2;   // 274

private:
  uint8_t _sel = 0;
  bool _lockedView = false;
  bool _wantsExit = false;
  int8_t _more = -1;          // item whose Settings page was asked for (M / long press)
  int16_t _chipNowX = 0, _chipLockX = 0, _chipEndX = 0;   // title bar chips, for touch

  QsItem itemAt(int slot) const { return (QsItem)(slot + (_lockedView ? QS_L_BLE : QS_BLE)); }

  static Ui2Icon iconOf(QsItem it) {
    switch (it) {
      case QS_BLE:     case QS_L_BLE:     return UI2_IC_BLE;
      case QS_WIFI:    case QS_L_WIFI:    return UI2_IC_WIFI;
      case QS_GPS:     case QS_L_GPS:     return UI2_IC_GPS;
      case QS_MODEM:   case QS_L_MODEM:   return UI2_IC_SIM;
      case QS_TONES:   case QS_L_TONES:   return UI2_IC_BELL;
      case QS_VIBRATE: case QS_L_VIBRATE: return UI2_IC_VIBRATE;
      case QS_LIGHT:                      return UI2_IC_SUN;
      case QS_KEYS:                       return UI2_IC_KEYBOARD;
      case QS_L_SAVING:                   return UI2_IC_MOON;
      case QS_L_USB:                      return UI2_IC_BOLT;
      default:                            return UI2_NO_ICON;
    }
  }

  static const char* labelOf(QsItem it) {
    switch (it) {
      case QS_BLE:     case QS_L_BLE:     return "Bluetooth";
      case QS_WIFI:    case QS_L_WIFI:    return "WiFi";
      case QS_GPS:     case QS_L_GPS:     return "GPS";
      case QS_MODEM:   case QS_L_MODEM:   return "4G";
      case QS_TONES:   case QS_L_TONES:   return MECK_TR("Tones", "Sons");
      case QS_VIBRATE: case QS_L_VIBRATE: return MECK_TR("Vibration", "Vibreur");
      case QS_LIGHT:                      return MECK_TR("Light", "\xC3\x89" "clairage");
      case QS_KEYS:                       return MECK_TR("Keys light", "LED clavier");
      case QS_L_SAVING:                   return MECK_TR("Power saving", "\xC3\x89" "conomie");
      case QS_L_USB:                      return MECK_TR("On USB", "Sur USB");
      default:                            return "";
    }
  }

  static int16_t tileX(int slot) { return ui2::TILE_X0 + (slot % 2) * (ui2::TILE_W + ui2::TILE_GAP_X); }
  static int16_t tileY(int slot) { return TILE_Y0 + (slot / 2) * (TILE_H + TILE_GAP_Y); }

  // Same anatomy as a home tile, smaller icon: selection = inverted,
  // switching = dashed outline, not in this build = dashed and hatched
  void drawTile(Ui2Canvas& c, int slot, bool& anyPending) {
    using namespace ui2;
    const QsItem it = itemAt(slot);
    QsTileState st;
    meckQsState(it, st);
    if (st.pending) anyPending = true;
    const int16_t x = tileX(slot), y = tileY(slot);
    const bool sel = slot == _sel;
    const bool fg = !sel;
    if (sel) c.fillRoundRect(x, y, TILE_W, TILE_H, TILE_RADIUS, true);
    else if (st.pending || !st.available) c.dashedRect(x, y, TILE_W, TILE_H, true);
    else c.roundRect(x, y, TILE_W, TILE_H, TILE_RADIUS, true);

    if (!st.available) c.hatch(x + 7, y + 6, 16, 16, fg);
    c.icon(x + 7, y + 6, iconOf(it), 16, fg);
    if (st.available) c.toggle(x + TILE_W - 7 - 26, y + 7, st.on, fg);

    c.textFit(x + 7, Ui2Canvas::lineBaseline(UI2_F_LABEL, y + 23, 15), TILE_W - 14, labelOf(it), UI2_F_LABEL, fg);
    c.textFit(x + 7, Ui2Canvas::lineBaseline(UI2_F_META, y + 37, 13), TILE_W - 14,
              st.available ? st.line : MECK_TR("Not in this build", "Absent de ce build"), UI2_F_META, fg);
  }

public:
  // Keeps the selection and view from the last time
  void open() { _wantsExit = false; _more = -1; }
  bool wantsExit() const { return _wantsExit; }
  void clearExit() { _wantsExit = false; }
  // Item whose Settings page was asked for, once; -1 = none
  int takeMore() { int m = _more; _more = -1; return m; }

  bool handleInput(char c) override {
    switch (c) {
      case 'w': case 'W': case (char)KEY_UP: case (char)KEY_PREV:
        _sel = (_sel + TILES - 2) % TILES; return true;
      case 's': case 'S': case (char)KEY_DOWN: case (char)KEY_NEXT:
        _sel = (_sel + 2) % TILES; return true;
      case 'a': case 'A': case (char)KEY_LEFT:
        _sel = (_sel + TILES - 1) % TILES; return true;
      case 'd': case 'D': case (char)KEY_RIGHT:
        _sel = (_sel + 1) % TILES; return true;
      case '\r':
        meckQsSwitch(itemAt(_sel)); return true;
      case 'm': case 'M':
        _more = itemAt(_sel); return true;
      case 'l': case 'L':
        _lockedView = !_lockedView; return true;
      case 'q': case 'Q': case '\b': case KEY_CANCEL:
        _wantsExit = true; return true;
      default:
        return false;
    }
  }

  // Touch, physical pixels. Tap: header closes, a chip picks the view, a
  // tile is selected and switched. Long press: the tile's Settings page.
  void tapAt(int16_t px, int16_t py) {
    using namespace ui2;
    if (py < HDR_H) { _wantsExit = true; return; }
    if (py < TITLE_Y + TITLE_H) {
      if (px >= _chipNowX && px < _chipLockX) _lockedView = false;
      else if (px >= _chipLockX && px < _chipEndX) _lockedView = true;
      return;
    }
    int slot = slotAt(px, py);
    if (slot < 0) return;
    _sel = slot;
    meckQsSwitch(itemAt(slot));
  }
  void longPressAt(int16_t px, int16_t py) {
    int slot = slotAt(px, py);
    if (slot < 0) return;
    _sel = slot;
    _more = itemAt(slot);
  }
  int slotAt(int16_t px, int16_t py) const {
    for (int s = 0; s < TILES; s++) {
      if (px >= tileX(s) && px < tileX(s) + ui2::TILE_W && py >= tileY(s) && py < tileY(s) + TILE_H) return s;
    }
    return -1;
  }

  int render(DisplayDriver& display) override {
    using namespace ui2;
    display.startFrame();
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);

    // Title bar: the title, then the two views as chips on the right
    const char* now = MECK_TR("Now", "Maintenant");
    const char* locked = MECK_TR("Locked", "Verrouill\xC3\xA9");
    _chipEndX = SCREEN_W - PAD;
    _chipLockX = _chipEndX - Ui2Canvas::chipWidth(locked, UI2_IC_LOCK);
    _chipNowX = _chipLockX - 4 - Ui2Canvas::chipWidth(now);
    c.chip(_chipNowX, TITLE_Y + 5, now, UI2_NO_ICON, !_lockedView);
    c.chip(_chipLockX, TITLE_Y + 5, locked, UI2_IC_LOCK, _lockedView);
    c.textFit(PAD, Ui2Canvas::boxBaseline(UI2_F_TITLE, TITLE_Y, TITLE_H - 1), _chipNowX - 6 - PAD,
              _lockedView ? MECK_TR("When locked", "Verrouill\xC3\xA9") : MECK_TR("Quick settings", "R\xC3\xA9glages rapides"),
              UI2_F_TITLE);
    c.hline(0, TITLE_Y + TITLE_H - 1, SCREEN_W);

    bool anyPending = false;
    for (int s = 0; s < TILES; s++) drawTile(c, s, anyPending);

    char verdict[64];
    meckQsLockVerdict(verdict, sizeof(verdict));
    c.icon(PAD, VERDICT_Y + 4, UI2_IC_LOCK, 12);
    c.textFit(PAD + 16, Ui2Canvas::boxBaseline(UI2_F_SMALL, VERDICT_Y, FOOTER_Y - VERDICT_Y), SCREEN_W - 2 * PAD - 16,
              verdict, UI2_F_SMALL);

    const Ui2Key keys[] = {
      ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Switch", "Basculer")),
      ui2Key("WASD", MECK_TR("Move", "D\xC3\xA9placer"), true),
      ui2Key("M", MECK_TR("More", "Plus")),
      ui2Key("L", _lockedView ? MECK_TR("Now", "Maintenant") : MECK_TR("Locked", "Verrouill\xC3\xA9")),
    };
    Ui2Chrome::footer(c, keys, 4);
    // While something switches, follow it closely
    return anyPending ? 1000 : 5000;
  }
};
