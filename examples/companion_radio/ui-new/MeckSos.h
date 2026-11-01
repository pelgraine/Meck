#pragma once
// =============================================================================
// SOS / emergency mode -- the logic (docs/sos.md)
//
//   IDLE --hold 3 s / menu--> COUNTDOWN --10 s / 5 s--> ACTIVE --"I'm OK"--> CANCELLING
//     ^                           | any input                |  Stop            | Stop, or
//     +---------------------------+--------------------------+------------------+ OK heard
//
// ACTIVE sends the SOS at once and then again after 1, 2, 5 and every 10
// minutes. Before each send the GPS gets a window to find a fix and is
// switched off again once it has one, so the device can sleep in between.
// A position older than 2 hours is not sent at all ("position unknown")
// rather than a wrong one, and when a fix turns up after a send that had no
// fresh position, an update goes out at once.
//
// "I'm OK" cancels a rescue, which matters as much as raising one, so it is
// repeated on the same schedule until it was sent at least twice and at
// least one repeater was heard passing it on.
//
// No hardware or mesh code here: main.cpp feeds in the time, the GPS state
// and the repeater echoes, and carries out what tick() asks for. Times are
// millis() values; all comparisons survive the 49-day wrap.
// =============================================================================

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "MeckGeo.h"

class MeckSos {
public:
  enum State : uint8_t { IDLE, COUNTDOWN, ACTIVE, CANCELLING };
  enum Msg : uint8_t {
    MSG_SOS,          // "SOS! Need help."
    MSG_OK_CANCEL,    // "I'm OK - cancel SOS." (ends an SOS)
    MSG_OK,           // "I'm OK." (no SOS running: a check-in)
  };
  // How the last SOS ended, for the screen
  enum Outcome : uint8_t { OUT_NONE, OUT_COUNTDOWN_CANCELLED, OUT_STOPPED, OUT_OK_CONFIRMED };

  // tick() result bits. The GPS is not among them: main.cpp keeps the
  // hardware in line with gpsOn() every loop.
  static const uint16_t ACT_SEND_SOS = 1 << 0;
  static const uint16_t ACT_SEND_OK  = 1 << 1;   // MSG_OK_CANCEL or MSG_OK: see pendingOkMsg()
  static const uint16_t ACT_PULSE    = 1 << 2;   // countdown second: buzz, flash, redraw
  static const uint16_t ACT_REDRAW   = 1 << 3;
  static const uint16_t ACT_FINISHED = 1 << 4;   // "I'm OK" confirmed, back to IDLE

  static const uint32_t HOLD_MS           = 3000;     // top button hold that arms the countdown
  static const uint32_t COUNTDOWN_HOLD_MS = 10000;    // countdown after the hold
  static const uint32_t COUNTDOWN_MENU_MS = 5000;     // after the menu: already a deliberate second step
  static const uint32_t GPS_WARMUP_MS     = 90000;    // GPS on this long before a send
  static const uint32_t GPS_EXTRA_MS      = 180000;   // after a send without a fresh fix
  static const uint32_t FIX_FRESH_MS      = 300000;   // a fix this recent is "now"
  static const uint32_t FIX_MAX_AGE_MS    = 7200000;  // older positions are not sent
  static const uint32_t RETRY_MS          = 10000;    // after a send that failed
  static const uint8_t  OK_MIN_SENDS      = 2;
  static const uint8_t  ECHO_MAX          = 16;       // distinct repeaters remembered
  static const uint32_t REPLY_KEEP_MS     = 1800000;  // a DM is not pushed off by public chatter for this long

  // Gap after the n-th send (n = 1, 2, ...): 1, 2, 5, then 10 minutes
  static uint32_t intervalAfter(uint16_t n) {
    if (n <= 1) return 60000;
    if (n == 2) return 120000;
    if (n == 3) return 300000;
    return 600000;
  }

  // --- Inputs -----------------------------------------------------------------

  // Start the countdown (the hold, or the menu's SOS). False when an SOS
  // is already counting down or running.
  bool arm(uint32_t now, uint32_t countdownMs) {
    if (_state != IDLE) return false;
    _state = COUNTDOWN;
    _deadline = now + countdownMs;
    _lastPulseSec = -1;
    _sends = 0;
    _okSends = 0;
    _echoN = 0;
    _okEchoN = 0;
    _bestSnr = -128;
    _needUpdate = false;
    _gpsUntil = 0;
    _holdoffUntil = 0;
    _outcome = OUT_NONE;
    _reply[0] = 0;
    _replyImportant = false;
    return true;
  }

