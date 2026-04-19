#pragma once

// =============================================================================
// FastEPDDisplay — Parallel e-ink display driver for T5 S3 E-Paper Pro
//
// Architecture:
//   - FastEPD handles hardware init, power management, and display refresh
//   - Adafruit_GFX GFXcanvas1 handles all drawing/text rendering
//   - On endFrame(), canvas buffer is copied to FastEPD and display is updated
//
// This avoids depending on FastEPD's drawing API — only uses its well-tested
// hardware interface (initPanel, fullUpdate, partialUpdate, currentBuffer).
// =============================================================================

#include <Adafruit_GFX.h>
#include "variant.h"  // EPD_WIDTH, EPD_HEIGHT (only compiled for T5S3 builds)
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSans12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans24pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSerif12pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>

// Meck custom font styles (Noto Sans, Montserrat)
#include "MeckFonts.h"

#include "DisplayDriver.h"

// GxEPD2 color constant compatibility — MapScreen uses these directly
#ifndef GxEPD_BLACK
#define GxEPD_BLACK  0x0000
#endif
#ifndef GxEPD_WHITE
#define GxEPD_WHITE  0xFFFF
#endif

// Forward declare FastEPD class (actual include in .cpp)
class FASTEPD;

// Inline CRC32 for frame change detection
// (Copied from GxEPDDisplay.h — avoids CRC32/PNGdec name collision)
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


class FastEPDDisplay : public DisplayDriver {
  FASTEPD* _epd;
  GFXcanvas1* _canvas;       // Adafruit_GFX 1-bit drawing surface (960×540)
  bool _init = false;
  bool _isOn = false;
  uint16_t _curr_color;      // GxEPD_BLACK or GxEPD_WHITE for canvas drawing
  FrameCRC32 _frameCRC;
  uint32_t _lastCRC = 0;
  int _fullRefreshCount = 0;  // Track for periodic slow refresh
  uint32_t _lastUpdateMs = 0; // Rate limiting — minimum interval between refreshes
  bool _forcePartial = false; // When true, use partial updates (VKB typing)
  bool _darkMode = false;     // Invert all pixels (black bg, white text)
  bool _portraitMode = false; // Rotated 90° (540×960 logical)

  // Virtual 128×128 → physical canvas mapping (runtime, changes with portrait)
  float scale_x  = 7.5f;       // 960 / 128 (landscape default)
  float scale_y  = 4.21875f;   // 540 / 128 (landscape default)
  static constexpr float offset_x = 0.0f;
  static constexpr float offset_y = 0.0f;

public:
  FastEPDDisplay() : DisplayDriver(128, 128), _epd(nullptr), _canvas(nullptr) {}
  ~FastEPDDisplay();

  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
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
    if (_canvas) _canvas->drawPixel(x, y, color ? 1 : 0);
  }
  int16_t rawWidth()  { return EPD_WIDTH; }
  int16_t rawHeight() { return EPD_HEIGHT; }

  void drawTextRaw(int16_t x, int16_t y, const char* text, uint16_t color) {
    if (!_canvas) return;
    _canvas->setFont(NULL);
    _canvas->setTextSize(3);  // 3× built-in 5×7 = 15×21, readable on 960×540
    _canvas->setTextColor(color ? 1 : 0);
    _canvas->setCursor(x, y);
    _canvas->print(text);
  }

  void invalidateFrameCRC() { _lastCRC = 0; }

  // Temporarily force partial (no-flash) updates — use during VKB typing
  void setForcePartial(bool partial) { _forcePartial = partial; }
  bool isForcePartial() const { return _forcePartial; }

  // Dark mode — invert all pixels in endFrame (black bg, white text)
  void setDarkMode(bool dark);
  bool isDarkMode() const { return _darkMode; }

  // Portrait mode — rotate canvas 90° (540×960 logical), swap scale factors
  void setPortraitMode(bool portrait);
  bool isPortraitMode() const { return _portraitMode; }
};