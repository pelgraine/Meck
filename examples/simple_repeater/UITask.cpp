#include "UITask.h"
#include <Arduino.h>
#include <helpers/CommonCLI.h>

#ifdef HAS_4G_MODEM
#include "CellularMQTT.h"
#endif

#ifdef MECK_WIFI_REMOTE
#include "WiFiMQTT.h"
#endif

#if defined(HAS_4G_MODEM) || defined(MECK_WIFI_REMOTE)
#define AUTO_OFF_DISABLED    true
#else
#define AUTO_OFF_DISABLED    false
#endif

#define AUTO_OFF_MILLIS      20000  // 20 seconds (ignored when AUTO_OFF_DISABLED)
#define BOOT_SCREEN_MILLIS   4000   // 4 seconds

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] PROGMEM = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

void UITask::begin(NodePrefs* node_prefs, const char* build_date, const char* firmware_version) {
  _prevBtnState = HIGH;
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _node_prefs = node_prefs;
  _display->turnOn();

  char *version = strdup(firmware_version);
  char *dash = strchr(version, '-');
  if (dash) *dash = 0;

  snprintf(_version_info, sizeof(_version_info), "%s (%s)", version, build_date);
  free(version);
}

void UITask::renderCurrScreen() {
  char tmp[80];
  if (millis() < BOOT_SCREEN_MILLIS) {
    // Boot screen — logo + version
    _display->setColor(DisplayDriver::BLUE);
    int logoWidth = 128;
    _display->drawXbm((_display->width() - logoWidth) / 2, 3, meshcore_logo, logoWidth, 13);

    _display->setColor(DisplayDriver::LIGHT);
    _display->setTextSize(1);
    uint16_t versionWidth = _display->getTextWidth(_version_info);
    _display->setCursor((_display->width() - versionWidth) / 2, 22);
    _display->print(_version_info);

#if defined(HAS_4G_MODEM)
    const char* node_type = "< Remote Repeater >";
#elif defined(MECK_WIFI_REMOTE)
    const char* node_type = "< WiFi Repeater >";
#else
    const char* node_type = "< Repeater >";
#endif
    uint16_t typeWidth = _display->getTextWidth(node_type);
    _display->setCursor((_display->width() - typeWidth) / 2, 35);
    _display->print(node_type);
  } else {
    // Home screen — node info + connection status
    _display->setCursor(0, 0);
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::GREEN);
    _display->print(_node_prefs->node_name);

    _display->setCursor(0, 20);
    _display->setColor(DisplayDriver::YELLOW);
    sprintf(tmp, "FREQ: %06.3f SF%d", _node_prefs->freq, _node_prefs->sf);
    _display->print(tmp);

    _display->setCursor(0, 30);
    sprintf(tmp, "BW: %03.2f CR: %d", _node_prefs->bw, _node_prefs->cr);
    _display->print(tmp);

    // --- Cellular status (4G variant) ---
#ifdef HAS_4G_MODEM
    int y = 44;

    _display->setCursor(0, y);
    _display->setColor(DisplayDriver::LIGHT);
    sprintf(tmp, "4G: %s", cellularMQTT.stateString());
    _display->print(tmp);
    y += 10;

    _display->setCursor(0, y);
    sprintf(tmp, "CSQ: %d (%d bars)", cellularMQTT.getCSQ(), cellularMQTT.getSignalBars());
    _display->print(tmp);
    y += 10;

    const char* oper = cellularMQTT.getOperator();
    if (oper[0]) {
      _display->setCursor(0, y);
      sprintf(tmp, "Op: %.16s", oper);
      _display->print(tmp);
      y += 10;
    }

    _display->setCursor(0, y);
    _display->setColor(cellularMQTT.isConnected() ? DisplayDriver::GREEN : DisplayDriver::YELLOW);
    sprintf(tmp, "MQTT: %s", cellularMQTT.isConnected() ? "Connected" : "---");
    _display->print(tmp);
    y += 10;

    const char* ip4g = cellularMQTT.getIPAddress();
    if (ip4g[0]) {
      _display->setColor(DisplayDriver::LIGHT);
      _display->setCursor(0, y);
      sprintf(tmp, "IP: %s", ip4g);
      _display->print(tmp);
      y += 10;
    }

    uint32_t upSec = millis() / 1000;
    uint32_t upH = upSec / 3600;
    uint32_t upM = (upSec % 3600) / 60;
    _display->setColor(DisplayDriver::LIGHT);
    _display->setCursor(0, y);
    sprintf(tmp, "Up: %luh %lum  Heap:%dk", upH, upM, ESP.getFreeHeap() / 1024);
    _display->print(tmp);
#endif

    // --- WiFi status (WiFi variant) ---
#ifdef MECK_WIFI_REMOTE
    int y = 44;

    _display->setCursor(0, y);
    _display->setColor(DisplayDriver::LIGHT);
    sprintf(tmp, "WiFi: %s", wifiMQTT.stateString());
    _display->print(tmp);
    y += 10;

    _display->setCursor(0, y);
    sprintf(tmp, "RSSI: %d (%d bars)", wifiMQTT.getRSSI(), wifiMQTT.getSignalBars());
    _display->print(tmp);
    y += 10;

    _display->setCursor(0, y);
    sprintf(tmp, "SSID: %.16s", wifiMQTT.getSSID());
    _display->print(tmp);
    y += 10;

    _display->setCursor(0, y);
    _display->setColor(wifiMQTT.isConnected() ? DisplayDriver::GREEN : DisplayDriver::YELLOW);
    sprintf(tmp, "MQTT: %s", wifiMQTT.isConnected() ? "Connected" : "---");
    _display->print(tmp);
    y += 10;

    const char* ipWifi = wifiMQTT.getIPAddress();
    if (ipWifi[0]) {
      _display->setColor(DisplayDriver::LIGHT);
      _display->setCursor(0, y);
      sprintf(tmp, "IP: %s", ipWifi);
      _display->print(tmp);
      y += 10;
    }

    uint32_t upSec = millis() / 1000;
    uint32_t upH = upSec / 3600;
    uint32_t upM = (upSec % 3600) / 60;
    _display->setColor(DisplayDriver::LIGHT);
    _display->setCursor(0, y);
    sprintf(tmp, "Up: %luh %lum  Heap:%dk", upH, upM, ESP.getFreeHeap() / 1024);
    _display->print(tmp);
#endif
  }
}

void UITask::loop() {
#ifdef PIN_USER_BTN
  if (millis() >= _next_read) {
    int btnState = digitalRead(PIN_USER_BTN);
    if (btnState != _prevBtnState) {
      if (btnState == LOW) {
        if (!_display->isOn()) {
          _display->turnOn();
        }
        _auto_off = millis() + AUTO_OFF_MILLIS;
      }
      _prevBtnState = btnState;
    }
    _next_read = millis() + 200;
  }
#endif

  if (_display->isOn()) {
    if (millis() >= _next_refresh) {
      _display->startFrame();
      renderCurrScreen();
      _display->endFrame();

      _next_refresh = millis() + 10000;
    }
    if (!AUTO_OFF_DISABLED && millis() > _auto_off) {
      _display->turnOff();
    }
  }
}