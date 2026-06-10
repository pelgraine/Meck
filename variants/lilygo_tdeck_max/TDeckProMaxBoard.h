#pragma once

// =============================================================================
// TDeckProMaxBoard — Board support for LilyGo T-Deck Pro MAX V0.1
//
// Standalone board class inheriting ESP32Board directly (decoupled from the
// Pro TDeckBoard). Provides its own BQ27220 fuel gauge methods plus:
//   - XL9555 I/O expander initialisation and control
//   - XL9555-routed peripheral power management
//   - Touch/keyboard reset via XL9555
//   - Modem power/PWRKEY via XL9555
//   - LoRa antenna selection via XL9555
//   - Audio output mux (ES8311 vs A7682E) via XL9555
//   - Speaker amplifier enable via XL9555
//
// The XL9555 must be initialised before LoRa, GPS, modem, or touch are used.
// All power enables, resets, and switches go through I2C — not direct GPIO.
// =============================================================================

#include "variant.h"
#include <Wire.h>
#include <Arduino.h>
#include "helpers/ESP32Board.h"   // Direct base -- MAX no longer inherits TDeckBoard (Pro)
#include <driver/rtc_io.h>

// BQ27220 Fuel Gauge Registers (moved here from TDeckBoard.h when MAX was
// decoupled from the Pro board class; the BQ27220 hardware is identical).
#define BQ27220_REG_TEMPERATURE    0x06  // Temperature (0.1 K)
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

#ifndef BQ27220_DESIGN_CAPACITY_MAH
#define BQ27220_DESIGN_CAPACITY_MAH  1400
#endif

class TDeckProMaxBoard : public ESP32Board {
public:
  void begin();

  const char* getManufacturerName() const {
    return "LilyGo T-Deck Pro MAX";
  }

  // -------------------------------------------------------------------------
  // XL9555 I/O Expander — lightweight inline driver
  //
  // The XL9555 has 16 I/O pins across two 8-bit ports.
  // Pin 0-7 = Port 0, Pin 8-15 = Port 1.
  // We shadow the output state in _xlPort0/_xlPort1 to allow
  // single-bit set/clear without read-modify-write over I2C.
  // -------------------------------------------------------------------------

  // Initialise XL9555: set all used pins as outputs, apply boot defaults.
  // Returns true if I2C communication with XL9555 succeeded.
  bool xl9555_init();

  // Set a single XL9555 pin HIGH or LOW (pin 0-15).
  void xl9555_digitalWrite(uint8_t pin, bool value);

  // Read the current output state of a pin (from shadow, not I2C read).
  bool xl9555_digitalRead(uint8_t pin) const;

  // Write raw port values (for batch updates).
  void xl9555_writePort0(uint8_t val);
  void xl9555_writePort1(uint8_t val);

  // -------------------------------------------------------------------------
  // High-level peripheral control (delegates to XL9555)
  // -------------------------------------------------------------------------

  // Modem (A7682E) power control
  void modemPowerOn();       // Enable SGM6609 boost (6609_EN HIGH)
  void modemPowerOff();      // Disable SGM6609 boost (6609_EN LOW)
  void modemPwrkeyPulse();   // Toggle PWRKEY: HIGH 100ms → LOW 1200ms → HIGH

  // Audio output selection
  void selectAudioES8311();  // AUDIO_SEL LOW → ES8311 output to speaker/headphones
  void selectAudioModem();   // AUDIO_SEL HIGH → A7682E output to speaker/headphones
  void amplifierEnable();    // NS4150B amplifier ON (louder speaker)
  void amplifierDisable();   // NS4150B amplifier OFF (saves power)

  // LoRa antenna selection (SKY13453 RF switch)
  void loraAntennaInternal();  // LORA_SEL HIGH → internal PCB antenna (default)
  void loraAntennaExternal();  // LORA_SEL LOW → external IPEX antenna

  // Motor (DRV2605) power
  void motorEnable();          // MOTOR_EN HIGH
  void motorDisable();         // MOTOR_EN LOW

  // Touch controller reset via XL9555
  void touchReset();           // Pulse TOUCH_RST: LOW 20ms → HIGH, then 50ms settle

  // Keyboard reset via XL9555
  void keyboardReset();        // Pulse KEY_RST: LOW 20ms → HIGH, then 50ms settle

  // GPS power control via XL9555
  void gpsPowerOn();           // GPS_EN HIGH
  void gpsPowerOff();          // GPS_EN LOW

  // LoRa power control via XL9555
  void loraPowerOn();          // LORA_EN HIGH
  void loraPowerOff();         // LORA_EN LOW

  // -------------------------------------------------------------------------
  // E-ink front-light control
  // On MAX, IO41 has a working backlight circuit (boost converter + LEDs).
  // PWM control for brightness is possible via ledc.
  // -------------------------------------------------------------------------
  void backlightOn();
  void backlightOff();
  void backlightSetBrightness(uint8_t duty);  // 0-255, via LEDC PWM
  bool isBacklightOn() const;

  // -------------------------------------------------------------------------
  // BQ27220 fuel gauge (moved from TDeckBoard when MAX was decoupled).
  // Identical hardware to the Pro; these read the same registers.
  // -------------------------------------------------------------------------
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

  // -------------------------------------------------------------------------
  // Sleep / power-off (moved verbatim from TDeckBoard when MAX was decoupled).
  // NOTE: powerOff() still references PIN_PERF_POWERON (-1 on MAX) and the
  // #ifdef P_LORA_EN block (P_LORA_EN undefined on MAX, so it compiles out).
  // Behaviour-preserving lift-and-shift; a MAX-correct XL9555 powerOff() is a
  // known follow-up.
  // -------------------------------------------------------------------------
  void powerOff() override {
    // True hibernate: deep sleep with no software wake sources.
    // Only a hardware reset (reset button) or USB power-on wakes the device.
    // BLE, WiFi, 4G, GPS, and LoRa are already shut down by UITask
    // before this method is called.

    btStop();  // Belt and suspenders -- BLE controller stop

    // Cut power to peripherals (keyboard, BQ27220, sensors)
    pinMode(PIN_PERF_POWERON, OUTPUT);
    digitalWrite(PIN_PERF_POWERON, LOW);

    // Cut power to LoRa module (radio already in standby from radio_driver.powerOff)
    #ifdef P_LORA_EN
      digitalWrite(P_LORA_EN, LOW);
    #endif

    // Hold LoRa NSS high to prevent SX1262 drawing current from floating CS
    rtc_gpio_hold_en((gpio_num_t)P_LORA_NSS);

    // Enter deep sleep with no wake sources -- only hardware reset wakes
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);
    esp_deep_sleep_start();
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

private:
  // Shadow registers for XL9555 output ports (avoid I2C read-modify-write)
  uint8_t _xlPort0 = XL9555_BOOT_PORT0;
  uint8_t _xlPort1 = XL9555_BOOT_PORT1;
  bool    _xlReady = false;
  bool    _backlightOn = false;  // tracks frontlight on/off for isBacklightOn()

  // Low-level I2C helpers
  bool xl9555_writeReg(uint8_t reg, uint8_t val);
  uint8_t xl9555_readReg(uint8_t reg);
};