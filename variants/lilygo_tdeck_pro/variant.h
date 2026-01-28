#pragma once

// =============================================================================
// LilyGo T-Deck Pro v1.1 - Pin Definitions
// Based on Meshtastic PR #9378 for T-Deck Pro V1.1 compatibility
// =============================================================================

// Peripheral Power Control
#define PIN_PERF_POWERON 10  // Powers on peripherals

// -----------------------------------------------------------------------------
// E-Ink Display (GDEQ031T10 - 240x320)
// E-ink SHARES the SPI bus with LoRa (SCK=36, MOSI=33)
// They use different chip selects: E-ink CS=34, LoRa CS=3
// -----------------------------------------------------------------------------
#define PIN_EINK_CS    34
#define PIN_EINK_DC    35
#define PIN_EINK_RES   16    // Reset pin - must be held HIGH
#define PIN_EINK_BUSY  37
#define PIN_EINK_SCLK  36    // Shared with LoRa
#define PIN_EINK_MOSI  33    // Shared with LoRa
#define PIN_EINK_BL    45    // Backlight PWM (optional, V1.1 feature)

// Legacy aliases for MeshCore compatibility
#define PIN_DISPLAY_CS    PIN_EINK_CS
#define PIN_DISPLAY_DC    PIN_EINK_DC
#define PIN_DISPLAY_RST   PIN_EINK_RES
#define PIN_DISPLAY_BUSY  PIN_EINK_BUSY
#define PIN_DISPLAY_SCLK  PIN_EINK_SCLK
#define PIN_DISPLAY_MOSI  PIN_EINK_MOSI  // GPIO 33, shared with LoRa

// Display dimensions - native resolution of GDEQ031T10
#define LCD_HOR_SIZE 240
#define LCD_VER_SIZE 320

// E-ink model for GxEPD2
#define EINK_DISPLAY_MODEL GxEPD2_310_GDEQ031T10

// -----------------------------------------------------------------------------
// SPI Bus - Shared by LoRa, SD Card, AND E-ink display
// -----------------------------------------------------------------------------
#define BOARD_SPI_SCLK 36
#define BOARD_SPI_MISO 47
#define BOARD_SPI_MOSI 33

// -----------------------------------------------------------------------------
// I2C Bus
// -----------------------------------------------------------------------------
#define I2C_SDA 13
#define I2C_SCL 14

// I2C Device Addresses
#define I2C_ADDR_TOUCH      0x1A  // CST328/CST3530
#define I2C_ADDR_GYROSCOPE  0x28  // BHI260AP
#define I2C_ADDR_KEYBOARD   0x34  // TCA8418
#define I2C_ADDR_BQ27220    0x55  // Fuel gauge
#define I2C_ADDR_DRV2605    0x5A  // Motor driver (V1.1 only)
#define I2C_ADDR_BQ25896    0x6B  // Charger

// -----------------------------------------------------------------------------
// Touch Controller (CST328/CST3530)
// V1.1 uses CST3530 with different reset pin
// -----------------------------------------------------------------------------
#define HAS_TOUCHSCREEN 1
#define CST328_PIN_INT 12
#define CST328_PIN_RST 38    // V1.1: GPIO 38 (was 45 on V1.0)

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------
#define HAS_GPS 1
#define GPS_BAUDRATE 38400
#define PIN_GPS_EN 39
#define GPS_EN_ACTIVE 1
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43
#define PIN_GPS_PPS 1

// -----------------------------------------------------------------------------
// Buttons & Controls
// -----------------------------------------------------------------------------
#define BUTTON_PIN 0
#define PIN_USER_BTN 0

// Vibration Motor - V1.1 uses DRV2605 driver
#define HAS_DRV2605 1
#define PIN_DRV_EN 2

// -----------------------------------------------------------------------------
// SD Card
// -----------------------------------------------------------------------------
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SPI_MOSI 33
#define SPI_SCK 36
#define SPI_MISO 47
#define SPI_CS 48
#define SDCARD_CS SPI_CS

// -----------------------------------------------------------------------------
// Keyboard (TCA8418)
// -----------------------------------------------------------------------------
#define KB_BL_PIN 42
#define BOARD_KEYBOARD_INT 15
#define HAS_PHYSICAL_KEYBOARD 1

// -----------------------------------------------------------------------------
// Audio - I2S Output (PCM5102A version)
// -----------------------------------------------------------------------------
#define BOARD_I2S_BCLK 7
#define BOARD_I2S_DOUT 8
#define BOARD_I2S_LRC  9

// Microphone (PDM)
#define BOARD_MIC_DATA  17
#define BOARD_MIC_CLOCK 18

// -----------------------------------------------------------------------------
// Sensors
// -----------------------------------------------------------------------------
// Note: V1.1 removed LTR553ALS light sensor to free up GPIO 16 for e-ink reset
#define HAS_BHI260AP        // Gyroscope/IMU
#define BOARD_GYRO_INT 21

// -----------------------------------------------------------------------------
// Power Management
// -----------------------------------------------------------------------------
#define HAS_BQ27220 1
#define BQ27220_I2C_ADDR 0x55
#define BQ27220_I2C_SDA I2C_SDA
#define BQ27220_I2C_SCL I2C_SCL
#define BQ27220_DESIGN_CAPACITY 1400

#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896

// -----------------------------------------------------------------------------
// LoRa Radio (SX1262)
// -----------------------------------------------------------------------------
#define USE_SX1262
#define USE_SX1268

#define LORA_EN    46   // Enable pin - must be HIGH for radio to work
#define LORA_SCK   36
#define LORA_MISO  47
#define LORA_MOSI  33   // Shared with e-ink and SD card
#define LORA_CS    3
#define LORA_RESET 4
#define LORA_DIO0  -1   // Not connected on SX1262
#define LORA_DIO1  5    // SX1262 IRQ
#define LORA_DIO2  6    // SX1262 BUSY

// SX126X driver aliases (Meshtastic compatibility)
#define SX126X_CS    LORA_CS
#define SX126X_DIO1  LORA_DIO1
#define SX126X_BUSY  LORA_DIO2
#define SX126X_RESET LORA_RESET

// RadioLib/MeshCore compatibility aliases
#define P_LORA_NSS    LORA_CS
#define P_LORA_DIO_1  LORA_DIO1
#define P_LORA_RESET  LORA_RESET
#define P_LORA_BUSY   LORA_DIO2
#define P_LORA_SCLK   LORA_SCK
#define P_LORA_MISO   LORA_MISO
#define P_LORA_MOSI   LORA_MOSI
#define P_LORA_EN     LORA_EN

// -----------------------------------------------------------------------------
// 4G Modem - A7682E (A7682E version boards only)
// Note: I2S pins (7,8,9) conflict - don't use audio and modem simultaneously
// -----------------------------------------------------------------------------
#define MODEM_POWER_EN 41   // BOARD_6609_EN
#define MODEM_PWRKEY   40
#define MODEM_RST      9
#define MODEM_RI       7
#define MODEM_DTR      8
#define MODEM_RX       10
#define MODEM_TX       11