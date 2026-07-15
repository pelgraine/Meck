#pragma once

// =============================================================================
// WatchChannelConfigScreen -- per-channel settings for the T-Watch S3 / S3 Plus.
//
// The SettingsScreen SUB_CHANNELS sub-screen drives every per-channel action
// from physical-keyboard hotkeys on the selected row (Ent: edit region scope,
// N: cycle notifications, T: tone, X/Hold: delete), none of which exist on the
// touch-only watch. This standalone screen replaces it on MECK_TWATCH builds,
// following the WatchAlarmScreen / WatchNotesScreen conventions.
//
// LIST mode shows each channel with its region scope tag and notification
// state; tap a row to select it, tap again (or PWR/Enter) to open it. DETAIL
// mode offers: Region (opens the watch keyboard via TWKB_SCOPE, pre-filled
// with the current scope), Clear region (reverts to the device default),
// Notify (tap-cycles All -> Mentions -> Off, mirroring the desktop order),
// and Delete via the button (channel 1+ only): a short press of the PWR key
// (S3) or the boot button (S3 Plus) arms it, a second press within 3s
// confirms. The tone picker is deliberately absent -- no audio path.
//
// Exit: the footer Back returns to the Settings screen (via a flag polled by
// UITask::loop); the universal top-strip tap still goes home.
//
// Geometry: 240x240 physical, UI_ZOOM=2, so display.width()/height() = 120.
// =============================================================================

// NOTE on include order: mirrors WatchNotesScreen.h -- this header uses
// NodePrefs (NOTIF_* constants, channel_notif[]) and the_mesh, which the two
// call sites (UITask.cpp, main.cpp) already have in scope before including it.
#include <Arduino.h>
#include <string.h>
#include <MeshCore.h>
#include <helpers/ChannelDetails.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/LGFXDisplay.h>

class UITask;

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 20
#endif

#define WCC_ROWS 4    // list rows per page

class WatchChannelConfigScreen : public UIScreen {
public:
  enum Mode { LIST, DETAIL };

private:
  UITask*    _task;
  NodePrefs* _node_prefs;

  uint8_t _chIdx[MAX_GROUP_CHANNELS];   // populated channel slots, in order
  int     _numCh = 0;

  Mode _mode = LIST;
  int  _sel = 0;          // selected list row (index into _chIdx)
  int  _scrollTop = 0;
  int  _detail = -1;      // channel slot open in DETAIL (value from _chIdx)
  bool _confirmDelete = false;
  unsigned long _confirmAt = 0;

  // Keyboard / exit handshake -- UITask::loop() polls these (UITask is only
  // forward declared here, so this screen cannot call it directly).
  bool _wantKeyboard = false;
  bool _wantExit = false;
  char _editScope[31];    // pre-fill text handed to the keyboard

  // Layout (logical pixels)
  static const int HEADER_H = 14;
  static const int ROW_H    = 20;
  static const int FOOTER_Y = 100;

  void rebuildList() {
    // Scan ALL slots -- the companion app may write non-contiguously, and
    // gaps can appear after deletion (same scan as SettingsScreen).
    _numCh = 0;
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        _chIdx[_numCh++] = i;
      }
    }
    if (_sel >= _numCh) _sel = _numCh > 0 ? _numCh - 1 : 0;
    if (_scrollTop > _sel) _scrollTop = _sel;
  }

  const char* notifLabel(uint8_t idx) const {
    uint8_t n = _node_prefs->channel_notif[idx];
    return (n == NOTIF_NONE) ? "Off" : (n == NOTIF_MENTIONS) ? "@" : "All";
  }

  // Replicates SettingsScreen::deleteChannel (kept in sync manually). Note:
  // channel_notif[] is not shifted during compaction -- this matches the
  // desktop behaviour exactly.
  void deleteChannel(uint8_t idx) {
    ChannelDetails empty;
    memset(&empty, 0, sizeof(empty));
    int total = 0;
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        total = i + 1;
      }
    }
    for (int i = idx; i < total - 1; i++) {
      ChannelDetails next;
      if (the_mesh.getChannel(i + 1, next)) {
        the_mesh.setChannel(i, next);
      }
    }
    the_mesh.setChannel(total - 1, empty);
    the_mesh.saveChannels();
    Serial.printf("WatchChannelCfg: Deleted channel at idx %d, compacted %d channels\n", idx, total);
  }

  void ensureVisible() {
    if (_sel < _scrollTop) _scrollTop = _sel;
    if (_sel >= _scrollTop + WCC_ROWS) _scrollTop = _sel - WCC_ROWS + 1;
  }

