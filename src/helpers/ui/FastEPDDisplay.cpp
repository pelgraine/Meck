#include "FastEPDDisplay.h"
#include "FastEPD.h"
#include <string.h>

// Fallback if FastEPD doesn't define these constants
#ifndef BBEP_SUCCESS
#define BBEP_SUCCESS 0
#endif
#ifndef CLEAR_FAST
#define CLEAR_FAST 0
#endif
#ifndef CLEAR_SLOW
#define CLEAR_SLOW 1
#endif
#ifndef BB_MODE_1BPP
#define BB_MODE_1BPP 0
#endif

// FastEPD constants (defined in FastEPD.h)
// BB_PANEL_LILYGO_T5PRO_V2 — board ID for V2 hardware
// BB_MODE_1BPP — 1-bit per pixel mode
// CLEAR_FAST, CLEAR_SLOW — full refresh modes

// Periodic slow (deep) refresh to clear ghosting
#define FULL_SLOW_PERIOD 20  // every 20 fast-refreshes, do a slow refresh

FastEPDDisplay::~FastEPDDisplay() {
  delete _canvas;
  delete _epd;
}

bool FastEPDDisplay::begin() {
  if (_init) return true;

  Serial.println("[FastEPD] Initializing T5S3 E-Paper Pro V2...");

  // Create FastEPD instance and init hardware
  _epd = new FASTEPD;
  // Meshtastic-proven init for V2 hardware (pinned FastEPD fork commit)
  Serial.println("[FastEPD] Using BB_PANEL_LILYGO_T5PRO_V2");
  int rc = _epd->initPanel(BB_PANEL_LILYGO_T5PRO_V2, 28000000);
  if (rc != BBEP_SUCCESS) {
    Serial.printf("[FastEPD] initPanel FAILED: %d\n", rc);
    delete _epd;
    _epd = nullptr;
    return false;
  }
  Serial.printf("[FastEPD] Panel initialized (rc=%d)\n", rc);

  // Enable display via PCA9535 GPIO (required for V2 hardware)
  // Pin 0 on PCA9535 = EP_OE (output enable for source driver)
  _epd->ioPinMode(0, OUTPUT);
  _epd->ioWrite(0, HIGH);
  Serial.println("[FastEPD] PCA9535 EP_OE set HIGH");

  // Set 1-bit per pixel mode
  _epd->setMode(BB_MODE_1BPP);
  Serial.println("[FastEPD] Mode set to 1BPP");

  // Create Adafruit_GFX canvas for drawing (960×540, 1-bit)
  // ~64KB, should auto-allocate in PSRAM on ESP32-S3 with PSRAM enabled
  _canvas = new GFXcanvas1(EPD_WIDTH, EPD_HEIGHT);
  if (!_canvas || !_canvas->getBuffer()) {
    Serial.println("[FastEPD] Canvas allocation FAILED!");
    return false;
  }
  Serial.printf("[FastEPD] Canvas allocated: %dx%d (%d bytes)\n",
                EPD_WIDTH, EPD_HEIGHT, (EPD_WIDTH * EPD_HEIGHT) / 8);

  // Initial clear — white screen
  Serial.println("[FastEPD] Calling clearWhite()...");
  _epd->clearWhite();
  Serial.println("[FastEPD] Calling fullUpdate(true) for initial clear...");
  _epd->fullUpdate(true);  // blocking initial clear
  _epd->backupPlane();     // Save clean state for subsequent diffs
  Serial.println("[FastEPD] Initial clear complete");

  // Set canvas defaults
  _canvas->fillScreen(1);  // White background (bit=1 → white in FastEPD)
  _canvas->setTextColor(0);  // Black text (bit=0 → black in FastEPD)
#ifdef MECK_SERIF_FONT
  _canvas->setFont(&FreeSerif12pt7b);
#else
  _canvas->setFont(&FreeSans12pt7b);
#endif
  _canvas->setTextWrap(false);

  _curr_color = GxEPD_BLACK;
  _init = true;
  _isOn = true;

  Serial.println("[FastEPD] Display ready (960x540, 1BPP)");
  return true;
}

void FastEPDDisplay::turnOn() {
  if (!_init) begin();
  _isOn = true;
}

void FastEPDDisplay::turnOff() {
  _isOn = false;
}

void FastEPDDisplay::clear() {
  if (!_canvas) return;
  _canvas->fillScreen(1);  // White
  _canvas->setTextColor(0);
  _frameCRC.reset();
}

void FastEPDDisplay::startFrame(Color bkg) {
  if (!_canvas) return;
  _canvas->fillScreen(1);  // White background
  _canvas->setTextColor(0);  // Black text
  _curr_color = GxEPD_BLACK;
  _frameCRC.reset();
}

void FastEPDDisplay::setTextSize(int sz) {
  if (!_canvas) return;
  _frameCRC.update<int>(sz);

  // Font mapping for 960×540 display at ~234 DPI
  // Toggle between font families via -D MECK_SERIF_FONT build flag
  switch(sz) {
    case 0:  // Body text — reader content, settings rows, messages, footers
#ifdef MECK_SERIF_FONT
      _canvas->setFont(&FreeSerif12pt7b);
#else
      _canvas->setFont(&FreeSans12pt7b);
#endif
      _canvas->setTextSize(1);
      break;
    case 1:  // Headings — screen titles, channel names (bold, same height as body)
      _canvas->setFont(&FreeSansBold12pt7b);
      _canvas->setTextSize(1);
      break;
    case 2:  // Large bold — MSG count, tile letters
      _canvas->setFont(&FreeSansBold18pt7b);
      _canvas->setTextSize(1);
      break;
    case 3:  // Extra large — splash screen title
      _canvas->setFont(&FreeSansBold24pt7b);
      _canvas->setTextSize(1);
      break;
    case 5:  // Clock face — lock screen (FreeSansBold24pt scaled 5×)
      _canvas->setFont(&FreeSansBold24pt7b);
      _canvas->setTextSize(5);
      break;
    default:
#ifdef MECK_SERIF_FONT
      _canvas->setFont(&FreeSerif12pt7b);
#else
      _canvas->setFont(&FreeSans12pt7b);
#endif
      _canvas->setTextSize(1);
      break;
  }
}

