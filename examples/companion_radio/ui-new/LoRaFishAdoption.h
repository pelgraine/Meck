#pragma once
// Included after the pet serializers in LoRaFish.cpp: all storage stays in the UI
// owner; the radio callback can stage a packet but never writes flash itself.
#include "../LoRaFishEgg.h"
static LoRaFishEgg loRaFishEggSession;
static LoRaFishEggReceipt loRaFishEggReceipt;
static int loRaFishEggBank=-1;
static bool loRaFishEggStorageHealthy=true, loRaFishEggWindowStarted=false;
static bool loRaFishEggApplying=false, loRaFishEggSaveScheduled=false;
static uint32_t loRaFishEggWindowMs=0, loRaFishEggSaveDue=0;
static char loRaFishEggTankName[24]={};

static bool readLoRaFishRecord(const char* path,uint8_t* data,size_t size,bool* exists=nullptr) {
  File f=SPIFFS.open(path,FILE_READ);
  if(exists) {
    *exists=SPIFFS.exists(path);
  }
  if (!f) return false;
  bool ok=f.size()==size && f.read(data,size)==size;f.close();return ok;
}
static const char* loRaFishEggPath(int bank) { return bank ? "/lfegg1.dat" : "/lfegg0.dat"; }
static bool writeLoRaFishEggReceipt(LoRaFishEggReceipt next) {
  const int bank=loRaFishEggBank==0?1:0;
  const char* path=loRaFishEggPath(bank);
  if (!writeLoRaFishRecord(path,next.bytes,sizeof(next.bytes))) return false;
  LoRaFishEggReceipt check;
  if (!readLoRaFishRecord(path,check.bytes,sizeof(check.bytes)) || !check.valid() ||
      memcmp(check.bytes,next.bytes,sizeof(check.bytes))) return false;
  loRaFishEggReceipt=check;loRaFishEggBank=bank;return true;
}
static void loadLoRaFishEggReceipt() {
  LoRaFishEggReceipt a,b; bool ea=false,eb=false;
  bool va=readLoRaFishRecord(loRaFishEggPath(0),a.bytes,sizeof(a.bytes),&ea) && a.valid();
  bool vb=readLoRaFishRecord(loRaFishEggPath(1),b.bytes,sizeof(b.bytes),&eb) && b.valid();
  loRaFishEggStorageHealthy=va || vb || (!ea && !eb);
  if (va || vb) {
    loRaFishEggBank=vb && (!va || (int32_t)(b.sequence()-a.sequence())>0) ? 1 : 0;
    loRaFishEggReceipt=loRaFishEggBank ? b : a;
    if (loRaFishEggReceipt.bytes[8]==1) {
      loRaFishEggSession=loRaFishEggReceipt.restore();loRaFishEggApplying=true;
      loRaFishAdoptionEligible=true;
    }
  }
}
static void closeLoRaFishAdoption() {
  if (loRaFishEggApplying) return; // Accepted durable intent must finish/recover.
  loRaFishEggSession.close();loRaFishAdoptionEligible=false;loRaFishEggWindowStarted=false;
  loRaFishEggSaveScheduled=false;
  loRaFishPage=loRaFishNeedsPetSetup?LORAFISH_PET_SETUP_PAGE:(loRaFishNeedsAppearanceSetup?9:0);fishLastMinute=millis();
}
static bool selectLoRaFishEggTank(bool next) {
  const int count=the_mesh.getNumContacts();int start=0;
  if (next && loRaFishEggSession.phase!=LoRaFishEgg::Closed) {
    for(int i=0;i<count;++i) {
      ContactInfo c;
      if(the_mesh.getContactByIdx(i,c) && !memcmp(c.id.pub_key,loRaFishEggSession.peer,32)) {
        start=i+1;break;
      }
    }
  }
  // Explicit selection invalidates the previous nonce, including a removed peer.
  loRaFishEggSession.close();
  for(int i=0;i<count;++i) {
    ContactInfo c;
    if (the_mesh.getContactByIdx((start+i)%count,c) &&
        c.type==ADV_TYPE_CHAT && LoRaFishText::hasEgg(c.name,sizeof(c.name))) {
      loRaFishEggSession.begin(c.id.pub_key,the_mesh.loRaFishRandom(),the_mesh.loRaFishRandom(),millis());
      LoRaFishText::display(loRaFishEggTankName,sizeof(loRaFishEggTankName),c.name,sizeof(c.name));
      return true;
    }
  }
  return false;
}
// Meck: inside namespace lfx; the global loragotchiEggMessage() in LoRaFish.cpp
// calls this for MyMesh.
static bool loRaFishEggHandleMessage(const uint8_t* key,const char* text,char* reply,size_t size) {
  if (reply && size) reply[0]=0;
  if (loRaFishEggReceipt.replay(key,text)) {
    loRaFishEggReceipt.restore().ack(reply,size);return true;
  }
  if (!loRaFishAdoptionEligible || !loRaFishEggStorageHealthy || loRaFishEggApplying ||
      !loRaFishEggWindowStarted || millis()-loRaFishEggWindowMs>=120000UL) return false;
  return loRaFishEggSession.receive(key,text,millis());
}
static void serviceLoRaFishAdoption(bool visible) {
  const uint32_t now=millis();
  if (!loRaFishAdoptionEligible) return;
  if (loRaFishEggApplying || loRaFishEggSession.phase==LoRaFishEgg::Offered) {
    if (loRaFishEggSaveScheduled && (int32_t)(now-loRaFishEggSaveDue)<0) return;
    loRaFishEggSaveScheduled=true;loRaFishEggSaveDue=now+5000;
    if (!loRaFishEggApplying) {
      LoRaFishEggReceipt intent;
      intent.capture(loRaFishEggSession,loRaFishEggReceipt.sequence()+1,false);
      if(!writeLoRaFishEggReceipt(intent)) return;
      loRaFishEggApplying=true;
    }
    const LoRaFishEgg accepted=loRaFishEggReceipt.restore();
    resetLoRaFishPet();
    loRaFishGeneration=accepted.generation;loRaFishColor=accepted.body;
    loRaFishScaleColor=accepted.scales;loRaFishEyeColor=accepted.eyes;
    // LEG1/H1 eggs predate species: explicit tropical fish, never mystery.
    loRaFishSpecies=loRaFishEggSpecies=LoRaFishSpecies::TropicalFish;
    loRaFishMysterySeed=0;loRaFishNeedsPetSetup=false;
    loRaFishNeedsAppearanceSetup=false;loRaFishSyncDXAppearance();
    loRaFishSettingsLastAttempt=loRaFishLastAttempt=now-LORAFISH_SAVE_RETRY_MS;
    saveLoRaFishSettings();saveLoRaFish();
    uint8_t cfg[LORAFISH_SETTINGS_SIZE],stored[LORAFISH_SETTINGS_SIZE];
    LoRaFishState pet;
    encodeLoRaFishSettings(cfg);
    if (loRaFishSettingsSaveFailed || loRaFishSaveFailed ||
        !readLoRaFishRecord("/lorafish.cfg",stored,sizeof(stored)) || memcmp(cfg,stored,sizeof(cfg)) ||
        !readLoRaFishRecord("/lorafish.dat",(uint8_t*)&pet,sizeof(pet)) || memcmp(&pet,&fish,sizeof(pet))) return;
    LoRaFishEggReceipt completed;completed.capture(accepted,loRaFishEggReceipt.sequence()+1,true);
    if (!writeLoRaFishEggReceipt(completed)) return;
    loRaFishEggApplying=false;closeLoRaFishAdoption();
    ContactInfo* tank=the_mesh.lookupContactByPubKey(accepted.peer,32);
    if(tank) {
      char reply[80];accepted.ack(reply,sizeof(reply));uint32_t hash=0,timeout=0;
      the_mesh.sendMessage(*tank,the_mesh.getRTCClock()->getCurrentTime(),0,reply,hash,timeout);
    }
    return;
  }
  if (loRaFishEggWindowStarted && now-loRaFishEggWindowMs>=120000UL) {
    closeLoRaFishAdoption();return;
  }
  if (!visible) return;
  if (!loRaFishEggStorageHealthy) { closeLoRaFishAdoption();return; }
  if (!loRaFishEggWindowStarted) {
    loRaFishEggWindowStarted=true;loRaFishEggWindowMs=now;
    selectLoRaFishEggTank(false);
  }
  if (loRaFishEggSession.phase==LoRaFishEgg::Closed) {
    if(now-loRaFishEggWindowMs>=10000UL) { closeLoRaFishAdoption();return; }
    selectLoRaFishEggTank(false);
  }
  if (loRaFishEggSession.due(now) && the_mesh.loRaFishQueueReady()) {
    ContactInfo* tank=the_mesh.lookupContactByPubKey(loRaFishEggSession.peer,32);
    if (!tank) { closeLoRaFishAdoption();return; }
    char hello[80];loRaFishEggSession.hello(hello,sizeof(hello));uint32_t hash=0,timeout=0;
    int result=the_mesh.sendMessage(*tank,the_mesh.getRTCClock()->getCurrentTime(),0,hello,hash,timeout);
    loRaFishEggSession.attempted(now,result<0?5000:timeout);
  }
}
