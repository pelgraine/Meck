#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "NotesScreen.h"
#include "RepeaterAdminScreen.h"
#include "PathEditorScreen.h"
#include "DiscoveryScreen.h"
#include "LastHeardScreen.h"
#include "RxLogScreen.h"
#include "Tracescreen.h"
#include "GamesMenuScreen.h"
#include "SnakeScreen.h"
#include "MinesweeperScreen.h"
#if defined(LilyGo_TDeck_Pro)
#include "GBCEmulatorScreen.h"
#endif
#ifdef MECK_WEB_READER
  #include "WebReaderScreen.h"
#endif
#if   HAS_GPS
  #include "MapScreen.h"
#endif
#include "target.h"
#if defined(LilyGo_TDeck_Pro_Max)
  #include "DRV2605Haptic.h"   // haptic motor for "Buzzer (vibrate)" channels
#endif
#if defined(MECK_AUDIO_VARIANT) || defined(LilyGo_TDeck_Pro)
  #include "HomeIcons.h"
#endif
#if defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
  #include <WiFi.h>
#endif
#if defined(BLE_PIN_CODE) && defined(MECK_WIFI_COMPANION)
  // Combined BLE + WiFi companion (meck_max_ble_wifi): defined in main.cpp
  extern bool meckCompanionIsBLE();
  extern bool meckCompanionUseBLE();
  extern void meckCompanionUseNone();
#endif

#ifndef AUTO_OFF_MILLIS
  #define AUTO_OFF_MILLIS     15000   // 15 seconds
#endif
// Right-aligned values on the dense home pages compensate for the e-ink X
// origin offset so they are not clipped at the right edge. Default 0 for
// boards that do not define EINK_X_OFFSET (no change to their layout).
#ifndef EINK_X_OFFSET
  #define EINK_X_OFFSET 0
#endif
#define BOOT_SCREEN_MILLIS   3000   // 3 seconds

#ifdef PIN_STATUS_LED
#define LED_ON_MILLIS     20
#define LED_ON_MSG_MILLIS 200
#define LED_CYCLE_MILLIS  4000
#endif

#define LONG_PRESS_MILLIS   1200

#ifndef UI_RECENT_LIST_SIZE
    #define UI_RECENT_LIST_SIZE 4
#endif

#define PRESS_LABEL "long press"

#include "icons.h"
#include "ChannelScreen.h"
#include "ChannelPickerScreen.h"
#include "ContactsScreen.h"
#include "TextReaderScreen.h"
#include "SettingsScreen.h"
#include "MeckLang.h"
#ifdef MECK_AUDIO_VARIANT
#include "AudiobookPlayerScreen.h"
#include "VoiceMessageScreen.h"
#endif
#ifdef HAS_4G_MODEM
  #include "SMSScreen.h"
  #include "ModemManager.h"
#endif
#if defined(MECK_AUDIO_VARIANT) || defined(HAS_4G_MODEM)
  #include "NotifSounds.h"
#endif

// Per-channel notification suppression flag.
// Set by newMsg() based on channel_notif preference, checked by notify()
// to suppress buzzer/vibration.  Safe because both are called sequentially
// from the same mesh callback on the same thread.
static bool s_lastMsgSuppressed = false;

#ifdef KB_BL_PIN
// Drive the keyboard-backlight pin for the message/ring flash. On the T-Deck
// Pro MAX the manual (both-shifts) keyboard-backlight toggle drives this pin
// via LEDC (analogWrite); once LEDC is bound to a pin a later digitalWrite is
// swallowed, so the flash must use analogWrite too or it silently does nothing
// after the manual toggle has been used. Other boards keep the plain GPIO write.
static inline void kbBacklightFlashWrite(bool on) {
  #if defined(LilyGo_TDeck_Pro_Max)
    analogWrite(KB_BL_PIN, on ? 255 : 0);
  #else
    digitalWrite(KB_BL_PIN, on ? HIGH : LOW);
  #endif
}
#endif

class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[24];

