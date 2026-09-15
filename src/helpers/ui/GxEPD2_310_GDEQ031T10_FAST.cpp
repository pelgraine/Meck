// =============================================================================
// GxEPD2_310_GDEQ031T10_FAST -- Meck EXPERIMENT copy of the GxEPD2 GDEQ031T10
// driver (ZinggJM/GxEPD2 1.6.9, src/gdeq/GxEPD2_310_GDEQ031T10.{h,cpp}, GPL-3.0).
// Changes from the library file: the class is renamed, the include is the
// library's public header, and _Update_Part() switches the UC8253 to
// "LUT from register" and loads the register waveform that Larry Bank's
// bb_epaper library (bitbank2/bb_epaper, src/bb_ep.inl, epd31_init_part,
// commit 81f21e9 of 7 Sep 2026) uses for this exact panel on the T-Deck Pro:
// one short push per pixel towards its new colour (9 frames + 1) instead of
// the factory OTP partial waveform. Full refreshes are unchanged (OTP).
// Purpose: measure the partial refresh time and judge the picture.
// Selected only by the meck_max_ble_fastlut build environment.
// =============================================================================
// Display Library for SPI e-paper panels from Dalian Good Display and boards from Waveshare.
// Requires HW SPI and Adafruit_GFX. Caution: the e-paper panels require 3.3V supply AND data lines!
//
// based on Demo Example from Good Display: https://www.good-display.com/product/426.html
// Panel: GDEQ031T10 : https://www.good-display.com/product/426.html
// Controller: UC8253 : https://v4.cecdn.yun300.cn/100001_1909185148/UC8253.pdf
//
// Author: Jean-Marc Zingg
//
// Version: see library.properties
//
// Library: https://github.com/ZinggJM/GxEPD2

#include "GxEPD2_310_GDEQ031T10_FAST.h"

GxEPD2_310_GDEQ031T10_FAST::GxEPD2_310_GDEQ031T10_FAST(int16_t cs, int16_t dc, int16_t rst, int16_t busy) :
  GxEPD2_EPD(cs, dc, rst, busy, LOW, 10000000, WIDTH, HEIGHT, panel, hasColor, hasPartialUpdate, hasFastPartialUpdate)
{
}

void GxEPD2_310_GDEQ031T10_FAST::clearScreen(uint8_t value)
{
  // full refresh needed for all cases (previous != screen)
  _writeScreenBuffer(0x10, value); // set previous
  _writeScreenBuffer(0x13, value); // set current
  refresh(false); // full refresh
  _initial_write = false;
}

void GxEPD2_310_GDEQ031T10_FAST::writeScreenBuffer(uint8_t value)
{
  if (_initial_write) return clearScreen(value);
  _writeScreenBuffer(0x13, value); // set current
}

void GxEPD2_310_GDEQ031T10_FAST::writeScreenBufferAgain(uint8_t value)
{
  _writeScreenBuffer(0x10, value); // set previous
  //_writeScreenBuffer(0x13, value); // set current, not needed
}

void GxEPD2_310_GDEQ031T10_FAST::_writeScreenBuffer(uint8_t command, uint8_t value)
{
  if (!_init_display_done) _InitDisplay();
  _writeCommand(command);
  _startTransfer();
  for (uint32_t i = 0; i < uint32_t(WIDTH) * uint32_t(HEIGHT) / 8; i++)
  {
    _transfer(value);
  }
  _endTransfer();
}

void GxEPD2_310_GDEQ031T10_FAST::writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x13, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_310_GDEQ031T10_FAST::writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x10, bitmap, x, y, w, h, invert, mirror_y, pgm); // set previous
  _writeImage(0x13, bitmap, x, y, w, h, invert, mirror_y, pgm); // set current
}

void GxEPD2_310_GDEQ031T10_FAST::writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x10, bitmap, x, y, w, h, invert, mirror_y, pgm); // set previous
  //_writeImage(0x13, bitmap, x, y, w, h, invert, mirror_y, pgm); // set current, not needed
}

