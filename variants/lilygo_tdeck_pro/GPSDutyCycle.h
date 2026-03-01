#pragma once

#include <Arduino.h>
#include "variant.h"
#include "GPSStreamCounter.h"
#include "GPSAiding.h"

// GPS Duty Cycle Manager
// Controls the hardware GPS enable pin (PIN_GPS_EN) to save power.
// When enabled, cycles between acquiring a fix and sleeping with power cut.
//
// After each power-on, sends UBX-MGA-INI aiding messages (last known
// position + RTC time) to the MIA-M10Q to reduce TTFF from cold-start
// (~3 min) down to ~30-60 seconds.
//
// States:
//   OFF        — User has disabled GPS. Hardware power is cut.
//   ACQUIRING  — GPS module powered on, waiting for a fix or timeout.
//   SLEEPING   — GPS module powered off, timer counting down to next cycle.

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

// Delay after hardware power-on before sending UBX aiding (ms).
// The MIA-M10Q needs time to boot its firmware before it can accept
// UBX commands on UART.  200ms is conservative; 100ms may work.
#ifndef GPS_BOOT_DELAY_MS
#define GPS_BOOT_DELAY_MS         250
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
                   _stream(nullptr), _serial(nullptr),
                   _aid_lat(0.0), _aid_lon(0.0), _aid_has_pos(false),
                   _rtc_time_fn(nullptr) {}

  // Attach the stream counter so we can reset it on power cycles
  void setStreamCounter(GPSStreamCounter* stream) { _stream = stream; }

  // Attach the raw GPS serial port for sending UBX aiding commands.
  // This should be the same underlying Stream that GPSStreamCounter wraps
  // (e.g. &Serial2).  If not set, aiding is silently skipped.
  void setSerialPort(Stream* serial) { _serial = serial; }

  // Provide the last known position for aiding on next power-on.
  // Call this at startup with saved prefs, and again after each fix
  // with updated coordinates.  lat/lon in degrees.
  void setLastKnownPosition(double lat, double lon) {
    if (lat != 0.0 || lon != 0.0) {
      _aid_lat = lat;
      _aid_lon = lon;
      _aid_has_pos = true;
    }
  }

  // Provide a function that returns the current UTC epoch (Unix time).
  // Used for time aiding.  Typically: []() { return rtc.getCurrentTime(); }
  // If not set or if the returned value is < year 2024, time aiding is skipped.
  void setRTCTimeSource(uint32_t (*fn)()) { _rtc_time_fn = fn; }

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

  // Notify that a GPS fix was obtained.  Optionally update the stored
  // aiding position so the next power cycle uses the freshest data.
  void notifyFix() {
    if (_state == GPSDutyState::ACQUIRING && !_got_fix) {
      _got_fix = true;
      _last_fix_time = millis();
      MESH_DEBUG_PRINTLN("GPS duty: fix notification received");
    }
  }

  // Extended version: also capture the fix position for future aiding
  void notifyFix(double lat, double lon) {
    notifyFix();
    setLastKnownPosition(lat, lon);
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

    // Send aiding data after the module has booted
    _sendAiding();
  }

  void _powerOff() {
    #ifdef PIN_GPS_EN
      digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
    #endif
  }

  // Send UBX-MGA-INI position and time aiding to the GPS module.
  // Called immediately after _powerOn().  The module needs a short
  // boot delay before it can process UBX commands.
  void _sendAiding() {
    if (_serial == nullptr) return;

    // Wait for the MIA-M10Q firmware to boot after power-on
    delay(GPS_BOOT_DELAY_MS);

    // Gather aiding data
    uint32_t utcTime = 0;
    if (_rtc_time_fn) {
      utcTime = _rtc_time_fn();
    }

    bool hasTime = (utcTime >= 1704067200UL);  // >= 2024-01-01

    if (!_aid_has_pos && !hasTime) {
      MESH_DEBUG_PRINTLN("GPS aid: no aiding data available (cold start)");
      return;
    }

    MESH_DEBUG_PRINTLN("GPS aid: sending aiding (pos=%s, time=%s)",
                       _aid_has_pos ? "yes" : "no",
                       hasTime ? "yes" : "no");

    // Use generous accuracy for stale position data.
    // 300m (30000cm) is reasonable for a device that hasn't moved much.
    // If position is from prefs (potentially very old), use 500m.
    uint32_t posAccCm = 50000;  // 500m default for saved prefs

    // If we got a fix this boot session, the position is fresher
    if (_last_fix_time > 0) {
      unsigned long ageMs = millis() - _last_fix_time;
      if (ageMs < 3600000UL) {        // < 1 hour old
        posAccCm = 10000;             // 100m
      } else if (ageMs < 86400000UL) { // < 24 hours old
        posAccCm = 30000;             // 300m
      }
    }

    // Time accuracy: RTC without GPS sync drifts ~2ppm = ~1 min/month.
    // After a recent GPS time sync, accuracy is within a few seconds.
    // Conservative default: 10 seconds.
    uint16_t timeAccSec = 10;
    if (_time_synced) {
      // RTC was synced from GPS this boot — much more accurate
      timeAccSec = 2;
    }

    GPSAiding::sendAllAiding(*_serial, _aid_lat, _aid_lon,
                             utcTime, posAccCm, timeAccSec);
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

  // Aiding support
  Stream* _serial;              // Raw GPS UART for sending UBX commands
  double _aid_lat;              // Last known latitude (degrees)
  double _aid_lon;              // Last known longitude (degrees)
  bool _aid_has_pos;            // true if we have a valid position
  uint32_t (*_rtc_time_fn)();   // Returns current UTC epoch, or nullptr
};

#endif // HAS_GPS