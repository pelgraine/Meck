#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "LoRaFishColour.h"
#include "LoRaFishSpecies.h"

// Session-only copy of the first COMMIT. Every retry must be self-contained:
// the first packet may be lost, and the receiver cannot invent missing genes.
class LoRaFishCommitFrame {
  uint8_t destination[32] = {};
  uint32_t transaction = 0, fish = 0;
  char payload[128] = {};
public:
  void prepare(const uint8_t* key, uint32_t tx, uint32_t fid, bool colour,
               uint8_t body, uint8_t scales, uint8_t eyes, uint16_t tint, uint8_t monoPanel = 0,
               uint16_t species = 0, bool extensions = true) {
    memcpy(destination, key, sizeof(destination));
    transaction = tx;
    fish = fid;
    snprintf(payload, sizeof(payload), "LFG1|COMMIT|%lu|%lu|%c|%u|%u|%u|%u",
             (unsigned long)tx, (unsigned long)fid, colour ? 'C' : 'M',
             (unsigned)LoRaFishColour::fallback(body), (unsigned)LoRaFishColour::fallback(scales),
             (unsigned)LoRaFishColour::fallback(eyes), (unsigned)tint);
    if (!extensions) return; // strict legacy RX expects exactly nine fields
    if (body > 6 || scales > 6 || eyes > 6) {
      const size_t used = strlen(payload);
      snprintf(payload + used, sizeof(payload) - used, "|H1|%u|%u|%u", body, scales, eyes);
    }
    const size_t used = strlen(payload);
    snprintf(payload + used, sizeof(payload) - used, "|P1|%u", (unsigned)(monoPanel < 3 ? monoPanel : 0));
    const size_t speciesOffset = strlen(payload);
    snprintf(payload + speciesOffset, sizeof(payload) - speciesOffset, "|S1|%u", (unsigned)species);
  }

  const char* forVisit(const uint8_t* key, uint32_t tx, uint32_t fid) const {
    return payload[0] && transaction == tx && fish == fid &&
        !memcmp(destination, key, sizeof(destination)) ? payload : nullptr;
  }
};
