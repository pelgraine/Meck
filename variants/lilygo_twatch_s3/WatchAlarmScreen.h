#pragma once

// =============================================================================
// WatchAlarmScreen -- vibrate-only alarm clock for the LilyGo T-Watch S3.
//
// The T-Deck Pro's AlarmScreen.h is bound to ESP32-audioI2S, an SD card and a
// 1-21 volume scale, none of which exist here. This is a separate, much smaller
// screen that keeps AlarmScreen's conventions (bit 0 = Sunday day-of-week mask,
// dowFromEpoch, the 3-buzz / 4s-pause vibrate cadence, effect 14) so the two
// stay recognisably related.
//
// Alarm is the DRV2605L on I2C 0x5A driving the 0827 coin motor. Its BLDO2 rail
// is already up from power_init(), so no motorEnable() call is needed.
//
// Config lives on the LittleFS partition mounted at /maps in main.cpp -- the
// same one the Plus uses for map tiles. The watch has no SD card.
//
// Geometry: the display is 240x240 physical, UI_ZOOM=2, so display.width() and
// display.height() both report 120 logical pixels.
// =============================================================================

// NOTE on include order: this header uses NodePrefs, which lives at
// examples/companion_radio/NodePrefs.h and is not on this variant's -I path.
// UITask.h pulls it in via "../NodePrefs.h", so WatchAlarmScreen.h must always
// be included *after* UITask.h. Both call sites (UITask.cpp, main.cpp) do.
// This mirrors how TWatchComposeScreens.h depends on ChannelScreen.h.
#include <Arduino.h>
#include <string.h>
#include <LittleFS.h>
#include <MeshCore.h>                    // mesh::RTCClock
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/LGFXDisplay.h>
#include "DRV2605Haptic.h"

class UITask;

#define WATCH_ALARM_SLOT_COUNT   4
#define WATCH_ALARM_CFG_PATH     "/alarms.cfg"
#define WATCH_ALARM_CFG_MAGIC    0x334B414CUL   // "LAK3"
#define WATCH_ALARM_CFG_VERSION  1

// Day-of-week bitmask, bit 0 = Sunday (matches AlarmScreen.h)
#define WDOW_SUN  (1 << 0)
#define WDOW_MON  (1 << 1)
#define WDOW_TUE  (1 << 2)
#define WDOW_WED  (1 << 3)
#define WDOW_THU  (1 << 4)
#define WDOW_FRI  (1 << 5)
#define WDOW_SAT  (1 << 6)
#define WDOW_ALL       0x7F
#define WDOW_WEEKDAYS  (WDOW_MON | WDOW_TUE | WDOW_WED | WDOW_THU | WDOW_FRI)
#define WDOW_WEEKEND   (WDOW_SAT | WDOW_SUN)

// Vibrate cadence, identical to AlarmScreen's silent alarm on the T-Deck MAX.
#define WATCH_ALARM_EFFECT        14      // library 1: strong buzz, ~1s
#define WATCH_ALARM_BUZZ_MS       1200    // spacing between buzz starts
#define WATCH_ALARM_GROUP         3       // buzzes per group
#define WATCH_ALARM_PAUSE_MS      4000    // pause after each group
#define WATCH_ALARM_RINGING_MS    300000  // 5 minute auto-dismiss
#define WATCH_ALARM_FIRE_COOLDOWN 90      // seconds; do not re-fire the same slot

struct WatchAlarmSlot {
  bool    enabled;
  uint8_t hour;     // 0-23
  uint8_t minute;   // 0-59
  uint8_t days;     // day-of-week bitmask; WDOW_ALL = every day
};

struct WatchAlarmConfig {
  uint32_t       magic;
  uint8_t        version;
  uint8_t        _pad[3];
  WatchAlarmSlot slots[WATCH_ALARM_SLOT_COUNT];
};

class WatchAlarmScreen : public UIScreen {
public:
  enum Mode { LIST, EDIT, RINGING };

private:
  UITask*          _task;
  mesh::RTCClock*  _rtc;
  NodePrefs*       _node_prefs;

