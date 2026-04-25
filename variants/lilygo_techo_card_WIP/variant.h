#pragma once

// =============================================================================
// LilyGo T-Echo Card — Pin Definitions for Meck Firmware
//
// nRF52840 + SX1262 + SSD1315 (72×40 OLED) + L76K GPS + MAX98357 Speaker
// + MP34DT05 PDM Microphone + ICM20948 IMU + BQ25896 Charger + Solar
//
// Pin notation from LilyGo pinmap: (port, pin) → nRF GPIO = port*32 + pin
//
// Cross-referenced against:
//   - LilyGo official: T-Echo-Card/libraries/private_library/t_echo_card_config.h
//   - Meshtastic PR #10267 (caveman99 T-Echo-Card support)
// =============================================================================

#define LILYGO_TECHO_CARD

// -----------------------------------------------------------------------------
// I2C Bus (shared: OLED, IMU ICM20948, fuel gauge BQ27220 if present)
// -----------------------------------------------------------------------------
#define I2C_SDA             36    // (1, 4)
#define I2C_SCL             34    // (1, 2)
#define PIN_BOARD_SDA       I2C_SDA
#define PIN_BOARD_SCL       I2C_SCL

// -----------------------------------------------------------------------------
// SX1262 LoRa Radio (SPI bit-bang on nRF52)
// -----------------------------------------------------------------------------
#define P_LORA_NSS          11    // (0, 11) — CS
#define P_LORA_RESET        7     // (0, 7)  — RST
#define P_LORA_SCLK         13    // (0, 13) — SCK
#define P_LORA_MOSI         15    // (0, 15) — MOSI
#define P_LORA_MISO         17    // (0, 17) — MISO
#define P_LORA_BUSY         14    // (0, 14) — BUSY
#define P_LORA_DIO_1        40    // (1, 8)  — DIO1 / interrupt

// RF switch control (HPB16B3 / S62F module)
// DIO2 is used internally by the SX1262 for RF switch control.
// RF_VC1 / RF_VC2 are external PA/LNA select lines.
// Meshtastic PR #10267 maps these as TXEN/RXEN — verify on hardware
// whether DIO2-as-RF-switch alone is sufficient, or if VC1/VC2 are also
// needed for full TX/RX performance.
#define LORA_DIO2           5     // (0, 5)  — DIO2 (RF switch / TXCO)
#define LORA_RF_VC1         27    // (0, 27) — RF_VC1 (potential TXEN)
#define LORA_RF_VC2         33    // (1, 1)  — RF_VC2 (potential RXEN)

// SX1262 TCXO voltage via DIO3
// Meshtastic PR #10267 sets this to 1.8V. Without it, the TCXO may not
// start and the radio will have frequency drift or fail to init.
// Confirm on hardware — if the module has a TCXO fed by DIO3, this is needed.
#define SX126X_DIO3_TCXO_VOLTAGE  1.8f

// LoRa radio power: RT9080 controls the 3V3 rail for all peripherals
// including LoRa. No dedicated LoRa power enable pin.
#define PIN_LORA_EN         -1

// Default radio settings (Australia)
#ifndef LORA_TX_POWER
#define LORA_TX_POWER       22
#endif

// -----------------------------------------------------------------------------
// 0.42" OLED Display — SSD1315 (SSD1306-compatible), 72×40, I2C
//
// The SSD1315 has a 128×64 GDDRAM, but the physical panel is only 72×40.
// The visible window is mapped at columns 28–99, pages 3–7 (rows 24–63).
// This means:
//   - Horizontal: auto-centred by driver ((128 - 72) / 2 = 28)
//   - Vertical: need SETDISPLAYOFFSET = 24 (3 pages × 8 rows) so that
//     data written to pages 0–4 appears on the physical display.
//
// If the MeshCore OLEDDisplay driver's display() method sends PAGEADDR
// starting at page 0, the SETDISPLAYOFFSET command should handle the
// mapping. If content appears shifted or blank, the alternative is to
// modify the PAGEADDR commands to write to pages 3–7 directly.
//
// Ref: Meshtastic PR #10267 uses setYOffset(3) to shift every PAGEADDR
// write, plus GEOMETRY_72_40 which sets SETMULTIPLEX to 39.
// -----------------------------------------------------------------------------
#define HAS_OLED            1
#define OLED_I2C_ADDR       0x3C
#define OLED_WIDTH          72
#define OLED_HEIGHT         40
#define OLED_SDA            I2C_SDA
#define OLED_SCL            I2C_SCL