  // Any key, touch or button press while counting down
  bool cancelCountdown() {
    if (_state != COUNTDOWN) return false;
    _state = IDLE;
    _outcome = OUT_COUNTDOWN_CANCELLED;
    return true;
  }

  // The menu's "I'm OK": ends a running SOS (repeated until heard), or
  // without one sends a single check-in
  void requestOk(uint32_t now) {
    if (_state == ACTIVE) {
      _state = CANCELLING;
      _okSends = 0;
      _okEchoN = 0;
      _nextSendAt = now;
      _needUpdate = false;
      _gpsUntil = 0;
      _holdoffUntil = 0;
    } else if (_state == IDLE) {
      _oneShotOk = true;
    }
  }

  // The menu's "Stop": ends everything without a message
  void stop() {
    if (_state == IDLE) return;
    _state = IDLE;
    _outcome = OUT_STOPPED;
  }

  // Called every loop. fixLive = the GPS is powered and has a fix now.
  uint16_t tick(uint32_t now, bool fixLive, double lat, double lon) {
    uint16_t acts = 0;
    if (fixLive && (lat != 0 || lon != 0)) {
      _hasFix = true;
      _lat = lat;
      _lon = lon;
      _fixAt = now;
    }
    bool held = _holdoffUntil && !reached(now, _holdoffUntil);
    if (_gpsUntil && reached(now, _gpsUntil)) {
      _gpsUntil = 0;
      _needUpdate = false;     // no fix in time: the next scheduled send has to do
    }
    switch (_state) {
      case IDLE:
        if (_oneShotOk && !held) acts |= ACT_SEND_OK;
        break;
      case COUNTDOWN: {
        int32_t left = (int32_t)(_deadline - now);
        if (left <= 0) {
          _state = ACTIVE;
          _nextSendAt = now;
          acts |= ACT_REDRAW;
        } else {
          int sec = (left + 999) / 1000;
          if (sec != _lastPulseSec) {
            _lastPulseSec = sec;
            acts |= ACT_PULSE;
          }
        }
        break;
      }
      case ACTIVE:
        break;
      case CANCELLING:
        if (_okSends >= OK_MIN_SENDS && _okEchoN > 0) {
          _state = IDLE;
          _outcome = OUT_OK_CONFIRMED;
          acts |= ACT_FINISHED | ACT_REDRAW;
        }
        break;
    }
    if (_state == ACTIVE && !held) {
      if (reached(now, _nextSendAt)) acts |= ACT_SEND_SOS;
      else if (_needUpdate && _hasFix && _fixAt == now) acts |= ACT_SEND_SOS;   // fix after a send without one
    }
    if (_state == CANCELLING && !held && reached(now, _nextSendAt)) acts |= ACT_SEND_OK;

    _gpsOn = gpsWanted(now);
    return acts;
  }

  // Which "I'm OK" an ACT_SEND_OK is
  Msg pendingOkMsg() const { return _state == CANCELLING ? MSG_OK_CANCEL : MSG_OK; }

  // The message went out (to at least the public channel)
  void onSent(Msg m, uint32_t now) {
    _holdoffUntil = 0;
    if (m == MSG_SOS) {
      bool scheduled = reached(now, _nextSendAt);
      _sends++;
      bool fresh = fixFresh(now);
      if (scheduled) {
        _nextSendAt = now + intervalAfter(_sends);
        _needUpdate = !fresh;
      } else {
        _needUpdate = false;                 // the one update after a send without a fix
      }
      if (_needUpdate) {
        _gpsUntil = now + GPS_EXTRA_MS;
        _gpsFrom = now;
      } else {
        _gpsUntil = 0;
      }
    } else if (m == MSG_OK_CANCEL) {
      _okSends++;
      _nextSendAt = now + intervalAfter(_okSends);
    } else {
      _oneShotOk = false;
    }
  }

  // The send failed (no packet buffer): try again a little later
  void onSendFailed(uint32_t now) {
    _holdoffUntil = now + RETRY_MS;
    if (_holdoffUntil == 0) _holdoffUntil = 1;
  }

