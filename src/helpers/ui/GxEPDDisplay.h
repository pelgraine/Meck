#pragma once

// =============================================================================
// GxEPDDisplay STUB for CrowPanel (and other non-e-ink LGFX targets)
//
// This file shadows src/helpers/ui/GxEPDDisplay.h to prevent the LovyanGFX vs
// Adafruit_GFX GFXfont type collision at link time. MapScreen.h unconditionally
// includes GxEPDDisplay.h and uses a GxEPDDisplay* member — this stub provides
// the minimal API so that compilation and linking succeed.
//
// On CrowPanel the map screen is inert (no SD card when function switch is in
// WM mode, no keyboard to navigate to it). The _einkDisplay pointer in
// MapScreen will be a bad cast but is null-checked before every draw call.
// =============================================================================

#include <helpers/ui/DisplayDriver.h>

// GxEPD color constants referenced by MapScreen.h
#ifndef GxEPD_BLACK
  #define GxEPD_BLACK 0
  #define GxEPD_WHITE 1
#endif

class GxEPDDisplay : public DisplayDriver {
public:
  GxEPDDisplay() : DisplayDriver(128, 128) {}

  // --- MapScreen raw pixel API (stubs) ---
  void drawPixelRaw(int16_t x, int16_t y, uint16_t color) {}
  int16_t rawWidth()  { return 0; }
  int16_t rawHeight() { return 0; }
  void drawTextRaw(int16_t x, int16_t y, const char* text, uint16_t color) {}
  void invalidateFrameCRC() {}

  // --- DisplayDriver pure virtuals (no-op implementations) ---
  bool isOn() override { return false; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override {}
  void startFrame(Color bkg = DARK) override {}
  void setTextSize(int sz) override {}
  void setColor(Color c) override {}
  void setCursor(int x, int y) override {}
  void print(const char* str) override {}
  void fillRect(int x, int y, int w, int h) override {}
  void drawRect(int x, int y, int w, int h) override {}
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override {}
  uint16_t getTextWidth(const char* str) override { return 0; }
  void endFrame() override {}
};