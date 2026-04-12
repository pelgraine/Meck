// =============================================================================
// TechoCardBoard — Implementation for LilyGo T-Echo Card
// =============================================================================

#include "TechoCardBoard.h"
#include "variant.h"

// nRF52840 SAADC includes
#include "nrf.h"

void TechoCardBoard::begin() {
  NRF52BoardDCDC::begin();

  // Configure battery ADC pin as analog input
  pinMode(PIN_VBAT_READ, INPUT);

  // Configure button(s)
  pinMode(PIN_BUTTON_A, INPUT_PULLUP);
  pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);

  // Buzzer off
  #if defined(HAS_BUZZER) && PIN_BUZZER >= 0
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
  #endif

  // RGB LED off at boot
  #if defined(HAS_RGB_LED)
    ledOff();
  #endif
}

// -----------------------------------------------------------------------------
// Battery voltage reading via nRF52840 SAADC
//
// The T-Echo Card has a voltage divider on AIN0 (P0.02).
// nRF52840 SAADC: 12-bit, internal 0.6V reference, configurable gain.
// With 1/6 gain: input range 0–3.6V. Multiply by divider ratio (ADC_MULTIPLIER).
//
// NOTE: The T-Echo Lite has a known issue reading 100% / 6.00V constantly.
// This is likely caused by incorrect SAADC configuration (wrong gain, wrong
// reference, or the pin being pulled high by the charging circuit).
// We use explicit SAADC register programming to avoid that issue.
// -----------------------------------------------------------------------------
float TechoCardBoard::getBatteryVoltage() {
  uint32_t now = millis();

  // Cache battery reading — only read every 10 seconds
  if (_cached_battery_mv > 0 && (now - _last_battery_read) < 10000) {
    return _cached_battery_mv;
  }

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
// RGB LED — WS2812 via NeoPixel protocol
// Simple bit-bang implementation for nRF52840 at 64MHz
// -----------------------------------------------------------------------------
void TechoCardBoard::setLED(uint8_t r, uint8_t g, uint8_t b) {
  #if defined(HAS_RGB_LED)
    // TODO: Implement WS2812 bit-bang or use Adafruit_NeoPixel library
    // For initial bringup, just use the Adafruit_NeoPixel library
    // which is available in the Adafruit nRF52 Arduino core
    (void)r; (void)g; (void)b;
  #endif
}

void TechoCardBoard::ledOff() {
  setLED(0, 0, 0);
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