public:
  SplashScreen(UITask* task) : _task(task) {
    // strip off dash and commit hash by changing dash to null terminator
    // e.g: v1.2.3-abcdef -> v1.2.3
    const char *ver = FIRMWARE_VERSION;
    const char *dash = strchr(ver, '-');

    int len = dash ? dash - ver : strlen(ver);
    if (len >= sizeof(_version_info)) len = sizeof(_version_info) - 1;
    memcpy(_version_info, ver, len);
    _version_info[len] = 0;

    dismiss_after = millis() + BOOT_SCREEN_MILLIS;
  }

  int render(DisplayDriver& display) override {
    // meshcore logo
    display.setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    display.drawXbm((display.width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    // version info
    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(2);
    display.drawTextCentered(display.width()/2, 22, _version_info);

    display.setTextSize(1);
    display.drawTextCentered(display.width()/2, 42, FIRMWARE_BUILD_DATE);

    return 1000;
  }

void poll() override {
  if (millis() >= dismiss_after) {
    Serial.println(">>> SplashScreen calling gotoHomeScreen() <<<");
    _task->gotoHomeScreen();
  }
}
};

class HomeScreen : public UIScreen {
  enum HomePage {
    FIRST,
    RECENT,
    RADIO,
#ifdef BLE_PIN_CODE
    BLUETOOTH,
#endif
#ifdef MECK_WIFI_COMPANION
    WIFI_STATUS,
#endif
    ADVERT,
#if ENV_INCLUDE_GPS == 1 && !defined(MECK_40MHZ_TEST)
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
#if HAS_BQ27220
    BATTERY,
#endif
    SHUTDOWN,
    TIMEZONES,  // world clock; last page, so A or a right swipe from the tiles reaches it
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  unsigned long _shutdown_at;   // earliest time to proceed with shutdown (after e-ink refresh)
  bool _poweroff_selected;     // true = "power off" highlighted, false = "hibernate"
  bool _poweroff_confirm;      // true = showing confirmation prompt for power off
  bool _poweroff_msg_shown;    // true = "powering off..." already displayed once
  bool _editing_utc;
  int8_t _saved_utc_offset;  // for cancel/undo
  // Timezones page: selected row (0 = Home, 1 = Zone 1, 2 = Zone 2) and
  // the zone offset editor, which holds the edited value until saved.
  uint8_t _tz_sel;
  bool _tz_editing;
  int8_t _tz_edit_val;

  AdvertPath recent[UI_RECENT_LIST_SIZE];


void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts, int* outIconX = nullptr) {
    // Use BQ27220 fuel gauge State of Charge when available — it tracks
    // the real LiPo discharge curve, impedance, and temperature. The old
    // linear voltage mapping (3.0–4.2V) over-reports while charging
    // (voltage inflated by charger current) and is non-linear across the
    // flat middle of the discharge curve.
    uint8_t batteryPercentage = 0;
#if HAS_BQ27220
    batteryPercentage = _task->getBatteryPercent();
#else
    // Fallback for boards without a fuel gauge
    if (batteryMilliVolts > 0) {
      const int minMilliVolts = 3000;
      const int maxMilliVolts = 4200;
      int pct = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      batteryPercentage = (uint8_t)pct;
    }
#endif

    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(_node_prefs->smallTextSize());

    // T-Deck Pro: icon + percentage text (icon hidden in large font)
    int iconWidth = 16;
    int iconHeight = 6;
    int iconY = 0;
    int textY = iconY - 3;

    // measure percentage text width to position icon + text together at right edge
    char pctStr[5];
    sprintf(pctStr, "%d%%", batteryPercentage);
    uint16_t textWidth = display.getTextWidth(pctStr);

    if (_node_prefs->large_font || display.getFontStyle() > 0) {
      // Large font or custom proportional font: text only — icon doesn't align
      int textX = display.width() - textWidth - 2;
      if (outIconX) *outIconX = textX;
      display.setCursor(textX, textY);
      display.print(pctStr);
    } else {
      // Classic tiny font (monospaced): icon + text
      // layout: [icon][cap 2px][gap 2px][text][margin 2px]
      int totalWidth = iconWidth + 2 + 2 + textWidth + 2;
      int iconX = display.width() - totalWidth;

      if (outIconX) *outIconX = iconX;

      // battery outline
      display.drawRect(iconX, iconY, iconWidth, iconHeight);

      // battery "cap"
      display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 2, iconHeight / 2);

      // fill the battery based on the percentage
      int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
      display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

      // draw percentage text after the battery cap
      int textX = iconX + iconWidth + 2 + 2;  // after cap + gap
      display.setCursor(textX, textY);
      display.print(pctStr);
    }
    display.setTextSize(1);  // restore default text size
  }

#ifdef MECK_AUDIO_VARIANT
  // ---- Audio background playback indicator ----
  // Shows a small play symbol to the left of the battery icon when an
  // audiobook is actively playing in the background.
  // Uses the font renderer (not manual pixel drawing) since it handles
  // the e-ink coordinate scaling correctly.
  void renderAudioIndicator(DisplayDriver& display, int batteryLeftX) {
    if (!_task->isAudioPlayingInBackground()) return;

    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(_node_prefs->smallTextSize()); // tiny font (same as clock & battery %)
    int x = batteryLeftX - display.getTextWidth(">>") - 2;
    display.setCursor(x, -3); // align vertically with battery text
    display.print(">>");
    display.setTextSize(1); // restore
  }

  // ---- Alarm enabled indicator ----
  // Shows a small bell icon to the left of the audio indicator
  // (or battery icon if no audio playing) when any alarm is enabled.
  void renderAlarmIndicator(DisplayDriver& display, int batteryLeftX) {
    AlarmScreen* alarmScr = (AlarmScreen*)_task->getAlarmScreen();
    if (!alarmScr || alarmScr->enabledCount() == 0) return;

    // Calculate X: shift left past audio indicator if it's showing
    int rightEdge = batteryLeftX;
    if (_task->isAudioPlayingInBackground()) {
      display.setTextSize(_node_prefs->smallTextSize());
      rightEdge = rightEdge - display.getTextWidth(">>") - 2;
    }

    display.setColor(DisplayDriver::GREEN);
    int x = rightEdge - BELL_ICON_W - 2;
    display.drawXbm(x, 1, icon_bell_small, BELL_ICON_W, BELL_ICON_H);
  }
#endif

  // ==========================================================================
  // Timezones home page (world clock) -- ported from the watch and Meck-P4
  //
  // Row 0 = Home (the device utc_offset_hours), rows 1/2 = the two saved
  // zones (prefs clock_slot_a/b). Each row shows the zone label and offset,
  // the local HH:MM, a +/-1D day marker when the zone sits on a different
  // calendar day to Home, and city codes for the offset (table copied from
  // Meck-P4, which copied it from the watch). W/S or a long-press picks a
  // row and Enter opens its offset editor; in the editor W/S or taps adjust
  // the offset, Enter or a long-press saves, Q cancels. Editing Home
  // changes the device UTC offset, as on the watch and P4.
  // ==========================================================================

  // Row 0 label y and row pitch, in virtual coordinates. Also used by
  // selectTimezoneRowAt() for the touch long-press hit test.
  enum { TZ_ROW_TOP = 16, TZ_ROW_H = 34 };

  static const char* tzRowLabel(int r) {
    return (r == 1) ? "Zone 1" : (r == 2) ? "Zone 2" : MECK_TR("Home", "Local");
  }

  static const char* tzCities(int8_t off) {
    static const char* const CITIES[27] = {
      "BIT/HWL",      /* -12 */  "PPG/NIU/MDY",  /* -11 */  "HNL/PPT/RAR",  /* -10 */
      "ANC/JNU",      /*  -9 */  "LAX/VAN/SEA",  /*  -8 */  "PHX/HMO/MZT",  /*  -7 */
      "MEX/REG/GUA",  /*  -6 */  "BOG/LIM/PTY",  /*  -5 */  "CCS/LPB/SDQ",  /*  -4 */
      "BUE/SAO/MVD",  /*  -3 */  "FEN/SGS",      /*  -2 */  "RAI/PDL",      /*  -1 */
      "REY/ACC/DKR",  /*   0 */  "LOS/ALG/TUN",  /*  +1 */  "JNB/KRT/HRE",  /*  +2 */
      "NBO/MOW/ADD",  /*  +3 */  "DXB/BAK/TBS",  /*  +4 */  "KHI/TAS/SVX",  /*  +5 */
      "DAC/ALA/OMS",  /*  +6 */  "BKK/JKT/HAN",  /*  +7 */  "PER/BEI/HKG",  /*  +8 */
      "TYO/SEL/YKS",  /*  +9 */  "BNE/POM/VLA",  /* +10 */  "NOU/HIR/VLI",  /* +11 */
      "SUV/TRW/MAJ",  /* +12 */  "TBU/APW",      /* +13 */  "CXI"           /* +14 */
    };
    int i = (int)off + 12;
    if (i < 0 || i > 26) return "";
    return CITIES[i];
  }

  int8_t tzRowOffset(int r) const {
    if (r == 1) return _node_prefs->clock_slot_a;
    if (r == 2) return _node_prefs->clock_slot_b;
    return _node_prefs->utc_offset_hours;
  }

  // Same clock-validity test as the header clock: before the clock is set
  // the page shows "Clock not set" and its keys do nothing.
  bool tzClockValid() const { return _rtc->getCurrentTime() > 1700000000; }

  void renderTimezonesPage(DisplayDriver& display) {
    char buf[24];

    if (_tz_editing) {
      // Offset editor, laid out like the watch's edit page: row name, the
      // offset being edited, its city codes, then the key and touch hints.
      display.setTextSize(1);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 18, tzRowLabel(_tz_sel));
      display.setTextSize(5);
      display.setColor(DisplayDriver::LIGHT);
      snprintf(buf, sizeof(buf), "UTC%+d", (int)_tz_edit_val);
      display.drawTextCentered(display.width() / 2, 48, buf);
      display.setTextSize(1);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 64, tzCities(_tz_edit_val));
      display.setTextSize(_node_prefs->smallTextSize());
      display.drawTextCentered(display.width() / 2, 84, MECK_TR("W/S:adj Enter:ok Q:cancel", "W/S:r\xC3\xA9gler Ent:ok Q:annuler"));
      display.drawTextCentered(display.width() / 2, 96, MECK_TR("Tap:-/+  Hold:save", "Appui:-/+  Maintien:enreg."));
      display.setTextSize(1);
      return;
    }

    if (!tzClockValid()) {
      display.setTextSize(1);
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 50, MECK_TR("Clock not set", "Heure non r\xC3\xA9" "gl\xC3\xA9" "e"));
      return;
    }

    const uint32_t now = _rtc->getCurrentTime();
    const int32_t home_local = (int32_t)now + ((int32_t)tzRowOffset(0) * 3600);
    for (int r = 0; r < 3; r++) {
      const int ry = TZ_ROW_TOP + r * TZ_ROW_H;
      const int8_t off = tzRowOffset(r);
      const int32_t local = (int32_t)now + ((int32_t)off * 3600);
      int hrs = (local / 3600) % 24;
      if (hrs < 0) hrs += 24;
      int mins = (local / 60) % 60;
      if (mins < 0) mins += 60;

      // Label line: selection marker, zone name and offset
      display.setTextSize(1);
      display.setColor(DisplayDriver::GREEN);
      if (r == _tz_sel) display.drawTextLeftAlign(0, ry, ">");
      snprintf(buf, sizeof(buf), "%s UTC%+d", tzRowLabel(r), (int)off);
      display.drawTextLeftAlign(7, ry, buf);

      // Local time, large
      display.setTextSize(5);
      display.setColor(DisplayDriver::LIGHT);
      snprintf(buf, sizeof(buf), "%02d:%02d", hrs, mins);
      display.drawTextCentered(display.width() / 2, ry + 17, buf);

      // Day marker (left) and city codes (right) under the time
      display.setTextSize(1);
      display.setColor(DisplayDriver::GREEN);
      const int dayDiff = (int)((local / 86400) - (home_local / 86400));
      if (dayDiff != 0) {
        snprintf(buf, sizeof(buf), MECK_TR("%+dD", "%+dJ"), dayDiff);
        display.drawTextLeftAlign(7, ry + 27, buf);
      }
      display.drawTextRightAlign(display.width() - 1 - EINK_X_OFFSET, ry + 27, tzCities(off));
    }
  }

  // Keys on the Timezones page. Returns true when the key is used here;
  // anything else falls through to the page cycling in handleInput().
  // Keyboard W/S arrive as 'w'/'s' because main.cpp passes them through on
  // this page (as it passes S through on the Hibernate page). Touch taps
  // arrive as KEY_PREV (left half) / KEY_NEXT (right half) and a long-press
  // as KEY_ENTER, after selectTimezoneRowAt() has picked the row.
  bool handleTimezonesKey(char c) {
    if (_tz_editing) {
      // Zone offset editor: intercept all keys. Q and Shift+Del never get
      // here: main.cpp sends them to UITask::gotoHomeScreen(), whose
      // cancelEditing() closes the editor without saving.
      if (c == 'w' || c == KEY_NEXT) {
        if (_tz_edit_val < 14) _tz_edit_val++;
      } else if (c == 's' || c == KEY_PREV) {
        if (_tz_edit_val > -12) _tz_edit_val--;
      } else if (c == KEY_ENTER) {
        if (_tz_sel == 1)      _node_prefs->clock_slot_a = _tz_edit_val;
        else if (_tz_sel == 2) _node_prefs->clock_slot_b = _tz_edit_val;
        else                   _node_prefs->utc_offset_hours = _tz_edit_val;
        the_mesh.savePrefs();
        Serial.printf("Timezones: %s = UTC%+d\n", tzRowLabel(_tz_sel), (int)_tz_edit_val);
        _tz_editing = false;
      }
      return true;
    }
    if (!tzClockValid()) return false;
    if (c == 'w') { _tz_sel = (_tz_sel + 2) % 3; return true; }
    if (c == 's') { _tz_sel = (_tz_sel + 1) % 3; return true; }
    if (c == KEY_ENTER) {
      _tz_edit_val = tzRowOffset(_tz_sel);
      _tz_editing = true;
      return true;
    }
    return false;
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  bool sensors_scroll = false;
  int sensors_scroll_offset = 0;
  int next_sensors_refresh = 0;
  
  void refresh_sensors() {
    if (millis() > next_sensors_refresh) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      sensors.querySensors(0xFF, sensors_lpp);
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
      sensors_scroll = sensors_nb > UI_RECENT_LIST_SIZE;
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = millis() + 5000; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = millis() + 60000; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0), 
       _shutdown_init(false), _shutdown_at(0), _poweroff_selected(false), _poweroff_confirm(false),
       _poweroff_msg_shown(false), _editing_utc(false), _saved_utc_offset(0),
       _tz_sel(0), _tz_editing(false), _tz_edit_val(0), sensors_lpp(200) {  }

  bool isEditingUTC() const { return _editing_utc; }
  bool isFirstPage() const { return _page == HomePage::FIRST; }
  bool isOnRecentPage() const { return _page == HomePage::RECENT; }
  bool isOnShutdownPage() const { return _page == HomePage::SHUTDOWN; }
  bool isOnTimezonesPage() const { return _page == HomePage::TIMEZONES; }
  bool isEditingTimezone() const { return _tz_editing; }
  // Touch long-press on the Timezones page (main.cpp mapTouchLongPress):
  // select the zone row under the finger before the Enter that opens its
  // editor. vy comes from touchToVirtual(), which lands 5 units below the
  // render y of the same point (setCursor adds 5), so row r's label, time
  // and city codes sit in the band from TZ_ROW_TOP + r * TZ_ROW_H to the
  // next row's.
  void selectTimezoneRowAt(int vy) {
    if (_tz_editing) return;  // in the editor a long-press saves instead
    int r = (vy - TZ_ROW_TOP) / TZ_ROW_H;
    if (vy < TZ_ROW_TOP) r = 0;
    if (r > 2) r = 2;
    _tz_sel = (uint8_t)r;
  }
  void cancelEditing() { 
    if (_editing_utc) {
      _node_prefs->utc_offset_hours = _saved_utc_offset;
      _editing_utc = false;
    }
    _tz_editing = false;  // Timezones zone editor: discard the unsaved value
  }

  void poll() override {
    if (_shutdown_init && millis() >= _shutdown_at && !_task->isButtonPressed()) {
      if (_poweroff_selected) _task->setFullPowerOff(true);
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
#if defined(LilyGo_TDeck_Pro)
    _task->setHomeShowingTiles(false);  // Reset — only set true on FIRST page
#endif

    // Power off: full-screen message, no header
    // First render: "powering off..." + wake instruction
    // Second render onward: wake instruction only (persists on e-ink)
    if (_shutdown_init && _poweroff_selected) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (!_poweroff_msg_shown) {
        _poweroff_msg_shown = true;
        display.drawTextCentered(display.width() / 2, 30, MECK_TR("powering off...", "extinction..."));
        display.drawTextCentered(display.width() / 2, 46, MECK_TR("plug in USB-C to turn on", "USB-C pour rallumer"));
        return 1500;
      } else {
        display.drawTextCentered(display.width() / 2, 38, MECK_TR("plug in USB-C to turn on", "USB-C pour rallumer"));
        return 5000;
      }
    }

    // node name (tinyfont to avoid overlapping clock)
    display.setTextSize(_node_prefs->smallTextSize());
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    #define HOME_HDR_Y -3
    display.setCursor(0, HOME_HDR_Y);
    display.print(filtered_name);
    // battery voltage + status icons
#ifdef MECK_AUDIO_VARIANT
    int battLeftX = display.width(); // default if battery doesn't render
    renderBatteryIndicator(display, _task->getBattMilliVolts(), &battLeftX);

    // audio background playback indicator (>> icon next to battery)
    renderAudioIndicator(display, battLeftX);

    // alarm enabled indicator (AL icon, left of audio or battery)
    renderAlarmIndicator(display, battLeftX);
#else
    renderBatteryIndicator(display, _task->getBattMilliVolts());
#endif

    // centered clock — only show when time is valid
    {
      uint32_t now = _rtc->getCurrentTime();
      if (now > 1700000000) {  // valid timestamp (after ~Nov 2023)
        // Apply UTC offset from prefs
        int32_t local = (int32_t)now + ((int32_t)_node_prefs->utc_offset_hours * 3600);
        int hrs = (local / 3600) % 24;
        if (hrs < 0) hrs += 24;
        int mins = (local / 60) % 60;
        if (mins < 0) mins += 60;

        char timeBuf[6];
        sprintf(timeBuf, "%02d:%02d", hrs, mins);

        display.setTextSize(_node_prefs->smallTextSize());
        display.setColor(DisplayDriver::LIGHT);
        uint16_t tw = display.getTextWidth(timeBuf);
        int clockX = (display.width() - tw) / 2;
        // Ensure clock doesn't overlap the node name
        int nameRight = display.getTextWidth(filtered_name) + 4;
        if (clockX < nameRight) clockX = nameRight;
        display.setCursor(clockX, HOME_HDR_Y);
        display.print(timeBuf);
        display.setTextSize(1);  // restore
      }
    }
    // curr page indicator
#if defined(LilyGo_TDeck_Pro)
    int y = 8;   // Tighter under header; frees room for the MSG strip above the tile grid
#else
    int y = 14;
#endif
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

    if (_page == HomePage::FIRST) {
#if defined(LilyGo_TDeck_Pro)
      _task->setHomeShowingTiles(true);
#endif
#if defined(LilyGo_TDeck_Pro)
      // ----- T-Deck Pro: Touch tile grid home screen (T-Watch / P4 style) -----
      // Rendered in RAW PHYSICAL pixels (240x320) via GxEPDDisplay raw
      // helpers, bypassing the 128x128 virtual scaling so icons stay 1:1
      // and the dithered grey borders read as continuous bands.
      // Border shades approximate greys with 1-bit dither (0 = solid,
      // 1 = dark grey, 2 = light grey), cycling so no row pairs two
      // identical shades and no column stacks the same shade.
      // Layout must stay in sync with the touch hit-test in main.cpp
      // mapTouchTap().
      {
        GxEPDDisplay* eink = static_cast<GxEPDDisplay*>(&display);
        const uint16_t fg = eink->rawFgColor();
        // label_fr: French (Experimental Features > Language)
        struct MaxTile { const uint8_t* icon; const char* label; const char* label_fr; uint8_t shade; };
        static const MaxTile tiles[12] = {
          { icon_envelope,   "Messages",   "Messages",     0 }, { icon_people,  "Contacts", "Contacts",  1 },
          { icon_gear,       "Settings",   "Param\xC3\xA8tres", 2 }, { icon_search,  "Discover", "Recherche", 0 },
          { icon_trace,      "Trace",      "Trace",        1 }, { icon_map,     "Maps",     "Cartes",    2 },
          { icon_notepad,    "Notes",      "Notes",        0 }, { icon_book,    "Reader",   "Lecteur",   1 },
          { icon_headphones, "Audiobooks", "Livres audio", 2 }, { icon_bell,    "Alarm",    "Alarme",    0 },
          { icon_globe,      "Browser",    "Navigateur",   1 }, { icon_gamepad, "Games",    "Jeux",      2 },
        };
        // Physical layout matching the approved mockup: 2 cols x 6 rows
        // plus a full-width Phone tile, icon stacked above the label.
        // Labels honour the user font style (Classic 5x7 / Noto 7pt /
        // Montserrat 7pt) via drawTextRawStyled.
        const int tileW = 111, tileH = 32, gapX = 5, gapY = 3;
        const int gridX = 6;
        const int gridY = 68;   // below the status strip at y=46 (page dots render at physical ~30-38 with the Max dot row at virtual y=8)
        const int radius = 6;
        const int borderT = 3;  // 3px band so the dither reads as grey

        for (int i = 0; i < 12; i++) {
          int row = i / 2, col = i % 2;
          int tx = gridX + col * (tileW + gapX);
          int ty = gridY + row * (tileH + gapY);
          eink->drawRoundRectShadedRaw(tx, ty, tileW, tileH, radius, borderT, tiles[i].shade, fg);
          eink->drawXbmRaw(tx + (tileW - HOME_ICON_W) / 2, ty + 5, tiles[i].icon, HOME_ICON_W, HOME_ICON_H, fg);
          const char* label = MECK_TR(tiles[i].label, tiles[i].label_fr);
          int lw = eink->measureTextRawStyled(label);
          eink->drawTextRawStyled(tx + (tileW - lw) / 2, ty + 17, label, fg);
#ifdef MECK_40MHZ_TEST
          // 40 MHz test build: Maps(5), Audiobooks(8), Alarm(9) and Browser(10)
          // are gated -- knock out every other pixel so the tile reads greyed.
          // Taps on these tiles are inert (see mapTouchTap in main.cpp).
          if (i == 5 || i == 8 || i == 9 || i == 10) {
            const uint16_t bg = (fg == GxEPD_BLACK) ? GxEPD_WHITE : GxEPD_BLACK;
            for (int py = ty; py < ty + tileH; py++) {
              for (int px = tx; px < tx + tileW; px++) {
                if (((px + py) & 1) == 0) eink->drawPixelRaw(px, py, bg);
              }
            }
          }
#endif
        }

        // Full-width Phone tile (row 6) -- omitted on Pro audio builds
        // (MECK_AUDIO_VARIANT without HAS_4G_MODEM): no modem, and the
        // tile's tap was already inert on those builds (see mapTouchTap).
        #if !defined(MECK_AUDIO_VARIANT) || defined(HAS_4G_MODEM)
        {
          int ty = gridY + 6 * (tileH + gapY);
          int tw = tileW * 2 + gapX;
          eink->drawRoundRectShadedRaw(gridX, ty, tw, tileH, radius, borderT, 0, fg);
          eink->drawXbmRaw(gridX + (tw - HOME_ICON_W) / 2, ty + 5, icon_phone, HOME_ICON_W, HOME_ICON_H, fg);
          const char* phoneLabel = MECK_TR("Phone", "T\xC3\xA9l\xC3\xA9phone");
          int lw = eink->measureTextRawStyled(phoneLabel);
          eink->drawTextRawStyled(gridX + (tw - lw) / 2, ty + 17, phoneLabel, fg);
#ifdef MECK_40MHZ_TEST
          // Phone is gated on the 40 MHz test build -- grey it out too
          {
            const uint16_t bg = (fg == GxEPD_BLACK) ? GxEPD_WHITE : GxEPD_BLACK;
            for (int py = ty; py < ty + tileH; py++) {
              for (int px = gridX; px < gridX + tw; px++) {
                if (((px + py) & 1) == 0) eink->drawPixelRaw(px, py, bg);
              }
            }
          }
#endif
        }
        #endif  // Phone tile (Pro audio builds omit)

        // Top status strip (physical, between page dots and grid): unread
        // count plus connection state / WiFi IP / BLE pin
        {
          sprintf(tmp, "MSG: %d", _task->getUnreadMsgCount());
          char rightBuf[40];
          rightBuf[0] = 0;
      #if defined(BLE_PIN_CODE) || defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
          if (_task->hasConnection()) {
            strcpy(rightBuf, MECK_TR("< Connected >", "< Connect\xC3\xA9 >"));
          }
      #endif
      #if defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
          if (rightBuf[0] == 0) {
            IPAddress ip = WiFi.localIP();
            if (ip != IPAddress(0,0,0,0)) {
              snprintf(rightBuf, sizeof(rightBuf), "IP: %d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], TCP_PORT);
            }
          }
      #endif
      #if defined(BLE_PIN_CODE) && defined(MECK_WIFI_COMPANION)
          // Combined build: show the PIN only while Bluetooth is the connection
          if (rightBuf[0] == 0 && meckCompanionIsBLE() && _task->isSerialEnabled() && the_mesh.getBLEPin() != 0) {
            sprintf(rightBuf, "Pin:%d", the_mesh.getBLEPin());
          }
      #elif defined(BLE_PIN_CODE)
          if (rightBuf[0] == 0 && _task->isSerialEnabled() && the_mesh.getBLEPin() != 0) {
            sprintf(rightBuf, "Pin:%d", the_mesh.getBLEPin());
          }
      #endif
          int stripY = 46;
          int mw = eink->measureTextRawStyled(tmp);
          if (rightBuf[0]) {
            int rw = eink->measureTextRawStyled(rightBuf);
            int startX = (eink->rawWidth() - (mw + 12 + rw)) / 2;
            if (startX < 0) startX = 0;
            eink->drawTextRawStyled(startX, stripY, tmp, fg);
            eink->drawTextRawStyled(startX + mw + 12, stripY, rightBuf, fg);
          } else {
            eink->drawTextRawStyled((eink->rawWidth() - mw) / 2, stripY, tmp, fg);
          }
        }
        display.setTextSize(1);  // restore driver font state after raw text
      }
#else // not LilyGo_TDeck_Pro
      int y = 20;
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getUnreadMsgCount());
      display.drawTextCentered(display.width() / 2, y, tmp);
#if defined(LilyGo_TDeck_Pro_Max)
      y += 10;  // MAX: pull < Connected > up under MSG to make room for [T] Phone
#else
      y += 14;  // Reduced from 18
#endif

      #if defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
        IPAddress ip = WiFi.localIP();
        if (ip != IPAddress(0,0,0,0)) {
          snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d:%d", ip[0], ip[1], ip[2], ip[3], TCP_PORT);
          display.setTextSize(_node_prefs->smallTextSize());  // Tiny font for IP
          display.drawTextCentered(display.width() / 2, y, tmp);
          y += _node_prefs->smallLineH() - 1;
        }
      #endif
      #if defined(BLE_PIN_CODE) || defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(_node_prefs->smallTextSize());  // Tiny font for Connected
        display.drawTextCentered(display.width() / 2, y, "< Connected >");
        y += _node_prefs->smallLineH() - 1;
#ifdef BLE_PIN_CODE
      } else if (_task->isSerialEnabled() && the_mesh.getBLEPin() != 0) {
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, y, tmp);
#if defined(LilyGo_TDeck_Pro_Max)
        y += 6;  // MAX: tighter pin-to-menu gap so [T] Phone is not clipped
#else
        y += 18;
#endif
#endif
      }
      #endif

      // Keyboard shortcut menu
      // ----- T-Deck Pro: Keyboard shortcut text menu -----
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(_node_prefs->smallTextSize());
      int menuLH = _node_prefs->smallLineH();

      if (_node_prefs->large_font || display.getFontStyle() > 0) {
        // Proportional font: two-column layout with fixed X positions
        y += 2;
        int col1, col2;
        if (_node_prefs->large_font) {
          col1 = 2;
          int leftW = display.getTextWidth("[M] Messages");
          col2 = col1 + leftW + 3;
        } else {
          col1 = display.width() / 10;
          col2 = display.width() * 11 / 20;
        }

        display.setCursor(col1, y); display.print("[M] Messages");
        display.setCursor(col2, y); display.print("[C] Contacts");
        y += menuLH;
        display.setCursor(col1, y); display.print("[N] Notes");
        display.setCursor(col2, y); display.print("[S] Settings");
        y += menuLH;
#if HAS_GPS
        display.setCursor(col1, y); display.print("[E] Reader");
        display.setCursor(col2, y); display.print("[G] Maps");
#else
        display.setCursor(col1, y); display.print("[E] Reader");
#endif
        y += menuLH;
#if defined(HAS_4G_MODEM) && defined(MECK_AUDIO_VARIANT)
        display.setCursor(col1, y); display.print("[P] Audio");
        display.setCursor(col2, y); display.print("[K] Alarm");
        y += menuLH;
  #ifdef MECK_WEB_READER
        display.setCursor(col1, y); display.print("[B] Browser");
        display.setCursor(col2, y); display.print("[F] Discover");
  #else
        display.setCursor(col1, y); display.print("[F] Discover");
  #endif
        y += menuLH;
        display.setCursor(col1, y); display.print("[T] Phone");
#elif defined(HAS_4G_MODEM) && defined(MECK_WEB_READER)
        display.setCursor(col1, y); display.print("[T] Phone");
        display.setCursor(col2, y); display.print("[B] Browser");
#elif defined(HAS_4G_MODEM)
        display.setCursor(col1, y); display.print("[T] Phone");
        display.setCursor(col2, y); display.print("[F] Discover");
#elif defined(MECK_AUDIO_VARIANT)
        display.setCursor(col1, y); display.print("[P] Audio");
        display.setCursor(col2, y); display.print("[K] Alarm");
        y += menuLH;
  #ifdef MECK_WEB_READER
        display.setCursor(col1, y); display.print("[B] Browser");
        display.setCursor(col2, y); display.print("[F] Discover");
  #else
        display.setCursor(col1, y); display.print("[F] Discover");
  #endif
#elif defined(MECK_WEB_READER)
        display.setCursor(col1, y); display.print("[B] Browser");
#else
        display.setCursor(col1, y); display.print("[F] Discover");
#endif
        y += menuLH;
        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(col1, y); display.print("[R] Trace");
        display.setCursor(col2, y); display.print("[J] Games");
        display.setColor(DisplayDriver::LIGHT);
        y += menuLH;
        y += 2;
      } else {
        // Monospaced built-in font (Classic): centered space-padded strings
#if defined(LilyGo_TDeck_Pro_Max)
        y += 2;  // MAX: Press sits closer under < Connected >
#else
        y += 6;
#endif
        display.drawTextCentered(display.width() / 2, y, "Press:");
        y += 12;
        display.drawTextCentered(display.width() / 2, y, "[M] Messages    [C] Contacts  ");
        y += 10;
        display.drawTextCentered(display.width() / 2, y, "[N] Notes       [S] Settings  ");
        y += 10;
#if HAS_GPS
        display.drawTextCentered(display.width() / 2, y, "[E] Reader      [G] Maps      ");
#else
        display.drawTextCentered(display.width() / 2, y, "[E] Reader                    ");
#endif
        y += 10;
#if defined(HAS_4G_MODEM) && defined(MECK_AUDIO_VARIANT)
        display.drawTextCentered(display.width() / 2, y, "[P] Audiobooks  [K] Alarm     ");
        y += 10;
  #ifdef MECK_WEB_READER
        display.drawTextCentered(display.width() / 2, y, "[B] Browser     [F] Discover  ");
  #else
        display.drawTextCentered(display.width() / 2, y, "[F] Discover                  ");
  #endif
#elif defined(HAS_4G_MODEM) && defined(MECK_WEB_READER)
        display.drawTextCentered(display.width() / 2, y, "[T] Phone       [B] Browser   ");
#elif defined(HAS_4G_MODEM)
        display.drawTextCentered(display.width() / 2, y, "[T] Phone       [F] Discover  ");
#elif defined(MECK_AUDIO_VARIANT)
        display.drawTextCentered(display.width() / 2, y, "[P] Audiobooks  [K] Alarm     ");
        y += 10;
  #ifdef MECK_WEB_READER
        display.drawTextCentered(display.width() / 2, y, "[B] Browser     [F] Discover  ");
  #else
        display.drawTextCentered(display.width() / 2, y, "[F] Discover                  ");
  #endif
#elif defined(MECK_WEB_READER)
        display.drawTextCentered(display.width() / 2, y, "[B] Browser                   ");
#else
        display.drawTextCentered(display.width() / 2, y, "[F] Discover                  ");
#endif
        y += 10;
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(display.width() / 2, y, "[R] Trace       [J] Games     ");
        display.setColor(DisplayDriver::LIGHT);
        y += 10;
#if defined(HAS_4G_MODEM) && defined(MECK_AUDIO_VARIANT)
        // Phone on its own line below Trace/Games, centered like the "Press:" header
        display.drawTextCentered(display.width() / 2, y, "[T] Phone");
        y += 14;
#endif
      }

      // Nav hint (only if room)
      if (y < display.height() - 14) {
        display.setColor(DisplayDriver::GREEN);
        display.drawTextCentered(display.width() / 2, y,
          (_node_prefs->large_font || display.getFontStyle() > 0) ? "A/D: cycle views" : "Press A/D to cycle home views");
      }
      display.setTextSize(1);  // restore
#endif // not LilyGo_TDeck_Pro
    } else if (_page == HomePage::RECENT) {
      the_mesh.getRecentlyHeard(recent, UI_RECENT_LIST_SIZE);
      display.setColor(DisplayDriver::GREEN);
      int y = 20;
      for (int i = 0; i < UI_RECENT_LIST_SIZE; i++, y += 11) {
        auto a = &recent[i];
        if (a->name[0] == 0) continue;  // empty slot
        int secs = _rtc->getCurrentTime() - a->recv_timestamp;
        if (secs < 60) {
          sprintf(tmp, "%ds", secs);
        } else if (secs < 60*60) {
          sprintf(tmp, MECK_TR("%dm", "%dmin"), secs / 60);
        } else {
          sprintf(tmp, "%dh", secs / (60*60));
        }
        
        int timestamp_width = display.getTextWidth(tmp);
        int max_name_width = display.width() - timestamp_width - 1 - EINK_X_OFFSET;
        
        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1 - EINK_X_OFFSET, y);
        display.print(tmp);
      }
      // Hint for full Last Heard screen
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(_node_prefs->smallTextSize());
      display.drawTextCentered(display.width() / 2, display.height() - 24,
                               MECK_TR("H: Full Last Heard list", "H: Liste compl\xC3\xA8" "te"));
    } else if (_page == HomePage::RADIO) {
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(1);
      // freq / sf
      display.setCursor(0, 20);
      sprintf(tmp, "FQ: %06.3f   SF: %d", _node_prefs->freq, _node_prefs->sf);
      display.print(tmp);

      display.setCursor(0, 31);
      sprintf(tmp, "BW: %03.2f     CR: %d", _node_prefs->bw, _node_prefs->cr);
      display.print(tmp);

      // tx power,  noise floor
      display.setCursor(0, 42);
      sprintf(tmp, "TX: %ddBm", _node_prefs->tx_power_dbm);
      display.print(tmp);
      display.setCursor(0, 53);
      sprintf(tmp, MECK_TR("Noise floor: %d", "Bruit de fond : %d"), radio_driver.getNoiseFloor());
      display.print(tmp);
      display.setCursor(0, 64);
      sprintf(tmp, MECK_TR("RX packets: %u", "Paquets RX : %u"), (unsigned)the_mesh.getRxPacketCount());
      display.print(tmp);
#if defined(BLE_PIN_CODE) && defined(MECK_WIFI_COMPANION)
    } else if (_page == HomePage::BLUETOOTH) {
      // Combined build: Bluetooth counts as on only while it is the
      // connection, not while WiFi is
      bool btOn = meckCompanionIsBLE() && _task->isSerialEnabled();
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18,
          btOn ? bluetooth_on : bluetooth_off,
          32, 32);
      if (btOn && _task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 53, MECK_TR("< Connected >", "< Connect\xC3\xA9" " >"));
      } else if (btOn && the_mesh.getBLEPin() != 0) {
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 53, tmp);
      }
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 68, MECK_TR("toggle: " PRESS_LABEL, "basculer : appui long"));
      display.drawTextCentered(display.width() / 2, 78, MECK_TR("or press Enter key", "ou touche Entr\xC3\xA9" "e"));
