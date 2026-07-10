#pragma once

#include "variant.h"  // Board-specific pin definitions (I2C, addresses, IRQs)

#include <Wire.h>
#include <Arduino.h>
#include "XPowersLib.h"
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

// LilyGo T-Watch S3 board (non-GPS, 470 mAh).
//
// Power is managed by an AXP2101 PMU on the main I2C bus. Two pointers to the
// same object are kept, because XPowersLib splits its API by access specifier:
//   PMU  (XPowersLibInterface*) -- the power-channel ops (setPowerChannelVoltage,
//        enable/disablePowerOutput, isPowerChannelEnable) are PROTECTED on
//        XPowersAXP2101 and only reachable through the interface.
//   _axp (XPowersAXP2101*)      -- setIrqLevelTime() and the PWRON press/release
//        edges isPekeyNegativeIrq()/isPekeyPositiveIrq() are AXP2101-only and
//        absent from the interface.
// Everything else is public on both.
class SensorBMA423;  // full include kept in the .cpp to avoid a BLE-build header clash

class TWatchS3Board : public ESP32Board {
  XPowersLibInterface* PMU = NULL;
  XPowersAXP2101* _axp = NULL;   // same object as PMU, concrete type
  SensorBMA423* _accel = nullptr;
  static volatile bool _tilt_flag;
  static void IRAM_ATTR onTiltISR();   // defined in the .cpp (IRAM relocation)

  bool power_init();

public:
  void begin();

  // Returns true once when the BMA423 tilt (wrist-raise) interrupt has fired.
  bool tiltFired();

  // The AXP2101 handle, for PMUButton. NULL if the PMU failed to init.
  XPowersAXP2101* getPMU() { return _axp; }

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

    rtc_gpio_set_direction((gpio_num_t)P_LORA_DIO_1, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pulldown_en((gpio_num_t)P_LORA_DIO_1);
    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    // NOTE: the PWR key is not a GPIO on this board, so it cannot be added to
    // the ext1 mask. pin_wake_btn is accepted for signature compatibility with
    // ESP32Board but callers on this variant pass -1.
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

  uint16_t getBattMilliVolts() override {
    return PMU ? PMU->getBattVoltage() : 0;
  }

  uint8_t getBatteryPercent() override {
    return PMU ? PMU->getBatteryPercent() : 0;
  }

  // Wrapper-free BMA423 step count (raw I2C; defined in the .cpp).
  uint32_t getStepCount();

  // Power-debug probe: battery, VBUS, CPU clock, BT controller state and live
  // rail states. Called periodically from UITask::loop.
  void printPowerDebug();

  const char* getManufacturerName() const override {
    return "LilyGo T-Watch S3";
  }
};
