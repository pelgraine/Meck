#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <helpers/ESP32Board.h>

// CrowPanel 7.0" Advance Series
//
// V1.0: PCA9557/TCA9534 I/O expander at 0x18 for display power/reset/backlight
// V1.2: STC8H1K28 MCU at 0x30 (6-step brightness)
// V1.3: STC8H1K28 MCU at 0x30 (246-step brightness)
//
// I2C bus (shared between touch GT911, RTC at 0x51, and I/O controller)
// SDA = IO15, SCL = IO16
//
// Function select switch (active-low DIP switches on PCB rear):
//   S1=0 S0=0 → MIC & SPK (IO4/5/6 to speaker, IO19/20 to mic)
//   S1=0 S0=1 → WM (IO4/5/6 + IO19/20 to wireless module)  ← REQUIRED FOR LORA
//   S1=1 S0=1 → MIC & TF Card (IO4/5/6 to SD, IO19/20 to mic)

#define PIN_BOARD_SDA       15
#define PIN_BOARD_SCL       16

// Touch pins (GT911 at 0x5D)
#define PIN_TOUCH_SDA       PIN_BOARD_SDA
#define PIN_TOUCH_SCL       PIN_BOARD_SCL
#define PIN_TOUCH_INT       1    // IO1_TP_INT (active low pulse, not level-based)
#define PIN_TOUCH_RST       -1   // Controlled via STC8H1K28 P1.7 (v1.3) or TCA9534 (v1.0)

// STC8H1K28 I2C address (v1.2/v1.3 only)
#define STC8H_ADDR          0x30

class CrowPanel70Board : public ESP32Board {
public:
  void begin() {
    // NOTE: Wire.begin(SDA, SCL) is called in target.cpp radio_init() BEFORE
    // lcd.init(), to ensure correct init order for v1.3 STC8H1K28 backlight.
    // Do NOT call Wire.begin() here — it would conflict with LovyanGFX's
    // internal Wire usage for the GT911 touch controller.

    ESP32Board::begin();
  }

  const char* getManufacturerName() const override {
#ifdef CROWPANEL_V13
    return "CrowPanel 7.0 V1.3";
#else
    return "CrowPanel 7.0";
#endif
  }

  // --- STC8H1K28 control (v1.3) ---
  // These are safe to call on all versions; on v1.0 they'll talk to
  // a nonexistent I2C device and silently fail.

  void setBacklightBrightness(uint8_t level) {
    // 0 = max, 244 = min, 245 = off
    Wire.beginTransmission(STC8H_ADDR);
    Wire.write(level);
    Wire.endTransmission();
  }

  void buzzerOn() {
    Wire.beginTransmission(STC8H_ADDR);
    Wire.write(246);
    Wire.endTransmission();
  }

  void buzzerOff() {
    Wire.beginTransmission(STC8H_ADDR);
    Wire.write(247);
    Wire.endTransmission();
  }
};