public:
  WatchChannelConfigScreen(UITask* task, NodePrefs* node_prefs)
    : _task(task), _node_prefs(node_prefs) {
    _editScope[0] = 0;
  }

  void enter() {
    rebuildList();
    _mode = LIST;
    _detail = -1;
    _confirmDelete = false;
    _wantKeyboard = false;
    _wantExit = false;
  }

  // --- handshake (polled from UITask::loop) ---
  bool wantsKeyboard() const { return _wantKeyboard; }
  void clearWantKeyboard() { _wantKeyboard = false; }
  bool wantsExit() const { return _wantExit; }
  void clearWantExit() { _wantExit = false; }
  const char* getEditText() const { return _editScope; }

  // Save the keyboard result as the open channel's region scope. An empty
  // string clears it back to the device default (same as Clear). Returns
  // NULL on success, else an error message.
  const char* applyComposedScope(const char* txt) {
    if (_detail < 0) return "No channel open";
    ChannelDetails ch;
    if (!the_mesh.getChannel((uint8_t)_detail, ch)) return "Channel gone";
    if (txt == nullptr) txt = "";
    strncpy(ch.scope_name, txt, sizeof(ch.scope_name));
    ch.scope_name[30] = '\0';
    the_mesh.setChannel((uint8_t)_detail, ch);
    the_mesh.saveChannels();
    Serial.printf("WatchChannelCfg: Channel %d scope set to '%s'\n",
                  _detail, ch.scope_name[0] ? ch.scope_name : "(device default)");
    return nullptr;
  }

  // ---------------------------------------------------------------------------
  // Rendering. 120x120 logical.
  // ---------------------------------------------------------------------------
