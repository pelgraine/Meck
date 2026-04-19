#pragma once

// =============================================================================
// LilyGo T-Deck Pro MAX V0.1 - Pin Definitions
// Hardware revision: HD-V3-250911
//
// KEY DIFFERENCES FROM T-Deck Pro V1.1:
//   - XL9555 I/O expander (0x20) controls peripheral power, resets, and switches
//     (LoRa EN, GPS EN, modem power, touch RST, keyboard RST, antenna sel, etc.)
//   - 4G (A7682E) and audio (ES8311) coexist on ONE board — no longer mutually exclusive
//   - ES8311 I2C codec replaces PCM5102A (needs I2C config, different I2S pins)
//   - E-ink RST moved: IO9 (was IO16)
//   - E-ink BL moved: IO41 (was IO45, now has working front-light hardware!)
//   - GPS UART moved: RX=IO2, TX=IO16 (was RX=IO44, TX=IO43)
//   - GPS/LoRa power via XL9555 (was direct GPIO 39/46)
//   - Touch RST via XL9555 IO07 (was GPIO 38)
//   - Modem power/PWRKEY via XL9555 (was direct GPIO 41/40)
//   - No PIN_PERF_POWERON (IO10 is now modem UART RX)
//   - Battery: 1500 mAh (was 1400 mAh)
//   - LoRa antenna switch (SKY13453) controlled by XL9555 IO04
//   - Audio output mux (A7682E vs ES8311) controlled by XL9555 IO12
//   - Speaker amplifier (NS4150B) enable via XL9555 IO06
// =============================================================================

// -----------------------------------------------------------------------------
// E-Ink Display (GDEQ031T10 - 240x320)
// E-ink SHARES the SPI bus with LoRa and SD card (SCK=36, MOSI=33, MISO=47)
// They use different chip selects: E-ink CS=34, LoRa CS=3, SD CS=48
// -----------------------------------------------------------------------------
#define PIN_EINK_CS    34
#define PIN_EINK_DC    35
#define PIN_EINK_RES    9    // MAX: IO9 (was IO16 on V1.1)
#define PIN_EINK_BUSY  37
#define PIN_EINK_SCLK  36    // Shared with LoRa + SD
#define PIN_EINK_MOSI  33    // Shared with LoRa + SD
#define PIN_EINK_BL    41    // MAX: IO41 — working front-light! (was IO45 non-functional on V1.1)

// Legacy aliases for MeshCore compatibility
#define PIN_DISPLAY_CS    PIN_EINK_CS
#define PIN_DISPLAY_DC    PIN_EINK_DC
#define PIN_DISPLAY_RST   PIN_EINK_RES
#define PIN_DISPLAY_BUSY  PIN_EINK_BUSY
#define PIN_DISPLAY_SCLK  PIN_EINK_SCLK
#define PIN_DISPLAY_MOSI  PIN_EINK_MOSI

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

// Aliases for ESP32Board base class compatibility
#define PIN_BOARD_SDA I2C_SDA
#define PIN_BOARD_SCL I2C_SCL

// I2C Device Addresses
#define I2C_ADDR_ES8311     0x18  // ES8311 audio codec (NEW on MAX)
#define I2C_ADDR_TOUCH      0x1A  // CST328
#define I2C_ADDR_XL9555     0x20  // XL9555 I/O expander (NEW on MAX)
#define I2C_ADDR_GYROSCOPE  0x28  // BHI260AP
#define I2C_ADDR_KEYBOARD   0x34  // TCA8418
#define I2C_ADDR_BQ27220    0x55  // Fuel gauge
#define I2C_ADDR_DRV2605    0x5A  // Motor driver (haptic)
#define I2C_ADDR_BQ25896    0x6B  // Charger

// -----------------------------------------------------------------------------
// XL9555 I/O Expander — Pin Assignments
//
// The XL9555 replaces direct GPIO control of peripheral power enables,
// resets, and switches. It must be initialised over I2C before LoRa, GPS,
// modem, or touch can be used.
//
// Port 0: pins 0-7, registers 0x02 (output) / 0x06 (direction)
// Port 1: pins 8-15, registers 0x03 (output) / 0x07 (direction)
// Direction: 0 = output, 1 = input
// -----------------------------------------------------------------------------
#define HAS_XL9555 1

// XL9555 I2C registers
#define XL9555_REG_INPUT_0   0x00
#define XL9555_REG_INPUT_1   0x01
#define XL9555_REG_OUTPUT_0  0x02
#define XL9555_REG_OUTPUT_1  0x03
#define XL9555_REG_INVERT_0  0x04
#define XL9555_REG_INVERT_1  0x05
#define XL9555_REG_CONFIG_0  0x06   // 0=output, 1=input
#define XL9555_REG_CONFIG_1  0x07

