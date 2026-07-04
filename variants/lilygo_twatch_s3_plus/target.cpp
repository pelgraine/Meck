#include <Arduino.h>
#include "variant.h"
#include "target.h"

TWatchS3PlusBoard board;

// LoRa SX1262 on its own SPI bus (the display uses SPI3_HOST separately).
static SPIClass loraSpi;
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, loraSpi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// GPS: u-blox MIA-M10Q on Serial2 (the GPS pins are reclaimed from Serial1 to
// Serial2 in main.cpp setup(), matching the Meck T-Deck Pro pattern). The
// BLDO1 rail that powers the module is off at boot and gated by gps_enabled.
#if HAS_GPS
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
  // NOTE: board.begin() is called by main.cpp setup() before radio_init();
  // Wire is already initialised there with the correct pins.
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  loraSpi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, P_LORA_NSS);
  return radio.std_init(&loraSpi);
}

uint32_t radio_get_rng_seed() {
  return radio.random(0x7FFFFFFF);
}

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq);
  radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw);
  radio.setCodingRate(cr);

  // Longer preamble for low SF improves reliability -- each symbol is shorter
  // at low SF, so more symbols are needed for reliable detection.
  uint16_t preamble = (sf <= 8) ? 32 : 16;
  radio.setPreambleLength(preamble);
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