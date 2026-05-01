#include "GxEPDDisplay.h"

#ifdef EXP_PIN_BACKLIGHT
  #include <PCA9557.h>
  extern PCA9557 expander;
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

// ---------------------------------------------------------------------------
// UTF-8 helpers for diacritic / extended Latin rendering
// ---------------------------------------------------------------------------

// Decode one UTF-8 character. Returns codepoint, sets *consumed to byte count.
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
  // Invalid lead byte or truncated sequence -- skip one byte
  *consumed = 1;
  return 0xFFFD; // replacement character
}

// Fold a Unicode codepoint to its ASCII base letter.
// Returns the ASCII char, or 0 if no folding exists.
static char foldToAscii(uint32_t cp) {
  if (cp < 0x80) return (char)cp;

  // Latin-1 Supplement (U+00C0 -- U+00FF)
  if (cp >= 0xC0 && cp <= 0xFF) {
    //                  C0                                  CF
    static const char f[64] = {
      'A','A','A','A','A','A','A','C','E','E','E','E','I','I','I','I',
      'D','N','O','O','O','O','O',  0 ,'O','U','U','U','U','Y',  0 ,'s',
      'a','a','a','a','a','a','a','c','e','e','e','e','i','i','i','i',
      'd','n','o','o','o','o','o',  0 ,'o','u','u','u','u','y',  0 ,'y'
    };
    return f[cp - 0xC0];
  }

  // Latin Extended-A (U+0100 -- U+017F) -- Czech carons, Polish, etc.
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
      case 0x124: case 0x126: return 'H';
      case 0x125: case 0x127: return 'h';
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
      case 0x152: return 'O'; case 0x153: return 'o'; // OE ligature
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

  return 0; // not foldable
}

// Check if a string contains any non-ASCII bytes
static bool hasNonAscii(const char* str) {
  for (int i = 0; str[i]; i++) {
    if ((uint8_t)str[i] >= 0x80) return true;
  }
  return false;
}

#ifdef ESP32
  // E-ink and LoRa SHARE the same SPI bus (SCK=36, MOSI=33)
  // They MUST use the same SPI peripheral (HSPI) to avoid GPIO conflicts
  // Different chip selects allow both to coexist: E-ink CS=34, LoRa CS=3
  SPIClass displaySpi(HSPI);
#endif

bool GxEPDDisplay::begin() {
#ifdef ESP32
  // Initialize HSPI with shared pins
  // SCK=36, MISO=47 (for LoRa receive), MOSI=33, SS=34 (e-ink CS)
  displaySpi.begin(PIN_DISPLAY_SCLK, 47, PIN_DISPLAY_MOSI, PIN_DISPLAY_CS);
  
  // Tell GxEPD2 to use our SPI instance
  // Using slower speed (4MHz) for reliable e-ink communication
  display.epd2.selectSPI(displaySpi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
#elif defined(LILYGO_TECHO)
  // T-Echo Lite: display on SPI1 (pins 19/20), LoRa on SPI (pins 13/15/17)
  SPI1.begin();
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
#endif

  // Initialize with:
  // - 115200 baud for debug serial
  // - initial=true (do initial update)
  // - reset_duration=2 (ms)
  // - pulldown_rst_mode=false
  display.init(115200, true, 2, false);
  display.setRotation(DISPLAY_ROTATION);
  setTextSize(1);  // Default to size 1
#ifdef EINK_FULL_REFRESH_ONLY
  display.setFullWindow();
  display.fillScreen(GxEPD_WHITE);
  display.display(false);  // Full refresh (SSD1681 doesn't support partial)
#else
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.fillScreen(GxEPD_WHITE);
  display.display(true);
#endif
  
  #if DISP_BACKLIGHT
  digitalWrite(DISP_BACKLIGHT, LOW);
  pinMode(DISP_BACKLIGHT, OUTPUT);
  #endif
  
  _init = true;
  _isOn = true;  // Set display as "on" after initialization
  return true;
}

void GxEPDDisplay::turnOn() {
  if (!_init) begin();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN) && !defined(LilyGo_TDeck_Pro)
  digitalWrite(DISP_BACKLIGHT, HIGH);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, HIGH);
#endif
  _isOn = true;
}

void GxEPDDisplay::turnOff() {
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN) && !defined(LilyGo_TDeck_Pro)
  // Only toggle backlight on boards that actually have one.
  // T-Deck Pro defines DISP_BACKLIGHT (GPIO 45) but has no physical backlight —
  // setting _isOn=false would stop the render loop, making the device appear frozen.
  digitalWrite(DISP_BACKLIGHT, LOW);
  _isOn = false;
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, LOW);
  _isOn = false;
