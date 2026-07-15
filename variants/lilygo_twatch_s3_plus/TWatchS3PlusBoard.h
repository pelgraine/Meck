#pragma once

#include "variant.h"  // Board-specific pin definitions (I2C, user btn, addresses)

#include <Wire.h>
#include <Arduino.h>
#include "XPowersLib.h"
#include "helpers/ESP32Board.h"
#include <driver/rtc_io.h>

// LilyGo T-Watch S3 Plus board.
//
// Power is managed by an AXP2101 PMU on the main I2C bus. The PMU power rails
// (per the T-Watch S3 Plus PowerManage table) are brought up in power_init().
class SensorBMA423;  // full include kept in the .cpp to avoid a BLE-build header clash

class TWatchS3PlusBoard : public ESP32Board {
  XPowersLibInterface* PMU = NULL;
  SensorBMA423* _accel = nullptr;
  static volatile bool _tilt_flag;
  static void IRAM_ATTR onTiltISR();   // defined in the .cpp (IRAM relocation)

  bool power_init();

public:
  void begin();

  // Returns true once when the BMA423 tilt (wrist-raise) interrupt has fired.
  bool tiltFired();

  void enterDeepSleep(uint32_t secs, int pin_wake_btn) {
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

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

  uint16_t getBattMilliVolts() override {
    return PMU ? PMU->getBattVoltage() : 0;
  }

  uint8_t getBatteryPercent() override {
    return PMU ? PMU->getBatteryPercent() : 0;
  }

  // Wrapper-free BMA423 step count (raw I2C; defined in the .cpp).
  uint32_t getStepCount();

  // GPS power is the AXP2101 BLDO1 rail. Off at boot; toggled at runtime via
  // the gps_enabled pref (boot) and the "gps on/off" CLI command (live).
  void gpsPowerOn();
  void gpsPowerOff();

  // TEMP power-debug probe: battery, VBUS, CPU clock, BT controller state and
  // live rail states (incl. GPS/BLDO1). Called periodically from UITask::loop.
  void printPowerDebug();
#if defined(MECK_DIAG_PMU_BTN)
  // TEMP DIAGNOSTIC (power bisect): reproduce the S3 PMUButton's 30 ms PMU
  // register polling on this build. Called from UITask::loop; remove together
  // with the meck_twatch_s3_plus_diag_pmubtn env.
  void diagPollPmuKey();
#endif

  const char* getManufacturerName() const override {
    return "LilyGo T-Watch S3 Plus";
  }
};