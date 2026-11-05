#pragma once
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// H1 alleles: original palette 0..5, Mono 6, 240 hue steps 8..247.
// 7 remains a UI-only guest tint; 255 remains a UI-only white flash.
namespace LoRaFishColour {
inline bool valid(unsigned value) { return value <= 6 || (value >= 8 && value <= 247); }
inline uint8_t hue(uint8_t value) {
  static const uint8_t palette[] = {235, 20, 40, 80, 120, 200};
  return value < 6 ? palette[value] : value >= 8 && value <= 247 ? value - 8 : 0;
}
inline uint8_t fallback(uint8_t value) {
  if (value <= 6) return value;
  unsigned best = 241; uint8_t selected = 0;
  for (uint8_t i = 0; i < 6; ++i) {
    int distance = (int)hue(value) - hue(i);
    if (distance < 0) distance = -distance;
    if (distance > 120) distance = 240 - distance;
    if ((unsigned)distance < best) { best = distance; selected = i; }
  }
  return selected;
}
inline uint16_t rgb565(uint8_t value) {
  static const uint16_t palette[] = {0xF986, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0xF81F};
  if (value < 6) return palette[value];
  const unsigned h = hue(value), sector = h / 40, ramp = (h % 40) * 255 / 40;
  unsigned r = 0, g = 0, b = 0;
  switch (sector) {
    case 0: r=255; g=ramp; break;
    case 1: r=255-ramp; g=255; break;
    case 2: g=255; b=ramp; break;
    case 3: g=255-ramp; b=255; break;
    case 4: r=ramp; b=255; break;
    default: r=255; b=255-ramp; break;
  }
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}
inline uint8_t inherit(uint8_t a, uint8_t b, unsigned sample) {
  sample %= 241;
  // Mono is not a hue. A mixed pair chooses one parent; solo/identical stays exact.
  if (a == b) return a;
  if (a == 6 || b == 6) return sample < 121 ? a : b;
  if (!sample) return a;
  if (sample == 240) return b;
  int delta = (int)hue(b) - hue(a);
  if (delta > 120) delta -= 240;
  if (delta < -120) delta += 240;
  return (uint8_t)(8 + (hue(a) + delta * (int)sample / 240 + 240) % 240);
}
// Strict decimal parser with MCU-width-independent overflow checks.
inline bool number(const char*& p, uint32_t& result, uint32_t maximum, char end) {
  result = 0;
  if (*p < '0' || *p > '9') return false;
  do {
    unsigned digit = (unsigned)(*p++ - '0');
    if (digit > maximum || result > (maximum - digit) / 10) return false;
    result = result * 10 + digit;
  } while (*p >= '0' && *p <= '9');
  if (*p != end) return false;
  if (end) ++p;
  return true;
}
inline bool extension(const char* p, uint8_t& body, uint8_t& scales, uint8_t& eyes) {
  if (!p || strncmp(p, "|H1|", 4)) return false;
  p += 4;
  uint32_t b, s, e;
  if (!number(p,b,247,'|') || !number(p,s,247,'|') || !number(p,e,247,strchr(p,'|') ? '|' : 0) ||
      !valid(b) || !valid(s) || !valid(e)) return false;
  bool panelSeen=false,speciesSeen=false;
  while (*p) {
    uint32_t ignored=0,maximum=0;
    if (!strncmp(p,"P1|",3) && !panelSeen) {panelSeen=true;maximum=2;}
    else if (!strncmp(p,"S1|",3) && !speciesSeen) {speciesSeen=true;maximum=65535;}
    else return false;
    p+=3;
    if (!number(p,ignored,maximum,strchr(p,'|') ? '|' : 0)) return false;
  }
  body=b; scales=s; eyes=e; return true;
}
}