  WatchAlarmConfig _cfg;
  Mode             _mode = LIST;
  int              _sel = 0;            // selected slot in LIST / slot under EDIT
  WatchAlarmSlot   _edit;               // working copy while editing

  // Ringing state
  int              _ringSlot = -1;
  unsigned long    _ringStartMs = 0;
  unsigned long    _vibNextMs = 0;
  int              _vibCount = 0;

  // Fire tracking
  uint32_t         _lastFireEpoch[WATCH_ALARM_SLOT_COUNT] = {0};

  DRV2605Haptic    _haptic;
  bool             _hapticReady = false;

  static int dowFromEpoch(uint32_t epoch) {
    // 1970-01-01 was a Thursday (index 4 with 0 = Sunday)
    return (int)((epoch / 86400 + 4) % 7);
  }

  int32_t localNow() const {
    uint32_t utc = _rtc->getCurrentTime();
    if (utc < 1700000000) return -1;   // clock not set yet
    return (int32_t)utc + ((int32_t)_node_prefs->utc_offset_hours * 3600);
  }

  static const char* daysLabel(uint8_t d) {
    if (d == WDOW_ALL)      return "Daily";
    if (d == WDOW_WEEKDAYS) return "Mon-Fri";
    if (d == WDOW_WEEKEND)  return "Sat/Sun";
    if (d == 0)             return "Once";
    return "Custom";
  }

  static uint8_t nextDaysPreset(uint8_t d) {
    if (d == WDOW_ALL)      return WDOW_WEEKDAYS;
    if (d == WDOW_WEEKDAYS) return WDOW_WEEKEND;
    if (d == WDOW_WEEKEND)  return 0;          // Once
    return WDOW_ALL;
  }

  void loadDefaults() {
    memset(&_cfg, 0, sizeof(_cfg));
    _cfg.magic   = WATCH_ALARM_CFG_MAGIC;
    _cfg.version = WATCH_ALARM_CFG_VERSION;
    for (int i = 0; i < WATCH_ALARM_SLOT_COUNT; i++) {
      _cfg.slots[i].enabled = false;
      _cfg.slots[i].hour    = 7;
      _cfg.slots[i].minute  = 0;
      _cfg.slots[i].days    = WDOW_ALL;
    }
  }

public:
  WatchAlarmScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* node_prefs)
    : _task(task), _rtc(rtc), _node_prefs(node_prefs) {
    loadDefaults();
  }

  // Read the config from LittleFS. Called once from UITask::begin() after the
  // /maps partition is mounted. A missing or malformed file leaves the defaults.
  void load() {
    File f = LittleFS.open(WATCH_ALARM_CFG_PATH, "r");
    if (!f) return;
    WatchAlarmConfig tmp;
    size_t n = f.read((uint8_t*)&tmp, sizeof(tmp));
    f.close();
    if (n != sizeof(tmp)) return;
    if (tmp.magic != WATCH_ALARM_CFG_MAGIC) return;
    if (tmp.version != WATCH_ALARM_CFG_VERSION) return;
    for (int i = 0; i < WATCH_ALARM_SLOT_COUNT; i++) {
      if (tmp.slots[i].hour > 23 || tmp.slots[i].minute > 59) return;  // reject junk
    }
    _cfg = tmp;
  }

  void save() {
    File f = LittleFS.open(WATCH_ALARM_CFG_PATH, "w");
    if (!f) {
      Serial.println("WatchAlarmScreen: save FAILED (LittleFS)");
      return;
    }
    f.write((const uint8_t*)&_cfg, sizeof(_cfg));
    f.close();
  }

  Mode getMode() const { return _mode; }
  bool isRinging() const { return _mode == RINGING; }

  void enter() { _mode = LIST; }

  // ---------------------------------------------------------------------------
  // Firing. Driven from UITask::loop() so an alarm still fires with the display
  // off or another screen showing. Returns true on the loop where it fires.
  // ---------------------------------------------------------------------------
  bool tick() {
    if (_mode == RINGING) {
      driveVibrate();
      if (millis() - _ringStartMs > WATCH_ALARM_RINGING_MS) dismiss();
      return false;
    }

    int32_t local = localNow();
    if (local < 0) return false;

    int hrs  = (local / 3600) % 24;  if (hrs < 0)  hrs  += 24;
    int mins = (local / 60) % 60;    if (mins < 0) mins += 60;
    int dow  = dowFromEpoch((uint32_t)local);
    uint32_t nowEpoch = (uint32_t)local;

    for (int i = 0; i < WATCH_ALARM_SLOT_COUNT; i++) {
      const WatchAlarmSlot& s = _cfg.slots[i];
      if (!s.enabled) continue;
      if (s.hour != hrs || s.minute != mins) continue;
      if (s.days != 0 && !(s.days & (1 << dow))) continue;   // days==0 means Once
      if (nowEpoch - _lastFireEpoch[i] < WATCH_ALARM_FIRE_COOLDOWN) continue;

      _lastFireEpoch[i] = nowEpoch;
      if (_cfg.slots[i].days == 0) {   // one-shot: disarm after firing
        _cfg.slots[i].enabled = false;
        save();
      }
      startRinging(i);
      return true;
    }
    return false;
  }

  void startRinging(int slot) {
    _mode        = RINGING;
    _ringSlot    = slot;
    _ringStartMs = millis();
    _vibNextMs   = 0;
    _vibCount    = 0;
  }

  void dismiss() {
    _mode     = LIST;
    _ringSlot = -1;
  }