#endif
  // T-Deck Pro: _isOn stays true — e-ink has no backlight, render loop must keep running
}

void GxEPDDisplay::clear() {
  if (_darkMode) {
    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(GxEPD_WHITE);
  } else {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
  }
  display_crc.reset();
}

void GxEPDDisplay::startFrame(Color bkg) {
  if (_darkMode) {
    display.fillScreen(GxEPD_BLACK);
    display.setTextColor(_curr_color = GxEPD_WHITE);
  } else {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(_curr_color = GxEPD_BLACK);
  }
  display_crc.reset();
}

void GxEPDDisplay::setTextSize(int sz) {
  display_crc.update<int>(sz);
  display_crc.update<uint8_t>(_fontStyle);

  // Check for custom font style first (Noto Sans, Montserrat)
#ifdef HAS_MECK_FONTS
  const GFXfont* customFont = meckGetFont(_fontStyle, sz);
  if (customFont) {
    display.setFont(customFont);
    _currentFont = customFont;
    // textSize 5 (clock face) uses x2 scaling even with custom fonts
    _currentTextScale = (sz == 5) ? 2 : 1;
    display.setTextSize(_currentTextScale);
    return;
  }
#endif

  // Classic style (or fallback) -- original FreeSans fonts
  _currentTextScale = 1;
  switch(sz) {
    case 0:  // Tiny - built-in 6x8 pixel font
      display.setFont(NULL);
      _currentFont = nullptr;
      display.setTextSize(1);
      break;
    case 1:  // Small - use 9pt (was 9pt)
      display.setFont(&FreeSans9pt7b);
      _currentFont = &FreeSans9pt7b;
      display.setTextSize(1);
      break;
    case 2:  // Medium Bold - use 9pt bold instead of 12pt
      display.setFont(&FreeSans9pt7b);
      _currentFont = &FreeSans9pt7b;
      display.setTextSize(1);
      break;
    case 3:  // Large - use 12pt instead of 18pt
      display.setFont(&FreeSansBold12pt7b);
      _currentFont = &FreeSansBold12pt7b;
      display.setTextSize(1);
      break;
    case 5:  // Extra Large - lock screen clock face
      display.setFont(&FreeSansBold12pt7b);
      _currentFont = &FreeSansBold12pt7b;
      _currentTextScale = 2;
      display.setTextSize(2);  // GxEPD2 native 2x scaling on 12pt bold
      break;
    default:
      display.setFont(&FreeSans9pt7b);
      _currentFont = &FreeSans9pt7b;
      display.setTextSize(1);
      break;
  }
}

void GxEPDDisplay::setColor(Color c) {
  display_crc.update<Color> (c);
  if (_darkMode) {
    // Dark mode: DARK = black (background), LIGHT/GREEN/YELLOW = white (foreground)
    if (c == DARK) {
      display.setTextColor(_curr_color = GxEPD_BLACK);
    } else {
      display.setTextColor(_curr_color = GxEPD_WHITE);
    }
  } else {
    // Normal e-paper: DARK = white (background), LIGHT/GREEN/YELLOW = black (foreground)
    if (c == DARK) {
      display.setTextColor(_curr_color = GxEPD_WHITE);
    } else {
      display.setTextColor(_curr_color = GxEPD_BLACK);
    }
  }
}

void GxEPDDisplay::setCursor(int x, int y) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  // Add extra offset (+5) to push text baseline down, preventing ascenders from overlapping elements above
  display.setCursor((x+offset_x)*scale_x, (y+offset_y+5)*scale_y);
}

void GxEPDDisplay::print(const char* str) {
  display_crc.update<char>(str, strlen(str));

  if (!hasNonAscii(str)) {
    // Pure ASCII fast path -- no decoding needed
    display.print(str);
    return;
  }

  const uint8_t* s = (const uint8_t*)str;
  int len = strlen(str);
  int pos = 0;

  // Check if current font is an 8b font (covers codepoints > 0xFF)
  bool has8bFont = _currentFont && _currentFont->last > 0xFF;

  while (pos < len) {
    uint8_t b = s[pos];

    if (b < 0x80) {
      // ASCII: use normal Adafruit GFX path (handles newlines etc.)
      display.write(b);
      pos++;
    } else {
      // Multi-byte UTF-8 sequence
      int consumed;
      uint32_t cp = utf8Decode(s + pos, len - pos, &consumed);

      if (has8bFont && cp >= _currentFont->first && cp <= _currentFont->last) {
        // Render directly from 8b font glyph table
        drawGlyphAtCursor((uint16_t)cp);
      } else if (!has8bFont) {
        // Classic/7b font -- fold to ASCII
        char folded = foldToAscii(cp);
        if (folded) display.write((uint8_t)folded);
      }
      // else: codepoint outside font range, skip
      pos += consumed;
    }
  }
}

