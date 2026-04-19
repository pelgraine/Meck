#pragma once

// =============================================================================
// Arduino pin compatibility header for LilyGo T-Echo Card
//
// This file provides Arduino-standard pin name aliases for the nRF52840 GPIOs.
// Only needed if creating a custom board variant inside the Adafruit nRF52
// Arduino framework package. If using build flag overrides in platformio.ini,
// this file is optional.
// =============================================================================

// On nRF52840, Arduino digital pin numbers map 1:1 to nRF GPIO numbers
// (0–47 for port 0 and port 1).

// LED
#define LED_BUILTIN         PIN_LED1
#define PIN_LED1            39    // WS2812 RGB LED data (1, 7)
#define LED_STATE_ON        1

// Buttons
#define PIN_BUTTON1         42    // Button A — orange front button (1, 10)
#define PIN_BUTTON2         24    // Boot button (0, 24)

// Serial (USB CDC)
// nRF52840 native USB — no UART pin assignment needed for Serial
// Serial1 is used for GPS
#define PIN_SERIAL1_RX      21    // GPS TX → nRF RX (0, 21)
#define PIN_SERIAL1_TX      19    // nRF TX → GPS RX (0, 19)

// I2C
#define PIN_WIRE_SDA        36    // (1, 4)
#define PIN_WIRE_SCL        34    // (1, 2)

// SPI (LoRa — directly mapped, RadioLib handles pin control)
#define PIN_SPI_MISO        17    // (0, 17)
#define PIN_SPI_MOSI        15    // (0, 15)
#define PIN_SPI_SCK         13    // (0, 13)

// Analog
#define PIN_A0              2     // (0, 2) — Battery ADC / AIN0

// QSPI Flash (ZD25WQ32CEIGR 4MB)
// nRF52840 QSPI uses fixed pins — framework handles these
// #define PIN_QSPI_SCK     19  // Conflict with GPS TX — check if QSPI is on different pins
// NOTE: QSPI pin mapping needs verification on actual hardware.
// The T-Echo Card may use SPI (not QSPI) for external flash.

// NFC (dedicated nRF52840 NFC pins — not GPIO-assignable)
// NFC1 = P0.09, NFC2 = P0.10
// These are only usable as NFC when NFC is enabled in UICR.
// If NFC is disabled, they become GPIO9 and GPIO10.

// PDM Microphone
#define PIN_PDM_CLK         35    // (1, 3)
#define PIN_PDM_DIN         37    // (1, 5)

// I2S Speaker (MAX98357)
#define PIN_I2S_SCK         16    // BCLK (0, 16)
#define PIN_I2S_LRCK        22    // LRCK / WS (0, 22)
#define PIN_I2S_SDOUT       20    // DATA (0, 20)
