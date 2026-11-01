#pragma once

#include <MeshCore.h>
#include <Arduino.h>

#if defined(ESP_PLATFORM)

#include <rom/rtc.h>
#include <sys/time.h>
#include <Wire.h>
#include "esp_wifi.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"

class ESP32Board : public mesh::MainBoard {
protected:
  uint8_t startup_reason;

public:
  void begin() {
    // for future use, sub-classes SHOULD call this from their begin()
    startup_reason = BD_STARTUP_NORMAL;

  #ifdef ESP32_CPU_FREQ
    setCpuFrequencyMhz(ESP32_CPU_FREQ);
  #endif

  #ifdef PIN_VBAT_READ
    // battery read support
    pinMode(PIN_VBAT_READ, INPUT);
    adcAttachPin(PIN_VBAT_READ);
  #endif

  #ifdef P_LORA_TX_LED
    pinMode(P_LORA_TX_LED, OUTPUT);
    digitalWrite(P_LORA_TX_LED, LOW);
  #endif

  #if defined(PIN_BOARD_SDA) && defined(PIN_BOARD_SCL)
   #if PIN_BOARD_SDA >= 0 && PIN_BOARD_SCL >= 0
    Wire.begin(PIN_BOARD_SDA, PIN_BOARD_SCL);
   #endif
  #else
    Wire.begin();
  #endif
  }

  // Temperature from ESP32 MCU
  float getMCUTemperature() override {
    uint32_t raw = 0;

    // To get and average the temperature so it is more accurate, especially in low temperature
    for (int i = 0; i < 4; i++) {
      raw += temperatureRead();
    }

    return raw / 4;
  }

  void enterLightSleep(uint32_t secs) {
#if defined(CONFIG_IDF_TARGET_ESP32S3) && defined(P_LORA_DIO_1) // Supported ESP32 variants
    if (rtc_gpio_is_valid_gpio((gpio_num_t)P_LORA_DIO_1)) { // Only enter sleep mode if P_LORA_DIO_1 is RTC pin
      esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
      esp_sleep_enable_ext1_wakeup((1ULL << P_LORA_DIO_1), ESP_EXT1_WAKEUP_ANY_HIGH); // Wake on LoRa packet

      if (secs > 0) {
        esp_sleep_enable_timer_wakeup(secs * 1000000ULL); // Timer wake (microseconds)
      }

      esp_light_sleep_start(); // CPU halts here, resumes on wake
    }
#endif
  }

  void sleep(uint32_t secs) override {
    // To check for WiFi status to see if there is active OTA
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    
    if (err != ESP_OK) {          // WiFi is off ~ No active OTA, safe to go to sleep
      enterLightSleep(secs);      // To wake up after "secs" seconds or when receiving a LoRa packet
    }
  }

  uint8_t getStartupReason() const override { return startup_reason; }

#if defined(P_LORA_TX_LED)
  void onBeforeTransmit() override {
    digitalWrite(P_LORA_TX_LED, HIGH);   // turn TX LED on
  }
  void onAfterTransmit() override {
    digitalWrite(P_LORA_TX_LED, LOW);   // turn TX LED off
  }
#elif defined(P_LORA_TX_NEOPIXEL_LED)
  #define NEOPIXEL_BRIGHTNESS    64  // white brightness (max 255)

  void onBeforeTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS, NEOPIXEL_BRIGHTNESS);   // turn TX neopixel on (White)
  }
  void onAfterTransmit() override {
    neopixelWrite(P_LORA_TX_NEOPIXEL_LED, 0, 0, 0);   // turn TX neopixel off
  }
#endif

  uint16_t getBattMilliVolts() override {
  #ifdef PIN_VBAT_READ
    analogReadResolution(12);

    uint32_t raw = 0;
    for (int i = 0; i < 4; i++) {
      raw += analogReadMilliVolts(PIN_VBAT_READ);
    }
    raw = raw / 4;

    return (2 * raw);
  #else
    return 0;  // not supported
  #endif
  }

  const char* getManufacturerName() const override {
    return "Generic ESP32";
  }

  void reboot() override {
    esp_restart();
  }

  bool startOTAUpdate(const char* id, char reply[]) override;
};

