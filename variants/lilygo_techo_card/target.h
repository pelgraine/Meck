#pragma once

// =============================================================================
// MeshCore target declarations for LilyGo T-Echo Card
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <RadioLib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/NRF52Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/SSD1306Display.h>
#endif

#include <helpers/sensors/EnvironmentSensorManager.h>

#include "TechoCardBoard.h"

// Hardware object declarations (instantiated in target.cpp)
extern RADIO_CLASS radio;
extern WRAPPER_CLASS radio_driver;
extern TechoCardBoard board;
extern AutoDiscoverRTCClock rtc_clock;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
#endif

extern EnvironmentSensorManager sensors;

// Target functions — called from main.cpp and MyMesh.cpp
bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
void radio_reset_agc();