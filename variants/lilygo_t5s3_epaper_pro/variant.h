#pragma once

// =============================================================================
// LilyGo T5 S3 E-Paper Pro V2 (H752-01/H752-B) - Pin Definitions for Meck
//
// 4.7" parallel e-ink (ED047TC1, 960x540, 16-grey) — NO SPI display
// GT911 capacitive touch (no physical keyboard)
// SX1262 LoRa, BQ27220+BQ25896 battery, PCF85063 RTC, PCA9535 IO expander
// =============================================================================

// Board identifier
#define LilyGo_T5S3_EPaper_Pro  1
#define T5_S3_EPAPER_PRO_V2     1

// -----------------------------------------------------------------------------
// I2C Bus — shared by GT911, PCF85063, PCA9535, BQ25896, BQ27220, TPS65185
// -----------------------------------------------------------------------------
#define I2C_SDA  39
#define I2C_SCL  40

// Aliases for ESP32Board base class compatibility
#define PIN_BOARD_SDA  I2C_SDA
#define PIN_BOARD_SCL  I2C_SCL

// I2C Device Addresses
#define I2C_ADDR_GT911       0x5D  // Touch controller
#define I2C_ADDR_PCF85063    0x51  // RTC
#define I2C_ADDR_PCA9535     0x20  // IO expander (e-ink power control)
#define I2C_ADDR_BQ27220     0x55  // Fuel gauge
#define I2C_ADDR_BQ25896     0x6B  // Battery charger
#define I2C_ADDR_TPS65185    0x68  // E-ink power driver

// -----------------------------------------------------------------------------
// SPI Bus — shared by LoRa and SD card
// Different from T-Deck Pro! (T-Deck: 33/47/36, T5S3: 13/21/14)
// -----------------------------------------------------------------------------
#define BOARD_SPI_SCLK  14
#define BOARD_SPI_MISO  21
#define BOARD_SPI_MOSI  13

// -----------------------------------------------------------------------------
// LoRa Radio (SX1262)
// SPI bus shared with SD card, different chip selects
// -----------------------------------------------------------------------------
#define P_LORA_NSS    46
#define P_LORA_DIO_1  10   // IRQ
#define P_LORA_RESET   1
#define P_LORA_BUSY   47
#define P_LORA_SCLK   BOARD_SPI_SCLK
#define P_LORA_MISO   BOARD_SPI_MISO
#define P_LORA_MOSI   BOARD_SPI_MOSI
// Note: No P_LORA_EN on T5S3 — LoRa is always powered

// -----------------------------------------------------------------------------
// E-Ink Display (ED047TC1 — 8-bit parallel, NOT SPI)
// Driven by epdiy/FastEPD library via TPS65185 + PCA9535
// GxEPD2 is NOT used on this board.
// -----------------------------------------------------------------------------
// Parallel data bus (directly wired to ESP32-S3 GPIOs)
#define EP_D0   5
#define EP_D1   6
#define EP_D2   7
#define EP_D3  15
#define EP_D4  16
#define EP_D5  17
#define EP_D6  18
#define EP_D7   8

// Control signals
#define EP_CKV  48   // Clock vertical
#define EP_STH  41   // Start horizontal
#define EP_LEH  42   // Latch enable horizontal
#define EP_STV  45   // Start vertical
#define EP_CKH   4   // Clock horizontal (edge)

// E-ink power is managed by TPS65185 through PCA9535 IO expander:
//   PCA9535 IO10 -> EP_OE (output enable, source driver)
//   PCA9535 IO11 -> EP_MODE (output mode, gate driver)
//   PCA9535 IO13 -> TPS_PWRUP
//   PCA9535 IO14 -> VCOM_CTRL
//   PCA9535 IO15 -> TPS_WAKEUP
//   PCA9535 IO16 -> TPS_PWR_GOOD (input)
//   PCA9535 IO17 -> TPS_INT (input)

// Display dimensions — native resolution of ED047TC1
#define EPD_WIDTH   960
#define EPD_HEIGHT  540

// Backlight (warm-tone front-light — functional on V2!)
#define BOARD_BL_EN  11