// XL9555 pin assignments (0-7 = Port 0, 8-15 = Port 1)
#define XL_PIN_6609_EN       0    // HIGH: Enable A7682E power supply (SGM6609 boost)
#define XL_PIN_LORA_EN       1    // HIGH: Enable SX1262 power supply
#define XL_PIN_GPS_EN        2    // HIGH: Enable GPS power supply
#define XL_PIN_1V8_EN        3    // HIGH: Enable BHI260AP 1.8V power supply
#define XL_PIN_LORA_SEL      4    // HIGH: internal antenna, LOW: external antenna (SKY13453)
#define XL_PIN_MOTOR_EN      5    // HIGH: Enable DRV2605 power supply
#define XL_PIN_AMPLIFIER     6    // HIGH: Enable NS4150B speaker power amplifier
#define XL_PIN_TOUCH_RST     7    // LOW: Reset touch controller (active-low)
#define XL_PIN_PWRKEY_EN     8    // HIGH: A7682E POWERKEY toggle
#define XL_PIN_KEY_RST       9    // LOW: Reset keyboard (active-low)
#define XL_PIN_AUDIO_SEL    10    // HIGH: A7682E audio out, LOW: ES8311 audio out
// Pins 11-15 are reserved

// Default XL9555 output state at boot (all power enables ON, resets de-asserted)
// Bit layout: [P07..P00] = TOUCH_RST=1, AMP=0, MOTOR_EN=0, LORA_SEL=1, 1V8=1, GPS=1, LORA=1, 6609=0
//             [P17..P10] = reserved=0, AUDIO_SEL=0, KEY_RST=1, PWRKEY=0
//
// Conservative boot defaults for Meck:
//   - LoRa ON, GPS ON, 1.8V ON, internal antenna
//   - Modem OFF (6609_EN LOW), PWRKEY LOW (toggled later if needed)
//   - Motor OFF, Amplifier OFF (saves power, enabled on demand)
//   - Touch RST HIGH (not resetting), Keyboard RST HIGH (not resetting)
//   - Audio select LOW (ES8311 by default — Meck controls this when needed)
#define XL9555_BOOT_PORT0  0b10011110  // 0x9E: T_RST=1, AMP=0, MOT=0, LSEL=1, 1V8=1, GPS=1, LORA=1, 6609=0
#define XL9555_BOOT_PORT1  0b00000010  // 0x02: ..., ASEL=0, KRST=1, PKEY=0

// -----------------------------------------------------------------------------
// Touch Controller (CST328)
// NOTE: Touch RST is via XL9555 pin 7, NOT a direct GPIO!
// CST328_PIN_RST is defined as -1 to signal "not a direct GPIO".
// The board class handles touch reset via XL9555 in begin().
// -----------------------------------------------------------------------------
#define HAS_TOUCHSCREEN 1
#define CST328_PIN_INT 12
#define CST328_PIN_RST -1     // MAX: Routed through XL9555 IO07 — handled by board class

// -----------------------------------------------------------------------------
// GPS
// NOTE: GPS power enable is via XL9555 pin 2, NOT a direct GPIO!
// PIN_GPS_EN is intentionally NOT defined — the board class handles it via XL9555.
// -----------------------------------------------------------------------------
#define HAS_GPS 1
#define GPS_BAUDRATE 38400
// #define PIN_GPS_EN — NOT a direct GPIO on MAX (XL9555 IO02)
#define GPS_RX_PIN  2         // MAX: IO2 (was IO44 on V1.1) — ESP32 receives from GPS
#define GPS_TX_PIN 16         // MAX: IO16 (was IO43 on V1.1) — ESP32 sends to GPS
#define PIN_GPS_PPS  1

// -----------------------------------------------------------------------------
// Buttons & Controls
// -----------------------------------------------------------------------------
#define BUTTON_PIN 0
#define PIN_USER_BTN 0

// Vibration Motor — DRV2605 driver (same as V1.1)
// Motor power enable is via XL9555 pin 5, not a direct GPIO.
#define HAS_DRV2605 1

// -----------------------------------------------------------------------------
// SD Card
// -----------------------------------------------------------------------------
#define HAS_SDCARD
#define SDCARD_USE_SPI1
#define SPI_MOSI 33
#define SPI_SCK  36
#define SPI_MISO 47
#define SPI_CS   48
#define SDCARD_CS SPI_CS

// -----------------------------------------------------------------------------
// Keyboard (TCA8418)
// NOTE: Keyboard RST is via XL9555 pin 9 (active-low).
// The board class handles keyboard reset via XL9555 in begin().
// -----------------------------------------------------------------------------
#define KB_BL_PIN 42
#define BOARD_KEYBOARD_INT 15
#define HAS_PHYSICAL_KEYBOARD 1

// -----------------------------------------------------------------------------
// Audio — ES8311 I2C Codec (NEW on MAX — replaces PCM5102A)
//
// ES8311 is an I2C-controlled audio codec (unlike PCM5102A which needed no config).
// It requires I2C register setup for input source, gain, volume, etc.
// Speaker/headphone output is shared with A7682E modem audio, selected via
// XL9555 pin AUDIO_SEL: LOW = ES8311, HIGH = A7682E.
// Power amplifier (NS4150B) for speaker enabled via XL9555 pin AMPLIFIER.
//
// I2S pin mapping for ES8311 (completely different from V1.1 PCM5102A!):
//   MCLK  = IO38  (master clock — ES8311 needs this, PCM5102A didn't)
//   SCLK  = IO39  (bit clock, aka BCLK)
//   LRCK  = IO18  (word select, aka LRC/WS)
//   DSDIN = IO17  (DAC serial data in — ESP32 sends audio TO codec)
//   ASDOUT= IO40  (ADC serial data out — codec sends mic audio TO ESP32)
// -----------------------------------------------------------------------------
#define HAS_ES8311_AUDIO 1

