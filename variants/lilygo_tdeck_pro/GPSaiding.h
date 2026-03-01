#pragma once

#include <Arduino.h>

// =============================================================================
// GPS Aiding Helper for u-blox MIA-M10Q
// =============================================================================
//
// Sends UBX-MGA-INI aiding messages to the GNSS receiver after each power
// cycle to reduce Time-To-First-Fix (TTFF).  When the duty cycle manager
// cuts hardware power, the MIA-M10Q loses all ephemeris, almanac, position,
// and time data — every wake-up is a cold start (~2-3 min).
//
// By injecting the last known position (UBX-MGA-INI-POS_LLH) and the
// current UTC time (UBX-MGA-INI-TIME_UTC) immediately after power-on,
// the receiver can narrow its satellite search window and skip the slow
// almanac download phase.  This typically reduces TTFF from ~3 min to
// 30-60 seconds even without backup battery support.
//
// Usage:
//   After powering the GPS module on and waiting for it to boot (~200ms),
//   call sendPositionAid() and sendTimeAid() via the GPS serial port.
//
// Reference:
//   u-blox MIA-M10Q Integration Manual (UBX-21028173)
//   Section 2.13 - AssistNow and aiding data
// =============================================================================

namespace GPSAiding {

// ---- UBX frame helpers ----

// Compute UBX Fletcher checksum over a buffer (class+id+length+payload)
static void ubxChecksum(const uint8_t* buf, size_t len, uint8_t& ckA, uint8_t& ckB) {
  ckA = 0;
  ckB = 0;
  for (size_t i = 0; i < len; i++) {
    ckA += buf[i];
    ckB += ckA;
  }
}

// Send a complete UBX frame:  sync(2) + class/id/len/payload + checksum(2)
// 'body' points to class byte, 'bodyLen' = 4 (header) + payload length
static void ubxSend(Stream& port, const uint8_t* body, size_t bodyLen) {
  const uint8_t sync[] = { 0xB5, 0x62 };
  port.write(sync, 2);
  port.write(body, bodyLen);

  uint8_t ckA, ckB;
  ubxChecksum(body, bodyLen, ckA, ckB);
  port.write(ckA);
  port.write(ckB);
}

// ---- Aiding messages ----

// Send UBX-MGA-INI-POS_LLH (position aiding)
//
// lat, lon: degrees (double, e.g. -33.8688, 151.2093)
// altCm:    altitude in centimetres (0 if unknown)
// accCm:    position accuracy estimate in cm (e.g. 30000 = 300m)
//
// The MIA-M10Q uses this to seed its position estimate, reducing the
// satellite search space from global to a regional window.
static void sendPositionAid(Stream& port, double lat, double lon,
                            int32_t altCm = 0, uint32_t accCm = 30000)
{
  // Validate: skip if position is clearly invalid (0,0 or out of range)
  if (lat == 0.0 && lon == 0.0) return;
  if (lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) return;

  // UBX-MGA-INI (class 0x13, id 0x40), payload = 20 bytes
  // Payload layout for type 0x01 (POS_LLH):
  //   [0]    type      = 0x01
  //   [1]    version   = 0x00
  //   [2-3]  reserved  = 0x00
  //   [4-7]  lat       = int32 (degrees * 1e7)
  //   [8-11] lon       = int32 (degrees * 1e7)
  //   [12-15] alt      = int32 (cm above ellipsoid)
  //   [16-19] posAcc   = uint32 (cm, position accuracy)

  const uint16_t payloadLen = 20;
  uint8_t frame[4 + payloadLen];  // class + id + length(2) + payload

  // Header
  frame[0] = 0x13;  // class: MGA
  frame[1] = 0x40;  // id:    INI
  frame[2] = payloadLen & 0xFF;         // length low
  frame[3] = (payloadLen >> 8) & 0xFF;  // length high

  // Payload
  uint8_t* p = &frame[4];
  memset(p, 0, payloadLen);

  p[0] = 0x01;  // type = POS_LLH
  p[1] = 0x00;  // version

  int32_t latE7 = (int32_t)(lat * 1e7);
  int32_t lonE7 = (int32_t)(lon * 1e7);

  memcpy(&p[4],  &latE7, 4);
  memcpy(&p[8],  &lonE7, 4);
  memcpy(&p[12], &altCm, 4);
  memcpy(&p[16], &accCm, 4);

  ubxSend(port, frame, sizeof(frame));

  MESH_DEBUG_PRINTLN("GPS aid: sent POS_LLH lat=%.5f lon=%.5f acc=%ucm",
                     lat, lon, (unsigned)accCm);
}

// Send UBX-MGA-INI-TIME_UTC (time aiding)
//
// utcEpoch: Unix timestamp (seconds since 1970-01-01)
// accSec:   time accuracy estimate in seconds (e.g. 2 = within 2s)
//
// Providing approximate UTC time lets the receiver predict which
// satellites should be visible, dramatically speeding up acquisition.
static void sendTimeAid(Stream& port, uint32_t utcEpoch, uint16_t accSec = 2)
{
  // Validate: skip if time looks invalid (before year 2024)
  if (utcEpoch < 1704067200UL) return;  // 2024-01-01

  // Break epoch into calendar components
  // Simple gmtime implementation for embedded use
  uint32_t t = utcEpoch;
  uint16_t year;
  uint8_t month, day, hour, minute, second;

  second = t % 60; t /= 60;
  minute = t % 60; t /= 60;
  hour   = t % 24; t /= 24;

  // Days since 1970-01-01
  uint32_t days = t;

  // Calculate year
  year = 1970;
  while (true) {
    uint16_t daysInYear = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
    if (days < daysInYear) break;
    days -= daysInYear;
    year++;
  }

  // Calculate month and day
  bool leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
  const uint16_t daysInMonth[] = { 31, (uint16_t)(leap ? 29 : 28), 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  month = 1;
  for (int i = 0; i < 12; i++) {
    if (days < daysInMonth[i]) break;
    days -= daysInMonth[i];
    month++;
  }
  day = days + 1;

  // UBX-MGA-INI (class 0x13, id 0x40), payload = 24 bytes
  // Payload layout for type 0x10 (TIME_UTC):
  //   [0]     type       = 0x10
  //   [1]     version    = 0x00
  //   [2]     ref        = 0x00 (UTC reference)
  //   [3]     leapSecs   = 0x80 (unknown)
  //   [4-5]   year       = uint16
  //   [6]     month      = uint8 (1-12)
  //   [7]     day        = uint8 (1-31)
  //   [8]     hour       = uint8 (0-23)
  //   [9]     minute     = uint8 (0-59)
  //   [10]    second     = uint8 (0-60)
  //   [11]    reserved1  = 0x00
  //   [12-15] ns         = uint32 (nanoseconds, 0)
  //   [16-17] tAccS      = uint16 (accuracy, seconds part)
  //   [18-19] reserved2  = 0x0000
  //   [20-23] tAccNs     = uint32 (accuracy, nanoseconds part, 0)

  const uint16_t payloadLen = 24;
  uint8_t frame[4 + payloadLen];

  // Header
  frame[0] = 0x13;  // class: MGA
  frame[1] = 0x40;  // id:    INI
  frame[2] = payloadLen & 0xFF;
  frame[3] = (payloadLen >> 8) & 0xFF;

  // Payload
  uint8_t* p = &frame[4];
  memset(p, 0, payloadLen);

  p[0] = 0x10;  // type = TIME_UTC
  p[1] = 0x00;  // version
  p[2] = 0x00;  // ref = UTC
  p[3] = 0x80;  // leapSecs = unknown (signed, 0x80 = flag for unknown)

  memcpy(&p[4], &year, 2);
  p[6]  = month;
  p[7]  = day;
  p[8]  = hour;
  p[9]  = minute;
  p[10] = second;
  // p[11] reserved = 0

  uint32_t ns = 0;
  memcpy(&p[12], &ns, 4);
  memcpy(&p[16], &accSec, 2);
  // p[18-19] reserved = 0
  uint32_t tAccNs = 0;
  memcpy(&p[20], &tAccNs, 4);

  ubxSend(port, frame, sizeof(frame));

  MESH_DEBUG_PRINTLN("GPS aid: sent TIME_UTC %04u-%02u-%02u %02u:%02u:%02u acc=%us",
                     year, month, day, hour, minute, second, accSec);
}

// Convenience: send all available aiding data after a GPS power-on.
// Call this ~200ms after enabling PIN_GPS_EN to give the module time to boot.
//
// lat, lon:    last known position (degrees). Skipped if both are 0.
// utcEpoch:    current UTC time from RTC. Skipped if < year 2024.
// posAccCm:    position accuracy in cm (default 300m for stale fixes)
// timeAccSec:  time accuracy in seconds (default 2s for RTC-derived time)
static void sendAllAiding(Stream& port, double lat, double lon,
                          uint32_t utcEpoch,
                          uint32_t posAccCm = 30000,
                          uint16_t timeAccSec = 2)
{
  // Position aiding
  sendPositionAid(port, lat, lon, 0, posAccCm);

  // Small delay between messages to avoid overrunning the receiver's
  // input buffer at 38400 baud (each message is ~30 bytes, well within
  // one UART buffer, but better safe)
  delay(10);

  // Time aiding
  sendTimeAid(port, utcEpoch, timeAccSec);
}

} // namespace GPSAiding