#pragma once
#include "LoRaFishVisit.h"

// One RAM-only confirmation outbox, bound to the current visitor. A transport
// ACK stops ARRIVED retries; it does not remove the guest or certify home storage.
struct LoRaFishArrival {
  LoRaFishVisit retry;
  bool active = false, confirmed = false;
  uint8_t ownerKey[32] = {};
  uint32_t txid = 0, fishid = 0;
  uint32_t hashes[LoRaFishTiming::maxAttempts] = {};

  bool matches(const uint8_t* owner, uint32_t tx, uint32_t fish) const {
    return active && txid == tx && fishid == fish && !memcmp(ownerKey, owner, 32);
  }
  void start(const uint8_t* owner, uint32_t tx, uint32_t fish, uint32_t now) {
    *this = LoRaFishArrival{};
    active = true;
    memcpy(ownerKey, owner, 32);
    txid = tx;
    fishid = fish;
    retry.begin(LoRaFishVisit::Commit, now);
  }
  void sent(uint32_t hash, uint32_t now, uint32_t estimate, uint32_t random, bool queued) {
    if (queued && retry.attempts < LoRaFishTiming::maxAttempts) hashes[retry.attempts] = hash;
    retry.sent(now, estimate, random, queued);
  }
  bool acknowledge(uint32_t hash) {
    if (!active || confirmed) return false;
    for (uint8_t i = 0; i < retry.attempts; ++i) {
      if (hashes[i] == hash) {
        confirmed = true;
        retry.stop();
        return true;
      }
    }
    return false;
  }
};