// We do NOT define DISPLAY_CLASS or EINK_DISPLAY_MODEL here.
// The parallel display uses FastEPD, not GxEPD2.
// DISPLAY_CLASS will be defined in platformio.ini as FastEPDDisplay
// for builds that include display support.

// -----------------------------------------------------------------------------
// Touch Controller (GT911)
// No physical keyboard on this board — touch-only input
// -----------------------------------------------------------------------------
#define HAS_TOUCHSCREEN  1
#define GT911_PIN_INT     3
#define GT911_PIN_RST     9
#define GT911_PIN_SDA    I2C_SDA
#define GT911_PIN_SCL    I2C_SCL

// No keyboard
// #define HAS_PHYSICAL_KEYBOARD 0

// Compatibility: main.cpp references CST328 touch (T-Deck Pro).
// Map to GT911 equivalents so shared code compiles.
// The actual touch init for T5S3 will use GT911 in Phase 2.
#define CST328_PIN_INT  GT911_PIN_INT
#define CST328_PIN_RST  GT911_PIN_RST

// -----------------------------------------------------------------------------
// SD Card — shares SPI bus with LoRa
// -----------------------------------------------------------------------------
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SDCARD_CS  12
#define SPI_CS     SDCARD_CS

// -----------------------------------------------------------------------------
// GPS — Not present on H752-B (non-GPS variant)
// If a GPS model is used (H752-01/H752-02), define HAS_GPS=1
// and uncomment the GPS pins below.
// -----------------------------------------------------------------------------
// #define HAS_GPS 1
// #define GPS_BAUDRATE 38400
// #define GPS_RX_PIN  44
// #define GPS_TX_PIN  43

// Fallback for code that references GPS_BAUDRATE without HAS_GPS guard
// (e.g. MyMesh.cpp CLI rescue command)
#ifndef GPS_BAUDRATE
#define GPS_BAUDRATE 9600
#endif

// -----------------------------------------------------------------------------
// RTC — PCF85063 (proper hardware RTC, battery-backed!)
// This is a significant upgrade over T-Deck Pro which has no RTC.
// -----------------------------------------------------------------------------
#define HAS_PCF85063_RTC  1
#define PCF85063_I2C_ADDR 0x51
#define PCF85063_INT_PIN   2

// -----------------------------------------------------------------------------
// PCA9535 IO Expander
// Controls e-ink power sequencing and has a user button
// -----------------------------------------------------------------------------
#define HAS_PCA9535       1
#define PCA9535_I2C_ADDR  0x20
#define PCA9535_INT_PIN   38

// PCA9535 pin assignments (directly from LilyGo schematic):
// Port 0 (IO0x): IO00-IO07 — mostly unused/reserved
// Port 1 (IO1x):
#define PCA9535_EP_OE       0   // IO10 — EP output enable (source driver)
#define PCA9535_EP_MODE      1   // IO11 — EP mode (gate driver)
#define PCA9535_BUTTON       2   // IO12 — User button via IO expander
#define PCA9535_TPS_PWRUP    3   // IO13 — TPS65185 power up
#define PCA9535_VCOM_CTRL    4   // IO14 — VCOM control
#define PCA9535_TPS_WAKEUP   5   // IO15 — TPS65185 wakeup
#define PCA9535_TPS_PWRGOOD  6   // IO16 — TPS65185 power good (input)
#define PCA9535_TPS_INT      7   // IO17 — TPS65185 interrupt (input)

// -----------------------------------------------------------------------------
// Buttons & Controls
// -----------------------------------------------------------------------------
#define BUTTON_PIN   0       // Boot button (GPIO0)
#define PIN_USER_BTN 0

// -----------------------------------------------------------------------------
// Power Management
// -----------------------------------------------------------------------------
#define HAS_BQ27220  1
#define BQ27220_I2C_ADDR  0x55

// T5S3 E-Paper Pro battery (1500 mAh — larger than T-Deck Pro's 1400 mAh)
#ifndef BQ27220_DESIGN_CAPACITY_MAH
#define BQ27220_DESIGN_CAPACITY_MAH  1500
#endif

#define AUTO_SHUTDOWN_MILLIVOLTS  2800

// No explicit peripheral power pin on T5S3 (unlike T-Deck Pro's PIN_PERF_POWERON)
// Peripherals are always powered when the board is on.