private:
  void renderList(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 4, "Channels");

    if (_numCh == 0) {
      d->setRawColor(0x7BEF);
      display.drawTextCentered(display.width() / 2, 48, "No channels");
    }

    char buf[40];
    int visible = _numCh - _scrollTop;
    if (visible > WCC_ROWS) visible = WCC_ROWS;
    for (int i = 0; i < visible; i++) {
      uint8_t idx = _chIdx[_scrollTop + i];
      int y = HEADER_H + i * ROW_H;
      ChannelDetails ch;
      if (!the_mesh.getChannel(idx, ch)) continue;

      if (_scrollTop + i == _sel) {
        d->setRawColor(0x231D);                     // same blue as the home tile
        d->drawRoundRect(1, y, display.width() - 2, ROW_H - 2, 3);
      }
      d->setRawColor(0xFFFF);
      char nameBuf[15];
      strncpy(nameBuf, ch.name, sizeof(nameBuf) - 1);
      nameBuf[sizeof(nameBuf) - 1] = 0;
      display.setCursor(5, y + 2);
      display.print(nameBuf);

      snprintf(buf, sizeof(buf), "[%s]  %s",
               ch.scope_name[0] ? ch.scope_name : "*", notifLabel(idx));
      d->setRawColor(0x7BEF);
      d->printSmallFont(5, y + 13, buf);
    }

    // Footer: Back (left) and, when the list overflows a page, More (right).
    d->setRawColor(0x231D);
    d->drawRoundRect(1, FOOTER_Y, 58, 18, 3);
    d->setRawColor(0xFFFF);
    d->printSmallFont(20, FOOTER_Y + 8, "Back");
    if (_numCh > WCC_ROWS) {
      d->setRawColor(0x231D);
      d->drawRoundRect(61, FOOTER_Y, 58, 18, 3);
      d->setRawColor(0xC618);
      d->printSmallFont(78, FOOTER_Y + 8, "More");
    }
  }

  void renderDetail(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    char buf[44];

    ChannelDetails ch;
    if (_detail < 0 || !the_mesh.getChannel((uint8_t)_detail, ch)) {
      _mode = LIST;
      return;
    }

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    char nameBuf[18];
    strncpy(nameBuf, ch.name, sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = 0;
    display.drawTextCentered(display.width() / 2, 3, nameBuf);

    // Region row -- opens the keyboard.
    d->setRawColor(0x231D);
    d->drawRoundRect(1, 16, display.width() - 2, 18, 3);
    snprintf(buf, sizeof(buf), "Region: %s",
             ch.scope_name[0] ? ch.scope_name : "(default)");
    d->setRawColor(0xFFFF);
    d->printSmallFont(6, 24, buf);

    // Clear-region row -- dim when there is nothing to clear.
    d->setRawColor(ch.scope_name[0] ? 0x231D : 0x18C3);
    d->drawRoundRect(1, 36, display.width() - 2, 18, 3);
    d->setRawColor(ch.scope_name[0] ? 0xC618 : 0x4208);
    d->printSmallFont(6, 44, "Clear region (use default)");

    // Notify row -- tap cycles All -> Mentions -> Off.
    d->setRawColor(0x231D);
    d->drawRoundRect(1, 56, display.width() - 2, 18, 3);
    uint8_t n = _node_prefs->channel_notif[(uint8_t)_detail];
    snprintf(buf, sizeof(buf), "Notify: %s",
             (n == NOTIF_NONE) ? "Off" : (n == NOTIF_MENTIONS) ? "Mentions" : "All");
    d->setRawColor(0xFFFF);
    d->printSmallFont(6, 64, buf);

    // Delete row -- status only; deletion is driven by the button (see
    // handleInput). Channel 0 (public) cannot be deleted, same as desktop.
    bool canDel = (_detail > 0);
    d->setRawColor(canDel ? 0x231D : 0x18C3);
    d->drawRoundRect(1, 76, display.width() - 2, 18, 3);
    d->setRawColor(canDel ? (_confirmDelete ? 0xF800 : 0xC618) : 0x4208);
    d->printSmallFont(6, 84, !canDel ? "Delete (public ch)"
                              : _confirmDelete ? "Delete? press again" : "Button: delete");

    // Footer: Back to the list.
    d->setRawColor(0x231D);
    d->drawRoundRect(1, FOOTER_Y, 58, 18, 3);
    d->setRawColor(0xFFFF);
    d->printSmallFont(20, FOOTER_Y + 8, "Back");
  }

public:
  int render(DisplayDriver& display) override {
    if (_confirmDelete && millis() - _confirmAt > 3000) _confirmDelete = false;
    switch (_mode) {
      case LIST:   renderList(display);   return 500;
      case DETAIL: renderDetail(display); return 500;
    }
    return 500;
  }

  // Physical tap in logical coordinates. The top-strip home tap is handled by
  // main.cpp before this is called. Returns true if the tap was consumed.
  bool handleTap(int lx, int ly) {
    if (_mode == LIST) {
      if (ly >= FOOTER_Y) {
        if (lx < 60) {                       // Back -> Settings
          _wantExit = true;
        } else if (_numCh > WCC_ROWS) {      // More: next page (wraps)
          _scrollTop += WCC_ROWS;
          if (_scrollTop >= _numCh) _scrollTop = 0;
          _sel = _scrollTop;
        }
        return true;
      }
      if (ly >= HEADER_H) {
        int i = (ly - HEADER_H) / ROW_H;
        int idx = _scrollTop + i;
        if (i < WCC_ROWS && idx < _numCh) {
          if (idx == _sel) {                 // second tap on the row: open it
            _detail = _chIdx[idx];
            _mode = DETAIL;
            _confirmDelete = false;
          } else {
            _sel = idx;                      // first tap: select
            ensureVisible();
          }
          return true;
        }
      }
      return false;
    }

    // DETAIL
    if (ly >= FOOTER_Y) {                    // Back -> list
      _confirmDelete = false;
      _mode = LIST;
      rebuildList();
      return true;
    }
    ChannelDetails ch;
    if (_detail < 0 || !the_mesh.getChannel((uint8_t)_detail, ch)) {
      _mode = LIST;
      return true;
    }
    if (ly >= 16 && ly < 34) {               // Region -> keyboard, pre-filled
      strncpy(_editScope, ch.scope_name, sizeof(_editScope));
      _editScope[30] = '\0';
      _wantKeyboard = true;
      return true;
    }
    if (ly >= 36 && ly < 54) {               // Clear region
      if (ch.scope_name[0]) {
        ch.scope_name[0] = '\0';
        the_mesh.setChannel((uint8_t)_detail, ch);
        the_mesh.saveChannels();
        Serial.printf("WatchChannelCfg: Channel %d scope cleared\n", _detail);
      }
      return true;
    }
    if (ly >= 56 && ly < 74) {               // Notify: cycle All -> @ -> Off
      uint8_t cur = _node_prefs->channel_notif[(uint8_t)_detail];
      _node_prefs->channel_notif[(uint8_t)_detail] = (cur + 1) % 3;
      the_mesh.savePrefs();
      return true;
    }
    return true;                             // swallow other taps (incl. delete row)
  }

  bool handleInput(char c) override {
    // The PMU short press (S3) arrives as KEY_ENTER; the S3 Plus boot button
    // arrives as KEY_NEXT.
    if (_mode == LIST && c == KEY_ENTER) {
      if (_sel < _numCh) {
        _detail = _chIdx[_sel];
        _mode = DETAIL;
        _confirmDelete = false;
      }
      return true;
    }
    if (_mode == DETAIL && (c == KEY_ENTER || c == KEY_NEXT)) {
      // Button click deletes the open channel (channel 0 is protected):
      // first press arms, a second press within 3s confirms. Back is the
      // touch footer.
      if (_detail > 0) {
        if (_confirmDelete) {
          deleteChannel((uint8_t)_detail);
          _confirmDelete = false;
          _detail = -1;
          rebuildList();
          _mode = LIST;
        } else {
          _confirmDelete = true;
          _confirmAt = millis();
        }
      }
      return true;
    }
    return false;
  }
};