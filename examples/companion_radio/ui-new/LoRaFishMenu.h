#pragma once

#include <stdint.h>

// Stable identities keep return targets correct when optional rows appear/disappear.
enum class LoRaFishOption : uint8_t {
  Advert, Guess, Breed, Reset, Display, Sound, Schedule, RF, GNSS, ReturnGuest, Count
};

class LoRaFishOptions {
public:
  static bool available(LoRaFishOption option, bool sound, bool breed, bool guest=false) {
    return option < LoRaFishOption::Count &&
        (option != LoRaFishOption::Sound || sound) &&
        (option != LoRaFishOption::Breed || breed) &&
        (option != LoRaFishOption::ReturnGuest || guest);
  }
  static uint8_t count(bool sound, bool breed, bool guest=false) { return 7 + sound + breed + guest; }
  static LoRaFishOption at(uint8_t row, bool sound, bool breed, bool guest=false) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(LoRaFishOption::Count); ++i) {
      const auto option = static_cast<LoRaFishOption>(i);
      if (available(option, sound, breed, guest) && row-- == 0) return option;
    }
    return LoRaFishOption::Advert;
  }
  static uint8_t index(LoRaFishOption option, bool sound, bool breed, bool guest=false) {
    for (uint8_t row = 0; row < count(sound, breed, guest); ++row) {
      if (at(row, sound, breed, guest) == option) return row;
    }
    return 0;
  }
};

enum class LoRaFishSetting : uint8_t {
  Theme, Brightness, Rendering, Scales, Body, Eyes, SleepHour, WakeHour, UtcOffset,
  TimeoutAwake, TimeoutAsleep, DimAfter, Volume, WakeSound, SleepSound, MessageSound, MonoDisplayColour, Species
};

// Browsing is a draft: only Select produces a value to persist. Back cancels it.
class LoRaFishValuePicker {
  bool editing = false;
  LoRaFishSetting setting = LoRaFishSetting::Theme;
  uint8_t candidate = 0;
  uint8_t valueCount = 0;
public:
  void open(LoRaFishSetting key, uint8_t value, uint8_t count) {
    setting = key;
    valueCount = count;
    candidate = count ? value % count : 0;
    editing = count != 0;
  }
  bool active() const { return editing; }
  LoRaFishSetting key() const { return setting; }
  uint8_t value() const { return candidate; }
  uint8_t count() const { return valueCount; }
  void move(bool previous = false) {
    if (editing) candidate = (candidate + (previous ? valueCount - 1 : 1)) % valueCount;
  }
  void cancel() { editing = false; }
  bool select(uint8_t& value) {
    if (!editing) return false;
    value = candidate;
    editing = false;
    return true;
  }
};
