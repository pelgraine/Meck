#pragma once

// =============================================================================
// SD.h -- nRF52 shim for T-Echo Card
//
// Maps the Arduino SD library API to Adafruit InternalFileSystem so that
// screen headers (NotesScreen, MapScreen, TextReaderScreen, EpubZipReader)
// compile without modification. The T-Echo Card has no SD card slot --
// all file I/O goes to the 4MB QSPI flash via LittleFS.
//
// File class is already provided by the Adafruit LittleFS BSP.
// =============================================================================

#include <InternalFileSystem.h>

class SDClass {
public:
  bool begin(int cs = -1) {
    (void)cs;
    return true;  // InternalFS is initialised in main.cpp setup()
  }

  File open(const char* path, uint8_t mode = FILE_O_READ) {
    return InternalFS.open(path, mode);
  }

  bool exists(const char* path) {
    return InternalFS.exists(path);
  }

  bool mkdir(const char* path) {
    return InternalFS.mkdir(path);
  }

  bool remove(const char* path) {
    return InternalFS.remove(path);
  }

  bool rename(const char* from, const char* to) {
    return InternalFS.rename(from, to);
  }
};

static SDClass SD;