// Render one glyph from the current 8b font at the display cursor position.
// Mimics Adafruit_GFX::drawChar() but supports 16-bit codepoints.
void GxEPDDisplay::drawGlyphAtCursor(uint16_t cp) {
  if (!_currentFont || cp < _currentFont->first || cp > _currentFont->last) return;

  uint16_t idx = cp - _currentFont->first;
  GFXglyph* glyph = &_currentFont->glyph[idx];

  uint8_t gw = glyph->width;
  uint8_t gh = glyph->height;
  int8_t xo = glyph->xOffset;
  int8_t yo = glyph->yOffset;
  uint16_t bo = glyph->bitmapOffset;
  uint8_t xa = glyph->xAdvance;

  int16_t cx = display.getCursorX();
  int16_t cy = display.getCursorY();

  if (gw > 0 && gh > 0) {
    const uint8_t* bitmap = _currentFont->bitmap;
    uint8_t bit = 0, bits = 0;

    for (uint8_t yy = 0; yy < gh; yy++) {
      for (uint8_t xx = 0; xx < gw; xx++) {
        if (!(bit++ & 7)) bits = bitmap[bo++];
        if (bits & 0x80) {
          if (_currentTextScale == 1) {
            display.drawPixel(cx + xo + xx, cy + yo + yy, _curr_color);
          } else {
            // Scaled rendering (clock face etc.)
            display.fillRect(
              cx + (xo + xx) * _currentTextScale,
              cy + (yo + yy) * _currentTextScale,
              _currentTextScale, _currentTextScale, _curr_color);
          }
        }
        bits <<= 1;
      }
    }
  }

  // Advance cursor by xAdvance (scaled)
  display.setCursor(cx + xa * _currentTextScale, cy);
}

void GxEPDDisplay::fillRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.fillRect((x+offset_x)*scale_x, (y+offset_y)*scale_y, w*scale_x, h*scale_y, _curr_color);
}

void GxEPDDisplay::drawRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.drawRect((x+offset_x)*scale_x, (y+offset_y)*scale_y, w*scale_x, h*scale_y, _curr_color);
}

void GxEPDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display_crc.update<uint8_t>(bits, w * h / 8);
  // Calculate the base position in display coordinates (with offset applied)
  uint16_t startX = (x + offset_x) * scale_x;
  uint16_t startY = (y + offset_y) * scale_y;
  
  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;
  
  // Process the bitmap row by row
  for (uint16_t by = 0; by < h; by++) {
    // Calculate the target y-coordinates for this logical row
    int y1 = startY + (int)(by * scale_y);
    int y2 = startY + (int)((by + 1) * scale_y);
    int block_h = y2 - y1;
    
    // Scan across the row bit by bit
    for (uint16_t bx = 0; bx < w; bx++) {
      // Calculate the target x-coordinates for this logical column
      int x1 = startX + (int)(bx * scale_x);
      int x2 = startX + (int)((bx + 1) * scale_x);
      int block_w = x2 - x1;
      
      // Get the current bit
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;
      
      // If the bit is set, draw a block of pixels
      if (bitSet) {
        // Draw the block as a filled rectangle
        display.fillRect(x1, y1, block_w, block_h, _curr_color);
      }
    }
  }
}

uint16_t GxEPDDisplay::getTextWidth(const char* str) {
  if (!hasNonAscii(str)) {
    // Pure ASCII fast path
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    return ceil((w + 1) / scale_x);
  }

  bool has8bFont = _currentFont && _currentFont->last > 0xFF;

  if (has8bFont) {
    // 8b font: sum xAdvance for each decoded codepoint
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
    return ceil((totalAdv + 1) / scale_x);
  }

  // Classic/7b: fold to ASCII, then measure the folded string
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
  display.getTextBounds(folded, 0, 0, &x1, &y1, &w, &h);
  return ceil((w + 1) / scale_x);
}

void GxEPDDisplay::endFrame() {
  uint32_t crc = display_crc.finalize();
  if (crc != last_display_crc_value) {
#ifdef EINK_FULL_REFRESH_ONLY
    display.display(false);  // Full refresh (SSD1681 doesn't support partial)
#else
    display.display(true);   // Partial refresh
#endif
    last_display_crc_value = crc;
  }
}

void GxEPDDisplay::translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
  // If an 8b font is active, pass through UTF-8 bytes (font can render them)
  if (_currentFont && _currentFont->last > 0xFF) {
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
    return;
  }

  // Classic/7b font: fold accented chars to ASCII base letters
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
      pos++;  // skip control chars
    }
  }
  dest[j] = 0;
}