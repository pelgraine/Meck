#pragma once

#include <Arduino.h>

// CPU Frequency Scaling for ESP32-S3
//
// Typical current draw (CPU only, rough):
//   240 MHz  ~70-80 mA
//   160 MHz  ~50-60 mA
//    80 MHz  ~30-40 mA
//    40 MHz  ~15-20 mA  (low-power / lock screen mode)
//
// SPI peripherals and UART use their own clock dividers from the APB clock,
// so LoRa, e-ink, and GPS serial all work fine at 80MHz and 40MHz.

#ifdef ESP32

#ifndef CPU_FREQ_IDLE
#define CPU_FREQ_IDLE   80     // MHz — normal mesh listening
#endif

#ifndef CPU_FREQ_BOOST
#define CPU_FREQ_BOOST  240    // MHz — heavy processing
#endif

#ifndef CPU_FREQ_LOW_POWER
#define CPU_FREQ_LOW_POWER 80   // MHz — lock screen / idle standby (40 MHz breaks I2C)
#endif

#ifndef CPU_BOOST_TIMEOUT_MS
#define CPU_BOOST_TIMEOUT_MS  10000  // 10 seconds
#endif

class CPUPowerManager {
public:
  CPUPowerManager() : _boosted(false), _lowPower(false), _boost_started(0) {}

  void begin() {
    setCpuFrequencyMhz(CPU_FREQ_IDLE);
    _boosted = false;
    _lowPower = false;
    MESH_DEBUG_PRINTLN("CPU power: idle at %d MHz", CPU_FREQ_IDLE);
  }

  void loop() {
    if (_boosted && (millis() - _boost_started >= CPU_BOOST_TIMEOUT_MS)) {
      // Return to low-power if locked, otherwise normal idle
      if (_lowPower) {
        setCpuFrequencyMhz(CPU_FREQ_LOW_POWER);
        MESH_DEBUG_PRINTLN("CPU power: boost expired, returning to low-power %d MHz", CPU_FREQ_LOW_POWER);
      } else {
        setCpuFrequencyMhz(CPU_FREQ_IDLE);
        MESH_DEBUG_PRINTLN("CPU power: idle at %d MHz", CPU_FREQ_IDLE);
      }
      _boosted = false;
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
    if (_lowPower) {
      _lowPower = false;
    }
  }

  // Low-power mode — drops CPU to 40 MHz for lock screen standby.
  // If currently boosted, the boost timeout will return to 40 MHz
  // instead of 80 MHz.
  void setLowPower() {
    _lowPower = true;
    if (!_boosted) {
      setCpuFrequencyMhz(CPU_FREQ_LOW_POWER);
      MESH_DEBUG_PRINTLN("CPU power: low-power at %d MHz", CPU_FREQ_LOW_POWER);
    }
    // If boosted, the loop() timeout will drop to low-power instead of idle
  }

  // Exit low-power mode — returns to normal idle (80 MHz).
  // If currently boosted, the boost timeout will return to idle
  // instead of low-power.
  void clearLowPower() {
    _lowPower = false;
    if (!_boosted) {
      setCpuFrequencyMhz(CPU_FREQ_IDLE);
      MESH_DEBUG_PRINTLN("CPU power: idle at %d MHz (low-power cleared)", CPU_FREQ_IDLE);
    }
    // If boosted, the loop() timeout will drop to idle as normal
  }

  bool isBoosted() const { return _boosted; }
  bool isLowPower() const { return _lowPower; }
  uint32_t getFrequencyMHz() const { return getCpuFrequencyMhz(); }

private:
  bool _boosted;
  bool _lowPower;
  unsigned long _boost_started;
};

#endif // ESP32