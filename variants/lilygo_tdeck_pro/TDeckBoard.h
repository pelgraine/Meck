#pragma once

#include "variant.h"  // Board-specific pin definitions
#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

// BQ27220 Fuel Gauge Registers
#define BQ27220_REG_VOLTAGE        0x08
#define BQ27220_REG_CURRENT        0x0C  // Instantaneous current (mA, signed)
#define BQ27220_REG_SOC            0x2C
#define BQ27220_REG_REMAIN_CAP     0x10  // Remaining capacity (mAh)
#define BQ27220_REG_FULL_CAP       0x12  // Full charge capacity (mAh)
#define BQ27220_REG_AVG_CURRENT    0x14  // Average current (mA, signed)
#define BQ27220_REG_TIME_TO_EMPTY  0x16  // Minutes until empty
#define BQ27220_REG_AVG_POWER      0x24  // Average power (mW, signed)
#define BQ27220_REG_DESIGN_CAP     0x3C  // Design capacity (mAh, read-only standard cmd)
#define BQ27220_REG_OP_STATUS      0x3A  // Operation status
#define BQ27220_I2C_ADDR 0x55

// T-Deck Pro battery capacity (all variants use 1400 mAh cell)
#ifndef BQ27220_DESIGN_CAPACITY_MAH
#define BQ27220_DESIGN_CAPACITY_MAH  1400
#endif

class TDeckBoard : public ESP32Board {
public:
  void begin();

  void powerOff() override {
    // Stop Bluetooth before power off
    btStop();
    // Don't call parent or enterDeepSleep - let normal shutdown continue
    // Display will show "hibernating..." text
  }

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
  uint16_t getBattMilliVolts() override;

  // Read state of charge percentage from BQ27220
  uint8_t getBatteryPercent();

  // Read average current in mA (negative = discharging, positive = charging)
  int16_t getAvgCurrent();

  // Read average power in mW (negative = discharging, positive = charging)
  int16_t getAvgPower();

  // Read time-to-empty in minutes (0xFFFF if charging/unavailable)
  uint16_t getTimeToEmpty();

  // Read remaining capacity in mAh
  uint16_t getRemainingCapacity();

  // Read full charge capacity in mAh (learned value, may need cycling to update)
  uint16_t getFullChargeCapacity();

  // Read design capacity in mAh (the configured battery size)
  uint16_t getDesignCapacity();

  // Configure BQ27220 design capacity (checks on boot, writes only if wrong)
  bool configureFuelGauge(uint16_t designCapacity_mAh = BQ27220_DESIGN_CAPACITY_MAH);

  const char* getManufacturerName() const {
    return "LilyGo T-Deck Pro";
  }
};