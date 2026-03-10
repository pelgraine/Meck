#pragma once

// =============================================================================
// CrowPanel 7.0" Advance Series (V1.0 / V1.3)
// ESP32-S3-WROOM-1-N16R8 (16MB Flash, 8MB OPI-PSRAM)
// 800x480 IPS display, ST7277 driver, RGB parallel 16-bit
// GT911 capacitive touch at I2C 0x5D
// =============================================================================

// -----------------------------------------------------------------------------
// I2C Bus (shared: touch GT911, RTC PCF85063 at 0x51, STC8H1K28 at 0x30)
// -----------------------------------------------------------------------------
#define I2C_SDA 15
#define I2C_SCL 16
#define PIN_BOARD_SDA I2C_SDA
#define PIN_BOARD_SCL I2C_SCL

// -----------------------------------------------------------------------------
// Display
// -----------------------------------------------------------------------------
#define LCD_HOR_SIZE 800
#define LCD_VER_SIZE 480

// -----------------------------------------------------------------------------
// LoRa Radio (SX1262) — via wireless module connector
// Pin mapping depends on hardware version (defined in platformio.ini):
//   V1.3: NSS=19, DIO1=20, RESET=8,  BUSY=2, SPI 4/5/6
//   V1.0: NSS=0,  DIO1=20, RESET=19, BUSY=2, SPI 4/5/6
//
// IMPORTANT: Function select DIP switch must be set to WM mode (S0=1, S1=0)
// for the SPI bus to be routed to the wireless module connector.
// -----------------------------------------------------------------------------
#define USE_SX1262
#define USE_SX1268

// P_LORA_* pins are defined in platformio.ini per hardware version
// RadioLib/MeshCore compat aliases (only define if not already set by platformio)
#ifndef P_LORA_NSS
  #ifdef CROWPANEL_V13
    #define P_LORA_NSS    20
    #define P_LORA_DIO_1  19
    #define P_LORA_RESET  8
    #define P_LORA_BUSY   2
  #else
    #define P_LORA_NSS    0
    #define P_LORA_DIO_1  20
    #define P_LORA_RESET  19
    #define P_LORA_BUSY   2
  #endif
#endif
#ifndef P_LORA_SCLK
  #define P_LORA_SCLK   5
  #define P_LORA_MISO   4
  #define P_LORA_MOSI   6
#endif

// -----------------------------------------------------------------------------
// GPS — not present on CrowPanel
// Define a fallback GPS_BAUDRATE so that serial CLI code compiles cleanly
// (the CLI GPS commands will be inert since HAS_GPS is not defined)
// -----------------------------------------------------------------------------
// #define HAS_GPS  — intentionally NOT defined
#ifndef GPS_BAUDRATE
  #define GPS_BAUDRATE 9600
#endif

// -----------------------------------------------------------------------------
// SD Card — shares SPI bus with LoRa and speaker via function switch
// When function switch is in WM mode, SD card is NOT accessible.
// SD support can be enabled for standalone (non-LoRa) use with S1=1,S0=1
// -----------------------------------------------------------------------------
// #define HAS_SDCARD  — not defined by default (conflicts with LoRa SPI)

// SDCARD_CS dummy: NotesScreen, TextReaderScreen, EpubZipReader reference this
// unconditionally. -1 makes digitalWrite() a no-op on ESP32.
#ifndef SDCARD_CS
  #define SDCARD_CS -1
#endif

// E-ink display: not present. GxEPDDisplay.h is shadowed by a stub in the
// variant include path to avoid LovyanGFX/Adafruit_GFX type collision.

// -----------------------------------------------------------------------------
// Battery — CrowPanel has a battery connector but no fuel gauge IC
// Battery charging is handled by onboard circuit with CHG LED indicator
// -----------------------------------------------------------------------------
// #define HAS_BQ27220  — not present
#define NO_BATTERY_INDICATOR 1

// -----------------------------------------------------------------------------
// Audio — I2S speaker (shared pins, only when function switch = MIC&SPK)
// Not usable simultaneously with LoRa — commented out for reference
// -----------------------------------------------------------------------------
// #define BOARD_I2S_SDIN  4   // IO4 (shared with SPI MISO)
// #define BOARD_I2S_BCLK  5   // IO5 (shared with SPI SCK)
// #define BOARD_I2S_LRCLK 6   // IO6 (shared with SPI MOSI)

// MIC (v1.3: LMD3526B261 PDM mic)
// #define BOARD_MIC_DATA  20  // IO20 (shared with LoRa DIO1)
// #define BOARD_MIC_CLOCK 19  // IO19 (shared with LoRa NSS)

// -----------------------------------------------------------------------------
// Touch Screen
// -----------------------------------------------------------------------------
#define HAS_TOUCHSCREEN 1

// Touch controller: GT911 at 0x5D
// INT = IO1 (active-low pulse, used for GT911 address selection at boot)
// RST = STC8H1K28 P1.7 (v1.3) or TCA9534 pin 2 (v1.0)

// -----------------------------------------------------------------------------
// Keyboard — not present (touchscreen-only device)
// -----------------------------------------------------------------------------
// #define HAS_PHYSICAL_KEYBOARD  — not present

// -----------------------------------------------------------------------------
// Buttons — CrowPanel is touchscreen-only
// -----------------------------------------------------------------------------
// BOOT button (GPIO0) is for programming only, not a user button.
// Do NOT define PIN_USER_BTN — it would enable TouchButton code in UITask.cpp
// but the TouchButton object is not instantiated (HAS_TOUCH_SCREEN suppresses it).
// #define BUTTON_PIN 0
// #define PIN_USER_BTN 0

// -----------------------------------------------------------------------------
// UART
// -----------------------------------------------------------------------------
// UART0: TX=IO43, RX=IO44 (also USB CDC)
// UART1: TX=IO20, RX=IO19 (shared with LoRa DIO1/NSS when in WM mode!)

// -----------------------------------------------------------------------------
// RTC — PCF85063 at I2C 0x51 (confirmed from board silkscreen)
// -----------------------------------------------------------------------------
// AutoDiscoverRTCClock will find it automatically on the I2C bus

// -----------------------------------------------------------------------------
// STC8H1K28 I/O Controller (V1.2/V1.3 only)
// I2C slave at address 0x30
// Command bytes:
//   0       = backlight max brightness
//   1-244   = backlight dimmer
//   245     = backlight off
//   246     = buzzer on
//   247     = buzzer off
//   248     = speaker amp on
//   249     = speaker amp off
// -----------------------------------------------------------------------------
#ifdef CROWPANEL_V13
  #define STC8H_I2C_ADDR 0x30
#endif