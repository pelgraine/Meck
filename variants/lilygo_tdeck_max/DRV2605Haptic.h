#pragma once

#include <Arduino.h>
#include <Wire.h>

// Minimal inline DRV2605 haptic driver for the T-Deck Pro MAX.
//
// The DRV2605 sits on the shared Wire bus at I2C 0x5A. Its motor supply is
// gated by the XL9555 expander (board.motorEnable()), which MUST be switched on
// before begin() or the device will not ACK.
//
// The register sequence mirrors Adafruit_DRV2605::begin() followed by
// selectLibrary(1) + setMode(INTTRIG) - i.e. exactly what the LilyGo basic.ino
// example does: an ERM motor, open-loop, internal-trigger, effect library 1.
class DRV2605Haptic {
public:
  DRV2605Haptic(uint8_t addr = 0x5A, TwoWire* wire = &Wire)
    : _addr(addr), _wire(wire) {}

  // Returns false if the DRV2605 does not ACK (e.g. motor rail still off).
  bool begin() {
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) return false;

    writeReg(REG_MODE, 0x00);        // exit standby, internal trigger
    writeReg(REG_RTPIN, 0x00);       // no real-time playback input
    writeReg(REG_WAVESEQ1, 1);       // default effect: strong click
    writeReg(REG_WAVESEQ2, 0);       // end of sequence
    writeReg(REG_OVERDRIVE, 0);
    writeReg(REG_SUSTAINPOS, 0);
    writeReg(REG_SUSTAINNEG, 0);
    writeReg(REG_BREAK, 0);
    writeReg(REG_AUDIOMAX, 0x64);
    writeReg(REG_FEEDBACK, readReg(REG_FEEDBACK) & 0x7F);   // N_ERM_LRA = 0 -> ERM
    writeReg(REG_CONTROL3, readReg(REG_CONTROL3) | 0x20);   // ERM_OPEN_LOOP = 1
    writeReg(REG_LIBRARY, 1);        // ERM effect library 1
    writeReg(REG_MODE, 0x00);        // internal trigger
    return true;
  }

  // Fire a single library effect (1 = strong click, 100%).
  void buzz(uint8_t effect = 1) {
    writeReg(REG_WAVESEQ1, effect);
    writeReg(REG_WAVESEQ2, 0);
    writeReg(REG_GO, 1);
  }

private:
  static const uint8_t REG_MODE       = 0x01;
  static const uint8_t REG_RTPIN      = 0x02;
  static const uint8_t REG_LIBRARY    = 0x03;
  static const uint8_t REG_WAVESEQ1   = 0x04;
  static const uint8_t REG_WAVESEQ2   = 0x05;
  static const uint8_t REG_GO         = 0x0C;
  static const uint8_t REG_OVERDRIVE  = 0x0D;
  static const uint8_t REG_SUSTAINPOS = 0x0E;
  static const uint8_t REG_SUSTAINNEG = 0x0F;
  static const uint8_t REG_BREAK      = 0x10;
  static const uint8_t REG_AUDIOMAX   = 0x13;
  static const uint8_t REG_FEEDBACK   = 0x1A;
  static const uint8_t REG_CONTROL3   = 0x1D;

  uint8_t   _addr;
  TwoWire*  _wire;

  void writeReg(uint8_t reg, uint8_t val) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
  }

  uint8_t readReg(uint8_t reg) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->endTransmission(false);
    _wire->requestFrom(_addr, (uint8_t)1);
    return _wire->available() ? _wire->read() : 0;
  }
};
