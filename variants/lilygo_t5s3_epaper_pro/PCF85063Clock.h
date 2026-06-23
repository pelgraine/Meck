#pragma once
// =============================================================================
// PCF85063Clock — PCF8563/BM8563 RTC driver for T5S3 E-Paper Pro
//
// Time registers at 0x02–0x08 (PCF8563 layout):
//   0x02 Seconds, 0x03 Minutes, 0x04 Hours,
//   0x05 Days, 0x06 Weekdays, 0x07 Months, 0x08 Years
// =============================================================================

#include <Arduino.h>
#include <Wire.h>
#include <MeshCore.h>

#define PCF8563_ADDR        0x51
#define PCF8563_REG_SECONDS 0x02

// Reject timestamps outside 2024–2036 (blocks MeshCore contacts garbage)
#define EPOCH_MIN_SANE  1704067200UL
#define EPOCH_MAX_SANE  2082758400UL

class PCF85063Clock : public mesh::RTCClock {
public:
  PCF85063Clock() : _wire(nullptr), _millis_offset(0),
                    _has_hw_time(false), _time_set_this_session(false) {}

  bool begin(TwoWire& wire) {
    _wire = &wire;

    _wire->beginTransmission(PCF8563_ADDR);
    if (_wire->endTransmission() != 0) {
      Serial.println("[RTC] PCF8563 not found");
      return false;
    }

    // Repair any corrupted registers from prior wrong-offset writes
    repairRegisters();

    uint32_t t = readHardwareTime();
    if (t > EPOCH_MIN_SANE && t < EPOCH_MAX_SANE) {
      _has_hw_time = true;
      _millis_offset = t - (millis() / 1000);
      Serial.printf("[RTC] PCF8563 OK, time=%lu\n", t);
    } else {
      _has_hw_time = false;
      Serial.printf("[RTC] PCF8563 no valid time (%lu), awaiting BLE sync\n", t);
    }
    return true;
  }

  uint32_t getCurrentTime() override {
    if (_time_set_this_session) {
      return _millis_offset + (millis() / 1000);
    }
    if (_has_hw_time && _wire) {
      uint32_t t = readHardwareTime();
      if (t > EPOCH_MIN_SANE && t < EPOCH_MAX_SANE) {
        _millis_offset = t - (millis() / 1000);
        return t;
      }
      _has_hw_time = false;
    }
    return _millis_offset + (millis() / 1000);
  }

  void setCurrentTime(uint32_t time) override {
    if (time < EPOCH_MIN_SANE || time > EPOCH_MAX_SANE) {
      Serial.printf("[RTC] setCurrentTime(%lu) REJECTED\n", time);
      return;
    }
    _millis_offset = time - (millis() / 1000);
    _time_set_this_session = true;
    Serial.printf("[RTC] setCurrentTime(%lu) OK\n", time);
    if (_wire) writeHardwareTime(time);
  }

private:
  TwoWire*  _wire;
  uint32_t  _millis_offset;
  bool      _has_hw_time;
  bool      _time_set_this_session;

  // ---- Register helpers ----
  void writeReg(uint8_t reg, uint8_t val) {
    _wire->beginTransmission(PCF8563_ADDR);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
  }

  uint8_t readReg(uint8_t reg) {
    _wire->beginTransmission(PCF8563_ADDR);
    _wire->write(reg);
    if (_wire->endTransmission(false) != 0) return 0xFF;
    if (_wire->requestFrom((uint8_t)PCF8563_ADDR, (uint8_t)1) != 1) return 0xFF;
    return _wire->read();
  }

  // ---- Fix registers corrupted by prior PCF85063A-mode writes ----
  void repairRegisters() {
    uint8_t hours = readReg(0x04) & 0x3F;
    if (bcd2dec(hours) > 23) {
      Serial.printf("[RTC] Repairing hours (0x%02X→0x00)\n", hours);
      writeReg(0x04, 0x00);
    }
    uint8_t days = readReg(0x05) & 0x3F;
    if (bcd2dec(days) == 0 || bcd2dec(days) > 31) {
      Serial.printf("[RTC] Repairing days (0x%02X→0x01)\n", days);
      writeReg(0x05, 0x01);
    }
    uint8_t month = readReg(0x07) & 0x1F;
    if (bcd2dec(month) == 0 || bcd2dec(month) > 12) {
      Serial.printf("[RTC] Repairing month (0x%02X→0x01)\n", month);
      writeReg(0x07, 0x01);
    }
  }

