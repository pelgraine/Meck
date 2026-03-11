#pragma once

// Include variant.h first to ensure all board-specific defines are available
#include "variant.h"

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <T5S3Board.h>
#include <helpers/AutoDiscoverRTCClock.h>

// Display support — FastEPDDisplay for parallel e-ink (not GxEPD2)
#ifdef DISPLAY_CLASS
  #include <helpers/ui/FastEPDDisplay.h>
  #include <helpers/ui/MomentaryButton.h>
#endif

// No GPS on H752-B (non-GPS variant)
// If porting to H752-01/H752-02 with GPS, enable this:
#if HAS_GPS
  #include "helpers/sensors/EnvironmentSensorManager.h"
  #include "helpers/sensors/MicroNMEALocationProvider.h"
  #include "GPSStreamCounter.h"
#else
  #include <helpers/SensorManager.h>
#endif

extern T5S3Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;

#if HAS_GPS
  extern GPSStreamCounter gpsStream;
  extern EnvironmentSensorManager sensors;
#else
  extern SensorManager sensors;
#endif

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  extern MomentaryButton user_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
void radio_reset_agc();
