#pragma once
#include "LoRaFishColour.h"

// LEG1 is independent of visit custody. Names are discovery hints, never identity.
class LoRaFishEgg {
public:
  enum Phase : uint8_t { Closed, Probe, Choose, Offered };
  Phase phase=Closed;
  uint8_t peer[32]={};
  uint32_t nonce[2]={}, egg=0, started=0, nextSend=0;
  uint16_t generation=1;
  uint8_t body=6, scales=6, eyes=6, attempts=0;
  void close() { phase=Closed; }
  void begin(const uint8_t* key, uint32_t a, uint32_t b, uint32_t now) {
    *this=LoRaFishEgg{}; memcpy(peer,key,32);nonce[0]=a;nonce[1]=b;
    started=nextSend=now;phase=Probe;
  }
  bool expired(uint32_t now) const { return now-started>=120000UL; }
  bool due(uint32_t now) const {
    return phase==Probe && attempts<4 && (int32_t)(now-nextSend)>=0 && !expired(now);
  }
  void attempted(uint32_t now, uint32_t estimate) {
    ++attempts;
    nextSend=now+(estimate<5000 ? 5000 : estimate>60000 ? 60000 : estimate);
  }
  void hello(char* out,size_t size) const {
    snprintf(out,size,"LEG1|HELLO|%lu|%lu|H1",(unsigned long)nonce[0],(unsigned long)nonce[1]);
  }
  void ack(char* out,size_t size) const {
    snprintf(out,size,"LEG1|SAVED|%lu|%lu|%lu",(unsigned long)nonce[0],
             (unsigned long)nonce[1],(unsigned long)egg);
  }
  bool receive(const uint8_t* key,const char* text,uint32_t now) {
    if (phase==Closed || expired(now) || memcmp(peer,key,32)) return false;
    const bool ready=!strncmp(text,"LEG1|READY|",11);
    if (!ready && strncmp(text,"LEG1|EGG|",9)) return false;
    const char* p=text+(ready?11:9);uint32_t a,b,id,gen,bc,sc,ec;
    if (!LoRaFishColour::number(p,a,UINT32_MAX,'|') ||
        !LoRaFishColour::number(p,b,UINT32_MAX,'|') || a!=nonce[0] || b!=nonce[1]) return false;
    if (ready) {
      if (strcmp(p,"H1") || phase!=Probe) return false;
      phase=Choose;return true;
    }
    if (phase!=Choose || !LoRaFishColour::number(p,id,UINT32_MAX,'|') || !id ||
        !LoRaFishColour::number(p,gen,65535,'|') || !gen || strncmp(p,"H1|",3)) return false;
    p+=3;
    if (!LoRaFishColour::number(p,bc,247,'|') || !LoRaFishColour::number(p,sc,247,'|') ||
        !LoRaFishColour::number(p,ec,247,0) || !LoRaFishColour::valid(bc) ||
        !LoRaFishColour::valid(sc) || !LoRaFishColour::valid(ec)) return false;
    egg=id;generation=gen;body=bc;scales=sc;eyes=ec;phase=Offered;return true;
  }
};

// Alternating receipt journal. A pending record is a recoverable adoption intent;
// a complete one is the only authority for SAVED. Serialised layout, no ABI padding.
struct LoRaFishEggReceipt {
  uint8_t bytes[64]={};
  static uint32_t get(const uint8_t* p) {
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
  }
  static void put(uint8_t* p,uint32_t v) { for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i)); }
  uint32_t sequence() const { return get(bytes+4); }
  uint32_t hash() const {
    uint32_t h=2166136261UL;for(unsigned i=0;i<60;++i)h=(h^bytes[i])*16777619UL;return h;
  }
  bool valid() const {
    return !memcmp(bytes,"EGJ1",4) && (bytes[8]==1 || bytes[8]==2) &&
      get(bytes+60)==hash() && get(bytes+49)!=0 && (bytes[53] || bytes[54]) &&
      LoRaFishColour::valid(bytes[55]) && LoRaFishColour::valid(bytes[56]) && LoRaFishColour::valid(bytes[57]);
  }
  void seal() { put(bytes+60,hash()); }
  void capture(const LoRaFishEgg& e,uint32_t seq,bool complete) {
    memset(bytes,0,sizeof(bytes));memcpy(bytes,"EGJ1",4);put(bytes+4,seq);bytes[8]=complete?2:1;
    memcpy(bytes+9,e.peer,32);put(bytes+41,e.nonce[0]);put(bytes+45,e.nonce[1]);put(bytes+49,e.egg);
    bytes[53]=e.generation;bytes[54]=e.generation>>8;bytes[55]=e.body;bytes[56]=e.scales;bytes[57]=e.eyes;seal();
  }
  LoRaFishEgg restore() const {
    LoRaFishEgg e;memcpy(e.peer,bytes+9,32);e.nonce[0]=get(bytes+41);e.nonce[1]=get(bytes+45);
    e.egg=get(bytes+49);e.generation=(uint16_t)bytes[53]|(uint16_t)bytes[54]<<8;
    e.body=bytes[55];e.scales=bytes[56];e.eyes=bytes[57];e.phase=LoRaFishEgg::Offered;return e;
  }
  bool replay(const uint8_t* key,const char* text) const {
    if (!valid() || bytes[8]!=2) return false;
    LoRaFishEgg e=restore();e.phase=LoRaFishEgg::Choose;
    if (!e.receive(key,text,0)) return false;
    return e.egg==get(bytes+49) && e.generation==((unsigned)bytes[53]|(unsigned)bytes[54]<<8) &&
        e.body==bytes[55] && e.scales==bytes[56] && e.eyes==bytes[57];
  }
};