private:
  void driveVibrate() {
    if (!_hapticReady) {
      _hapticReady = _haptic.begin();   // BLDO2 is already up; no motorEnable()
      if (!_hapticReady) return;
    }
    if (millis() < _vibNextMs) return;

    _haptic.buzz(WATCH_ALARM_EFFECT);
    if (++_vibCount >= WATCH_ALARM_GROUP) {
      _vibCount  = 0;
      _vibNextMs = millis() + WATCH_ALARM_PAUSE_MS;
    } else {
      _vibNextMs = millis() + WATCH_ALARM_BUZZ_MS;
    }
  }

  // ---------------------------------------------------------------------------
  // Rendering. 120x120 logical.
  // ---------------------------------------------------------------------------
  static const int ROW_Y0 = 22;   // first list row top
  static const int ROW_H  = 22;   // list row height
  static const int TOGGLE_X = 88; // left edge of the ON/OFF badge column

  void renderList(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 6, "Alarms");

    char buf[16];
    for (int i = 0; i < WATCH_ALARM_SLOT_COUNT; i++) {
      int y = ROW_Y0 + i * ROW_H;
      const WatchAlarmSlot& s = _cfg.slots[i];

      if (i == _sel) {
        d->setRawColor(0x231D);                     // same blue as the home tile
        d->drawRoundRect(1, y, display.width() - 2, ROW_H - 2, 3);
      }

      d->setRawColor(s.enabled ? 0xFFFF : 0x7BEF);  // white when armed, grey when off
      snprintf(buf, sizeof(buf), "%02d:%02d", s.hour, s.minute);
      display.setCursor(5, y + 3);
      display.print(buf);

      d->setRawColor(0x7BEF);
      d->printSmallFont(5, y + 14, daysLabel(s.days));

      d->setRawColor(s.enabled ? 0xC618 : 0x4208);
      d->printSmallFont(TOGGLE_X + 4, y + 8, s.enabled ? "ON" : "OFF");
    }

    display.setColor(DisplayDriver::LIGHT);
    d->setRawColor(0x7BEF);
    d->printSmallFont(4, display.height() - 7, "Tap time:edit  ON/OFF:arm");
  }

  void renderEdit(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 4, "Edit alarm");

    // Up arrows / value / down arrows. Hour occupies x 10..54, minute 66..110.
    d->setRawColor(0xC618);
    display.drawTextCentered(32, 22, "+");
    display.drawTextCentered(88, 22, "+");
    display.drawTextCentered(32, 74, "-");
    display.drawTextCentered(88, 74, "-");

    char buf[8];
    d->setRawColor(0xFFFF);
    display.setTextSize(2);
    snprintf(buf, sizeof(buf), "%02d", _edit.hour);
    display.drawTextCentered(32, 44, buf);
    snprintf(buf, sizeof(buf), "%02d", _edit.minute);
    display.drawTextCentered(88, 44, buf);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 48, ":");

    // Footer: left half cycles the day preset, right half saves.
    d->setRawColor(0x231D);
    d->drawRoundRect(1, 92, 58, 18, 3);
    d->drawRoundRect(61, 92, 58, 18, 3);
    d->setRawColor(0xC618);
    d->printSmallFont(6, 100, daysLabel(_edit.days));
    d->setRawColor(0xFFFF);
    d->printSmallFont(78, 100, "Save");
  }

  void renderRinging(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    char buf[8];

    d->setRawColor(0xFFFF);
    if (_ringSlot >= 0) {
      snprintf(buf, sizeof(buf), "%02d:%02d", _cfg.slots[_ringSlot].hour,
                                              _cfg.slots[_ringSlot].minute);
    } else {
      strcpy(buf, "--:--");
    }
    d->printClockFont(display.width() / 2, display.height() / 2 - 22, buf);

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, display.height() - 24, "ALARM");
    ((LGFXDisplay*)&display)->printSmallFont(24, display.height() - 10, "Tap to dismiss");
  }

