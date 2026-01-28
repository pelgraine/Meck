#pragma once

#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

// BQ27220 Fuel Gauge Registers
#define BQ27220_REG_VOLTAGE 0x08
#define BQ27220_REG_CURRENT 0x0C
#define BQ27220_REG_SOC 0x2C
#define BQ27220_I2C_ADDR 0x55

class TDeckBoard : public ESP32Board {
public:
  void begin();

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Make sure the DIO1 and NSS GPIOs are held at required levels during deep sleep
    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);

    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    if (pin_wake_btn < 0) {
      esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH);
    } else {
      esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1) | (1ULL << pin_wake_btn), ESP_EXT1_WAKEUP_ANY_HIGH);
    }

    if (secs > 0) {
      esp_sleep_enable_timer_wakeup(secs * 1000000ULL);
    }

    // Finally set ESP32 into sleep
    esp_deep_sleep_start();  // CPU halts here and never returns!
  }

  // Read battery voltage from BQ27220 fuel gauge via I2C
  // Returns 0 silently if fuel gauge not present/responding
  uint16_t getBattMilliVolts() {
    #if HAS_BQ27220
      Wire.beginTransmission(BQ27220_I2C_ADDR);
      Wire.write(BQ27220_REG_VOLTAGE);
      if (Wire.endTransmission(false) != 0) {
        return 0;  // I2C error - fuel gauge not responding
      }
      
      uint8_t count = Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2);
      if (count != 2) {
        return 0;  // Read error
      }
      
      uint16_t voltage = Wire.read();
      voltage |= (Wire.read() << 8);
      return voltage;  // Already in mV
    #else
      return 0;
    #endif
  }

  // Read state of charge percentage from BQ27220
  // Returns 0 silently if fuel gauge not present/responding
  uint8_t getBatteryPercent() {
    #if HAS_BQ27220
      Wire.beginTransmission(BQ27220_I2C_ADDR);
      Wire.write(BQ27220_REG_SOC);
      if (Wire.endTransmission(false) != 0) {
        return 0;
      }
      
      uint8_t count = Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2);
      if (count != 2) {
        return 0;
      }
      
      uint16_t soc = Wire.read();
      soc |= (Wire.read() << 8);
      return (uint8_t)min(soc, (uint16_t)100);
    #else
      return 0;
    #endif
  }

  const char* getManufacturerName() const {
    return "LilyGo T-Deck Pro";
  }
};