  // A repeater passed one of our messages on. hash = the first path hash
  // of the echo (bph bytes, 1-3), snr in quarter dB.
  void addEcho(Msg m, const uint8_t* hash, uint8_t bph, int8_t snr) {
    uint32_t key = 0;
    for (uint8_t i = 0; i < bph && i < 3; i++) key = (key << 8) | hash[i];
    key |= (uint32_t)bph << 24;
    if (m == MSG_SOS) {
      addKey(_echo, _echoN, key);
      if (snr > _bestSnr) _bestSnr = snr;
    } else if (m == MSG_OK_CANCEL) {
      addKey(_okEcho, _okEchoN, key);
    }
  }

  // Drops the remembered fix (debug CLI: after a faked one)
  void forgetFix() { _hasFix = false; }

  // A message arrived on the public / an SOS channel or as a DM while an
  // SOS runs: shown on the SOS screen so a reply is not missed. important =
  // a DM or an SOS channel; public chatter does not replace one of those
  // for REPLY_KEEP_MS.
  void setReply(const char* from, const char* text, uint32_t now, bool important) {
    if (!important && _replyImportant && _reply[0] && now - _replyAt < REPLY_KEEP_MS) return;
    snprintf(_reply, sizeof(_reply), "%s: %s", from ? from : "", text ? text : "");
    _replyAt = now;
    _replyImportant = important;
  }

  // --- Message text -----------------------------------------------------------

  // ASCII text for a message; returns its length. A name prefix of up to
  // 32 chars is added by the mesh, the whole must stay within 133 chars.
  int buildText(Msg m, char* out, size_t n, uint32_t now, int battPct) const {
    if (!out || n == 0) return 0;
    out[0] = 0;
    size_t len = 0;
    const char* head = m == MSG_SOS ? "SOS! Need help." : m == MSG_OK_CANCEL ? "I'm OK - cancel SOS." : "I'm OK.";
    len = append(out, n, len, "%s", head);
    char age[12];
    if (fixUsable(now)) {
      char loc[48];
      meckGeoFormatShare(loc, sizeof(loc), _lat, _lon);
      formatAge(age, sizeof(age), fixAgeMs(now));
      len = append(out, n, len, " %s fix %s", loc, age);
    } else if (m == MSG_SOS) {
      if (_hasFix) {
        formatAge(age, sizeof(age), fixAgeMs(now));
        len = append(out, n, len, " Position unknown, last fix %s ago.", age);
      } else {
        len = append(out, n, len, " Position unknown.");
      }
    }
    if (battPct >= 0) len = append(out, n, len, " bat %d%%", battPct > 100 ? 100 : battPct);
    if (m == MSG_SOS) len = append(out, n, len, " #%u", (unsigned)(_sends + 1));
    return (int)len;
  }

  // "45s", "12m", "1h05m"
  static void formatAge(char* out, size_t n, uint32_t ms) {
    uint32_t s = ms / 1000;
    if (s < 60) snprintf(out, n, "%us", (unsigned)s);
    else if (s < 3600) snprintf(out, n, "%um", (unsigned)(s / 60));
    else snprintf(out, n, "%uh%02um", (unsigned)(s / 3600), (unsigned)((s / 60) % 60));
  }

  // --- State for the screen, the sleep code and the debug CLI ----------------

  State state() const { return _state; }
  bool running() const { return _state != IDLE; }
  bool gpsOn() const { return _gpsOn; }
  Outcome outcome() const { return _outcome; }
  int countdownSecs(uint32_t now) const {
    if (_state != COUNTDOWN) return 0;
    int32_t left = (int32_t)(_deadline - now);
    return left <= 0 ? 0 : (left + 999) / 1000;
  }
  uint16_t sends() const { return _sends; }
  uint16_t okSends() const { return _okSends; }
  uint8_t heardCount() const { return _echoN; }
  uint8_t okHeardCount() const { return _okEchoN; }
  int8_t bestSnr() const { return _bestSnr; }    // quarter dB, -128 = none
  // ms until the next scheduled send (ACTIVE / CANCELLING), 0 when due
  uint32_t nextSendIn(uint32_t now) const {
    if (_state != ACTIVE && _state != CANCELLING) return 0;
    int32_t d = (int32_t)(_nextSendAt - now);
    return d > 0 ? (uint32_t)d : 0;
  }
  bool hasFix() const { return _hasFix; }
  double lat() const { return _lat; }
  double lon() const { return _lon; }
  uint32_t fixAgeMs(uint32_t now) const { return _hasFix ? now - _fixAt : 0xFFFFFFFF; }
  bool fixFresh(uint32_t now) const { return _hasFix && fixAgeMs(now) <= FIX_FRESH_MS; }
  bool fixUsable(uint32_t now) const { return _hasFix && fixAgeMs(now) <= FIX_MAX_AGE_MS; }
  const char* reply() const { return _reply; }
  uint32_t replyAt() const { return _replyAt; }