// SSD1315 display offset: 3 pages = 24 rows
// Applied via SETDISPLAYOFFSET (0xD3) after display.begin() in target.cpp
#define OLED_DISPLAY_OFFSET 24

// OLED / peripheral power control via RT9080 enable pin
// This controls the 3V3 rail for OLED, GPS, LoRa, and sensors.
#define PIN_OLED_EN         30    // (0, 30) — RT9080_EN

// No hardware reset pin for OLED on T-Echo Card
#define PIN_OLED_RESET      -1

// -----------------------------------------------------------------------------
// GPS — L76K Multi-GNSS (GPS, GLONASS, BeiDou, QZSS)
// -----------------------------------------------------------------------------
#define HAS_GPS             1
#define GPS_BAUDRATE        9600
#define PIN_GPS_TX          19    // (0, 19) — nRF TX → GPS RX
#define PIN_GPS_RX          21    // (0, 21) — nRF RX ← GPS TX
#define PIN_GPS_EN          47    // (1, 15) — GPS power enable
#define PIN_GPS_WAKEUP      25    // (0, 25) — GPS wakeup
#define PIN_GPS_1PPS        23    // (0, 23) — 1PPS time pulse
#define PIN_GPS_RF_EN       29    // (0, 29) — GPS RF / LNA enable

// -----------------------------------------------------------------------------
// Battery & Power
// -----------------------------------------------------------------------------
#define PIN_VBAT_READ       2     // (0, 2) = AIN0 — battery voltage ADC
#define BATTERY_ADC_AIN     0     // nRF SAADC AIN channel number
#define BATTERY_CAPACITY_MAH 800

// Battery voltage divider enable gate
// P0.31 controls a FET/switch that enables the resistive divider feeding
// AIN0. Must be driven HIGH before ADC read and LOW after to avoid
// parasitic drain through the divider.
// Source: LilyGo t_echo_card_config.h → BATTERY_MEASUREMENT_CONTROL
// Confirmed: Meshtastic PR #10267 → ADC_CTRL (0 + 31), ADC_CTRL_ENABLED HIGH
#define BATTERY_MEASUREMENT_CONTROL   31    // (0, 31)
#define BATTERY_MEASUREMENT_ACTIVE    HIGH

// Battery voltage divider calibration
// nRF52840 SAADC: 0.6V internal ref, 1/6 gain → 0–3.6V range
// With 2:1 resistive divider, multiply by 2 to get actual cell voltage.
// Adjust ADC_MULTIPLIER after measuring real voltage vs ADC reading.
#ifndef ADC_MULTIPLIER
#define ADC_MULTIPLIER      2.0f
#endif

// BQ25896 charger is on I2C but managed by hardware — no software control needed
// Solar panel input: 0.25W 5V via VBUS

// Auto-shutdown threshold (millivolts)
#define AUTO_SHUTDOWN_MILLIVOLTS 3200

// -----------------------------------------------------------------------------
// Speaker — MAX98357 I2S Class-D Mono Amp (8Ω, 1W)
// -----------------------------------------------------------------------------
#define HAS_SPEAKER         1
#define PIN_SPK_EN          43    // (1, 11) — speaker amplifier enable
#define PIN_SPK_EN2         3     // (0, 3)  — secondary enable
#define PIN_SPK_BCLK        16    // (0, 16) — I2S bit clock
#define PIN_SPK_DATA        20    // (0, 20) — I2S data out
#define PIN_SPK_LRCK        22    // (0, 22) — I2S word select / LRCK

// -----------------------------------------------------------------------------
// Microphone — MP34DT05 Digital MEMS PDM
// -----------------------------------------------------------------------------
#define HAS_MICROPHONE      1
#define PIN_MIC_CLK         35    // (1, 3) — PDM clock
#define PIN_MIC_DATA        37    // (1, 5) — PDM data

// -----------------------------------------------------------------------------
// Buttons
// -----------------------------------------------------------------------------
#define PIN_BUTTON_A        42    // (1, 10) — orange button (front, main user button)
#define PIN_BUTTON_BOOT     24    // (0, 24) — boot button (nRF52840 BOOT)
#define PIN_USER_BTN        PIN_BUTTON_A

// Button_B is RESET — hardware only, no GPIO