void GxEPD2_310_GDEQ031T10_FAST::writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImage(0x10, bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_310_GDEQ031T10_FAST::_writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  uint16_t wb = (w + 7) / 8; // width bytes, bitmaps are padded
  x -= x % 8; // byte boundary
  w = wb * 8; // byte boundary
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  if (_initial_write) writeScreenBuffer(); // initial full screen buffer clean
  _writeCommand(0x91); // partial in
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      // use wb, h of bitmap for index!
      uint16_t idx = mirror_y ? j + dx / 8 + uint16_t((h - 1 - (i + dy))) * wb : j + dx / 8 + uint16_t(i + dy) * wb;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  _writeCommand(0x92); // partial out
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void GxEPD2_310_GDEQ031T10_FAST::writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x13, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_310_GDEQ031T10_FAST::writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x10, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm); // set previous
  //_writeImagePart(0x13, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm); // set current, not needed
}

void GxEPD2_310_GDEQ031T10_FAST::writeImagePartToPrevious(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  _writeImagePart(0x10, bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_310_GDEQ031T10_FAST::_writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
  if ((w_bitmap < 0) || (h_bitmap < 0) || (w < 0) || (h < 0)) return;
  if ((x_part < 0) || (x_part >= w_bitmap)) return;
  if ((y_part < 0) || (y_part >= h_bitmap)) return;
  uint16_t wb_bitmap = (w_bitmap + 7) / 8; // width bytes, bitmaps are padded
  x_part -= x_part % 8; // byte boundary
  w = w_bitmap - x_part < w ? w_bitmap - x_part : w; // limit
  h = h_bitmap - y_part < h ? h_bitmap - y_part : h; // limit
  x -= x % 8; // byte boundary
  w = 8 * ((w + 7) / 8); // byte boundary, bitmaps are padded
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x; // limit
  int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y; // limit
  int16_t dx = x1 - x;
  int16_t dy = y1 - y;
  w1 -= dx;
  h1 -= dy;
  if ((w1 <= 0) || (h1 <= 0)) return;
  if (!_init_display_done) _InitDisplay();
  if (_initial_write) writeScreenBuffer(); // initial full screen buffer clean
  _writeCommand(0x91); // partial in
  _setPartialRamArea(x1, y1, w1, h1);
  _writeCommand(command);
  _startTransfer();
  for (int16_t i = 0; i < h1; i++)
  {
    for (int16_t j = 0; j < w1 / 8; j++)
    {
      uint8_t data;
      // use wb_bitmap, h_bitmap of bitmap for index!
      uint16_t idx = mirror_y ? x_part / 8 + j + dx / 8 + uint16_t((h_bitmap - 1 - (y_part + i + dy))) * wb_bitmap : x_part / 8 + j + dx / 8 + uint16_t(y_part + i + dy) * wb_bitmap;
      if (pgm)
      {
#if defined(__AVR) || defined(ESP8266) || defined(ESP32)
        data = pgm_read_byte(&bitmap[idx]);
#else
        data = bitmap[idx];
#endif
      }
      else
      {
        data = bitmap[idx];
      }
      if (invert) data = ~data;
      _transfer(data);
    }
  }
  _endTransfer();
  _writeCommand(0x92); // partial out
  delay(1); // yield() to avoid WDT on ESP8266 and ESP32
}

void GxEPD2_310_GDEQ031T10_FAST::writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black)
  {
    writeImage(black, x, y, w, h, invert, mirror_y, pgm);
  }
}

void GxEPD2_310_GDEQ031T10_FAST::writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black)
  {
    writeImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
}

void GxEPD2_310_GDEQ031T10_FAST::writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1)
  {
    writeImage(data1, x, y, w, h, invert, mirror_y, pgm);
  }
}

void GxEPD2_310_GDEQ031T10_FAST::drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImageAgain(bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_310_GDEQ031T10_FAST::drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  writeImagePart(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  refresh(x, y, w, h);
  writeImagePartAgain(bitmap, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
}

void GxEPD2_310_GDEQ031T10_FAST::drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black)
  {
    drawImage(black, x, y, w, h, invert, mirror_y, pgm);
  }
}

void GxEPD2_310_GDEQ031T10_FAST::drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
    int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (black)
  {
    drawImagePart(black, x_part, y_part, w_bitmap, h_bitmap, x, y, w, h, invert, mirror_y, pgm);
  }
}

