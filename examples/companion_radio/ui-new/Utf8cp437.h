#pragma once
// =============================================================================
// Utf8CP437.h - UTF-8 decoding and Unicode-to-CP437 mapping
//
// The Adafruit GFX built-in 6x8 font uses the CP437 character set for codes
// 128-255. This header provides utilities to:
//   1. Decode UTF-8 multi-byte sequences into Unicode codepoints
//   2. Map Unicode codepoints to CP437 byte values for display
//
// Used by both EpubProcessor (at XHTML→text conversion time) and
// TextReaderScreen (at render time for plain .txt files).
// =============================================================================

// Map a Unicode codepoint to its CP437 equivalent byte.
// Returns the CP437 byte (0x80-0xFF) for supported accented characters,
// the codepoint itself for ASCII (0x20-0x7E), or 0 if unmappable.
inline uint8_t unicodeToCP437(uint32_t cp) {
  // ASCII passthrough
  if (cp >= 0x20 && cp < 0x7F) return (uint8_t)cp;

  switch (cp) {
    // Uppercase accented
    case 0x00C7: return 0x80;  // Ç
    case 0x00C9: return 0x90;  // É
    case 0x00C4: return 0x8E;  // Ä
    case 0x00C5: return 0x8F;  // Å
    case 0x00C6: return 0x92;  // Æ
    case 0x00D6: return 0x99;  // Ö
    case 0x00DC: return 0x9A;  // Ü
    case 0x00D1: return 0xA5;  // Ñ

    // Lowercase accented
    case 0x00E9: return 0x82;  // é
    case 0x00E2: return 0x83;  // â
    case 0x00E4: return 0x84;  // ä
    case 0x00E0: return 0x85;  // à
    case 0x00E5: return 0x86;  // å
    case 0x00E7: return 0x87;  // ç
    case 0x00EA: return 0x88;  // ê
    case 0x00EB: return 0x89;  // ë
    case 0x00E8: return 0x8A;  // è
    case 0x00EF: return 0x8B;  // ï
    case 0x00EE: return 0x8C;  // î
    case 0x00EC: return 0x8D;  // ì
    case 0x00E6: return 0x91;  // æ
    case 0x00F4: return 0x93;  // ô
    case 0x00F6: return 0x94;  // ö
    case 0x00F2: return 0x95;  // ò
    case 0x00FB: return 0x96;  // û
    case 0x00F9: return 0x97;  // ù
    case 0x00FF: return 0x98;  // ÿ
    case 0x00FC: return 0x81;  // ü
    case 0x00E1: return 0xA0;  // á
    case 0x00ED: return 0xA1;  // í
    case 0x00F3: return 0xA2;  // ó
    case 0x00FA: return 0xA3;  // ú
    case 0x00F1: return 0xA4;  // ñ

    // Currency / symbols
    case 0x00A2: return 0x9B;  // ¢
    case 0x00A3: return 0x9C;  // £
    case 0x00A5: return 0x9D;  // ¥
    case 0x00BF: return 0xA8;  // ¿
    case 0x00A1: return 0xAD;  // ¡
    case 0x00AB: return 0xAE;  // «
    case 0x00BB: return 0xAF;  // »
    case 0x00B0: return 0xF8;  // °
    case 0x00B1: return 0xF1;  // ±
    case 0x00B5: return 0xE6;  // µ
    case 0x00DF: return 0xE1;  // ß

    // Typographic (smart quotes, dashes, etc.)
    case 0x2018: case 0x2019: return '\'';  // Smart single quotes
    case 0x201C: case 0x201D: return '"';   // Smart double quotes
    case 0x2013: case 0x2014: return '-';   // En/em dash
    case 0x2010: case 0x2011: case 0x2012: case 0x2015: return '-'; // Hyphens/bars
    case 0x2026: return 0xFD;              // Ellipsis (CP437 has no …, use ²? no, skip)
    case 0x2022: return 0x07;              // Bullet → CP437 bullet
    case 0x00A0: return ' ';               // Non-breaking space
    case 0x2039: case 0x203A: return '\''; // Single guillemets
    case 0x2032: return '\'';              // Prime
    case 0x2033: return '"';               // Double prime

    default: return 0;  // Unmappable
  }
}

// Decode a single UTF-8 character from a byte buffer.
// Returns the Unicode codepoint and advances *pos past the full sequence.
// If the sequence is invalid, returns 0xFFFD (replacement char) and advances by 1.
//
// buf:    input buffer
// bufLen: total buffer length
// pos:    pointer to current position (updated on return)
inline uint32_t decodeUtf8Char(const char* buf, int bufLen, int* pos) {
  int i = *pos;
  if (i >= bufLen) return 0;

  uint8_t c = (uint8_t)buf[i];

  // ASCII (single byte)
  if (c < 0x80) {
    *pos = i + 1;
    return c;
  }

  // Continuation byte without lead byte — skip
  if (c < 0xC0) {
    *pos = i + 1;
    return 0xFFFD;
  }

  uint32_t codepoint;
  int extraBytes;

  if ((c & 0xE0) == 0xC0) {
    codepoint = c & 0x1F;
    extraBytes = 1;
  } else if ((c & 0xF0) == 0xE0) {
    codepoint = c & 0x0F;
    extraBytes = 2;
  } else if ((c & 0xF8) == 0xF0) {
    codepoint = c & 0x07;
    extraBytes = 3;
  } else {
    *pos = i + 1;
    return 0xFFFD;
  }

  // Verify we have enough bytes and they're valid continuation bytes
  if (i + extraBytes >= bufLen) {
    *pos = i + 1;
    return 0xFFFD;
  }

  for (int b = 1; b <= extraBytes; b++) {
    uint8_t cb = (uint8_t)buf[i + b];
    if ((cb & 0xC0) != 0x80) {
      *pos = i + 1;
      return 0xFFFD;
    }
    codepoint = (codepoint << 6) | (cb & 0x3F);
  }

  *pos = i + 1 + extraBytes;
  return codepoint;
}

// Check if a byte is a UTF-8 continuation byte (10xxxxxx)
inline bool isUtf8Continuation(uint8_t c) {
  return (c & 0xC0) == 0x80;
}