public:
  int render(DisplayDriver& display) override {
    switch (_mode) {
      case LIST:    renderList(display);    return 500;
      case EDIT:    renderEdit(display);    return 200;
      case RINGING: renderRinging(display); return 250;
    }
    return 500;
  }

  // Physical tap in logical (display.width()) coordinates. Returns true if the
  // tap was consumed. The status-bar strip is handled by main.cpp before this.
  bool handleTap(int lx, int ly) {
    if (_mode == RINGING) { dismiss(); return true; }

    if (_mode == LIST) {
      for (int i = 0; i < WATCH_ALARM_SLOT_COUNT; i++) {
        int y = ROW_Y0 + i * ROW_H;
        if (ly < y || ly >= y + ROW_H - 2) continue;
        if (lx >= TOGGLE_X) {                       // ON/OFF badge: arm / disarm
          _cfg.slots[i].enabled = !_cfg.slots[i].enabled;
          _sel = i;
          save();
        } else {                                    // time area: open the editor
          _sel  = i;
          _edit = _cfg.slots[i];
          _mode = EDIT;
        }
        return true;
      }
      return false;
    }

    // EDIT
    if (ly >= 92) {                                 // footer
      if (lx < 60) {
        _edit.days = nextDaysPreset(_edit.days);
      } else {
        commitEdit();
      }
      return true;
    }
    bool hourCol = (lx < display_mid());
    if (ly < 40) {                                  // upper half: increment
      if (hourCol) _edit.hour = (_edit.hour + 1) % 24;
      else         _edit.minute = (_edit.minute + 1) % 60;
      return true;
    }
    if (ly >= 62) {                                 // lower half: decrement
      if (hourCol) _edit.hour = (_edit.hour + 23) % 24;
      else         _edit.minute = (_edit.minute + 59) % 60;
      return true;
    }
    return false;
  }

  bool handleInput(char c) override {
    // The PMU short press arrives as KEY_ENTER.
    if (_mode == RINGING) { dismiss(); return true; }
    if (_mode == EDIT && c == KEY_ENTER) { commitEdit(); return true; }
    if (_mode == LIST && c == KEY_ENTER) {
      _edit = _cfg.slots[_sel];
      _mode = EDIT;
      return true;
    }
    return false;
  }

private:
  int display_mid() const { return 60; }   // half of the 120px logical width

  void commitEdit() {
    _cfg.slots[_sel] = _edit;
    _cfg.slots[_sel].enabled = true;       // saving an edit arms the alarm
    save();
    _mode = LIST;
  }
};
