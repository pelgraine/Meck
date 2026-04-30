// =============================================================================
// TechoCardBoard — Implementation for LilyGo T-Echo Card
//
// Patches applied from Meshtastic PR #10267 (caveman99):
//   1. RT9080 power rail reset cycle in begin() — prevents LoRa TX brown-out
//   2. Battery measurement control pin (P0.31) — enables voltage divider
//   3. WS2812 NeoPixel implementation via Adafruit_NeoPixel
// =============================================================================

#include "TechoCardBoard.h"
#include "variant.h"

// nRF52840 SAADC includes
#include "nrf.h"

void TechoCardBoard::begin() {
  NRF52BoardDCDC::begin();

  // -------------------------------------------------------------------------
  // RT9080 3V3 rail: clean reset cycle
  //
  // From Meshtastic PR #10267 (earlyInitVariant): if the nRF52840 was in a
  // half-enabled state from a previous soft reset, the RT9080 LDO can be in
  // an indeterminate state. Toggling EN HIGH→LOW→HIGH with 100ms dwell
  // forces a clean power-on. Without this, the 3V3 rail can brown-out when
  // LoRa TX fires at full power (+22 dBm).
  // -------------------------------------------------------------------------
  #if PIN_OLED_EN >= 0
    pinMode(PIN_OLED_EN, OUTPUT);
    digitalWrite(PIN_OLED_EN, HIGH);
    delay(100);
    digitalWrite(PIN_OLED_EN, LOW);
    delay(100);
    digitalWrite(PIN_OLED_EN, HIGH);
    delay(100);
  #endif

  // -------------------------------------------------------------------------
  // Park peripheral enable pins LOW before the rest of setup runs.
  // Prevents peripherals from sinking current while the 3V3 rail is ramping.
  // (Adapted from Meshtastic PR #10267 earlyInitVariant)
  // -------------------------------------------------------------------------
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, LOW);
  #endif
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    pinMode(PIN_GPS_RF_EN, OUTPUT);
    digitalWrite(PIN_GPS_RF_EN, LOW);
  #endif
  #if defined(HAS_BUZZER) && PIN_BUZZER >= 0
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  #endif

  // Configure battery measurement control pin
  #if defined(BATTERY_MEASUREMENT_CONTROL)
    pinMode(BATTERY_MEASUREMENT_CONTROL, OUTPUT);
    digitalWrite(BATTERY_MEASUREMENT_CONTROL, !BATTERY_MEASUREMENT_ACTIVE);
  #endif

  // Configure battery ADC pin as analog input
  pinMode(PIN_VBAT_READ, INPUT);

  // Configure button(s)
  pinMode(PIN_BUTTON_A, INPUT_PULLUP);
  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);

  // Initialise WS2812 NeoPixels (3 independent LEDs, all off at boot)
  #if defined(HAS_RGB_LED)
    _pixel_power.begin();
    _pixel_power.clear();
    _pixel_power.show();

    _pixel_notify.begin();
    _pixel_notify.clear();
    _pixel_notify.show();

    _pixel_pairing.begin();
    _pixel_pairing.clear();
    _pixel_pairing.show();
  #endif
}

// -----------------------------------------------------------------------------
// Battery voltage reading via nRF52840 SAADC
//
// The T-Echo Card has a gated voltage divider on AIN0 (P0.02).
// BATTERY_MEASUREMENT_CONTROL (P0.31) must be driven HIGH to enable the
// divider before reading, and LOW after to avoid parasitic drain.
//
// nRF52840 SAADC: 12-bit, internal 0.6V reference, 1/6 gain.
// With 1/6 gain: input range 0–3.6V. Multiply by divider ratio (ADC_MULTIPLIER).
// -----------------------------------------------------------------------------
float TechoCardBoard::getBatteryVoltage() {
  uint32_t now = millis();

  // Cache battery reading — only read every 10 seconds
  if (_cached_battery_mv > 0 && (now - _last_battery_read) < 10000) {
    return _cached_battery_mv;
  }

  // Enable battery voltage divider
  #if defined(BATTERY_MEASUREMENT_CONTROL)
    digitalWrite(BATTERY_MEASUREMENT_CONTROL, BATTERY_MEASUREMENT_ACTIVE);
    delay(5);  // Allow divider to settle
  #endif

  // Configure SAADC for single-shot reading
  NRF_SAADC->RESOLUTION = SAADC_RESOLUTION_VAL_12bit;

  // Channel 0: AIN0 (P0.02), 1/6 gain, internal 0.6V reference
  // Effective range: 0 – 3.6V
  NRF_SAADC->CH[0].PSELP = SAADC_CH_PSELP_PSELP_AnalogInput0;  // AIN0
  NRF_SAADC->CH[0].PSELN = SAADC_CH_PSELN_PSELN_NC;             // Single-ended
  NRF_SAADC->CH[0].CONFIG =
    (SAADC_CH_CONFIG_GAIN_Gain1_6 << SAADC_CH_CONFIG_GAIN_Pos) |
    (SAADC_CH_CONFIG_REFSEL_Internal << SAADC_CH_CONFIG_REFSEL_Pos) |
    (SAADC_CH_CONFIG_TACQ_40us << SAADC_CH_CONFIG_TACQ_Pos) |
    (SAADC_CH_CONFIG_MODE_SE << SAADC_CH_CONFIG_MODE_Pos) |
    (SAADC_CH_CONFIG_BURST_Disabled << SAADC_CH_CONFIG_BURST_Pos) |
    (SAADC_CH_CONFIG_RESP_Bypass << SAADC_CH_CONFIG_RESP_Pos) |
    (SAADC_CH_CONFIG_RESN_Bypass << SAADC_CH_CONFIG_RESN_Pos);

  // Set up result buffer
  volatile int16_t result = 0;
  NRF_SAADC->RESULT.PTR = (uint32_t)&result;
  NRF_SAADC->RESULT.MAXCNT = 1;

  // Enable, calibrate on first use
  NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Enabled;

  // Start and wait for sample
  NRF_SAADC->EVENTS_END = 0;
  NRF_SAADC->TASKS_START = 1;
  while (!NRF_SAADC->EVENTS_STARTED);
  NRF_SAADC->EVENTS_STARTED = 0;

  NRF_SAADC->TASKS_SAMPLE = 1;
  while (!NRF_SAADC->EVENTS_END);
  NRF_SAADC->EVENTS_END = 0;

  NRF_SAADC->TASKS_STOP = 1;
  while (!NRF_SAADC->EVENTS_STOPPED);
  NRF_SAADC->EVENTS_STOPPED = 0;

  // Disable SAADC to save power
  NRF_SAADC->ENABLE = SAADC_ENABLE_ENABLE_Disabled;

  // Disable battery voltage divider to save power
  #if defined(BATTERY_MEASUREMENT_CONTROL)
    digitalWrite(BATTERY_MEASUREMENT_CONTROL, !BATTERY_MEASUREMENT_ACTIVE);
  #endif

  // Convert: voltage = (result / 4096) * 3.6V * ADC_MULTIPLIER * 1000 (mV)
  if (result < 0) result = 0;
  float voltage_mv = ((float)result / 4096.0f) * 3600.0f * _adc_multiplier;

  _cached_battery_mv = voltage_mv;
  _last_battery_read = now;

  return voltage_mv;
}