#elif defined(BLE_PIN_CODE)
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, 53, MECK_TR("< Connected >", "< Connect\xC3\xA9" " >"));
      } else if (_task->isSerialEnabled() && the_mesh.getBLEPin() != 0) {
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
        display.drawTextCentered(display.width() / 2, 53, tmp);
      }
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 68, MECK_TR("toggle: " PRESS_LABEL, "basculer : appui long"));
      display.drawTextCentered(display.width() / 2, 78, MECK_TR("or press Enter key", "ou touche Entr\xC3\xA9" "e"));
#endif
#ifdef MECK_WIFI_COMPANION
    } else if (_page == HomePage::WIFI_STATUS) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 18, MECK_TR("WiFi Companion", "Compagnon WiFi"));

      int wy = 36;
      display.setTextSize(_node_prefs->smallTextSize());
      int wLH = _node_prefs->smallLineH() + 1;
      if (WiFi.status() == WL_CONNECTED) {
        display.setColor(DisplayDriver::GREEN);
        snprintf(tmp, sizeof(tmp), "SSID: %s", WiFi.SSID().c_str());
        display.drawTextCentered(display.width() / 2, wy, tmp);
        wy += wLH;
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.drawTextCentered(display.width() / 2, wy, tmp);
        wy += wLH;
        snprintf(tmp, sizeof(tmp), "Port: %d", TCP_PORT);
        display.drawTextCentered(display.width() / 2, wy, tmp);
        wy += wLH + 2;
        if (_task->hasConnection()) {
          display.setColor(DisplayDriver::GREEN);
          display.setTextSize(1);
          display.drawTextCentered(display.width() / 2, wy, MECK_TR("< App Connected >", "< Appli connect\xC3\xA9" "e >"));
        } else {
          display.setColor(DisplayDriver::YELLOW);
          display.setTextSize(1);
          display.drawTextCentered(display.width() / 2, wy, MECK_TR("Waiting for app...", "Attente de l'appli..."));
        }
      } else {
        display.setColor(DisplayDriver::RED);
        display.drawTextCentered(display.width() / 2, wy, MECK_TR("Not connected", "Non connect\xC3\xA9"));
        wy += wLH + 2;
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, wy, MECK_TR("Configure in Settings", "Configurer dans Param\xC3\xA8tres"));
      }
      // Enter (or long press) turns the WiFi radio on or off, as the Settings
      // "WiFi Radio" row does. Tiny size on every setting: at 9pt the line is
      // wider than the screen in Montserrat (130 of 128).
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(0);
      display.drawTextCentered(display.width() / 2, 90,
          (WiFi.getMode() != WIFI_OFF)
            ? MECK_TR("Press Enter to Turn Off Wifi", "Entr\xC3\xA9" "e : couper le WiFi")
            : MECK_TR("Press Enter to Turn On Wifi", "Entr\xC3\xA9" "e : activer le WiFi"));
      display.setTextSize(1);
#endif
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.drawTextCentered(display.width() / 2, 57, MECK_TR("advert: " PRESS_LABEL, "annonce : appui long"));
      display.drawTextCentered(display.width() / 2, 67, MECK_TR("or press Enter key", "ou touche Entr\xC3\xA9" "e"));
