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

#ifndef _GxEPD2_310_GDEQ031T10_FAST_H_
#define _GxEPD2_310_GDEQ031T10_FAST_H_

#include <GxEPD2_EPD.h>

class GxEPD2_310_GDEQ031T10_FAST : public GxEPD2_EPD
{
  public:
    // attributes
    static const uint16_t WIDTH = 240;
    static const uint16_t WIDTH_VISIBLE = WIDTH;
    static const uint16_t HEIGHT = 320;
    static const GxEPD2::Panel panel = GxEPD2::GDEQ031T10;
    static const bool hasColor = false;
    static const bool hasPartialUpdate = true;
    static const bool usePartialUpdateWindow = true; // set false for better image
    static const bool hasFastPartialUpdate = true; // set this false to force full refresh always
    static const bool useFastFullUpdate = true; // set false for extended (low) temperature range, 1015000us vs 3082001us
    static const uint16_t power_on_time = 50; // ms, e.g. 45000us
    static const uint16_t power_off_time = 50; // ms, e.g. 45000us
    static const uint16_t full_refresh_time = 1100; // ms, e.g. 1015000us
    static const uint16_t partial_refresh_time = 700; // ms, e.g. 650000us
    // constructor
    GxEPD2_310_GDEQ031T10_FAST(int16_t cs, int16_t dc, int16_t rst, int16_t busy);
    // methods (virtual)
    //  Support for Bitmaps (Sprites) to Controller Buffer and to Screen
    void clearScreen(uint8_t value = 0xFF); // init controller memory and screen (default white)
    void writeScreenBuffer(uint8_t value = 0xFF); // init controller memory (default white)
    void writeScreenBufferAgain(uint8_t value = 0xFF); // init previous buffer controller memory (default white)
    // write to controller memory, without screen refresh; x and w should be multiple of 8
    void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImageToPrevious(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartToPrevious(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                                int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                        int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // for differential update: set current and previous buffers equal (for fast partial update to work correctly)
    void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void writeImagePartAgain(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                             int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, without screen refresh; x and w should be multiple of 8
    void writeNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write to controller memory, with screen refresh; x and w should be multiple of 8
    void drawImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImage(const uint8_t* black, const uint8_t* color, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void drawImagePart(const uint8_t* black, const uint8_t* color, int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                       int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    // write sprite of native data to controller memory, with screen refresh; x and w should be multiple of 8
    void drawNative(const uint8_t* data1, const uint8_t* data2, int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void refresh(bool partial_update_mode = false); // screen refresh from controller memory to full screen
    void refresh(int16_t x, int16_t y, int16_t w, int16_t h); // screen refresh from controller memory, partial screen
    void powerOff(); // turns off generation of panel driving voltages, avoids screen fading over time
    // Meck: choose the waveform for partial refreshes. Default: the fast
    // register waveform. The Game Boy emulator switches to the factory OTP
    // waveform while a game runs (its dithered picture needs the longer drive).
    void setFastWaveform(bool on) { _fastWaveform = on; }
    bool fastWaveform() const { return _fastWaveform; }
    // Fast partials since the last full refresh, for ghosting housekeeping.
    uint16_t fastPartialsSinceFull() const { return _fastPartialsSinceFull; }
    void hibernate(); // turns powerOff() and sets controller to deep sleep for minimum power use, ONLY if wakeable by RST (rst >= 0)
  private:
    void _writeScreenBuffer(uint8_t command, uint8_t value);
    void _writeImage(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _writeImagePart(uint8_t command, const uint8_t bitmap[], int16_t x_part, int16_t y_part, int16_t w_bitmap, int16_t h_bitmap,
                         int16_t x, int16_t y, int16_t w, int16_t h, bool invert = false, bool mirror_y = false, bool pgm = false);
    void _setPartialRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
    void _PowerOn();
    void _PowerOff();
    void _InitDisplay();
    void _Update_Full();
    void _Update_Part();
    void _writeLut(uint8_t command, const uint8_t* lut);
    bool _fastWaveform = true;              // register waveform (true) or factory OTP (false) for partials
    uint16_t _fastPartialsSinceFull = 0;    // fast partial refreshes since the last full refresh
};

#endif