uint8_t TechoCardBoard::getBatteryPercent() {
  float mv = getBatteryVoltage();
  if (mv <= 0) return 0;

  // Simple linear approximation for single-cell LiPo
  // 3200 mV = 0%, 4200 mV = 100%
  if (mv >= 4200.0f) return 100;
  if (mv <= 3200.0f) return 0;
  return (uint8_t)(((mv - 3200.0f) / 1000.0f) * 100.0f);
}

// -----------------------------------------------------------------------------
// GPS power control
// -----------------------------------------------------------------------------
void TechoCardBoard::enableGPS(bool enable) {
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    digitalWrite(PIN_GPS_EN, enable ? HIGH : LOW);
  #endif
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    digitalWrite(PIN_GPS_RF_EN, enable ? HIGH : LOW);
  #endif
}

// -----------------------------------------------------------------------------
// Speaker power control
// -----------------------------------------------------------------------------
void TechoCardBoard::enableSpeaker(bool enable) {
  #if defined(HAS_SPEAKER)
    digitalWrite(PIN_SPK_EN, enable ? HIGH : LOW);
    #if PIN_SPK_EN2 >= 0
      digitalWrite(PIN_SPK_EN2, enable ? HIGH : LOW);
    #endif
  #endif
}

// -----------------------------------------------------------------------------
// RGB LEDs — WS2812 via Adafruit_NeoPixel
//
// Three independent WS2812s on separate data pins (not a chain).
// Each is a 1-pixel NeoPixel strand.
//
// setLED() drives all three to the same colour (legacy interface).
// For per-LED control, use setStatusLED() with a role index.
// -----------------------------------------------------------------------------
void TechoCardBoard::setLED(uint8_t r, uint8_t g, uint8_t b) {
  #if defined(HAS_RGB_LED)
    uint32_t color = Adafruit_NeoPixel::Color(r, g, b);
    _pixel_power.setPixelColor(0, color);
    _pixel_power.show();
    _pixel_notify.setPixelColor(0, color);
    _pixel_notify.show();
    _pixel_pairing.setPixelColor(0, color);
    _pixel_pairing.show();
  #else
    (void)r; (void)g; (void)b;
  #endif
}

void TechoCardBoard::ledOff() {
  setLED(0, 0, 0);
}

void TechoCardBoard::setStatusLED(uint8_t led_index, uint32_t color) {
  #if defined(HAS_RGB_LED)
    switch (led_index) {
      case 0:  // Power / charge
        _pixel_power.setPixelColor(0, color);
        _pixel_power.show();
        break;
      case 1:  // Notification / mesh activity
        _pixel_notify.setPixelColor(0, color);
        _pixel_notify.show();
        break;
      case 2:  // BLE pairing
        _pixel_pairing.setPixelColor(0, color);
        _pixel_pairing.show();
        break;
    }
  #else
    (void)led_index; (void)color;
  #endif
}

// -----------------------------------------------------------------------------
// Buzzer — PWM tone generation
// -----------------------------------------------------------------------------
void TechoCardBoard::buzz(uint16_t freq_hz, uint16_t duration_ms) {
  #if defined(HAS_BUZZER) && PIN_BUZZER >= 0
    if (freq_hz == 0 || duration_ms == 0) {
      noTone(PIN_BUZZER);
      return;
    }
    tone(PIN_BUZZER, freq_hz, duration_ms);
  #else
    (void)freq_hz; (void)duration_ms;
  #endif
}
