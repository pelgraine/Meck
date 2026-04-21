#pragma once
// FS.h — nRF52 compatibility stub
// ESP32 Arduino core provides this as the base filesystem abstraction.
// On nRF52, File and filesystem types come from Adafruit_LittleFS.
// This stub exists solely to satisfy #include <FS.h> in shared headers.

#include <Arduino.h>
#include <time.h>    // struct tm, gmtime — implicit on ESP32, needs explicit on nRF52

// ESP32 FS.h defines these mode strings; some shared code references them
#ifndef FILE_READ
#define FILE_READ  "r"
#endif
#ifndef FILE_WRITE
#define FILE_WRITE "w"
#endif
#ifndef FILE_APPEND
#define FILE_APPEND "a"
#endif