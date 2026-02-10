#include <Arduino.h>
#include "variant.h"
#include "target.h"

TDeckBoard board;

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
  MicroNMEALocationProvider gps(Serial2, &rtc_clock);
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
  if (result) {
    radio.setPreambleLength(32);
    MESH_DEBUG_PRINTLN("radio_init() - preamble set to 32 symbols");
  }
  MESH_DEBUG_PRINTLN("radio_init() - radio.std_init() returned: %s", result ? "SUCCESS" : "FAILED");
  return result;
#else
  MESH_DEBUG_PRINTLN("radio_init() - calling radio.std_init() without custom SPI...");
  bool result = radio.std_init();
  if (result) {
    radio.setPreambleLength(32);
    MESH_DEBUG_PRINTLN("radio_init() - preamble set to 32 symbols");
  }
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