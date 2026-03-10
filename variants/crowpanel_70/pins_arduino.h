#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

// USB Serial (native USB CDC on ESP32-S3)
static const uint8_t TX = 43;
static const uint8_t RX = 44;

// I2C (shared between touch GT911, RTC PCF85063 at 0x51, STC8H1K28 at 0x30)
static const uint8_t SDA = 15;
static const uint8_t SCL = 16;

// Default SPI — mapped to wireless module slot (active when function switch = WM)
// V1.3 LoRa pin mapping (confirmed from board silkscreen):
//   NSS=19, DIO1=20, RESET=8, BUSY=2, SPI on 4/5/6
// V1.0 LoRa pin mapping (original):
//   NSS=0,  DIO1=20, RESET=19, BUSY=2, SPI on 4/5/6
#ifdef CROWPANEL_V13
  static const uint8_t SS = 20;     // LoRa NSS (V1.3: GPIO20, confirmed by SPI probe)
#else
  static const uint8_t SS = 0;      // LoRa NSS (V1.0: on boot strapping pin)
#endif
static const uint8_t MOSI = 6;    // LoRa MOSI / SD MOSI / I2S LRCLK (shared)
static const uint8_t MISO = 4;    // LoRa MISO / SD MISO / I2S SDIN (shared)
static const uint8_t SCK = 5;     // LoRa SCK  / SD CLK  / I2S BCLK (shared)

// Analog pins
static const uint8_t A0 = 1;
static const uint8_t A1 = 2;
static const uint8_t A2 = 3;
static const uint8_t A3 = 4;
static const uint8_t A4 = 5;
static const uint8_t A5 = 6;

// Touch pins
static const uint8_t T1 = 1;
static const uint8_t T2 = 2;
static const uint8_t T3 = 3;
static const uint8_t T4 = 4;
static const uint8_t T5 = 5;
static const uint8_t T6 = 6;

#endif /* Pins_Arduino_h */