#if ENV_INCLUDE_GPS == 1 && !defined(MECK_40MHZ_TEST)
    } else if (_page == HomePage::GPS) {
      extern GPSStreamCounter gpsStream;
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;

      // GPS state line
      if (!_node_prefs->gps_enabled) {
        strcpy(buf, MECK_TR("gps off", "gps \xC3\xA9" "teint"));
      } else {
        strcpy(buf, MECK_TR("gps on", "gps allum\xC3\xA9"));
      }
      display.drawTextLeftAlign(0, y, buf);

      if (nmea == NULL) {
        y = y + 12;
        display.drawTextLeftAlign(0, y, MECK_TR("Can't access GPS", "GPS inaccessible"));
      } else {
        strcpy(buf, nmea->isValid() ? "fix" : MECK_TR("no fix", "sans fix"));
        display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "sat");
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        y = y + 12;

        // NMEA sentence counter — confirms baud rate and data flow
        display.drawTextLeftAlign(0, y, MECK_TR("sentences", "phrases"));
        if (_node_prefs->gps_enabled) {
          uint16_t sps = gpsStream.getSentencesPerSec();
          uint32_t total = gpsStream.getSentenceCount();
          sprintf(buf, "%u/s (%lu)", sps, (unsigned long)total);
        } else {
          strcpy(buf, MECK_TR("hw off", "\xC3\xA9" "teint"));
        }
        display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        y = y + 12;

        display.drawTextLeftAlign(0, y, "pos");
        sprintf(buf, "%.4f %.4f", 
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "alt");
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        y = y + 12;
      }
      // Show RTC time and UTC offset on GPS page
      {
        uint32_t now = _rtc->getCurrentTime();
        if (now > 1700000000) {
          int32_t local = (int32_t)now + ((int32_t)_node_prefs->utc_offset_hours * 3600);
          int hrs = (local / 3600) % 24;
          if (hrs < 0) hrs += 24;
          int mins = (local / 60) % 60;
          if (mins < 0) mins += 60;
          display.drawTextLeftAlign(0, y, MECK_TR("time(U)", "heure(U)"));
          sprintf(buf, "%02d:%02d UTC%+d", hrs, mins, _node_prefs->utc_offset_hours);
          display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        } else {
          display.drawTextLeftAlign(0, y, MECK_TR("time(U)", "heure(U)"));
          display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, MECK_TR("no sync", "non synchro"));
        }
        y = y + 12;
      }

      // UTC offset editor overlay
      if (_editing_utc) {
        // Draw background box
        int bx = 4, by = 20, bw = display.width() - 8, bh = 40;
        display.setColor(DisplayDriver::DARK);
        display.fillRect(bx, by, bw, bh);
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(bx, by, bw, bh);

        // Show current offset value
        display.setTextSize(2);
        sprintf(buf, "UTC%+d", _node_prefs->utc_offset_hours);
        display.drawTextCentered(display.width() / 2, by + 4, buf);

        // Show controls hint
        display.setTextSize(_node_prefs->smallTextSize());
        display.drawTextCentered(display.width() / 2, by + bh - 10, MECK_TR("W/S:adj Enter:ok Q:cancel", "W/S:r\xC3\xA9gler Ent:ok Q:annuler"));
        display.setTextSize(1);
      }

#endif
#if UI_SENSORS_PAGE == 1
    } else if (_page == HomePage::SENSORS) {
      int y = 18;
      refresh_sensors();
      char buf[30];
      char name[30];
      LPPReader r(sensors_lpp.getBuffer(), sensors_lpp.getSize());

      for (int i = 0; i < sensors_scroll_offset; i++) {
        uint8_t channel, type;
        r.readHeader(channel, type);
        r.skipData(type);
      }

      for (int i = 0; i < (sensors_scroll?UI_RECENT_LIST_SIZE:sensors_nb); i++) {
        uint8_t channel, type;
        if (!r.readHeader(channel, type)) { // reached end, reset
          r.reset();
          r.readHeader(channel, type);
        }

        display.setCursor(0, y);
        float v;
        switch (type) {
          case LPP_GPS: // GPS
            float lat, lon, alt;
            r.readGPS(lat, lon, alt);
            strcpy(name, "gps"); sprintf(buf, "%.4f %.4f", lat, lon);
            break;
          case LPP_VOLTAGE:
            r.readVoltage(v);
            strcpy(name, "voltage"); sprintf(buf, "%6.2f", v);
            break;
          case LPP_CURRENT:
            r.readCurrent(v);
            strcpy(name, "current"); sprintf(buf, "%.3f", v);
            break;
          case LPP_TEMPERATURE:
            r.readTemperature(v);
            strcpy(name, "temperature"); sprintf(buf, "%.2f", v);
            break;
          case LPP_RELATIVE_HUMIDITY:
            r.readRelativeHumidity(v);
            strcpy(name, "humidity"); sprintf(buf, "%.2f", v);
            break;
          case LPP_BAROMETRIC_PRESSURE:
            r.readPressure(v);
            strcpy(name, "pressure"); sprintf(buf, "%.2f", v);
            break;
          case LPP_ALTITUDE:
            r.readAltitude(v);
            strcpy(name, "altitude"); sprintf(buf, "%.0f", v);
            break;
          case LPP_POWER:
            r.readPower(v);
            strcpy(name, "power"); sprintf(buf, "%6.2f", v);
            break;
          default:
            r.skipData(type);
            strcpy(name, "unk"); sprintf(buf, "");
        }
        display.setCursor(0, y);
        display.print(name);
        display.setCursor(
          display.width()-display.getTextWidth(buf)-1, y
        );
        display.print(buf);
        y = y + 12;
      }
      if (sensors_scroll) sensors_scroll_offset = (sensors_scroll_offset+1)%sensors_nb;
      else sensors_scroll_offset = 0;
#endif
#if HAS_BQ27220
    } else if (_page == HomePage::BATTERY) {
      char buf[30];
      int y = 18;

      // Title
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, y, MECK_TR("Battery Gauge", "Jauge batterie"));
      y += 12;

      display.setColor(DisplayDriver::LIGHT);

      // Time to empty
      uint16_t tte = board.getTimeToEmpty();
      display.drawTextLeftAlign(0, y, MECK_TR("remaining", "autonomie"));
      if (tte == 0xFFFF || tte == 0) {
        strcpy(buf, tte == 0 ? MECK_TR("depleted", "\xC3\xA9" "puis\xC3\xA9" "e") : MECK_TR("charging", "en charge"));
      } else if (tte >= 60) {
        sprintf(buf, MECK_TR("%dh %dm", "%dh %dmin"), tte / 60, tte % 60);
      } else {
        sprintf(buf, "%d min", tte);
      }
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Average current
      int16_t avgCur = board.getAvgCurrent();
      display.drawTextLeftAlign(0, y, MECK_TR("avg current", "courant moyen"));
      sprintf(buf, "%d mA", avgCur);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Average power
      int16_t avgPow = board.getAvgPower();
      display.drawTextLeftAlign(0, y, MECK_TR("avg power", "puissance moy."));
      sprintf(buf, "%d mW", avgPow);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Voltage (already available)
      uint16_t mv = board.getBattMilliVolts();
      display.drawTextLeftAlign(0, y, MECK_TR("voltage", "tension"));
      sprintf(buf, "%d.%03d V", mv / 1000, mv % 1000);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Remaining capacity (clamped to design capacity — gauge FCC may be
      // stale from factory defaults until a full charge cycle re-learns it)
      uint16_t remCap = board.getRemainingCapacity();
      uint16_t desCap = board.getDesignCapacity();
      if (desCap > 0 && remCap > desCap) remCap = desCap;
      display.drawTextLeftAlign(0, y, MECK_TR("remaining cap", "capacit\xC3\xA9" " rest."));
      sprintf(buf, "%d mAh", remCap);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Battery temperature
      int16_t battTemp = board.getBattTemperature();
      display.drawTextLeftAlign(0, y, MECK_TR("temperature", "temp\xC3\xA9" "rature"));
      sprintf(buf, "%d.%d C", battTemp / 10, abs(battTemp % 10));
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
#endif
    }
    else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, 34, MECK_TR("hibernating...", "mise en veille..."));
      } else if (_poweroff_confirm) {
        // Confirmation prompt for power off
        display.drawXbm((display.width() - 32) / 2, 20, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 56, MECK_TR("power off device?", "\xC3\xA9teindre l'appareil ?"));
        display.drawTextCentered(display.width() / 2, 66, MECK_TR("usb-c to wake", "usb-c pour r\xC3\xA9veiller"));
        display.drawTextCentered(display.width() / 2, 82, MECK_TR("Enter:yes  q:no", "Entr\xC3\xA9" "e:oui  q:non"));
      } else {
        // Menu: hibernate / power off
        display.drawXbm((display.width() - 32) / 2, 20, power_icon, 32, 32);
        const int y1 = 56, y2 = 68;
        char line1[48], line2[48];
#if defined(LilyGo_TDeck_Pro)
        snprintf(line1, sizeof(line1), MECK_TR("%shibernate: long press/Enter", "%sveille : appui long/Entr\xC3\xA9" "e"), _poweroff_selected ? " " : ">");
        snprintf(line2, sizeof(line2), MECK_TR("%spower off: long press/Enter", "%sarr\xC3\xAA" "t : appui long/Entr\xC3\xA9" "e"), _poweroff_selected ? ">" : " ");
#else
        snprintf(line1, sizeof(line1), "%shibernate: " PRESS_LABEL, _poweroff_selected ? " " : ">");
        snprintf(line2, sizeof(line2), "%spower off: " PRESS_LABEL, _poweroff_selected ? ">" : " ");
#endif
        display.drawTextCentered(display.width() / 2, y1, line1);
        display.drawTextCentered(display.width() / 2, y2, line2);
      }
    }
    else if (_page == HomePage::TIMEZONES) {
      renderTimezonesPage(display);
    }
    return (_editing_utc || _tz_editing) ? 700 : 5000;
  }

  bool handleInput(char c) override {
    // UTC offset editing mode - intercept all keys
    if (_editing_utc) {
      if (c == 'w' || c == KEY_PREV) {
        // Increment offset
        if (_node_prefs->utc_offset_hours < 14) {
          _node_prefs->utc_offset_hours++;
        }
        return true;
      }
      if (c == 's' || c == KEY_NEXT) {
        // Decrement offset
        if (_node_prefs->utc_offset_hours > -12) {
          _node_prefs->utc_offset_hours--;
        }
        return true;
      }
      if (c == KEY_ENTER) {
        // Save and exit
        Serial.printf("UTC offset saving: %d\n", _node_prefs->utc_offset_hours);
        the_mesh.savePrefs();
        _editing_utc = false;
        _task->showAlert(MECK_TR("UTC offset saved", "D\xC3\xA9" "calage UTC enregistr\xC3\xA9"), 800);
        Serial.println("UTC offset save complete");
        return true;
      }
      if (c == 'q' || c == 'u') {
        // Cancel - restore original value
        _node_prefs->utc_offset_hours = _saved_utc_offset;
        _editing_utc = false;
        return true;
      }
      return true;  // Consume all other keys while editing
    }

    // Timezones page: W/S row select, Enter to edit, and the zone editor's
    // keys (see handleTimezonesKey). Keys it does not use fall through to
    // the page cycling below.
    if (_page == HomePage::TIMEZONES && handleTimezonesKey(c)) {
      return true;
    }

    // SHUTDOWN page -- intercept up/down and Enter before page cycling
    if (_page == HomePage::SHUTDOWN) {
      if (_poweroff_confirm) {
        // Confirmation mode for power off
        if (c == KEY_ENTER) {
          _shutdown_init = true;
          _shutdown_at = millis() + 2500;  // extra time for two-phase e-ink update
          return true;
        }
        // Cancel: q, left, prev
        if (c == 'q' || c == KEY_LEFT || c == KEY_PREV) {
          _poweroff_confirm = false;
          return true;
        }
        return true;  // eat all other keys while confirming
      }
      // Up/down toggles between hibernate and power off
      // Only 'w'/'s' (keyboard) — KEY_NEXT/KEY_PREV fall through to page cycling
      // so touch swipes and taps can still navigate away from this page.
      if (c == 'w' || c == 's') {
        _poweroff_selected = !_poweroff_selected;
        return true;
      }
      if (c == KEY_ENTER) {
        if (_poweroff_selected) {
          _poweroff_confirm = true;
        } else {
          _shutdown_init = true;
          _shutdown_at = millis() + 900;
        }
        return true;
      }
      // Left/right fall through to page cycling below
    }

    if (c == KEY_LEFT || c == KEY_PREV || c == 'a') {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT || c == 'd') {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert(MECK_TR("Recent adverts", "Annonces r\xC3\xA9" "centes"), 800);
      }
      return true;
    }
#if defined(BLE_PIN_CODE) && defined(MECK_WIFI_COMPANION)
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      // Combined build: Bluetooth on turns WiFi off; Bluetooth off leaves
      // neither on. Once the Web Reader has freed Bluetooth, it needs a reboot.
      if (meckCompanionIsBLE()) {
        meckCompanionUseNone();
      } else if (!meckCompanionUseBLE()) {
        _task->showAlert(MECK_TR("Reboot for Bluetooth", "Red\xC3\xA9marrer (Bluetooth)"), 1500);
      }
      return true;
    }
#elif defined(BLE_PIN_CODE)
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isSerialEnabled()) {  // toggle Bluetooth on/off
        _task->disableSerial();
      } else {
        _task->enableSerial();
      }
      return true;
    }
#endif
#ifdef MECK_WIFI_COMPANION
    if (c == KEY_ENTER && _page == HomePage::WIFI_STATUS) {
      // WiFi radio on/off, as the Settings "WiFi Radio" row does (this is a
      // copy of that row's code). Network setup and password stay in Settings.
#if defined(BLE_PIN_CODE) && defined(MECK_WIFI_COMPANION)
      // Combined build: WiFi on turns Bluetooth off; WiFi off leaves
      // neither on
      extern bool meckCompanionIsWiFi();
      extern bool meckCompanionUseWiFi(bool connectSaved);
      if (meckCompanionIsWiFi()) {
        meckCompanionUseNone();
      } else if (!meckCompanionUseWiFi(true)) {
        _task->showAlert(MECK_TR("WiFi failed to start", "\xC3\x89" "chec du WiFi"), 1500);
      }
#else
      if (WiFi.getMode() != WIFI_OFF) {
        // Turn WiFi OFF
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
        Serial.println("Home: WiFi radio OFF");
      } else {
        // Turn WiFi ON -- reconnect using saved credentials
        WiFi.mode(WIFI_STA);
        if (SD.exists("/web/wifi.cfg")) {
          File f = SD.open("/web/wifi.cfg", FILE_READ);
          if (f) {
            String ssid = f.readStringUntil('\n'); ssid.trim();
            String pass = f.readStringUntil('\n'); pass.trim();
            f.close();
            digitalWrite(SDCARD_CS, HIGH);
            if (ssid.length() > 0) {
              WiFi.begin(ssid.c_str(), pass.c_str());
              unsigned long timeout = millis() + 8000;
              while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
                delay(100);
              }
              if (WiFi.status() == WL_CONNECTED) {
                Serial.printf("Home: WiFi ON, connected to %s\n", ssid.c_str());
              } else {
                Serial.println("Home: WiFi ON, but connection failed");
              }
            }
          } else {
            digitalWrite(SDCARD_CS, HIGH);
          }
        }
        Serial.println("Home: WiFi radio ON");
      }
