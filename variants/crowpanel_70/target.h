#pragma once

#include "variant.h"   // Board-specific defines (HAS_GPS, GPS_BAUDRATE, etc.)

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <CrowPanel70Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

#ifdef DISPLAY_CLASS
  #include <LGFX_CrowPanel70.h>
  #include <CrowPanel70Display.h>
  #ifndef HAS_TOUCH_SCREEN
    #include <TouchButton.h>
  #endif
#endif

extern CrowPanel70Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  extern LGFX_CrowPanel70 lcd;
  extern CrowPanel70Display display;
  #ifndef HAS_TOUCH_SCREEN
    extern TouchButton user_btn;
  #endif
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();