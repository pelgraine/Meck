#pragma once

// =============================================================================
// TouchInput - Minimal CST328/CST3530 touch driver for T-Deck Pro
//
// Uses raw I2C reads on the shared Wire bus. No external library needed.
// Protocol confirmed via raw serial capture from actual hardware:
//
//   Register 0xD000, 7 bytes:
//   buf[0]: event flags (0xAB = idle/no touch, other = active touch)
//   buf[1]: X coordinate high data
//   buf[2]: Y coordinate high data
//   buf[3]: X low nibble (bits 7:4) | Y low nibble (bits 3:0)
//   buf[4]: pressure
//   buf[5]: touch count (& 0x7F), typically 0x01 for single touch
//   buf[6]: 0xAB always (check byte, ignore)
//
//   Coordinate formula:
//     x = (buf[1] << 4) | ((buf[3] >> 4) & 0x0F)   → 0..239
//     y = (buf[2] << 4) | (buf[3] & 0x0F)           → 0..319
//
// Hardware: CST328 at 0x1A, INT=GPIO12, RST=GPIO38 (V1.1)
//
// Guard: HAS_TOUCHSCREEN
// =============================================================================

#ifdef HAS_TOUCHSCREEN

#ifndef TOUCH_INPUT_H
#define TOUCH_INPUT_H

#include <Arduino.h>
#include <Wire.h>

class TouchInput {
public:
  static const uint8_t TOUCH_ADDR = 0x1A;

  TouchInput(TwoWire* wire = &Wire)
    : _wire(wire), _intPin(-1), _initialized(false), _debugCount(0), _lastPoll(0) {}

  bool begin(int intPin) {
    _intPin = intPin;
    pinMode(_intPin, INPUT);

    // Verify the touch controller is present on the bus
    _wire->beginTransmission(TOUCH_ADDR);
    uint8_t err = _wire->endTransmission();
    if (err != 0) {
      Serial.printf("[Touch] CST328 not found at 0x%02X (err=%d)\n", TOUCH_ADDR, err);
      return false;
    }

    Serial.printf("[Touch] CST328 found at 0x%02X, INT=GPIO%d\n", TOUCH_ADDR, _intPin);
    _initialized = true;
    return true;
  }

  bool isReady() const { return _initialized; }

  // Poll for touch. Returns true if a finger is down, fills x and y.
  // Coordinates are in physical display space (0-239 X, 0-319 Y).
  // NOTE: CST328 INT pin is pulse-based, not level. We cannot rely on
  // digitalRead(INT) for touch state. Instead, always read and check buf[0].
  bool getPoint(int16_t &x, int16_t &y) {
    if (!_initialized) return false;

    // Rate limit: poll at most every 20ms (50 Hz) to avoid I2C bus congestion
    unsigned long now = millis();
    if (now - _lastPoll < 20) return false;
    _lastPoll = now;

    uint8_t buf[7];
    memset(buf, 0, sizeof(buf));

    // Write register address 0xD000
    _wire->beginTransmission(TOUCH_ADDR);
    _wire->write(0xD0);
    _wire->write(0x00);
    if (_wire->endTransmission(false) != 0) return false;

    // Read 7 bytes of touch data
    uint8_t received = _wire->requestFrom(TOUCH_ADDR, (uint8_t)7);
    if (received < 7) return false;
    for (int i = 0; i < 7; i++) buf[i] = _wire->read();

    // buf[0] == 0xAB means idle (no touch active)
    if (buf[0] == 0xAB) return false;

    // buf[0] == 0x00 can appear on finger-up transition — ignore
    if (buf[0] == 0x00) return false;

    // Touch count from buf[5]
    uint8_t count = buf[5] & 0x7F;
    if (count == 0 || count > 5) return false;

    // Parse coordinates (CST226/CST328 format confirmed by hardware capture)
    //   x = (buf[1] << 4) | high nibble of buf[3]
    //   y = (buf[2] << 4) | low nibble of buf[3]
    int16_t tx = ((int16_t)buf[1] << 4) | ((buf[3] >> 4) & 0x0F);
    int16_t ty = ((int16_t)buf[2] << 4) | (buf[3] & 0x0F);

    // Sanity check (panel is 240x320)
    if (tx < 0 || tx > 260 || ty < 0 || ty > 340) return false;

    // Debug: log first 20 touch events with parsed coordinates
    if (_debugCount < 50) {
      Serial.printf("[Touch] Raw: %02X %02X %02X %02X %02X %02X %02X → x=%d y=%d\n",
                    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6],
                    tx, ty);
      _debugCount++;
    }

    x = tx;
    y = ty;
    return true;
  }

private:
  TwoWire* _wire;
  int _intPin;
  bool _initialized;
  int _debugCount;
  unsigned long _lastPoll;
};

#endif // TOUCH_INPUT_H
#endif // HAS_TOUCHSCREEN