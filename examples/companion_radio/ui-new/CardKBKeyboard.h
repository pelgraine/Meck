#pragma once
// =============================================================================
// CardKBKeyboard — M5Stack CardKB (or compatible) I2C keyboard driver
//
// Polls 0x5F on the shared I2C bus via QWIIC connector.
// Maps CardKB special key codes to Meck key constants.
//
// Usage:
//   CardKBKeyboard cardkb;
//   if (cardkb.begin()) { /* detected */ }
//   char key = cardkb.readKey();  // returns 0 if no key
// =============================================================================

#if defined(LilyGo_T5S3_EPaper_Pro) && defined(MECK_CARDKB)
#ifndef CARDKB_KEYBOARD_H
#define CARDKB_KEYBOARD_H

#include <Arduino.h>
#include <Wire.h>

// I2C address (defined in variant.h, fallback here)
#ifndef CARDKB_I2C_ADDR
#define CARDKB_I2C_ADDR 0x5F
#endif

// CardKB special key codes (from M5Stack documentation)
#define CARDKB_KEY_UP      0xB5
#define CARDKB_KEY_DOWN    0xB6
#define CARDKB_KEY_LEFT    0xB4
#define CARDKB_KEY_RIGHT   0xB7
#define CARDKB_KEY_TAB     0x09
#define CARDKB_KEY_ESC     0x1B
#define CARDKB_KEY_BS      0x08
#define CARDKB_KEY_ENTER   0x0D
#define CARDKB_KEY_DEL     0x7F
#define CARDKB_KEY_FN      0x00   // Fn modifier (swallowed by CardKB internally)

class CardKBKeyboard {
public:
  CardKBKeyboard() : _detected(false) {}

  // Probe for CardKB on the I2C bus. Call after Wire.begin().
  bool begin() {
    Wire.beginTransmission(CARDKB_I2C_ADDR);
    _detected = (Wire.endTransmission() == 0);
    if (_detected) {
      Serial.println("[CardKB] Detected at 0x5F");
    }
    return _detected;
  }

  // Re-probe (e.g. for hot-plug detection every few seconds)
  bool probe() {
    Wire.beginTransmission(CARDKB_I2C_ADDR);
    _detected = (Wire.endTransmission() == 0);
    return _detected;
  }

  bool isDetected() const { return _detected; }

  // Poll for a keypress. Returns 0 if no key available.
  // Returns raw ASCII for printable chars, or Meck KEY_* constants for nav keys.
  char readKey() {
    if (!_detected) return 0;

    Wire.requestFrom((uint8_t)CARDKB_I2C_ADDR, (uint8_t)1);
    if (!Wire.available()) return 0;

    uint8_t raw = Wire.read();
    if (raw == 0) return 0;

    // Map CardKB special keys to Meck constants
    switch (raw) {
      case CARDKB_KEY_UP:    return 0xF2;  // KEY_PREV
      case CARDKB_KEY_DOWN:  return 0xF1;  // KEY_NEXT
      case CARDKB_KEY_LEFT:  return 0xF3;  // KEY_LEFT
      case CARDKB_KEY_RIGHT: return 0xF4;  // KEY_RIGHT
      case CARDKB_KEY_ENTER: return '\r';
      case CARDKB_KEY_BS:    return '\b';
      case CARDKB_KEY_DEL:   return '\b';  // Treat delete same as backspace
      case CARDKB_KEY_ESC:   return 0x1B;  // ESC — handled by caller
      case CARDKB_KEY_TAB:   return 0x09;  // Tab — available for future use
      default:
        // Printable ASCII — pass through unchanged
        if (raw >= 0x20 && raw <= 0x7E) {
          return (char)raw;
        }
        // Unknown code — ignore
        return 0;
    }
  }

private:
  bool _detected;
};

#endif // CARDKB_KEYBOARD_H
#endif // LilyGo_T5S3_EPaper_Pro && MECK_CARDKB