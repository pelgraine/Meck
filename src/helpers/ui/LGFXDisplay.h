#pragma once

#include <helpers/ui/DisplayDriver.h>

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#ifndef UI_ZOOM
  #define UI_ZOOM 1
#endif

class LGFXDisplay : public DisplayDriver {
protected:
  LGFX_Device* display;
  LGFX_Sprite buffer;

  bool _isOn = false;
  int _color = TFT_WHITE;

public:
  LGFXDisplay(int w, int h, LGFX_Device &disp)
    : DisplayDriver(w/UI_ZOOM, h/UI_ZOOM), display(&disp) {}
  bool begin();
  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
#if defined(LILYGO_TWATCH_S3_PLUS)
  // Set an exact RGB565 colour, bypassing the Color enum + watch grey remap.
  // Used by the watch P4-style tile grid for per-tile border/fill colours.
  void setRawColor(uint16_t c) { _color = c; buffer.setTextColor(c); }
  // Rounded-rect helpers for the P4-style tile grid (LovyanGFX buffer).
  void fillRoundRect(int x, int y, int w, int h, int r) { buffer.fillRoundRect(x, y, w, h, r, _color); }
  void drawRoundRect(int x, int y, int w, int h, int r) { buffer.drawRoundRect(x, y, w, h, r, _color); }
  // Plot a single pixel at an exact RGB565 colour directly into the sprite
  // buffer. Used by WatchMapScreen to draw decoded map tiles, markers and the
  // crosshair (parallels GxEPDDisplay::drawPixelRaw on the e-ink boards).
  void drawPixelRaw(int x, int y, uint16_t c) { buffer.drawPixel(x, y, c); }
  // Render a string in a very small font at (x,y) so long node names fit
  // beside the centred clock, then restore the default GLCD font + size so
  // later text is unaffected. Uses the colour set via setColor()/setRawColor().
  void printSmallFont(int x, int y, const char* str) {
    buffer.setFont(&fonts::TomThumb);
    buffer.setTextColor(_color);
    buffer.setTextSize(1);
    buffer.setCursor(x, y);
    buffer.print(str);
    buffer.setFont(&fonts::Font0);   // restore default 6x8 GLCD font
    buffer.setTextSize(1);
  }
  // Width of a string in the tiny font (the font printSmallFont uses), for
  // right-aligning tiny-font header text. Restores the default font after.
  uint16_t smallTextWidth(const char* str) {
    buffer.setFont(&fonts::TomThumb);
    uint16_t w = buffer.textWidth(str);
    buffer.setFont(&fonts::Font0);
    return w;
  }
  // Enable/disable horizontal text wrap on the sprite buffer. The compose
  // ticker turns wrap off so a long scrolling line is clipped at the screen
  // edge instead of wrapping onto the rows below.
  void setTextWrap(bool en) { buffer.setTextWrap(en, false); }
  // Draw a string centred horizontally at mid_x with its top at y, in
  // FreeSansBold 24pt (used for the clock HH:MM), then restore the default
  // GLCD font + size so later text is unaffected.
  void printClockFont(int mid_x, int y, const char* str) {
    buffer.setFont(&fonts::FreeSansBold24pt7b);
    buffer.setTextColor(_color);
    buffer.setTextSize(1);
    int w = buffer.textWidth(str);
    buffer.setCursor(mid_x - w / 2, y);
    buffer.print(str);
    buffer.setFont(&fonts::Font0);   // restore default 6x8 GLCD font
    buffer.setTextSize(1);
  }
#endif
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
  virtual bool getTouch(int *x, int *y);
};