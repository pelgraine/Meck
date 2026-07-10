#pragma once

// Include variant.h first to ensure all board-specific defines are available
#include "variant.h"

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <TWatchS3Board.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/SensorManager.h>

#ifdef DISPLAY_CLASS
  #include <TWatchS3Display.h>
  #include <PMUButton.h>
#endif

extern TWatchS3Board board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern SensorManager sensors;

#ifdef DISPLAY_CLASS
  extern DISPLAY_CLASS display;
  // Not a MomentaryButton: the only key on this board is the AXP2101 PWRON.
  extern PMUButton user_btn;
#endif

bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(uint8_t dbm);
mesh::LocalIdentity radio_new_identity();
void radio_reset_agc();