#endif
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert(MECK_TR("Advert sent!", "Annonce envoy\xC3\xA9" "e !"), 1000);
      } else {
        _task->showAlert(MECK_TR("Advert failed..", "\xC3\x89" "chec de l'annonce.."), 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1 && !defined(MECK_40MHZ_TEST)
    if (c == KEY_ENTER && _page == HomePage::GPS) {
      _task->toggleGPS();
      return true;
    }
    if (c == 'u' && _page == HomePage::GPS) {
      _editing_utc = true;
      _saved_utc_offset = _node_prefs->utc_offset_hours;
      return true;
    }
#endif
#if UI_SENSORS_PAGE == 1
    if (c == KEY_ENTER && _page == HomePage::SENSORS) {
      _task->toggleGPS();
      next_sensors_refresh=0;
      return true;
    }
#endif
    return false;
  }
};

// MsgPreviewScreen removed — all platforms now use toast alerts for new messages

// ==========================================================================
// Lock Screen -- T-Deck Pro
// Big clock, battery %, unread message count.
// T-Deck Pro: Double-press boot button to lock/unlock. Touch+keyboard disabled.
// ==========================================================================

#if defined(LilyGo_TDeck_Pro)
class LockScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _node_prefs;

public:
  LockScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* node_prefs)
    : _task(task), _rtc(rtc), _node_prefs(node_prefs) {}

  int render(DisplayDriver& display) override {
    uint32_t now = _rtc->getCurrentTime();

    char timeBuf[6] = "--:--";
    if (now > 1700000000) {
      int32_t local = (int32_t)now + ((int32_t)_node_prefs->utc_offset_hours * 3600);
      int hrs = (local / 3600) % 24;
      if (hrs < 0) hrs += 24;
      int mins = (local / 60) % 60;
      if (mins < 0) mins += 60;
      sprintf(timeBuf, "%02d:%02d", hrs, mins);
    }

    // ---- Huge clock: HH:MM on one line ----
    display.setTextSize(5);  // T-Deck Pro: FreeSansBold12pt at GxEPD 2× scale
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 55, timeBuf);

    // ---- Battery + unread on one line ----
    display.setTextSize(1);
    {
      int pct = 0;
#if HAS_BQ27220
      pct = _task->getBatteryPercent();
#else
      uint16_t mv = _task->getBattMilliVolts();
      if (mv > 0) {
        pct = ((mv - 3000) * 100) / (4200 - 3000);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
      }
#endif

      int unread = _task->getUnreadMsgCount();
      char infoBuf[32];
      if (unread > 0) {
        sprintf(infoBuf, MECK_TR("%d%%  |  %d unread", "%d%%  |  %d non lus"), pct, unread);
      } else {
        sprintf(infoBuf, "%d%%", pct);
      }
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 108, infoBuf);
    }

#ifdef HAS_4G_MODEM
    // ---- Unread SMS / missed-call notification ----
    // Sourced from the SMS screen's own unread and unseen state, so it
    // persists across re-locks and reboots until the conversation or the
    // call log is actually opened.
    {
      SMSScreen* smsScr = _task->getSMSScreen();
      int sms = smsScr ? smsScr->getUnreadSmsCount() : 0;
      int missed = smsScr ? smsScr->getUnseenMissedCount() : 0;
      if (sms > 0 || missed > 0) {
        char notifBuf[32];
        if (sms > 0 && missed > 0) {
          sprintf(notifBuf, MECK_TR("SMS: %d  Missed: %d", "SMS : %d  Manqu\xC3\xA9s : %d"), sms, missed);
        } else if (sms > 0) {
          sprintf(notifBuf, "SMS: %d", sms);
        } else {
          sprintf(notifBuf, MECK_TR("Missed: %d", "Manqu\xC3\xA9s : %d"), missed);
        }
        display.setTextSize(1);
        display.setColor(DisplayDriver::GREEN);
        display.drawTextCentered(display.width() / 2, display.height() - 12, notifBuf);
      }
    }
#endif

    // ---- Unlock hint ----

    return 30000;
  }

  bool handleInput(char c) override {
    return false;
  }
};
#endif

void UITask::begin(DisplayDriver* display, SensorManager* sensors, NodePrefs* node_prefs) {
  _display = display;
  _sensors = sensors;
  _auto_off = millis() + AUTO_OFF_MILLIS;

#if (defined(PIN_USER_BTN))
  user_btn.begin();
#endif
#if defined(PIN_USER_BTN_ANA)
  analog_btn.begin();
#endif

  _node_prefs = node_prefs;

  // Initialize message dedup ring buffer
  memset(_dedup, 0, sizeof(_dedup));
  _dedupIdx = 0;

  // Allocate per-contact DM unread tracking (PSRAM if available)
#if defined(ESP32) && defined(BOARD_HAS_PSRAM)
  _dmUnread = (uint8_t*)ps_calloc(MAX_CONTACTS, sizeof(uint8_t));
#else
  _dmUnread = new uint8_t[MAX_CONTACTS]();
#endif

#if ENV_INCLUDE_GPS == 1
  // Apply GPS preferences from stored prefs
  if (_sensors != NULL && _node_prefs != NULL) {
    _sensors->setSettingValue("gps", _node_prefs->gps_enabled ? "1" : "0");
    if (_node_prefs->gps_interval > 0) {
      char interval_str[12];  // Max: 24 hours = 86400 seconds (5 digits + null)
      sprintf(interval_str, "%u", _node_prefs->gps_interval);
      _sensors->setSettingValue("gps_interval", interval_str);
    }
  }
#endif

  if (_display != NULL) {
    _display->turnOn();
  }

#ifdef PIN_BUZZER
  buzzer.begin();
  buzzer.quiet(_node_prefs->buzzer_quiet);
#endif

#ifdef PIN_VIBRATION
  vibration.begin();
#endif

  // Keyboard backlight for message flash notifications
#ifdef KB_BL_PIN
  pinMode(KB_BL_PIN, OUTPUT);
  digitalWrite(KB_BL_PIN, LOW);
#endif

#ifdef HAS_4G_MODEM
  // Sync ringtone enabled state to modem manager
  modemManager.setRingtoneEnabled(node_prefs->ringtone_enabled);
#endif

  ui_started_at = millis();
  _alert_expiry = 0;
#if defined(LilyGo_TDeck_Pro)
  _lastInputMillis = millis();
#endif

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  channel_screen = new ChannelScreen(this, &rtc_clock);
  ((ChannelScreen*)channel_screen)->setDMUnreadPtr(_dmUnread);
  channel_picker_screen = new ChannelPickerScreen(this);
  ((ChannelPickerScreen*)channel_picker_screen)->setChannelScreen((ChannelScreen*)channel_screen);
  contacts_screen = new ContactsScreen(this, &rtc_clock);
  ((ContactsScreen*)contacts_screen)->setDMUnreadPtr(_dmUnread);
  text_reader = new TextReaderScreen(this, node_prefs);
  notes_screen = new NotesScreen(this, node_prefs);
  settings_screen = new SettingsScreen(this, &rtc_clock, node_prefs);
  repeater_admin = nullptr;  // Lazy-initialized on first use to preserve heap for audio
  path_editor = nullptr;     // Lazy-initialized on first use from contacts screen
  discovery_screen = new DiscoveryScreen(this, &rtc_clock);
  last_heard_screen = new LastHeardScreen(&rtc_clock);
  rxlog_screen = new RxLogScreen(this, &rtc_clock);
  trace_screen = new TraceScreen(this, &rtc_clock);
  games_menu_screen = new GamesMenuScreen(this);
  snake_screen = new SnakeScreen(this, &rtc_clock);
  minesweeper_screen = new MinesweeperScreen(this);
#if defined(LilyGo_TDeck_Pro)
  gbc_screen = new GBCEmulatorScreen(this);
#endif
#if defined(LilyGo_TDeck_Pro)
  lock_screen = new LockScreen(this, &rtc_clock, node_prefs);
#endif
  audiobook_screen = nullptr;  // Created and assigned from main.cpp if audio hardware present
#ifdef MECK_AUDIO_VARIANT
  alarm_screen = nullptr;      // Created and assigned from main.cpp if audio hardware present
  voice_screen = nullptr;      // Created and assigned from main.cpp on first mic key press
#endif
#ifdef HAS_4G_MODEM
  sms_screen = new SMSScreen(this, node_prefs);
#endif
#if   HAS_GPS
  map_screen = new MapScreen(this);
#else
  map_screen = nullptr;
#endif

  // Apply saved dark mode preference (T-Deck Pro)
#if defined(LilyGo_TDeck_Pro)
  if (_node_prefs->dark_mode) {
    ::display.setDarkMode(true);
  }

  // Apply saved font style preference (Classic / Noto Sans / Montserrat)
  if (_node_prefs->ui_font_style > 0) {
    ::display.setFontStyle(_node_prefs->ui_font_style);
  }
#endif

  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
  _next_refresh = millis() + 100;  // trigger re-render to show updated text
}

void UITask::showBootHint(bool immediate) {
  if (immediate) {
    // Activate now — used when hint should overlay the current screen (e.g. onboarding)
    _hintActive = true;
    _hintExpiry = millis() + 8000;  // 8 seconds auto-dismiss
    _pendingBootHint = false;
    _next_refresh = millis() + 100;
    Serial.println("[UI] Boot hint activated (immediate)");
  } else {
    // Defer until after splash screen — actual activation happens in gotoHomeScreen()
    _pendingBootHint = true;
    Serial.println("[UI] Boot hint pending (will show after splash)");
  }
}

void UITask::dismissBootHint() {
  if (!_hintActive) return;
  _hintActive = false;
  _hintExpiry = 0;
  // Persist so hint never shows again
  if (_node_prefs) {
    _node_prefs->hint_shown = 1;
    the_mesh.savePrefs();
  }
  _next_refresh = millis() + 100;
  Serial.println("[UI] Boot hint dismissed");
}

void UITask::notify(UIEventType t) {
  // Per-channel notification gating: if the last message was from a
  // muted channel (or mentions-only without an @mention), suppress
  // buzzer and vibration.  Ack events are never suppressed.
  if (s_lastMsgSuppressed && t != UIEventType::ack) {
    s_lastMsgSuppressed = false;  // Consume the flag
    return;
  }
  s_lastMsgSuppressed = false;

#if defined(PIN_BUZZER)
switch(t){
  case UIEventType::contactMessage:
    // gemini's pick
    buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
    break;
  case UIEventType::channelMessage:
    buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
    break;
  case UIEventType::ack:
    buzzer.play("ack:d=32,o=8,b=120:c");
    break;
  case UIEventType::roomMessage:
  case UIEventType::newContactMessage:
  case UIEventType::none:
  default:
    break;
}
#endif

#ifdef PIN_VIBRATION
  // Trigger vibration for all UI events except none
  if (t != UIEventType::none) {
    vibration.trigger();
  }
#endif
}


void UITask::msgRead(int msgcount) {
  _msgcount = msgcount;
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount,
                    const uint8_t* path, int8_t snr, uint8_t scope_idx) {
  _msgcount = msgcount;

  // --- Dedup: suppress retry spam (same sender + text within 60s) ---
  uint32_t nameH = simpleHash(from_name);
  uint32_t textH = simpleHash(text);
  unsigned long now = millis();
  for (int i = 0; i < MSG_DEDUP_SIZE; i++) {
    if (_dedup[i].name_hash == nameH && _dedup[i].text_hash == textH &&
        (now - _dedup[i].millis) < MSG_DEDUP_WINDOW_MS) {
      // Duplicate — suppress UI notification but still queued for BLE sync
      Serial.println("[Dedup] Suppressed duplicate");
      return;
    }
  }
  // Record this message in the dedup ring
  _dedup[_dedupIdx].name_hash = nameH;
  _dedup[_dedupIdx].text_hash = textH;
  _dedup[_dedupIdx].millis = now;
  _dedupIdx = (_dedupIdx + 1) % MSG_DEDUP_SIZE;

  // Determine channel index by looking up the channel name
  // For channel messages, from_name is the channel name
  // For contact messages, from_name is the contact name (channel_idx = 0xFF)
  uint8_t channel_idx = 0xFF;  // Default: unknown/contact message
  for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (the_mesh.getChannel(i, ch) && strcmp(ch.name, from_name) == 0) {
      channel_idx = i;
      break;
    }
  }

  // --- Per-channel notification preference check ---
  // Determines whether to suppress toast, buzzer, keyboard flash, vibration,
  // display wake, and unread counter for this message.  Messages are ALWAYS
  // stored in history regardless -- only alerts and unread badges are gated.
  bool suppressNotif = false;
  {
    int notifSlot = (channel_idx == 0xFF) ? MAX_GROUP_CHANNELS : (int)channel_idx;
    if (notifSlot >= 0 && notifSlot < (int)sizeof(_node_prefs->channel_notif)) {
      uint8_t pref = _node_prefs->channel_notif[notifSlot];
      if (pref == NOTIF_NONE) {
        suppressNotif = true;
      } else if (pref == NOTIF_MENTIONS) {
        // Check for @nodename or @[nodename] in message text (case-insensitive).
        // MeshCore companion app sends mentions as @[node name] with brackets.
        suppressNotif = true;  // Suppress unless mention found
        if (_node_prefs->node_name[0] != '\0') {
          char tagPlain[36];
          char tagBracket[38];
          snprintf(tagPlain, sizeof(tagPlain), "@%s", _node_prefs->node_name);
          snprintf(tagBracket, sizeof(tagBracket), "@[%s]", _node_prefs->node_name);
          int lenPlain = strlen(tagPlain);
          int lenBracket = strlen(tagBracket);
          const char* p = text;
          while (*p) {
            if (strncasecmp(p, tagBracket, lenBracket) == 0 ||
                strncasecmp(p, tagPlain, lenPlain) == 0) {
              suppressNotif = false;  // Mentioned -- notify
              break;
            }
            p++;
          }
        }
      }
    }
  }
  // Set the flag for notify() which is called immediately after newMsg().
  // If a custom notification tone is assigned and notifications are active,
  // request MP3 playback and suppress the RTTTL buzzer so they don't overlap.
#if defined(LilyGo_TDeck_Pro_Max)
  // Channel set to "Buzzer (vibrate)": pulse the motor instead of a tone.
  if (!suppressNotif && notifSounds.isVibrateForChannel(channel_idx)) {
    static DRV2605Haptic s_haptic;
    static bool s_hapticReady = false;
    if (!s_hapticReady) {
      board.motorEnable();
      delay(10);                 // let the motor rail settle before I2C
      s_hapticReady = s_haptic.begin();
    }
    if (s_hapticReady) s_haptic.buzz(14);  // 14 = Strong Buzz ~1s (effect 1 strong click was too brief)
    s_lastMsgSuppressed = true;  // suppress the default RTTTL buzzer
  } else
