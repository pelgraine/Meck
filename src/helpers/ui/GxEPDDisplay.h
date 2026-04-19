#pragma once

// T5S3 E-Paper Pro uses parallel e-ink (FastEPD), not SPI (GxEPD2)
#if defined(LilyGo_T5S3_EPaper_Pro)
  #include "FastEPDDisplay.h"
  using GxEPDDisplay = FastEPDDisplay;
#else

#include <SPI.h>
#include <Wire.h>

#define ENABLE_GxEPD2_GFX 0

#include <GxEPD2_BW.h>
#include <GxEPD2_3C.h>
#include <GxEPD2_4C.h>
#include <GxEPD2_7C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>

// Meck custom font styles (Noto Sans, Montserrat) — only available in
// companion radio builds which have -I examples/companion_radio/ui-new
#if __has_include("MeckFonts.h")
  #include "MeckFonts.h"
  #define HAS_MECK_FONTS 1
#endif

// Inline CRC32 for frame change detection (replaces bakercp/CRC32
// to avoid naming collision with PNGdec's bundled CRC32.h)
class FrameCRC32 {
  uint32_t _crc = 0xFFFFFFFF;
public:
  void reset() { _crc = 0xFFFFFFFF; }
  template<typename T> void update(T val) {
    const uint8_t* p = (const uint8_t*)&val;
    for (size_t i = 0; i < sizeof(T); i++) {
      _crc ^= p[i];
      for (int b = 0; b < 8; b++)
        _crc = (_crc >> 1) ^ (0xEDB88320 & -(int32_t)(_crc & 1));
    }
  }
  template<typename T> void update(const T* data, size_t len) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < len * sizeof(T); i++) {
      _crc ^= p[i];
      for (int b = 0; b < 8; b++)
        _crc = (_crc >> 1) ^ (0xEDB88320 & -(int32_t)(_crc & 1));
    }
  }
  uint32_t finalize() { return _crc ^ 0xFFFFFFFF; }
};

#include "DisplayDriver.h"

class GxEPDDisplay : public DisplayDriver {

#if defined(EINK_DISPLAY_MODEL)
  GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT> display;
  const float scale_x  = EINK_SCALE_X; 
  const float scale_y  = EINK_SCALE_Y;
  const float offset_x = EINK_X_OFFSET;
  const float offset_y = EINK_Y_OFFSET;
#else
  GxEPD2_BW<GxEPD2_150_BN, 200> display;
  const float scale_x  = 1.5625f;
  const float scale_y  = 1.5625f;
  const float offset_x = 0;
  const float offset_y = 10;
#endif
  bool _init = false;
  bool _isOn = false;
  bool _darkMode = false;
  uint16_t _curr_color;
  FrameCRC32 display_crc;
  int last_display_crc_value = 0;

public:
#if defined(EINK_DISPLAY_MODEL)
  GxEPDDisplay() : DisplayDriver(128, 128), display(EINK_DISPLAY_MODEL(PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST, PIN_DISPLAY_BUSY)) {}
#else
  GxEPDDisplay() : DisplayDriver(128, 128), display(GxEPD2_150_BN(DISP_CS, DISP_DC, DISP_RST, DISP_BUSY)) {}
#endif

  bool begin();

  bool isOn() override {return _isOn;};
  void turnOn() override;
  void turnOff() override;

  // Dark mode — inverts background/foreground for e-ink
  bool isDarkMode() const { return _darkMode; }
  void setDarkMode(bool on) { _darkMode = on; }

  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;

  // --- Raw pixel access for MapScreen (bypasses scaling) ---
  void drawPixelRaw(int16_t x, int16_t y, uint16_t color) {
    display.drawPixel(x, y, color);
  }
  int16_t rawWidth()  { return display.width(); }
  int16_t rawHeight() { return display.height(); }

  // Draw text at raw (unscaled) physical coordinates using built-in 5x7 font
  void drawTextRaw(int16_t x, int16_t y, const char* text, uint16_t color) {
    display.setFont(NULL);          // Built-in 5x7 font
    display.setTextSize(1);
    display.setTextColor(color);
    display.setCursor(x, y);
    display.print(text);
  }

  // Force endFrame() to push to display even if CRC unchanged
  // (needed because drawPixelRaw bypasses CRC tracking)
  void invalidateFrameCRC() { last_display_crc_value = 0; }
};

#endif // !LilyGo_T5S3_EPaper_Pro