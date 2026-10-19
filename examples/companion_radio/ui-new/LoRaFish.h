#pragma once

// LoRaGotchi DX fish for Meck (MECK_LORAFISH builds). See LoRaFish.cpp.

#ifdef MECK_LORAFISH

class UITask;
class GxEPDDisplay;
struct NodePrefs;
namespace mesh { class RTCClock; }

// Results of a gesture or key on the fish screen.
enum {
  LORAFISH_MENU_STAY = 0,   // stay on the fish screen (redraw)
  LORAFISH_MENU_EXIT,       // back to the Games menu
  LORAFISH_MENU_HOME        // to the aquarium home page
};

void loRaFishBegin(UITask* task, NodePrefs* prefs, mesh::RTCClock* rtc);
void loRaFishLoop(bool aquariumOnScreen);

// Mesh events that feed the fish.
void loRaFishOnMessage();        // a text message arrived
void loRaFishOnContactAdvert();  // an advert from a contact was heard
void loRaFishOnAck();            // UI ack event (for example, advert sent)

// Draw onto the panel (physical pixels); return milliseconds until the next
// frame. The aquarium is drawn under Meck's home header.
int  loRaFishRenderAquarium(GxEPDDisplay& d);
int  loRaFishRenderMenu(GxEPDDisplay& d);

// Long press / Enter on the aquarium page. Returns true when the fish screen
// should open (to choose a new fish's colours; colour displays only).
bool loRaFishAquariumHold();

// Fish screen (opened from the Games menu).
void loRaFishOpenMenu();
int  loRaFishMenuTap(int px, int py);   // physical panel pixels
int  loRaFishMenuSwipe(int dx, int dy);
int  loRaFishMenuHold();
int  loRaFishMenuKey(char c);           // q / Shift+Del, a, d, w, s, Enter

#endif
