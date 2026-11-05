#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <helpers/UTF8Helpers.h>
#include "../LoRaGotchiVersion.h"

// Display-only fallback for the small built-in ASCII fonts. Keep raw names in
// contacts/adverts; protocolName sanitises only the outgoing fish label.
namespace LoRaFishText {

inline uint32_t next(const char* text, size_t capacity, size_t& pos) {
  const uint8_t first = (uint8_t)text[pos];
  size_t length = first < 0x80 ? 1 : first >= 0xC2 && first <= 0xDF ? 2 :
      first >= 0xE0 && first <= 0xEF ? 3 : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
  if (!length || length > capacity - pos) { ++pos; return 0xFFFD; }
  // Bound and terminate the single code point before using MeshCore's validator.
  char sequence[5] = {};
  for (size_t i = 0; i < length; ++i) {
    if (!text[pos + i]) { ++pos; return 0xFFFD; }
    sequence[i] = text[pos + i];
  }
  if (mesh::validUtf8PrefixLength(sequence, length) != length) {
    ++pos;
    return 0xFFFD;
  }
  uint32_t code = first & (length == 1 ? 0x7F : length == 2 ? 0x1F : length == 3 ? 0x0F : 0x07);
  for (size_t i = 1; i < length; ++i) code = (code << 6) | ((uint8_t)sequence[i] & 0x3F);
  pos += length;
  return code;
}

inline bool isFish(uint32_t code) {
  // Preserve the four legacy fish glyphs and their exact ASCII fallback.
  return code == 0x1F41F || code == 0x1F420 || code == 0x1F421 || code == 0x1F988;
}

inline bool isAirMarker(uint32_t code) {
#if LORAGOTCHI_AIR_EDITION
  switch(code) {
    case 0x1F985: case 0x1F426: case 0x1F989: case 0x1F986: case 0x1F9A2:
    case 0x1F427: case 0x1F99C: case 0x1F9A9: case 0x1F99A: case 0x1F983:
    case 0x1F413: case 0x1F54A: case 0x1F987: case 0x1F41D: case 0x1F98B:
    case 0x1FAB0: case 0x1FAB1: case 0x1F41B:return true;
    default:return false;
  }
#else
  (void)code;return false;
#endif
}
inline bool isWaterMarker(uint32_t code) {
  if (isFish(code)) return true;
#if LORAGOTCHI_SPECIES_EDITION
  // Exact supported animals plus the owner's explicit Water marker, not all
  // water-related Unicode. No generic waves, droplets, stars or boating signs.
  switch(code) {
    case 0x1F42C: // dolphin
    case 0x1F433: case 0x1F40B: // spouting whale / whale
    case 0x1F9AD: // seal
    case 0x1F990: case 0x1F991: // shrimp / squid
    case 0x1F99E: case 0x1F980: // lobster / crab
    case 0x1F419: case 0x1FABC: // octopus / jellyfish
    case 0x1F41A: // shell-dweller
    case 0x1F4A6: // explicit Water marker: sweat droplets
      return true;
    default: return false;
  }
#else
  return false;
#endif
}
// Historical API name kept for callers. Now tests the exact Water allowlist.
inline bool hasFish(const char* text, size_t capacity) {
  if (!text) return false;
  for (size_t pos = 0; pos < capacity && text[pos]; ) {
    const uint32_t code=next(text,capacity,pos);
    if (isWaterMarker(code) || isAirMarker(code)) return true;
  }
  return false;
}

inline void display(char* dest, size_t size, const char* text, size_t capacity) {
  if (!dest || !size) return;
  size_t out = 0;
  if (text) for (size_t pos = 0; pos < capacity && text[pos]; ) {
    const uint32_t code = next(text, capacity, pos);
    // Presentation selectors, joiners and emoji modifiers have no separate glyph.
    if (code == 0x200D || code == 0xFE0E || code == 0xFE0F ||
        (code >= 0x1F3FB && code <= 0x1F3FF) || (code >= 0xE0020 && code <= 0xE007F)) continue;
    if (isWaterMarker(code) || isAirMarker(code)) {
      if (out + 3 >= size) break;
      memcpy(dest + out, isFish(code) ? "><>" : isAirMarker(code) ? "[A]" : "[W]", 3);
      out += 3;
    } else {
      if (out + 1 >= size) break;
      dest[out++] = code >= 32 && code <= 126 ? (char)code : ' ';
    }
  }
  dest[out] = 0;
}

// Discovery always examines the original contact name, never the wire fish name.
inline bool hasEgg(const char* text, size_t capacity) {
  if (!text) return false;
  for (size_t pos=0; pos<capacity && text[pos]; )
    if (next(text,capacity,pos)==0x1F95A) return true;
  return false;
}

inline bool emoji(uint32_t c) {
  return (c>=0x1F000 && c<=0x1FAFF) || (c>=0x2600 && c<=0x27BF) ||
      (c>=0x2300 && c<=0x23FF) || (c>=0x2B00 && c<=0x2BFF) ||
      (c>=0x2190 && c<=0x21FF) || c==0xA9 || c==0xAE || c==0x203C ||
      c==0x2049 || c==0x2122 || c==0x2139 || c==0x3030 || c==0x303D ||
      c==0x3297 || c==0x3299 || c==0x24C2 || c==0x25AA || c==0x25AB ||
      c==0x25B6 || c==0x25C0 || (c>=0x25FB && c<=0x25FE) || c==0x2934 || c==0x2935;
}

// Bounded UTF-8 fish label, not a mutation of the MeshCore node name.
inline void protocolName(char* dest, size_t size, const char* text, size_t capacity) {
  if (!dest || !size) return;
  size_t out=0; bool space=false;
  if (text) for (size_t pos=0; pos<capacity && text[pos]; ) {
    const size_t start=pos;
    const uint32_t c=next(text,capacity,pos);
    if ((c>='0' && c<='9') || c=='#' || c=='*') {
      size_t peek=pos;
      uint32_t following=peek<capacity && text[peek] ? next(text,capacity,peek) : 0;
      if (following==0xFE0F && peek<capacity && text[peek]) following=next(text,capacity,peek);
      if (following==0x20E3) { pos=peek; continue; }
    }
    if (emoji(c) || c==0xFFFD || c==0x200D || c==0x200C || c==0x20E3 ||
        (c>=0xFE00 && c<=0xFE0F) || (c>=0xE0000 && c<=0xE01EF) ||
        (c>=0x200B && c<=0x200F) || (c>=0x202A && c<=0x202E) ||
        (c>=0x2060 && c<=0x206F) || c==0xFEFF) continue;
    if (c<=32 || (c>=0x7F && c<=0x9F) || c=='|' || c==0xA0 ||
        (c>=0x2000 && c<=0x200A) || c==0x2028 || c==0x2029 || c==0x3000) {
      space=out>0; continue;
    }
    const size_t length=pos-start;
    if (out+length+(space?1:0)>=size) break;
    if (space) dest[out++]=' ';
    memcpy(dest+out,text+start,length);out+=length;space=false;
  }
  dest[out]=0;
  if (!out) {
    const char fallback[]="DX Fish";
    const size_t count=size-1<sizeof(fallback)-1 ? size-1 : sizeof(fallback)-1;
    memcpy(dest,fallback,count);dest[count]=0;
  }
}

// Preserve valid raw UTF-8 in the existing bounded DX name field, without
// cutting a multibyte character at the end of the wire-format name limit.
inline void rawPrefix(char* dest, size_t size, const char* text, size_t capacity) {
  if (!dest || !size) return;
  size_t copied = 0;
  if (text) for (size_t pos = 0; pos < capacity && text[pos]; ) {
    const size_t start = pos;
    if (next(text, capacity, pos) == 0xFFFD || pos >= size) break;
    memcpy(dest + start, text + start, pos - start);
    copied = pos;
  }
  dest[copied] = 0;
}

}  // namespace LoRaFishText