void GxEPD2_310_GDEQ031T10_FAST::drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert, bool mirror_y, bool pgm)
{
  if (data1)
  {
    drawImage(data1, x, y, w, h, invert, mirror_y, pgm);
  }
}

void GxEPD2_310_GDEQ031T10_FAST::refresh(bool partial_update_mode)
{
  if (partial_update_mode) refresh(0, 0, WIDTH, HEIGHT);
  else
  {
    _Update_Full();
    _initial_refresh = false; // initial full update done
  }
}

void GxEPD2_310_GDEQ031T10_FAST::refresh(int16_t x, int16_t y, int16_t w, int16_t h)
{
  if (_initial_refresh) return refresh(false); // initial update needs be full update
  // intersection with screen
  int16_t w1 = x < 0 ? w + x : w; // reduce
  int16_t h1 = y < 0 ? h + y : h; // reduce
  int16_t x1 = x < 0 ? 0 : x; // limit
  int16_t y1 = y < 0 ? 0 : y; // limit
  w1 = x1 + w1 < int16_t(WIDTH) ? w1 : int16_t(WIDTH) - x1; // limit
  h1 = y1 + h1 < int16_t(HEIGHT) ? h1 : int16_t(HEIGHT) - y1; // limit
  if ((w1 <= 0) || (h1 <= 0)) return;
  // make x1, w1 multiple of 8
  w1 += x1 % 8;
  if (w1 % 8 > 0) w1 += 8 - w1 % 8;
  x1 -= x1 % 8;
  if (usePartialUpdateWindow) _writeCommand(0x91); // partial in
  _setPartialRamArea(x1, y1, w1, h1);
  _Update_Part();
  if (usePartialUpdateWindow) _writeCommand(0x92); // partial out
}

void GxEPD2_310_GDEQ031T10_FAST::powerOff(void)
{
  _PowerOff();
}

void GxEPD2_310_GDEQ031T10_FAST::hibernate()
{
  _PowerOff();
  if (_rst >= 0)
  {
    _writeCommand(0x07); // deep sleep
    _writeData(0xA5);    // check code
    _hibernating = true;
    _init_display_done = false;
  }
}

void GxEPD2_310_GDEQ031T10_FAST::_setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
  uint16_t xe = (x + w - 1) | 0x0007; // byte boundary inclusive (last byte)
  uint16_t ye = y + h - 1;
  x &= 0xFFF8; // byte boundary
  _writeCommand(0x90); // partial window
  _writeData(x);
  _writeData(xe);
  _writeData(y / 256);
  _writeData(y % 256);
  _writeData(ye / 256);
  _writeData(ye % 256);
  _writeData(0x01);
}

void GxEPD2_310_GDEQ031T10_FAST::_PowerOn()
{
  if (!_power_is_on)
  {
    _writeCommand(0x04);
    _waitWhileBusy("_PowerOn", power_on_time);
  }
  _power_is_on = true;
}

void GxEPD2_310_GDEQ031T10_FAST::_PowerOff()
{
  if (_power_is_on)
  {
    _writeCommand(0x02); // power off
    _waitWhileBusy("_PowerOff", power_off_time);
  }
  _power_is_on = false;
}

void GxEPD2_310_GDEQ031T10_FAST::_InitDisplay()
{
  if (_hibernating) _reset();
  else
  {
    _writeCommand(0x00); // PANEL SETTING
    _writeData(0x1e);    // soft reset
    _writeData(0x0d);
    delay(1);
  }
  _power_is_on = false;
  _writeCommand(0x00); // PANEL SETTING
  _writeData(0x1f);    // KW: 3f, KWR: 2F, BWROTP: 0f, BWOTP: 1f
  _writeData(0x0d);
  _init_display_done = true;
}

void GxEPD2_310_GDEQ031T10_FAST::_Update_Full()
{
  _fastPartialsSinceFull = 0;
  if (useFastFullUpdate)
  {
    _writeCommand(0xE0); // Cascade Setting (CCSET)
    _writeData(0x02);    // TSFIX
    _writeCommand(0xE5); // Force Temperature (TSSET)
    _writeData(0x5A);    // 90, 1015000us
    //_writeData(0x6E);    // 110, 1542001
  }
  _writeCommand(0x50);
  _writeData(0x97);
  _PowerOn();
  _writeCommand(0x12); //display refresh
  _waitWhileBusy("_Update_Full", full_refresh_time);
  _init_display_done = false; // needed, reason unknown
}