#endif
  {
  #ifdef MECK_AUDIO_VARIANT
  if (!suppressNotif) {
  #ifdef MECK_40MHZ_TEST
    const char* customSound = NULL;  // MP3 tones gated -- default buzzer path runs
  #else
    const char* customSound = notifSounds.getSoundForChannel(channel_idx);
  #endif
    if (customSound && customSound[0] != '\0') {
      char soundPath[48];
      snprintf(soundPath, sizeof(soundPath), "/alarms/%s", customSound);
      notifSounds.requestPlay(soundPath);
      s_lastMsgSuppressed = true;  // Suppress buzzer -- MP3 replaces it
    } else {
      s_lastMsgSuppressed = suppressNotif;
    }
  } else {
    s_lastMsgSuppressed = suppressNotif;
  }
  #elif defined(HAS_4G_MODEM)
  if (!suppressNotif) {
    const char* customSound = notifSounds.getSoundForChannel(channel_idx);
    if (customSound && customSound[0] != '\0') {
      int8_t toneIdx = ModemManager::findToneByName(customSound);
      if (toneIdx >= 0) {
        modemManager.requestNotifTone(toneIdx);
      }
      s_lastMsgSuppressed = true;  // Suppress buzzer -- modem tone replaces it
    } else {
      s_lastMsgSuppressed = suppressNotif;
    }
  } else {
    s_lastMsgSuppressed = suppressNotif;
  }
  #else
  s_lastMsgSuppressed = suppressNotif;
  #endif
  }

  // Add to channel history screen with channel index, path data, and SNR
  // For DMs (channel_idx == 0xFF):
  //   - Regular DMs: prefix text with sender name ("NodeName: hello")
  //   - Room server messages: text already contains "OriginalSender: message",
  //     don't double-prefix. Tag with room server name for conversation filtering.
  bool isRoomMsg = false;
  if (channel_idx == 0xFF) {
    // Check if sender is a room server
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo senderContact;
    for (uint32_t ci = 0; ci < numContacts; ci++) {
      if (the_mesh.getContactByIdx(ci, senderContact) && strcmp(senderContact.name, from_name) == 0) {
        if (senderContact.type == ADV_TYPE_ROOM) isRoomMsg = true;
        break;
      }
    }

    if (isRoomMsg) {
      // Room server: text already has "Poster: message" format — store as-is
      // Tag with room server name for conversation filtering
      ((ChannelScreen *) channel_screen)->addMessage(channel_idx, path_len, from_name, text, path, snr, from_name, suppressNotif);
    } else {
      // Regular DM: prefix with sender name
      char dmFormatted[CHANNEL_MSG_TEXT_LEN];
      snprintf(dmFormatted, sizeof(dmFormatted), "%s: %s", from_name, text);
      ((ChannelScreen *) channel_screen)->addMessage(channel_idx, path_len, from_name, dmFormatted, path, snr, nullptr, suppressNotif);
    }
  } else {
    ((ChannelScreen *) channel_screen)->addMessage(channel_idx, path_len, from_name, text, path, snr, nullptr, suppressNotif, scope_idx);
  }
  
  // If user is currently viewing this channel on the device, or companion
  // app is connected (they'll see it there), mark as read immediately
  if ((isOnChannelScreen() && 
      ((ChannelScreen *) channel_screen)->getViewChannelIdx() == channel_idx) ||
      hasConnection()) {
    ((ChannelScreen *) channel_screen)->markChannelRead(channel_idx);
  }

  // Per-contact DM unread tracking: find contact index by name
  // Skip increment when companion app is connected (user sees DMs there)
  if (channel_idx == 0xFF && _dmUnread && !hasConnection()) {
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo contact;
    for (uint32_t ci = 0; ci < numContacts; ci++) {
      if (the_mesh.getContactByIdx(ci, contact) && strcmp(contact.name, from_name) == 0) {
        if (_dmUnread[ci] < 255) _dmUnread[ci]++;
        break;
      }
    }
  }

  // Don't interrupt user with popup - just show brief notification
  // Messages are stored in channel history, accessible via tile/key
  // Suppress toasts for room server messages (bulk sync would spam toasts)
#if defined(LilyGo_TDeck_Pro)
  // No toasts over a running Game Boy game either: on a busy mesh they would
  // be constant. The header on that screen shows the unread count instead.
  // Snake and Minesweeper are other screens and keep their toasts.
  const bool gbcPlaying = isOnGBCScreen() && ((GBCEmulatorScreen*)gbc_screen)->isRunning();
#else
  const bool gbcPlaying = false;
#endif
  if (!isOnRepeaterAdmin() && !isRoomMsg && !suppressNotif && !gbcPlaying) {
    char alertBuf[40];
    snprintf(alertBuf, sizeof(alertBuf), MECK_TR("New: %s", "De : %s"), from_name);
    showAlert(alertBuf, 2000);
  }
  // Ensure picker badges update after toaster clears
  if (isOnChannelPickerScreen()) {
    forceRefresh();
  }

  if (_display != NULL && !suppressNotif) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    // Throttle refresh during room sync — batch messages instead of 648ms render per msg
    if (isRoomMsg) {
      unsigned long earliest = millis() + 3000;  // At most one refresh per 3s during sync
      if (_next_refresh < earliest) _next_refresh = earliest;
    } else {
      _next_refresh = 100;  // trigger refresh
    }
    }
  }

  // Keyboard flash notification (suppress for room sync and muted channels)
#ifdef KB_BL_PIN
  if (_node_prefs->kb_flash_notify && !isRoomMsg && !suppressNotif) {
    kbBacklightFlashWrite(true);
    _kb_flash_off_at = millis() + 200;  // 200ms flash
  }
#endif
}

void UITask::userLedHandler() {
#ifdef PIN_STATUS_LED
  int cur_time = millis();
  if (cur_time > next_led_change) {
    if (led_state == 0) {
      led_state = 1;
      if (_msgcount > 0) {
        last_led_increment = LED_ON_MSG_MILLIS;
      } else {
        last_led_increment = LED_ON_MILLIS;
      }
      next_led_change = cur_time + last_led_increment;
    } else {
      led_state = 0;
      next_led_change = cur_time + LED_CYCLE_MILLIS - last_led_increment;
    }
    digitalWrite(PIN_STATUS_LED, led_state == LED_STATE_ON);
  }
#endif
}

#ifndef EINK_FAST_FULL_EVERY
#define EINK_FAST_FULL_EVERY 20
#endif

void UITask::setCurrScreen(UIScreen* c) {
#if defined(LilyGo_TDeck_Pro) && defined(EINK_FASTLUT_EXPERIMENT)
  // Fast-waveform housekeeping: the register waveform pushes every pixel on
  // every partial and builds up ghosting and ink imbalance. After
  // EINK_FAST_FULL_EVERY fast partials, make the redraw that comes with this
  // screen change a full refresh, so the flash lands where a redraw is
  // happening anyway rather than mid-typing.
  if (_display != NULL) {
    GxEPDDisplay* gd = static_cast<GxEPDDisplay*>(_display);
    if (gd->fastPartialsSinceFull() >= EINK_FAST_FULL_EVERY) gd->requestFullRefresh();
  }
#endif
  curr = c;
  _alert_expiry = 0;  // Dismiss any active toast — prevents stale overlay from
                       // triggering extra 644ms e-ink refreshes on the new screen
  if (_hintActive) dismissBootHint();  // Dismiss hint when navigating away
  _next_refresh = 100;
}

/*
  hardware-agnostic pre-shutdown activity should be done here
*/
void UITask::shutdown(bool restart){

  #ifdef PIN_BUZZER
  /* note: we have a choice here -
     we can do a blocking buzzer.loop() with non-deterministic consequences
     or we can set a flag and delay the shutdown for a couple of seconds
     while a non-blocking buzzer.loop() plays out in UITask::loop()
  */
  buzzer.shutdown();
  uint32_t buzzer_timer = millis(); // fail-safe shutdown
  while (buzzer.isPlaying() && (millis() - 2500) < buzzer_timer)
    buzzer.loop();

  #endif // PIN_BUZZER

  if (restart) {
    _board->reboot();
  } else {
    // Disable BLE if active
    if (_serial != NULL && _serial->isEnabled()) {
      _serial->disable();
    }

    // Disable WiFi if active
    #if defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
    #endif

    // Disable 4G modem if active
    #ifdef HAS_4G_MODEM
      modemManager.shutdown();
    #endif

    // Disable GPS if active
    #if ENV_INCLUDE_GPS == 1
    {
      if (_sensors != NULL && _node_prefs != NULL && _node_prefs->gps_enabled) {
        _sensors->setSettingValue("gps", "0");
        #ifdef PIN_GPS_EN
          digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
        #endif
        #if defined(LilyGo_TDeck_Pro_Max)
          board.gpsPowerOff();  // MAX: GPS power is XL9555-routed, not PIN_GPS_EN
        #endif
      }
    }
    #endif

    // Power off LoRa radio, display, and board
    radio_driver.powerOff();
    _display->turnOff();

    // BQ25896 ship mode: disconnect battery from VSYS entirely.
    // Must happen BEFORE _board->powerOff() cuts PIN_PERF_POWERON
    // (I2C pull-ups need VDD3V3 to complete the transaction).
    // TI recommends: set BATFET_DLY=1 first, then BATFET_DIS=1 as
    // the last I2C write to avoid bricking the I2C state machine.
    // After tSM_DLY (~10-15s) the BATFET opens during deep sleep.
    // Wake: USB-C plug-in only (no reset button -- no power to ESP32).
    #if defined(LilyGo_TDeck_Pro_Max)
    if (_full_poweroff) {
      // MAX uses the SY6970 charger @ 0x6A (the BQ25896 @ 0x6B is absent --
      // I2C probe ACKs only at 0x6A). XPowersLib's SY6970 shutdown() closes the
      // battery path by setting REG09 bit 5 (BATFET_DIS) via read-modify-write,
      // with no BATFET_DLY. Mirror that exactly here so charger plug-in re-wakes
      // the board after a full power-off.
      Wire.beginTransmission(I2C_ADDR_SY6970);
      Wire.write(0x09);
      Wire.endTransmission(false);
      Wire.requestFrom((uint8_t)I2C_ADDR_SY6970, (uint8_t)1);
      uint8_t reg09 = Wire.read();

      Wire.beginTransmission(I2C_ADDR_SY6970);
      Wire.write(0x09);
      Wire.write(reg09 | 0x20);  // BATFET_DIS = bit 5
      Wire.endTransmission();
    }
    #elif defined(I2C_ADDR_BQ25896)
    if (_full_poweroff) {
      Wire.beginTransmission(I2C_ADDR_BQ25896);
      Wire.write(0x09);
      Wire.endTransmission(false);
      Wire.requestFrom((uint8_t)I2C_ADDR_BQ25896, (uint8_t)1);
      uint8_t reg09 = Wire.read();

      // Step 1: set BATFET_DLY=1 (bit 3) for safe I2C completion
      Wire.beginTransmission(I2C_ADDR_BQ25896);
      Wire.write(0x09);
      Wire.write(reg09 | 0x08);  // BATFET_DLY = bit 3
      Wire.endTransmission();

      // Step 2: set BATFET_DIS=1 (bit 5) -- MUST be the last I2C write
      Wire.beginTransmission(I2C_ADDR_BQ25896);
      Wire.write(0x09);
      Wire.write(reg09 | 0x28);  // BATFET_DIS (0x20) | BATFET_DLY (0x08)
      Wire.endTransmission();
    }
    #endif

    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#if (defined(PIN_USER_BTN))
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if (defined(PIN_USER_BTN))
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
#if defined(LilyGo_TDeck_Pro)
    // T-Deck Pro: single click ignored while locked — double-press to unlock
    if (_locked) {
      c = 0;
    } else {
      c = checkDisplayOn(KEY_NEXT);
    }
#else
    c = checkDisplayOn(KEY_NEXT);
#endif
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
    c = handleDoubleClick(KEY_PREV);
  } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#endif
#if defined(PIN_USER_BTN_ANA)
  if (abs(millis() - _analogue_pin_read_millis) > 10) {
    ev = analog_btn.check();
    if (ev == BUTTON_EVENT_CLICK) {
      c = checkDisplayOn(KEY_NEXT);
    } else if (ev == BUTTON_EVENT_LONG_PRESS) {
      c = handleLongPress(KEY_ENTER);
    } else if (ev == BUTTON_EVENT_DOUBLE_CLICK) {
      c = handleDoubleClick(KEY_PREV);
    } else if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
      c = handleTripleClick(KEY_SELECT);
    }
    _analogue_pin_read_millis = millis();
  }
#endif
#if defined(BACKLIGHT_BTN)
  if (millis() > next_backlight_btn_check) {
    bool touch_state = digitalRead(PIN_BUTTON2);
#if defined(DISP_BACKLIGHT)
    digitalWrite(DISP_BACKLIGHT, !touch_state);
#elif defined(EXP_PIN_BACKLIGHT)
    expander.digitalWrite(EXP_PIN_BACKLIGHT, !touch_state);
#endif
    next_backlight_btn_check = millis() + 300;
  }
#endif

  if (c != 0 && curr) {
    // Dismiss boot hint on any button input
    if (_hintActive) {
      dismissBootHint();
      c = 0;  // Consume the press
    }
  }

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
#if defined(LilyGo_TDeck_Pro)
    _lastInputMillis = millis();  // Reset auto-lock idle timer
#endif
  }

  userLedHandler();

  // Turn off keyboard flash after timeout
#ifdef KB_BL_PIN
  if (_kb_flash_off_at && millis() >= _kb_flash_off_at) {
  #ifdef HAS_4G_MODEM
    // Don't turn off LED if incoming call flash is active
    if (!_incomingCallRinging) {
      kbBacklightFlashWrite(false);
    }
  #else
    kbBacklightFlashWrite(false);
  #endif
    _kb_flash_off_at = 0;
  }
#endif

  // Incoming call LED flash — rapid repeated pulse while ringing
#if defined(HAS_4G_MODEM) && defined(KB_BL_PIN)
  {
    bool ringing = modemManager.isRinging();

    if (ringing && !_incomingCallRinging) {
      // Ringing just started
      _incomingCallRinging = true;
      _callFlashState = false;
      _nextCallFlash = 0;  // Start immediately

      // Wake display for incoming call
      if (_display != NULL && !_display->isOn()) {
        _display->turnOn();
      }
      _auto_off = millis() + 60000;  // Keep display on while ringing (60s)

    } else if (!ringing && _incomingCallRinging) {
      // Ringing stopped
      _incomingCallRinging = false;
      // Only turn off LED if message flash isn't also active
      if (!_kb_flash_off_at) {
        kbBacklightFlashWrite(false);
      }
      _callFlashState = false;
    }

    // Rapid LED flash while ringing (if kb_flash_notify is ON)
    if (_incomingCallRinging && _node_prefs->kb_flash_notify) {
      unsigned long now = millis();
      if (now >= _nextCallFlash) {
        _callFlashState = !_callFlashState;
        kbBacklightFlashWrite(_callFlashState);
        // 250ms on, 250ms off — fast pulse to distinguish from single msg flash
        _nextCallFlash = now + 250;
      }
      // Extend auto-off while ringing
      _auto_off = millis() + 60000;
    }
  }
