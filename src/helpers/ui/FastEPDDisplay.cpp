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
#define FULL_SLOW_PERIOD 1  // every frame -- eliminates ghosting (increase to 2+ for less flashing)

// ---------------------------------------------------------------------------
// UTF-8 helpers for diacritic / extended Latin rendering
// ---------------------------------------------------------------------------

static uint32_t utf8Decode(const uint8_t* s, int len, int* consumed) {
  if (len <= 0) { *consumed = 0; return 0; }
  uint8_t b = s[0];
  if (b < 0x80) { *consumed = 1; return b; }
  if ((b & 0xE0) == 0xC0 && len >= 2 && (s[1] & 0xC0) == 0x80) {
    *consumed = 2;
    return ((uint32_t)(b & 0x1F) << 6) | (s[1] & 0x3F);
  }
  if ((b & 0xF0) == 0xE0 && len >= 3 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
    *consumed = 3;
    return ((uint32_t)(b & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
  }
  if ((b & 0xF8) == 0xF0 && len >= 4 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
    *consumed = 4;
    return ((uint32_t)(b & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
  }
  *consumed = 1;
  return 0xFFFD;
}

static char foldToAscii(uint32_t cp) {
  if (cp < 0x80) return (char)cp;
  if (cp >= 0xC0 && cp <= 0xFF) {
    static const char f[64] = {
      'A','A','A','A','A','A','A','C','E','E','E','E','I','I','I','I',
      'D','N','O','O','O','O','O',  0 ,'O','U','U','U','U','Y',  0 ,'s',
      'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',
      'd','n','o','o','o','o','o',  0 ,'o','u','u','u','u','y',  0 ,'y'
    };
    return f[cp - 0xC0];
  }
  if (cp >= 0x100 && cp <= 0x17F) {
    switch (cp) {
      case 0x100: case 0x102: case 0x104: return 'A';
      case 0x101: case 0x103: case 0x105: return 'a';
      case 0x106: case 0x108: case 0x10A: case 0x10C: return 'C';
      case 0x107: case 0x109: case 0x10B: case 0x10D: return 'c';
      case 0x10E: case 0x110: return 'D';
      case 0x10F: case 0x111: return 'd';
      case 0x112: case 0x114: case 0x116: case 0x118: case 0x11A: return 'E';
      case 0x113: case 0x115: case 0x117: case 0x119: case 0x11B: return 'e';
      case 0x11C: case 0x11E: case 0x120: case 0x122: return 'G';
      case 0x11D: case 0x11F: case 0x121: case 0x123: return 'g';
      case 0x124: case 0x126: return 'H'; case 0x125: case 0x127: return 'h';
      case 0x128: case 0x12A: case 0x12C: case 0x12E: case 0x130: return 'I';
      case 0x129: case 0x12B: case 0x12D: case 0x12F: case 0x131: return 'i';
      case 0x134: return 'J'; case 0x135: return 'j';
      case 0x136: return 'K'; case 0x137: case 0x138: return 'k';
      case 0x139: case 0x13B: case 0x13D: case 0x13F: case 0x141: return 'L';
      case 0x13A: case 0x13C: case 0x13E: case 0x140: case 0x142: return 'l';
      case 0x143: case 0x145: case 0x147: return 'N';
      case 0x144: case 0x146: case 0x148: case 0x149: return 'n';
      case 0x14C: case 0x14E: case 0x150: return 'O';
      case 0x14D: case 0x14F: case 0x151: return 'o';
      case 0x152: return 'O'; case 0x153: return 'o';
      case 0x154: case 0x156: case 0x158: return 'R';
      case 0x155: case 0x157: case 0x159: return 'r';
      case 0x15A: case 0x15C: case 0x15E: case 0x160: return 'S';
      case 0x15B: case 0x15D: case 0x15F: case 0x161: return 's';
      case 0x162: case 0x164: case 0x166: return 'T';
      case 0x163: case 0x165: case 0x167: return 't';
      case 0x168: case 0x16A: case 0x16C: case 0x16E: case 0x170: case 0x172: return 'U';
      case 0x169: case 0x16B: case 0x16D: case 0x16F: case 0x171: case 0x173: return 'u';
      case 0x174: return 'W'; case 0x175: return 'w';
      case 0x176: case 0x178: return 'Y'; case 0x177: return 'y';
      case 0x179: case 0x17B: case 0x17D: return 'Z';
      case 0x17A: case 0x17C: case 0x17E: return 'z';
    }
  }
  return 0;
}

static bool hasNonAscii(const char* str) {
  for (int i = 0; str[i]; i++) {
    if ((uint8_t)str[i] >= 0x80) return true;
  }
  return false;
}

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
  _frameCRC.update<bool>(_darkMode);
  _frameCRC.update<bool>(_portraitMode);
  _frameCRC.update<uint8_t>(_fontStyle);
}

void FastEPDDisplay::setTextSize(int sz) {
  if (!_canvas) return;
  _frameCRC.update<int>(sz);

  // Check for custom font style first (Noto Sans, Montserrat)
  const GFXfont* customFont = meckGetFont(_fontStyle, sz);
  if (customFont) {
    _canvas->setFont(customFont);
    _currentFont = customFont;
    // textSize 5 (clock face) uses x5 scaling even with custom fonts
    _currentTextScale = (sz == 5) ? 5 : 1;
    _canvas->setTextSize(_currentTextScale);
    return;
  }

  // Classic style (or fallback) -- original FreeSans/FreeSerif fonts
  // Toggle between font families via -D MECK_SERIF_FONT build flag
  _currentTextScale = 1;
  switch(sz) {
    case 0:  // Body text -- reader content, settings rows, messages, footers
#ifdef MECK_SERIF_FONT
      _canvas->setFont(&FreeSerif12pt7b);
      _currentFont = &FreeSerif12pt7b;
#else
      _canvas->setFont(&FreeSans12pt7b);
      _currentFont = &FreeSans12pt7b;
#endif
      _canvas->setTextSize(1);
      break;
    case 1:  // Headings -- screen titles, channel names (bold, same height as body)
      _canvas->setFont(&FreeSansBold12pt7b);
      _currentFont = &FreeSansBold12pt7b;
      _canvas->setTextSize(1);
      break;
    case 2:  // Large bold -- MSG count, tile letters
      _canvas->setFont(&FreeSansBold18pt7b);
      _currentFont = &FreeSansBold18pt7b;
      _canvas->setTextSize(1);
      break;
    case 3:  // Extra large -- splash screen title
      _canvas->setFont(&FreeSansBold24pt7b);
      _currentFont = &FreeSansBold24pt7b;
      _canvas->setTextSize(1);
      break;
    case 5:  // Clock face -- lock screen (FreeSansBold24pt scaled 5x)
      _canvas->setFont(&FreeSansBold24pt7b);
      _currentFont = &FreeSansBold24pt7b;
      _currentTextScale = 5;
      _canvas->setTextSize(5);
      break;
    default:
#ifdef MECK_SERIF_FONT
      _canvas->setFont(&FreeSerif12pt7b);
      _currentFont = &FreeSerif12pt7b;
#else
      _canvas->setFont(&FreeSans12pt7b);
      _currentFont = &FreeSans12pt7b;
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

  if (!hasNonAscii(str)) {
    // Pure ASCII fast path
    _canvas->print(str);
    return;
  }

  const uint8_t* s = (const uint8_t*)str;
  int len = strlen(str);
  int pos = 0;
  bool has8bFont = _currentFont && _currentFont->last > 0xFF;

  while (pos < len) {
    uint8_t b = s[pos];
    if (b < 0x80) {
      _canvas->write(b);
      pos++;
    } else {
      int consumed;
      uint32_t cp = utf8Decode(s + pos, len - pos, &consumed);
      if (has8bFont && cp >= _currentFont->first && cp <= _currentFont->last) {
        drawGlyphAtCursor((uint16_t)cp);
      } else if (!has8bFont) {
        char folded = foldToAscii(cp);
        if (folded) _canvas->write((uint8_t)folded);
      }
      pos += consumed;
    }
  }
}

void FastEPDDisplay::drawGlyphAtCursor(uint16_t cp) {
  if (!_canvas || !_currentFont || cp < _currentFont->first || cp > _currentFont->last) return;

  uint16_t idx = cp - _currentFont->first;
  GFXglyph* glyph = &_currentFont->glyph[idx];

  uint8_t gw = glyph->width;
  uint8_t gh = glyph->height;
  int8_t xo = glyph->xOffset;
  int8_t yo = glyph->yOffset;
  uint16_t bo = glyph->bitmapOffset;
  uint8_t xa = glyph->xAdvance;

  int16_t cx = _canvas->getCursorX();
  int16_t cy = _canvas->getCursorY();

  // Canvas color: 0 = black, 1 = white
  uint16_t canvasColor = (_curr_color == GxEPD_BLACK) ? 0 : 1;

  if (gw > 0 && gh > 0) {
    const uint8_t* bitmap = _currentFont->bitmap;
    uint8_t bit = 0, bits = 0;

    for (uint8_t yy = 0; yy < gh; yy++) {
      for (uint8_t xx = 0; xx < gw; xx++) {
        if (!(bit++ & 7)) bits = bitmap[bo++];
        if (bits & 0x80) {
          if (_currentTextScale == 1) {
            _canvas->drawPixel(cx + xo + xx, cy + yo + yy, canvasColor);
          } else {
            _canvas->fillRect(
              cx + (xo + xx) * _currentTextScale,
              cy + (yo + yy) * _currentTextScale,
              _currentTextScale, _currentTextScale, canvasColor);
          }
        }
        bits <<= 1;
      }
    }
  }

  _canvas->setCursor(cx + xa * _currentTextScale, cy);
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

  if (!hasNonAscii(str)) {
    int16_t x1, y1;
    uint16_t w, h;
    _canvas->getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    return (uint16_t)ceil((w + 1) / scale_x);
  }

  bool has8bFont = _currentFont && _currentFont->last > 0xFF;

  if (has8bFont) {
    const uint8_t* s = (const uint8_t*)str;
    int len = strlen(str);
    int pos = 0;
    int totalAdv = 0;
    while (pos < len) {
      int consumed;
      uint32_t cp = utf8Decode(s + pos, len - pos, &consumed);
      if (cp >= _currentFont->first && cp <= _currentFont->last) {
        totalAdv += _currentFont->glyph[cp - _currentFont->first].xAdvance * _currentTextScale;
      }
      pos += consumed;
    }
    return (uint16_t)ceil((totalAdv + 1) / scale_x);
  }

  // Classic/7b: fold to ASCII, then measure
  char folded[256];
  const uint8_t* s = (const uint8_t*)str;
  int len = strlen(str);
  int pos = 0, fi = 0;
  while (pos < len && fi < 254) {
    uint8_t b = s[pos];
    if (b < 0x80) {
      folded[fi++] = (char)b;
      pos++;
    } else {
      int consumed;
      uint32_t cp = utf8Decode(s + pos, len - pos, &consumed);
      char fc = foldToAscii(cp);
      if (fc) folded[fi++] = fc;
      pos += consumed;
    }
  }
  folded[fi] = 0;
  int16_t x1, y1;
  uint16_t w, h;
  _canvas->getTextBounds(folded, 0, 0, &x1, &y1, &w, &h);
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
  uint8_t* src = _canvas->getBuffer();
  uint8_t* dst = _epd->currentBuffer();
  size_t bufSize = ((uint32_t)EPD_WIDTH * EPD_HEIGHT) / 8;

  if (!src || !dst) return;

  memcpy(dst, src, bufSize);

  // Dark mode: invert every byte in the buffer (white↔black)
  if (_darkMode) {
    for (size_t i = 0; i < bufSize; i++) dst[i] = ~dst[i];
  }

  // Refresh strategy:
  //   partialUpdate(true) — no flash, differential, keeps previous buffer
  //   fullUpdate(false)   — brief flash, clears ghosting (CLEAR_FAST)
  //   fullUpdate(true)    — full white flash, cleanest (boot only)
  //
  // Use partial for most frames. Periodic full refresh every N frames
  // to clear accumulated ghosting artifacts.
  _fullRefreshCount++;
  if (_forcePartial) {
    // VKB typing mode — no flash, fast differential update
    _epd->partialUpdate(true);
    _fullRefreshCount = 0;  // Reset so next non-partial frame does full refresh
  } else if (_fullRefreshCount >= FULL_SLOW_PERIOD) {
    _fullRefreshCount = 0;
    _epd->fullUpdate(true);   // Full clean refresh — clears all ghosting
  } else {
    _epd->partialUpdate(true);  // No flash — differential
  }
  _epd->backupPlane();
}

void FastEPDDisplay::translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
  if (_currentFont && _currentFont->last > 0xFF) {
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return;
  }
  // Classic/7b: fold accented chars to ASCII
  size_t j = 0;
  const uint8_t* s = (const uint8_t*)src;
  int len = strlen(src);
  int pos = 0;
  while (pos < len && j < dest_size - 1) {
    uint8_t b = s[pos];
    if (b >= 32 && b <= 126) {
      dest[j++] = (char)b;
      pos++;
    } else if (b >= 0x80) {
      int consumed;
      uint32_t cp = utf8Decode(s + pos, len - pos, &consumed);
      char folded = foldToAscii(cp);
      if (folded) dest[j++] = folded;
      pos += consumed;
    } else {
      pos++;
    }
  }
  dest[j] = 0;
}

void FastEPDDisplay::setDarkMode(bool dark) {
  _darkMode = dark;
  _lastCRC = 0;  // Force redraw
  Serial.printf("[FastEPD] Dark mode: %s\n", dark ? "ON" : "OFF");
}

void FastEPDDisplay::setPortraitMode(bool portrait) {
  if (_portraitMode == portrait) return;
  _portraitMode = portrait;

  if (!_canvas) return;

  if (portrait) {
    _canvas->setRotation(3);  // 270° CW — USB-C on right when held portrait
    scale_x = (float)EPD_HEIGHT / 128.0f;  // 540 / 128 = 4.21875
    scale_y = (float)EPD_WIDTH / 128.0f;   // 960 / 128 = 7.5
    Serial.printf("[FastEPD] Portrait mode: ON (logical %dx%d, scale %.2f x %.2f)\n",
                  EPD_HEIGHT, EPD_WIDTH, scale_x, scale_y);
  } else {
    _canvas->setRotation(0);  // Normal landscape
    scale_x = (float)EPD_WIDTH / 128.0f;   // 960 / 128 = 7.5
    scale_y = (float)EPD_HEIGHT / 128.0f;  // 540 / 128 = 4.21875
    Serial.printf("[FastEPD] Portrait mode: OFF (logical %dx%d, scale %.2f x %.2f)\n",
                  EPD_WIDTH, EPD_HEIGHT, scale_x, scale_y);
  }
  _lastCRC = 0;  // Force redraw
}