// Register waveform from bb_epaper epd31_init_part (see banner). Five tables of
// 42 bytes: VCOM (0x20), WW (0x21), BW (0x22), WB (0x23), BB (0x24). UC8253
// group layout per Larry Bank's comment: group repeat, level+count 1,
// level+count 1, level+count 2, level+count 2, repeat 1, repeat 2. Only the
// first group is used; the other five are zero. Each level+count byte is
// the level in bits 7:6 (10 = towards white, 01 = towards black, 00 = VCOM
// DC) and the frame count in bits 5:0. bb_epaper ships 9 frames (0x89 /
// 0x49 / 0x09, measured 244 ms per partial on the Max); FAST_LUT_FRAMES
// is the tuning knob: more frames, darker blacks, longer refresh.
#ifndef FAST_LUT_FRAMES
#define FAST_LUT_FRAMES 12
#endif
#define FAST_TO_WHITE  (uint8_t)(0x80 | FAST_LUT_FRAMES)
#define FAST_TO_BLACK  (uint8_t)(0x40 | FAST_LUT_FRAMES)
#define FAST_VCOM_DC   (uint8_t)(0x00 | FAST_LUT_FRAMES)
static const uint8_t fast_lut_vcom[42] = {
  0x01, FAST_VCOM_DC, 0x01, 0x00, 0x00, 0x01, 0x00,
  0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0 };
static const uint8_t fast_lut_ww[42] = {
  0x01, FAST_TO_WHITE, 0x01, 0x00, 0x00, 0x01, 0x00,
  0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0 };
static const uint8_t fast_lut_bw[42] = {
  0x01, FAST_TO_WHITE, 0x01, 0x00, 0x00, 0x01, 0x00,
  0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0 };
static const uint8_t fast_lut_wb[42] = {
  0x01, FAST_TO_BLACK, 0x01, 0x00, 0x00, 0x01, 0x00,
  0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0 };
static const uint8_t fast_lut_bb[42] = {
  0x01, FAST_TO_BLACK, 0x01, 0x00, 0x00, 0x01, 0x00,
  0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0, 0,0,0,0,0,0,0 };

void GxEPD2_310_GDEQ031T10_FAST::_writeLut(uint8_t command, const uint8_t* lut)
{
  _writeCommand(command);
  for (int i = 0; i < 42; i++) _writeData(lut[i]);
}

void GxEPD2_310_GDEQ031T10_FAST::_Update_Part()
{
  if (_fastWaveform)
  {
    // bb_epaper epd31_init_part order: PSR (LUT from register), CCSET, TSSET,
    // CDI, the five LUTs, then power on. The library's OTP path wrote PSR 0x1F
    // in _InitDisplay(); 0x3F is the same panel setting with the REG bit set.
    _writeCommand(0x00); // PANEL SETTING
    _writeData(0x3f);    // KW, LUT from register
    _writeData(0x0d);
    _writeCommand(0xE0); // Cascade Setting (CCSET)
    _writeData(0x02);    // TSFIX
    _writeCommand(0xE5); // Force Temperature (TSSET)
    _writeData(0x79);    // 121
    _writeCommand(0x50);
    _writeData(0xD7);
    _writeLut(0x20, fast_lut_vcom);
    _writeLut(0x21, fast_lut_ww);
    _writeLut(0x22, fast_lut_bw);
    _writeLut(0x23, fast_lut_wb);
    _writeLut(0x24, fast_lut_bb);
    _fastPartialsSinceFull++;
  }
  else
  {
    // The library's own partial path: factory OTP waveform (PSR 0x1F was
    // rewritten by _InitDisplay() before the image write) at forced
    // temperature 121.
    _writeCommand(0xE0); // Cascade Setting (CCSET)
    _writeData(0x02);    // TSFIX
    _writeCommand(0xE5); // Force Temperature (TSSET)
    _writeData(0x79);    // 121
    _writeCommand(0x50);
    _writeData(0xD7);
  }
  _PowerOn();
  _writeCommand(0x12); //display refresh
  _waitWhileBusy("_Update_Part", partial_refresh_time);
  _init_display_done = false; // needed, reason unknown
}