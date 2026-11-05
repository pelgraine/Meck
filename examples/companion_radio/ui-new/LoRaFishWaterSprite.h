#pragma once
#include "../LoRaFishSpecies.h"
#include "LoRaFishWaterArt.h"

// Palette resolved at runtime by the same genetics/theme converter as legacy fish.
// No framebuffer, heap or per-species RGB assets; max footprint is 32x24 pixels.
namespace LoRaFishWaterSprite {
template<class Display, class Colour>
inline void draw(Display& d,int x,int y,bool right,uint16_t id,uint8_t stage,
                 uint32_t now,Colour body,Colour fins,Colour eyes,Colour background) {
  if (!id || !LoRaFishSpecies::waterKnown(id)) return; // caller preserves legacy fish
  const unsigned phase=(now/280U)&1U;
  const unsigned width=stage==0?20:stage==1?26:32;
  const unsigned height=stage==0?15:stage==1?19:24;
  const uint8_t* mask=LoRaFishWaterArt::masks[id-1][phase];
  const Colour palette[]={background,body,fins,eyes};
  // Coalesce equal semantic pixels into horizontal runs. Transparent pixels
  // never erase other pets; no extra framebuffer or colour-specific art needed.
  for(unsigned dy=0;dy<height;++dy) {
    const unsigned sy=dy*24/height;
    auto roleAt=[&](unsigned dx) -> uint8_t {
      const unsigned bit=sy*32+dx*32/width;
      return (mask[bit/4]>>((bit%4)*2))&3;
    };
    for(unsigned dx=0;dx<width;) {
      const unsigned start=dx;const uint8_t role=roleAt(dx++);
      while(dx<width && roleAt(dx)==role)++dx;
      if(!role)continue;
      const int left=right ? (int)start-(int)width/2 : (int)width/2-(int)dx+1;
      d.setColor(palette[role]);
      d.fillRect(x+left,y+(int)dy-(int)height/2,(int)(dx-start),1);
    }
  }
}
}