// Active LOW for nRF52 buttons (internal pull-up, press = LOW)
#define BUTTON_ACTIVE_LOW   1

// -----------------------------------------------------------------------------
// Buzzer
// -----------------------------------------------------------------------------
#define HAS_BUZZER          1
#define PIN_BUZZER          38    // (1, 6) — piezo buzzer data / PWM

// -----------------------------------------------------------------------------
// WS2812 RGB LEDs — 3 independent LEDs on separate data lines (NOT a chain)
//
// Confirmed by Meshtastic PR #10267: each WS2812 is on its own GPIO,
// driven as a 1-pixel NeoPixel strand. The bare-die WS2812s are very
// bright at full intensity — scale to ~25% (0x40 max per channel).
//
// Role assignments (matching Meshtastic PR #10267):
//   DATA_1 (P1.7)  → power/charge status (red)
//   DATA_2 (P1.12) → notification / mesh activity (green)
//   DATA_3 (P0.28) → BLE pairing status (blue)
// -----------------------------------------------------------------------------
#define HAS_RGB_LED         1
#define PIN_RGB_LED_1       39    // (1, 7)  — WS2812 data 1 (power/charge)
#define PIN_RGB_LED_2       44    // (1, 12) — WS2812 data 2 (notification)
#define PIN_RGB_LED_3       28    // (0, 28) — WS2812 data 3 (BLE pairing)

// Default NeoPixel colours at 25% brightness (0x40 max per channel)
#define NEOPIXEL_COLOR_POWER      0x400000  // red
#define NEOPIXEL_COLOR_NOTIFY     0x004000  // green
#define NEOPIXEL_COLOR_PAIRING    0x000040  // blue

// Legacy aliases (kept for any code referencing these)
#define PIN_NEOPIXEL        PIN_RGB_LED_1
#define NUM_NEOPIXELS       3

// -----------------------------------------------------------------------------
// IMU — ICM20948 9-axis MotionTracking (accelerometer + gyro + compass)
// -----------------------------------------------------------------------------
#define HAS_IMU             1
#define IMU_I2C_ADDR        0x68
#define IMU_SDA             I2C_SDA
#define IMU_SCL             I2C_SCL

// -----------------------------------------------------------------------------
// NFC — nRF52840 NFC-A (Type 2 Tag)
// NFC uses dedicated NFC1/NFC2 pins on nRF52840 (P0.09 / P0.10)
// These are fixed by silicon — no GPIO config needed.
// NFC is handled by the nRF52 SDK nfc_t2t_lib.
// -----------------------------------------------------------------------------
#define HAS_NFC             1

// -----------------------------------------------------------------------------
// External Flash — ZD25WQ32CEIGR (4MB QSPI Flash)
//
// Pin mapping confirmed from LilyGo t_echo_card_config.h and Meshtastic
// PR #10267. These are on a separate SPI bus from LoRa.
// The Adafruit nRF52 core supports QSPI via Adafruit_SPIFlash + LittleFS.
// -----------------------------------------------------------------------------
#define HAS_EXT_FLASH       1
#define PIN_QSPI_SCK        4     // (0, 4)
#define PIN_QSPI_CS         12    // (0, 12)
#define PIN_QSPI_IO0        6     // (0, 6)  — MOSI / D0
#define PIN_QSPI_IO1        8     // (0, 8)  — MISO / D1
#define PIN_QSPI_IO2        41    // (1, 9)  — WP / D2
#define PIN_QSPI_IO3        26    // (0, 26) — HOLD / D3

// -----------------------------------------------------------------------------
// No SD Card on T-Echo Card
// Settings stored in LittleFS on internal/external flash
// -----------------------------------------------------------------------------
// #define HAS_SDCARD

// -----------------------------------------------------------------------------
// No dedicated RTC chip — time from GPS or BLE companion sync
// nRF52840 has a 32.768 kHz RTC peripheral for timekeeping while running
// -----------------------------------------------------------------------------
// #define HAS_PCF85063_RTC

// -----------------------------------------------------------------------------
// Misc / Compatibility
// -----------------------------------------------------------------------------

// No e-ink display
// #define HAS_EINK

// This board has no physical keyboard
// #define HAS_PHYSICAL_KEYBOARD

// Fallback for code that references GPS_BAUDRATE without HAS_GPS guard
#ifndef GPS_BAUDRATE
#define GPS_BAUDRATE        9600
#endif
