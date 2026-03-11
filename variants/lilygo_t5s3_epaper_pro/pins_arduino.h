#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include <stdint.h>

#define USB_VID 0x303a
#define USB_PID 0x1001

// Default Wire will be mapped to RTC, Touch, PCA9535, BQ25896, BQ27220, TPS65185
static const uint8_t SDA = 39;
static const uint8_t SCL = 40;

// Default SPI will be mapped to LoRa + SD card
static const uint8_t SS   = 46;   // LoRa CS
static const uint8_t MOSI = 13;
static const uint8_t MISO = 21;
static const uint8_t SCK  = 14;

#endif /* Pins_Arduino_h */
