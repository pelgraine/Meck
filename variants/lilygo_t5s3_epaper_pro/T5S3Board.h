#pragma once

#include "variant.h"
#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

// BQ27220 Fuel Gauge Registers (shared with TDeckBoard)
#define BQ27220_REG_TEMPERATURE    0x06
#define BQ27220_REG_VOLTAGE        0x08
#define BQ27220_REG_CURRENT        0x0C
#define BQ27220_REG_SOC            0x2C
#define BQ27220_REG_REMAIN_CAP     0x10
#define BQ27220_REG_FULL_CAP       0x12
#define BQ27220_REG_AVG_CURRENT    0x14
#define BQ27220_REG_TIME_TO_EMPTY  0x16
#define BQ27220_REG_AVG_POWER      0x24
#define BQ27220_REG_DESIGN_CAP     0x3C
#define BQ27220_REG_OP_STATUS      0x3A

class T5S3Board : public ESP32Board {
public:
  void begin();

  void powerOff() override {
    btStop();
    // Turn off backlight before sleeping
    #ifdef BOARD_BL_EN
      digitalWrite(BOARD_BL_EN, LOW);
    #endif
  }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    // Hold LoRa DIO1 and NSS during deep sleep
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

    esp_deep_sleep_start();
  }

  // BQ27220 fuel gauge interface (identical register protocol to TDeckBoard)
  uint16_t getBattMilliVolts() override;
  uint8_t  getBatteryPercent();
  int16_t  getAvgCurrent();
  int16_t  getAvgPower();
  uint16_t getTimeToEmpty();
  uint16_t getRemainingCapacity();
  uint16_t getFullChargeCapacity();
  uint16_t getDesignCapacity();
  int16_t  getBattTemperature();
  bool     configureFuelGauge(uint16_t designCapacity_mAh = BQ27220_DESIGN_CAPACITY_MAH);

  // Backlight control (GPIO11 — functional warm-tone front-light, PWM capable)
  // Brightness 0-255 (0=off, 153=comfortable reading, 255=max)
  bool _backlightOn = false;
  uint8_t _backlightBrightness = 153;  // Same default as Meshtastic

  void setBacklight(bool on) {
    #ifdef BOARD_BL_EN
      _backlightOn = on;
      analogWrite(BOARD_BL_EN, on ? _backlightBrightness : 0);
    #endif
  }

  void setBacklightBrightness(uint8_t brightness) {
    #ifdef BOARD_BL_EN
      _backlightBrightness = brightness;
      if (_backlightOn) {
        analogWrite(BOARD_BL_EN, brightness);
      }
    #endif
  }

  bool isBacklightOn() const { return _backlightOn; }

  void toggleBacklight() {
    setBacklight(!_backlightOn);
  }

  const char* getManufacturerName() const {
    return "LilyGo T5S3 E-Paper Pro";
  }
};