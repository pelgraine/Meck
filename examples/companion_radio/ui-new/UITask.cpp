#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
#include "NotesScreen.h"
#endif
#include "RepeaterAdminScreen.h"
#include "PathEditorScreen.h"
#include "DiscoveryScreen.h"
#include "LastHeardScreen.h"
#include "RxLogScreen.h"
#include "Tracescreen.h"
#include "GamesMenuScreen.h"
#include "SnakeScreen.h"
#include "MinesweeperScreen.h"
#ifdef MECK_WEB_READER
  #include "WebReaderScreen.h"
#endif
#if defined(MECK_TWATCH) && HAS_GPS
  #include "WatchMapScreen.h"
#elif HAS_GPS && !defined(LILYGO_TECHO_CARD)
  #include "MapScreen.h"
#endif
#include "target.h"
#if defined(LilyGo_TDeck_Pro_Max)
  #include "DRV2605Haptic.h"   // haptic motor for "Buzzer (vibrate)" channels
#endif
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(MECK_AUDIO_VARIANT) || defined(MECK_TWATCH)
  #include "HomeIcons.h"
#endif
#if defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
  #include <WiFi.h>
#endif
#if defined(LilyGo_T5S3_EPaper_Pro) && !defined(BLE_PIN_CODE) && !defined(MECK_WIFI_COMPANION)
  #include "esp_sleep.h"
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
  #if defined(LilyGo_T5S3_EPaper_Pro)
    #define UI_RECENT_LIST_SIZE 8
  #else
    #define UI_RECENT_LIST_SIZE 4
  #endif
#endif

#define PRESS_LABEL "long press"

#include "icons.h"
#include "ChannelScreen.h"
#include "ChannelPickerScreen.h"
#include "ContactsScreen.h"
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
#include "TextReaderScreen.h"
#endif
#include "SettingsScreen.h"
#ifdef MECK_AUDIO_VARIANT
#include "AudiobookPlayerScreen.h"
#include "VoiceMessageScreen.h"
#endif
#if defined(MECK_TWATCH)
#include <LittleFS.h>          // step history persistence (the "maps" partition)
#endif
#if defined(LILYGO_TWATCH_S3)
#include "WatchAlarmScreen.h"
#endif
#if defined(MECK_TWATCH)
#include "WatchNotesScreen.h"   // After UITask.h -- needs NodePrefs
#include "WatchChannelConfigScreen.h"   // After UITask.h -- needs NodePrefs, the_mesh
#endif
#ifdef TWATCH_COMPOSE_ENABLED
#include "TWatchComposeScreens.h"
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
#if defined(MECK_TWATCH)
    display.setTextSize(1);  // 240x240: size 2 overflows the 120px virtual width and wraps
#else
    display.setTextSize(2);
#endif
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
#elif defined(MECK_WIFI_COMPANION)
    WIFI_STATUS,
#endif
    ADVERT,
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
#if UI_SENSORS_PAGE == 1
    SENSORS,
#endif
#if HAS_BQ27220
    BATTERY,
#endif
#if !defined(MECK_TWATCH)
    SHUTDOWN,
#endif
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

#if defined(LilyGo_T5S3_EPaper_Pro)
    // T5S3: text-only battery indicator — "Batt 99% 4.1v"
    char battStr[20];
    float volts = batteryMilliVolts / 1000.0f;
    snprintf(battStr, sizeof(battStr), "Batt %d%% %.1fv", batteryPercentage, volts);
    uint16_t textWidth = display.getTextWidth(battStr);
    int textX = display.width() - textWidth - 2;
    if (outIconX) *outIconX = textX;
    display.setCursor(textX, 0);
    display.print(battStr);
    display.setTextSize(1);  // restore default text size
#elif defined(LILYGO_TECHO_LITE)
    // T-Echo Lite: text-only battery (icon misaligns due to fillRect/setCursor offset mismatch at 2× scale)
    char battStr[8];
    snprintf(battStr, sizeof(battStr), "%d%%", batteryPercentage);
    uint16_t textWidth = display.getTextWidth(battStr);
    int textX = display.width() - textWidth - 2;
    if (outIconX) *outIconX = textX;
    display.setCursor(textX, 0);  // Same baseline as node name (HOME_HDR_Y)
    display.print(battStr);
    display.setTextSize(1);
#else
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
#endif
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
       _poweroff_msg_shown(false), _editing_utc(false), _saved_utc_offset(0), sensors_lpp(200) {  }

  bool isEditingUTC() const { return _editing_utc; }
  bool isFirstPage() const { return _page == HomePage::FIRST; }
  bool isOnRecentPage() const { return _page == HomePage::RECENT; }
#if defined(MECK_TWATCH)
  bool isOnShutdownPage() const { return false; }
#else
  bool isOnShutdownPage() const { return _page == HomePage::SHUTDOWN; }
#endif
  void cancelEditing() { 
    if (_editing_utc) {
      _node_prefs->utc_offset_hours = _saved_utc_offset;
      _editing_utc = false;
    }
  }

  void poll() override {
    if (_shutdown_init && millis() >= _shutdown_at && !_task->isButtonPressed()) {
      if (_poweroff_selected) _task->setFullPowerOff(true);
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(MECK_TWATCH)
    _task->setHomeShowingTiles(false);  // Reset — only set true on FIRST page
#endif

    // Power off: full-screen message, no header
    // First render: "powering off..." + wake instruction
    // Second render onward: wake instruction only (persists on e-ink)
    if (_shutdown_init && _poweroff_selected) {
#if defined(LilyGo_T5S3_EPaper_Pro)
      board.setBacklight(false);
#endif
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (!_poweroff_msg_shown) {
        _poweroff_msg_shown = true;
        display.drawTextCentered(display.width() / 2, 30, "powering off...");
        display.drawTextCentered(display.width() / 2, 46, "plug in USB-C to turn on");
        return 1500;
      } else {
        display.drawTextCentered(display.width() / 2, 38, "plug in USB-C to turn on");
        return 5000;
      }
    }

    // node name (tinyfont to avoid overlapping clock)
    display.setTextSize(_node_prefs->smallTextSize());
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
#if defined(LilyGo_T5S3_EPaper_Pro)
    // T5S3: FreeSans12pt ascenders need more room than built-in font.
    // Shift header elements down by 4 virtual units (~17px physical).
    #define HOME_HDR_Y 1
#elif defined(LILYGO_TECHO_LITE)
    #define HOME_HDR_Y 0
#elif defined(MECK_TWATCH)
    #define HOME_HDR_Y 1
#else
    #define HOME_HDR_Y -3
#endif
    display.setCursor(0, HOME_HDR_Y);
#if defined(MECK_TWATCH)
    // Watch: render the name in a very small font so long names fit beside the
    // centred clock instead of overrunning it. Colour was set above (GREEN).
    ((LGFXDisplay*)&display)->printSmallFont(0, HOME_HDR_Y, filtered_name);
#else
    display.print(filtered_name);
#endif
    // battery voltage + status icons
#ifdef MECK_AUDIO_VARIANT
    int battLeftX = display.width(); // default if battery doesn't render
    renderBatteryIndicator(display, _task->getBattMilliVolts(), &battLeftX);

    // audio background playback indicator (>> icon next to battery)
    renderAudioIndicator(display, battLeftX);

    // alarm enabled indicator (AL icon, left of audio or battery)
    renderAlarmIndicator(display, battLeftX);
#elif !defined(MECK_TWATCH)
    renderBatteryIndicator(display, _task->getBattMilliVolts());
#endif

    // centered clock — only show when time is valid
#if defined(MECK_TWATCH)
    // Watch: right-aligned header cluster, right to left -- battery %, unread
    // count, then clock, all in the tiny node-name font. Battery % is
    // colour-coded by charge level.
    {
      LGFXDisplay* lcd = (LGFXDisplay*)&display;

      // Battery % from the AXP2101 fuel gauge, rightmost.
      int pct = (int)_task->getBatteryPercent();
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      char battbuf[6];
      sprintf(battbuf, "%d%%", pct);
      int battX = display.width() - lcd->smallTextWidth(battbuf) - 2;
      uint16_t battColor = (pct >= 50) ? 0x07E0     // green  50-100%
                         : (pct >= 25) ? 0xFFE0     // yellow 25-50%
                         : (pct >= 10) ? 0xFD20     // orange 10-25%
                                       : 0xF800;    // red    0-10%
      lcd->setRawColor(battColor);
      lcd->printSmallFont(battX, HOME_HDR_Y, battbuf);

      // Unread count (blue), to the left of the battery %.
      char numbuf[6];
      sprintf(numbuf, "%d", _task->getUnreadMsgCount());
      int numX = battX - lcd->smallTextWidth(numbuf) - 3;
      display.setColor(DisplayDriver::BLUE);
      lcd->printSmallFont(numX, HOME_HDR_Y, numbuf);

      // Clock (white), to the left of the unread count, when time is valid.
      uint32_t now = _rtc->getCurrentTime();
      if (now > 1700000000) {  // valid timestamp (after ~Nov 2023)
        int32_t local = (int32_t)now + ((int32_t)_node_prefs->utc_offset_hours * 3600);
        int hrs = (local / 3600) % 24;
        if (hrs < 0) hrs += 24;
        int mins = (local / 60) % 60;
        if (mins < 0) mins += 60;
        char timeBuf[6];
        sprintf(timeBuf, "%02d:%02d", hrs, mins);
        int clockX = (display.width() - lcd->smallTextWidth(timeBuf)) / 2;
        // Long node names: keep the clock centred when there is room,
        // otherwise push it right so it starts just past the name. When even
        // that would run into the unread count, hide the clock instead.
        int nameEndX = lcd->smallTextWidth(filtered_name) + 3;
        if (clockX < nameEndX) clockX = nameEndX;
        if (clockX + lcd->smallTextWidth(timeBuf) + 3 <= numX) {
          display.setColor(DisplayDriver::LIGHT);
          lcd->printSmallFont(clockX, HOME_HDR_Y, timeBuf);
        }
      }
    }
#else
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
#endif
    // curr page indicator
#if defined(LILYGO_TECHO_LITE)
    int y = 13;   // Below header
#elif defined(LilyGo_T5S3_EPaper_Pro)
    int y = 14;  // Closer to header
#elif defined(MECK_TWATCH)
    int y = (_page == HomePage::FIRST) ? 9 : 18;  // first page: tuck up under the header
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
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(MECK_TWATCH)
      _task->setHomeShowingTiles(true);
#endif
#if defined(LilyGo_T5S3_EPaper_Pro)
  #if defined(BLE_PIN_CODE) || defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
      int y = 18;  // Tighter spacing — connectivity info fills gap below dots
  #else
      int y = 26;  // Standalone: extra line below dots (no IP/Connected row)
  #endif
#elif defined(LILYGO_TECHO_LITE)
      int y = 18;  // Below page dots
#elif defined(MECK_TWATCH)
      int y = 12;  // 4-row grid starts just below the dots
#else
      int y = 20;
#endif
#if !defined(MECK_TWATCH)
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getUnreadMsgCount());
      display.drawTextCentered(display.width() / 2, y, tmp);
#endif
#if defined(LILYGO_TECHO_LITE)
      y += 12;  // Compact
#elif defined(LilyGo_TDeck_Pro_Max)
      y += 10;  // MAX: pull < Connected > up under MSG to make room for [T] Phone
#elif defined(MECK_TWATCH)
      // watch: no MSG banner gap -- the 4-row grid sits just under the dots
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
#if defined(LILYGO_TECHO_LITE)
        y += 14;  // Compact
#elif defined(LilyGo_TDeck_Pro_Max)
        y += 6;  // MAX: tighter pin-to-menu gap so [T] Phone is not clipped
#else
        y += 18;
#endif
#endif
      }
      #endif

      // ----- T5S3: Tappable tile grid (touch-friendly home screen) -----
