#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "NotesScreen.h"
#include "RepeaterAdminScreen.h"
#include "PathEditorScreen.h"
#include "DiscoveryScreen.h"
#include "LastHeardScreen.h"
#ifdef MECK_WEB_READER
  #include "WebReaderScreen.h"
#endif
#if HAS_GPS
  #include "MapScreen.h"
#endif
#include "target.h"
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(MECK_AUDIO_VARIANT)
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

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#elif defined(LilyGo_T5S3_EPaper_Pro)
  #define PRESS_LABEL "long press"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"
#include "ChannelScreen.h"
#include "ContactsScreen.h"
#include "TextReaderScreen.h"
#include "SettingsScreen.h"
#ifdef MECK_AUDIO_VARIANT
#include "AudiobookPlayerScreen.h"
#include "VoiceMessageScreen.h"
#endif
#ifdef HAS_4G_MODEM
  #include "SMSScreen.h"
  #include "ModemManager.h"
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
    SHUTDOWN,
    Count    // keep as last
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  bool _shutdown_init;
  unsigned long _shutdown_at;   // earliest time to proceed with shutdown (after e-ink refresh)
  bool _editing_utc;
  int8_t _saved_utc_offset;  // for cancel/undo

  AdvertPath recent[UI_RECENT_LIST_SIZE];


void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts, int* outIconX = nullptr) {
    // Use voltage-based estimation to match BLE app readings
    uint8_t batteryPercentage = 0;
    if (batteryMilliVolts > 0) {
      const int minMilliVolts = 3000;
      const int maxMilliVolts = 4200;
      int pct = ((batteryMilliVolts - minMilliVolts) * 100) / (maxMilliVolts - minMilliVolts);
      if (pct < 0) pct = 0;
      if (pct > 100) pct = 100;
      batteryPercentage = (uint8_t)pct;
    }

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

    if (_node_prefs->large_font) {
      // Large font: text only — no room for icon in header
      int textX = display.width() - textWidth - 2;
      if (outIconX) *outIconX = textX;
      display.setCursor(textX, textY);
      display.print(pctStr);
    } else {
      // Tiny font: icon + text
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
       _shutdown_init(false), _shutdown_at(0), _editing_utc(false), _saved_utc_offset(0), sensors_lpp(200) {  }

  bool isEditingUTC() const { return _editing_utc; }
  bool isOnRecentPage() const { return _page == HomePage::RECENT; }
  void cancelEditing() { 
    if (_editing_utc) {
      _node_prefs->utc_offset_hours = _saved_utc_offset;
      _editing_utc = false;
    }
  }

  void poll() override {
    if (_shutdown_init && millis() >= _shutdown_at && !_task->isButtonPressed()) {
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
#if defined(LilyGo_T5S3_EPaper_Pro)
    _task->setHomeShowingTiles(false);  // Reset — only set true on FIRST page
#endif
    // node name (tinyfont to avoid overlapping clock)
    display.setTextSize(_node_prefs->smallTextSize());
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
#if defined(LilyGo_T5S3_EPaper_Pro)
    // T5S3: FreeSans12pt ascenders need more room than built-in font.
    // Shift header elements down by 4 virtual units (~17px physical).
    #define HOME_HDR_Y 1
#else
    #define HOME_HDR_Y -3
#endif
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
#if defined(LilyGo_T5S3_EPaper_Pro)
    int y = 14;  // Closer to header
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
#if defined(LilyGo_T5S3_EPaper_Pro)
      _task->setHomeShowingTiles(true);
#endif
#if defined(LilyGo_T5S3_EPaper_Pro)
  #if defined(BLE_PIN_CODE) || defined(WIFI_SSID) || defined(MECK_WIFI_COMPANION)
      int y = 18;  // Tighter spacing — connectivity info fills gap below dots
  #else
      int y = 26;  // Standalone: extra line below dots (no IP/Connected row)
  #endif
#else
      int y = 20;
#endif
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getUnreadMsgCount());
      display.drawTextCentered(display.width() / 2, y, tmp);
      y += 14;  // Reduced from 18

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
        y += 18;
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
        const int tileH = 28;
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
            int iconY = ty + 4;
            display.drawXbm(iconX, iconY, tiles[row][col].icon, HOME_ICON_W, HOME_ICON_H);

            // Label centered below icon
            display.setTextSize(_node_prefs->smallTextSize());
            display.drawTextCentered(tx + tileW / 2, ty + 18, tiles[row][col].label);
          }
        }

        // Nav hint below grid
        y = gridY + 2 * tileH + gapY + 2;
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(_node_prefs->smallTextSize());
        display.drawTextCentered(display.width() / 2, y, "Tap tile to open");
      }
      display.setTextSize(1);

