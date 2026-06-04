#pragma once

#include <Arduino.h>

// Transparent Stream wrapper that counts NMEA sentences (newline-delimited)
// flowing from the GPS serial port to the MicroNMEA parser.
//
// Usage:  Instead of  MicroNMEALocationProvider gps(Serial2, &rtc_clock);
//         Use:        GPSStreamCounter gpsStream(Serial2);
//                     MicroNMEALocationProvider gps(gpsStream, &rtc_clock);
//
// Every read() call passes through to the underlying stream; when a '\n'
// is seen the sentence counter increments.  This lets the UI display a
// live "nmea" count so users can confirm the baud rate is correct and
// the GPS module is actually sending data.
//
// MAX addition: as bytes pass through, each completed $xxGSV sentence is
// tapped for its "satellites in view" field. The per-constellation talkers
// (GP/GL/GA/BD/GB/...) are tracked separately and summed, so the total
// reflects multi-constellation reception (used to confirm $PCAS04,7 took
// effect). This costs only a small per-byte accumulation in read().

class GPSStreamCounter : public Stream {
public:
  GPSStreamCounter(Stream& inner)
    : _inner(inner), _sentences(0), _sentences_snapshot(0),
      _last_snapshot(0), _sentences_per_sec(0), _line_len(0), _gsv_n(0) {}

  // --- Stream read interface (passes through) ---
  int available() override { return _inner.available(); }
  int peek()      override { return _inner.peek(); }

  int read() override {
    int c = _inner.read();
    if (c < 0) return c;

    // Accumulate the current sentence so completed $xxGSV lines can be tapped
    // for their satellites-in-view count.
    if (c == '$') {
      _line_len = 0;
      _line[_line_len++] = (char)c;
    } else if (c == '\n') {
      if (_line_len > 0) { _line[_line_len] = '\0'; parseGSV(_line); }
      _sentences++;
      _line_len = 0;
    } else if (c != '\r' && _line_len < (int)sizeof(_line) - 1) {
      _line[_line_len++] = (char)c;
    }
    return c;
  }

  // --- Stream write interface (pass through for NMEA commands if needed) ---
  size_t write(uint8_t b) override { return _inner.write(b); }

  // --- Sentence counting API ---

  // Total sentences received since boot (or last reset)
  uint32_t getSentenceCount() const { return _sentences; }

  // Sentences received per second (updated each time you call it,
  // with a 1-second rolling window)
  uint16_t getSentencesPerSec() {
    unsigned long now = millis();
    unsigned long elapsed = now - _last_snapshot;
    if (elapsed >= 1000) {
      uint32_t delta = _sentences - _sentences_snapshot;
      // Scale to per-second if interval wasn't exactly 1000ms
      _sentences_per_sec = (uint16_t)((delta * 1000UL) / elapsed);
      _sentences_snapshot = _sentences;
      _last_snapshot = now;
    }
    return _sentences_per_sec;
  }

  // Total satellites in view across all constellations, summed from the latest
  // $xxGSV per talker. Talkers not heard in the last few seconds are dropped so
  // the figure tracks the live sky rather than accumulating stale entries.
  uint16_t getSatellitesInView() {
    uint16_t total = 0;
    unsigned long now = millis();
    for (int i = 0; i < _gsv_n; i++) {
      if (now - _gsv[i].seen <= 8000) total += _gsv[i].inview;
    }
    return total;
  }

  // Per-talker in-view summary, e.g. "GP=11 GL=0 GA=5 BD=8", for diagnostics.
  // Only talkers heard in the last few seconds are listed.
  void getInViewBreakdown(char* buf, size_t len) {
    if (len == 0) return;
    buf[0] = '\0';
    size_t pos = 0;
    unsigned long now = millis();
    for (int i = 0; i < _gsv_n; i++) {
      if (now - _gsv[i].seen > 8000) continue;
      int n = snprintf(buf + pos, len - pos, "%s%c%c=%u",
                       pos ? " " : "", _gsv[i].t0, _gsv[i].t1, _gsv[i].inview);
      if (n <= 0 || (size_t)n >= len - pos) break;
      pos += (size_t)n;
    }
  }

  // Reset all counters (e.g. when GPS hardware power cycles)
  void resetCounters() {
    _sentences = 0;
    _sentences_snapshot = 0;
    _sentences_per_sec = 0;
    _last_snapshot = millis();
    _gsv_n = 0;
    _line_len = 0;
  }

private:
  Stream& _inner;
  volatile uint32_t _sentences;
  uint32_t _sentences_snapshot;
  unsigned long _last_snapshot;
  uint16_t _sentences_per_sec;

  // Current sentence accumulation (for the GSV tap)
  char _line[100];
  int  _line_len;

  // Per-talker satellites-in-view, e.g. GP (GPS), GL (GLONASS), BD/GB (BeiDou),
  // GA (Galileo). Small fixed table; combined "GN" talkers are skipped so they
  // don't double-count the per-constellation sentences.
  struct GsvEntry { char t0; char t1; uint8_t inview; unsigned long seen; };
  GsvEntry _gsv[6];
  int _gsv_n;

  void parseGSV(const char* s) {
    // Expect "$ttGSV,total,num,inview,..." where tt is the 2-char talker id.
    if (s[0] != '$' || s[1] == '\0' || s[2] == '\0') return;
    if (!(s[3] == 'G' && s[4] == 'S' && s[5] == 'V')) return;
    char t0 = s[1], t1 = s[2];
    if (t0 == 'G' && t1 == 'N') return;  // combined talker - skip to avoid double count

    // The satellites-in-view value is the field after the 3rd comma.
    const char* p = s;
    int commas = 0;
    while (*p && commas < 3) { if (*p == ',') commas++; p++; }
    if (commas < 3) return;
    int inview = atoi(p);

    unsigned long now = millis();
    for (int i = 0; i < _gsv_n; i++) {
      if (_gsv[i].t0 == t0 && _gsv[i].t1 == t1) {
        _gsv[i].inview = (uint8_t)inview;
        _gsv[i].seen = now;
        return;
      }
    }
    if (_gsv_n < (int)(sizeof(_gsv) / sizeof(_gsv[0]))) {
      _gsv[_gsv_n].t0 = t0;
      _gsv[_gsv_n].t1 = t1;
      _gsv[_gsv_n].inview = (uint8_t)inview;
      _gsv[_gsv_n].seen = now;
      _gsv_n++;
    }
  }
};