#if defined(LilyGo_T5S3_EPaper_Pro)
      // 3×2 grid of tiles below MSG count
      // Virtual coords (128×128), scaled by DisplayDriver
      {
        struct Tile { const uint8_t* icon; const char* label; };
        const Tile tiles[2][3] = {
          { {icon_envelope, "Messages"}, {icon_people, "Contacts"}, {icon_gear, "Settings"} },
#ifdef MECK_WEB_READER
          { {icon_book, "Reader"},       {icon_notepad, "Notes"},   {icon_search, "Browser"} }
#else
          { {icon_book, "Reader"},       {icon_notepad, "Notes"},   {icon_search, "Discover"} }
#endif
        };

        const int tileW = 40;
        const int tileH = 22;
        const int gapX = 1;
        const int gapY = 1;
        const int gridW = tileW * 3 + gapX * 2;
        const int gridX = (display.width() - gridW) / 2;
        const int gridY = y + 2;
        _task->setTileGridVY(gridY);  // Store for touch hit testing

        for (int row = 0; row < 2; row++) {
          for (int col = 0; col < 3; col++) {
            int tx = gridX + col * (tileW + gapX);
            int ty = gridY + row * (tileH + gapY);

            // Tile border
            display.setColor(DisplayDriver::LIGHT);
            display.drawRect(tx, ty, tileW, tileH);

            // Icon centered in tile
            int iconX = tx + (tileW - HOME_ICON_W) / 2;
            int iconY = ty + 2;
            display.drawXbm(iconX, iconY, tiles[row][col].icon, HOME_ICON_W, HOME_ICON_H);

            // Label centered below icon
            display.setTextSize(_node_prefs->smallTextSize());
            display.drawTextCentered(tx + tileW / 2, ty + 15, tiles[row][col].label);
          }
        }

        // Third row: Trace (col 0) + Games (col 1)
        {
          int row3y = gridY + 2 * (tileH + gapY);

          // Trace tile (column 0)
          int col0x = gridX;
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(col0x, row3y, tileW, tileH);
          int iconX = col0x + (tileW - HOME_ICON_W) / 2;
          int iconY = row3y + 2;
          display.drawXbm(iconX, iconY, icon_trace, HOME_ICON_W, HOME_ICON_H);
          display.setTextSize(_node_prefs->smallTextSize());
          display.drawTextCentered(col0x + tileW / 2, row3y + 15, "Trace");

          // Games tile (column 1)
          int col1x = gridX + (tileW + gapX);
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(col1x, row3y, tileW, tileH);
          iconX = col1x + (tileW - HOME_ICON_W) / 2;
          iconY = row3y + 2;
          display.drawXbm(iconX, iconY, icon_gamepad, HOME_ICON_W, HOME_ICON_H);
          display.setTextSize(_node_prefs->smallTextSize());
          display.drawTextCentered(col1x + tileW / 2, row3y + 15, "Games");
        }

        // Nav hint at bottom of screen
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(_node_prefs->smallTextSize());
        display.drawTextCentered(display.width() / 2, display.height() - 8, "Tap tile to open");
      }
      display.setTextSize(1);

#elif defined(MECK_TWATCH)
      // ----- T-Watch S3 Plus: P4-style coloured tile grid (3x2) -----
      // Border colours approximate the Meck P4 home palette (RGB565): a white
      // icon + label on a dark navy fill, each tile a distinct bright border.
      // setRawColor() pushes exact RGB565 past the Color enum + watch grey remap.
      {
        struct Tile { const uint8_t* icon; const char* label; uint16_t color; };
        const Tile tiles[4][2] = {
          { {icon_envelope, "Messages", 0x0CB1}, {icon_people,  "Contacts", 0xCAA0} },
          { {icon_gear,     "Settings", 0x0560}, {icon_search,  "Discover", 0xF81F} },
#if defined(LILYGO_TWATCH_S3)
          { {icon_trace,    "Trace",    0xF800}, {icon_alarm,   "Alarms",   0x231D} },
#else
          { {icon_trace,    "Trace",    0xF800}, {icon_map,     "Maps",     0x231D} },
#endif
          { {icon_notepad,  "Notes",    0x07FF}, {icon_star,    "Steps",    0xFFE0} },
        };
        const uint16_t TILE_FILL = 0x18C5;   // dark navy

        const int cols = 2, rows = 4;
        const int tileW = 56, tileH = 24, gapX = 4, gapY = 2;
        const int gridW = tileW * cols + gapX * (cols - 1);
        const int gridX = (display.width() - gridW) / 2;
        const int gridY = y + 2;
        _task->setTileGridVY(gridY);

        LGFXDisplay* lcd = (LGFXDisplay*)&display;  // watch display is always LGFXDisplay
        for (int row = 0; row < rows; row++) {
          for (int col = 0; col < cols; col++) {
            int tx = gridX + col * (tileW + gapX);
            int ty = gridY + row * (tileH + gapY);
            lcd->setRawColor(TILE_FILL);
            lcd->fillRoundRect(tx, ty, tileW, tileH, 4);
            lcd->setRawColor(tiles[row][col].color);
            lcd->drawRoundRect(tx, ty, tileW, tileH, 4);
            display.setColor(DisplayDriver::LIGHT);
            int iconX = tx + (tileW - HOME_ICON_W) / 2;
            int iconY = ty + 3;
            display.drawXbm(iconX, iconY, tiles[row][col].icon, HOME_ICON_W, HOME_ICON_H);
            display.setTextSize(_node_prefs->smallTextSize());
            display.drawTextCentered(tx + tileW / 2, ty + 16, tiles[row][col].label);
          }
        }
        display.setTextSize(1);
      }

#else
      // Non-T5S3: keyboard shortcut menu
#if defined(LILYGO_TECHO_LITE)
      // T-Echo Lite: compact centered menu (tiny font fits 117px virtual width)
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(0);  // 6×8 built-in font
      y += 2;
      display.drawTextCentered(display.width() / 2, y, "M:Msgs  C:Contacts");
      y += 8;
      display.drawTextCentered(display.width() / 2, y, "S:Set   F:Discover");
      y += 8;
      display.drawTextCentered(display.width() / 2, y, "H:Last Heard");
      y += 9;
      if (y < display.height() - 14) {
        display.setColor(DisplayDriver::GREEN);
        display.drawTextCentered(display.width() / 2, y, "Arrows: cycle views");
      }
      display.setTextSize(1);  // restore
#else
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
#endif // LILYGO_TECHO_LITE
#endif
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
          sprintf(tmp, "%dm", secs / 60);
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
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(display.width() / 2, display.height() - 24,
                               "Tap here for full Last Heard list");
#elif defined(MECK_TWATCH)
      display.drawTextCentered(display.width() / 2, display.height() - 24,
                               "Long Press: Full Last Heard List");
#else
      display.drawTextCentered(display.width() / 2, display.height() - 24,
                               "H: Full Last Heard list");
#endif
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
      sprintf(tmp, "Noise floor: %d", radio_driver.getNoiseFloor());
      display.print(tmp);
      display.setCursor(0, 64);
      sprintf(tmp, "RX packets: %u", (unsigned)the_mesh.getRxPacketCount());
      display.print(tmp);