// Last known time, kept in RTC memory: it survives a crash, watchdog or
// brownout reset, not a power loss. From MeshCore 35142a88, but held in one
// function-local static: a plain static in this header would give every
// source file its own copy, and begin() and tick() could use different ones.
// [0] = time, [1] = magic.
inline uint32_t* esp32RtcTimeBackup() {
  static RTC_NOINIT_ATTR uint32_t backup[2];
  return backup;
}
#define RTC_BACKUP_MAGIC  0xAA55CC33

#ifdef MECK_CLOCK_NVS_BACKUP
// The reset button (EN) clears RTC memory too, so a board that gets reset
// by hand also keeps the time in NVS, every MECK_CLOCK_NVS_PERIOD seconds
// (720 small writes a day, well within NVS wear levelling). After such a
// reset the clock resumes from the last save: up to that period behind,
// plus the time the device was off, instead of the May 2024 seed. The next
// phone or GPS sync corrects it.
#include <Preferences.h>
#ifndef MECK_CLOCK_NVS_PERIOD
#define MECK_CLOCK_NVS_PERIOD 120
#endif
inline uint32_t& esp32NvsTimeSaved() {   // last value written (or read at boot)
  static uint32_t saved = 0;
  return saved;
}
inline uint32_t esp32NvsTimeLoad() {
  Preferences p;
  uint32_t t = 0;
  if (p.begin("meckclock", true)) {
    t = p.getUInt("t", 0);
    p.end();
  }
  return t;
}
inline void esp32NvsTimeSave(uint32_t t) {
  Preferences p;
  if (p.begin("meckclock", false)) {
    p.putUInt("t", t);
    p.end();
  }
  esp32NvsTimeSaved() = t;
}
#endif

class ESP32RTCClock : public mesh::RTCClock {
public:
  ESP32RTCClock() { }
  void begin() {
    // Check if the ESP32 RTC peripheral retained a sane time across reset.
    // On power-on the value is undefined; on watchdog/brownout/SW reset it
    // may retain valid time OR garbage.  Accept times in a plausible window
    // (Nov 2023 → May 2033); otherwise restore the last time saved in RTC
    // memory (or in NVS, whichever is later), and only without one re-seed.
    time_t _now;
    time(&_now);
#ifdef MECK_CLOCK_NVS_BACKUP
    uint32_t nvs = esp32NvsTimeLoad();
    if (nvs < 1700000000 || nvs > 2000000000) nvs = 0;
    esp32NvsTimeSaved() = nvs;
#endif
    if (_now < 1700000000 || _now > 2000000000) {
      struct timeval tv;
      uint32_t* backup = esp32RtcTimeBackup();
      uint32_t best = 0;
      if (backup[1] == RTC_BACKUP_MAGIC && backup[0] >= 1700000000 && backup[0] <= 2000000000) {
        best = backup[0];
      }
#ifdef MECK_CLOCK_NVS_BACKUP
      if (nvs > best) best = nvs;
#endif
      tv.tv_sec = best ? best : 1715770351;  // 15 May 2024, 8:50pm UTC (safe seed)
      tv.tv_usec = 0;
      settimeofday(&tv, NULL);
    }
  }
  // Called from the main loop: keep the RTC-memory copy of the time current
  void tick() override {
    time_t now;
    time(&now);
    uint32_t* backup = esp32RtcTimeBackup();
    if (now >= 1700000000 && now <= 2000000000 && (uint32_t)now != backup[0]) {
      backup[0] = (uint32_t)now;
      backup[1] = RTC_BACKUP_MAGIC;
    }
#ifdef MECK_CLOCK_NVS_BACKUP
    // Periodically; a sync that moves the clock back, or forward by more
    // than the period, is saved at the next tick
    uint32_t saved = esp32NvsTimeSaved();
    if (now >= 1700000000 && now <= 2000000000 &&
        ((uint32_t)now < saved || (uint32_t)now - saved >= MECK_CLOCK_NVS_PERIOD)) {
      esp32NvsTimeSave((uint32_t)now);
    }
#endif
  }
  uint32_t getCurrentTime() override {
    time_t _now;
    time(&_now);
    return _now;
  }
  void setCurrentTime(uint32_t time) override { 
    struct timeval tv;
    tv.tv_sec = time;
    tv.tv_usec = 0;
    settimeofday(&tv, NULL);
  }
};

#endif