#else
      // ----- T-Deck Pro: Keyboard shortcut text menu -----
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(_node_prefs->smallTextSize());
      int menuLH = _node_prefs->smallLineH();

      if (_node_prefs->large_font) {
        // Proportional font: two-column layout with fixed X positions
        y += 2;
        int col1 = 2;
        int col2 = display.width() / 2;

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
#if defined(HAS_4G_MODEM) && defined(MECK_WEB_READER)
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
        y += menuLH + 2;
      } else {
        // Monospaced built-in font: centered space-padded strings
        y += 6;
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
#if defined(HAS_4G_MODEM) && defined(MECK_WEB_READER)
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
        y += 14;
      }

      // Nav hint (only if room)
      if (y < display.height() - 14) {
        display.setColor(DisplayDriver::GREEN);
        display.drawTextCentered(display.width() / 2, y,
          _node_prefs->large_font ? "A/D: cycle views" : "Press A/D to cycle home views");
      }
      display.setTextSize(1);  // restore
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
        int max_name_width = display.width() - timestamp_width - 1;
        
        char filtered_recent_name[sizeof(a->name)];
        display.translateUTF8ToBlocks(filtered_recent_name, a->name, sizeof(filtered_recent_name));
        display.drawTextEllipsized(0, y, max_name_width, filtered_recent_name);
        display.setCursor(display.width() - timestamp_width - 1, y);
        display.print(tmp);
      }
      // Hint for full Last Heard screen
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(_node_prefs->smallTextSize());
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(display.width() / 2, display.height() - 24,
                               "Tap here for full Last Heard list");
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
#else
      display.drawTextCentered(display.width() / 2, 72, "toggle: " PRESS_LABEL);
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
#else
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
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
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "sat");
        sprintf(buf, "%d", nmea->satellitesCount());
        display.drawTextRightAlign(display.width()-1, y, buf);
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
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;

        display.drawTextLeftAlign(0, y, "pos");
        sprintf(buf, "%.4f %.4f", 
          nmea->getLatitude()/1000000., nmea->getLongitude()/1000000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
        y = y + 12;
        display.drawTextLeftAlign(0, y, "alt");
        sprintf(buf, "%.2f", nmea->getAltitude()/1000.);
        display.drawTextRightAlign(display.width()-1, y, buf);
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
          display.drawTextRightAlign(display.width()-1, y, buf);
        } else {
          display.drawTextLeftAlign(0, y, "time(U)");
          display.drawTextRightAlign(display.width()-1, y, "no sync");
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
      display.drawTextRightAlign(display.width()-1, y, buf);
      y += 10;

      // Average current
      int16_t avgCur = board.getAvgCurrent();
      display.drawTextLeftAlign(0, y, "avg current");
      sprintf(buf, "%d mA", avgCur);
      display.drawTextRightAlign(display.width()-1, y, buf);
      y += 10;

      // Average power
      int16_t avgPow = board.getAvgPower();
      display.drawTextLeftAlign(0, y, "avg power");
      sprintf(buf, "%d mW", avgPow);
      display.drawTextRightAlign(display.width()-1, y, buf);
      y += 10;

      // Voltage (already available)
      uint16_t mv = board.getBattMilliVolts();
      display.drawTextLeftAlign(0, y, "voltage");
      sprintf(buf, "%d.%03d V", mv / 1000, mv % 1000);
      display.drawTextRightAlign(display.width()-1, y, buf);
      y += 10;

      // Remaining capacity (clamped to design capacity — gauge FCC may be
      // stale from factory defaults until a full charge cycle re-learns it)
      uint16_t remCap = board.getRemainingCapacity();
      uint16_t desCap = board.getDesignCapacity();
      if (desCap > 0 && remCap > desCap) remCap = desCap;
      display.drawTextLeftAlign(0, y, "remaining cap");
      sprintf(buf, "%d mAh", remCap);
      display.drawTextRightAlign(display.width()-1, y, buf);
      y += 10;

      // Battery temperature
      int16_t battTemp = board.getBattTemperature();
      display.drawTextLeftAlign(0, y, "temperature");
      sprintf(buf, "%d.%d C", battTemp / 10, abs(battTemp % 10));
      display.drawTextRightAlign(display.width()-1, y, buf);
#endif
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (_shutdown_init) {
#if defined(LilyGo_T5S3_EPaper_Pro)
        board.setBacklight(false);  // Turn off backlight on hibernate
#endif
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.drawXbm((display.width() - 32) / 2, 28, power_icon, 32, 32);
#else
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
#endif
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.drawTextCentered(display.width() / 2, 64, "hibernate:" PRESS_LABEL);
#else
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
#endif
      }
    }
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

    if (c == KEY_LEFT || c == KEY_PREV) {
      _page = (_page + HomePage::Count - 1) % HomePage::Count;
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
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
    if (c == KEY_ENTER && _page == HomePage::SHUTDOWN) {
      _shutdown_init = true;
      _shutdown_at = millis() + 900;  // allow e-ink refresh (644ms) before shutdown
      return true;
    }
    return false;
  }
};

class MsgPreviewScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;

  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  #define MAX_UNREAD_MSGS   32
  int num_unread;
  MsgEntry unread[MAX_UNREAD_MSGS];

public:
  MsgPreviewScreen(UITask* task, mesh::RTCClock* rtc) : _task(task), _rtc(rtc) { num_unread = 0; }

  void addPreview(uint8_t path_len, const char* from_name, const char* msg) {
    if (num_unread >= MAX_UNREAD_MSGS) return;  // full

    auto p = &unread[num_unread++];
    p->timestamp = _rtc->getCurrentTime();
    if (path_len == 0xFF) {
      sprintf(p->origin, "(D) %s:", from_name);
    } else {
      sprintf(p->origin, "(%d) %s:", (uint32_t) path_len, from_name);
    }
    StrHelper::strncpy(p->msg, msg, sizeof(p->msg));
  }

  int render(DisplayDriver& display) override {
    char tmp[16];
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    sprintf(tmp, "Unread: %d", num_unread);
    display.print(tmp);

    auto p = &unread[0];

    int secs = _rtc->getCurrentTime() - p->timestamp;
    if (secs < 60) {
      sprintf(tmp, "%ds", secs);
    } else if (secs < 60*60) {
      sprintf(tmp, "%dm", secs / 60);
    } else {
      sprintf(tmp, "%dh", secs / (60*60));
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);  // horiz line

    display.setCursor(0, 14);
    display.setColor(DisplayDriver::YELLOW);
    char filtered_origin[sizeof(p->origin)];
    display.translateUTF8ToBlocks(filtered_origin, p->origin, sizeof(filtered_origin));
    display.print(filtered_origin);

    display.setCursor(0, 25);
    display.setColor(DisplayDriver::LIGHT);
    char filtered_msg[sizeof(p->msg)];
    display.translateUTF8ToBlocks(filtered_msg, p->msg, sizeof(filtered_msg));
    display.printWordWrap(filtered_msg, display.width());

#if AUTO_OFF_MILLIS==0 // probably e-ink
    return 10000; // 10 s
#else
    return 1000;  // next render after 1000 ms
#endif
  }

  bool handleInput(char c) override {
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      num_unread--;
      if (num_unread == 0) {
        _task->gotoHomeScreen();
      } else {
        // delete first/curr item from unread queue
        for (int i = 0; i < num_unread; i++) {
          unread[i] = unread[i + 1];
        }
      }
      return true;
    }
    if (c == KEY_ENTER) {
      num_unread = 0;  // clear unread queue
      _task->gotoHomeScreen();
      return true;
    }
    return false;
  }
};

// ==========================================================================
// Lock Screen — T5S3 and T-Deck Pro
// Big clock, battery %, unread message count.
// T5S3: Long press boot button to lock/unlock. Touch disabled while locked.
// T-Deck Pro: Double-press boot button to lock/unlock. Touch+keyboard disabled.
// ==========================================================================
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
      uint16_t mv = _task->getBattMilliVolts();
      int pct = 0;
      if (mv > 0) {
        pct = ((mv - 3000) * 100) / (4200 - 3000);
        if (pct < 0) pct = 0;
        if (pct > 100) pct = 100;
      }

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

