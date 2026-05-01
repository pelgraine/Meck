// =============================================================================
// MeshCore target implementation for LilyGo T-Echo Card
//
// nRF52840 + SX1262 (HPB16B3 / S62F module) + SSD1315 OLED + L76K GPS
// =============================================================================

#include <Arduino.h>
#include "target.h"
#include "variant.h"

// --- Board ---
TechoCardBoard board;

// --- Clock ---
// No hardware RTC on T-Echo Card — VolatileRTCClock tracks time via millis().
// Time gets set from GPS lock or BLE companion app sync.
// AutoDiscoverRTCClock probes I2C for hardware RTCs; if none found, uses fallback.
VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// --- Radio ---
// nRF52 Adafruit BSP SPIClass requires (peripheral, MISO, SCK, MOSI)
#if defined(P_LORA_SCLK)
  static SPIClass spi(NRF_SPIM3, P_LORA_MISO, P_LORA_SCLK, P_LORA_MOSI);
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

// --- Display ---
#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
#endif

// --- Sensor manager ---
#if ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  static MicroNMEALocationProvider gps_provider(Serial1);
  EnvironmentSensorManager sensors(gps_provider);
#else
  EnvironmentSensorManager sensors;
#endif

// --- Target initialization ---
bool radio_init() {
  // Board-level init — cycles RT9080 3V3 rail, parks peripheral pins LOW
  board.begin();

  // Enable GPS power (was parked LOW in board.begin())
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    digitalWrite(PIN_GPS_EN, HIGH);
    delay(10);
  #endif

  // GPS RF/LNA enable
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    digitalWrite(PIN_GPS_RF_EN, HIGH);
  #endif

  // Initialise GPS UART
  #if defined(HAS_GPS)
    Serial1.setPins(PIN_GPS_RX, PIN_GPS_TX);
    Serial1.begin(GPS_BAUDRATE);
  #endif

  // Speaker off by default
  #if defined(HAS_SPEAKER)
    pinMode(PIN_SPK_EN, OUTPUT);
    digitalWrite(PIN_SPK_EN, LOW);
    #if PIN_SPK_EN2 >= 0
      pinMode(PIN_SPK_EN2, OUTPUT);
      digitalWrite(PIN_SPK_EN2, LOW);
    #endif
  #endif

  // Initialise I2C
  Wire.begin();
  Wire.setClock(400000);

  // Initialise clocks — probe I2C for hardware RTCs, fall back to VolatileRTCClock
  rtc_clock.begin(Wire);

  // Initialise display
  #ifdef DISPLAY_CLASS
    display.begin();
  #endif

  // SX1262 DIO2 as RF switch control
  radio.setDio2AsRfSwitch(true);

  // SX1262 TCXO via DIO3 (1.8V, from Meshtastic PR #10267)
  // TODO: Verify on hardware — if module uses crystal, remove this call
  radio.setTCXO(1.8f);

  // Initialise radio
  #if defined(P_LORA_SCLK)
    return radio.std_init(&spi);
  #else
    return radio.std_init();
  #endif
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

void radio_reset_agc() {
  radio.setRxBoostedGainMode(true);
}