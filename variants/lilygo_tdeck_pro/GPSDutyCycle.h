#pragma once

#include <Arduino.h>
#include "variant.h"
#include "GPSStreamCounter.h"

// GPS Duty Cycle Manager
// Controls the hardware GPS enable pin (PIN_GPS_EN) to save power.
// When enabled, cycles between acquiring a fix and sleeping with power cut.
//
// States:
//   OFF        – User has disabled GPS. Hardware power is cut.
//   ACQUIRING  – GPS module powered on, waiting for a fix or timeout.
//   SLEEPING   – GPS module powered off, timer counting down to next cycle.

#if HAS_GPS

// How long to leave GPS powered on while acquiring a fix (ms)
#ifndef GPS_ACQUIRE_TIMEOUT_MS
#define GPS_ACQUIRE_TIMEOUT_MS    180000   // 3 minutes
#endif

// How long to sleep between acquisition cycles (ms)
#ifndef GPS_SLEEP_DURATION_MS
#define GPS_SLEEP_DURATION_MS     900000   // 15 minutes
#endif

// If we get a fix quickly, power off immediately but still respect
// a minimum on-time so the RTC can sync properly
#ifndef GPS_MIN_ON_TIME_MS
#define GPS_MIN_ON_TIME_MS        5000     // 5 seconds after fix
#endif

enum class GPSDutyState : uint8_t {
  OFF = 0,       // User-disabled, hardware power off
  ACQUIRING,     // Hardware on, waiting for fix
  SLEEPING       // Hardware off, timer running
};

class GPSDutyCycle {
public:
  GPSDutyCycle() : _state(GPSDutyState::OFF), _state_entered(0),
                   _last_fix_time(0), _got_fix(false), _time_synced(false),
                   _stream(nullptr) {}

  // Attach the stream counter so we can reset it on power cycles
  void setStreamCounter(GPSStreamCounter* stream) { _stream = stream; }

  // Call once in setup() after board.begin() and GPS serial init.
  void begin(bool initial_enable) {
    if (initial_enable) {
      _powerOn();
      _setState(GPSDutyState::ACQUIRING);
    } else {
      _powerOff();
      _setState(GPSDutyState::OFF);
    }
  }

  // Call every iteration of loop().
  // Returns true if GPS hardware is currently powered on.
  bool loop() {
    switch (_state) {
      case GPSDutyState::OFF:
        return false;

      case GPSDutyState::ACQUIRING: {
        unsigned long elapsed = millis() - _state_entered;

        if (_got_fix && elapsed >= GPS_MIN_ON_TIME_MS) {
          MESH_DEBUG_PRINTLN("GPS duty: fix acquired, powering off for %u min",
                             (unsigned)(GPS_SLEEP_DURATION_MS / 60000));
          _powerOff();
          _setState(GPSDutyState::SLEEPING);
          return false;
        }

        if (elapsed >= GPS_ACQUIRE_TIMEOUT_MS) {
          MESH_DEBUG_PRINTLN("GPS duty: acquire timeout (%us), sleeping",
                             (unsigned)(GPS_ACQUIRE_TIMEOUT_MS / 1000));
          _powerOff();
          _setState(GPSDutyState::SLEEPING);
          return false;
        }

        return true;
      }

      case GPSDutyState::SLEEPING: {
        if (millis() - _state_entered >= GPS_SLEEP_DURATION_MS) {
          MESH_DEBUG_PRINTLN("GPS duty: waking up for next acquisition cycle");
          _got_fix = false;
          _powerOn();
          _setState(GPSDutyState::ACQUIRING);
          return true;
        }
        return false;
      }
    }
    return false;
  }

  void notifyFix() {
    if (_state == GPSDutyState::ACQUIRING && !_got_fix) {
      _got_fix = true;
      _last_fix_time = millis();
      MESH_DEBUG_PRINTLN("GPS duty: fix notification received");
    }
  }

  void notifyTimeSync() {
    _time_synced = true;
  }

  void enable() {
    if (_state == GPSDutyState::OFF) {
      _got_fix = false;
      _powerOn();
      _setState(GPSDutyState::ACQUIRING);
      MESH_DEBUG_PRINTLN("GPS duty: enabled, starting acquisition");
    }
  }

  void disable() {
    _powerOff();
    _setState(GPSDutyState::OFF);
    _got_fix = false;
    MESH_DEBUG_PRINTLN("GPS duty: disabled, power off");
  }

  void forceWake() {
    if (_state == GPSDutyState::SLEEPING) {
      _got_fix = false;
      _powerOn();
      _setState(GPSDutyState::ACQUIRING);
      MESH_DEBUG_PRINTLN("GPS duty: forced wake for user request");
    }
  }

  GPSDutyState getState() const { return _state; }
  bool isHardwareOn() const { return _state == GPSDutyState::ACQUIRING; }
  bool hadFix() const { return _got_fix; }
  bool hasTimeSynced() const { return _time_synced; }

  uint32_t sleepRemainingSecs() const {
    if (_state != GPSDutyState::SLEEPING) return 0;
    unsigned long elapsed = millis() - _state_entered;
    if (elapsed >= GPS_SLEEP_DURATION_MS) return 0;
    return (GPS_SLEEP_DURATION_MS - elapsed) / 1000;
  }

  uint32_t acquireElapsedSecs() const {
    if (_state != GPSDutyState::ACQUIRING) return 0;
    return (millis() - _state_entered) / 1000;
  }

private:
  void _powerOn() {
    #ifdef PIN_GPS_EN
      digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);
      delay(10);
    #endif
    if (_stream) _stream->resetCounters();
  }

  void _powerOff() {
    #ifdef PIN_GPS_EN
      digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
    #endif
  }

  void _setState(GPSDutyState s) {
    _state = s;
    _state_entered = millis();
  }

  GPSDutyState _state;
  unsigned long _state_entered;
  unsigned long _last_fix_time;
  bool _got_fix;
  bool _time_synced;
  GPSStreamCounter* _stream;
};

#endif // HAS_GPS