#pragma once

#include <Arduino.h>

// CPU Frequency Scaling for ESP32-S3
//
// Typical current draw (CPU only, rough):
//   240 MHz  ~70-80 mA
//   160 MHz  ~50-60 mA
//    80 MHz  ~30-40 mA
//
// SPI peripherals and UART use their own clock dividers from the APB clock,
// so LoRa, e-ink, and GPS serial all work fine at 80MHz.

#ifdef ESP32

#ifndef CPU_FREQ_IDLE
#define CPU_FREQ_IDLE   80     // MHz — normal mesh listening
#endif

#ifndef CPU_FREQ_BOOST
#define CPU_FREQ_BOOST  240    // MHz — heavy processing
#endif

#ifndef CPU_BOOST_TIMEOUT_MS
#define CPU_BOOST_TIMEOUT_MS  10000  // 10 seconds
#endif

class CPUPowerManager {
public:
  CPUPowerManager() : _boosted(false), _boost_started(0) {}

  void begin() {
    setCpuFrequencyMhz(CPU_FREQ_IDLE);
    _boosted = false;
    MESH_DEBUG_PRINTLN("CPU power: idle at %d MHz", CPU_FREQ_IDLE);
  }

  void loop() {
    if (_boosted && (millis() - _boost_started >= CPU_BOOST_TIMEOUT_MS)) {
      setIdle();
    }
  }

  void setBoost() {
    if (!_boosted) {
      setCpuFrequencyMhz(CPU_FREQ_BOOST);
      _boosted = true;
      MESH_DEBUG_PRINTLN("CPU power: boosted to %d MHz", CPU_FREQ_BOOST);
    }
    _boost_started = millis();
  }

  void setIdle() {
    if (_boosted) {
      setCpuFrequencyMhz(CPU_FREQ_IDLE);
      _boosted = false;
      MESH_DEBUG_PRINTLN("CPU power: idle at %d MHz", CPU_FREQ_IDLE);
    }
  }

  bool isBoosted() const { return _boosted; }
  uint32_t getFrequencyMHz() const { return getCpuFrequencyMhz(); }

private:
  bool _boosted;
  unsigned long _boost_started;
};

#endif // ESP32