#pragma once
// SD.h — nRF52 compatibility stub for Meck
// Maps Arduino SD API to Adafruit InternalFS (LittleFS on QSPI flash).
// T-Echo Lite has no SD card slot; file operations use internal flash.

#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <time.h>    // struct tm, gmtime — implicit on ESP32, explicit on nRF52

// ESP32 SD uses string file modes; define them here for compile compatibility
#ifndef FILE_READ
#define FILE_READ  "r"
#endif
#ifndef FILE_WRITE
#define FILE_WRITE "w"
#endif

class SDClass {
public:
  // InternalFS is already initialised by main — begin() is a no-op
  bool begin(uint8_t cs = 0) { return true; }

  // Accept any extra args (cs, SPI, freq) without complaint
  template<typename... Args>
  bool begin(Args...) { return true; }

  bool exists(const char* path) { return InternalFS.exists(path); }
  bool remove(const char* path) { return InternalFS.remove(path); }
  bool mkdir(const char* path)  { return InternalFS.mkdir(path); }

  // String mode overload — matches ESP32 SD API (FILE_READ="r", FILE_WRITE="w", "r+")
  Adafruit_LittleFS_Namespace::File open(const char* path, const char* mode = "r") {
    uint8_t m = FILE_O_READ;
    if (mode) {
      if (mode[0] == 'w')                    m = FILE_O_WRITE;
      else if (mode[0] == 'r' && mode[1] == '+') m = FILE_O_WRITE;
    }
    return InternalFS.open(path, m);
  }
};

// Static instance per translation unit — no state (just forwards to InternalFS singleton)
static SDClass SD;