#endif

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif


if (curr) curr->poll();


  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      // Defer display refresh while BLE is actively transferring contacts.
      // E-ink partial update blocks for ~820ms, stalling the BLE send queue
      // and adding ~1.6s of dead time to a full contact sync.
      if (_notifAudioActive) {
        _next_refresh = millis() + 200;  // Defer e-ink refresh during notif tone (SPI bus contention)
      } else if (_serial != NULL && _serial->hasPendingData()) {
        _next_refresh = millis() + 500;  // Re-check in 500ms
      } else {
      // Sync dark mode with prefs (settings toggle takes effect here)
#if defined(LilyGo_TDeck_Pro)
      if (_node_prefs && display.isDarkMode() != (_node_prefs->dark_mode != 0)) {
        display.setDarkMode(_node_prefs->dark_mode != 0);
      }
#endif
      // Sync font style with prefs (settings toggle takes effect here)
#if defined(LilyGo_TDeck_Pro)
      if (_node_prefs && display.getFontStyle() != _node_prefs->ui_font_style) {
        display.setFontStyle(_node_prefs->ui_font_style);
      }
#endif
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (_hintActive && millis() < _hintExpiry) {
        // Boot navigation hint overlay — multi-line, larger box
        _display->setTextSize(1);
        int w = _display->width();
        int h = _display->height();
        int boxX = w / 8;
        int boxY = h / 5;
        int boxW = w - boxX * 2;
        int boxH = h * 3 / 5;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(boxX, boxY, boxW, boxH);
        _display->setColor(DisplayDriver::LIGHT);
        _display->drawRect(boxX, boxY, boxW, boxH);
        int cx = w / 2;
        int lineH = 11;
        int startY = boxY + 6;
        _display->drawTextCentered(cx, startY, "M:Msgs  C:Contacts");
        _display->drawTextCentered(cx, startY + lineH, MECK_TR("S:Settings  E:Reader", "S:Options  E:Lecteur"));
        _display->drawTextCentered(cx, startY + lineH * 2, MECK_TR("N:Notes  W/S:Scroll", "N:Notes  W/S:D\xC3\xA9" "filer"));
        _display->drawTextCentered(cx, startY + lineH * 3, MECK_TR("A/D:Cycle Left/Right", "A/D:Gauche/Droite"));
        _display->drawTextCentered(cx, startY + lineH * 4 + 4, MECK_TR("[X to dismiss]", "[X pour fermer]"));
        _next_refresh = _hintExpiry;
      } else if (_hintActive) {
        // Hint expired — auto-dismiss
        dismissBootHint();
        _next_refresh = millis() + 200;
      } else if (millis() < _alert_expiry) {  // render alert popup
        _display->setTextSize(1);
        int y = _display->height() / 3;
        int p = _display->height() / 32;
        _display->setColor(DisplayDriver::DARK);
        _display->fillRect(p, y, _display->width() - p*2, y);
        _display->setColor(DisplayDriver::LIGHT);  // draw box border
        _display->drawRect(p, y, _display->width() - p*2, y);
        _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
        _next_refresh = _alert_expiry;   // will need refresh when alert is dismissed
      } else {
        _next_refresh = millis() + delay_millis;
      }
      _display->endFrame();

      // E-ink render throttle: enforce minimum interval between renders.
      // Partial update blocks for ~644ms; full refresh blocks for ~3000ms.
      // Without this floor, changing readings (battery, uptime) trigger
      // back-to-back renders that cause continuous flashing.
#ifdef EINK_FULL_REFRESH_ONLY
      unsigned long minNext = millis() + 300000;  // Full refresh: 5 min idle
#else
      unsigned long minNext = millis() + 800;   // Partial refresh: 800ms floor
#endif
#if defined(LilyGo_TDeck_Pro)
      // Game Boy emulator: no floor while a game runs. The picture changes
      // every frame, so the panel should refresh back-to-back at its own
      // partial-refresh rate rather than pause 800 ms between pictures.
      if (isOnGBCScreen() && ((GBCEmulatorScreen*)gbc_screen)->isRunning()) minNext = millis();
#endif
      if (_next_refresh < minNext) _next_refresh = minNext;

      // Toast dismissal must not be starved by the e-ink render throttle:
      // any render inside the alert window (e.g. a DM status push) re-draws
      // the overlay and the floor above can push the scheduled clearing
      // render well past _alert_expiry, leaving the toast on screen. If an
      // alert is still active and due to lapse before the next scheduled
      // render, wake at expiry so the clearing frame lands on time.
      if (_alert_expiry != 0 && millis() < _alert_expiry && _next_refresh > _alert_expiry) {
        _next_refresh = _alert_expiry;
      }
      }  // end else (not bulk syncing)
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

  // Auto-lock idle timer — runs regardless of display on/off state
#if defined(LilyGo_TDeck_Pro)
  if (_node_prefs && _node_prefs->auto_lock_minutes > 0 && !_locked) {
    uint8_t alm = _node_prefs->auto_lock_minutes;
    // Only act on valid option values (guards against garbage from uninitialised prefs)
    if (alm == 2 || alm == 5 || alm == 10 || alm == 15 || alm == 30) {
      unsigned long lock_timeout = (unsigned long)alm * 60000UL;
      if (millis() - _lastInputMillis >= lock_timeout) {
        lockScreen();
      }
    }
  }

  // Lock screen clock refresh — keeps the displayed time current.
  // T-Deck Pro: every 1 minute.
  // Wakes the display driver briefly to render, then auto-off handles it.
#if defined(LilyGo_TDeck_Pro)
  // T-Deck Pro: refresh every 1 minute
  if (_locked && _display != NULL) {
    const unsigned long LOCK_REFRESH_INTERVAL = 1UL * 60UL * 1000UL;  // 1 minute
#else
  if (_locked && _display != NULL) {
    const unsigned long LOCK_REFRESH_INTERVAL = 2UL * 60UL * 1000UL;  // 2 minutes
#endif
    if (millis() - _lastLockRefresh >= LOCK_REFRESH_INTERVAL) {
      _lastLockRefresh = millis();
      if (!_display->isOn()) {
        _display->turnOn();
        _auto_off = millis() + 5000;  // Stay on just long enough to render + settle
      }
      _next_refresh = 0;  // Trigger immediate render
    }
  }
#endif

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {
      _low_batt_count++;
      if (_low_batt_count >= 3) {  // 3 consecutive low readings (~24s) to avoid transient sags

      // show low battery shutdown alert on e-ink (persists after power loss)
      #if defined(LilyGo_TDeck_Pro)
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::RED);
        _display->drawTextCentered(_display->width() / 2, 20, MECK_TR("Low Battery.", "Batterie faible."));
        _display->drawTextCentered(_display->width() / 2, 40, MECK_TR("Shutting Down!", "Extinction !"));
        _display->endFrame();
      }
      #endif

      // Flush contacts before the battery dies -- the chunked lazy save may not
      // have run for hours. Blocking save (tmp-then-rename, brownout-safe).
      the_mesh.saveContacts();

      shutdown();
      }
    } else {
      _low_batt_count = 0;
    }
    next_batt_chck = millis() + 8000;
  }
#endif

#if defined(LilyGo_TDeck_Pro_Max)
  // TEMPORARY: charger + fuel gauge readout on serial every 10 s (read-only)
  {
    static unsigned long nextChargerDump = 0;
    if ((long)(millis() - nextChargerDump) >= 0) {
      nextChargerDump = millis() + 10000;
      board.chargerDebugPrint();
    }
  }
#endif
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
#if defined(LilyGo_TDeck_Pro)
    _lastInputMillis = millis();  // Reset auto-lock idle timer
#endif
  }
  return c;
}

char UITask::handleLongPress(char c) {
  if (millis() - ui_started_at < 8000) {   // long press in first 8 seconds since startup -> CLI/rescue
    the_mesh.enterCLIRescue();
    c = 0;   // consume event
  }
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
#if defined(LilyGo_TDeck_Pro)
  // Double-click boot button → lock/unlock screen
  if (_locked) {
    unlockScreen();
  } else {
    lockScreen();
  }
  c = 0;
#endif
  checkDisplayOn(c);
  return c;
}

char UITask::handleTripleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: triple click triggered");
  checkDisplayOn(c);
  toggleBuzzer();
  c = 0;
  return c;
}

#if defined(LilyGo_TDeck_Pro)
void UITask::lockScreen() {
  if (_locked) return;
  _locked = true;
  _screenBeforeLock = curr;
  setCurrScreen(lock_screen);
  // Ensure display is on so lock screen renders (auto-off may have turned it off)
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
#if defined(LilyGo_TDeck_Pro_Max)
  // MAX: IO41 frontlight stays lit otherwise, burning power behind the
  // lock screen. Plain Pro V1.1 has no working frontlight to switch off.
  if (board.isBacklightOn()) board.backlightOff();
#endif
  _next_refresh = 0;  // Draw lock screen immediately
  _auto_off = millis() + 60000;  // 60s before display off while locked
  _lastLockRefresh = millis();   // Start lock screen clock refresh cycle
  Serial.println("[UI] Screen locked — entering low-power mode");
}

void UITask::unlockScreen() {
  if (!_locked) return;
  _locked = false;
  if (_screenBeforeLock) {
    setCurrScreen(_screenBeforeLock);
  } else {
    gotoHomeScreen();
  }
  _screenBeforeLock = nullptr;
  // Ensure display is on so unlocked screen renders
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _lastInputMillis = millis();  // Reset auto-lock idle timer
  _next_refresh = 0;
  Serial.println("[UI] Screen unlocked — exiting low-power mode");
}
#endif // LilyGo_TDeck_Pro

bool UITask::getGPSState() {
  #if ENV_INCLUDE_GPS == 1
    return _node_prefs != NULL && _node_prefs->gps_enabled;
  #else
    return false;
  #endif
}

void UITask::toggleGPS() {
  #if ENV_INCLUDE_GPS == 1
    if (_sensors != NULL) {
      if (_node_prefs->gps_enabled) {
        // Disable GPS — cut hardware power
        _sensors->setSettingValue("gps", "0");
        _node_prefs->gps_enabled = 0;
        #ifdef PIN_GPS_EN
          digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
        #endif
        #if defined(LilyGo_TDeck_Pro_Max)
          board.gpsPowerOff();  // MAX: GPS power is XL9555-routed, not PIN_GPS_EN
        #endif
        notify(UIEventType::ack);
      } else {
#ifdef MECK_40MHZ_TEST
        // 40 MHz test build: GPS is gated -- refuse the enable, leave rail off
        showAlert(MECK_TR("GPS gated (40MHz build)", "GPS bloqu\xC3\xA9 (40 MHz)"), 1200);
        return;
#endif
        // Enable GPS — power on hardware
        _sensors->setSettingValue("gps", "1");
        _node_prefs->gps_enabled = 1;
        #ifdef PIN_GPS_EN
          digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);
        #endif
        #if defined(LilyGo_TDeck_Pro_Max)
          board.gpsPowerOn();  // MAX: GPS power is XL9555-routed, not PIN_GPS_EN
        #endif
        notify(UIEventType::ack);
      }
      the_mesh.savePrefs();
      showAlert(_node_prefs->gps_enabled ? MECK_TR("GPS: Enabled", "GPS : activ\xC3\xA9") : MECK_TR("GPS: Disabled", "GPS : d\xC3\xA9sactiv\xC3\xA9"), 800);
      _next_refresh = 0;
    }
  #endif
}

void UITask::toggleBuzzer() {
    // Toggle buzzer quiet mode
  #ifdef PIN_BUZZER
    if (buzzer.isQuiet()) {
      buzzer.quiet(false);
      notify(UIEventType::ack);
    } else {
      buzzer.quiet(true);
    }
    _node_prefs->buzzer_quiet = buzzer.isQuiet();
    the_mesh.savePrefs();
    showAlert(buzzer.isQuiet() ? MECK_TR("Buzzer: OFF", "Buzzer : coup\xC3\xA9") : MECK_TR("Buzzer: ON", "Buzzer : activ\xC3\xA9"), 800);
    _next_refresh = 0;  // trigger refresh
  #endif
}

void UITask::injectKey(char c) {
  if (c != 0 && curr) {
    // Turn on display if it's off
    if (_display != NULL && !_display->isOn()) {
      _display->turnOn();
    }
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
#if defined(LilyGo_TDeck_Pro)
    _lastInputMillis = millis();  // Reset auto-lock idle timer
#endif
    // Debounce refresh when editing UTC offset - e-ink takes 644ms per refresh
    // so don't queue another render until the current one could have finished
    if (isEditingHomeScreen()) {
      unsigned long earliest = millis() + 700;
      if (_next_refresh < earliest) {
        _next_refresh = earliest;
      }
    }
#ifdef EINK_FULL_REFRESH_ONLY
    // Full-refresh displays (SSD1681): debounce printable character input.
    // Compose typing (0x20-0x7E) pushes the render 2.5s into the future so
    // the user can type a whole word before a ~2.2s full refresh fires.
    // Navigation/special keys (arrows, enter, escape, etc.) refresh
    // immediately so scrolling and screen changes remain responsive.
    else if ((unsigned char)c >= 0x20 && (unsigned char)c <= 0x7E) {
      unsigned long earliest = millis() + 2500;
      if (_next_refresh < earliest) _next_refresh = earliest;
    } else {
      _next_refresh = 100;  // navigation key — refresh now
    }
#else
    else {
      _next_refresh = 100;  // trigger refresh
    }
#endif
  }
}

void UITask::gotoHomeScreen() {
  // Cancel any active editing state when navigating to home
  ((HomeScreen *) home)->cancelEditing();
  setCurrScreen(home);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;

  // Activate deferred boot hint now that home screen is visible
  if (_pendingBootHint) {
    _pendingBootHint = false;
    _hintActive = true;
    _hintExpiry = millis() + 8000;  // 8 seconds auto-dismiss
    _next_refresh = millis() + 100;
    Serial.println("[UI] Boot hint activated");
  }
}

bool UITask::isEditingHomeScreen() const {
  return curr == home && (((HomeScreen *) home)->isEditingUTC() ||
                          ((HomeScreen *) home)->isEditingTimezone());
}

bool UITask::isHomeOnRecentPage() const {
  return curr == home && ((HomeScreen *) home)->isOnRecentPage();
}

bool UITask::isHomeOnShutdownPage() const {
  return curr == home && ((HomeScreen *) home)->isOnShutdownPage();
}

bool UITask::isHomeOnTimezonesPage() const {
  return curr == home && ((HomeScreen *) home)->isOnTimezonesPage();
}

