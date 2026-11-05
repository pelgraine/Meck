#pragma once

#include <stdint.h>

// Gameplay timing only: no display, transport, RTC ownership, or flash writes.
struct LoRaFishHappiness {
  static constexpr uint32_t decayMs = 30UL * 60UL * 1000UL;
  static constexpr uint32_t mealMs = 60UL * 60UL * 1000UL;
  static constexpr uint32_t hourMs = 60UL * 60UL * 1000UL;
  static constexpr uint32_t dayMs = 24UL * hourMs;
  static constexpr uint32_t aquariumMs = 6UL * hourMs;
  uint32_t awakeMs = 0;
  uint32_t mealCooldownMs = 0;
  uint32_t aquariumCooldownMs = 0;

  void resetCare() {
    awakeMs = 0;
    mealCooldownMs = 0;
    // Per-device community reward cap survives a new fish/generation.
  }

  static bool sleeping(uint8_t hour, uint8_t sleepHour, uint8_t wakeHour) {
    if (sleepHour == wakeHour) return false;
    return sleepHour > wakeHour ? hour >= sleepHour || hour < wakeHour
                                : hour >= sleepHour && hour < wakeHour;
  }

  bool valid() const {
    return awakeMs < decayMs && mealCooldownMs <= mealMs && aquariumCooldownMs <= aquariumMs;
  }
  bool same(const LoRaFishHappiness& other) const {
    return awakeMs == other.awakeMs && mealCooldownMs == other.mealCooldownMs &&
        aquariumCooldownMs == other.aquariumCooldownMs;
  }

  // Call before lifecycle age changes. Split delayed updates at local-hour
  // boundaries so sleep/wake transitions do not classify the entire gap as now.
  void advance(uint32_t elapsed, bool living, uint32_t localEndMs,
               uint8_t sleepHour, uint8_t wakeHour, uint8_t waste, uint8_t& happy) {
    mealCooldownMs = elapsed >= mealCooldownMs ? 0 : mealCooldownMs - elapsed;
    aquariumCooldownMs = elapsed >= aquariumCooldownMs ? 0 : aquariumCooldownMs - elapsed;
    if (!living) return;
    uint32_t local = (localEndMs + dayMs - elapsed % dayMs) % dayMs;
    while (elapsed) {
      uint32_t step = hourMs - local % hourMs;
      if (step > elapsed) step = elapsed;
      if (!sleeping(local / hourMs, sleepHour, wakeHour)) {
        awakeMs += step;
        while (awakeMs >= decayMs) {
          awakeMs -= decayMs;
          const uint8_t drain = 1 + (waste > 3 ? 3 : waste);
          happy = happy > drain ? happy - drain : 0;
        }
      }
      elapsed -= step;
      local = (local + step) % dayMs;
    }
  }

  bool meal(bool eligible, uint8_t& happy) {
    if (!eligible || mealCooldownMs) return false;
    mealCooldownMs = mealMs;
    happy = happy <= 97 ? happy + 3 : 100;
    return true;
  }

  bool aquarium(bool eligible, uint8_t& happy) {
    if (!eligible || aquariumCooldownMs) return false;
    aquariumCooldownMs = aquariumMs;
    happy = happy <= 95 ? happy + 5 : 100;
    return true;
  }
};
