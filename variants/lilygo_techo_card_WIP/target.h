#pragma once

// =============================================================================
// MeshCore target declarations for LilyGo T-Echo Card
// =============================================================================

#include <Arduino.h>
#include <RadioLib.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/RadioLibWrappers.h>
#include <helpers/NRF52Board.h>
#include <helpers/CustomSX1262Wrapper.h>
#include <helpers/IdentityStore.h>
#include <helpers/NodePrefs.h>
#include <helpers/DataStore.h>
#include <helpers/SensorManager.h>

#ifdef DISPLAY_CLASS
#include <helpers/ui/DisplaySSD1306.h>
#endif

#include "TechoCardBoard.h"

// Hardware object declarations (instantiated in target.cpp)
extern RADIO_CLASS radio_driver;
extern WRAPPER_CLASS radio_wrapper;
extern TechoCardBoard board;

#ifdef DISPLAY_CLASS
extern DISPLAY_CLASS display;
#endif

extern mesh::IdentityStore identity_store;
extern mesh::NodePrefs node_prefs;
extern mesh::DataStore data_store;
extern EnvironmentSensorManager sensor_manager;

// Target initialization
void target_setup();