#ifdef BLE_PIN_CODE
    } else if (_page == HomePage::BLUETOOTH) {
      display.setColor(DisplayDriver::GREEN);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawXbm((display.width() - 32) / 2, 28,
#else
      display.drawXbm((display.width() - 32) / 2, 18,
#endif
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.drawTextCentered(display.width() / 2, 64, "< Connected >");
#else
        display.drawTextCentered(display.width() / 2, 53, "< Connected >");
#endif
      } else if (_task->isSerialEnabled() && the_mesh.getBLEPin() != 0) {
        display.setColor(DisplayDriver::RED);
        display.setTextSize(2);
        sprintf(tmp, "Pin:%d", the_mesh.getBLEPin());
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.drawTextCentered(display.width() / 2, 64, tmp);
#else
        display.drawTextCentered(display.width() / 2, 53, tmp);
#endif
      }
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(display.width() / 2, 80, "toggle: " PRESS_LABEL);
#elif defined(MECK_TWATCH)
      display.drawTextCentered(display.width() / 2, 68, "toggle: " PRESS_LABEL);
#else
      display.drawTextCentered(display.width() / 2, 68, "toggle: " PRESS_LABEL);
      display.drawTextCentered(display.width() / 2, 78, "or press Enter key");
#endif
#endif
#ifdef MECK_WIFI_COMPANION
    } else if (_page == HomePage::WIFI_STATUS) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 18, "WiFi Companion");

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
          display.drawTextCentered(display.width() / 2, wy, "< App Connected >");
        } else {
          display.setColor(DisplayDriver::YELLOW);
          display.setTextSize(1);
          display.drawTextCentered(display.width() / 2, wy, "Waiting for app...");
        }
      } else {
        display.setColor(DisplayDriver::RED);
        display.drawTextCentered(display.width() / 2, wy, "Not connected");
        wy += wLH + 2;
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width() / 2, wy, "Configure in Settings");
      }
      display.setTextSize(1);
#endif
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::GREEN);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawXbm((display.width() - 32) / 2, 28, advert_icon, 32, 32);
#else
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
#endif
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(display.width() / 2, 64, "advert: " PRESS_LABEL);
#elif defined(MECK_TWATCH)
      display.drawTextCentered(display.width() / 2, 57, "advert: " PRESS_LABEL);
#else
      display.drawTextCentered(display.width() / 2, 57, "advert: " PRESS_LABEL);
      display.drawTextCentered(display.width() / 2, 67, "or press Enter key");
#endif
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      extern GPSStreamCounter gpsStream;
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;

      // GPS state line
      if (!_node_prefs->gps_enabled) {
        strcpy(buf, "gps off");
      } else {
        strcpy(buf, "gps on");
      }
      display.drawTextLeftAlign(0, y, buf);

      if (nmea == NULL) {
        y = y + 12;
        display.drawTextLeftAlign(0, y, "Can't access GPS");
      } else {
        strcpy(buf, nmea->isValid()?"fix":"no fix");
        display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "sat");
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        y = y + 12;

        // NMEA sentence counter — confirms baud rate and data flow
        display.drawTextLeftAlign(0, y, "sentences");
        if (_node_prefs->gps_enabled) {
          uint16_t sps = gpsStream.getSentencesPerSec();
          uint32_t total = gpsStream.getSentenceCount();
          sprintf(buf, "%u/s (%lu)", sps, (unsigned long)total);
        } else {
          strcpy(buf, "hw off");
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
          display.drawTextLeftAlign(0, y, "time(U)");
          sprintf(buf, "%02d:%02d UTC%+d", hrs, mins, _node_prefs->utc_offset_hours);
          display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
        } else {
          display.drawTextLeftAlign(0, y, "time(U)");
          display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, "no sync");
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
        display.drawTextCentered(display.width() / 2, by + bh - 10, "W/S:adj Enter:ok Q:cancel");
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
      display.drawTextCentered(display.width() / 2, y, "Battery Gauge");
      y += 12;

      display.setColor(DisplayDriver::LIGHT);

      // Time to empty
      uint16_t tte = board.getTimeToEmpty();
      display.drawTextLeftAlign(0, y, "remaining");
      if (tte == 0xFFFF || tte == 0) {
        strcpy(buf, tte == 0 ? "depleted" : "charging");
      } else if (tte >= 60) {
        sprintf(buf, "%dh %dm", tte / 60, tte % 60);
      } else {
        sprintf(buf, "%d min", tte);
      }
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Average current
      int16_t avgCur = board.getAvgCurrent();
      display.drawTextLeftAlign(0, y, "avg current");
      sprintf(buf, "%d mA", avgCur);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Average power
      int16_t avgPow = board.getAvgPower();
      display.drawTextLeftAlign(0, y, "avg power");
      sprintf(buf, "%d mW", avgPow);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Voltage (already available)
      uint16_t mv = board.getBattMilliVolts();
      display.drawTextLeftAlign(0, y, "voltage");
      sprintf(buf, "%d.%03d V", mv / 1000, mv % 1000);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Remaining capacity (clamped to design capacity — gauge FCC may be
      // stale from factory defaults until a full charge cycle re-learns it)
      uint16_t remCap = board.getRemainingCapacity();
      uint16_t desCap = board.getDesignCapacity();
      if (desCap > 0 && remCap > desCap) remCap = desCap;
      display.drawTextLeftAlign(0, y, "remaining cap");
      sprintf(buf, "%d mAh", remCap);
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
      y += 10;

      // Battery temperature
      int16_t battTemp = board.getBattTemperature();
      display.drawTextLeftAlign(0, y, "temperature");
      sprintf(buf, "%d.%d C", battTemp / 10, abs(battTemp % 10));
      display.drawTextRightAlign(display.width()-1-EINK_X_OFFSET, y, buf);
#endif
    }
#if !defined(MECK_TWATCH)
    else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (_shutdown_init) {
#if defined(LilyGo_T5S3_EPaper_Pro)
        board.setBacklight(false);
#endif
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else if (_poweroff_confirm) {
        // Confirmation prompt for power off
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.drawXbm((display.width() - 32) / 2, 28, power_icon, 32, 32);
#else
        display.drawXbm((display.width() - 32) / 2, 20, power_icon, 32, 32);
#endif
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.drawTextCentered(display.width() / 2, 64, "power off device?");
        display.drawTextCentered(display.width() / 2, 76, "usb-c to wake");
#else
        display.drawTextCentered(display.width() / 2, 56, "power off device?");
        display.drawTextCentered(display.width() / 2, 66, "usb-c to wake");
        display.drawTextCentered(display.width() / 2, 82, "Enter:yes  q:no");
#endif
      } else {
        // Menu: hibernate / power off
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.drawXbm((display.width() - 32) / 2, 20, power_icon, 32, 32);
        const int y1 = 58, y2 = 70;
#else
        display.drawXbm((display.width() - 32) / 2, 20, power_icon, 32, 32);
        const int y1 = 56, y2 = 68;
#endif
        char line1[48], line2[48];
#if defined(LilyGo_TDeck_Pro)
        snprintf(line1, sizeof(line1), "%shibernate: long press/Enter", _poweroff_selected ? " " : ">");
        snprintf(line2, sizeof(line2), "%spower off: long press/Enter", _poweroff_selected ? ">" : " ");
#else
        snprintf(line1, sizeof(line1), "%shibernate: " PRESS_LABEL, _poweroff_selected ? " " : ">");
        snprintf(line2, sizeof(line2), "%spower off: " PRESS_LABEL, _poweroff_selected ? ">" : " ");
#endif
        display.drawTextCentered(display.width() / 2, y1, line1);
        display.drawTextCentered(display.width() / 2, y2, line2);
      }
    }
#endif
    return _editing_utc ? 700 : 5000;
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
        _task->showAlert("UTC offset saved", 800);
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

#if !defined(MECK_TWATCH)
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
#endif

    if (c == KEY_LEFT || c == KEY_PREV || c == 'a') {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT || c == 'd') {
      _page = (_page + 1) % HomePage::Count;
      if (_page == HomePage::RECENT) {
        _task->showAlert("Recent adverts", 800);
      }
      return true;
    }
#ifdef BLE_PIN_CODE
    if (c == KEY_ENTER && _page == HomePage::BLUETOOTH) {
      if (_task->isSerialEnabled()) {  // toggle Bluetooth on/off
        _task->disableSerial();
      } else {
        _task->enableSerial();
      }
      return true;
    }
#endif
    if (c == KEY_ENTER && _page == HomePage::ADVERT) {
      _task->notify(UIEventType::ack);
      if (the_mesh.advert()) {
        _task->showAlert("Advert sent!", 1000);
      } else {
        _task->showAlert("Advert failed..", 1000);
      }
      return true;
    }
#if ENV_INCLUDE_GPS == 1
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
// Lock Screen — T5S3 and T-Deck Pro
// Big clock, battery %, unread message count.
// T5S3: Long press boot button to lock/unlock. Touch disabled while locked.
// T-Deck Pro: Double-press boot button to lock/unlock. Touch+keyboard disabled.
// ==========================================================================
#if defined(MECK_TWATCH)
// Watch lock/clock screen: big HH:MM plus a day+date line, shown when the
// display is woken (by raise-to-wake or a tap) after the idle timeout.
class ClockScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _node_prefs;
public:
  ClockScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* node_prefs)
    : _task(task), _rtc(rtc), _node_prefs(node_prefs) { }

  int render(DisplayDriver& display) override {
    uint32_t now = _rtc->getCurrentTime();
    char buf[24];
    display.setColor(DisplayDriver::LIGHT);

    // tiny daily step count, above the clock
    {
      char sbuf[20];
      snprintf(sbuf, sizeof(sbuf), "%u steps", (unsigned)_task->getTodaySteps());
      display.setColor(DisplayDriver::GREEN);   // muted grey on the watch palette
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 6, sbuf);
      display.setColor(DisplayDriver::LIGHT);   // restore for the clock
    }

    // big clock
    if (now > 1700000000) {  // valid timestamp (after ~Nov 2023)
      int32_t local = (int32_t)now + ((int32_t)_node_prefs->utc_offset_hours * 3600);
      int hrs = (local / 3600) % 24;
      if (hrs < 0) hrs += 24;
      int mins = (local / 60) % 60;
      if (mins < 0) mins += 60;
      sprintf(buf, "%02d:%02d", hrs, mins);
    } else {
      strcpy(buf, "--:--");
    }
    ((LGFXDisplay*)&display)->printClockFont(display.width() / 2, display.height() / 2 - 30, buf);

    // day + date line below the clock
    if (now > 1700000000) {
      time_t local = (time_t)now + (time_t)_node_prefs->utc_offset_hours * 3600;
      struct tm tmv;
      gmtime_r(&local, &tmv);
      static const char* const days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
      static const char* const mons[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                         "Jul","Aug","Sep","Oct","Nov","Dec"};
      sprintf(buf, "%s %d %s", days[tmv.tm_wday], tmv.tm_mday, mons[tmv.tm_mon]);
      display.setTextSize(2);
      display.setColor(DisplayDriver::GREEN);  // light grey on the watch palette
      display.drawTextCentered(display.width() / 2, display.height() / 2 + 12, buf);
      display.setColor(DisplayDriver::LIGHT);  // restore for the battery line
    }

    // battery % and unread count
    int pct = (int)_task->getBatteryPercent();
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    sprintf(buf, "%d%% | %d unread", pct, _task->getUnreadMsgCount());
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, display.height() / 2 + 38, buf);

    return 1000;  // refresh about once a second
  }

  bool handleInput(char c) override {
    _task->gotoHomeScreen();  // any input dismisses the clock screen back to the tiles
    return true;
  }
};

