// =============================================================================
// g_ADigitalPinMap — nRF52840 Arduino pin to GPIO mapping
//
// Required by the Adafruit nRF52 BSP (SPI.cpp, Wire, NeoPixel, etc.).
// On nRF52840, Arduino pin numbers map 1:1 to nRF GPIO numbers.
// 48 entries: P0.00–P0.31 (0–31) + P1.00–P1.15 (32–47)
// =============================================================================

#include <Arduino.h>

const uint32_t g_ADigitalPinMap[] = {
   0,  1,  2,  3,  4,  5,  6,  7,   // P0.00 – P0.07
   8,  9, 10, 11, 12, 13, 14, 15,   // P0.08 – P0.15
  16, 17, 18, 19, 20, 21, 22, 23,   // P0.16 – P0.23
  24, 25, 26, 27, 28, 29, 30, 31,   // P0.24 – P0.31
  32, 33, 34, 35, 36, 37, 38, 39,   // P1.00 – P1.07
  40, 41, 42, 43, 44, 45, 46, 47,   // P1.08 – P1.15
};