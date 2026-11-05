#pragma once
#include <stdint.h>
#include <string.h>
// Bounded host-initiated return tracking. RAM only, matching regular DX's
// existing reboot-repatriates-pets policy. RX has its separate durable journal.
struct LoRaFishGuestReturn {
  enum Status : uint8_t { Idle, Pending, Confirmed, Failed };
  static constexpr uint8_t maxAttempts = 4;
  static constexpr uint32_t retryMs = 10000;
  Status status = Idle;
  uint8_t attempts = 0;
  uint32_t due = 0, hashes[maxAttempts] = {};
  bool queued[maxAttempts] = {};

  void start(uint32_t now) {
    status = Pending;
    attempts = 0;
    due = now;
    memset(hashes, 0, sizeof(hashes));
    memset(queued, 0, sizeof(queued));
  }
  bool ready(uint32_t now) {
    if (status != Pending || int32_t(now - due) < 0) return false;
    if (attempts >= maxAttempts) {
      status = Failed;
      return false;
    }
    return true;
  }
  void sent(uint32_t hash, uint32_t now, uint32_t timeout, bool success) {
    if (status != Pending || attempts >= maxAttempts) return;
    hashes[attempts] = hash;
    queued[attempts++] = success;
    // Allow the radio's estimated direct/flood round trip before retrying.
    // Keep signed deadline arithmetic valid even with a corrupt estimate.
    if (!success || timeout < retryMs) timeout = retryMs;
    if (timeout > 0x7FFFFFFFUL) timeout = 0x7FFFFFFFUL;
    due = now + timeout;
  }
  bool acknowledge(uint32_t hash) {
    if (status != Pending && status != Failed) return false;
    for (uint8_t i = 0; i < attempts; ++i) {
      if (queued[i] && hashes[i] == hash) {
        status = Confirmed;
        return true;
      }
    }
    return false;
  }
};