#define BOARD_ES8311_MCLK   38
#define BOARD_ES8311_SCLK   39
#define BOARD_ES8311_LRCK   18
#define BOARD_ES8311_DSDIN  17    // ESP32 → ES8311 (speaker/headphone output)
#define BOARD_ES8311_ASDOUT 40    // ES8311 → ESP32 (microphone input)

// Compatibility aliases for ESP32-audioI2S library (setPinout expects BCLK, LRC, DOUT)
#define BOARD_I2S_BCLK  BOARD_ES8311_SCLK    // IO39
#define BOARD_I2S_LRC   BOARD_ES8311_LRCK    // IO18
#define BOARD_I2S_DOUT  BOARD_ES8311_DSDIN   // IO17
#define BOARD_I2S_MCLK  BOARD_ES8311_MCLK    // IO38 (ESP32-audioI2S may need setMCLK)

// Microphone — ES8311 built-in ADC (replaces separate PDM mic on V1.1)
// Mic data comes through I2S ASDOUT pin, not a separate PDM interface.
#define BOARD_MIC_I2S_DIN BOARD_ES8311_ASDOUT // IO40

// -----------------------------------------------------------------------------
// Sensors
// -----------------------------------------------------------------------------
#define HAS_BHI260AP        // Gyroscope/IMU (1.8V power via XL9555 IO03)
#define BOARD_GYRO_INT 21

// -----------------------------------------------------------------------------
// Power Management
// -----------------------------------------------------------------------------
#define HAS_BQ27220 1
#define BQ27220_I2C_ADDR 0x55
#define BQ27220_I2C_SDA I2C_SDA
#define BQ27220_I2C_SCL I2C_SCL
#define BQ27220_DESIGN_CAPACITY 1500    // MAX: 1500 mAh (was 1400 on V1.1)
#define BQ27220_DESIGN_CAPACITY_MAH 1500 // Alias used by TDeckBoard.h

#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896

// -----------------------------------------------------------------------------
// LoRa Radio (SX1262)
// NOTE: LoRa power enable is via XL9555 pin 1, NOT GPIO 46!
// The board class enables LoRa power via XL9555 in begin().
// P_LORA_EN is intentionally NOT defined here — handled by board class.
// Antenna selection: XL9555 pin 4 (HIGH=internal, LOW=external via SKY13453).
// -----------------------------------------------------------------------------
#define USE_SX1262
#define USE_SX1268

// LORA_EN is NOT a direct GPIO on MAX — omit the define entirely.
// If any code references P_LORA_EN, it must be guarded with #ifndef HAS_XL9555.
// #define LORA_EN  — NOT DEFINED (was GPIO 46 on V1.1)

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
// P_LORA_EN is NOT defined — LoRa power is via XL9555, handled in board begin()

// -----------------------------------------------------------------------------
// 4G Modem — A7682E (ALWAYS PRESENT on MAX — no longer optional!)
//
// On V1.1, 4G and audio were mutually exclusive hardware configurations.
// On MAX, both coexist. The XL9555 controls:
//   - 6609_EN (XL pin 0): modem power supply (SGM6609 boost converter)
//   - PWRKEY (XL pin 8): modem power key toggle
// Audio output from modem vs ES8311 is selected by AUDIO_SEL (XL pin 10).
//
// MODEM_POWER_EN and MODEM_PWRKEY are NOT direct GPIOs — ModemManager
// needs MAX-aware paths (see integration guide).
// MODEM_RST does not exist on MAX (IO9 is now LCD_RST).
// -----------------------------------------------------------------------------
// Direct GPIO modem pins (still accessible as regular GPIO):
#define MODEM_RI   7     // Ring indicator (interrupt input)
#define MODEM_DTR  8     // Data terminal ready (output)
#define MODEM_RX  10     // UART RX (ESP32 receives from modem)
#define MODEM_TX  11     // UART TX (ESP32 sends to modem)

// XL9555-routed modem pins — these are NOT direct GPIO!
// MODEM_POWER_EN and MODEM_PWRKEY are intentionally NOT defined.
// Existing code guarded by #ifdef MODEM_POWER_EN / #ifdef HAS_4G_MODEM will
// be skipped. Use board.modemPowerOn()/modemPwrkeyPulse() instead.
// MODEM_RST does not exist on MAX (IO9 is LCD_RST).

// Compatibility: PIN_PERF_POWERON does not exist on MAX (IO10 is modem UART RX).
// Defined as -1 so TDeckBoard.cpp compiles (parent class), but never used at runtime.
#define PIN_PERF_POWERON -1
