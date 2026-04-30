#pragma once

// =============================================================================
// TechoCardBoard — Board class for LilyGo T-Echo Card
//
// Extends NRF52BoardDCDC with:
//   - Battery ADC (AIN0, P0.02) with gated voltage divider (P0.31)
//   - Solar charging via BQ25896
//   - GPS power control (L76K)
//   - Speaker/mic enable
//   - WS2812 RGB LEDs (3 independent, via Adafruit_NeoPixel)
//   - Buzzer
//   - NFC NDEF contact sharing
// =============================================================================

#include <Arduino.h>
#include <helpers/NRF52Board.h>
#include "variant.h"

// WS2812 NeoPixel support — 3 independent LEDs on separate GPIOs
#if defined(HAS_RGB_LED)
  #include <Adafruit_NeoPixel.h>
#endif

#ifdef NRF52_POWER_MANAGEMENT
// Power management config for T-Echo Card
// AIN0 (P0.02) for battery voltage sensing
// REFSEL=4 → 5/8 VDD ≈ 2.0625V threshold (with 2:1 divider → ~4.125V cell)
static const PowerMgtConfig TECHO_CARD_POWER_CONFIG = {
  .lpcomp_ain_channel = BATTERY_ADC_AIN,   // AIN0
  .lpcomp_refsel = 4,                       // 5/8 VDD
  .voltage_bootlock = 3100,                 // Don't boot below 3.1V
};
#endif

class TechoCardBoard : public NRF52BoardDCDC {
private:
  float _adc_multiplier;
  uint32_t _last_battery_read;
  float _cached_battery_mv;

  // Three independent WS2812 NeoPixels (1 LED per strand)
  #if defined(HAS_RGB_LED)
    Adafruit_NeoPixel _pixel_power   = Adafruit_NeoPixel(1, PIN_RGB_LED_1, NEO_GRB + NEO_KHZ800);
    Adafruit_NeoPixel _pixel_notify  = Adafruit_NeoPixel(1, PIN_RGB_LED_2, NEO_GRB + NEO_KHZ800);
    Adafruit_NeoPixel _pixel_pairing = Adafruit_NeoPixel(1, PIN_RGB_LED_3, NEO_GRB + NEO_KHZ800);
  #endif

public:
  TechoCardBoard()
    : _adc_multiplier(ADC_MULTIPLIER),
      _last_battery_read(0),
      _cached_battery_mv(0) {}

  void begin() override;

  // Battery
  float getBatteryVoltage() override;
  uint8_t getBatteryPercent() override;
  float getAdcMultiplier() override { return _adc_multiplier; }
  void setAdcMultiplier(float mult) { _adc_multiplier = mult; }

  // GPS power control
  void enableGPS(bool enable);

  // Speaker power control
  void enableSpeaker(bool enable);

  // RGB LEDs — all three to same colour (legacy interface)
  void setLED(uint8_t r, uint8_t g, uint8_t b);
  void ledOff();

  // Per-LED status control (0=power, 1=notify, 2=pairing)
  // colour is packed 0xRRGGBB — use NEOPIXEL_COLOR_* defines from variant.h
  void setStatusLED(uint8_t led_index, uint32_t color);

  // Buzzer
  void buzz(uint16_t freq_hz, uint16_t duration_ms);

#ifdef NRF52_POWER_MANAGEMENT
  const PowerMgtConfig* getPowerConfig() const override {
    return &TECHO_CARD_POWER_CONFIG;
  }
#endif
};
