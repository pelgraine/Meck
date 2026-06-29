#pragma once

// =============================================================================
// LilyGo T-Watch S3 Plus - Board-level pin definitions
// Source: LilyGo hardware doc (lilygo-t-watch-s3-plus.md) and the working
// MeshCore companion build.
//
// NOTE: LoRa (P_LORA_*) pins and the SX126x radio parameters are supplied as
// -D build flags in this variant's platformio.ini, matching the Meck T-Deck
// Pro convention. The display SPI/backlight and FT6336U touch pins live inline
// in TWatchS3PlusDisplay.h because LovyanGFX needs them in its panel config.
// =============================================================================

// -----------------------------------------------------------------------------
// Main I2C bus (shared): AXP2101 PMU, PCF8563 RTC, BMA423 accel, DRV2605 haptic
// The FT6336U touch panel is on a SEPARATE bus (Wire1), configured in the
// display class -- it is not on this bus.
// -----------------------------------------------------------------------------
#define I2C_SDA 10
#define I2C_SCL 11

// Aliases for ESP32Board base class compatibility
#define PIN_BOARD_SDA I2C_SDA
#define PIN_BOARD_SCL I2C_SCL

// -----------------------------------------------------------------------------
// I2C device addresses (7-bit)
// -----------------------------------------------------------------------------
#define I2C_ADDR_PMU      0x34  // AXP2101 power management
#define I2C_ADDR_RTC      0x51  // PCF8563 real-time clock
#define I2C_ADDR_ACCEL    0x19  // BMA423 accelerometer
#define I2C_ADDR_HAPTIC   0x5A  // DRV2605 haptic driver
#define I2C_ADDR_TOUCH    0x38  // FT6336U capacitive touch (on Wire1)

// -----------------------------------------------------------------------------
// Interrupt / control pins
// -----------------------------------------------------------------------------
#ifndef PIN_PMU_IRQ
  #define PIN_PMU_IRQ   21  // AXP2101 interrupt
#endif
#define PIN_RTC_IRQ     17  // PCF8563 interrupt
#define PIN_ACCEL_IRQ   14  // BMA423 interrupt

// User button -- GPIO0 "Custom Button" on the T-Watch S3 Plus (idles HIGH,
// active LOW), the same arrangement Meck uses on the T-Deck Pro.
#define PIN_USER_BTN    0

// -----------------------------------------------------------------------------
// Display dimensions (ST7789V3 IPS, 240x240)
// -----------------------------------------------------------------------------
#define LCD_HOR_SIZE 240
#define LCD_VER_SIZE 240

// -----------------------------------------------------------------------------
// Storage
// -----------------------------------------------------------------------------
// The T-Watch S3 Plus has no SD card slot. Notes/Reader/Epub screens reference
// SDCARD_CS unconditionally, so it must be defined for them to compile. -1 is a
// safe no-op on ESP32 (digitalWrite/pinMode reject out-of-range pins), and SD
// mounts will simply fail at runtime. Persistent storage arrives in Phase 3 via
// a LittleFS partition.
#define SDCARD_CS -1

// -----------------------------------------------------------------------------
// GPS (u-blox MIA-M10Q on Serial2)
// Pin values mirror the working MeshCore companion build. Power is on the
// AXP2101 BLDO1 rail and is gated at runtime via the gps_enabled pref +
// board.gpsPowerOn()/gpsPowerOff() -- it is NOT a GPIO enable line, so
// PIN_GPS_EN is deliberately left undefined (the main.cpp PIN_GPS_EN blocks
// are skipped and the watch branch toggles BLDO1 instead).
//
// NOTE: GPS RX/TX wiring labelling is ambiguous between the LilyGo doc and the
// MeshCore build. These values match the verified MeshCore config; if the
// module never acquires, swapping GPS_RX_PIN/GPS_TX_PIN is the first thing to try.
// -----------------------------------------------------------------------------
#define HAS_GPS      1
#define GPS_BAUDRATE 38400
#define GPS_RX_PIN   41
#define GPS_TX_PIN   42