#if defined(PIN_USER_BTN)
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
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  channel_screen = new ChannelScreen(this, &rtc_clock);
  ((ChannelScreen*)channel_screen)->setDMUnreadPtr(_dmUnread);
  contacts_screen = new ContactsScreen(this, &rtc_clock);
  ((ContactsScreen*)contacts_screen)->setDMUnreadPtr(_dmUnread);
  text_reader = new TextReaderScreen(this, node_prefs);
  notes_screen = new NotesScreen(this, node_prefs);
  settings_screen = new SettingsScreen(this, &rtc_clock, node_prefs);
  repeater_admin = nullptr;  // Lazy-initialized on first use to preserve heap for audio
  path_editor = nullptr;     // Lazy-initialized on first use from contacts screen
  discovery_screen = new DiscoveryScreen(this, &rtc_clock);
  last_heard_screen = new LastHeardScreen(&rtc_clock);
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
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
#if HAS_GPS
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
  if (_node_prefs->dark_mode) {
    ::display.setDarkMode(true);
  }

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
  if (msgcount == 0 && curr == msg_preview) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount,
                    const uint8_t* path, int8_t snr) {
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

  // Add to preview screen (for notifications on non-keyboard devices)
  ((MsgPreviewScreen *) msg_preview)->addPreview(path_len, from_name, text);
  
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
      ((ChannelScreen *) channel_screen)->addMessage(channel_idx, path_len, from_name, text, path, snr, from_name);
    } else {
      // Regular DM: prefix with sender name
      char dmFormatted[CHANNEL_MSG_TEXT_LEN];
      snprintf(dmFormatted, sizeof(dmFormatted), "%s: %s", from_name, text);
      ((ChannelScreen *) channel_screen)->addMessage(channel_idx, path_len, from_name, dmFormatted, path, snr);
    }
  } else {
    ((ChannelScreen *) channel_screen)->addMessage(channel_idx, path_len, from_name, text, path, snr);
  }
  
  // If user is currently viewing this channel, mark it as read immediately
  // (they can see the message arrive in real-time)
  if (isOnChannelScreen() && 
      ((ChannelScreen *) channel_screen)->getViewChannelIdx() == channel_idx) {
    ((ChannelScreen *) channel_screen)->markChannelRead(channel_idx);
  }

  // Per-contact DM unread tracking: find contact index by name
  if (channel_idx == 0xFF && _dmUnread) {
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo contact;
    for (uint32_t ci = 0; ci < numContacts; ci++) {
      if (the_mesh.getContactByIdx(ci, contact) && strcmp(contact.name, from_name) == 0) {
        if (_dmUnread[ci] < 255) _dmUnread[ci]++;
        break;
      }
    }
  }
  
#if defined(LilyGo_TDeck_Pro) || defined(LilyGo_T5S3_EPaper_Pro)
  // Don't interrupt user with popup - just show brief notification
  // Messages are stored in channel history, accessible via tile/key
  // Suppress toasts for room server messages (bulk sync would spam toasts)
  if (!isOnRepeaterAdmin() && !isRoomMsg) {
    char alertBuf[40];
    snprintf(alertBuf, sizeof(alertBuf), "New: %s", from_name);
    showAlert(alertBuf, 2000);
  }
#else
  // Other devices: Show full preview screen (legacy behavior, skip room sync)
  if (!isRoomMsg) setCurrScreen(msg_preview);
#endif

  if (_display != NULL) {
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

  // Keyboard flash notification (suppress for room sync)
#ifdef KB_BL_PIN
  if (_node_prefs->kb_flash_notify && !isRoomMsg) {
    digitalWrite(KB_BL_PIN, HIGH);
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
      }
    }
    #endif

    // Power off LoRa radio, display, and board
    radio_driver.powerOff();
    _display->turnOff();
    _board->powerOff();
  }
}

bool UITask::isButtonPressed() const {
#ifdef PIN_USER_BTN
  return user_btn.isPressed();
#else
  return false;
#endif
}

void UITask::loop() {
  char c = 0;
#if UI_HAS_JOYSTICK
  int ev = user_btn.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_ENTER);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_ENTER);  // REVISIT: could be mapped to different key code
  }
  ev = joystick_left.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_LEFT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_LEFT);
  }
  ev = joystick_right.check();
  if (ev == BUTTON_EVENT_CLICK) {
    c = checkDisplayOn(KEY_RIGHT);
  } else if (ev == BUTTON_EVENT_LONG_PRESS) {
    c = handleLongPress(KEY_RIGHT);
  }
  ev = back_btn.check();
  if (ev == BUTTON_EVENT_TRIPLE_CLICK) {
    c = handleTripleClick(KEY_SELECT);
  }