// Steps screen: big daily step count in the clock font. Opened by long-pressing
// the Steps home tile; any tap dismisses back to the tiles.
class StepsScreen : public UIScreen {
  UITask* _task;
public:
  StepsScreen(UITask* task) : _task(task) { }

  int render(DisplayDriver& display) override {
    display.setColor(DisplayDriver::LIGHT);
    char buf[16];
    snprintf(buf, sizeof(buf), "%u", (unsigned)_task->getTodaySteps());
    ((LGFXDisplay*)&display)->printClockFont(display.width() / 2, display.height() / 2 - 18, buf);
    return 1000;
  }

  bool handleInput(char c) override {
    if (c == KEY_ENTER) {              // touch long-press, or the S3's PWR key
      _task->gotoStepsHistoryScreen();
      return true;
    }
    _task->gotoHomeScreen();
    return true;
  }
};

// Steps history: today plus the previous six days, as labelled bars. Reached by
// long-pressing the steps screen. Any input returns to the steps screen.
class StepsHistoryScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _node_prefs;

  static const char* dowShort(int dow) {
    static const char* names[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    return (dow >= 0 && dow < 7) ? names[dow] : "??";
  }

public:
  StepsHistoryScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* node_prefs)
    : _task(task), _rtc(rtc), _node_prefs(node_prefs) {}

  int render(DisplayDriver& display) override {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    const int W = display.width();

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(W / 2, 3, "Last 7 days");

    uint32_t vals[7];
    uint32_t maxv = 1;
    for (int i = 0; i < 7; i++) {
      vals[i] = _task->getStepHistory(i);
      if (vals[i] > maxv) maxv = vals[i];
    }

    // Weekday of today, from the local day-of-epoch (1970-01-01 was a Thursday).
    int todayDow = -1;
    uint32_t now = _rtc->getCurrentTime();
    if (now > 1700000000) {
      int32_t localDay = ((int32_t)now + ((int32_t)_node_prefs->utc_offset_hours * 3600)) / 86400;
      todayDow = (int)((localDay + 4) % 7);
    }

    char buf[12];
    for (int i = 0; i < 7; i++) {
      int y = 16 + i * 14;
      int barW = (int)(((uint64_t)(W - 6) * vals[i]) / maxv);
      if (barW < 2) barW = 2;

      d->setRawColor(i == 0 ? 0x231D : 0x18C5);   // today in the tile blue
      d->fillRoundRect(3, y, barW, 12, 2);

      const char* label = "-";
      if (i == 0) label = "Today";
      else if (todayDow >= 0) label = dowShort((todayDow - i + 14) % 7);

      d->setRawColor(0xFFFF);
      d->printSmallFont(6, y + 8, label);
      snprintf(buf, sizeof(buf), "%lu", (unsigned long)vals[i]);
      d->printSmallFont(W - 4 - d->smallTextWidth(buf), y + 8, buf);
    }
    return 1000;
  }

  bool handleInput(char c) override {
    _task->gotoStepsScreen();
    return true;
  }
};
#endif

#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.setTextSize(5);  // T5S3: FreeSansBold24pt × 5
#else
    display.setTextSize(5);  // T-Deck Pro: FreeSansBold12pt at GxEPD 2× scale
#endif
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
        sprintf(infoBuf, "%d%%  |  %d unread", pct, unread);
      } else {
        sprintf(infoBuf, "%d%%", pct);
      }
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, 108, infoBuf);
    }

    // ---- Unlock hint ----
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.setTextSize(_node_prefs->smallTextSize());
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextCentered(display.width() / 2, 120, "Hold button to unlock");
#endif

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

#if defined(PIN_USER_BTN) || defined(MECK_PMU_BUTTON)
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
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
  text_reader = new TextReaderScreen(this, node_prefs);
  notes_screen = new NotesScreen(this, node_prefs);
#else
  text_reader = nullptr;   // T-Echo Lite: excluded to save RAM (256KB nRF52)
  notes_screen = nullptr;
#endif
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
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
  lock_screen = new LockScreen(this, &rtc_clock, node_prefs);
#endif
#if defined(MECK_TWATCH)
  lock_screen = new ClockScreen(this, &rtc_clock, node_prefs);
  steps_screen = new StepsScreen(this);
  steps_history_screen = new StepsHistoryScreen(this, &rtc_clock, node_prefs);
  // LittleFS ("/maps" partition) is mounted in setup() before UITask::begin().
  loadSteps();
#endif
#if defined(LILYGO_TWATCH_S3)
  // LittleFS ("/maps" partition) is mounted in setup() before UITask::begin().
  watch_alarm_screen = new WatchAlarmScreen(this, &rtc_clock, node_prefs);
  ((WatchAlarmScreen*)watch_alarm_screen)->load();
#endif
#if defined(MECK_TWATCH)
  // LittleFS ("/maps" partition) is mounted in setup() before UITask::begin().
  watch_notes_screen = new WatchNotesScreen(this, &rtc_clock, node_prefs);
  watch_channel_cfg_screen = new WatchChannelConfigScreen(this, node_prefs);
#endif
#ifdef TWATCH_COMPOSE_ENABLED
  tw_picker   = new TWatchChannelPicker(_display);
  tw_channel  = new TWatchChannelScreen(_display);
  tw_keyboard = new TWatchKeyboardScreen(_display);
  ((TWatchChannelPicker*)tw_picker)->setStore((ChannelScreen*)channel_screen);
  ((TWatchChannelScreen*)tw_channel)->setStore((ChannelScreen*)channel_screen);
#endif
  audiobook_screen = nullptr;  // Created and assigned from main.cpp if audio hardware present
#ifdef MECK_AUDIO_VARIANT
  alarm_screen = nullptr;      // Created and assigned from main.cpp if audio hardware present
  voice_screen = nullptr;      // Created and assigned from main.cpp on first mic key press
#endif
#ifdef HAS_4G_MODEM
  sms_screen = new SMSScreen(this, node_prefs);
#endif
#if defined(MECK_TWATCH) && HAS_GPS
  map_screen = new WatchMapScreen(this);
#elif HAS_GPS && !defined(LILYGO_TECHO_CARD)
  map_screen = new MapScreen(this);
#else
  map_screen = nullptr;
#endif

#if defined(LilyGo_T5S3_EPaper_Pro)
  // Apply saved display preferences before first render
  if (_node_prefs->portrait_mode) {
    ::display.setPortraitMode(true);
  }
#endif

  // Apply saved dark mode preference (both T-Deck Pro and T5S3)
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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
    const char* customSound = notifSounds.getSoundForChannel(channel_idx);
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
#ifdef TWATCH_COMPOSE_ENABLED
      (curr == tw_channel &&
      ((TWatchChannelScreen*)tw_channel)->getChannelIdx() == channel_idx) ||