void UITask::selectHomeTimezoneRowAt(int vy) {
  if (curr == home) ((HomeScreen *) home)->selectTimezoneRowAt(vy);
}

void UITask::gotoChannelScreen(bool resetDmView) {
  ChannelScreen* cs = (ChannelScreen*)channel_screen;
  // If currently showing DM view, reset to channel 0 (unless caller opts out)
  if (resetDmView && cs->getViewChannelIdx() == 0xFF) {
    cs->setViewChannelIdx(0);
  }
  cs->resetScroll();
  // Mark the currently viewed channel as read
  cs->markChannelRead(cs->getViewChannelIdx());
  setCurrScreen(channel_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoChannelPickerScreen() {
  ChannelScreen* cs = (ChannelScreen*)channel_screen;
  ChannelPickerScreen* pick = (ChannelPickerScreen*)channel_picker_screen;
  pick->enter(cs->getViewChannelIdx());
  setCurrScreen(channel_picker_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}


void UITask::gotoDMTab() {
  ((ChannelScreen *) channel_screen)->setViewChannelIdx(0xFF);  // switches + marks read
  ((ChannelScreen *) channel_screen)->resetScroll();
  setCurrScreen(channel_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoDMConversation(const char* contactName, int contactIdx, uint8_t perms) {
  ChannelScreen* cs = (ChannelScreen*)channel_screen;
  cs->setViewChannelIdx(0xFF);  // enters inbox mode + marks read
  cs->openConversation(contactName, contactIdx, perms);  // switches to conversation mode
  cs->resetScroll();
  setCurrScreen(channel_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoContactsScreen() {
  ((ContactsScreen *) contacts_screen)->resetScroll();
  setCurrScreen(contacts_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoTextReader() {
  if (!text_reader) return;  // Not available on this platform
  TextReaderScreen* reader = (TextReaderScreen*)text_reader;
  if (_display != NULL) {
    reader->enter(*_display);
  }
  setCurrScreen(text_reader);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoNotesScreen() {
  if (!notes_screen) return;  // Not available on this platform
  NotesScreen* notes = (NotesScreen*)notes_screen;
  if (_display != NULL) {
    notes->enter(*_display);
  }
  // Set fresh timestamp and wire up time getter for note creation
  notes->setTimestamp(rtc_clock.getCurrentTime(),
                      _node_prefs ? _node_prefs->utc_offset_hours : 0);
  notes->setTimeGetter([]() -> uint32_t { return rtc_clock.getCurrentTime(); });
  setCurrScreen(notes_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoSettingsScreen() {
  ((SettingsScreen *) settings_screen)->enter();
  setCurrScreen(settings_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoOnboarding() {
  ((SettingsScreen *) settings_screen)->enterOnboarding();
  setCurrScreen(settings_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoAudiobookPlayer() {
#ifdef MECK_AUDIO_VARIANT
  if (audiobook_screen == nullptr) return;  // No audio hardware
  AudiobookPlayerScreen* abPlayer = (AudiobookPlayerScreen*)audiobook_screen;
  if (_display != NULL) {
    abPlayer->enter(*_display);
  }
  setCurrScreen(audiobook_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
#endif
}

#ifdef MECK_AUDIO_VARIANT
void UITask::gotoAlarmScreen() {
  if (alarm_screen == nullptr) return;
  AlarmScreen* alarmScr = (AlarmScreen*)alarm_screen;
  if (_display != NULL) {
    alarmScr->enter(*_display);
  }
  setCurrScreen(alarm_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoVoiceScreen() {
  if (voice_screen == nullptr) return;
  VoiceMessageScreen* voiceScr = (VoiceMessageScreen*)voice_screen;
  if (_display != NULL) {
    voiceScr->enter(*_display);
  }
  setCurrScreen(voice_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

#ifdef HAS_4G_MODEM
void UITask::gotoSMSScreen() {
  SMSScreen* smsScr = (SMSScreen*)sms_screen;
  // activate() resets the view to the app menu, which would wipe the ringing
  // or in-call screen that onCallEvent() has just put up. Skip it mid-call.
  if (!smsScr->isInCallView()) {
    smsScr->activate();
  }
  setCurrScreen(sms_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

uint8_t UITask::getChannelScreenViewIdx() const {
  return ((ChannelScreen *) channel_screen)->getViewChannelIdx();
}

int UITask::getUnreadMsgCount() const {
  return ((ChannelScreen *) channel_screen)->getTotalUnread();
}

void UITask::addSentChannelMessage(uint8_t channel_idx, const char* sender, const char* text) {
  // Format the message as "Sender: message"
  char formattedMsg[CHANNEL_MSG_TEXT_LEN];
  snprintf(formattedMsg, sizeof(formattedMsg), "%s: %s", sender, text);

  // Tag the bubble with this send's fingerprint so the "Heard by" overlay can
  // later match it to the repeat track (MyMesh::getHeardBy).
  uint8_t fp[12];  // SENT_FINGERPRINT_SIZE
  bool haveFp = the_mesh.getLastSentFingerprint(fp);

  // Add to channel history with path_len=0 (local message)
  ((ChannelScreen *) channel_screen)->addMessage(channel_idx, 0, sender, formattedMsg,
                                                  nullptr, 0, nullptr, false, 0xFF, 0, 0,
                                                  haveFp ? fp : nullptr);
}

void UITask::addSentDM(const char* recipientName, const char* sender, const char* text,
                       uint32_t send_ref, uint8_t send_total) {
  // Format as "Sender: message" and tag with recipient's peer hash
  char formattedMsg[CHANNEL_MSG_TEXT_LEN];
  snprintf(formattedMsg, sizeof(formattedMsg), "%s: %s", sender, text);
  ((ChannelScreen *) channel_screen)->addMessage(0xFF, 0, sender, formattedMsg,
                                                  nullptr, 0, recipientName,
                                                  false, 0xFF, send_ref, send_total);
}

void UITask::dmSendStatus(uint32_t send_ref, uint8_t status, uint8_t attempt, uint8_t total) {
  if (((ChannelScreen *) channel_screen)->setSendStatus(send_ref, status, attempt, total)) {
    // Repaint promptly if the user is looking at the conversation
    if (isOnChannelScreen()) forceRefresh();
  }
}

void UITask::markChannelReadFromBLE(uint8_t channel_idx) {
  ((ChannelScreen *) channel_screen)->markChannelRead(channel_idx);
  // If clearing DMs, also zero all per-contact DM counts
  if (channel_idx == 0xFF && _dmUnread) {
    memset(_dmUnread, 0, MAX_CONTACTS * sizeof(uint8_t));
  }
  // Trigger a refresh so the home screen unread count updates in real-time
  _next_refresh = millis() + 200;
}

void UITask::markAllChannelsRead() {
  ((ChannelScreen *) channel_screen)->markAllRead();
  if (_dmUnread) {
    memset(_dmUnread, 0, MAX_CONTACTS * sizeof(uint8_t));
  }
  _next_refresh = millis() + 200;
}

bool UITask::hasDMUnread(int contactIdx) const {
  if (!_dmUnread || contactIdx < 0 || contactIdx >= MAX_CONTACTS) return false;
  return _dmUnread[contactIdx] > 0;
}

int UITask::getDMUnreadCount(int contactIdx) const {
  if (!_dmUnread || contactIdx < 0 || contactIdx >= MAX_CONTACTS) return 0;
  return _dmUnread[contactIdx];
}

void UITask::clearDMUnread(int contactIdx) {
  if (!_dmUnread || contactIdx < 0 || contactIdx >= MAX_CONTACTS) return;
  int count = _dmUnread[contactIdx];
  if (count > 0) {
    _dmUnread[contactIdx] = 0;
    ((ChannelScreen *) channel_screen)->subtractDMUnread(count);
    _next_refresh = millis() + 200;
  }
}

void UITask::gotoRepeaterAdmin(int contactIdx) {
  // Lazy-initialize on first use (same pattern as audiobook player)
  if (repeater_admin == nullptr) {
    repeater_admin = new RepeaterAdminScreen(this, &rtc_clock);
  }

  // Get contact name for the screen header
  ContactInfo contact;
  char name[32];
  strcpy(name, MECK_TR("Unknown", "Inconnu"));
  if (the_mesh.getContactByIdx(contactIdx, contact)) {
    strncpy(name, contact.name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
  }

  RepeaterAdminScreen* admin = (RepeaterAdminScreen*)repeater_admin;
  admin->openForContact(contactIdx, name);
  setCurrScreen(repeater_admin);

  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoRepeaterAdminDirect(int contactIdx) {
  // Open admin and auto-submit cached password (skips password screen)
  _skipRoomRedirect = true;  // Don't redirect back to conversation after login
  gotoRepeaterAdmin(contactIdx);
  RepeaterAdminScreen* admin = (RepeaterAdminScreen*)repeater_admin;
  if (admin && admin->getState() == RepeaterAdminScreen::STATE_PASSWORD_ENTRY) {
    // If password was pre-filled from cache, simulate Enter to submit login
    admin->handleInput('\r');
  }
}

void UITask::gotoPathEditor(int contactIdx) {
  // Lazy-initialize on first use
  if (path_editor == nullptr) {
    path_editor = new PathEditorScreen(this, &rtc_clock);
  }

  PathEditorScreen* editor = (PathEditorScreen*)path_editor;
  editor->openForContact(contactIdx);
  setCurrScreen(path_editor);

  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoDiscoveryScreen() {
  ((DiscoveryScreen*)discovery_screen)->resetScroll();
  setCurrScreen(discovery_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoLastHeardScreen() {
  ((LastHeardScreen*)last_heard_screen)->resetScroll();
  setCurrScreen(last_heard_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoRxLogScreen() {
  ((RxLogScreen*)rxlog_screen)->resetScroll();
  setCurrScreen(rxlog_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoTraceScreen() {
  TraceScreen* ts = (TraceScreen*)trace_screen;
  ts->enter(the_mesh.getNodePrefs()->path_hash_mode);
  setCurrScreen(trace_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}



void UITask::gotoGamesMenu() {
  GamesMenuScreen* gm = (GamesMenuScreen*)games_menu_screen;
  gm->enter();
  setCurrScreen(games_menu_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoSnakeScreen() {
  SnakeScreen* ss = (SnakeScreen*)snake_screen;
  ss->enter();
  setCurrScreen(snake_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoMinesweeperScreen() {
  MinesweeperScreen* ms = (MinesweeperScreen*)minesweeper_screen;
  ms->enter();
  setCurrScreen(minesweeper_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

#if defined(LilyGo_TDeck_Pro)
void UITask::gotoGBCScreen() {
#ifdef MECK_40MHZ_TEST
  // The emulator needs 240 MHz; at 40 it is unusable. Refuse from every
  // launch route (tile, J-key, games menu) and stay put. Snake and
  // Minesweeper are fine at 40 MHz and are not gated.
  showAlert(MECK_TR("Game Boy needs 240MHz", "Game Boy exige 240 MHz"), 1500);
  return;
#endif
  GBCEmulatorScreen* gb = (GBCEmulatorScreen*)gbc_screen;
  gb->enter();
  setCurrScreen(gbc_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

void UITask::onTraceResult(uint32_t tag, uint8_t flags, const uint8_t* path_snrs,
                           const uint8_t* path_hashes, uint8_t path_len, int8_t final_snr) {
  TraceScreen* ts = (TraceScreen*)trace_screen;
  if (ts) {
    ts->onTraceResult(tag, flags, path_snrs, path_hashes, path_len, final_snr);
    _next_refresh = 100;  // Force refresh to show results
  }
}

#ifdef MECK_WEB_READER
void UITask::gotoWebReader() {
  // Lazy-initialize on first use (same pattern as audiobook player)
  if (web_reader == nullptr) {
    Serial.printf("WebReader: lazy init - free heap: %d, largest block: %d\n",
                   ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    web_reader = new WebReaderScreen(this, _node_prefs);
    Serial.printf("WebReader: init complete - free heap: %d\n", ESP.getFreeHeap());
  }
  WebReaderScreen* wr = (WebReaderScreen*)web_reader;
  if (_display != NULL) {
    wr->enter(*_display);
  }
  // Heap diagnostic — check state after web reader entry (WiFi connects later)
  Serial.printf("[HEAP] WebReader enter - free: %u, largest: %u, PSRAM: %u\n",
      ESP.getFreeHeap(), ESP.getMaxAllocHeap(), ESP.getFreePsram());
  setCurrScreen(web_reader);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

#if HAS_GPS
void UITask::gotoMapScreen() {
  if (!map_screen) return;  // Not available on this platform
  MapScreen* map = (MapScreen*)map_screen;
  if (_display != NULL) {
    map->enter(*_display);
  }
  setCurrScreen(map_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

void UITask::onAdminLoginResult(bool success, uint8_t permissions, uint32_t server_time) {
  if (repeater_admin && isOnRepeaterAdmin()) {
    ((RepeaterAdminScreen*)repeater_admin)->onLoginResult(success, permissions, server_time);
    _next_refresh = 100;  // trigger re-render

    if (success) {
      int cidx = ((RepeaterAdminScreen*)repeater_admin)->getContactIdx();
      if (cidx >= 0) {
        clearDMUnread(cidx);

        // Room server login: redirect to conversation view with stored permissions.
        // Admin users see L:Admin footer to access the admin panel.
        // Skip redirect if user explicitly pressed L to get to admin.
        if (!_skipRoomRedirect) {
          ContactInfo contact;
          if (the_mesh.getContactByIdx(cidx, contact) && contact.type == ADV_TYPE_ROOM) {
            uint8_t maskedPerms = permissions & 0x03;
            gotoDMConversation(contact.name, cidx, maskedPerms);
            return;
          }
        }
        _skipRoomRedirect = false;
      }
    }
  }
}

void UITask::onAdminCliResponse(const char* from_name, const char* text) {
  if (repeater_admin && isOnRepeaterAdmin()) {
    ((RepeaterAdminScreen*)repeater_admin)->onCliResponse(text);
    _next_refresh = 100;  // trigger re-render
  }
}

void UITask::onAdminTelemetryResult(const uint8_t* data, uint8_t len) {
  Serial.printf("[UITask] onAdminTelemetryResult: %d bytes, onAdmin=%d\n", len, isOnRepeaterAdmin());
  if (repeater_admin && isOnRepeaterAdmin()) {
    ((RepeaterAdminScreen*)repeater_admin)->onTelemetryResult(data, len);
    _next_refresh = 100;  // trigger re-render
  }
}

#ifdef MECK_AUDIO_VARIANT
bool UITask::isAudioPlayingInBackground() const {
  if (!audiobook_screen) return false;
  AudiobookPlayerScreen* player = (AudiobookPlayerScreen*)audiobook_screen;
  return player->isAudioActive();
}

bool UITask::isAudioPausedInBackground() const {
  if (!audiobook_screen) return false;
  AudiobookPlayerScreen* player = (AudiobookPlayerScreen*)audiobook_screen;
  return player->isBookOpen() && !player->isAudioActive();
}
#endif