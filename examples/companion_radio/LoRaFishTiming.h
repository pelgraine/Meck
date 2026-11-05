#ifndef LORAFISH_TIMING_H
#define LORAFISH_TIMING_H
#include <stdint.h>

// Kept identical in DX and RX. LFG1 opcodes and stored records are unchanged.
namespace LoRaFishTiming {
static constexpr uint32_t phaseMs = 120000;
static constexpr uint32_t reserveMs = 300000;
static constexpr uint8_t maxAttempts = 4;
inline bool reached(uint32_t now, uint32_t due) { return int32_t(now - due) >= 0; }

inline uint32_t retryMs(uint32_t estimate, uint8_t attempts, uint32_t random) {
  // Backoff has a 60s floor cap, NOT a cap on the radio's timeout estimate.
  uint32_t backoff = 10000UL << (attempts > 3 ? 3 : (attempts ? attempts - 1 : 0));
  if (backoff > 60000) backoff = 60000;
  // A transport ACK is shorter than an application reply. Include allowance
  // for that reply and queueing; no promise about unbounded congestion.
  uint64_t route = uint64_t(estimate) * 2 + 2000;
  uint64_t base = route > backoff ? route : backoff;
  uint64_t delay = base + random % (base / 4 + 1);
  return delay > 0x7fffffffUL ? 0x7fffffffUL : uint32_t(delay);
}
}

#endif
