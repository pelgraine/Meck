#pragma once

// =============================================================================
// LilyGo T-Watch S3 (non-GPS, 470 mAh) - Board-level pin definitions
//
// Sources, in order of authority:
//   1. T_WATCH-S3 schematic (rev 25-03-24)
//   2. Xinyuan-LilyGO/TTGO_TWatch_Library, branch t-watch-s3, src/utilities.h
//   3. LilyGo hardware doc docs/hardware/lilygo-t-watch-s3.md
//
// Every pin below is identical to the T-Watch S3 Plus. The two boards differ in
// what is *populated*, not in how the ESP32-S3 is wired:
//   - no onboard GNSS (the Plus has a MIA-M10Q on BLDO1; here BLDO1 is unused
//     and GPIO 41/42 go to an optional external GPS shield)
//   - no GPIO user button; the only control is the side PWR key, which is wired
//     to the AXP2101 PWRON pin (schematic sheet 1: SW7 -> PWR_KEY -> pin 30)
//   - MAX98357A speaker amp on DLDO1, PDM mic on +3V3, IR LED on GPIO2 -- none
//     of which Meck compiles in
//
// NOTE: LoRa (P_LORA_*) pins and the SX126x radio parameters are supplied as
// -D build flags in this variant's platformio.ini, matching the T-Watch S3 Plus
// and T-Deck Pro convention. The display SPI/backlight and FT6336U touch pins
// live inline in TWatchS3Display.h because LovyanGFX needs them in its panel
// config.
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
  #define PIN_PMU_IRQ   21  // AXP2101 interrupt (open-drain, active LOW)
#endif
#define PIN_RTC_IRQ     17  // PCF8563 interrupt
#define PIN_ACCEL_IRQ   14  // BMA423 interrupt

// User button: deliberately NOT defined.
//
// The T-Watch S3 has no GPIO button. Schematic sheet 1 wires the side tact
// switch SW7 to net PWR_KEY, which lands on AXP2101 pin 30 (PWRON). Button
// events therefore arrive over I2C as PMU interrupts, not as a pin level, and
// are read by PMUButton (see PMUButton.h). MECK_PMU_BUTTON is defined in this
// variant's platformio.ini in place of PIN_USER_BTN.
//
// #define PIN_USER_BTN <none>

// -----------------------------------------------------------------------------
// Display dimensions (ST7789V 1.54" IPS, 240x240)
// -----------------------------------------------------------------------------
#define LCD_HOR_SIZE 240
#define LCD_VER_SIZE 240

// -----------------------------------------------------------------------------
// Storage
// -----------------------------------------------------------------------------
// The T-Watch S3 has no SD card slot. Notes/Reader/Epub screens reference
// SDCARD_CS unconditionally, so it must be defined for them to compile. -1 is a
// safe no-op on ESP32 (digitalWrite/pinMode reject out-of-range pins), and SD
// mounts will simply fail at runtime.
#define SDCARD_CS -1

// -----------------------------------------------------------------------------
// GPS: none. HAS_GPS and ENV_INCLUDE_GPS are left undefined / zeroed in
// platformio.ini, which drops the map screen, the GPS home page and the BLDO1
// rail control. The optional external GPS shield lands on GPIO41 (RX) /
// GPIO42 (TX) if it is ever wired up.
// -----------------------------------------------------------------------------
// #define HAS_GPS 1
// #define GPS_BAUDRATE 38400
// #define GPS_RX_PIN   41
// #define GPS_TX_PIN   42

// Fallback for code that references GPS_BAUDRATE without a HAS_GPS guard
// (e.g. MyMesh.cpp "gps.baud" CLI command)
#ifndef GPS_BAUDRATE
#define GPS_BAUDRATE 9600
#endif
