#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "NotesScreen.h"
#include "target.h"
#include "GPSDutyCycle.h"
#ifdef WIFI_SSID
  #include <WiFi.h>
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
  #define UI_RECENT_LIST_SIZE 4
#endif

#if UI_HAS_JOYSTICK
  #define PRESS_LABEL "press Enter"
#else
  #define PRESS_LABEL "long press"
#endif

#include "icons.h"
#include "ChannelScreen.h"
#include "ContactsScreen.h"
#include "TextReaderScreen.h"
#include "SettingsScreen.h"
#include "RepeaterAdminScreen.h"

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
  bool _editing_utc;
  int8_t _saved_utc_offset;  // for cancel/undo
  AdvertPath recent[UI_RECENT_LIST_SIZE];


void renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts, int* outIconX = nullptr) {
    uint8_t batteryPercentage = 0;
#if HAS_BQ27220
    // Use fuel gauge SOC directly — accurate across the full discharge curve
    batteryPercentage = board.getBatteryPercent();
#else
    // Fallback: voltage-based linear estimation for boards without fuel gauge
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

    // battery icon dimensions (smaller to match tiny percentage text)
    int iconWidth = 16;
    int iconHeight = 6;

    // measure percentage text width to position icon + text together at right edge
    display.setTextSize(0);
    char pctStr[5];
    sprintf(pctStr, "%d%%", batteryPercentage);
    uint16_t textWidth = display.getTextWidth(pctStr);

    // layout: [icon 16px][cap 2px][gap 2px][text][margin 2px]
    int totalWidth = iconWidth + 2 + 2 + textWidth + 2;
    int iconX = display.width() - totalWidth;
    int iconY = 0;  // vertically align with node name text

    // battery outline
    display.drawRect(iconX, iconY, iconWidth, iconHeight);

    // battery "cap"
    display.fillRect(iconX + iconWidth, iconY + (iconHeight / 4), 2, iconHeight / 2);

    // fill the battery based on the percentage
    int fillWidth = (batteryPercentage * (iconWidth - 4)) / 100;
    display.fillRect(iconX + 2, iconY + 2, fillWidth, iconHeight - 4);

    // draw percentage text after the battery cap, offset upward to center with icon
    // (setCursor adds +5 internally for baseline, so compensate for the tiny font)
    int textX = iconX + iconWidth + 2 + 2;  // after cap + gap
    int textY = iconY - 3;  // offset up to vertically center with icon
    display.setCursor(textX, textY);
    display.print(pctStr);
    display.setTextSize(1);  // restore default text size
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
       _shutdown_init(false), _editing_utc(false), _saved_utc_offset(0), sensors_lpp(200) {  }

  bool isEditingUTC() const { return _editing_utc; }
  void cancelEditUTC() { 
    if (_editing_utc) {
      _node_prefs->utc_offset_hours = _saved_utc_offset;
      _editing_utc = false;
    }
  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      _task->shutdown();
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
    // node name (tinyfont to avoid overlapping clock)
    display.setTextSize(0);
    display.setColor(DisplayDriver::GREEN);
    char filtered_name[sizeof(_node_prefs->node_name)];
    display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
    display.setCursor(0, -3);
    display.print(filtered_name);

    // battery voltage
    renderBatteryIndicator(display, _task->getBattMilliVolts());

    // centered clock (tinyfont) - only show when time is valid
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

        display.setTextSize(0);  // tinyfont
        display.setColor(DisplayDriver::LIGHT);
        uint16_t tw = display.getTextWidth(timeBuf);
        int clockX = (display.width() - tw) / 2;
        display.setCursor(clockX, -3);  // align with battery text Y
        display.print(timeBuf);
        display.setTextSize(1);  // restore
      }
    }
    // curr page indicator
    int y = 14;
    int x = display.width() / 2 - 5 * (HomePage::Count-1);
    for (uint8_t i = 0; i < HomePage::Count; i++, x += 10) {
      if (i == _page) {
        display.fillRect(x-1, y-1, 3, 3);
      } else {
        display.fillRect(x, y, 1, 1);
      }
    }

    if (_page == HomePage::FIRST) {
      int y = 20;
      display.setColor(DisplayDriver::YELLOW);
      display.setTextSize(2);
      sprintf(tmp, "MSG: %d", _task->getMsgCount());
      display.drawTextCentered(display.width() / 2, y, tmp);
      y += 18;

      #ifdef WIFI_SSID
        IPAddress ip = WiFi.localIP();
        snprintf(tmp, sizeof(tmp), "IP: %d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, y, tmp);
        y += 12;
      #endif
      #if defined(BLE_PIN_CODE) || defined(WIFI_SSID)
      if (_task->hasConnection()) {
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(1);
        display.drawTextCentered(display.width() / 2, y, "< Connected >");
        y += 12;
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

      // Menu shortcuts - tinyfont monospaced grid
      y += 6;
      display.setColor(DisplayDriver::LIGHT);
      display.setTextSize(0);  // tinyfont 6x8 monospaced
      display.drawTextCentered(display.width() / 2, y, "Press:");
      y += 12;
      display.drawTextCentered(display.width() / 2, y, "[M] Messages  [C] Contacts");
      y += 10;
      display.drawTextCentered(display.width() / 2, y, "[N] Notes     [S] Settings");
      y += 10;
      display.drawTextCentered(display.width() / 2, y, "[E] Reader                ");
      y += 14;

      // Nav hint
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(display.width() / 2, y, "Press A/D to cycle home views");
      display.setTextSize(1);  // restore
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
      display.drawXbm((display.width() - 32) / 2, 18,
          _task->isSerialEnabled() ? bluetooth_on : bluetooth_off,
          32, 32);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, 64 - 11, "toggle: " PRESS_LABEL);
#endif
    } else if (_page == HomePage::ADVERT) {
      display.setColor(DisplayDriver::GREEN);
      display.drawXbm((display.width() - 32) / 2, 18, advert_icon, 32, 32);
      display.drawTextCentered(display.width() / 2, 64 - 11, "advert: " PRESS_LABEL);
#if ENV_INCLUDE_GPS == 1
    } else if (_page == HomePage::GPS) {
      extern GPSDutyCycle gpsDuty;
      extern GPSStreamCounter gpsStream;
      LocationProvider* nmea = sensors.getLocationProvider();
      char buf[50];
      int y = 18;

      // GPS state line with duty cycle info
      if (!_node_prefs->gps_enabled) {
        strcpy(buf, "gps off");
      } else {
        switch (gpsDuty.getState()) {
          case GPSDutyState::ACQUIRING: {
            uint32_t elapsed = gpsDuty.acquireElapsedSecs();
            sprintf(buf, "acquiring %us", (unsigned)elapsed);
            break;
          }
          case GPSDutyState::SLEEPING: {
            uint32_t remain = gpsDuty.sleepRemainingSecs();
            if (remain >= 60) {
              sprintf(buf, "sleep %um%02us", (unsigned)(remain / 60), (unsigned)(remain % 60));
            } else {
              sprintf(buf, "sleep %us", (unsigned)remain);
            }
            break;
          }
          default:
            strcpy(buf, "gps off");
        }
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

        // NMEA sentence counter ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â confirms baud rate and data flow
        display.drawTextLeftAlign(0, y, "sentences");
        if (gpsDuty.isHardwareOn()) {
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
        display.setTextSize(0);
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
#endif
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(DisplayDriver::GREEN);
      display.setTextSize(1);
      if (_shutdown_init) {
        display.drawTextCentered(display.width() / 2, 34, "hibernating...");
      } else {
        display.drawXbm((display.width() - 32) / 2, 18, power_icon, 32, 32);
        display.drawTextCentered(display.width() / 2, 64 - 11, "hibernate:" PRESS_LABEL);
      }
    }
    return _editing_utc ? 700 : 5000;   // match e-ink refresh cycle while editing UTC
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
      _shutdown_init = true;  // need to wait for button to be released
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

  ui_started_at = millis();
  _alert_expiry = 0;

  splash = new SplashScreen(this);
  home = new HomeScreen(this, &rtc_clock, sensors, node_prefs);
  msg_preview = new MsgPreviewScreen(this, &rtc_clock);
  channel_screen = new ChannelScreen(this, &rtc_clock);
  contacts_screen = new ContactsScreen(this, &rtc_clock);
  text_reader = new TextReaderScreen(this);
  notes_screen = new NotesScreen(this);
  settings_screen = new SettingsScreen(this, &rtc_clock, node_prefs);
  repeater_admin = new RepeaterAdminScreen(this, &rtc_clock);
  audiobook_screen = nullptr;  // Created and assigned from main.cpp if audio hardware present
  setCurrScreen(splash);
}

void UITask::showAlert(const char* text, int duration_millis) {
  strcpy(_alert, text);
  _alert_expiry = millis() + duration_millis;
  _next_refresh = millis() + 100;  // trigger re-render to show updated text
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
  if (msgcount == 0) {
    gotoHomeScreen();
  }
}

void UITask::newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount) {
  _msgcount = msgcount;

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
  
  // Add to channel history screen with channel index
  ((ChannelScreen *) channel_screen)->addMessage(channel_idx, path_len, from_name, text);
  
#if defined(LilyGo_TDeck_Pro)
  // T-Deck Pro: Don't interrupt user with popup - just show brief notification
  // Messages are stored in channel history, accessible via 'M' key
  char alertBuf[40];
  snprintf(alertBuf, sizeof(alertBuf), "New: %s", from_name);
  showAlert(alertBuf, 2000);
#else
  // Other devices: Show full preview screen (legacy behavior)
  setCurrScreen(msg_preview);
#endif

  if (_display != NULL) {
    if (!_display->isOn() && !hasConnection()) {
      _display->turnOn();
    }
    if (_display->isOn()) {
    _auto_off = millis() + AUTO_OFF_MILLIS;  // extend the auto-off timer
    _next_refresh = 100;  // trigger refresh
    }
  }

  // Keyboard flash notification
#ifdef KB_BL_PIN
  if (_node_prefs->kb_flash_notify) {
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
    _display->turnOff();
    radio_driver.powerOff();
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
    c = checkDisplayOn(KEY_NEXT);
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
    curr->handleInput(c);
    _auto_off = millis() + AUTO_OFF_MILLIS;   // extend auto-off timer
    _next_refresh = 100;  // trigger refresh
  }

  userLedHandler();

  // Turn off keyboard flash after timeout
#ifdef KB_BL_PIN
  if (_kb_flash_off_at && millis() >= _kb_flash_off_at) {
    digitalWrite(KB_BL_PIN, LOW);
    _kb_flash_off_at = 0;
  }
#endif

#ifdef PIN_BUZZER
  if (buzzer.isPlaying())  buzzer.loop();
#endif

if (curr) curr->poll();

  if (_display != NULL && _display->isOn()) {
    if (millis() >= _next_refresh && curr) {
      _display->startFrame();
      int delay_millis = curr->render(*_display);
      if (millis() < _alert_expiry) {  // render alert popup
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
    }
#if AUTO_OFF_MILLIS > 0
    if (millis() > _auto_off) {
      _display->turnOff();
    }
#endif
  }

#ifdef PIN_VIBRATION
  vibration.loop();
#endif

#ifdef AUTO_SHUTDOWN_MILLIVOLTS
  if (millis() > next_batt_chck) {
    uint16_t milliVolts = getBattMilliVolts();
    if (milliVolts > 0 && milliVolts < AUTO_SHUTDOWN_MILLIVOLTS) {

      // show low battery shutdown alert
      // we should only do this for eink displays, which will persist after power loss
      #if defined(THINKNODE_M1) || defined(LILYGO_TECHO)
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

bool UITask::getGPSState() {
  #if ENV_INCLUDE_GPS == 1
    return _node_prefs != NULL && _node_prefs->gps_enabled;
  #else
    return false;
  #endif
}

void UITask::toggleGPS() {
  #if ENV_INCLUDE_GPS == 1
    extern GPSDutyCycle gpsDuty;

    if (_sensors != NULL) {
      if (_node_prefs->gps_enabled) {
        // Disable GPS ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â cut hardware power
        _sensors->setSettingValue("gps", "0");
        _node_prefs->gps_enabled = 0;
        gpsDuty.disable();
        notify(UIEventType::ack);
      } else {
        // Enable GPS ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â start duty cycle
        _sensors->setSettingValue("gps", "1");
        _node_prefs->gps_enabled = 1;
        gpsDuty.enable();
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
  ((HomeScreen *) home)->cancelEditUTC();
  setCurrScreen(home);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

bool UITask::isEditingHomeScreen() const {
  return curr == home && ((HomeScreen *) home)->isEditingUTC();
}

void UITask::gotoChannelScreen() {
  ((ChannelScreen *) channel_screen)->resetScroll();
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
  if (audiobook_screen == nullptr) return;  // No audio hardware
  setCurrScreen(audiobook_screen);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}

void UITask::gotoRepeaterAdmin(int contactIdx) {
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

uint8_t UITask::getChannelScreenViewIdx() const {
  return ((ChannelScreen *) channel_screen)->getViewChannelIdx();
}

void UITask::addSentChannelMessage(uint8_t channel_idx, const char* sender, const char* text) {
  // Format the message as "Sender: message"
  char formattedMsg[CHANNEL_MSG_TEXT_LEN];
  snprintf(formattedMsg, sizeof(formattedMsg), "%s: %s", sender, text);
  
  // Add to channel history with path_len=0 (local message)
  ((ChannelScreen *) channel_screen)->addMessage(channel_idx, 0, sender, formattedMsg);
}

void UITask::onAdminLoginResult(bool success, uint8_t permissions, uint32_t server_time) {
  if (isOnRepeaterAdmin()) {
    ((RepeaterAdminScreen*)repeater_admin)->onLoginResult(success, permissions, server_time);
    _next_refresh = 100;  // trigger re-render
  }
}

void UITask::onAdminCliResponse(const char* from_name, const char* text) {
  if (isOnRepeaterAdmin()) {
    ((RepeaterAdminScreen*)repeater_admin)->onCliResponse(text);
    _next_refresh = 100;  // trigger re-render
  }
}