#endif
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
  if (!isOnRepeaterAdmin() && !isRoomMsg && !suppressNotif) {
    char alertBuf[40];
    snprintf(alertBuf, sizeof(alertBuf), "New: %s", from_name);
    showAlert(alertBuf, 2000);
  }
  // Ensure picker badges update after toaster clears
  if (isOnChannelPickerScreen()) {
    forceRefresh();
  }

  if (_display != NULL && !suppressNotif) {
#if defined(MECK_TWATCH)
    // Watch: a new message must not wake the screen. The unread counter has
    // already been incremented above; leave the display asleep so the count
    // is only seen on the next tilt-wake to the clock screen. Only refresh
    // when the display is already on.
    if (_display->isOn()) {
      _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
      if (isRoomMsg) {
        unsigned long earliest = millis() + 3000;  // At most one refresh per 3s during sync
        if (_next_refresh < earliest) _next_refresh = earliest;
      } else {
        _next_refresh = 100;  // trigger refresh
      }
    }
#else
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
#endif
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

void UITask::setCurrScreen(UIScreen* c) {
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
#if defined(PIN_USER_BTN) || defined(MECK_PMU_BUTTON)
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if defined(MECK_TWATCH)
  // TEMP power-debug probe: every 60s, log display state + board power stats.
  {
    static unsigned long _pwr_dbg_next = 0;
    if (millis() >= _pwr_dbg_next) {
      _pwr_dbg_next = millis() + 60000;
      Serial.printf("[PWR] uptime=%lus display=%s auto_off_in=%ldms\n",
                    millis() / 1000,
                    (_display && _display->isOn()) ? "ON" : "off",
                    (long)(_auto_off - millis()));
      board.printPowerDebug();
    }
  }
#endif
#if defined(MECK_DIAG_PMU_BTN)
  // TEMP DIAGNOSTIC (power bisect): drive the PMU-key poll replica every loop
  // (it rate-limits itself to 30 ms, matching PMUButton). Remove with the env.
  board.diagPollPmuKey();
#endif
#if defined(PIN_USER_BTN) || defined(MECK_PMU_BUTTON)
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
#if defined(LilyGo_T5S3_EPaper_Pro)
    // T5S3: single click = cycle pages on home, go back to home from elsewhere
    // Ignored while locked — long press required to unlock
    if (_locked) {
      c = 0;
    } else if (_vkbActive) {
      onVKBCancel();
      c = 0;
    } else if (curr == home) {
      c = checkDisplayOn(KEY_NEXT);
    } else {
      // Navigate back: reader reading→file list, file list→home, others→home
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
      if (isOnTextReader()) {
        TextReaderScreen* reader = (TextReaderScreen*)text_reader;
        if (reader && reader->isReading()) {
          c = checkDisplayOn('q');  // reading mode: close book → file list
        } else {
          gotoHomeScreen();  // file list: go home
          c = 0;
        }
      } else if (isOnNotesScreen()) {
        NotesScreen* notes = (NotesScreen*)notes_screen;
        if (notes && notes->isEditing()) {
          notes->triggerSaveAndExit();  // save and return to file list
        } else {
          notes->exitNotes();
          gotoHomeScreen();
        }
        c = 0;
      } else
#endif
      if (isOnChannelPickerScreen()) {
        gotoHomeScreen();  // picker → home
        c = 0;
      } else if (isOnChannelScreen()) {
        gotoChannelPickerScreen();  // channel messages → picker
        c = 0;
      } else {
        gotoHomeScreen();
        c = 0;  // consumed
      }
    }
#elif defined(LilyGo_TDeck_Pro)
    // T-Deck Pro: single click ignored while locked — double-press to unlock
    if (_locked) {
      c = 0;
    } else {
      c = checkDisplayOn(KEY_NEXT);
    }
#elif defined(MECK_PMU_BUTTON)
    // T-Watch S3: the only key is the AXP2101 PWRON, so a short press is the
    // sole button gesture available. It acts as enter/select/confirm (in the
    // contacts screen, that opens the path editor). A >=6s hold is a hardware
    // power-off and never reaches here.
    c = checkDisplayOn(KEY_ENTER);
    if (c && isOnContactsScreen()) {
      ContactsScreen* cs = (ContactsScreen*)getContactsScreen();
      if (cs) {
        int idx = cs->getSelectedContactIdx();
        if (idx >= 0) gotoPathEditor(idx);
      }
      c = 0;   // handled here; do not also pass Enter through
    }
#elif defined(LILYGO_TWATCH_S3_PLUS)
    // T-Watch S3 Plus: on the contacts screen the boot button opens the path
    // editor for the selected contact; elsewhere it wakes the display / KEY_NEXT.
    c = checkDisplayOn(KEY_NEXT);
    if (c && isOnContactsScreen()) {
      ContactsScreen* cs = (ContactsScreen*)getContactsScreen();
      if (cs) {
        int idx = cs->getSelectedContactIdx();
        if (idx >= 0) gotoPathEditor(idx);
      }
      c = 0;
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
    // Dismiss boot hint on any button input (boot button on T5S3)
    if (_hintActive) {
      dismissBootHint();
      c = 0;  // Consume the press
    }
  }

  if (c != 0 && curr) {
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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

#if defined(LILYGO_TWATCH_S3)
  // Alarms are checked every loop, not from the screen's poll(), so one fires
  // with the display off or another screen showing.
  if (watch_alarm_screen) {
    WatchAlarmScreen* wa = (WatchAlarmScreen*)watch_alarm_screen;
    bool fired = wa->tick();
    if (fired || wa->isRinging()) {
      // Hold the screen and the display for the whole ring. Without this the
      // display auto-offs after AUTO_OFF_MILLIS, the buzzing trips the BMA423
      // tilt detector, and raise-to-wake below swaps in the clock screen. It
      // also guarantees the PWR key reaches handleInput() as KEY_ENTER rather
      // than being consumed by checkDisplayOn() as a wake.
      if (curr != watch_alarm_screen) setCurrScreen(watch_alarm_screen);
      if (_display != NULL && !_display->isOn()) _display->turnOn();
      _auto_off = millis() + AUTO_OFF_MILLIS;
      if (fired) _next_refresh = 0;
    }
  }
#endif

if (curr) curr->poll();

#ifdef TWATCH_COMPOSE_ENABLED
  // Channel picker -> channel screen (long-press select) / exit
  if (curr == tw_picker) {
    TWatchChannelPicker* picker = (TWatchChannelPicker*)tw_picker;
    if (picker->isConfirmed()) {
      ((TWatchChannelScreen*)tw_channel)->setSelfName(the_mesh.getNodePrefs()->node_name);
      ((TWatchChannelScreen*)tw_channel)->activate(picker->getSelectedChannelIdx(),
                                                   picker->getSelectedChannelName());
      setCurrScreen(tw_channel);
      picker->acknowledgeConfirm();
    }
    if (picker->wantsExit()) {
      picker->acknowledgeExit();
      gotoHomeScreen();
    }
  }
  // Channel screen -> keyboard (compose bar tap) / exit
  else if (curr == tw_channel) {
    TWatchChannelScreen* cs = (TWatchChannelScreen*)tw_channel;
    if (cs->wantsCompose()) {
      cs->acknowledgeCompose();
      ((TWatchKeyboardScreen*)tw_keyboard)->activate(cs->getChannelIdx(), cs->getChannelName());
      setCurrScreen(tw_keyboard);
    }
    if (cs->wantsExit()) {
      cs->acknowledgeExit();
      gotoHomeScreen();
    }
  }
  // Keyboard -> send on channel (returns to channel screen) / exit
  else if (curr == tw_keyboard) {
    TWatchKeyboardScreen* kb = (TWatchKeyboardScreen*)tw_keyboard;
    if (kb->wantsExit()) {
      kb->acknowledgeExit();
      gotoHomeScreen();
    }
    const char* sendText = nullptr;
    if (kb->consumeSendRequest(&sendText) && sendText) {
      TWatchKeyboardScreen::Purpose purpose = kb->getPurpose();
      int ctxIdx = kb->getContextIdx();
      if (purpose == TWatchKeyboardScreen::TWKB_CHANNEL) {
        ChannelDetails ch;
        if (the_mesh.getChannel(kb->getChannelIdx(), ch)) {
          uint32_t ts = rtc_clock.getCurrentTime();
          the_mesh.sendGroupMessage(ts, ch.channel, the_mesh.getNodeName(),
                                    sendText, strlen(sendText));
          showAlert("Sent!", 800);
          addSentChannelMessage(kb->getChannelIdx(), the_mesh.getNodePrefs()->node_name, sendText);
        }
        kb->clearOutBuf();
        setCurrScreen(tw_channel);
      } else if (purpose == TWatchKeyboardScreen::TWKB_DM) {
        bool dmSuccess = false;
        uint32_t sendRef = 0;
        uint8_t sendTotal = 0;
        if (strlen(sendText) > 0 &&
            the_mesh.uiSendDirectMessage((uint32_t)ctxIdx, sendText, &sendRef, &sendTotal)) {
          ContactInfo dmRecipient;
          if (the_mesh.getContactByIdx(ctxIdx, dmRecipient)) {
            addSentDM(dmRecipient.name, the_mesh.getNodePrefs()->node_name, sendText,
                      sendRef, sendTotal);
          }
          dmSuccess = true;
        }
        kb->clearOutBuf();
        ContactInfo dmContact;
        if (the_mesh.getContactByIdx(ctxIdx, dmContact)) {
          gotoDMConversation(dmContact.name, ctxIdx, 0);
        } else {
          gotoHomeScreen();
        }
        showAlert(dmSuccess ? "DM sent!" : "DM failed!", 1500);
      } else if (purpose == TWatchKeyboardScreen::TWKB_PATH) {
        PathEditorScreen* pe = (PathEditorScreen*)getPathEditorScreen();
        const char* perr = pe ? pe->applyComposedPath(sendText) : "No editor";
        kb->clearOutBuf();
        if (path_editor) setCurrScreen(path_editor);
        else gotoHomeScreen();
        if (perr) showAlert(perr, 1500);
      } else if (purpose == TWatchKeyboardScreen::TWKB_NOTE) {
        WatchNotesScreen* wn = (WatchNotesScreen*)watch_notes_screen;
        const char* nerr = wn ? wn->applyComposedNote(sendText) : "No notes";
        kb->clearOutBuf();
        if (watch_notes_screen) setCurrScreen(watch_notes_screen);
        else gotoHomeScreen();
        if (nerr) showAlert(nerr, 1200);
      } else if (purpose == TWatchKeyboardScreen::TWKB_SCOPE) {
        WatchChannelConfigScreen* wc = (WatchChannelConfigScreen*)watch_channel_cfg_screen;
        const char* serr = wc ? wc->applyComposedScope(sendText) : "No channel cfg";
        kb->clearOutBuf();
        if (watch_channel_cfg_screen) setCurrScreen(watch_channel_cfg_screen);
        else gotoHomeScreen();
        if (serr) showAlert(serr, 1200);
      } else {  // TWKB_ADMIN_PASSWORD or TWKB_ADMIN_CLI
        RepeaterAdminScreen* admin = (RepeaterAdminScreen*)getRepeaterAdminScreen();
        if (admin) {
          for (int i = 0; sendText[i]; i++) admin->handleInput(sendText[i]);
          admin->handleInput('\r');
        }
        kb->clearOutBuf();
        if (repeater_admin) setCurrScreen(repeater_admin);
        else gotoHomeScreen();
      }
    }
  }
  else if (curr == path_editor && path_editor != nullptr) {
    PathEditorScreen* pe = (PathEditorScreen*)path_editor;
    if (pe->wantsKeyboard()) {
      pe->clearWantKeyboard();
      openTWatchKeyboard(TWatchKeyboardScreen::TWKB_PATH, pe->getContactIdx());
    }
  }
  else if (curr == watch_notes_screen && watch_notes_screen != nullptr) {
    WatchNotesScreen* wn = (WatchNotesScreen*)watch_notes_screen;
    if (wn->wantsKeyboard()) {
      wn->clearWantKeyboard();
      openTWatchKeyboard(TWatchKeyboardScreen::TWKB_NOTE, 0);
      if (wn->editPending()) {
        ((TWatchKeyboardScreen*)tw_keyboard)->setInitialText(wn->getEditText());
      }
    }
  }
  else if (curr == watch_channel_cfg_screen && watch_channel_cfg_screen != nullptr) {
    WatchChannelConfigScreen* wc = (WatchChannelConfigScreen*)watch_channel_cfg_screen;
    if (wc->wantsKeyboard()) {
      wc->clearWantKeyboard();
      openTWatchKeyboard(TWatchKeyboardScreen::TWKB_SCOPE, 0);
      ((TWatchKeyboardScreen*)tw_keyboard)->setInitialText(wc->getEditText());
    } else if (wc->wantsExit()) {
      wc->clearWantExit();
      gotoSettingsScreen();
    }
  }
  else if (curr == settings_screen && settings_screen != nullptr) {
    SettingsScreen* ss = (SettingsScreen*)settings_screen;
    if (ss->wantsWatchChannels()) {
      ss->clearWantsWatchChannels();
      if (watch_channel_cfg_screen) {
        ((WatchChannelConfigScreen*)watch_channel_cfg_screen)->enter();
        setCurrScreen(watch_channel_cfg_screen);
      }
    }
  }
#endif

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
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
      if (_node_prefs && display.isDarkMode() != (_node_prefs->dark_mode != 0)) {
        display.setDarkMode(_node_prefs->dark_mode != 0);
      }
#endif
#if defined(LilyGo_T5S3_EPaper_Pro)
      // Sync portrait mode with prefs (T5S3 only)
      if (_node_prefs && display.isPortraitMode() != (_node_prefs->portrait_mode != 0)) {
        display.setPortraitMode(_node_prefs->portrait_mode != 0);
        // Text reader layout depends on orientation -- force recalculation
        if (text_reader) {
          ((TextReaderScreen*)text_reader)->invalidateLayout();
        }
      }
#endif
      // Sync font style with prefs (settings toggle takes effect here)
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
      if (_node_prefs && display.getFontStyle() != _node_prefs->ui_font_style) {
        display.setFontStyle(_node_prefs->ui_font_style);
      }
#endif
      _display->startFrame();
#if defined(LilyGo_T5S3_EPaper_Pro)
      if (_vkbActive) {
        display.setForcePartial(true);  // No flash while typing
        _vkb.render(*_display);
        _next_refresh = millis() + 500;  // Moderate refresh for cursor blink
        // Check if keyboard was submitted or cancelled during render cycle
        if (_vkb.status() == VKB_SUBMITTED) {
          onVKBSubmit();
        } else if (_vkb.status() == VKB_CANCELLED) {
          onVKBCancel();
        }
      } else {
        // Default: allow full refresh. Override for notes editing (no flash while typing).
        display.setForcePartial(false);
        if (isOnNotesScreen() && ((NotesScreen*)notes_screen)->isEditing()) {
          display.setForcePartial(true);
        }
        int delay_millis = curr->render(*_display);

        // Check if settings screen needs VKB for WiFi password entry
#ifdef MECK_WIFI_COMPANION
        if (isOnSettingsScreen() && !_vkbActive) {
          SettingsScreen* ss = (SettingsScreen*)settings_screen;
          if (ss->needsWifiVKB()) {
            ss->clearWifiNeedsVKB();
            showVirtualKeyboard(VKB_WIFI_PASSWORD, "WiFi Password", "", 63);
          }
        }
#endif

        // Check if settings screen needs VKB for text editing (channel name, freq, APN)
        if (isOnSettingsScreen() && !_vkbActive) {
          SettingsScreen* ss = (SettingsScreen*)settings_screen;
          if (ss->needsTextVKB()) {
            ss->clearTextNeedsVKB();
            // Pick a context-appropriate label
            const char* label = "Edit";
            SettingsRowType rt = ss->getCurrentRowType();
            if (rt == ROW_NAME) label = "Node Name";
            else if (rt == ROW_ADD_CHANNEL) label = "Channel Name";
            else if (rt == ROW_FREQ) label = "Frequency";
            showVirtualKeyboard(VKB_SETTINGS_TEXT, label, ss->getEditBuf(), 31);
          }
        }

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
#if defined(LilyGo_T5S3_EPaper_Pro)
          _display->drawTextCentered(cx, startY, "Swipe: Navigate");
          _display->drawTextCentered(cx, startY + lineH, "Tap: Select");
          _display->drawTextCentered(cx, startY + lineH * 2, "Long Press: Action");
          _display->drawTextCentered(cx, startY + lineH * 3, "Boot Btn: Home");
          _display->drawTextCentered(cx, startY + lineH * 4 + 4, "[Tap to dismiss hint]");
#else
          _display->drawTextCentered(cx, startY, "M:Msgs  C:Contacts");
          _display->drawTextCentered(cx, startY + lineH, "S:Settings  E:Reader");
          _display->drawTextCentered(cx, startY + lineH * 2, "N:Notes  W/S:Scroll");
          _display->drawTextCentered(cx, startY + lineH * 3, "A/D:Cycle Left/Right");
          _display->drawTextCentered(cx, startY + lineH * 4 + 4, "[X to dismiss hint]");
#endif
          _next_refresh = _hintExpiry;
        } else if (_hintActive) {
          // Hint expired — auto-dismiss
          dismissBootHint();
          _next_refresh = millis() + 200;
        } else if (millis() < _alert_expiry) {
          _display->setTextSize(1);
          int y = _display->height() / 3;
          int p = _display->height() / 32;
          _display->setColor(DisplayDriver::DARK);
          _display->fillRect(p, y, _display->width() - p*2, y);
          _display->setColor(DisplayDriver::LIGHT);
          _display->drawRect(p, y, _display->width() - p*2, y);
          _display->drawTextCentered(_display->width() / 2, y + p*3, _alert);
          _next_refresh = _alert_expiry;
        } else {
          _next_refresh = millis() + delay_millis;
        }
      }
#else
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
        _display->drawTextCentered(cx, startY + lineH, "S:Settings  E:Reader");
        _display->drawTextCentered(cx, startY + lineH * 2, "N:Notes  W/S:Scroll");
        _display->drawTextCentered(cx, startY + lineH * 3, "A/D:Cycle Left/Right");
        _display->drawTextCentered(cx, startY + lineH * 4 + 4, "[X to dismiss]");
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
#endif
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
#if defined(MECK_TWATCH)
  // Raise-to-wake: a wrist-raise (BMA423 tilt) while the display is off turns
  // it back on showing the clock screen and restarts the idle timer.
  if (_display != NULL && !_display->isOn() && board.tiltFired()) {
    _display->turnOn();
    setCurrScreen(lock_screen);
    _auto_off = millis() + AUTO_OFF_MILLIS;
  }

  // Accumulate steps, roll the day over at local midnight, persist periodically.
  updateSteps();
#endif

  // Auto-lock idle timer — runs regardless of display on/off state
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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
  // T-Deck Pro: every 1 minute. T5S3: every 2 minutes.
  // Wakes the display driver briefly to render, then auto-off handles it.
  // T5S3 standalone: no refreshes once powersaving begins — the device
  // shows "hibernating..." and enters light sleep instead.
#if defined(LilyGo_T5S3_EPaper_Pro) && !defined(BLE_PIN_CODE) && !defined(MECK_WIFI_COMPANION)
  // T5S3 standalone: only refresh while still active (before powersaving kicks in)
  if (_locked && _display != NULL && _display->isOn()) {
    const unsigned long LOCK_REFRESH_INTERVAL = 2UL * 60UL * 1000UL;  // 2 minutes
#elif defined(LilyGo_T5S3_EPaper_Pro)
  // T5S3 BLE/WiFi: refresh every 2 minutes
  if (_locked && _display != NULL) {
    const unsigned long LOCK_REFRESH_INTERVAL = 2UL * 60UL * 1000UL;  // 2 minutes
#elif defined(LilyGo_TDeck_Pro)
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

  // ── T5S3 standalone powersaving ──────────────────────────────────────────
  // When locked with display off, enter ESP32 light sleep (~8 mA total).
  // Radio stays in continuous RX — DIO1 going HIGH wakes the CPU instantly.
  // Boot button (GPIO0 LOW) and a 30-min safety timer also wake.
  // First sleep starts 60s after lock; subsequent cycles wake for 5s to let
  // the mesh stack process/relay any received packet, then sleep again.
#if defined(LilyGo_T5S3_EPaper_Pro) && !defined(BLE_PIN_CODE) && !defined(MECK_WIFI_COMPANION)
  if (_locked && _display != NULL && !_display->isOn()) {
    unsigned long now = millis();
    if (now - _psLastActive >= _psNextSleepSecs * 1000UL) {
      // First sleep entry: render a static "hibernating..." frame on the
      // e-ink. Since e-ink retains its image indefinitely without power,
      // this tells the user the device is in low-power mode until they
      // wake it with the boot button.
      if (_psNextSleepSecs == 60) {
        _display->turnOn();
        _display->startFrame();
        _display->setTextSize(1);
        _display->setColor(DisplayDriver::GREEN);
        _display->drawTextCentered(_display->width() / 2, 34, "hibernating...");
        _display->endFrame();
        delay(700);  // Allow e-ink refresh to complete
        _display->turnOff();
      }
      Serial.println("[POWERSAVE] Entering light sleep (locked+idle)");
      board.sleep(1800);  // Light sleep up to 30 min
      // ── CPU resumes here on wake ──
      unsigned long wakeAt = millis();
      _psLastActive = wakeAt;
      _psNextSleepSecs = 5;  // Stay awake 5s for mesh processing

      esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
      if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        // Boot button pressed — unlock and return to normal use
        Serial.println("[POWERSAVE] Woke by button — unlocking");
        unlockScreen();
        _psNextSleepSecs = 60;  // Reset to long delay after user interaction
      } else if (cause == ESP_SLEEP_WAKEUP_EXT1) {
        Serial.println("[POWERSAVE] Woke by LoRa packet");
      } else if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        Serial.println("[POWERSAVE] Woke by timer");
      }
    }
  } else if (!_locked) {
    // Not locked — keep powersaving timer reset so first sleep is 60s after lock
    _psLastActive = millis();
    _psNextSleepSecs = 60;
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
      #if defined(THINKNODE_M1) || defined(LILYGO_TECHO) || defined(LilyGo_TDeck_Pro)
      if (_display != NULL) {
        _display->startFrame();
        _display->setTextSize(2);
        _display->setColor(DisplayDriver::RED);
        _display->drawTextCentered(_display->width() / 2, 20, "Low Battery.");
        _display->drawTextCentered(_display->width() / 2, 40, "Shutting Down!");
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
}

char UITask::checkDisplayOn(char c) {
  if (_display != NULL) {
    if (!_display->isOn()) {
      _display->turnOn();   // turn display on and consume event
      c = 0;
    }
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 0;  // trigger refresh
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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
#if defined(LilyGo_T5S3_EPaper_Pro)
  else if (_vkbActive) {
    onVKBCancel();  // Long press while VKB → cancel
    c = 0;
  } else if (_locked) {
    unlockScreen();
    c = 0;
  } else {
    lockScreen();
    c = 0;
  }
#endif
  return c;
}

char UITask::handleDoubleClick(char c) {
  MESH_DEBUG_PRINTLN("UITask: double click triggered");
#if defined(LilyGo_T5S3_EPaper_Pro)
  // Double-click boot button → full brightness backlight toggle
  if (board.isBacklightOn()) {
    board.setBacklight(false);
  } else {
    board.setBacklightBrightness(153);
    board.setBacklight(true);
  }
  c = 0;  // consume event — don't pass through as navigation
#elif defined(LilyGo_TDeck_Pro)
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
#if defined(LilyGo_T5S3_EPaper_Pro)
  // Triple-click → half brightness backlight (comfortable reading)
  if (board.isBacklightOn()) {
    board.setBacklight(false);  // If already on, turn off
  } else {
    board.setBacklightBrightness(4);
    board.setBacklight(true);
  }
#else
  toggleBuzzer();
#endif
  c = 0;
  return c;
}

#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
void UITask::lockScreen() {
  if (_locked) return;
  _locked = true;
  _screenBeforeLock = curr;
  setCurrScreen(lock_screen);
  // Ensure display is on so lock screen renders (auto-off may have turned it off)
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
#if defined(LilyGo_T5S3_EPaper_Pro)
  board.setBacklight(false);  // Save power (T5S3 backlight)
#endif
  _next_refresh = 0;  // Draw lock screen immediately
  _auto_off = millis() + 60000;  // 60s before display off while locked
  _lastLockRefresh = millis();   // Start lock screen clock refresh cycle
#if defined(LilyGo_T5S3_EPaper_Pro) && !defined(BLE_PIN_CODE) && !defined(MECK_WIFI_COMPANION)
  _psLastActive = millis();      // Start powersaving countdown (60s to first sleep)
  _psNextSleepSecs = 60;
#endif
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
#endif // LilyGo_T5S3_EPaper_Pro || LilyGo_TDeck_Pro

#if defined(LilyGo_T5S3_EPaper_Pro)
void UITask::showVirtualKeyboard(VKBPurpose purpose, const char* label, const char* initial, int maxLen, int contextIdx) {
  _vkb.open(purpose, label, initial, maxLen, contextIdx);
  _vkbActive = true;
  _vkbOpenedAt = millis();
  _screenBeforeVKB = curr;
  _next_refresh = 0;
  _auto_off = millis() + 120000;  // 2min timeout while typing
  Serial.printf("[UI] VKB opened: %s\n", label);
}

void UITask::onVKBSubmit() {
  _vkbActive = false;
  const char* text = _vkb.getText();
  VKBPurpose purpose = _vkb.purpose();
  int idx = _vkb.contextIdx();

  Serial.printf("[UI] VKB submit: purpose=%d idx=%d text='%s'\n", purpose, idx, text);

  switch (purpose) {
    case VKB_CHANNEL_MSG: {
      if (strlen(text) == 0) break;

      ChannelDetails channel;
      if (the_mesh.getChannel(idx, channel)) {
        uint32_t timestamp = rtc_clock.getCurrentTime();
        int textLen = strlen(text);
        if (the_mesh.sendGroupMessage(timestamp, channel.channel,
                                       the_mesh.getNodePrefs()->node_name,
                                       text, textLen)) {
          addSentChannelMessage(idx, the_mesh.getNodePrefs()->node_name, text);
          the_mesh.queueSentChannelMessage(idx, timestamp,
                                            the_mesh.getNodePrefs()->node_name, text);
          showAlert("Sent!", 1500);
        } else {
          showAlert("Send failed!", 1500);
        }
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_DM: {
      if (strlen(text) == 0) break;

      bool dmSuccess = false;
      uint32_t sendRef = 0;
      uint8_t sendTotal = 0;
      if (the_mesh.uiSendDirectMessage((uint32_t)idx, text, &sendRef, &sendTotal)) {
        // Add to channel screen so sent DM appears in conversation view
        ContactInfo dmRecipient;
        if (the_mesh.getContactByIdx(idx, dmRecipient)) {
          addSentDM(dmRecipient.name, the_mesh.getNodePrefs()->node_name, text,
                    sendRef, sendTotal);
        }
        dmSuccess = true;
      }
      // Return to DM conversation if we have contact info
      ContactInfo dmContact;
      if (the_mesh.getContactByIdx(idx, dmContact)) {
        ChannelScreen* cs = (ChannelScreen*)channel_screen;
        uint8_t savedPerms = (cs && cs->isDMConversation()) ? cs->getDMContactPerms() : 0;
        gotoDMConversation(dmContact.name, idx, savedPerms);
      } else if (_screenBeforeVKB) {
        setCurrScreen(_screenBeforeVKB);
      }
      // Show alert AFTER navigation (setCurrScreen clears prior alerts)
      showAlert(dmSuccess ? "DM sent!" : "DM failed!", 1500);
      break;
    }
    case VKB_ADMIN_PASSWORD: {
      // Feed each character to the admin screen, then Enter
      RepeaterAdminScreen* admin = (RepeaterAdminScreen*)getRepeaterAdminScreen();
      if (admin) {
        for (int i = 0; text[i]; i++) {
          admin->handleInput(text[i]);
        }
        admin->handleInput('\r');
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_ADMIN_CLI: {
      RepeaterAdminScreen* admin = (RepeaterAdminScreen*)getRepeaterAdminScreen();
      if (admin) {
        for (int i = 0; text[i]; i++) {
          admin->handleInput(text[i]);
        }
        admin->handleInput('\r');
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_SETTINGS_NAME: {
      if (strlen(text) > 0) {
        strncpy(_node_prefs->node_name, text, sizeof(_node_prefs->node_name) - 1);
        _node_prefs->node_name[sizeof(_node_prefs->node_name) - 1] = '\0';
        the_mesh.savePrefs();
        showAlert("Name saved", 1000);
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_SETTINGS_TEXT: {
      // Generic settings text edit — copy text back to settings edit buffer
      // and confirm via the normal Enter path (handles name/freq/channel/APN)
      SettingsScreen* ss = (SettingsScreen*)settings_screen;
      if (strlen(text) > 0) {
        ss->submitEditText(text);
      } else {
        // Empty submission — cancel the edit
        ss->handleInput('q');
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_NOTES: {
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
      NotesScreen* notes = (NotesScreen*)getNotesScreen();
      if (notes && strlen(text) > 0) {
        for (int i = 0; text[i]; i++) {
          notes->handleInput(text[i]);
        }
      }
#endif
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
#ifdef MECK_WIFI_COMPANION
    case VKB_WIFI_PASSWORD: {
      SettingsScreen* ss = (SettingsScreen*)settings_screen;
      ss->submitWifiPassword(text);
      if (WiFi.status() == WL_CONNECTED) {
        showAlert("WiFi connected!", 2000);
      } else {
        showAlert("WiFi failed", 2000);
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
#endif
#ifdef MECK_WEB_READER
    case VKB_WEB_URL: {
      WebReaderScreen* wr = (WebReaderScreen*)getWebReaderScreen();
      if (wr && strlen(text) > 0) {
        wr->setUrlText(text);     // Copy text + set _urlEditing = true
        wr->handleInput('\r');    // Triggers auto-prefix + fetch
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_WEB_SEARCH: {
      WebReaderScreen* wr = (WebReaderScreen*)getWebReaderScreen();
      if (wr && strlen(text) > 0) {
        wr->setSearchText(text);  // Copy text + set _searchEditing = true
        wr->handleInput('\r');    // Triggers DDG search URL build + fetch
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_WEB_WIFI_PASS: {
      WebReaderScreen* wr = (WebReaderScreen*)getWebReaderScreen();
      if (wr && strlen(text) > 0) {
        wr->setWifiPassText(text);  // Copy password text
        wr->handleInput('\r');      // Triggers WiFi connect
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_WEB_LINK: {
      WebReaderScreen* wr = (WebReaderScreen*)getWebReaderScreen();
      if (wr && strlen(text) > 0) {
        // Activate link input mode, feed digits, then submit
        wr->handleInput('l');  // Enter link selection mode
        for (int i = 0; text[i]; i++) {
          if (text[i] >= '0' && text[i] <= '9') {
            wr->handleInput(text[i]);
          }
        }
        wr->handleInput('\r');  // Confirm link number → navigate
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
#endif
    case VKB_TEXT_PAGE: {
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
      if (strlen(text) > 0) {
        int pageNum = atoi(text);
        TextReaderScreen* reader = (TextReaderScreen*)getTextReaderScreen();
        if (reader && pageNum > 0) {
          reader->gotoPage(pageNum);
        }
      }
#endif
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
    case VKB_TRACE_PATH: {
      TraceScreen* ts = (TraceScreen*)getTraceScreen();
      if (ts) {
        ts->setTypedPath(text);
      }
      if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
      break;
    }
  }
  _screenBeforeVKB = nullptr;
  _next_refresh = 0;
  display.setForcePartial(false);  // Next frame does full refresh to clear VKB ghosts
  display.invalidateFrameCRC();
}

void UITask::onVKBCancel() {
  _vkbActive = false;
  if (_screenBeforeVKB) setCurrScreen(_screenBeforeVKB);
  _screenBeforeVKB = nullptr;
  _next_refresh = 0;
  display.setForcePartial(false);  // Next frame does full refresh to clear VKB ghosts
  display.invalidateFrameCRC();
  Serial.println("[UI] VKB cancelled");
}

#ifdef MECK_CARDKB
void UITask::feedCardKBChar(char c) {
  if (_vkbActive) {
    // VKB is open — feed character into its text buffer
    if (_vkb.feedChar(c)) {
      _next_refresh = 0;  // Redraw VKB immediately
      _auto_off = millis() + 120000;  // Extend timeout while typing
      // Check if feedChar triggered submit or cancel
      if (_vkb.status() == VKB_SUBMITTED) {
        onVKBSubmit();
      } else if (_vkb.status() == VKB_CANCELLED) {
        onVKBCancel();
      }
    } else {
      // feedChar returned false — nav keys (arrows) while VKB is active
      // Not consumed; could be used for cursor movement in future
    }
  } else {
    // No VKB active — route as normal navigation key
    injectKey(c);
  }
}
#endif
#endif

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
        #if defined(MECK_TWATCH)
          board.gpsPowerOff();  // Watch: GPS power is the AXP2101 BLDO1 rail
        #endif
        notify(UIEventType::ack);
      } else {
        // Enable GPS — power on hardware
        _sensors->setSettingValue("gps", "1");
        _node_prefs->gps_enabled = 1;
        #ifdef PIN_GPS_EN
          digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);
        #endif
        #if defined(LilyGo_TDeck_Pro_Max)
          board.gpsPowerOn();  // MAX: GPS power is XL9555-routed, not PIN_GPS_EN
        #endif
        #if defined(MECK_TWATCH)
          board.gpsPowerOn();  // Watch: GPS power is the AXP2101 BLDO1 rail
        #endif
        notify(UIEventType::ack);
      }
      the_mesh.savePrefs();
      showAlert(_node_prefs->gps_enabled ? "GPS: Enabled" : "GPS: Disabled", 800);
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
    showAlert(buzzer.isQuiet() ? "Buzzer: OFF" : "Buzzer: ON", 800);
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
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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
  return curr == home && ((HomeScreen *) home)->isEditingUTC();
}

bool UITask::isHomeOnRecentPage() const {
  return curr == home && ((HomeScreen *) home)->isOnRecentPage();
}

bool UITask::isHomeOnShutdownPage() const {
  return curr == home && ((HomeScreen *) home)->isOnShutdownPage();
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

#ifdef TWATCH_COMPOSE_ENABLED
void UITask::openTWatchPicker() {
  TWatchChannelPicker* picker = (TWatchChannelPicker*)tw_picker;
  picker->beginChannelSelect();
  ChannelDetails ch;
  for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (the_mesh.getChannel(i, ch) && ch.name[0] != 0) {
      picker->addChannel(i, ch.name);
    }
  }
  setCurrScreen(tw_picker);
}

void UITask::openTWatchKeyboard(int purpose, int contextIdx) {
  ((TWatchKeyboardScreen*)tw_keyboard)->activateFor(
      (TWatchKeyboardScreen::Purpose)purpose, contextIdx);
  setCurrScreen(tw_keyboard);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

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
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
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
#endif
}

void UITask::gotoNotesScreen() {
#if defined(MECK_TWATCH)
  // Watch: the shared NotesScreen is SD-bound; use the LittleFS note pad.
  if (watch_notes_screen) {
    ((WatchNotesScreen*)watch_notes_screen)->enter();
    setCurrScreen(watch_notes_screen);
    if (_display != NULL && !_display->isOn()) _display->turnOn();
    _auto_off = millis() + AUTO_OFF_MILLIS;
    _next_refresh = 100;
    return;
  }
#endif
  if (!notes_screen) return;  // Not available on this platform
#if !defined(LILYGO_TECHO_LITE) && !defined(LILYGO_TECHO_CARD)
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
#endif
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
  smsScr->activate();
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
  
  // Add to channel history with path_len=0 (local message)
  ((ChannelScreen *) channel_screen)->addMessage(channel_idx, 0, sender, formattedMsg);
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
  char name[32] = "Unknown";
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

#if defined(MECK_TWATCH)

// Persisted step state. Lives on the LittleFS "maps" partition; the watches have
// no SD card. There is no clean-shutdown hook to save from -- a >=6s hold on the
// PWR key is a hardware power cut by the AXP2101 -- so saving is periodic.
#define STEPS_FILE           "/steps.dat"
#define STEPS_MAGIC          0x31505453UL   // "STP1"
#define STEPS_VERSION        1
#define STEPS_SAVE_INTERVAL_MS  300000UL    // flush at most every 5 minutes

struct StepStoreV1 {
  uint32_t magic;
  uint8_t  version;
  uint8_t  _pad[3];
  int32_t  lastStepDay;
  uint32_t todaySteps;
  uint32_t history[6];
};

void UITask::loadSteps() {
  File f = LittleFS.open(STEPS_FILE, "r");
  if (!f) return;
  StepStoreV1 st;
  size_t n = f.read((uint8_t*)&st, sizeof(st));
  f.close();
  if (n != sizeof(st) || st.magic != STEPS_MAGIC || st.version != STEPS_VERSION) return;

  _lastStepDay = st.lastStepDay;
  _todaySteps  = st.todaySteps;
  memcpy(_stepHistory, st.history, sizeof(_stepHistory));
}

void UITask::saveSteps() {
  StepStoreV1 st;
  memset(&st, 0, sizeof(st));
  st.magic       = STEPS_MAGIC;
  st.version     = STEPS_VERSION;
  st.lastStepDay = _lastStepDay;
  st.todaySteps  = _todaySteps;
  memcpy(st.history, _stepHistory, sizeof(_stepHistory));

  File f = LittleFS.open(STEPS_FILE, "w");
  if (!f) {
    Serial.println("steps: save FAILED (LittleFS)");
    return;
  }
  f.write((const uint8_t*)&st, sizeof(st));
  f.close();
  _stepsDirtySince = 0;
}

void UITask::shiftStepHistory(uint32_t completedDay) {
  for (int i = 5; i > 0; i--) _stepHistory[i] = _stepHistory[i - 1];
  _stepHistory[0] = completedDay;
}

void UITask::rollStepDays(int32_t newDay) {
  int32_t gap = newDay - _lastStepDay;
  if (gap <= 0) {          // clock corrected backwards: re-anchor, keep history
    _lastStepDay = newDay;
    return;
  }
  shiftStepHistory(_todaySteps);                    // the day that just ended
  for (int32_t d = 1; d < gap && d < 6; d++) {
    shiftStepHistory(0);                            // days the watch was switched off
  }
  _todaySteps  = 0;
  _lastStepDay = newDay;
  saveSteps();
}

void UITask::updateSteps() {
  uint32_t raw = board.getStepCount();

  // Seed from the chip, not from the file: SensorBMA423::begin() re-flashes the
  // feature engine (bma423_write_config_file) on every boot, zeroing the step
  // register. Seeding here is correct whether or not the count survived.
  if (!_stepsSeeded) { _lastRaw = raw; _stepsSeeded = true; }

  uint32_t delta = (raw >= _lastRaw) ? (raw - _lastRaw) : raw;   // raw < last => counter reset
  _lastRaw = raw;

  uint32_t now = rtc_clock.getCurrentTime();
  if (now <= 1700000000) return;   // clock not set: nothing to attribute steps to

  int32_t localDay = ((int32_t)now + ((int32_t)the_mesh.getNodePrefs()->utc_offset_hours * 3600)) / 86400;
  if (_lastStepDay == 0) {
    _lastStepDay = localDay;       // first valid read on a fresh install
  } else if (localDay != _lastStepDay) {
    rollStepDays(localDay);
  }

  if (delta) {
    _todaySteps += delta;
    if (_stepsDirtySince == 0) _stepsDirtySince = millis();
  }
  if (_stepsDirtySince && millis() - _stepsDirtySince >= STEPS_SAVE_INTERVAL_MS) {
    saveSteps();
  }
}

uint32_t UITask::getTodaySteps() {
  return _todaySteps;
}

uint32_t UITask::getStepHistory(int daysAgo) {
  if (daysAgo <= 0) return _todaySteps;
  if (daysAgo > 6)  return 0;
  return _stepHistory[daysAgo - 1];
}

void UITask::gotoStepsScreen() {
  setCurrScreen(steps_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoStepsHistoryScreen() {
  setCurrScreen(steps_history_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

#if defined(LILYGO_TWATCH_S3)
void UITask::gotoWatchAlarmScreen() {
  WatchAlarmScreen* wa = (WatchAlarmScreen*)watch_alarm_screen;
  if (wa) wa->enter();
  setCurrScreen(watch_alarm_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
#endif

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
  if (!map_screen) return;  // Not available on this platform (T-Echo Card)
#if defined(MECK_TWATCH)
  WatchMapScreen* map = (WatchMapScreen*)map_screen;
  if (_display != NULL) {
    map->enter(*_display);
  }
  setCurrScreen(map_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
#elif !defined(LILYGO_TECHO_CARD)
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
#endif
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