  // The device must not sleep: counting down, GPS on, or a send waiting
  bool wantsAwake(uint32_t now) const {
    if (_state == COUNTDOWN || _gpsOn || _oneShotOk) return true;
    if ((_state == ACTIVE || _state == CANCELLING) && reached(now, _nextSendAt)) return true;
    return false;
  }

  // ms until the next thing that is still ahead (for the sleep timer);
  // 0xFFFFFFFF = nothing. What is already due keeps the device awake
  // through wantsAwake() instead.
  uint32_t nextEventIn(uint32_t now) const {
    if (_state != ACTIVE && _state != CANCELLING) return 0xFFFFFFFF;
    uint32_t best = 0xFFFFFFFF;
    ahead(now, _nextSendAt, best);
    ahead(now, _nextSendAt - GPS_WARMUP_MS, best);
    if (_gpsUntil) ahead(now, _gpsUntil, best);
    return best;
  }

private:
  State _state = IDLE;
  Outcome _outcome = OUT_NONE;
  uint32_t _deadline = 0;          // countdown end
  int _lastPulseSec = -1;
  uint16_t _sends = 0, _okSends = 0;
  uint32_t _nextSendAt = 0;
  uint32_t _holdoffUntil = 0;      // 0 = none
  bool _needUpdate = false;        // last SOS had no fresh position
  bool _oneShotOk = false;
  bool _gpsOn = false;
  uint32_t _gpsUntil = 0, _gpsFrom = 0;   // GPS kept on after a send without a fix
  bool _hasFix = false;
  double _lat = 0, _lon = 0;
  uint32_t _fixAt = 0;
  uint32_t _echo[ECHO_MAX], _okEcho[ECHO_MAX];
  uint8_t _echoN = 0, _okEchoN = 0;
  int8_t _bestSnr = -128;
  char _reply[96] = "";
  uint32_t _replyAt = 0;
  bool _replyImportant = false;

  static bool reached(uint32_t now, uint32_t t) { return (int32_t)(now - t) >= 0; }
  static void ahead(uint32_t now, uint32_t t, uint32_t& best) {
    int32_t d = (int32_t)(t - now);
    if (d > 0 && (uint32_t)d < best) best = (uint32_t)d;
  }
  bool fixSince(uint32_t t) const { return _hasFix && (int32_t)(_fixAt - t) >= 0; }

  // GPS: always while counting down; before a send until a fix comes in;
  // after a send without a fresh fix for GPS_EXTRA_MS or until a fix
  bool gpsWanted(uint32_t now) const {
    if (_state == COUNTDOWN) return true;
    if (_state != ACTIVE && _state != CANCELLING) return false;
    uint32_t windowStart = _nextSendAt - GPS_WARMUP_MS;
    if (reached(now, windowStart) && !fixSince(windowStart)) return true;
    if (_gpsUntil && !reached(now, _gpsUntil) && !fixSince(_gpsFrom)) return true;
    return false;
  }

  static bool addKey(uint32_t* set, uint8_t& n, uint32_t key) {
    for (uint8_t i = 0; i < n; i++) if (set[i] == key) return false;
    if (n >= ECHO_MAX) return false;
    set[n++] = key;
    return true;
  }

  static size_t append(char* out, size_t n, size_t len, const char* fmt, ...) __attribute__((format(printf, 4, 5)));
};

inline size_t MeckSos::append(char* out, size_t n, size_t len, const char* fmt, ...) {
  if (len >= n - 1) return len;
  va_list ap;
  va_start(ap, fmt);
  int w = vsnprintf(out + len, n - len, fmt, ap);
  va_end(ap);
  if (w < 0) return len;
  len += (size_t)w;
  return len < n ? len : n - 1;
}