#elif defined(PIN_USER_BTN)
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
      digitalWrite(KB_BL_PIN, LOW);
    }
  #else
    digitalWrite(KB_BL_PIN, LOW);
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
        digitalWrite(KB_BL_PIN, LOW);
      }
      _callFlashState = false;
    }

    // Rapid LED flash while ringing (if kb_flash_notify is ON)
    if (_incomingCallRinging && _node_prefs->kb_flash_notify) {
      unsigned long now = millis();
      if (now >= _nextCallFlash) {
        _callFlashState = !_callFlashState;
        digitalWrite(KB_BL_PIN, _callFlashState ? HIGH : LOW);
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
      // Sync dark mode with prefs (settings toggle takes effect here)
      if (_node_prefs && display.isDarkMode() != (_node_prefs->dark_mode != 0)) {
        display.setDarkMode(_node_prefs->dark_mode != 0);
      }
#if defined(LilyGo_T5S3_EPaper_Pro)
      // Sync portrait mode with prefs (T5S3 only)
      if (_node_prefs && display.isPortraitMode() != (_node_prefs->portrait_mode != 0)) {
        display.setPortraitMode(_node_prefs->portrait_mode != 0);
        // Text reader layout depends on orientation — force recalculation
        if (text_reader) {
          ((TextReaderScreen*)text_reader)->invalidateLayout();
        }
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

      // E-ink render throttle: enforce minimum 800ms between renders.
      // Each partial update blocks for ~644ms. Without this floor, incoming
      // mesh notifications can trigger back-to-back renders that starve the
      // keyboard polling loop, causing TCA8418 FIFO overflow and lost keys.
      unsigned long minNext = millis() + 800;
      if (_next_refresh < minNext) _next_refresh = minNext;
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

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

  // Lock screen clock refresh — update time display every 15 minutes.
  // Runs outside the _display->isOn() gate so it works even after auto-off.
  // Wakes the display briefly to render, then lets auto-off turn it back off.
  if (_locked && _display != NULL) {
    const unsigned long LOCK_REFRESH_INTERVAL = 15UL * 60UL * 1000UL;  // 15 minutes
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
  _lastLockRefresh = millis();   // Start 15-min clock refresh cycle
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
      if (the_mesh.uiSendDirectMessage((uint32_t)idx, text)) {
        // Add to channel screen so sent DM appears in conversation view
        ContactInfo dmRecipient;
        if (the_mesh.getContactByIdx(idx, dmRecipient)) {
          addSentDM(dmRecipient.name, the_mesh.getNodePrefs()->node_name, text);
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
      NotesScreen* notes = (NotesScreen*)getNotesScreen();
      if (notes && strlen(text) > 0) {
        for (int i = 0; text[i]; i++) {
          notes->handleInput(text[i]);
        }
      }
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
      if (strlen(text) > 0) {
        int pageNum = atoi(text);
        TextReaderScreen* reader = (TextReaderScreen*)getTextReaderScreen();
        if (reader && pageNum > 0) {
          reader->gotoPage(pageNum);
        }
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
        notify(UIEventType::ack);
      } else {
        // Enable GPS — power on hardware
        _sensors->setSettingValue("gps", "1");
        _node_prefs->gps_enabled = 1;
        #ifdef PIN_GPS_EN
          digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);
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
    } else {
      _next_refresh = 100;  // trigger refresh
    }
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

void UITask::gotoChannelScreen() {
  ChannelScreen* cs = (ChannelScreen*)channel_screen;
  // If currently showing DM view, reset to channel 0
  if (cs->getViewChannelIdx() == 0xFF) {
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

void UITask::addSentDM(const char* recipientName, const char* sender, const char* text) {
  // Format as "Sender: message" and tag with recipient's peer hash
  char formattedMsg[CHANNEL_MSG_TEXT_LEN];
  snprintf(formattedMsg, sizeof(formattedMsg), "%s: %s", sender, text);
  ((ChannelScreen *) channel_screen)->addMessage(0xFF, 0, sender, formattedMsg,
                                                  nullptr, 0, recipientName);
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