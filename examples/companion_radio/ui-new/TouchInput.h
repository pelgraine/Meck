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

// The CST328 pulses its INT line low (falling edge) when a new touch report is
// ready. We attach an edge interrupt that sets this flag, and only read/ack the
// controller when it has fired -- mirroring the Hynitron driver, which reads
// only on the INT edge instead of blind-polling. Blind polling at 50 Hz was the
// source of the i2cRead -1/263 errors (reading when no report was pending).
namespace {
  volatile bool _touchIrqFired = false;
  void IRAM_ATTR _touchIsr() { _touchIrqFired = true; }
}

class TouchInput {
public:
  static const uint8_t TOUCH_ADDR = 0x1A;

  TouchInput(TwoWire* wire = &Wire)
    : _wire(wire), _intPin(-1), _initialized(false), _debugCount(0) {}

  bool begin(int intPin) {
    _intPin = intPin;
    pinMode(_intPin, INPUT);
    // On ESP32 every GPIO is interrupt-capable and digitalPinToInterrupt is an
    // identity macro (pin == interrupt number), but it is not always visible in
    // this header's include context -- pass the GPIO number directly.
    attachInterrupt(_intPin, _touchIsr, FALLING);

    // Verify the touch controller is present on the bus
    _wire->beginTransmission(TOUCH_ADDR);
    uint8_t err = _wire->endTransmission();
    if (err != 0) {
      Serial.printf("[Touch] CST328 not found at 0x%02X (err=%d)\n", TOUCH_ADDR, err);
      return false;
    }

    Serial.printf("[Touch] CST328 found at 0x%02X, INT=GPIO%d\n", TOUCH_ADDR, _intPin);

    // Enter normal report mode: write command register 0xD109. The Hynitron
    // driver does this once after reset (cst3xx_init -> set_workmode NOMAL_MODE).
    _wire->beginTransmission(TOUCH_ADDR);
    _wire->write(0xD1);
    _wire->write(0x09);
    _wire->endTransmission(true);

    _initialized = true;
    return true;
  }

  bool isReady() const { return _initialized; }

  // Returns true if a finger is down, fills x and y (physical display space:
  // 0-239 X, 0-319 Y). Reads only when the INT edge interrupt has fired.
  bool getPoint(int16_t &x, int16_t &y) {
    if (!_initialized) return false;

    // Only touch the bus when the INT line has signalled a new report. With no
    // pending report there is nothing to read, and reading anyway is what
    // produced the i2cRead -1/263 errors.
    if (!_touchIrqFired) return false;
    _touchIrqFired = false;

    uint8_t buf[7];
    memset(buf, 0, sizeof(buf));

    // Write register address 0xD000.
    // Use a STOP here (true), not a repeated start (false): the repeated-start
    // combined read (i2cWriteReadNonStop) is what was throwing the -1/263 errors
    // on this bus, while the keyboard's stop-then-read pattern never errors.
    _wire->beginTransmission(TOUCH_ADDR);
    _wire->write(0xD0);
    _wire->write(0x00);
    if (_wire->endTransmission(true) != 0) return false;

    // Read 7 bytes of touch data
    uint8_t received = _wire->requestFrom(TOUCH_ADDR, (uint8_t)7);
    if (received < 7) return false;
    for (int i = 0; i < 7; i++) buf[i] = _wire->read();

    // Acknowledge the report: write 0xAB to register 0xD000 so the controller
    // releases its buffer for the next frame. Required after EVERY read of
    // 0xD000 -- without it the CST328 re-serves stale frames (phantom touches)
    // and eventually NAKs the read. Matches the Hynitron driver tail-end write.
    _wire->beginTransmission(TOUCH_ADDR);
    _wire->write(0xD0);
    _wire->write(0x00);
    _wire->write(0xAB);
    _wire->endTransmission(true);

    // Check byte: a valid frame always has buf[6] == 0xAB. Reject anything else.
    if (buf[6] != 0xAB) return false;

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
};

#endif // TOUCH_INPUT_H
#endif // HAS_TOUCHSCREEN