void FastEPDDisplay::setColor(Color c) {
  if (!_canvas) return;
  _frameCRC.update<Color>(c);

  // Colours are inverted for e-paper:
  //   DARK  = background colour = WHITE on e-paper
  //   LIGHT = foreground colour = BLACK on e-paper
  if (c == DARK) {
    _canvas->setTextColor(1);  // White (background)
    _curr_color = GxEPD_WHITE;
  } else {
    _canvas->setTextColor(0);  // Black (foreground)
    _curr_color = GxEPD_BLACK;
  }
}

void FastEPDDisplay::setCursor(int x, int y) {
  if (!_canvas) return;
  _frameCRC.update<int>(x);
  _frameCRC.update<int>(y);

  // Scale virtual coordinates to physical, with baseline offset.
  // The +5 pushes text baseline down so ascenders at y=0 are visible.
  _canvas->setCursor(
    (int)((x + offset_x) * scale_x),
    (int)((y + offset_y + 5) * scale_y)
  );
}

void FastEPDDisplay::print(const char* str) {
  if (!_canvas || !str) return;
  _frameCRC.update<char>(str, strlen(str));
  _canvas->print(str);
}

void FastEPDDisplay::fillRect(int x, int y, int w, int h) {
  if (!_canvas) return;
  _frameCRC.update<int>(x);
  _frameCRC.update<int>(y);
  _frameCRC.update<int>(w);
  _frameCRC.update<int>(h);

  // Canvas uses 1-bit color: convert GxEPD color
  uint16_t canvasColor = (_curr_color == GxEPD_BLACK) ? 0 : 1;
  _canvas->fillRect(
    (int)((x + offset_x) * scale_x),
    (int)((y + offset_y) * scale_y),
    (int)(w * scale_x),
    (int)(h * scale_y),
    canvasColor
  );
}

void FastEPDDisplay::drawRect(int x, int y, int w, int h) {
  if (!_canvas) return;
  _frameCRC.update<int>(x);
  _frameCRC.update<int>(y);
  _frameCRC.update<int>(w);
  _frameCRC.update<int>(h);

  uint16_t canvasColor = (_curr_color == GxEPD_BLACK) ? 0 : 1;
  _canvas->drawRect(
    (int)((x + offset_x) * scale_x),
    (int)((y + offset_y) * scale_y),
    (int)(w * scale_x),
    (int)(h * scale_y),
    canvasColor
  );
}

void FastEPDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  if (!_canvas || !bits) return;
  _frameCRC.update<int>(x);
  _frameCRC.update<int>(y);
  _frameCRC.update<int>(w);
  _frameCRC.update<int>(h);
  _frameCRC.update<uint8_t>(bits, (w * h + 7) / 8);

  uint16_t canvasColor = (_curr_color == GxEPD_BLACK) ? 0 : 1;
  uint16_t startX = (int)((x + offset_x) * scale_x);
  uint16_t startY = (int)((y + offset_y) * scale_y);
  uint16_t widthInBytes = (w + 7) / 8;

  for (uint16_t by = 0; by < h; by++) {
    int y1 = startY + (int)(by * scale_y);
    int y2 = startY + (int)((by + 1) * scale_y);
    int block_h = y2 - y1;

    for (uint16_t bx = 0; bx < w; bx++) {
      int x1 = startX + (int)(bx * scale_x);
      int x2 = startX + (int)((bx + 1) * scale_x);
      int block_w = x2 - x1;

      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;

      if (bitSet) {
        _canvas->fillRect(x1, y1, block_w, block_h, canvasColor);
      }
    }
  }
}

uint16_t FastEPDDisplay::getTextWidth(const char* str) {
  if (!_canvas || !str) return 0;
  int16_t x1, y1;
  uint16_t w, h;
  _canvas->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return (uint16_t)ceil((w + 1) / scale_x);
}

void FastEPDDisplay::endFrame() {
  if (!_epd || !_canvas) return;

  uint32_t crc = _frameCRC.finalize();
  if (crc == _lastCRC) {
    return;  // Frame unchanged, skip display update
  }
  _lastCRC = crc;

  // Copy GFXcanvas1 buffer to FastEPD's current buffer — direct copy.
  // Both use same polarity: bit 1 = white, bit 0 = black.
  // (Meshtastic inverts because OLEDDisplay uses opposite convention — not us.)
  uint8_t* src = _canvas->getBuffer();
  uint8_t* dst = _epd->currentBuffer();
  size_t bufSize = ((uint32_t)EPD_WIDTH * EPD_HEIGHT) / 8;

  if (!src || !dst) return;

  memcpy(dst, src, bufSize);

  // fullUpdate(true) is the only refresh mode that gives clean transitions
  // on the ED047TC1 panel. CLEAR_FAST causes ghosting/mashing on this hardware.
  // The brief white flash between frames is inherent to this panel's waveform.
  _epd->fullUpdate(true);
  _epd->backupPlane();
}