  // ---- BCD ----
  static uint8_t bcd2dec(uint8_t bcd) { return ((bcd >> 4) * 10) + (bcd & 0x0F); }
  static uint8_t dec2bcd(uint8_t dec) { return ((dec / 10) << 4) | (dec % 10); }

  // ---- Date helpers ----
  static bool isLeap(int y) { return (y%4==0 && y%100!=0) || y%400==0; }
  static int daysInMonth(int m, int y) {
    static const uint8_t d[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    return (m==2 && isLeap(y)) ? 29 : d[m-1];
  }

  static uint32_t toEpoch(int yr, int mo, int dy, int h, int mi, int s) {
    uint32_t days = 0;
    for (int y = 1970; y < yr; y++) days += isLeap(y) ? 366 : 365;
    for (int m = 1; m < mo; m++) days += daysInMonth(m, yr);
    days += (dy - 1);
    return days * 86400UL + h * 3600UL + mi * 60UL + s;
  }

  static void fromEpoch(uint32_t ep, int& yr, int& mo, int& dy, int& h, int& mi, int& s) {
    s = ep % 60; ep /= 60;
    mi = ep % 60; ep /= 60;
    h  = ep % 24; ep /= 24;
    yr = 1970;
    while (true) { int d = isLeap(yr)?366:365; if (ep<(uint32_t)d) break; ep-=d; yr++; }
    mo = 1;
    while (true) { int d = daysInMonth(mo,yr); if (ep<(uint32_t)d) break; ep-=d; mo++; }
    dy = ep + 1;
  }

  // ---- Read time (burst from 0x02) ----
  uint32_t readHardwareTime() {
    _wire->beginTransmission(PCF8563_ADDR);
    _wire->write(PCF8563_REG_SECONDS);
    if (_wire->endTransmission(false) != 0) return 0;
    if (_wire->requestFrom((uint8_t)PCF8563_ADDR, (uint8_t)7) != 7) return 0;

    uint8_t raw[7];
    for (int i = 0; i < 7; i++) raw[i] = _wire->read();

    if (raw[0] & 0x80) {
      Serial.println("[RTC] OS flag set — clearing");
      writeReg(PCF8563_REG_SECONDS, raw[0] & 0x7F);
      return 0;
    }

    int second = bcd2dec(raw[0] & 0x7F);
    int minute = bcd2dec(raw[1] & 0x7F);
    int hour   = bcd2dec(raw[2] & 0x3F);
    int day    = bcd2dec(raw[3] & 0x3F);
    int month  = bcd2dec(raw[5] & 0x1F);
    int year   = 2000 + bcd2dec(raw[6]);

    if (month<1 || month>12 || day<1 || day>31 || hour>23 || minute>59 || second>59)
      return 0;

    return toEpoch(year, month, day, hour, minute, second);
  }

  // ---- Write time (burst to 0x02) ----
  void writeHardwareTime(uint32_t epoch) {
    int year, month, day, hour, minute, second;
    fromEpoch(epoch, year, month, day, hour, minute, second);

    static const int dow[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    int y = year; if (month < 3) y--;
    int wday = (y + y/4 - y/100 + y/400 + dow[month-1] + day) % 7;
    int yr = year - 2000;

    // Stop clock
    writeReg(0x00, 0x20);
    delay(5);

    // Burst write
    _wire->beginTransmission(PCF8563_ADDR);
    _wire->write(PCF8563_REG_SECONDS);
    _wire->write(dec2bcd(second) & 0x7F);
    _wire->write(dec2bcd(minute));
    _wire->write(dec2bcd(hour));
    _wire->write(dec2bcd(day));
    _wire->write(dec2bcd(wday));
    _wire->write(dec2bcd(month));
    _wire->write(dec2bcd(yr));
    _wire->endTransmission();
    delay(5);

    // Restart clock
    writeReg(0x00, 0x00);

    Serial.printf("[RTC] Wrote %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, day, hour, minute, second);
  }
};