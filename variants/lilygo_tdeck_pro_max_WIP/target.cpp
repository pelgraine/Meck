#include <Arduino.h>
#include "variant.h"
#include "target.h"

TDeckProMaxBoard board;

#if defined(P_LORA_SCLK)
  static SPIClass loraSpi(HSPI);
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, loraSpi);
#else
  RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
#endif

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

#if HAS_GPS
  // Wrap Serial2 with a sentence counter so the UI can show NMEA throughput.
  // MicroNMEALocationProvider reads through this wrapper transparently.
  GPSStreamCounter gpsStream(Serial2);
  MicroNMEALocationProvider gps(gpsStream, &rtc_clock);
  EnvironmentSensorManager sensors(gps);
#else
  SensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  DISPLAY_CLASS display;
  MomentaryButton user_btn(PIN_USER_BTN, 1000, true);
#endif

bool radio_init() {
  MESH_DEBUG_PRINTLN("radio_init() - starting");
  
  // NOTE: board.begin() is called by main.cpp setup() before radio_init()
  // I2C is already initialized there with correct pins
  
  fallback_clock.begin();
  MESH_DEBUG_PRINTLN("radio_init() - fallback_clock started");
  
  // Wire already initialized in board.begin() - just use it for RTC
  rtc_clock.begin(Wire);
  MESH_DEBUG_PRINTLN("radio_init() - rtc_clock started");

#if defined(P_LORA_SCLK)
  MESH_DEBUG_PRINTLN("radio_init() - initializing LoRa SPI...");
  loraSpi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  MESH_DEBUG_PRINTLN("radio_init() - SPI initialized, calling radio.std_init()...");
  bool result = radio.std_init(&loraSpi);
  MESH_DEBUG_PRINTLN("radio_init() - radio.std_init() returned: %s", result ? "SUCCESS" : "FAILED");
  return result;
#else
  MESH_DEBUG_PRINTLN("radio_init() - calling radio.std_init() without custom SPI...");
  bool result = radio.std_init();
  return result;
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

  // Longer preamble for low SF improves reliability — each symbol is shorter
  // at low SF, so more symbols are needed for reliable detection.
  // SF <= 8 gets 32 symbols (~65ms at SF7/62.5kHz); SF >= 9 keeps 16 (already ~131ms+).
  // See: https://github.com/meshcore-dev/MeshCore/pull/1954
  uint16_t preamble = (sf <= 8) ? 32 : 16;
  radio.setPreambleLength(preamble);
  MESH_DEBUG_PRINTLN("radio_set_params() - bw=%.1f sf=%u preamble=%u", bw, sf, preamble);
}

void radio_set_tx_power(uint8_t dbm) {
  radio.setOutputPower(dbm);
}

mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio);
  return mesh::LocalIdentity(&rng);
}

void radio_reset_agc() {
  radio.setRxBoostedGainMode(true);
}
