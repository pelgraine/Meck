// =============================================================================
// LoRaFish.cpp -- the LoRaGotchi DX fish inside Meck (MECK_LORAFISH builds,
// T-Deck Max e-ink)
//
// The fish engine and drawing code below is LoRaGotchi DX's own (by Robert
// Rayner / M0YNW, MIT licence), Water edition, taken from E's
// wio-l1-eink-anglerfish branch of LoRagotchi-DX, with a few marked edits for
// Meck. It runs in LoRaGotchi's colour mode with the T-Watch S3's tank scenery
// and draws into its own
// 128x128 RGB565 frame. Each colour is then turned into one of the grey
// patterns the home-screen tile borders use (GxEPDDisplay meckDitherKeep:
// solid, 75%, 50%, plus a 25% step from the same diagonal family, and paper),
// the frame is scaled 1.875x by nearest neighbour and drawn onto the panel in
// physical pixels with GxEPDDisplay::drawXbmRaw (as the Game Boy screen draws
// its picture). The pattern is fixed to panel position, as on the tiles.
//
// Everything lives in namespace lfx so LoRaGotchi's DisplayDriver/UIColor
// drawing API doesn't collide with Meck's. The public entry points are at the
// end of the file and declared in LoRaFish.h.
// =============================================================================
#ifdef MECK_LORAFISH

// LoRaGotchi build options used by its T-Watch S3 builds, always on here.
// The T-Watch S3's touch hint text (LORAFISH_WATCH_TOUCH) is left out.
#define LORAFISH_UI 1
#define LORAFISH_TALL_TANK
#define LORAFISH_CARDPUTER_25D
#define LORAFISH_FISH_WANDER
#define LORAFISH_NO_GNSS_PAGE

#include <Arduino.h>
#include <SPIFFS.h>
#include <Adafruit_GFX.h>
#include <esp_heap_caps.h>
#include <helpers/ui/GxEPDDisplay.h>
#include "UITask.h"
#include "../MyMesh.h"
#include "target.h"
#include "LoRaFish.h"
#include "PetResetConfirmation.h"
#include "AdvertMenu.h"
#include "LoRaFishMenu.h"
#include "LoRaFishHappiness.h"
#include "LoRaFishText.h"
#include "../LoRaFishColour.h"
#include "../LoRaFishSpecies.h"
#include "../LoRaFishEgg.h"
#include "../LoRagotchiChannelStatus.h"
#include "LoRaFishWaterSprite.h"

namespace lfx {

// ---- LoRaGotchi's drawing API (src/helpers/ui/DisplayDriver.h there) --------
using ColorVal = uint16_t;

class UIColor {
public:
  static ColorVal window_bkg, title_bkg, title_txt, primary_txt, secondary_txt, warning_txt, popup_bkg, popup_txt, corp_blue;
};
// Same starting palette as LoRaGotchi's T-Watch S3 build; the fish theme
// (applyLoRaFishTheme) replaces it at start-up.
ColorVal UIColor::window_bkg = 0x0000;
ColorVal UIColor::title_bkg = 0x001F;
ColorVal UIColor::title_txt = 0xFFFF;
ColorVal UIColor::primary_txt = 0xFFFF;
ColorVal UIColor::secondary_txt = 0xBDF7;
ColorVal UIColor::warning_txt = 0xFD20;
ColorVal UIColor::popup_bkg = 0x001F;
ColorVal UIColor::popup_txt = 0xFFFF;
ColorVal UIColor::corp_blue = 0x001A;

class DisplayDriver {
  int _w, _h;
protected:
  DisplayDriver(int w, int h) { _w = w; _h = h; }
public:
  int width() const { return _w; }
  int height() const { return _h; }
  virtual bool supportsColor() const { return false; }
  virtual bool isOn() = 0;
  virtual void turnOn() = 0;
  virtual void turnOff() = 0;
  virtual void setBrightness(uint8_t brightness) {}
  virtual void clear() = 0;
  virtual void startFrame(ColorVal bkg = UIColor::window_bkg) = 0;
  virtual void setTextSize(int sz) = 0;
  virtual void setColor(ColorVal c) = 0;
  virtual void setCursor(int x, int y) = 0;
  virtual void print(const char* str) = 0;
  virtual void fillRect(int x, int y, int w, int h) = 0;
  virtual void drawRect(int x, int y, int w, int h) = 0;
  virtual void drawXbm(int x, int y, const uint8_t* bits, int w, int h) = 0;
  virtual uint16_t getTextWidth(const char* str) = 0;
  virtual void drawTextCentered(int mid_x, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(mid_x - w/2, y);
    print(str);
  }
  virtual void drawTextRightAlign(int x_anch, int y, const char* str) {
    int w = getTextWidth(str);
    setCursor(x_anch - w, y);
    print(str);
  }
  virtual void drawTextLeftAlign(int x_anch, int y, const char* str) {
    setCursor(x_anch, y);
    print(str);
  }
  virtual void drawTextEllipsized(int x, int y, int max_width, const char* str) {
    char text[32];
    strncpy(text, str, sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    if (getTextWidth(text) > (uint16_t)max_width) {
      int keep = (max_width - (int)getTextWidth("...")) / 6;
      if (keep < 0) keep = 0;
      if (keep > (int)sizeof(text) - 4) keep = sizeof(text) - 4;
      strcpy(text + keep, "...");
    }
    setCursor(x, y);
    print(text);
  }
  virtual void endFrame() = 0;
};

// ---- Grey patterns ------------------------------------------------------------
// Shade 0 = solid ink, 1 = 75% ink, 2 = 50% checker (the home tile borders'
// three patterns), 3 = 25% ink (one step lighter, same diagonal family),
// 4 = paper. Decided per physical panel pixel, so the pattern stays put.
static inline bool fishShadeInk(int px, int py, uint8_t shade) {
  if (shade == 0) return true;
  if (shade == 1) return ((px + py) & 3) != 3;
  if (shade == 2) return ((px + py) & 1) == 0;
  if (shade == 3) return ((px + py) & 3) == 0;
  return false;
}

// Body fill markers: the fish sprites clear their body area before drawing
// the outline; on Meck that clear is drawn in a marker colour that carries
// the fish's body colour index (0x0800 + index), so the body can be filled
// with that colour's grey. No other colour the fish draws is in 0x0800-0x0807.
static constexpr uint16_t FISH_MASK_BASE = 0x0800;
static uint16_t s_fishMaskColor = FISH_MASK_BASE + 6;
static inline bool isFishMask(uint16_t c) { return c >= FISH_MASK_BASE && c <= FISH_MASK_BASE + 7; }

// Water species sprites have no outlines of their own, so each part is drawn in
// a marker carrying its grey (0x0810 + shade 0..4). shadeRows() fills the inside
// with that grey and inks every edge against the water, which keeps a pale fish
// visible on the paper tank. No other colour the fish draws is in this range.
static constexpr uint16_t FISH_TONE_BASE = 0x0810;
static inline bool isFishTone(uint16_t c) { return c >= FISH_TONE_BASE && c <= FISH_TONE_BASE + 4; }

// Body grey per fish colour: Coral, Tangerine, Gold, Mint, Lagoon, Orchid,
// Mono (and the grey used for a visiting black-and-white fish, 7).
static const uint8_t kFishBodyShade[8] = {1, 2, 3, 3, 2, 1, 4, 4};

// The fish colours, in LoRaGotchi's order (loRaFishColors below).
static const uint16_t kFishRGB[6] = {0xF986, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0xF81F};
static int fishColorIndex(uint16_t c) {
  for (int i = 0; i < 6; i++) if (kFishRGB[i] == c) return i;
  return -1;
}

// Grey for a colour by its brightness.
static uint8_t brightnessShade(uint16_t c) {
  const uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  const uint32_t lum = (r * 299 * 63 / 31 + g * 587 + b * 114 * 63 / 31) / 63;   // 0..1000
  return lum > 800 ? 4 : lum > 550 ? 3 : lum > 300 ? 2 : lum > 120 ? 1 : 0;
}

// Marker index for a fish body: a fine hue uses its nearest named colour.
static inline uint8_t fishMaskIndex(uint8_t allele) {
  if (allele <= 7) return allele;
  return allele == 0xFF ? 7 : LoRaFishColour::fallback(allele);
}

// Everything else: the tank scenery (drawCardputerTankDepth), the Paper
// theme's colours, then brightness for anything not listed.
static uint8_t sceneryShade(uint16_t c) {
  switch (c) {
    // Water, top to bottom: paper, then 25% in the deepest three bands.
    case 0x010B: case 0x012D: case 0x014F: case 0x0170: case 0x0190: return 4;
    case 0x01B0: case 0x01D0: case 0x01EF: return 3;
    case 0x2B75: return 4;   // light rays: left out (dotted noise on paper)
    case 0x6DBB: return 0;   // drifting particles
    case 0x0147: return 2;   // far reef
    case 0x6328: return 1;   // sand layers
    case 0x9C4B: return 2;
    case 0xDDAF: return 3;
    case 0x03C8: return 0;   // seaweed
    case 0x05A8: return 1;
    case 0x0125: return 4;   // soft shadow under the fish: left out (dotted halo)
    case 0xCFFF: return 4;   // glint on the fish's back
    case 0x2104: return 0;   // young fish outline
    case 0xDEFB: return 4;   // young fish outline on a dark body
    // Paper theme
    case 0xFFFF: return 4;
    case 0x0000: return 0;
    case 0x7BEF: return 0;
    case 0xF800: return 0;
    case 0x001F: return 0;
    case 0x041F: return 0;
    case 0xCE79: return 4;
    default: break;
  }
  return brightnessShade(c);
}

// Grey for one part of a Water species sprite. Mono and the white return flash
// are paper with an inked edge, like the original fish.
static uint8_t fishSpriteTone(uint8_t allele, uint16_t c) {
  if (allele == 6 || allele == 7 || allele == 0xFF) return 4;
  const int named = fishColorIndex(c);
  return named >= 0 ? kFishBodyShade[named] : brightnessShade(c);
}

// ---- The fish frame: 128x128 RGB565, drawn at 1.875x in greys ---------------
// Same canvas rules as LoRaGotchi's TWatchS3Display / TWatchS3TankView:
//   menu view  128x64, drawn at frame rows +32 (centred)
//   tank view  128x128; with shift, rows 16-63 move down by 32 (aquarium)
static constexpr int FRAME_W = 128;
static constexpr int FRAME_H = 128;
static constexpr int PANEL_W = 240;                 // 128 x 1.875
static constexpr int PANEL_STRIDE = PANEL_W / 8;    // 30 bytes per panel row

// Adafruit GFX canvas with its 16-bit pixels in PSRAM.
class FishCanvas16 : public Adafruit_GFX {
public:
  uint16_t* buf = nullptr;
  FishCanvas16(int16_t w, int16_t h) : Adafruit_GFX(w, h) {
    const size_t n = (size_t)w * h * sizeof(uint16_t);
    buf = (uint16_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (uint16_t*)malloc(n);
  }
  void drawPixel(int16_t x, int16_t y, uint16_t c) override {
    if (buf && x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT) buf[y * WIDTH + x] = c;
  }
  void fillScreen(uint16_t c) override {
    if (buf) for (int i = 0; i < WIDTH * HEIGHT; i++) buf[i] = c;
  }
};

class FishFrame {
public:
  FishCanvas16* spr = nullptr;
  uint8_t* shade = nullptr;    // grey per frame pixel (0..4)
  uint8_t* out = nullptr;      // scaled picture for drawXbmRaw, bit set = ink
  bool ok = false;
  uint16_t color = 0xFFFF;
  uint8_t textSize = 1;

  bool ensure() {
    if (ok) return true;
    if (!spr) spr = new FishCanvas16(FRAME_W, FRAME_H);
    if (!shade) {
      shade = (uint8_t*)heap_caps_malloc(FRAME_W * FRAME_H, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!shade) shade = (uint8_t*)malloc(FRAME_W * FRAME_H);
    }
    if (!out) {
      const size_t n = (size_t)PANEL_STRIDE * (FRAME_H * 15 / 8);
      out = (uint8_t*)heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!out) out = (uint8_t*)malloc(n);
    }
    ok = spr && spr->buf && shade && out;
    if (ok) { spr->setTextSize(1); spr->setTextWrap(false); }
    return ok;
  }

  // Grey for each frame pixel in rows [top, top+rows).
  void shadeRows(int top, int rows) {
    const uint16_t* p = spr->buf;
    for (int y = top; y < top + rows; y++) {
      for (int x = 0; x < FRAME_W; x++) {
        const uint16_t c = p[y * FRAME_W + x];
        uint8_t s;
        if (isFishMask(c)) {
          s = kFishBodyShade[c - FISH_MASK_BASE];          // body fill
        } else if (isFishTone(c)) {
          // Species sprite: its grey inside, solid ink along any edge.
          const bool edge =
              x == 0 || y == 0 || x + 1 == FRAME_W || y + 1 == FRAME_H ||
              !isFishTone(p[y * FRAME_W + x + 1]) || !isFishTone(p[y * FRAME_W + x - 1]) ||
              !isFishTone(p[(y + 1) * FRAME_W + x]) || !isFishTone(p[(y - 1) * FRAME_W + x]);
          s = edge ? 0 : (uint8_t)(c - FISH_TONE_BASE);
        } else if (fishColorIndex(c) >= 0) {
          // A fish colour: a scale dot inside a body takes the opposite of
          // the body's grey, the inside of a solid body area takes its own
          // grey, and outlines, fins and eyes are solid ink.
          const uint16_t n0 = (x + 1 < FRAME_W) ? p[y * FRAME_W + x + 1] : 0;
          const uint16_t n1 = (x > 0)           ? p[y * FRAME_W + x - 1] : 0;
          const uint16_t n2 = (y + 1 < FRAME_H) ? p[(y + 1) * FRAME_W + x] : 0;
          const uint16_t n3 = (y > 0)           ? p[(y - 1) * FRAME_W + x] : 0;
          if (n0 == n1 && n1 == n2 && n2 == n3 && n0 != c) {
            uint8_t under = 4;
            if (isFishMask(n0)) under = kFishBodyShade[n0 - FISH_MASK_BASE];
            else if (fishColorIndex(n0) >= 0) under = kFishBodyShade[fishColorIndex(n0)];
            s = under <= 2 ? 4 : 0;
          } else if (n0 == c && n1 == c && n2 == c && n3 == c) {
            s = kFishBodyShade[fishColorIndex(c)];
          } else {
            s = 0;
          }
        } else {
          s = sceneryShade(c);
        }
        shade[y * FRAME_W + x] = s;
      }
    }
  }

  // Scale frame rows [srcTop, srcTop+srcRows) by 1.875 (nearest neighbour)
  // and draw them on the panel at physical (0, dstY), in the panel's
  // foreground colour (so Meck's dark mode inverts the fish with the rest).
  void push(GxEPDDisplay& panel, int srcTop, int srcRows, int dstY) {
    if (!ok) return;
    shadeRows(srcTop, srcRows);
    const int dstRows = srcRows * 15 / 8;
    for (int oy = 0; oy < dstRows; oy++) {
      const int sy = srcTop + (oy * 8) / 15;
      const uint8_t* srow = &shade[(size_t)sy * FRAME_W];
      uint8_t* drow = &out[(size_t)oy * PANEL_STRIDE];
      const int py = dstY + oy;
      for (int bx = 0; bx < PANEL_STRIDE; bx++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
          const int px = bx * 8 + b;
          if (fishShadeInk(px, py, srow[(px * 8) / 15])) byte |= (uint8_t)(0x80 >> b);
        }
        drow[bx] = byte;
      }
    }
    panel.drawXbmRaw(0, dstY, out, PANEL_W, dstRows, panel.rawFgColor());
  }
};
static FishFrame frame;

class FishView : public DisplayDriver {
  int _yoff;
  bool _shift;
  int mapY(int y) const { return _shift ? ((y >= 16 && y < 64) ? y + 32 : y) : y + _yoff; }
public:
  FishView(int w, int h, int yoff, bool shiftTank) : DisplayDriver(w, h), _yoff(yoff), _shift(shiftTank) {}
  bool supportsColor() const override { return true; }
  bool isOn() override { return true; }
  void turnOn() override {}
  void turnOff() override {}
  void clear() override { if (frame.ok) frame.spr->fillScreen(0x0000); }
  void startFrame(ColorVal bkg = UIColor::window_bkg) override {
    if (frame.ok) { frame.spr->fillScreen(bkg); frame.spr->setCursor(0, 0); }
    frame.color = UIColor::primary_txt;
    if (frame.ok) frame.spr->setTextColor(frame.color);
  }
  void setTextSize(int sz) override {
    frame.textSize = (uint8_t)(sz < 1 ? 1 : sz);
    if (frame.ok) frame.spr->setTextSize(frame.textSize);
  }
  void setColor(ColorVal c) override { frame.color = c; if (frame.ok) frame.spr->setTextColor(frame.color); }
  // Adafruit GFX's built-in font positions text by its top edge, like
  // LovyanGFX's Font0 on the watch.
  void setCursor(int x, int y) override { if (frame.ok) frame.spr->setCursor(x, mapY(y)); }
  void print(const char* str) override { if (frame.ok) frame.spr->print(str); }
  void fillRect(int x, int y, int w, int h) override { if (frame.ok) frame.spr->fillRect(x, mapY(y), w, h, frame.color); }
  void drawRect(int x, int y, int w, int h) override { if (frame.ok) frame.spr->drawRect(x, mapY(y), w, h, frame.color); }
  // LoRaGotchi's bitmaps are MSB-first, which Adafruit GFX's drawBitmap matches.
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override { if (frame.ok) frame.spr->drawBitmap(x, mapY(y), bits, w, h, frame.color); }
  // The built-in font is 6 pixels wide per character (5 + 1 spacing).
  uint16_t getTextWidth(const char* str) override { return (uint16_t)(strlen(str) * 6 * frame.textSize); }
  void endFrame() override {}
};
static FishView menu_display(128, 64, 32, false);          // menus (centred band)
static FishView tank_display(128, 128, 0, true);           // aquarium, tank rows moved down
static FishView tank_scenery_display(128, 128, 0, false);  // full-height scenery

// Meck: on the aquarium page Meck's own header (node name, clock, battery)
// sits above the tank, so the fish's header line and battery icon are left
// out there. The tank's footer (stage, packets, unread) stays.
static bool s_noTankHeader = false;

// ---- Meck adjustments used by the ported code -------------------------------
// The fish's local time follows Meck's Home offset (whole hours).
static int8_t loRaFishMeckOffsetQuarters() {
  NodePrefs* p = the_mesh.getNodePrefs();
  return p ? (int8_t)(p->utc_offset_hours * 4) : 0;
}
// Display menu rows left out in Meck: screen timeout (0), brightness (1).
static bool loRaFishDisplayOptionHidden(uint8_t row) {
  return row == 0 || row == 1;
}
// Schedule page fields: Sleep and Wake only (no UTC offset row).
static constexpr uint8_t LORAFISH_TIME_FIELDS = 2;

// ---- Ported from LoRaGotchi DX (UITask.cpp) ----
static mesh::RTCClock* loRaFishRTC = nullptr;

static const char* loRaFishMeshNodeName = nullptr;
static bool loRaFishNameNotice = false;
static bool loRaFishChannelNotice = false;
// Stored in the settings file's MON1 field only. The Max reports a white panel.
static uint8_t loRaFishMonoDisplayColour = 1;
static void saveLoRaFishSettings();

static void beginLoRaFishChannelNotice() {
  loRaFishChannelNotice = LoRagotchiChannel::startupResult == LoRagotchiChannel::Result::Full;
}

static bool dismissLoRaFishNotice() {
  // One gesture dismisses one notice, without navigating, saving or sending.
  if (loRaFishChannelNotice) { loRaFishChannelNotice = false; return true; }
  if (loRaFishNameNotice) { loRaFishNameNotice = false; return true; }
  return false;
}

static void beginLoRaFishNameNotice() {
  loRaFishNameNotice = !LoRaFishText::hasFish(loRaFishMeshNodeName, 32);
}

static const char* loRaFishProtocolName() {
  static char name[16];
  LoRaFishText::protocolName(name, sizeof(name), loRaFishMeshNodeName, 32);
  return name[0] ? name : "LoRaFish";
}

static const char* loRaFishDisplayName() {
  if (!loRaFishMeshNodeName || !loRaFishMeshNodeName[0]) return "LoRaFish";

  // Keep the left header clear of the clock/right-side status on 128px displays.
  static char shortName[16];
  LoRaFishText::display(shortName, sizeof(shortName), loRaFishMeshNodeName, 32);
  return shortName;
}

// ============================================================
// LoRaFish MeshCore Edition v0.3.2 - DisplayDriver compatibility
// MeshCore owns radio + Companion BLE. This pet layer only observes UI events.
// ============================================================
struct LoRaFishStateV1 { // layout shipped before the DX and uptime counters
  uint32_t magic;
  uint32_t ageMinutes;
  uint32_t rfEvents;
  uint32_t messages;
  uint32_t txAcks;
  uint8_t happiness;
  uint8_t evolution;
};

struct LoRaFishState {
  uint32_t magic;
  uint32_t ageMinutes;
  uint32_t rfEvents;
  uint32_t messages;
  uint32_t txAcks;
  uint8_t happiness;
  uint8_t evolution;
  uint32_t dxVisits;
  uint32_t longestUptimeMinutes;
};

static constexpr uint32_t LORAFISH_MAGIC = 0x4C464D32; // "LFM2"
static constexpr uint32_t LORAFISH_MAGIC_V1 = 0x4C464D31; // "LFM1"
static void saveLoRaFish();
static void saveLoRaFishSettings();
static void scheduleLoRaFishPoop();
static LoRaFishState fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0};
static uint32_t fishLastMinute = 0;
static uint32_t fishLastSave = 0;
static LoRaFishHappiness loRaFishHappiness;
static LoRaFishHappiness lastWrittenLoRaFishHappiness;
static bool haveLastWrittenLoRaFishHappiness = false;
static uint32_t loRaFishHappyLastMs = 0;
static uint32_t loRaFishHappyLastSavedMs = 0;
static constexpr uint32_t LORAFISH_HAPPY_SETTINGS_MAGIC = 0x48415031; // "HAP1"
static void advanceLoRaFishHappiness(uint32_t until);
static void tickLoRaFish();
static unsigned long fishReactionUntil = 0;
static uint8_t fishReaction = 0; // 0 normal, 1 RX, 2 TX/ACK, 3 new contact

static uint8_t loRaFishPage = 0; // 0 aquarium, 1 stats, 2 clock, 3 pairing, 4 DX, 5 options, 6 RF, 7 GNSS, 8 theme, 9 fish, 10 schedule, 11 timeout, 12 display, 13 brightness
static LoRaFishOption loRaFishOption = LoRaFishOption::Advert;
static LoRaFishValuePicker loRaFishValuePicker;
static uint8_t loRaFishDisplayOption = 0;
static uint8_t loRaFishGuessNumber=0;
static uint8_t loRaFishGuessPrevious=0;
static uint8_t loRaFishGuessRounds=0;
static uint8_t loRaFishGuessScore=0;
static bool loRaFishGuessHigher=true;
static bool loRaFishGuessResultShown=false;
static bool loRaFishGuessCorrect=false;
static bool loRaFishGuessComplete=false;
static bool loRaFishSimulationMode = false;
static uint8_t loRaFishTimeField = 0;
static uint8_t loRaFishTimeoutField = 0;
static uint8_t loRaFishSoundField = 0;
// The 8-page line comes first: the host checks in test/ui read the first
// LORAFISH_PAGE_COUNT declaration in this file.
// LilyGo T-Watch S3: no GNSS receiver, so the GNSS page (7, the last main
// page) is left out, and the GNSS Monitor row is filtered out of Options
// (see loRaFishWatchHidesOption).
static constexpr uint8_t LORAFISH_PAGE_COUNT = 7;
static constexpr uint8_t LORAFISH_RESET_CONFIRM_PAGE = 17;
static PetResetConfirmation loRaFishResetConfirmation;
static constexpr uint8_t LORAFISH_ADVERT_PAGE = 18;
static AdvertMenu loRaFishAdvertMenu;
static constexpr uint8_t LORAFISH_DX_CONTACT_PAGE = 19;
static int8_t loRaFishRfHistory[48] = {0};
static uint8_t loRaFishRfHistoryCount = 0;
static uint32_t loRaFishRfSeenSeq = 0;
static uint8_t loRaFishTheme = 1;   // Meck: light (Paper) theme by default on e-ink
static uint8_t loRaFishColor = 0;
static uint16_t loRaFishSpecies=LoRaFishSpecies::TropicalFish;
static uint16_t loRaFishEggSpecies=LoRaFishSpecies::TropicalFish;
static uint32_t loRaFishMysterySeed=0;
static bool loRaFishNeedsPetSetup=false;
static uint8_t loRaFishPetSetupField=0;
static constexpr uint8_t LORAFISH_PET_SETUP_PAGE=20;
static constexpr uint8_t LORAFISH_MONOCHROME_COLOR = 6;
static constexpr uint8_t LORAFISH_MONOCHROME_RENDER_TINT = 7;
static uint8_t loRaFishScaleColor = 0;
static uint8_t loRaFishEyeColor = 0;
static uint8_t loRaFishColorField = 0;  // Scales, Body, Eyes, then Start fish during setup
static bool loRaFishAppearanceLoaded = false;
static bool loRaFishNeedsAppearanceSetup = false;
static bool loRaFishAdoptionEligible = false;
static bool loRaFishColorDisplay = false;

struct LoRaFishAppearance {
  uint8_t scales;
  uint8_t body;
  uint8_t eyes;
};
struct LoRaFishTravelAnimation {
  bool pending=false;
  bool active=false;
  bool entering=false;
  uint16_t species=0;
  uint8_t stage=2;
  LoRaFishAppearance appearance={0,0,0};
  unsigned long started=0;
  int startX=0;
  int endX=0;
  int y=38;
};
static constexpr unsigned long LORAFISH_TRAVEL_ANIMATION_MS = 1000UL;
static LoRaFishTravelAnimation loRaFishHomeTravelAnimation;
static LoRaFishTravelAnimation loRaFishGuestTravelAnimation;
static bool loRaFishTravelStateInitialized=false;
static bool loRaFishReturnNavigationHandled=false;
static bool loRaFishLastHomeAway=false;
static bool loRaFishLastGuestPresent=false;
static uint8_t loRaFishLastGuestStage=2;
static uint16_t loRaFishLastGuestSpecies=0;
static LoRaFishAppearance loRaFishLastGuestAppearance={0,0,0};
static uint16_t loRaFishGeneration = 1;
static bool loRaFishPendingEgg = false;
static LoRaFishAppearance loRaFishEggAppearance = {0,0,0};
static int8_t loRaFishMemorialEggSide = 0;

struct LoRaFishThemePalette {
  const char* name;
  ColorVal background;
  ColorVal titleBackground;
  ColorVal titleText;
  ColorVal primaryText;
  ColorVal secondaryText;
  ColorVal warningText;
  ColorVal popupBackground;
  ColorVal popupText;
  ColorVal accent;
};

static const LoRaFishThemePalette loRaFishThemes[] = {
  {"Midnight", 0x0861, 0x1193, 0xFFFF, 0xEF7D, 0x9CF3, 0xFFE0, 0x1082, 0xFFFF, 0x07FF},
  {"Paper",    0xFFFF, 0x001F, 0xFFFF, 0x0000, 0x7BEF, 0xF800, 0xCE79, 0x0000, 0x041F},
  {"Lagoon",   0x0128, 0x03B0, 0xFFFF, 0xDFFF, 0x7DDF, 0xFEA0, 0x032A, 0xFFFF, 0x4DFF},
  {"Sunset",   0x3006, 0xA205, 0xFFFF, 0xFFF1, 0xFD20, 0xF800, 0x5805, 0xFFFF, 0xF9A6},
};
static constexpr uint8_t LORAFISH_COLOR_THEME_COUNT = sizeof(loRaFishThemes) / sizeof(loRaFishThemes[0]);
static const char* loRaFishColorNames[] = {"Coral", "Tangerine", "Gold", "Mint", "Lagoon", "Orchid", "Mono"};
static constexpr uint8_t LORAFISH_MONO_DISPLAY_COLOUR_COUNT = 3;
static const ColorVal loRaFishColors[] = {0xF986, 0xFC00, 0xFFE0, 0x07E0, 0x07FF, 0xF81F};
static constexpr uint8_t LORAFISH_FISH_COLOR_COUNT = sizeof(loRaFishColors) / sizeof(loRaFishColors[0]);
static constexpr uint8_t LORAFISH_COLOR_ALLELE_COUNT = LORAFISH_FISH_COLOR_COUNT + 1;

static uint8_t loRaFishThemeCount() {
  return loRaFishColorDisplay ? LORAFISH_COLOR_THEME_COUNT : 2;
}

static void applyLoRaFishTheme() {
  const LoRaFishThemePalette& theme=loRaFishThemes[loRaFishTheme % loRaFishThemeCount()];
  if (!loRaFishColorDisplay) {
    const bool light=(loRaFishTheme == 1);
    const ColorVal background=light ? 1 : 0;
    const ColorVal foreground=light ? 0 : 1;
    UIColor::window_bkg=background;
    UIColor::title_bkg=background;
    UIColor::title_txt=foreground;
    UIColor::primary_txt=foreground;
    UIColor::secondary_txt=foreground;
    UIColor::warning_txt=foreground;
    UIColor::popup_bkg=background;
    UIColor::popup_txt=foreground;
    UIColor::corp_blue=foreground;
    return;
  }
  UIColor::window_bkg=theme.background;
  UIColor::title_bkg=theme.titleBackground;
  UIColor::title_txt=theme.titleText;
  UIColor::primary_txt=theme.primaryText;
  UIColor::secondary_txt=theme.secondaryText;
  UIColor::warning_txt=theme.warningText;
  UIColor::popup_bkg=theme.popupBackground;
  UIColor::popup_txt=theme.popupText;
  UIColor::corp_blue=theme.accent;
}

static ColorVal loRaFishColorValue(uint8_t color) {
  if (color==0xFF) return 0xFFFF;
  // Meck: a visiting black-and-white fish is drawn in ink. Its home's pixel
  // tint (white on most boards) would vanish on the paper water.
  if (color==LORAFISH_MONOCHROME_RENDER_TINT && loRaFishColorDisplay)
    return UIColor::primary_txt;
  // Meck: the panel shows greys, so a fine hue draws as its nearest named
  // colour. Display only; the stored and sent colour genes are unchanged.
  return loRaFishColorDisplay && LoRaFishColour::valid(color) && color != 6
      ? LoRaFishColour::rgb565(LoRaFishColour::fallback(color)) : UIColor::primary_txt;
}

static LoRaFishAppearance loRaFishLocalAppearance() {
  return {loRaFishScaleColor,loRaFishColor,loRaFishEyeColor};
}
static bool loRaFishAppearanceHasColor(const LoRaFishAppearance& appearance) {
  return appearance.scales != 6 || appearance.body != 6 || appearance.eyes != 6;
}
static void loRaFishSyncDXAppearance() {
  const LoRaFishAppearance appearance=loRaFishLocalAppearance();
  loragotchiDXLocalColorCapable=loRaFishColorDisplay;
  loragotchiDXLocalHasColorGenes=loRaFishAppearanceHasColor(appearance);
  loragotchiDXLocalColorIndex=appearance.body;
  loragotchiDXLocalScalesColorIndex=appearance.scales;
  loragotchiDXLocalEyesColorIndex=appearance.eyes;
  loragotchiDXLocalSpecies=loRaFishSpecies;
  loragotchiDXLocalMonoPanel=1;  // Meck: visits report a white panel
  loragotchiDXLocalMonoPixelTint=0xFFFF;
}
static unsigned long loRaFishReturnShakeUntil = 0;

static LoRaFishAppearance loRaFishGuestAppearance() {
  if (!loragotchiDXGuest.colorCapable)
    return {LORAFISH_MONOCHROME_COLOR,LORAFISH_MONOCHROME_COLOR,LORAFISH_MONOCHROME_COLOR};
  return {loragotchiDXGuest.scalesColor,loragotchiDXGuest.color,loragotchiDXGuest.eyesColor};
}

static LoRaFishAppearance loRaFishGuestRenderAppearance() {
  if (loRaFishColorDisplay && !loragotchiDXGuest.colorCapable)
    return {LORAFISH_MONOCHROME_RENDER_TINT,LORAFISH_MONOCHROME_RENDER_TINT,
            LORAFISH_MONOCHROME_RENDER_TINT};
  return loRaFishGuestAppearance();
}

// Lifecycle presentation: egg for first 5 minutes, then normal fish stages.
// At 7 days the pet reaches the end of its life and shows a memorial screen.
static constexpr uint32_t LORAFISH_EGG_MINUTES = 5;
static constexpr uint32_t LORAFISH_LIFESPAN_MINUTES = 7UL*24UL*60UL;
static bool loRaFishHatchMelodyPlayed = false;
static unsigned long loRaFishEvolutionFlashUntil = 0;

// User-selectable screen timeout. 5 means always on.
static uint8_t loRaFishTimeoutAwakeSetting = 0;
static uint8_t loRaFishTimeoutAsleepSetting = 0;
static const uint32_t loRaFishTimeoutValues[] = {15000UL,30000UL,60000UL,120000UL,180000UL,0UL};
static const char* loRaFishTimeoutShortNames[] = {"15s","30s","1m","2m","3m","Always"};
static uint8_t loRaFishBrightnessSetting = 3;
static const uint8_t loRaFishBrightnessValues[] = {64,128,192,255};
static const char* loRaFishBrightnessNames[] = {"25%","50%","75%","100%"};
static uint8_t loRaFishAlwaysOnDimSetting = 0;
static const uint32_t loRaFishAlwaysOnDimValues[] = {15000UL,30000UL,60000UL};
static const char* loRaFishAlwaysOnDimNames[] = {"15s","30s","60s"};
static unsigned long loRaFishLastActivityMs = 0;
static bool loRaFishScreenDimmed = false;
static bool loRaFishBrightnessNeedsApply = false;
static uint8_t loRaFishBuzzerVolume = 4;
static uint8_t loRaFishWakeSound = 0;
static uint8_t loRaFishSleepSound = 0;
static uint8_t loRaFishMessageSound = 1;
static uint8_t loRaFishSleepHour = 21;
static uint8_t loRaFishWakeHour = 7;
static int8_t loRaFishUtcOffsetQuarters = 0;
static bool loRaFishIsSleeping();
static bool loRaFishTimeoutSleepState = false;
static bool loRaFishTimeoutSleepStateKnown = false;
static uint32_t loRaFishScreenTimeoutMs() {
  const uint8_t setting=loRaFishIsSleeping() ? loRaFishTimeoutAsleepSetting : loRaFishTimeoutAwakeSetting;
  return loRaFishTimeoutValues[min((uint8_t)5,setting)];
}
static uint8_t loRaFishDisplayOptionCount() {
  return 5 + (LORAGOTCHI_SPECIES_EDITION ? 1 : 0);
}
static bool loRaFishCanBreed() {
  return !loRaFishPendingEgg && !loragotchiDXHome.away &&
      !loragotchiDXHome.pending && !loragotchiDXHome.committing &&
      fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES &&
      loragotchiDXGuest.present && loragotchiDXGuestReturn.status == LoRaFishGuestReturn::Idle &&
      fish.evolution>=2 && loragotchiDXGuest.stage>=2;
}
static constexpr bool loRaFishHasSound =
    false;
static uint8_t loRaFishOptionCount() {
  return LoRaFishOptions::count(loRaFishHasSound, loRaFishCanBreed(), loragotchiDXGuest.present);
}
static void returnToLoRaFishOptions(LoRaFishOption option) {
  loRaFishPage = 5;
  loRaFishOption = LoRaFishOptions::available(option, loRaFishHasSound, loRaFishCanBreed(), loragotchiDXGuest.present)
      ? option : LoRaFishOption::Advert;
}
static void cancelLoRaFishValue() {
  loRaFishValuePicker.cancel();
  // Standalone pickers return to Display; field pickers return to their field list.
  if (loRaFishPage == 8 || loRaFishPage == 13 || loRaFishPage == 15) loRaFishPage = 12;
}
static void openLoRaFishValue(LoRaFishSetting setting) {
  uint8_t value = 0, count = 0;
  switch (setting) {
    case LoRaFishSetting::Species:
      value = LoRaFishSpecies::selectionIndex(loRaFishSpecies);
      count = LoRaFishSpecies::selectableCount + (loRaFishNeedsPetSetup ? 1 : 0);
      break;
    case LoRaFishSetting::Theme: value = loRaFishTheme; count = loRaFishThemeCount(); break;
    case LoRaFishSetting::Brightness: value = loRaFishBrightnessSetting; count = 4; break;
    case LoRaFishSetting::Rendering: value = loRaFishSimulationMode; count = 2; break;
    case LoRaFishSetting::Scales: value = LoRaFishColour::fallback(loRaFishScaleColor); count = LORAFISH_COLOR_ALLELE_COUNT; break;
    case LoRaFishSetting::Body: value = LoRaFishColour::fallback(loRaFishColor); count = LORAFISH_COLOR_ALLELE_COUNT; break;
    case LoRaFishSetting::Eyes: value = LoRaFishColour::fallback(loRaFishEyeColor); count = LORAFISH_COLOR_ALLELE_COUNT; break;
    case LoRaFishSetting::SleepHour: value = loRaFishSleepHour; count = 24; break;
    case LoRaFishSetting::WakeHour: value = loRaFishWakeHour; count = 24; break;
    case LoRaFishSetting::UtcOffset: value = loRaFishUtcOffsetQuarters + 48; count = 105; break;
    case LoRaFishSetting::TimeoutAwake: value = loRaFishTimeoutAwakeSetting; count = 6; break;
    case LoRaFishSetting::TimeoutAsleep: value = loRaFishTimeoutAsleepSetting; count = 6; break;
    case LoRaFishSetting::DimAfter: value = loRaFishAlwaysOnDimSetting; count = 3; break;
  }
  loRaFishValuePicker.open(setting, value, count);
}
static void ensureLoRaFishMysterySeed() {
  if (!loRaFishMysterySeed) {
    loRaFishMysterySeed=((uint32_t)random(0,65536)<<16)|(uint32_t)random(0,65536);
    if (!loRaFishMysterySeed) loRaFishMysterySeed=1;
  }
}
static void chooseLoRaFishMysteryColours() {
  ensureLoRaFishMysterySeed();
  loRaFishScaleColor=LoRaFishSpecies::mysteryColour(loRaFishMysterySeed,0);
  loRaFishColor=LoRaFishSpecies::mysteryColour(loRaFishMysterySeed,1);
  loRaFishEyeColor=LoRaFishSpecies::mysteryColour(loRaFishMysterySeed,2);
  loRaFishSyncDXAppearance(); saveLoRaFishSettings();
}
static void selectLoRaFishValue() {
  uint8_t value;
  if (!loRaFishValuePicker.select(value)) return;
  tickLoRaFish();
  switch (loRaFishValuePicker.key()) {
    case LoRaFishSetting::Species:
      if (value==LoRaFishSpecies::selectableCount) { ensureLoRaFishMysterySeed(); loRaFishSpecies=LoRaFishSpecies::mystery(loRaFishMysterySeed); }
      else loRaFishSpecies=LoRaFishSpecies::selectionId(value);
      break;
    case LoRaFishSetting::Theme: loRaFishTheme = value; applyLoRaFishTheme(); break;
    case LoRaFishSetting::Brightness:
      loRaFishBrightnessSetting = value; loRaFishBrightnessNeedsApply = true; break;
    case LoRaFishSetting::Rendering: loRaFishSimulationMode = value; break;
    case LoRaFishSetting::Scales: loRaFishScaleColor = value; break;
    case LoRaFishSetting::Body: loRaFishColor = value; break;
    case LoRaFishSetting::Eyes: loRaFishEyeColor = value; break;
    case LoRaFishSetting::SleepHour: loRaFishSleepHour = value; break;
    case LoRaFishSetting::WakeHour: loRaFishWakeHour = value; break;
    case LoRaFishSetting::UtcOffset: loRaFishUtcOffsetQuarters = int(value) - 48; break;
    case LoRaFishSetting::TimeoutAwake: loRaFishTimeoutAwakeSetting = value; break;
    case LoRaFishSetting::TimeoutAsleep: loRaFishTimeoutAsleepSetting = value; break;
    case LoRaFishSetting::DimAfter: loRaFishAlwaysOnDimSetting = value; break;
  }
  loRaFishSyncDXAppearance();
  saveLoRaFishSettings();
  cancelLoRaFishValue();
}
static void applyLoRaFishBrightness(DisplayDriver& display, bool dimmed=false) {
  uint8_t brightness=loRaFishBrightnessValues[min((uint8_t)3,loRaFishBrightnessSetting)];
  if (dimmed) brightness=brightness/4 < 12 ? 12 : brightness/4;
  display.setBrightness(brightness);
}
static void loRaFishRegisterDisplayActivity(DisplayDriver& display) {
  loRaFishLastActivityMs=millis();
  loRaFishScreenDimmed=false;
  applyLoRaFishBrightness(display);
}
static unsigned long loRaFishBootMs = 0;

#define LORAFISH_TIMEOUT_MS() loRaFishScreenTimeoutMs()
// Tamagotchi-style automatic feeding and sleep cycle.
static bool loRaFishFeeding = false;
static bool loRaFishFeedChasing = false;
static unsigned long loRaFishFeedStarted = 0;
static int loRaFishFeedStartX = 22;
static int loRaFishFeedStartY = 35;
static int loRaFishFeedLastX = 0;
static bool loRaFishFeedLastXValid = false;
static constexpr unsigned long LORAFISH_FEED_DROP_MS = 1300UL;
static constexpr unsigned long LORAFISH_FEED_NOTICE_MS = 250UL;
static constexpr unsigned long LORAFISH_FEED_RUSH_MS = 350UL;
static constexpr unsigned long LORAFISH_FEED_EAT_MS = 150UL;
static constexpr unsigned long LORAFISH_FEED_RETURN_MS = 300UL;
static constexpr unsigned long LORAFISH_FEED_DONE_MS =
  LORAFISH_FEED_DROP_MS + LORAFISH_FEED_EAT_MS + LORAFISH_FEED_RETURN_MS;
static constexpr uint32_t LORAFISH_PACKETS_PER_FEED = 10;

// LilyGo T-Watch S3 (tall tank): the fish wanders instead of patrolling
// wall to wall. Time is split into segments; in each one the fish glides to a
// new spot (x anywhere inside the tank margins, y within the middle band that
// TWatchS3TankView moves to the centre of the tall tank), then hovers for the
// rest of the segment. About one segment in six is a quick dart. Every spot is
// derived from the segment number, so the path is still a pure function of
// millis(): feeding, visitors and travel animations keep working unchanged.
struct LoRaFishWanderInfo {
  bool hovering;
  unsigned long hoverElapsed;
};

static uint32_t loRaFishWanderHash(uint32_t n, uint32_t salt) {
  uint32_t h = n * 2654435761UL ^ salt * 0x9E3779B9UL;
  h ^= h >> 15; h *= 0x85EBCA6BUL;
  h ^= h >> 13; h *= 0xC2B2AE35UL;
  h ^= h >> 16;
  return h;
}

static void loRaFishWanderPoint(uint32_t n, uint8_t stage, int w, int& x, int& y) {
  const uint32_t h = loRaFishWanderHash(n, 0x51EDUL + stage);
  const int margin = 20;                 // same sprite-safe margin as the patrol path
  const int span = max(1, w - margin * 2);
  x = margin + (int)(h % (uint32_t)(span + 1));
  y = 30 + (int)((h >> 16) % 19U);       // rows 30-48: keeps every stage's sprite in rows 16-63
}

static void loRaFishWanderPosition(int w, uint8_t stage, unsigned long phaseOffset,
                                   int& x, int& y, bool& right, LoRaFishWanderInfo* info) {
  const unsigned long segMs = stage == 0 ? 5000UL : stage == 1 ? 6000UL : stage >= 3 ? 9500UL : 7500UL;
  const unsigned long t = millis() + phaseOffset;
  const uint32_t n = (uint32_t)(t / segMs);
  const unsigned long into = t % segMs;

  int x0, y0, x1, y1;
  loRaFishWanderPoint(n - 1, stage, w, x0, y0);
  loRaFishWanderPoint(n, stage, w, x1, y1);

  const uint32_t h = loRaFishWanderHash(n, 0xD1CEUL + stage);
  const bool dart = (h % 6U) == 0;
  const unsigned long moveMs = dart ? 500UL : segMs * (50UL + (h >> 8) % 26U) / 100UL;
  float e = 1.0f;
  if (into < moveMs) {
    const float p = into / (float)moveMs;
    e = dart ? 1.0f - (1.0f - p) * (1.0f - p) * (1.0f - p)   // burst, then coast
             : p * p * (3.0f - 2.0f * p);                     // ease in and out
  }
  x = x0 + (int)((x1 - x0) * e);
  y = y0 + (int)((y1 - y0) * e) + (int)(1.0f * sinf((millis() + phaseOffset) / 650.0f));
  right = x1 >= x0;
  if (info) {
    info->hovering = into >= moveMs;
    info->hoverElapsed = info->hovering ? into - moveMs : 0;
  }
}

// A couple of small bubbles drift up from the fish's mouth while it hovers.
static void drawLoRaFishWanderBubbles(DisplayDriver& d, int x, int y, bool right, uint8_t stage,
                                      const LoRaFishWanderInfo& info) {
  if (!info.hovering) return;
  static const int mouth[] = {9, 12, 15, 18};
  const int mx = x + (right ? mouth[min((uint8_t)3, stage)] : -mouth[min((uint8_t)3, stage)]);
  d.setColor(UIColor::secondary_txt);
  for (int i = 0; i < 2; i++) {
    const long age = (long)info.hoverElapsed - 250L - i * 450L;
    if (age < 0 || age > 1800L) continue;
    const int rise = (int)(age / 110L);
    d.drawRect(mx + (i ? 2 : 0) + (int)(1.5f * sinf(age / 260.0f)), y - 3 - rise, i ? 2 : 3, i ? 2 : 3);
  }
}

static void loRaFishNaturalPositionForStage(int w, uint8_t stage, unsigned long phaseOffset,
                                             int& x, int& y, bool& right) {
  loRaFishWanderPosition(w, stage, phaseOffset, x, y, right, nullptr);
}

static void loRaFishNaturalPosition(int w, int& x, int& y, bool& right) {
  loRaFishNaturalPositionForStage(w,fish.evolution,0,x,y,right);
}

// Gameplay and presentation are deliberately separate:
// a meal is credited immediately when RF earns it; this animation is optional.
static bool creditLoRaFishMeal(bool animate) {
  const bool eligible = !loRaFishNeedsAppearanceSetup && !loRaFishNeedsPetSetup &&
      fish.ageMinutes >= LORAFISH_EGG_MINUTES &&
      fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES && !loRaFishIsSleeping();
  if (!loRaFishHappiness.meal(eligible, fish.happiness)) return false;
  scheduleLoRaFishPoop();
  saveLoRaFish();

  if (animate && !loRaFishFeeding) {
    loRaFishFeeding = true;
    loRaFishFeedChasing = false;
    loRaFishFeedStarted = millis();
  }
  return true;
}
static uint32_t loRaFishNextAutoFeed = LORAFISH_PACKETS_PER_FEED;

static bool consumeLoRaFishMeals(bool animate) {
  bool fed = false;
  while (fish.rfEvents >= loRaFishNextAutoFeed) {
    if (creditLoRaFishMeal(animate)) fed = true;
    // Sleeping, egg/memorial and cooldown thresholds are consumed, not queued.
    loRaFishNextAutoFeed += LORAFISH_PACKETS_PER_FEED;
  }
  return fed;
}

// Tamagotchi-style tank hygiene.
static uint8_t loRaFishPoopCount = 0;
static unsigned long loRaFishNextPoopAt = 0;
static bool loRaFishCleaning = false;
static unsigned long loRaFishCleaningStarted = 0;
static constexpr uint8_t LORAFISH_MAX_POOP = 3;
static constexpr unsigned long LORAFISH_CLEAN_MS = 1400UL;

static void scheduleLoRaFishPoop() {
  // 30-90 seconds after a meal. A pending poop is not rescheduled by another meal.
  if (loRaFishNextPoopAt == 0 && loRaFishPoopCount < LORAFISH_MAX_POOP)
    loRaFishNextPoopAt = millis() + 30000UL + (random(60001L));
}

static bool tickLoRaFishPoop() {
  bool cleaned = false;
  if (loRaFishNextPoopAt && (long)(millis() - loRaFishNextPoopAt) >= 0) {
    if (loRaFishPoopCount < LORAFISH_MAX_POOP) loRaFishPoopCount++;
    loRaFishNextPoopAt = 0;
    saveLoRaFishSettings();
  }
  if (loRaFishCleaning && millis() - loRaFishCleaningStarted >= LORAFISH_CLEAN_MS) {
    loRaFishCleaning = false;
    loRaFishPoopCount = 0;
    loRaFishNextPoopAt = 0;
    fish.happiness = min((int)100, (int)fish.happiness + 3);
    saveLoRaFishSettings();
    saveLoRaFish();
    cleaned = true;
  }
  return cleaned;
}

static void startLoRaFishCleaning() {
  if (!loRaFishCleaning && loRaFishPoopCount > 0) {
    loRaFishCleaning = true;
    loRaFishCleaningStarted = millis();
  }
}

static void startLoRaFishFeeding() {
  // Presentation only. Meal state is credited by creditLoRaFishMeal().
  if (!loRaFishFeeding) {
    loRaFishFeeding = true;
    loRaFishFeedChasing = false;
    loRaFishFeedLastXValid = false;
    loRaFishFeedStarted = millis();
  }
}

static void resetLoRaFishPet() {
  // Recreate the initial persisted pet state.
  loRaFishAdoptionEligible = true;
  fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0};
  loRaFishHappiness.resetCare();
  loRaFishHappyLastMs = millis();
  fishReaction = 0;
  fishReactionUntil = 0;
  loRaFishFeeding = false;
  loRaFishFeedChasing = false;
  loRaFishFeedLastXValid = false;
  loRaFishFeedStarted = 0;
  loRaFishEvolutionFlashUntil = 0;
  loRaFishNextAutoFeed = LORAFISH_PACKETS_PER_FEED;
  loRaFishPoopCount = 0;
  loRaFishNextPoopAt = 0;
  loRaFishCleaning = false;
  loRaFishGeneration = 1;
  loRaFishSpecies=loRaFishEggSpecies=LoRaFishSpecies::freshSpecies;
  loRaFishMysterySeed=0; loRaFishNeedsPetSetup=(LORAGOTCHI_SPECIES_EDITION!=0); loRaFishPetSetupField=0;
  loRaFishPendingEgg = false;
  loRaFishMemorialEggSide = 0;
  loRaFishNeedsAppearanceSetup=loRaFishColorDisplay;
  if (!loRaFishColorDisplay) {
    loRaFishColor=LORAFISH_MONOCHROME_COLOR;
    loRaFishScaleColor=LORAFISH_MONOCHROME_COLOR;
    loRaFishEyeColor=LORAFISH_MONOCHROME_COLOR;
  }
  loRaFishSyncDXAppearance();
  if (loRaFishNeedsAppearanceSetup) {
    loRaFishColorField=0;
    loRaFishPage=9;
  }
  saveLoRaFishSettings();
  fishLastMinute = millis();
  fishLastSave = millis();
  saveLoRaFish();
}

static uint8_t inheritLoRaFishColor(uint8_t localColor,uint8_t visitorColor) {
  return LoRaFishColour::inherit(localColor, visitorColor, (unsigned)random(0,241));
}

static bool breedLoRaFishWithGuest() {
  if (!loRaFishCanBreed()) return false;
  const LoRaFishAppearance local=loRaFishLocalAppearance();
  const LoRaFishAppearance visitor=loRaFishGuestAppearance();
  loRaFishEggAppearance.scales=inheritLoRaFishColor(local.scales,visitor.scales);
  loRaFishEggAppearance.body=inheritLoRaFishColor(local.body,visitor.body);
  loRaFishEggAppearance.eyes=inheritLoRaFishColor(local.eyes,visitor.eyes);
  loRaFishEggSpecies=random(0,2) ? loragotchiDXGuest.species : loRaFishSpecies;
  loRaFishPendingEgg=true;
  loRaFishMemorialEggSide=0;
  saveLoRaFishSettings();
  return true;
}

// LoRaFish uses MeshCore's RTC as its source of truth. If MeshCore has not
// acquired a plausible epoch yet, retain the old build-time + uptime fallback.
static uint32_t buildSecondsOfDay() {
  int hh=0, mm=0, ss=0;
  sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);
  return (uint32_t)hh*3600UL + (uint32_t)mm*60UL + (uint32_t)ss;
}

static bool loRaFishMeshTimeValid(uint32_t epoch) {
  // 2024-01-01 UTC. Avoid treating an unset/near-zero RTC as real time.
  return epoch >= 1704067200UL;
}

static int64_t loRaFishLocalEpoch() {
  const uint32_t epoch = loRaFishRTC ? loRaFishRTC->getCurrentTime() : 0;
  const int64_t baseEpoch = loRaFishMeshTimeValid(epoch) ? epoch
      : (int64_t)DateTime(__DATE__,__TIME__).unixtime() + millis()/1000UL;
  const int64_t localEpoch = baseEpoch + (int32_t)loRaFishMeckOffsetQuarters()*900L;
  return localEpoch < 0 ? 0 : localEpoch;
}

static bool loRaFishClock(char* timeBuf, size_t timeLen,
                          char* dateBuf, size_t dateLen) {
  const bool meshTimeValid = loRaFishMeshTimeValid(loRaFishRTC ? loRaFishRTC->getCurrentTime() : 0);
  const int64_t localEpoch = loRaFishLocalEpoch();
  DateTime dt((uint32_t)localEpoch);
  static const char* mon[]={"Jan","Feb","Mar","Apr","May","Jun",
                            "Jul","Aug","Sep","Oct","Nov","Dec"};
  snprintf(timeBuf,timeLen,"%02u:%02u",dt.hour(),dt.minute());
  snprintf(dateBuf,dateLen,"%02u %s %04u",dt.day(),mon[dt.month()-1],dt.year());
  return meshTimeValid;
}

static void loRaFishFormatUtcOffset(char* text,size_t textLen,bool includeUtc=true) {
  const int minutes=(int)loRaFishMeckOffsetQuarters()*15;
  const char sign=minutes<0 ? '-' : '+';
  const int absoluteMinutes=abs(minutes);
  if (includeUtc)
    snprintf(text,textLen,"UTC%c%02d:%02d",sign,absoluteMinutes/60,absoluteMinutes%60);
  else
    snprintf(text,textLen,"%c%02d:%02d",sign,absoluteMinutes/60,absoluteMinutes%60);
}

static uint8_t fishEvolutionForAge(uint32_t mins) {
  // evolution still describes the fish after hatching; egg is age-driven.
  if (mins < 60) return 0;             // Fry
  if (mins < 6UL*60UL) return 1;       // Young
  if (mins < 3UL*24UL*60UL) return 2;  // Adult
  return 3;                            // Elder
}

static const char* fishStageName() {
  switch (fish.evolution) {
    case 0: return "FRY";
    case 1: return "YOUNG";
    case 2: return "ADULT";
    default: return "ELDER";
  }
}

// Preserve the 24-byte settings prefix and 12-byte HAP1 tail. AQU1 is an
// optional eight-byte extension. HUE1 appends ten bytes of finer colour genes;
// legacy readers keep the nearest-palette values in the original prefix.
static constexpr uint32_t LORAFISH_AQUARIUM_SETTINGS_MAGIC = 0x31555141UL; // "AQU1"
static constexpr size_t LORAFISH_SETTINGS_SIZE = 78;
static constexpr uint32_t LORAFISH_CHECKPOINT_MS = 5UL * 60000UL;
static constexpr uint32_t LORAFISH_SAVE_RETRY_MS = 5000UL;
static uint8_t lastWrittenLoRaFishSettings[LORAFISH_SETTINGS_SIZE];
static bool haveLastWrittenLoRaFishSettings = false;
static bool loRaFishSettingsSaveFailed = false;
static bool loRaFishSaveFailed = false;
static uint32_t loRaFishSettingsLastAttempt = 0;
static uint32_t loRaFishLastAttempt = 0;

static void encodeLoRaFishSettings(uint8_t* data) {
  const uint8_t prefix[] = {
      loRaFishTimeoutAwakeSetting, loRaFishPoopCount, loRaFishTheme, LoRaFishColour::fallback(loRaFishColor),
      loRaFishSleepHour, loRaFishWakeHour, (uint8_t)loRaFishUtcOffsetQuarters,
      loRaFishTimeoutAsleepSetting, loRaFishBrightnessSetting, loRaFishAlwaysOnDimSetting,
      loRaFishBuzzerVolume, loRaFishWakeSound, loRaFishSleepSound, loRaFishMessageSound,
      LoRaFishColour::fallback(loRaFishScaleColor), LoRaFishColour::fallback(loRaFishEyeColor)};
  memcpy(data, prefix, sizeof(prefix));
  memcpy(data + 16, &loRaFishGeneration, 2);
  data[18] = loRaFishPendingEgg ? 1 : 0;
  data[19] = LoRaFishColour::fallback(loRaFishEggAppearance.scales);
  data[20] = LoRaFishColour::fallback(loRaFishEggAppearance.body);
  data[21] = LoRaFishColour::fallback(loRaFishEggAppearance.eyes);
  data[22] = loRaFishNeedsAppearanceSetup ? 1 : 0;
  data[23] = loRaFishSimulationMode ? 1 : 0;
  memcpy(data + 24, &LORAFISH_HAPPY_SETTINGS_MAGIC, 4);
  memcpy(data + 28, &loRaFishHappiness.awakeMs, 4);
  memcpy(data + 32, &loRaFishHappiness.mealCooldownMs, 4);
  memcpy(data + 36, &LORAFISH_AQUARIUM_SETTINGS_MAGIC, 4);
  memcpy(data + 40, &loRaFishHappiness.aquariumCooldownMs, 4);
  memcpy(data + 44, "HUE1", 4);
  data[48]=loRaFishColor; data[49]=loRaFishScaleColor; data[50]=loRaFishEyeColor;
  data[51]=loRaFishEggAppearance.body; data[52]=loRaFishEggAppearance.scales;
  data[53]=loRaFishEggAppearance.eyes;
  memcpy(data + 54, "MON1", 4);
  data[58]=loRaFishMonoDisplayColour;
  data[59]=0;
  LoRaFishSpecies::encode(data+60,loRaFishSpecies,loRaFishEggSpecies,loRaFishMysterySeed,loRaFishNeedsPetSetup);
}

static const char* loRaFishRecordKey(const char* path) {
  if (!strcmp(path,"/lorafish.cfg")) return "cfg";
  if (!strcmp(path,"/lfegg0.dat")) return "egg0";
  if (!strcmp(path,"/lfegg1.dat")) return "egg1";
  return "state";
}

static bool writeLoRaFishRecord(const char* path, const uint8_t* data, size_t size) {
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) return false;
  bool complete = f.write(data, size) == size;
  f.close(); // These APIs return void: byte-count success is not power-loss atomicity.
  return complete;
}

static void saveLoRaFishSettings() {
  const uint32_t now = millis();
  if (loRaFishSettingsSaveFailed &&
      now - loRaFishSettingsLastAttempt < LORAFISH_SAVE_RETRY_MS) return;
  uint8_t data[LORAFISH_SETTINGS_SIZE];
  encodeLoRaFishSettings(data);
  if (!loRaFishSettingsSaveFailed && haveLastWrittenLoRaFishSettings &&
      memcmp(data, lastWrittenLoRaFishSettings, sizeof(data)) == 0) {
    loRaFishHappyLastSavedMs = now;
    return;
  }
  loRaFishSettingsLastAttempt = now;
  if (!writeLoRaFishRecord("/lorafish.cfg", data, sizeof(data))) {
    loRaFishSettingsSaveFailed = true;
    return;
  }
  memcpy(lastWrittenLoRaFishSettings, data, sizeof(data));
  haveLastWrittenLoRaFishSettings = true;
  loRaFishSettingsSaveFailed = false;
  lastWrittenLoRaFishHappiness = loRaFishHappiness;
  haveLastWrittenLoRaFishHappiness = true;
  loRaFishHappyLastSavedMs = now;
}

static void loadLoRaFishSettings() {
  // Only a genuinely fresh game starts with the edition default. Established
  // legacy DAT saves still default to fish when SPC1 is absent/corrupt.
  loRaFishSpecies=loRaFishEggSpecies=loRaFishNeedsPetSetup ?
      LoRaFishSpecies::freshSpecies : LoRaFishSpecies::TropicalFish;
  loRaFishMysterySeed=0;
  haveLastWrittenLoRaFishSettings = false;
  loRaFishSettingsSaveFailed = false;
  loRaFishHappiness = {};
  haveLastWrittenLoRaFishHappiness = false;
  loRaFishHappyLastMs = loRaFishHappyLastSavedMs = millis();
  File f = SPIFFS.open("/lorafish.cfg", FILE_READ);
  if (f) {
    uint8_t timeout=0;
    const bool hasTimeout=f.read(&timeout,1)==1;
    uint8_t poop=0;
    if (f.read(&poop,1)==1 && poop<=LORAFISH_MAX_POOP) loRaFishPoopCount=poop;
    uint8_t theme=0;
    if (f.read(&theme,1)==1 && theme<loRaFishThemeCount()) loRaFishTheme=theme;
    uint8_t color=0;
    if (f.read(&color,1)==1 && color<=LORAFISH_MONOCHROME_COLOR) loRaFishColor=color;
    loRaFishScaleColor=loRaFishColor;
    loRaFishEyeColor=loRaFishColor;
    uint8_t sleepHour=0;
    if (f.read(&sleepHour,1)==1 && sleepHour<24) loRaFishSleepHour=sleepHour;
    uint8_t wakeHour=0;
    if (f.read(&wakeHour,1)==1 && wakeHour<24) loRaFishWakeHour=wakeHour;
    int8_t utcOffset=0;
    if (f.read((uint8_t*)&utcOffset,1)==1 && utcOffset>=-48 && utcOffset<=56)
      loRaFishUtcOffsetQuarters=utcOffset;
    uint8_t asleepTimeout=0;
    if (f.read(&asleepTimeout,1)==1) {
      if (timeout<=5) loRaFishTimeoutAwakeSetting=timeout;
      if (asleepTimeout<=5) loRaFishTimeoutAsleepSetting=asleepTimeout;
    } else if (hasTimeout && timeout<=3) {
      loRaFishTimeoutAwakeSetting=timeout==3 ? 5 : timeout;
      loRaFishTimeoutAsleepSetting=loRaFishTimeoutAwakeSetting;
    }
    uint8_t brightness=0;
    if (f.read(&brightness,1)==1 && brightness<4) loRaFishBrightnessSetting=brightness;
    uint8_t dimSetting=0;
    if (f.read(&dimSetting,1)==1 && dimSetting<3) loRaFishAlwaysOnDimSetting=dimSetting;
    uint8_t volume=0;
    if (f.read(&volume,1)==1 && volume<5) loRaFishBuzzerVolume=volume;
    uint8_t wakeSound=0;
    if (f.read(&wakeSound,1)==1 && wakeSound<4) loRaFishWakeSound=wakeSound;
    uint8_t sleepSound=0;
    if (f.read(&sleepSound,1)==1 && sleepSound<3) loRaFishSleepSound=sleepSound;
    uint8_t messageSound=0;
    if (f.read(&messageSound,1)==1 && messageSound<4) loRaFishMessageSound=messageSound;
    uint8_t traitColor=loRaFishColor;
    const bool hasScaleTrait=f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR;
    if (hasScaleTrait) loRaFishScaleColor=traitColor;
    traitColor=loRaFishColor;
    const bool hasEyeTrait=f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR;
    if (hasEyeTrait) loRaFishEyeColor=traitColor;
    loRaFishAppearanceLoaded=hasScaleTrait && hasEyeTrait;
    if (!hasScaleTrait || !hasEyeTrait) {
      if (!loRaFishColorDisplay) {
        loRaFishColor=LORAFISH_MONOCHROME_COLOR;
        loRaFishScaleColor=LORAFISH_MONOCHROME_COLOR;
        loRaFishEyeColor=LORAFISH_MONOCHROME_COLOR;
      } else {
        loRaFishScaleColor=loRaFishColor;
        loRaFishEyeColor=loRaFishColor;
      }
    }
    uint16_t generation=1;
    if (f.read((uint8_t*)&generation,sizeof(generation))==sizeof(generation) && generation>0)
      loRaFishGeneration=generation;
    uint8_t hasEgg=0;
    if (f.read(&hasEgg,1)==1) loRaFishPendingEgg=hasEgg!=0;
    traitColor=loRaFishColor;
    if (f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR)
      loRaFishEggAppearance.scales=traitColor;
    traitColor=loRaFishColor;
    if (f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR)
      loRaFishEggAppearance.body=traitColor;
    traitColor=loRaFishColor;
    if (f.read(&traitColor,1)==1 && traitColor<=LORAFISH_MONOCHROME_COLOR)
      loRaFishEggAppearance.eyes=traitColor;
    uint8_t appearanceSetup=0;
    if (f.read(&appearanceSetup,1)==1) loRaFishNeedsAppearanceSetup=appearanceSetup!=0;
    uint8_t simulationMode=0;
    if (f.read(&simulationMode,1)==1) loRaFishSimulationMode=simulationMode!=0;
    uint32_t happyMagic = 0;
    LoRaFishHappiness happy;
    const bool hasHappyMagic = f.read((uint8_t*)&happyMagic, sizeof(happyMagic)) == sizeof(happyMagic);
    const bool hasAwakeMs = f.read((uint8_t*)&happy.awakeMs, sizeof(happy.awakeMs)) == sizeof(happy.awakeMs);
    const bool hasMealMs = f.read((uint8_t*)&happy.mealCooldownMs, sizeof(happy.mealCooldownMs)) == sizeof(happy.mealCooldownMs);
    if (hasHappyMagic && hasAwakeMs && hasMealMs &&
        happyMagic == LORAFISH_HAPPY_SETTINGS_MAGIC &&
        happy.valid()) {
      loRaFishHappiness = happy;
      lastWrittenLoRaFishHappiness = happy;
      haveLastWrittenLoRaFishHappiness = true;
    }
    uint32_t aquariumMagic = 0, aquariumCooldown = 0;
    if (f.size() > 36) loRaFishHappiness.aquariumCooldownMs = LoRaFishHappiness::aquariumMs;
    if (f.read((uint8_t*)&aquariumMagic, sizeof(aquariumMagic)) == sizeof(aquariumMagic) &&
        aquariumMagic == LORAFISH_AQUARIUM_SETTINGS_MAGIC) {
      // A recognized but damaged new tail must not shorten the reward cap.
      loRaFishHappiness.aquariumCooldownMs = LoRaFishHappiness::aquariumMs;
      if (f.read((uint8_t*)&aquariumCooldown, sizeof(aquariumCooldown)) == sizeof(aquariumCooldown) &&
          aquariumCooldown <= LoRaFishHappiness::aquariumMs)
        loRaFishHappiness.aquariumCooldownMs = aquariumCooldown;
    }
    uint8_t hues[10] = {};
    if (f.read(hues, sizeof(hues)) == sizeof(hues) && !memcmp(hues, "HUE1", 4)) {
      bool valid = true;
      for (unsigned i=4; i<10; ++i) valid = valid && LoRaFishColour::valid(hues[i]);
      if (valid) {
        loRaFishColor=hues[4]; loRaFishScaleColor=hues[5]; loRaFishEyeColor=hues[6];
        loRaFishEggAppearance={hues[8],hues[7],hues[9]};
      }
    }
    uint8_t mono[6] = {};
    if (f.read(mono, sizeof(mono)) == sizeof(mono) && !memcmp(mono, "MON1", 4) &&
        mono[4] < LORAFISH_MONO_DISPLAY_COLOUR_COUNT) {
      loRaFishMonoDisplayColour = mono[4];
    }
    uint8_t speciesTail[LoRaFishSpecies::savedSize]={};
    if (f.read(speciesTail,sizeof(speciesTail))==sizeof(speciesTail))
      LoRaFishSpecies::decode(speciesTail,sizeof(speciesTail),loRaFishSpecies,
          loRaFishEggSpecies,loRaFishMysterySeed,loRaFishNeedsPetSetup);
    if (!LORAGOTCHI_SPECIES_EDITION) loRaFishNeedsPetSetup=false;
    if (loRaFishNeedsPetSetup && !loRaFishAdoptionEligible) loRaFishPage=LORAFISH_PET_SETUP_PAGE;
    lastWrittenLoRaFishHappiness = loRaFishHappiness;
    uint8_t canonical[LORAFISH_SETTINGS_SIZE];
    encodeLoRaFishSettings(canonical);
    if (f.size() == sizeof(canonical) && f.seek(0) &&
        f.read(lastWrittenLoRaFishSettings, sizeof(canonical)) == sizeof(canonical)) {
      haveLastWrittenLoRaFishSettings =
          memcmp(canonical, lastWrittenLoRaFishSettings, sizeof(canonical)) == 0;
    }
    f.close();
  }
}

static bool sameLoRaFishRecord(const LoRaFishState& a, const LoRaFishState& b) {
  return a.magic == b.magic && a.ageMinutes == b.ageMinutes &&
         a.rfEvents == b.rfEvents && a.messages == b.messages &&
         a.txAcks == b.txAcks && a.happiness == b.happiness &&
         a.evolution == b.evolution && a.dxVisits == b.dxVisits &&
         a.longestUptimeMinutes == b.longestUptimeMinutes;
}

static LoRaFishState lastWrittenLoRaFish;
static bool haveLastWrittenLoRaFish = false;

static void saveLoRaFish() {
  // Care/transitions call this immediately; routine counters use a fixed maximum
  // checkpoint age. Only elapsed Happy timing waits for the five-minute checkpoint.
  const bool timingChanged = !loRaFishHappiness.same(lastWrittenLoRaFishHappiness);
  if (!haveLastWrittenLoRaFishHappiness || (timingChanged &&
      (loRaFishHappiness.mealCooldownMs > lastWrittenLoRaFishHappiness.mealCooldownMs ||
       loRaFishHappiness.aquariumCooldownMs > lastWrittenLoRaFishHappiness.aquariumCooldownMs ||
       loRaFishHappiness.awakeMs < lastWrittenLoRaFishHappiness.awakeMs ||
       (uint32_t)(millis() - loRaFishHappyLastSavedMs) >= 5UL*60000UL)))
    saveLoRaFishSettings();
  const uint32_t now = millis();
  if (loRaFishSaveFailed && now - loRaFishLastAttempt < LORAFISH_SAVE_RETRY_MS) return;
  if (!loRaFishSaveFailed && haveLastWrittenLoRaFish &&
      sameLoRaFishRecord(lastWrittenLoRaFish, fish)) {
    fishLastSave = now;
    return;
  }
  loRaFishLastAttempt = now;
  if (!writeLoRaFishRecord("/lorafish.dat", (const uint8_t*)&fish, sizeof(fish))) {
    loRaFishSaveFailed = true;
    return;
  }
  loRaFishSaveFailed = false;
  lastWrittenLoRaFish = fish;
  haveLastWrittenLoRaFish = true;
  fishLastSave = now;
}

static void loadLoRaFish() {
  haveLastWrittenLoRaFish = false;
  loRaFishSaveFailed = false;
  bool restored=false;
  File f = SPIFFS.open("/lorafish.dat", FILE_READ);
  if (f && f.size() == sizeof(LoRaFishStateV1)) {
    LoRaFishStateV1 old;
    if (f.read((uint8_t*)&old, sizeof(old)) == sizeof(old) &&
        old.magic == LORAFISH_MAGIC_V1) {
      fish = {LORAFISH_MAGIC, old.ageMinutes, old.rfEvents, old.messages,
              old.txAcks, old.happiness, old.evolution, 0, 0};
      restored=true;
      Serial.println("[LFG2] migrated LFM1 fish record");
    }
    f.close();
  } else if (f && f.size() == sizeof(fish)) {
    LoRaFishState tmp;
    if (f.read((uint8_t*)&tmp, sizeof(tmp)) == sizeof(tmp) &&
        tmp.magic == LORAFISH_MAGIC) {
      fish = tmp;
      lastWrittenLoRaFish = tmp;
      haveLastWrittenLoRaFish = true;
      restored=true;
    }
    f.close();
  }
  loRaFishNeedsPetSetup=(LORAGOTCHI_SPECIES_EDITION!=0) && !restored; loRaFishPetSetupField=0;
  loRaFishAdoptionEligible=!restored;
  loRaFishNeedsAppearanceSetup=!restored && loRaFishColorDisplay;
  if (loRaFishNeedsAppearanceSetup) {
    loRaFishPage=9;
    loRaFishColorField=0;
  }
  fish.evolution = fishEvolutionForAge(fish.ageMinutes);
  Serial.printf("[LFG2] loaded restored=%d age=%lu visits=%lu longestUp=%lu\n",
                restored ? 1 : 0,(unsigned long)fish.ageMinutes,
                (unsigned long)fish.dxVisits,(unsigned long)fish.longestUptimeMinutes);
  fishLastMinute = millis();
  fishLastSave = millis();
  loRaFishNextAutoFeed =
      ((fish.rfEvents / LORAFISH_PACKETS_PER_FEED) + 1) * LORAFISH_PACKETS_PER_FEED;
}

#include "LoRaFishAdoption.h"

static void ensureLoRaFishOffspring() {
  if (fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES || loRaFishPendingEgg) return;
  // Self-fertilise only when no visitor-bred egg exists. With a single parent,
  // all three inherited traits are its own, including Mono.
  loRaFishEggSpecies=loRaFishSpecies;
  loRaFishEggAppearance=loRaFishLocalAppearance();
  loRaFishPendingEgg=true;
  loRaFishMemorialEggSide=0;
  saveLoRaFishSettings();
}

static void hatchLoRaFishOffspring() {
  if (!loRaFishPendingEgg) return;
  // DX visits and uptime records belong to the device, so they survive a hatch.
  const uint32_t keepVisits = fish.dxVisits;
  const uint32_t keepUptime = fish.longestUptimeMinutes;
  fish = {LORAFISH_MAGIC, 0, 0, 0, 0, 80, 0, keepVisits, keepUptime};
  loRaFishHappiness.resetCare();
  loRaFishHappyLastMs = millis();
  loRaFishSpecies=loRaFishEggSpecies;
  loRaFishColor=loRaFishEggAppearance.body;
  loRaFishScaleColor=loRaFishEggAppearance.scales;
  loRaFishEyeColor=loRaFishEggAppearance.eyes;
  if (loRaFishGeneration<65535) loRaFishGeneration++;
  loRaFishPendingEgg=false;
  loRaFishMemorialEggSide=0;
  loRaFishPoopCount=0;
  loRaFishNextPoopAt=0;
  loRaFishCleaning=false;
  loRaFishFeeding=false;
  loRaFishFeedChasing=false;
  loRaFishFeedLastXValid=false;
  loRaFishEvolutionFlashUntil=0;
  loRaFishHatchMelodyPlayed=false;
  loRaFishNextAutoFeed=LORAFISH_PACKETS_PER_FEED;
  fishLastMinute=millis();
  fishLastSave=millis();
  loRaFishSyncDXAppearance();
  saveLoRaFishSettings();
  saveLoRaFish();
}

static void advanceLoRaFishHappiness(uint32_t until) {
  const uint32_t elapsed = until - loRaFishHappyLastMs;
  if (!elapsed) return;
  const uint32_t now = millis();
  const uint32_t localNowMs = (uint32_t)(loRaFishLocalEpoch() % 86400) * 1000UL + now % 1000UL;
  const uint32_t behind = now - until;
  const uint32_t localEndMs = (localNowMs + LoRaFishHappiness::dayMs -
      behind % LoRaFishHappiness::dayMs) % LoRaFishHappiness::dayMs;
  const bool living = !loRaFishNeedsAppearanceSetup && !loRaFishNeedsPetSetup &&
      fish.ageMinutes >= LORAFISH_EGG_MINUTES && fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES;
  loRaFishHappiness.advance(elapsed, living, localEndMs, loRaFishSleepHour,
                           loRaFishWakeHour, loRaFishPoopCount, fish.happiness);
  loRaFishHappyLastMs = until;
}

static void checkpointLoRaFish(uint32_t now) {
  if (loRaFishSettingsSaveFailed ||
      now - loRaFishHappyLastSavedMs >= LORAFISH_CHECKPOINT_MS) saveLoRaFishSettings();
  if (loRaFishSaveFailed || now - fishLastSave >= LORAFISH_CHECKPOINT_MS) saveLoRaFish();
}

// Only LoRagotchi-controlled shutdown/restart. Shared MeshCore reboot paths stay unchanged.
static void flushLoRaFish() {
  tickLoRaFish();
  // One final attempt even if a previous failure is inside the retry backoff.
  loRaFishSettingsLastAttempt = loRaFishLastAttempt = millis() - LORAFISH_SAVE_RETRY_MS;
  saveLoRaFishSettings();
  saveLoRaFish();
}

static void tickLoRaFish() {
  const uint32_t now = millis();
  const uint8_t previousHappiness = fish.happiness;
  if (loRaFishNeedsAppearanceSetup || loRaFishAdoptionEligible || loRaFishNeedsPetSetup) {
    advanceLoRaFishHappiness(now);
    fishLastMinute=now;
    checkpointLoRaFish(now);
    return;
  }
  const uint32_t uptimeMinutes = now / 60000UL;
  if (uptimeMinutes > fish.longestUptimeMinutes) fish.longestUptimeMinutes = uptimeMinutes;
  // Also recover pets saved on the memorial by firmware without the fallback.
  ensureLoRaFishOffspring();
  while (now - fishLastMinute >= 60000UL) {
    fishLastMinute += 60000UL;
    advanceLoRaFishHappiness(fishLastMinute);
    fish.ageMinutes++;
    uint8_t previousEvolution=fish.evolution;
    fish.evolution = fishEvolutionForAge(fish.ageMinutes);
    if (fish.ageMinutes >= LORAFISH_EGG_MINUTES && fish.evolution != previousEvolution)
      loRaFishEvolutionFlashUntil=millis()+5000UL;
    if (fish.ageMinutes == LORAFISH_EGG_MINUTES) {
      loRaFishHatchMelodyPlayed = false; // UITask::loop will play it once.
    }
    ensureLoRaFishOffspring();
    if (loRaFishPendingEgg &&
        fish.ageMinutes >= LORAFISH_LIFESPAN_MINUTES + LORAFISH_EGG_MINUTES) {
      hatchLoRaFishOffspring();
      break;
    }
    // Persist real lifecycle boundaries immediately; ordinary catch-up saves
    // are coalesced after the loop, not one write per historical five minutes.
    if (fish.ageMinutes == LORAFISH_EGG_MINUTES ||
        fish.ageMinutes == LORAFISH_LIFESPAN_MINUTES ||
        fish.evolution != previousEvolution) saveLoRaFish();
  }
  advanceLoRaFishHappiness(now);
  if (fish.happiness != previousHappiness) saveLoRaFish();
  checkpointLoRaFish(now);
}

static void reactLoRaFish(uint8_t reaction) {
  fishReaction = reaction;
  fishReactionUntil = millis() + 2500UL;
  fish.rfEvents++;
}

// -----------------------------------------------------------------------------
// LoRaFish drawing compatibility helpers
//
// MeshCore's DisplayDriver intentionally exposes a smaller primitive API than
// the U8g2-style driver used by the standalone/roaming LoRaFish firmware.
// Build pixels and arbitrary lines from fillRect(), which DisplayDriver supports.
// -----------------------------------------------------------------------------
static inline void fishPixel(DisplayDriver& display, int x, int y) {
  display.fillRect(x, y, 1, 1);
}

static void fishLine(DisplayDriver& display, int x0, int y0, int x1, int y1) {
  // Integer Bresenham line. No heap allocation and fine for this tiny sprite.
  int dx = abs(x1 - x0);
  int sx = (x0 < x1) ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = (y0 < y1) ? 1 : -1;
  int err = dx + dy;

  while (true) {
    fishPixel(display, x0, y0);
    if (x0 == x1 && y0 == y1) break;

    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

// helper

static void maskAdultFishSprite(DisplayDriver& d, int x, int y, bool right) {
  d.setColor(s_fishMaskColor);   // Meck: body fill marker (see the top of the file)

  if (right) {
    // Fill the rounded body interior.
    for (int yy = -5; yy <= 5; yy++) {
      int inset = 0;

      if (yy == -5 || yy == 5) inset = 3;
      else if (yy == -4 || yy == 4) inset = 1;

      fishLine(d,
               x - 7 + inset, y + yy,
               x + 9 - inset, y + yy);
    }

    // Fill forked tail.
    for (int yy = -6; yy <= 6; yy++) {
      int ax = abs(yy);
      int left = x - 14;
      int rightEdge = x - 8 - (ax / 3);
      fishLine(d, left, y + yy, rightEdge, y + yy);
    }

    // Dorsal fin.
    for (int yy = -9; yy <= -6; yy++) {
      int half = yy + 9;
      fishLine(d,
               x - 1 - half,
               y + yy,
               x - 1 + half,
               y + yy);
    }

    // Lower fin.
    for (int yy = 6; yy <= 9; yy++) {
      int half = 9 - yy;
      fishLine(d,
               x - 1 - half,
               y + yy,
               x + 2 + half,
               y + yy);
    }

  } else {
    // Mirrored body.
    for (int yy = -5; yy <= 5; yy++) {
      int inset = 0;

      if (yy == -5 || yy == 5) inset = 3;
      else if (yy == -4 || yy == 4) inset = 1;

      fishLine(d,
               x - 9 + inset, y + yy,
               x + 7 - inset, y + yy);
    }

    // Mirrored tail.
    for (int yy = -6; yy <= 6; yy++) {
      int ax = abs(yy);
      int leftEdge = x + 8 + (ax / 3);
      fishLine(d,
               leftEdge, y + yy,
               x + 14, y + yy);
    }

    // Dorsal fin.
    for (int yy = -9; yy <= -6; yy++) {
      int half = yy + 9;
      fishLine(d,
               x + 1 - half,
               y + yy,
               x + 1 + half,
               y + yy);
    }

    // Lower fin.
    for (int yy = 6; yy <= 9; yy++) {
      int half = 9 - yy;
      fishLine(d,
               x - 2 - half,
               y + yy,
               x + 1 + half,
               y + yy);
    }
  }
}

static void drawAdultFishSprite(DisplayDriver& display, int x, int y, bool right,
                               const LoRaFishAppearance& appearance) {
  // V0.3.1: recognisable aquarium fish designed for the real monochrome OLED.
  // Approx. 29x17 pixels. Mostly outline/detail rather than one solid blob.
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  const ColorVal scalesColor=loRaFishColorValue(appearance.scales);
  const ColorVal eyesColor=loRaFishColorValue(appearance.eyes);
  display.setColor(UIColor::primary_txt);

    // Clear everything behind the actual fish silhouette first.
  maskAdultFishSprite(display, x, y, right);

  // Now draw the fish normally.
  display.setColor(bodyColor);

  if (right) {
    // Forked tail.
    fishLine(display, x-8, y,   x-14, y-6);
    fishLine(display, x-14,y-6, x-12, y);
    fishLine(display, x-12,y,   x-14, y+6);
    fishLine(display, x-14,y+6, x-8,  y+2);

    // Rounded body outline.
    fishLine(display, x-8,y-2, x-5,y-5);
    fishLine(display, x-5,y-5, x+3,y-6);
    fishLine(display, x+3,y-6, x+8,y-4);
    fishLine(display, x+8,y-4, x+11,y-1);
    fishLine(display, x+11,y-1,x+11,y+2);
    fishLine(display, x+11,y+2,x+7,y+5);
    fishLine(display, x+7,y+5, x-3,y+6);
    fishLine(display, x-3,y+6, x-8,y+2);

    // Head/gill gives the fish a face rather than a rectangle.
    fishLine(display, x+5,y-4, x+5,y+4);
    display.setColor(eyesColor);
    display.fillRect(x+7,y-1,2,2);

    // Scales / body texture.
    display.setColor(scalesColor);
    fishPixel(display, x-4,y-2);
    fishPixel(display, x-1,y-3);
    fishPixel(display, x+2,y-2);
    fishPixel(display, x-5,y+1);
    fishPixel(display, x-2,y+2);
    fishPixel(display, x+1,y+1);
    fishPixel(display, x+3,y+3);

    // Dorsal fin and lower fin.
    display.setColor(bodyColor);
    fishLine(display, x-4,y-5, x-1,y-9);
    fishLine(display, x-1,y-9, x+3,y-6);
    fishLine(display, x-1,y+6, x+2,y+9);
    fishLine(display, x+2,y+9, x+4,y+5);

    // Small mouth.
    fishPixel(display, x+12,y+1);

  } else {
    // Mirrored left-facing fish.
    fishLine(display, x+8, y,   x+14, y-6);
    fishLine(display, x+14,y-6, x+12, y);
    fishLine(display, x+12,y,   x+14, y+6);
    fishLine(display, x+14,y+6, x+8,  y+2);

    fishLine(display, x+8,y-2, x+5,y-5);
    fishLine(display, x+5,y-5, x-3,y-6);
    fishLine(display, x-3,y-6, x-8,y-4);
    fishLine(display, x-8,y-4, x-11,y-1);
    fishLine(display, x-11,y-1,x-11,y+2);
    fishLine(display, x-11,y+2,x-7,y+5);
    fishLine(display, x-7,y+5, x+3,y+6);
    fishLine(display, x+3,y+6, x+8,y+2);

    fishLine(display, x-5,y-4, x-5,y+4);
    display.setColor(eyesColor);
    display.fillRect(x-9,y-1,2,2);

    display.setColor(scalesColor);
    fishPixel(display, x+4,y-2);
    fishPixel(display, x+1,y-3);
    fishPixel(display, x-2,y-2);
    fishPixel(display, x+5,y+1);
    fishPixel(display, x+2,y+2);
    fishPixel(display, x-1,y+1);
    fishPixel(display, x-3,y+3);

    display.setColor(bodyColor);
    fishLine(display, x+4,y-5, x+1,y-9);
    fishLine(display, x+1,y-9, x-3,y-6);
    fishLine(display, x+1,y+6, x-2,y+9);
    fishLine(display, x-2,y+9, x-4,y+5);

    fishPixel(display, x-12,y+1);
  }

  display.setColor(UIColor::primary_txt);
}
// Blend RGB565 channels independently; weights are eighths of genetic colour.
// Presentation only: never modify the stored/wire alleles (including H1).
static ColorVal loRaFishJuvenileTint(uint8_t allele, unsigned weight) {
  const ColorVal color=loRaFishColorValue(allele);
  if (!LoRaFishColour::valid(allele) || allele==LORAFISH_MONOCHROME_COLOR)
    return color;
  const ColorVal ivory=0xFFBC;
  const unsigned r=(((color>>11)&31)*weight+((ivory>>11)&31)*(8-weight)+4)/8;
  const unsigned g=(((color>>5)&63)*weight+((ivory>>5)&63)*(8-weight)+4)/8;
  const unsigned b=((color&31)*weight+(ivory&31)*(8-weight)+4)/8;
  return (ColorVal)((r<<11)|(g<<5)|b);
}

static bool drawJuvenileColourSprite(DisplayDriver& d,int x,int y,bool right,
                                    bool young,const LoRaFishAppearance& appearance) {
  // Keep OLEDs, fully Mono fish, guest pixel-tint and the white return flash
  // on their existing rendering path. Eyes alone do not colour the body.
  const bool bodyGene=LoRaFishColour::valid(appearance.body) && appearance.body!=LORAFISH_MONOCHROME_COLOR;
  const bool scaleGene=LoRaFishColour::valid(appearance.scales) && appearance.scales!=LORAFISH_MONOCHROME_COLOR;
  const bool eyeGene=LoRaFishColour::valid(appearance.eyes) && appearance.eyes!=LORAFISH_MONOCHROME_COLOR;
  if (!loRaFishColorDisplay || appearance.body==0xFF ||
      (!bodyGene && !scaleGene && !eyeGene)) return false;

  const ColorVal body=loRaFishJuvenileTint(appearance.body,young ? 5 : 2);
  const ColorVal fins=loRaFishJuvenileTint(young ? appearance.scales : appearance.body,
                                         young ? 6 : 5);
  // Neutral ink surrounds pale fills even on Paper; a Mono black body instead
  // gets a light eye/border. Eye genetics first express in the adult sprite.
  const unsigned light=((body>>11)&31)*299*2+((body>>5)&63)*587+(body&31)*114*2;
  const ColorVal ink=light>=31500 ? 0x2104 : 0xDEFB;
  const int rear=young ? -6 : -3, tail=young ? -11 : -8;
  const int half=young ? 3 : 2, tailHalf=young ? 5 : 4;
  const int front=young ? 5 : 3;
  // All regions use the same body-local coordinates, including on turns.
  auto line=[&](int ax,int ay,int bx,int by) {
    fishLine(d,x+(right ? ax : -ax),y+ay,x+(right ? bx : -bx),y+by);
  };
  auto rect=[&](int ax,int ay,int w,int h) {
    d.fillRect(x+(right ? ax : -ax-w+1),y+ay,w,h);
  };
  d.setColor(UIColor::window_bkg);
  for (int i=-tailHalf;i<=tailHalf;++i) line(tail,i,rear,0);
  if (young) {
    for (int i=0;i<=3;++i) line(-1,-3,1,-6+i);
    for (int i=0;i<=2;++i) line(-1,3,1,5-i);
  }
  d.setColor(ink);   // Meck: a pale tint would break up in the greys
  line(rear,0,tail,-tailHalf);
  line(tail,-tailHalf,tail,tailHalf);
  line(tail,tailHalf,rear,0);
  if (young) {
    line(-1,-3,1,-6);
    line(-1,3,1,5);
  }
  d.setColor(body);
  rect(rear,-half,front-rear+1,half*2+1);
  rect(front+1,-1,2,3);
  d.setColor(ink);
  line(rear,-half,front,-half);
  line(rear,half,front,half);
  line(rear,-half,rear,half);
  line(front,-half,front,-1);
  line(front,1,front,half);
  line(front+1,-1,front+2,-1);
  line(front+1,1,front+2,1);
  line(front+2,-1,front+2,1);
  line(young ? 3 : 2,-1,young ? 3 : 2,-1);
  d.setColor(UIColor::primary_txt);
  return true;
}

static void drawFryFishSprite(DisplayDriver& d, int x, int y, bool right,
                             const LoRaFishAppearance& appearance) {
  if (drawJuvenileColourSprite(d,x,y,right,false,appearance)) return;
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  const ColorVal scalesColor=loRaFishColorValue(appearance.scales);
  const ColorVal eyesColor=loRaFishColorValue(appearance.eyes);
  // Opaque mask
  d.setColor(s_fishMaskColor);   // Meck: body fill marker (see the top of the file)

  if (right) {
    // Tail interior
    for (int i = 0; i <= 4; i++) {
      fishLine(d, x-8, y-i, x-3, y);
      fishLine(d, x-8, y+i, x-3, y);
    }

    // Body/head
    d.fillRect(x-3, y-2, 9, 5);

  } else {
    for (int i = 0; i <= 4; i++) {
      fishLine(d, x+8, y-i, x+3, y);
      fishLine(d, x+8, y+i, x+3, y);
    }

    d.fillRect(x-6, y-2, 9, 5);
  }


  
  d.setColor(bodyColor);

  // Tiny fry: small body with a proportionally large tail.
  if (right) {
    fishLine(d,x-3,y,   x-8,y-4);
    fishLine(d,x-3,y,   x-8,y+4);
    fishLine(d,x-8,y-4, x-8,y+4);
    d.fillRect(x-3,y-2,7,5);
    d.fillRect(x+4,y-1,2,3);
    d.setColor(scalesColor);
    fishPixel(d,x-1,y+1);
    d.setColor(eyesColor);
    fishPixel(d,x+2,y-1);
  } else {
    fishLine(d,x+3,y,   x+8,y-4);
    fishLine(d,x+3,y,   x+8,y+4);
    fishLine(d,x+8,y-4, x+8,y+4);
    d.fillRect(x-4,y-2,7,5);
    d.fillRect(x-6,y-1,2,3);
    d.setColor(scalesColor);
    fishPixel(d,x+1,y+1);
    d.setColor(eyesColor);
    fishPixel(d,x-3,y-1);
  }
}

static void drawYoungFishSprite(DisplayDriver& d, int x, int y, bool right,
                               const LoRaFishAppearance& appearance) {
  if (drawJuvenileColourSprite(d,x,y,right,true,appearance)) return;
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  const ColorVal scalesColor=loRaFishColorValue(appearance.scales);
  const ColorVal eyesColor=loRaFishColorValue(appearance.eyes);

 // Opaque mask
  d.setColor(s_fishMaskColor);   // Meck: body fill marker (see the top of the file)

  if (right) {
    // Tail
    for (int i = 0; i <= 5; i++) {
      fishLine(d, x-11, y-i, x-6, y);
      fishLine(d, x-11, y+i, x-6, y);
    }

    // Body/head
    d.fillRect(x-6, y-3, 14, 7);

    // Dorsal and lower fins
    for (int i = 0; i <= 3; i++)
      fishLine(d, x-1, y-3, x+1, y-6+i);

    for (int i = 0; i <= 2; i++)
      fishLine(d, x-1, y+3, x+1, y+5-i);

  } else {
    for (int i = 0; i <= 5; i++) {
      fishLine(d, x+11, y-i, x+6, y);
      fishLine(d, x+11, y+i, x+6, y);
    }

    d.fillRect(x-8, y-3, 14, 7);

    for (int i = 0; i <= 3; i++)
      fishLine(d, x+1, y-3, x-1, y-6+i);

    for (int i = 0; i <= 2; i++)
      fishLine(d, x+1, y+3, x-1, y+5-i);
  }



  d.setColor(bodyColor);

  // Juvenile: longer/slimmer than fry with its first proper dorsal fin.
  if (right) {
    fishLine(d,x-6,y,    x-11,y-5);
    fishLine(d,x-6,y,    x-11,y+5);
    fishLine(d,x-11,y-5, x-11,y+5);
    d.fillRect(x-6,y-3,12,7);
    d.fillRect(x+6,y-1,2,3);
    fishLine(d,x-1,y-3, x+1,y-6);
    fishLine(d,x-1,y+3, x+1,y+5);
    d.setColor(scalesColor);
    fishPixel(d,x-2,y+1);
    fishPixel(d,x+1,y+2);
    d.setColor(eyesColor);
    fishPixel(d,x+4,y-1);
  } else {
    fishLine(d,x+6,y,    x+11,y-5);
    fishLine(d,x+6,y,    x+11,y+5);
    fishLine(d,x+11,y-5, x+11,y+5);
    d.fillRect(x-6,y-3,12,7);
    d.fillRect(x-8,y-1,2,3);
    fishLine(d,x+1,y-3, x-1,y-6);
    fishLine(d,x+1,y+3, x-1,y+5);
    d.setColor(scalesColor);
    fishPixel(d,x+2,y+1);
    fishPixel(d,x-1,y+2);
    d.setColor(eyesColor);
    fishPixel(d,x-5,y-1);
  }
}

static void drawElderFishSprite(DisplayDriver& d, int x, int y, bool right,
                               const LoRaFishAppearance& appearance) {
  const ColorVal bodyColor=loRaFishColorValue(appearance.body);
  d.setColor(s_fishMaskColor);   // Meck: body fill marker (see the top of the file)

  if (right) {
    for (int i = 0; i < 3; i++) {
      fishLine(d,x-14,y-6+i, x-17,y-8+i);
      fishLine(d,x-14,y+6-i, x-17,y+8-i);
      fishLine(d,x-1+i,y-9,  x-3+i,y-11);
    }
  } else {
    for (int i = 0; i < 3; i++) {
      fishLine(d,x+14,y-6+i, x+17,y-8+i);
      fishLine(d,x+14,y+6-i, x+17,y+8-i);
      fishLine(d,x+1-i,y-9,  x+3-i,y-11);
    }
  }

  // Keep the known-good adult fish, then add longer flowing fins.
  drawAdultFishSprite(d,x,y,right,appearance);
  d.setColor(bodyColor);

  if (right) {
    fishLine(d,x-14,y-6, x-17,y-8);
    fishLine(d,x-14,y+6, x-17,y+8);
    fishLine(d,x-1,y-9,  x-3,y-11);
  } else {
    fishLine(d,x+14,y-6, x+17,y-8);
    fishLine(d,x+14,y+6, x+17,y+8);
    fishLine(d,x+1,y-9,  x+3,y-11);
  }
}

// Cardputer-only lightweight 2.5D effects. Other boards compile none of this.
// LilyGo T-Watch S3: on the tall aquarium canvas, draw the scenery over the
// whole tank with no rows moved (see TWatchS3TankView).
#define LORAFISH_SCENERY_CANVAS(d) (&(d) == &tank_display ? (DisplayDriver&)tank_scenery_display : (d))
static void drawCardputerFishDepth(DisplayDriver& d,int x,int y,bool right,uint8_t stage,
                                  const LoRaFishAppearance& appearance) {
  const int radius = stage==0 ? 5 : stage==1 ? 7 : stage==3 ? 11 : 9;
  const ColorVal base=stage<2 && loRaFishColorDisplay && appearance.body!=0xFF
      ? loRaFishJuvenileTint(stage==0 ? appearance.body : appearance.scales,stage==0 ? 5 : 6)
      : loRaFishColorValue(appearance.body);
  // A soft dark silhouette under the original sprite, preserving its distinctive details.
  d.setColor(0x0125);
  for(int yy=-radius/2;yy<=radius/2;yy++) {
    const int inset=abs(yy)/2;
    d.fillRect(x-radius+inset+1,y+yy+2,(radius-inset)*2,1);
  }
  // Light-catching upper contour and animated fin glint.
  d.setColor(0xCFFF);
  d.fillRect(x-radius/2,y-radius/2-1,radius,1);
  d.setColor(base);
  const int sway=(millis()/220UL)%3;
  d.fillRect(x+(right ? -radius-5 : radius+3),y-2+sway,2,3);
}

static void drawCardputerTankDepth(DisplayDriver& d) {
  const int w=d.width(),h=d.height();
  // Horizontal bands are deliberately cheap on the ESP32-S3's display driver.
  static const ColorVal water[]={0x010B,0x012D,0x014F,0x0170,0x0190,0x01B0,0x01D0,0x01EF};
  const int top=17,bottom=h-17,span=bottom-top;
  for(int i=0;i<8;i++) {
    int y0=top+(span*i)/8, y1=top+(span*(i+1))/8;
    d.setColor(water[i]); d.fillRect(0,y0,w,y1-y0);
  }
  // Surface shafts of light, offset in successive bands for depth.
  d.setColor(0x2B75);
  for(int i=0;i<5;i++) {
    int x=18+i*48;
    fishLine(d,x,18,x-13,39);
    fishLine(d,x+3,18,x-10,39);
  }
  // Far-off reef silhouettes.
  d.setColor(0x0147);
  for(int i=0;i<7;i++) {
    int x=i*43-12, y=h-31+(i%3)*3;
    d.fillRect(x,y,30,h-18-y);
    d.fillRect(x+8,y-5,12,6);
  }
  // Perspective sand layers and glints.
  d.setColor(0x6328); d.fillRect(0,h-23,w,6);
  d.setColor(0x9C4B); d.fillRect(0,h-18,w,3);
  d.setColor(0xDDAF); d.fillRect(0,h-15,w,1);
  // Parallax seaweed: stems sway gently with millis(), no buffers/allocations.
  for(int i=0;i<7;i++) {
    int x=8+i*37, base=h-24, tall=13+(i%3)*5;
    int sway=(int)((millis()/480UL+i*3)%5)-2;
    d.setColor((i%2)?0x03C8:0x05A8);
    fishLine(d,x,base,x+sway,base-tall);
    fishLine(d,x+2,base,x+5-sway,base-tall+5);
  }
  // Tiny rising particles add depth without expensive alpha blending.
  d.setColor(0x6DBB);
  for(int i=0;i<8;i++) {
    int x=9+i*29, y=20+((i*17+millis()/190UL)%(unsigned)(span>1?span:1));
    d.fillRect(x,y,1,1);
  }
}

static void drawFishSpriteForStage(DisplayDriver& d,int x,int y,bool right,uint8_t stage,
                                  const LoRaFishAppearance& appearance,uint16_t species=0) {
  s_fishMaskColor=(uint16_t)(FISH_MASK_BASE+fishMaskIndex(appearance.body));   // Meck: body fill marker
  species=LoRaFishSpecies::renderId(species);
  if (!LoRaFishSpecies::waterKnown(species)) species=0;
  if (species) {
    ColorVal body=loRaFishColorValue(appearance.body), fins=loRaFishColorValue(appearance.scales);
    ColorVal eyes=loRaFishColorValue(appearance.eyes);
    if (stage<2 && loRaFishColorDisplay && appearance.body!=0xFF) {
      body=loRaFishJuvenileTint(appearance.body,stage==0?2:5);
      fins=loRaFishJuvenileTint(stage==0?appearance.body:appearance.scales,stage==0?5:6);
    }
    // Meck: each part is drawn in a grey marker (see FISH_TONE_BASE), and the
    // eyes take whichever of ink or paper stands out on the body.
    const uint8_t bodyTone=fishSpriteTone(appearance.body,body);
    const uint8_t finsTone=fishSpriteTone(stage==0?appearance.body:appearance.scales,fins);
    (void)eyes;
    LoRaFishWaterSprite::draw(d,x,y,right,species,stage,(uint32_t)millis(),
        (ColorVal)(FISH_TONE_BASE+bodyTone),(ColorVal)(FISH_TONE_BASE+finsTone),
        (ColorVal)(FISH_TONE_BASE+(bodyTone<=2?4:0)),UIColor::window_bkg);
    d.setColor(UIColor::primary_txt); return;
  }
  drawCardputerFishDepth(d,x,y,right,stage,appearance);
  switch(stage) {
    case 0: drawFryFishSprite(d,x,y,right,appearance); break;
    case 1: drawYoungFishSprite(d,x,y,right,appearance); break;
    case 2: drawAdultFishSprite(d,x,y,right,appearance); break;
    default: drawElderFishSprite(d,x,y,right,appearance); break;
  }
}

static void loRaFishQueueTravelAnimation(LoRaFishTravelAnimation& animation,bool entering,
                                         uint8_t stage,const LoRaFishAppearance& appearance,uint16_t species=0) {
  animation.species=species;
  animation.pending=true;
  animation.active=false;
  animation.entering=entering;
  animation.stage=stage;
  animation.appearance=appearance;
}

static void loRaFishObserveTravelTransitions() {
  // Radio completion, not a sampled animation edge, counts the visit. ARRIVED
  // and RETURN can both be received between two UI ticks at a community tank.
  if (loragotchiDXHome.completedVisit) {
    loragotchiDXHome.completedVisit = false;
    if (loragotchiDXHome.communityVisit) {
      const bool living = !loRaFishNeedsAppearanceSetup && !loRaFishNeedsPetSetup &&
          fish.ageMinutes >= LORAFISH_EGG_MINUTES && fish.ageMinutes < LORAFISH_LIFESPAN_MINUTES;
      loRaFishHappiness.aquarium(living, fish.happiness);
    }
    loragotchiDXHome.communityVisit = false;
    fish.dxVisits++;
    saveLoRaFish();
    Serial.printf("[LFG2] DX visit complete, total=%lu\n",(unsigned long)fish.dxVisits);
  }
  const bool homeAway=loragotchiDXHome.away;
  const bool guestPresent=loragotchiDXGuest.present;
  const LoRaFishAppearance guestAppearance=loRaFishGuestRenderAppearance();

  if (!loRaFishTravelStateInitialized) {
    loRaFishLastHomeAway=homeAway;
    loRaFishLastGuestPresent=guestPresent;
    loRaFishLastGuestStage=loragotchiDXGuest.stage;
    loRaFishLastGuestSpecies=loragotchiDXGuest.species;
    if (guestPresent) loRaFishLastGuestAppearance=guestAppearance;
    loRaFishTravelStateInitialized=true;
    return;
  }

  if (homeAway!=loRaFishLastHomeAway) {
    loRaFishQueueTravelAnimation(loRaFishHomeTravelAnimation,!homeAway,fish.evolution,
                                 loRaFishLocalAppearance(),loRaFishSpecies);
    loRaFishLastHomeAway=homeAway;
  }

  if (guestPresent!=loRaFishLastGuestPresent) {
    loRaFishQueueTravelAnimation(loRaFishGuestTravelAnimation,guestPresent,
        guestPresent ? loragotchiDXGuest.stage : loRaFishLastGuestStage,
        guestPresent ? guestAppearance : loRaFishLastGuestAppearance,
        guestPresent ? loragotchiDXGuest.species : loRaFishLastGuestSpecies);
    loRaFishLastGuestPresent=guestPresent;
  }

  if (guestPresent) {
    loRaFishLastGuestStage=loragotchiDXGuest.stage;
    loRaFishLastGuestSpecies=loragotchiDXGuest.species;
    loRaFishLastGuestAppearance=guestAppearance;
  }
}

static void loRaFishBeginTravelAnimation(LoRaFishTravelAnimation& animation,int width,
                                         int targetX,int targetY) {
  if (!animation.pending) return;
  animation.pending=false;
  animation.active=true;
  animation.started=millis();
  animation.startX=animation.entering ? -20 : targetX;
  animation.endX=animation.entering ? targetX : width+20;
  animation.y=targetY;
}

static bool loRaFishDrawTravelAnimation(DisplayDriver& display,
                                        LoRaFishTravelAnimation& animation) {
  if (!animation.active) return false;
  const unsigned long elapsed=millis()-animation.started;
  if (elapsed>=LORAFISH_TRAVEL_ANIMATION_MS) {
    animation.active=false;
    return false;
  }
  const float progress=elapsed/(float)LORAFISH_TRAVEL_ANIMATION_MS;
  const int x=animation.startX+(int)((animation.endX-animation.startX)*progress);
  drawFishSpriteForStage(display,x,animation.y,true,animation.stage,animation.appearance,animation.species);
  return true;
}

static void drawFishSprite(DisplayDriver& d,int x,int y,bool right,
                           const LoRaFishAppearance& appearance) {
  drawFishSpriteForStage(d,x,y,right,fish.evolution,appearance,loRaFishSpecies);
}

static void drawHomeFishSprite(DisplayDriver& display,int x,int y,bool right) {
  if (loragotchiDXReturnedFromMonochrome) {
    loragotchiDXReturnedFromMonochrome=false;
    loRaFishReturnShakeUntil=millis()+2400UL;
  }

  LoRaFishAppearance appearance=loRaFishLocalAppearance();
  if (loRaFishColorDisplay && (long)(loRaFishReturnShakeUntil-millis())>0) {
    const unsigned long phase=millis();
    if ((phase/180UL)&1UL) appearance.body=0xFF;
    x += ((phase/70UL)&1UL) ? 2 : -2;
  }
  drawFishSprite(display,x,y,right,appearance);
}

static void drawLoRaFishHeader(DisplayDriver& display, const char* title,
                               const char* right) {
  if (s_noTankHeader) return;   // Meck: aquarium page, Meck's header is above the tank
  const int w=display.width();
  display.setColor(UIColor::primary_txt);
  display.setTextSize(1);
  display.setCursor(2,5);
  display.print(title);
  display.drawTextRightAlign(w-2,5,right);

  // Keep the separator visibly clear of the header glyphs.
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,15,w-4,1);
}

static bool loRaFishBatteryValid = false;
static uint32_t loRaFishBatterySampledAt = 0;
static uint16_t loRaFishBatteryMilliVolts = 0;

static uint16_t loRaFishDisplayBattery(UITask* task) {
  const uint32_t now = millis();
  if (!loRaFishBatteryValid || now - loRaFishBatterySampledAt >= 5000UL) {
    loRaFishBatteryMilliVolts = task->getBattMilliVolts();
    loRaFishBatterySampledAt = now;
    loRaFishBatteryValid = true;
  }
  return loRaFishBatteryMilliVolts;
}

// Compact battery icon immediately to the left of the aquarium clock.
// Uses the same 3.3-4.2 V percentage mapping as the RF monitor.
static void drawLoRaFishTankBattery(DisplayDriver& display, UITask* task,
                                   const char* clockText) {
  if (s_noTankHeader) return;   // Meck: aquarium page, Meck's header shows the battery
  if (!task) return;
  const int w = display.width();
  const int clockWidth = (int)strlen(clockText) * 6; // default text size 1
  const int x = w - 2 - clockWidth - 21;
  if (x < 0) return;
  const uint16_t mv = loRaFishDisplayBattery(task);
  const int pct = constrain(((int)mv - 3300) * 100 / 900, 0, 100);
  display.setColor(UIColor::primary_txt);
  display.drawRect(x, 5, 15, 8);
  display.fillRect(x + 15, 7, 2, 4);
  if (mv > 0) {
    const int fill = (pct * 11 + 50) / 100;
    if (fill > 0) display.fillRect(x + 2, 7, fill, 4);
  } else {
    display.setCursor(x + 5, 5);
    display.print("?");
  }
}

static void drawLoRaFishEggSprite(DisplayDriver& display,int centerX,int topY) {
  int wobble=((millis()/350UL)%3)-1;
  display.setColor(UIColor::primary_txt);
  display.drawRect(centerX-7+wobble,topY+3,14,14);
  display.fillRect(centerX-5+wobble,topY,10,2);
  display.fillRect(centerX-6+wobble,topY+2,12,1);
  display.fillRect(centerX-5+wobble,topY+17,10,2);
}

static void drawLoRaFishTankStatusBar(DisplayDriver& display,const char* stage,UITask* task) {
  char packetCount[8];
  const uint32_t packets=lorafish_rf_sample_seq;
  if (packets<10000UL) {
    snprintf(packetCount,sizeof(packetCount),"%lu",(unsigned long)packets);
  } else if (packets<1000000UL) {
    snprintf(packetCount,sizeof(packetCount),"%luk",(unsigned long)(packets/1000UL));
  } else {
    snprintf(packetCount,sizeof(packetCount),"%luM",(unsigned long)(packets/1000000UL));
  }
  const int unread=max(0,task->getMsgCount());
  const char* pendingEgg=loRaFishPendingEgg ? "0 " : "";
  char status[40];
  snprintf(status,sizeof(status),"%s%s PKT:%s UNR:%d",pendingEgg,stage,packetCount,unread);
  if (display.getTextWidth(status)>display.width()-4)
    snprintf(status,sizeof(status),"%s%s P:%s U:%d",pendingEgg,stage,packetCount,unread);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(display.width()/2,display.height()-9,status);
}

static void drawLoRaFishEgg(DisplayDriver& display,UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8],db[24]; loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);
  drawLoRaFishTankBattery(display,task,tb);
  drawLoRaFishEggSprite(display,w/2,24);
  display.fillRect(2,h-15,w-4,1);
  // LilyGo T-Watch S3: hatching countdown in the tall tank, above the footer line.
  {
    const long msLeft=(long)(LORAFISH_EGG_MINUTES-fish.ageMinutes)*60000L-(long)(millis()-fishLastMinute);
    if (msLeft>0) {
      const long secsLeft=(msLeft+999)/1000;
      char countdown[24];
      snprintf(countdown,sizeof(countdown),"HATCHING IN %ld:%02ld",secsLeft/60,secsLeft%60);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,h-25,countdown);
    }
  }
  drawLoRaFishTankStatusBar(display,"0",task);
}

static void drawLoRaFishMemorial(DisplayDriver& display) {
  const int w=display.width(), h=display.height();
  drawLoRaFishHeader(display,loRaFishDisplayName(),"REST");
  display.setColor(UIColor::primary_txt);
  if (loRaFishPendingEgg) {
    if (loRaFishMemorialEggSide==0) loRaFishMemorialEggSide=random(0,2) ? 1 : -1;
    const int stoneCenter=w/2-loRaFishMemorialEggSide*18;
    const int eggCenter=w/2+loRaFishMemorialEggSide*18;
    display.fillRect(stoneCenter-12,28,24,18);
    display.drawRect(stoneCenter-9,22,18,25);
    display.drawTextCentered(stoneCenter,29,"RIP");
    drawLoRaFishEggSprite(display,eggCenter,27);
  } else {
    display.fillRect(w/2-12,28,24,18);
    display.drawRect(w/2-9,22,18,25);
    display.drawTextCentered(w/2,29,"RIP");
  }
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,h-15,w-4,1);
  if (loRaFishPendingEgg) {
    char generation[24];
    const uint16_t nextGeneration=loRaFishGeneration<65535 ? loRaFishGeneration+1 : 65535;
    snprintf(generation,sizeof(generation),"GEN %u HATCHING",(unsigned)nextGeneration);
    display.drawTextCentered(w/2,h-9,generation);
  } else {
    display.drawTextCentered(w/2,h-9,"Long press reset in OPTIONS");
  }
}

static const char* loRaFishOptionLabel(LoRaFishOption option) {
  static const char* labels[] = {"Send Advert", "Guess Game", "Breed with visitor", "Reset Pet",
      "Display", "Sound", "Schedule and Time", "RF Monitor", "GNSS Monitor", "Send visitor home"};
  return labels[static_cast<uint8_t>(option)];
}

static void drawLoRaFishMenuHint(DisplayDriver& display) {
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(display.width()/2, 56, "ENTER:OK  Q:BACK");
}

static void drawLoRaFishValuePicker(DisplayDriver& display) {
  const int w = display.width();
  const uint8_t value = loRaFishValuePicker.value();
  const auto setting = loRaFishValuePicker.key();
  static const char* titles[] = {"FIRMWARE THEME", "BRIGHTNESS", "GAME RENDERING",
      "SCALES", "BODY / BORDER", "EYES", "SLEEP HOUR", "WAKE HOUR", "UTC OFFSET",
      "AWAKE TIMEOUT", "ASLEEP TIMEOUT", "DIM AFTER", "VOLUME", "WAKE ALARM",
      "SLEEP CUE", "MESSAGE TONE", "SCREEN COLOUR", "SPECIES"};
  char valueText[24];
  const char* text = valueText;
  switch (setting) {
    case LoRaFishSetting::Species: text = value==LoRaFishSpecies::selectableCount ? "Mystery egg" : LoRaFishSpecies::label(LoRaFishSpecies::selectionId(value)); break;
    case LoRaFishSetting::Theme: text = loRaFishThemes[value].name; break;
    case LoRaFishSetting::Brightness: text = loRaFishBrightnessNames[value]; break;
    case LoRaFishSetting::Rendering: text = value ? "Simulation" : "Smooth"; break;
    case LoRaFishSetting::Scales:
    case LoRaFishSetting::Body:
    case LoRaFishSetting::Eyes: text = loRaFishColorNames[value]; break;
    case LoRaFishSetting::SleepHour:
    case LoRaFishSetting::WakeHour: snprintf(valueText, sizeof(valueText), "%02u:00", value); break;
    case LoRaFishSetting::UtcOffset: {
      const int minutes = (int(value) - 48) * 15;
      snprintf(valueText, sizeof(valueText), "UTC%c%02d:%02d", minutes < 0 ? '-' : '+',
               abs(minutes)/60, abs(minutes)%60);
      break;
    }
    case LoRaFishSetting::TimeoutAwake:
    case LoRaFishSetting::TimeoutAsleep: text = loRaFishTimeoutShortNames[value]; break;
    case LoRaFishSetting::DimAfter: text = loRaFishAlwaysOnDimNames[value]; break;
  }
  drawLoRaFishHeader(display, titles[static_cast<uint8_t>(setting)], "");
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(w/2, 27, text);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2, 42, "Enter to save");
  drawLoRaFishMenuHint(display);
}

// Meck: rows left out of Options because Meck has its own version of them.
static bool loRaFishWatchHidesOption(LoRaFishOption option) {
  return option == LoRaFishOption::Guess || option == LoRaFishOption::Sound ||
         option == LoRaFishOption::RF || option == LoRaFishOption::GNSS;
}

// Move the Options highlight one row forward or back, stepping over hidden rows.
static void loRaFishWatchStepOption(bool previous) {
  const bool breed = loRaFishCanBreed();
  const uint8_t count = loRaFishOptionCount();
  uint8_t row = LoRaFishOptions::index(loRaFishOption, loRaFishHasSound, breed, loragotchiDXGuest.present);
  for (uint8_t tries = 0; tries < count; ++tries) {
    row = (row + (previous ? count - 1 : 1)) % count;
    const auto option = LoRaFishOptions::at(row, loRaFishHasSound, breed, loragotchiDXGuest.present);
    if (!loRaFishWatchHidesOption(option)) {
      loRaFishOption = option;
      return;
    }
  }
}

// Meck: the Options list drawn without the hidden rows.
static void drawLoRaFishWatchOptions(DisplayDriver& display) {
  const bool breed = loRaFishCanBreed();
  if (!LoRaFishOptions::available(loRaFishOption, loRaFishHasSound, breed, loragotchiDXGuest.present) ||
      loRaFishWatchHidesOption(loRaFishOption))
    loRaFishOption = LoRaFishOption::Advert;
  LoRaFishOption rows[static_cast<uint8_t>(LoRaFishOption::Count)];
  uint8_t count = 0, selected = 0;
  for (uint8_t row = 0; row < loRaFishOptionCount() && count < static_cast<uint8_t>(LoRaFishOption::Count); ++row) {
    const auto option = LoRaFishOptions::at(row, loRaFishHasSound, breed, loragotchiDXGuest.present);
    if (loRaFishWatchHidesOption(option)) continue;
    if (option == loRaFishOption) selected = count;
    rows[count++] = option;
  }
  const uint8_t first = selected > 3 ? selected - 3 : 0;
  drawLoRaFishHeader(display, "OPTIONS", "");
  display.setColor(UIColor::primary_txt);
  for (uint8_t row = 0; row < 4 && first + row < count; ++row) {
    const auto option = rows[first + row];
    char line[24];
    snprintf(line, sizeof(line), "%c %s", option == loRaFishOption ? '>' : ' ', loRaFishOptionLabel(option));
    display.setCursor(3, 18 + row*9);
    display.print(line);
  }
  drawLoRaFishMenuHint(display);
}

static void drawLoRaFishOptions(DisplayDriver& display, UITask* task) {
  drawLoRaFishWatchOptions(display);
}

static void closeLoRaFishAdvertMenu() {
  loRaFishAdvertMenu.close();
  returnToLoRaFishOptions(LoRaFishOption::Advert);
}

static void drawLoRaFishAdvertMenu(DisplayDriver& display) {
  const int w = display.width();
  drawLoRaFishHeader(display, "SEND ADVERT", "");
  display.setColor(UIColor::primary_txt);
  static const char* labels[] = {"Local (zero-hop)", "Flood (multi-hop)", "Back"};
  for (uint8_t row = 0; row < 3; row++) {
    char line[24];
    snprintf(line, sizeof(line), "%c %s",
             row == static_cast<uint8_t>(loRaFishAdvertMenu.selected()) ? '>' : ' ', labels[row]);
    display.setCursor(3, 18 + row * 11);
    display.print(line);
  }
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}

static void cancelLoRaFishReset() {
  loRaFishResetConfirmation.cancel();
  returnToLoRaFishOptions(LoRaFishOption::Reset);
}

static void drawLoRaFishResetConfirmation(DisplayDriver& display) {
  const int w = display.width();
  drawLoRaFishHeader(display, "Start a new egg?", "");
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(w/2, 17, "Pet progress & eggs");
  display.drawTextCentered(w/2, 26, "will be lost forever.");
  const bool reset = loRaFishResetConfirmation.resetSelected();
  display.setCursor(3, 37);
  display.print(reset ? "  Cancel" : "> Cancel");
  display.setCursor(3, 46);
  display.print(reset ? "> Yes, reset pet" : "  Yes, reset pet");
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}

static void startLoRaFishGuessGame() {
  const uint32_t seed=(uint32_t)micros() ^ (uint32_t)millis() ^
      (fish.ageMinutes<<16) ^ fish.rfEvents;
  randomSeed(seed);
  loRaFishGuessNumber=(uint8_t)random(1,10);
  loRaFishGuessPrevious=0;
  loRaFishGuessRounds=0;
  loRaFishGuessScore=0;
  loRaFishGuessHigher=true;
  loRaFishGuessResultShown=false;
  loRaFishGuessCorrect=false;
  loRaFishGuessComplete=false;
}

static void submitLoRaFishGuess() {
  tickLoRaFish();
  if (loRaFishGuessComplete) {
    startLoRaFishGuessGame();
    return;
  }

  uint8_t nextNumber=(uint8_t)random(1,10);
  while (nextNumber==loRaFishGuessNumber)
    nextNumber=(uint8_t)random(1,10);

  loRaFishGuessPrevious=loRaFishGuessNumber;
  loRaFishGuessCorrect=loRaFishGuessHigher
      ? nextNumber>loRaFishGuessNumber
      : nextNumber<loRaFishGuessNumber;
  loRaFishGuessNumber=nextNumber;
  loRaFishGuessRounds++;
  loRaFishGuessResultShown=true;

  if (loRaFishGuessCorrect) {
    loRaFishGuessScore++;
    if (fish.happiness<100) {
      fish.happiness++;
      saveLoRaFish();
    }
  }
  if (loRaFishGuessRounds>=5) loRaFishGuessComplete=true;
}

static void drawLoRaFishGuessGame(DisplayDriver& display) {
  const int w=display.width();
  drawLoRaFishHeader(display,"GUESS GAME","");
  display.setColor(UIColor::primary_txt);

  if (loRaFishGuessComplete) {
    char scoreLine[24];
    snprintf(scoreLine,sizeof(scoreLine),"SCORE %u / 5",(unsigned)loRaFishGuessScore);
    display.drawTextCentered(w/2,26,scoreLine);
    display.drawTextCentered(w/2,40,"HAPPY +1 PER WIN");
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,55,"LONG: PLAY AGAIN");
    return;
  }

  const int resultY=21, correctY=31, numberY=25, roundY=42;
  char numberLine[20];
  if (loRaFishGuessResultShown) {
    snprintf(numberLine,sizeof(numberLine),"%u -> %u",
             (unsigned)loRaFishGuessPrevious,(unsigned)loRaFishGuessNumber);
    display.drawTextCentered(w/2,resultY,numberLine);
    display.drawTextCentered(w/2,correctY,loRaFishGuessCorrect?"CORRECT":"MISS");
  } else {
    snprintf(numberLine,sizeof(numberLine),"NUMBER %u",(unsigned)loRaFishGuessNumber);
    display.drawTextCentered(w/2,numberY,numberLine);
  }

  char roundLine[24];
  snprintf(roundLine,sizeof(roundLine),"ROUND %u/5  SCORE %u",
           (unsigned)(loRaFishGuessRounds+1),(unsigned)loRaFishGuessScore);
  display.drawTextCentered(w/2,roundY,roundLine);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,55,loRaFishGuessHigher?"HIGHER  LONG: PICK":"LOWER  LONG: PICK");
}

static void drawLoRaFishDisplayMenu(DisplayDriver& display) {
  static const char* monoLabels[] = {"Screen timeout", "Brightness", "Firmware theme", "Screen colour", "Game rendering", "Pet species"};
  static const char* colorLabels[] = {"Screen timeout", "Brightness", "Firmware theme", "Fish color", "Game rendering", "Pet species"};
  const uint8_t count = loRaFishDisplayOptionCount();
  drawLoRaFishHeader(display, "DISPLAY", "");
  display.setColor(UIColor::primary_txt);
  uint8_t line = 0;
  for (uint8_t option = 0; option < count; ++option) {
    if (loRaFishDisplayOptionHidden(option)) continue;   // Meck: timeout, brightness
    display.setCursor(3, 18 + line*9);
    display.print(option == loRaFishDisplayOption ? "> " : "  ");
    display.print(loRaFishColorDisplay ? colorLabels[option] : monoLabels[option]);
    line++;
  }
  drawLoRaFishMenuHint(display);
}





static void drawLoRaFishPetSetup(DisplayDriver& display) {
  drawLoRaFishHeader(display,"CHOOSE WATER PET","");
  const char* rows[]={LoRaFishSpecies::label(loRaFishSpecies),"Mystery colours",
      loRaFishColorDisplay ? "Choose colours" : "Keep colours","Start pet"};
  display.setColor(UIColor::primary_txt);
  for (uint8_t i=0;i<4;++i) { display.setCursor(3,18+i*9);
    display.print(i==loRaFishPetSetupField ? "> " : "  "); display.print(rows[i]); }
  drawLoRaFishMenuHint(display);
}
static void drawLoRaFishColorMenu(DisplayDriver& display) {
  static const char* parts[] = {"Scales", "Body", "Eyes", "Start fish"};
  const uint8_t colors[] = {loRaFishScaleColor, loRaFishColor, loRaFishEyeColor};
  drawLoRaFishHeader(display, loRaFishNeedsPetSetup ? "PET COLOURS" : (loRaFishNeedsAppearanceSetup ? "NEW FISH" : "FISH COLOR"), "");
  display.setColor(UIColor::primary_txt);
  for (uint8_t row = 0; row < ((loRaFishNeedsAppearanceSetup || loRaFishNeedsPetSetup) ? 4 : 3); ++row) {
    display.setCursor(3, 18 + row*9);
    display.print(row == loRaFishColorField ? "> " : "  ");
    display.print(row==3 && loRaFishNeedsPetSetup ? "Done colours" : parts[row]);
    if (row < 3) display.drawTextRightAlign(display.width()-3, 18 + row*9,
        colors[row] > 6 ? "Inherited" : loRaFishColorNames[colors[row]]);
  }
  drawLoRaFishMenuHint(display);
}

static void drawLoRaFishTimeMenu(DisplayDriver& display) {
  const int w=display.width();
  char value[20];
  drawLoRaFishHeader(display,"SCHEDULE","");

  const char* labels[]={"Sleep","Wake","UTC offset"};
  const int hours[]={loRaFishSleepHour,loRaFishWakeHour};
  display.setColor(UIColor::primary_txt);
  for (uint8_t row=0; row<LORAFISH_TIME_FIELDS; row++) {
    if (row<2) snprintf(value,sizeof(value),"%02d:00",hours[row]);
    else loRaFishFormatUtcOffset(value,sizeof(value),false);
    display.setCursor(3,22+row*11);
    display.print(row==loRaFishTimeField ? ">" : " ");
    display.print(row==2 ? "Offset" : labels[row]);
    display.drawTextRightAlign(w-3,22+row*11,value);
  }
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}

static void drawLoRaFishTimeoutMenu(DisplayDriver& display) {
  const int w=display.width();
  drawLoRaFishHeader(display,"TIMEOUT","");

  display.setColor(UIColor::primary_txt);
  const uint8_t rowCount=3;
  for (uint8_t row=0; row<rowCount; row++) {
    const uint8_t setting=row==0 ? loRaFishTimeoutAwakeSetting
                                 : row==1 ? loRaFishTimeoutAsleepSetting : loRaFishAlwaysOnDimSetting;
    const char* label=row==0 ? "Awake" : row==1 ? "Asleep" : "Dim after";
    const char* value=row<2 ? loRaFishTimeoutShortNames[min((uint8_t)5,setting)]
                : loRaFishAlwaysOnDimNames[min((uint8_t)2,loRaFishAlwaysOnDimSetting)];
    display.setCursor(3,22+row*12);
    display.print(row==loRaFishTimeoutField ? "> " : "  ");
    display.print(label);
    display.drawTextRightAlign(w-3,22+row*12,value);
  }
  display.setColor(UIColor::secondary_txt);
  drawLoRaFishMenuHint(display);
}




static uint32_t loRaFishDXContactIndex=0;
// Selection is a persistent-contact index, never a filtered row number.
static uint8_t loRaFishDXDestinationKey[PUB_KEY_SIZE];
static bool loRaFishDXDestinationKnown = false;

static bool loRaFishDXDestination(ContactInfo& c) {
  if (loRaFishDXDestinationKnown) {
    const ContactInfo* found = the_mesh.lookupContactByPubKey(loRaFishDXDestinationKey, PUB_KEY_SIZE);
    if (!found) return false;
    c = *found;
    return true;
  }
  return the_mesh.getContactByIdx(loragotchiDXHome.destinationIndex, c);   // Meck: no anon offset
}

static bool loRaFishDXEligible(const ContactInfo& c) {
  // A tank remains an honest MeshCore repeater advert, not a companion that
  // silently forwards. Only explicitly marked tanks join the visit picker.
  // Names are discovery hints, not authentication; the transaction still binds
  // to the selected full public key and requires the normal LFG1 handshake.
  const bool tank = c.type == ADV_TYPE_REPEATER &&
      !strncmp(c.name, "LRX1 ", 5);
  return (c.type == ADV_TYPE_CHAT || tank) && LoRaFishText::hasFish(c.name, sizeof(c.name));
}

static uint32_t loRaFishDXContactCount(uint32_t* selectedRow = nullptr) {
  uint32_t count = 0;
  for (int i = 0; i < the_mesh.getNumContacts(); ++i) {
    ContactInfo c;
    if (the_mesh.getContactByIdx((uint32_t)i, c) && loRaFishDXEligible(c)) {
      if (selectedRow && (uint32_t)i == loRaFishDXContactIndex) *selectedRow = count;
      ++count;
    }
  }
  return count;
}

static bool loRaFishDXGetContact(ContactInfo& c) {
  const int available = the_mesh.getNumContacts();
  if (available <= 0) return false;
  const uint32_t count = (uint32_t)available;
  if (loRaFishDXContactIndex >= count) loRaFishDXContactIndex = 0;
  for (uint32_t offset = 0; offset < count; ++offset) {
    const uint32_t index = (loRaFishDXContactIndex + offset) % count;
    if (the_mesh.getContactByIdx(index, c) && loRaFishDXEligible(c)) {
      loRaFishDXContactIndex = index;
      return true;
    }
  }
  return false;
}

static void moveLoRaFishDXContact(bool previous) {
  const int available = the_mesh.getNumContacts();
  if (available <= 0) return;
  const uint32_t count = (uint32_t)available;
  loRaFishDXContactIndex %= count;
  for (uint32_t step = 1; step <= count; ++step) {
    const uint32_t index = (loRaFishDXContactIndex + (previous ? count - step : step)) % count;
    ContactInfo c;
    if (the_mesh.getContactByIdx(index, c) && loRaFishDXEligible(c)) {
      loRaFishDXContactIndex = index;
      return;
    }
  }
}

static void drawLoRaFishName(DisplayDriver& display, int y, const char* name, size_t capacity) {
  char text[32];
  const size_t columns = min((int)sizeof(text) - 1, max(0, (display.width() - 6) / 6));
  LoRaFishText::display(text, columns + 1, name, capacity);
  display.drawTextCentered(display.width() / 2, y, text);
}

static void drawLoRaFishDX(DisplayDriver& display) {
  const int w=display.width(),h=display.height();
  const bool choosingContact = loRaFishPage == LORAFISH_DX_CONTACT_PAGE;
  char tb[8],db[24]; loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,"DX VISIT",tb);
  display.setColor(UIColor::primary_txt);
  const char* returnStatus=loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Pending?"RETURNING VISITOR":
      loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Failed?"RETURN UNCONFIRMED":
      loragotchiDXGuestReturn.status==LoRaFishGuestReturn::Confirmed?"RETURN DELIVERED":"Visit a friend";
  if (!choosingContact && loragotchiDXGuestReturn.status != LoRaFishGuestReturn::Idle &&
      (loragotchiDXHome.away || loragotchiDXHome.pending || loragotchiDXHome.committing))
    display.drawTextCentered(w/2,16,returnStatus);

  if(loragotchiDXHome.away) {
    display.drawTextCentered(w/2,27,loragotchiDXVisit.phase == LoRaFishVisit::Unconfirmed ?
        "RETURN UNCONFIRMED" : loragotchiDXVisit.phase == LoRaFishVisit::Recall ? "RECALLING..." : "FISH AWAY");
    display.setColor(UIColor::secondary_txt);
    drawLoRaFishName(display,39,loragotchiDXHome.destinationName,sizeof(loragotchiDXHome.destinationName));
  } else if(loragotchiDXHome.committing) {
    display.drawTextCentered(w/2,28,loragotchiDXVisit.phase == LoRaFishVisit::Unconfirmed ?
        "UNCONFIRMED" : loragotchiDXVisit.phase == LoRaFishVisit::Recall ? "RECALLING..." : "DELIVERING...");
    display.setColor(UIColor::secondary_txt);
    drawLoRaFishName(display,40,loragotchiDXHome.destinationName,sizeof(loragotchiDXHome.destinationName));
  } else if(loragotchiDXHome.pending) {
    display.drawTextCentered(w/2,28,"CALLING...");
    display.setColor(UIColor::secondary_txt);
    drawLoRaFishName(display,40,loragotchiDXHome.destinationName,sizeof(loragotchiDXHome.destinationName));
  } else if (!choosingContact) {
    display.drawTextCentered(w/2,27,loragotchiDXVisit.phase == LoRaFishVisit::NoReply ? "NO REPLY - RETRY" : returnStatus);
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,39,"Enter to choose");
  } else {
    ContactInfo c;
    if(loRaFishDXGetContact(c)) {
      drawLoRaFishName(display,27,c.name,sizeof(c.name));
      uint32_t row = 0;
      const uint32_t count = loRaFishDXContactCount(&row);
      char b[28]; snprintf(b,sizeof(b),"%lu/%lu fish tanks",
          (unsigned long)(row+1),(unsigned long)count);
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,39,b);
    } else {
      display.drawTextCentered(w/2,27,"NO FISH TANKS");
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,39,"Water-marked friends");
    }
  }
  display.setColor(UIColor::secondary_txt);
  const char* hint = "ENTER:OPEN  Q:BACK";
  if (choosingContact) {
    if (loragotchiDXHome.away || loragotchiDXHome.committing) hint = "ENTER:RECALL Q:BACK";
    else if (loragotchiDXHome.pending) hint = "Q:BACK";
    else hint = "ENTER:SEND  Q:BACK";
  }
  display.drawTextCentered(w/2,h-9,hint);
}

static void drawLoRaFishRFMonitor(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();

  // Capture one history point per actual MeshCore RX sample.
  if (lorafish_rf_sample_seq != loRaFishRfSeenSeq) {
    loRaFishRfSeenSeq = lorafish_rf_sample_seq;
    int v=(int)lorafish_last_rssi;
    if (v < -127) v=-127;
    if (v > -20) v=-20;
    if (loRaFishRfHistoryCount < 48) {
      loRaFishRfHistory[loRaFishRfHistoryCount++]=(int8_t)v;
    } else {
      memmove(&loRaFishRfHistory[0],&loRaFishRfHistory[1],47);
      loRaFishRfHistory[47]=(int8_t)v;
    }
  }

  char right[16];
  uint16_t mv=loRaFishDisplayBattery(task);
  int batt=(int)((mv-3300L)*100L/(4200L-3300L));
  batt=constrain(batt,0,100);
  snprintf(right,sizeof(right),"BAT %d%%",batt);
  drawLoRaFishHeader(display,"RF MONITOR",right);

  display.setColor(UIColor::primary_txt);
  char line[32];
  snprintf(line,sizeof(line),"RSSI %.0f  SNR %.1f",lorafish_last_rssi,lorafish_last_snr);
  display.setCursor(3,20);
  display.print(line);

  // 48-column RSSI history graph. -120 dBm is bottom, -40 dBm is top.
  const int gx=3, gy=31, gw=min(48,w-6), gh=20;
  display.setColor(UIColor::secondary_txt);
  display.fillRect(gx,gy+gh,gw,1);
  display.fillRect(gx,gy,1,gh+1);
  int start=max(0,(int)loRaFishRfHistoryCount-gw);
  for(int i=start;i<(int)loRaFishRfHistoryCount;i++) {
    int r=loRaFishRfHistory[i];
    int level=constrain((r+120)*gh/80,0,gh);
    int x=gx+(i-start);
    display.fillRect(x,gy+gh-level,1,max(1,level));
  }

  display.setColor(UIColor::secondary_txt);
  if (lorafish_last_rf_ms==0) {
    display.drawTextRightAlign(w-2,55,"RX --");
  } else {
    unsigned long age=(millis()-lorafish_last_rf_ms)/1000UL;
    snprintf(line,sizeof(line),"RX %lus",age);
    display.drawTextRightAlign(w-2,55,line);
  }

  // Graphical battery gauge.
  int bx=3, by=55, bw=38, bh=7;
  display.drawRect(bx,by,bw,bh);
  display.fillRect(bx+bw,by+2,2,3);
  int fill=(batt*(bw-4))/100;
  if(fill>0) display.fillRect(bx+2,by+2,fill,bh-4);
}

static void drawLoRaFishGNSS(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  LocationProvider* location=sensors.getLocationProvider();
  const bool enabled=task->getGPSState();
  drawLoRaFishHeader(display,"GNSS",enabled ? "ON" : "OFF");

  display.setColor(UIColor::primary_txt);
  if (!location) {
    display.drawTextCentered(w/2,27,"NO RECEIVER");
  } else if (!enabled) {
    display.drawTextCentered(w/2,27,"GNSS OFF");
  } else {
    display.drawTextCentered(w/2,22,location->isValid() ? "FIX" : "SEARCHING");
    char line[32];
    snprintf(line,sizeof(line),"SAT %ld",location->satellitesCount());
    display.drawTextCentered(w/2,33,line);
    if (location->isValid()) {
      snprintf(line,sizeof(line),"%.4f %.4f",
               location->getLatitude()/1000000.0,
               location->getLongitude()/1000000.0);
      display.drawTextCentered(w/2,44,line);
    } else {
      display.drawTextCentered(w/2,44,"WAITING FOR FIX");
    }
  }

  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,56,"LONG:TOGGLE");
}


// T-Deck 16-bit aquarium scenery. All shapes are integer pixel primitives;
// no spritesheets, dynamic allocation, or additional flash partitions.

static void drawLoRaFishBubbles(DisplayDriver& display) {
  const int h = display.height();

  // Different speeds and starting positions so they don't move together.
  int y1 = h - 10 - ((millis() / 180) % 35);
  int y2 = h - 10 - ((millis() / 260 + 13) % 38);
  int y3 = h - 10 - ((millis() / 340 + 25) % 36);

  // Gentle sideways wobble.
  int x1 = 15 + (int)(2.0f * sinf(millis() / 700.0f));
  int x2 = 29 + (int)(2.0f * sinf(millis() / 900.0f));
  int x3 = display.width() - 18 +
           (int)(2.0f * sinf(millis() / 800.0f));

  display.setColor(UIColor::secondary_txt);

  display.drawRect(x1, y1, 2, 2);
  display.drawRect(x2, y2, 3, 3);
  display.drawRect(x3, y3, 2, 2);
}


static void drawLoRaFishPoop(DisplayDriver& display, int x, int y, uint8_t phase) {
  display.setColor(UIColor::primary_txt);
  int wobble = phase & 1;

  // v0.3.5.3: gloriously oversized 9x8 Tamagotchi-style pile.
  display.fillRect(x+4+wobble, y,   2,1);
  display.fillRect(x+3,        y+1, 4,1);
  display.fillRect(x+2,        y+2, 5,1);
  display.fillRect(x+1,        y+3, 7,2);
  display.fillRect(x,          y+5, 9,2);
  display.fillRect(x+1,        y+7, 7,1);

  // Three-phase animated stink trails.
  uint8_t stink = (millis()/280UL + phase) % 3;
  if (stink == 0) {
    display.fillRect(x+1,y-5,1,2);
    display.fillRect(x+2,y-7,1,2);
    display.fillRect(x+6,y-6,1,2);
    display.fillRect(x+7,y-8,1,2);
  } else if (stink == 1) {
    display.fillRect(x+2,y-5,1,2);
    display.fillRect(x+1,y-7,1,2);
    display.fillRect(x+7,y-6,1,2);
    display.fillRect(x+6,y-8,1,2);
  } else {
    display.fillRect(x+1,y-6,1,2);
    display.fillRect(x+2,y-8,1,2);
    display.fillRect(x+6,y-5,1,2);
    display.fillRect(x+7,y-7,1,2);
  }
}

static void drawLoRaFishHygiene(DisplayDriver& display) {
  const int w=display.width(), h=display.height();
  const int floorY=h-20;
  uint8_t wiggle=(millis()/350UL)&1;

  static const int poopX[3]={20,58,96};
  for(uint8_t i=0;i<loRaFishPoopCount && i<LORAFISH_MAX_POOP;i++)
    drawLoRaFishPoop(display,poopX[i],floorY-8,wiggle ^ (i&1));

  if (loRaFishCleaning) {
    float p=(millis()-loRaFishCleaningStarted)/(float)LORAFISH_CLEAN_MS;
    if(p>1.0f) p=1.0f;
    int bx=-8+(int)((w+10)*p);

    // Tiny broom: diagonal handle + 7px brush.
    display.setColor(UIColor::primary_txt);
    fishLine(display,bx+5,floorY-15,bx,floorY-5);
    display.fillRect(bx-3,floorY-5,8,2);
    display.fillRect(bx-2,floorY-3,1,2);
    display.fillRect(bx,floorY-3,1,2);
    display.fillRect(bx+2,floorY-3,1,2);
    display.fillRect(bx+4,floorY-3,1,2);

    // Poop already passed by the broom visually disappears.
    for(uint8_t i=0;i<loRaFishPoopCount && i<LORAFISH_MAX_POOP;i++) {
      if (poopX[i]+9 > bx+5)
        drawLoRaFishPoop(display,poopX[i],floorY-8,wiggle ^ (i&1));
    }
  }
}


static void drawLoRaFishAquarium(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8], db[24];
  loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawCardputerTankDepth(LORAFISH_SCENERY_CANVAS(display));
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);
  drawLoRaFishTankBattery(display,task,tb);

  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,h-15,w-4,1);
  drawLoRaFishBubbles(display);
  //display.drawRect(15,21,2,2);
  //display.drawRect(24,31,3,3);
  //display.drawRect(w-18,20,2,2);
  //display.fillRect(6,h-16,2,6);
  //display.fillRect(w-9,h-18,2,8);

  int x=0, y=0;
  bool right=true;
  loRaFishNaturalPosition(w,x,y,right);

  if (loRaFishFeeding) {
    const unsigned long elapsed=millis()-loRaFishFeedStarted;
    const int foodX=w/2+18;
    const int targetX=foodX-15;
    const int targetY=35;

    if (elapsed<LORAFISH_FEED_DROP_MS) {
      const float dropProgress=elapsed/(float)LORAFISH_FEED_DROP_MS;
      const int foodY=18+(int)(16*dropProgress);
      display.setColor(UIColor::primary_txt);
      display.fillRect(foodX,foodY,2,2);
    }

    if (elapsed>=LORAFISH_FEED_NOTICE_MS && elapsed<LORAFISH_FEED_DONE_MS) {
      if (!loRaFishFeedChasing) {
        loRaFishFeedStartX=x;
        loRaFishFeedStartY=y;
        loRaFishFeedLastX=x;
        loRaFishFeedLastXValid=true;
        loRaFishFeedChasing=true;
      }

      const bool facingFood=targetX>=loRaFishFeedStartX;
      if (elapsed<LORAFISH_FEED_NOTICE_MS+LORAFISH_FEED_RUSH_MS) {
        const float rushProgress=(elapsed-LORAFISH_FEED_NOTICE_MS)/
            (float)LORAFISH_FEED_RUSH_MS;
        x=loRaFishFeedStartX+(int)((targetX-loRaFishFeedStartX)*rushProgress);
        y=loRaFishFeedStartY+(int)((targetY-loRaFishFeedStartY)*rushProgress);
        right=facingFood;
      } else if (elapsed<LORAFISH_FEED_DROP_MS+LORAFISH_FEED_EAT_MS) {
        x=targetX;
        y=targetY;
        right=facingFood;
      } else {
        const float returnProgress=(elapsed-LORAFISH_FEED_DROP_MS-
            LORAFISH_FEED_EAT_MS)/(float)LORAFISH_FEED_RETURN_MS;
        x=targetX+(int)((x-targetX)*returnProgress);
        y=targetY+(int)((y-targetY)*returnProgress);
        if (returnProgress<0.5f) right=facingFood;
      }

      if (loRaFishFeedLastXValid) {
        if (x>loRaFishFeedLastX) right=true;
        else if (x<loRaFishFeedLastX) right=false;
        loRaFishFeedLastX=x;
      }
    }

    if (elapsed>=LORAFISH_FEED_DONE_MS) {
      loRaFishFeeding=false;
      loRaFishFeedChasing=false;
      loRaFishFeedLastXValid=false;
    }
  }

 drawLoRaFishHygiene(display);
  const bool localSleeping = loRaFishIsSleeping();
  const int homeTargetX=localSleeping ? (loragotchiDXGuest.present ? w/3 : w/2-5) : x;
  const int homeTargetY=localSleeping ? 38 : y;
  loRaFishBeginTravelAnimation(loRaFishHomeTravelAnimation,w,homeTargetX,homeTargetY);
  if (!loragotchiDXHome.away && !loragotchiDXHome.committing) {
    // A guest must not freeze the host fish.  The home fish keeps its normal
    // swim path; if it is bedtime, only the home fish sleeps.
    bool homeTraveling=false;
    if (loRaFishHomeTravelAnimation.active && loRaFishHomeTravelAnimation.entering)
      homeTraveling=loRaFishDrawTravelAnimation(display,loRaFishHomeTravelAnimation);
    if (!homeTraveling && !loRaFishHomeTravelAnimation.active && localSleeping) {
      const int sleepX = loragotchiDXGuest.present ? w/3 : w/2-5;
      const int sleepY = 38;
      drawHomeFishSprite(display,sleepX,sleepY,true);
      display.setColor(UIColor::primary_txt);
      display.setCursor(min(w-4,sleepX+12), max(17,sleepY-11)); display.print("z");
      display.setCursor(min(w-4,sleepX+19), max(12,sleepY-16)); display.print("Z");
    } else if (!homeTraveling && !loRaFishHomeTravelAnimation.active) {
      drawHomeFishSprite(display,x,y,right);
      if (!loRaFishFeeding) {
        int wx, wy; bool wright; LoRaFishWanderInfo info;
        loRaFishWanderPosition(w, fish.evolution, 0, wx, wy, wright, &info);
        drawLoRaFishWanderBubbles(display, x, y, right, fish.evolution, info);
      }
    }
  } else {
    const bool homeLeaving=loRaFishHomeTravelAnimation.active &&
        !loRaFishHomeTravelAnimation.entering &&
        loRaFishDrawTravelAnimation(display,loRaFishHomeTravelAnimation);
    if (!homeLeaving && !loRaFishGuestTravelAnimation.active && !loragotchiDXGuest.present) {
      display.setColor(UIColor::secondary_txt);
      display.drawTextCentered(w/2,35,loragotchiDXHome.committing ?
          (loragotchiDXVisit.phase == LoRaFishVisit::Unconfirmed ? "VISIT UNCONFIRMED" : "DELIVERING...") : "FISH DX AWAY");
    }
  }

  const bool guestPresent=loragotchiDXGuest.present;
  const uint8_t guestStage=guestPresent ? loragotchiDXGuest.stage : loRaFishGuestTravelAnimation.stage;
  unsigned long guestPeriod=8000UL;
  if(guestStage==0) guestPeriod=5200UL;
  else if(guestStage==1) guestPeriod=6500UL;
  else if(guestStage>=3) guestPeriod=11000UL;
  int gx=0,gy=0; bool gright=true;
  loRaFishNaturalPositionForStage(w,guestStage,guestPeriod/2,gx,gy,gright);
  loRaFishBeginTravelAnimation(loRaFishGuestTravelAnimation,w,gx,gy);
  if (guestPresent) {
    // Same movement model as a lone fish, phase shifted by half a cycle.
    // This keeps the visitor inside the tank and removes the old erratic
    // full-screen sweep.
    const bool guestEntering=loRaFishGuestTravelAnimation.active &&
        loRaFishGuestTravelAnimation.entering &&
        loRaFishDrawTravelAnimation(display,loRaFishGuestTravelAnimation);
    if (!guestEntering && !loRaFishGuestTravelAnimation.active)
      drawFishSpriteForStage(display,gx,gy,gright,loragotchiDXGuest.stage,loRaFishGuestRenderAppearance(),loragotchiDXGuest.species);
    if (!loRaFishGuestTravelAnimation.active) {
      display.setColor(UIColor::secondary_txt);
      drawLoRaFishName(display,27,loragotchiDXGuest.guestName,sizeof(loragotchiDXGuest.guestName));
    }
  } else if (loRaFishGuestTravelAnimation.active && !loRaFishGuestTravelAnimation.entering) {
    loRaFishDrawTravelAnimation(display,loRaFishGuestTravelAnimation);
  }

  if (millis() < loRaFishEvolutionFlashUntil) {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,19,"* EVOLVED! *");
  } else if (fishReaction==1) display.drawTextCentered(w/2,19,"* MESSAGE *");
  else if (fishReaction==2) display.drawTextCentered(w/2,19,"* SENT *");
  else if (fishReaction==3) display.drawTextCentered(w/2,19,"* HELLO! *");

  const char* tankStage=fish.ageMinutes<LORAFISH_EGG_MINUTES ? "0" : fishStageName();
  drawLoRaFishTankStatusBar(display,tankStage,task);
}

static void drawLoRaFishStats(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  char generation[12];
  snprintf(generation,sizeof(generation),"G%u",(unsigned)loRaFishGeneration);
  drawLoRaFishHeader(display,"STATS","");
  display.setColor(UIColor::primary_txt);
  char statLine[48];

  snprintf(statLine,sizeof(statLine),"Age  %luh %02lum",
           (unsigned long)(fish.ageMinutes/60UL),
           (unsigned long)(fish.ageMinutes%60UL));
  display.setCursor(2,21);
  display.print(statLine);
  display.drawTextRightAlign(w-2,21,generation);

  snprintf(statLine,sizeof(statLine),"Happy %u%%",(unsigned)fish.happiness);
  display.setCursor(2,31);
  display.print(statLine);

  snprintf(statLine,sizeof(statLine),"RF %lu  Msg %lu",
           (unsigned long)fish.rfEvents,
           (unsigned long)fish.messages);
  display.setCursor(2,41);
  display.print(statLine);

  snprintf(statLine,sizeof(statLine),"ACK %lu  BLE %s",
           (unsigned long)fish.txAcks,
           task->hasConnection()?"ON":"OFF");
  display.setCursor(2,51);
  display.print(statLine);
  display.setColor(UIColor::secondary_txt);
  display.fillRect(2,60,w-4,1);
}

static void drawLoRaFishClockPage(DisplayDriver& display, UITask* task) {
  const int w=display.width();
  char tb[8], db[24];
  bool meshTime = loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawLoRaFishHeader(display,"CLOCK",task->hasConnection()?"PHONE":"MESH");

  display.setColor(UIColor::primary_txt);
  display.setTextSize(2);
  display.drawTextCentered(w/2,26,tb);
  display.setTextSize(1);
  display.drawTextCentered(w/2,44,db);
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(w/2,55,meshTime ? "RTC + LOCAL OFFSET" : "build-time fallback");
}

static void drawLoRaFishPairing(DisplayDriver& display) {
  const int w=display.width();
  drawLoRaFishHeader(display,"PHONE PAIRING","BLE");

  uint32_t activePin = the_mesh.getBLEPin();

  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(w/2,20,"MeshCore Companion");

  char pin[16];
  if (activePin != 0) {
    snprintf(pin,sizeof(pin),"%06lu",(unsigned long)activePin);
    display.setTextSize(2);
    display.drawTextCentered(w/2,30,pin);
    display.setTextSize(1);
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,45,"PIN for phone");
    display.drawTextCentered(w/2,55,"ENTER: next");
  } else {
    display.setColor(UIColor::secondary_txt);
    display.drawTextCentered(w/2,32,"No PIN configured");
    display.drawTextCentered(w/2,47,"BLE security off");
    display.drawTextCentered(w/2,63,"ENTER: next");
  }
}

static bool loRaFishIsSleeping() {
  return LoRaFishHappiness::sleeping((loRaFishLocalEpoch() / 3600) % 24,
                                    loRaFishSleepHour, loRaFishWakeHour);
}

static void drawLoRaFishSleeping(DisplayDriver& display, UITask* task) {
  const int w=display.width(), h=display.height();
  char tb[8], db[24];
  loRaFishClock(tb,sizeof(tb),db,sizeof(db));
  drawCardputerTankDepth(LORAFISH_SCENERY_CANVAS(display));
  drawLoRaFishBubbles(display);
  drawLoRaFishHeader(display,loRaFishDisplayName(),tb);
  drawLoRaFishTankBattery(display,task,tb);

 drawHomeFishSprite(display,w/2-5,38,true);

display.setColor(UIColor::primary_txt);

display.setCursor(w/2+12, 27);
display.print("z");

display.setCursor(w/2+19, 22);
display.print("Z");

display.setCursor(w/2+27, 17);
display.print("Z");

display.setColor(UIColor::secondary_txt);
display.fillRect(2,h-15,w-4,1);
display.drawTextCentered(w/2,h-9,"Sleeping");
}

static void drawLoRaFishChannelNotice(DisplayDriver& display) {
  const int x = display.width() / 2;
  display.setTextSize(1);
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(x, 2, "#loragotchi-dx");
  display.drawTextCentered(x, 14, "Could not join");
  display.drawTextCentered(x, 26, "No free channel slot");
  display.drawTextCentered(x, 38, "Channels unchanged");
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(x, 56, "ENTER TO DISMISS");
}

static void drawLoRaFishNameNotice(DisplayDriver& display) {
  const int x = display.width() / 2;
  drawLoRaFishHeader(display, "GET FOUND BY FRIENDS", "");
  display.setColor(UIColor::primary_txt);
  display.drawTextCentered(x, 18, "Add a Water emoji to");
  display.drawTextCentered(x, 27, "your MeshCore name");
  display.drawTextCentered(x, 36, "to appear as a");
  display.drawTextCentered(x, 45, "LoRagotchi-DX tank.");
  display.setColor(UIColor::secondary_txt);
  display.drawTextCentered(x, 56, "ENTER TO DISMISS");
}

static void drawLoRaFishAdoption(DisplayDriver& display) {
  drawLoRaFishHeader(display,"AQUARIUM EGGS","");
  display.setColor(UIColor::primary_txt);
  const bool saving=loRaFishEggApplying || loRaFishEggSession.phase==LoRaFishEgg::Offered;
  display.drawTextCentered(display.width()/2,17,saving ? "Saving egg..." :
      loRaFishEggSession.phase==LoRaFishEgg::Choose ? "Aquarium detected!" : "Looking for eggs...");
  display.drawTextEllipsized(3,29,display.width()-6,loRaFishEggTankName);
  display.drawTextCentered(display.width()/2,41,saving ? "Retrying if needed" :
      loRaFishEggSession.phase==LoRaFishEgg::Choose ? "Choose egg in app" : "Checking support");
  display.drawTextCentered(display.width()/2,55,saving ? "Please wait" : "D:NEXT  ENTER:SKIP");
}

static void drawLoRaFish(DisplayDriver& display, UITask* task) {
  if (loRaFishChannelNotice) {
    drawLoRaFishChannelNotice(display);
    return;
  }
  if (loRaFishNameNotice) {
    drawLoRaFishNameNotice(display);
    return;
  }
  if (loRaFishAdoptionEligible) {
    drawLoRaFishAdoption(display);
    return;
  }
  if (loRaFishNeedsPetSetup && loRaFishPage!=9 && !loRaFishValuePicker.active()) loRaFishPage=LORAFISH_PET_SETUP_PAGE;
  if (loRaFishValuePicker.active()) {
    drawLoRaFishValuePicker(display);
    return;
  }
  if (millis() >= fishReactionUntil) fishReaction=0;

  const bool sleeping = loRaFishIsSleeping();

  switch (loRaFishPage) {
    case LORAFISH_PET_SETUP_PAGE: drawLoRaFishPetSetup(display); break;
    case 1: drawLoRaFishStats(display,task); break;
    case 2: drawLoRaFishClockPage(display,task); break;
    case 3: drawLoRaFishPairing(display); break;
    case 4: drawLoRaFishDX(display); break;
    case LORAFISH_DX_CONTACT_PAGE: drawLoRaFishDX(display); break;
    case 5: drawLoRaFishOptions(display,task); break;
    case 6: drawLoRaFishRFMonitor(display,task); break;
    case 7: drawLoRaFishGNSS(display,task); break;
    case 8: drawLoRaFishDisplayMenu(display); break;
    case 9: drawLoRaFishColorMenu(display); break;
    case 10: drawLoRaFishTimeMenu(display); break;
    case 11: drawLoRaFishTimeoutMenu(display); break;
    case 12: drawLoRaFishDisplayMenu(display); break;
    case 13: drawLoRaFishDisplayMenu(display); break;
    case 15: drawLoRaFishDisplayMenu(display); break;
    case 16: drawLoRaFishGuessGame(display); break;
    case LORAFISH_RESET_CONFIRM_PAGE: drawLoRaFishResetConfirmation(display); break;
    case LORAFISH_ADVERT_PAGE: drawLoRaFishAdvertMenu(display); break;
    default:
      if (fish.ageMinutes >= LORAFISH_LIFESPAN_MINUTES) drawLoRaFishMemorial(display);
      else if (fish.ageMinutes < LORAFISH_EGG_MINUTES) drawLoRaFishEgg(display,task);
      // DX away state takes priority over the local sleep schedule.
      // drawLoRaFishAquarium() already handles away and guest states.
      else if (loragotchiDXHome.away || loragotchiDXGuest.present) drawLoRaFishAquarium(display,task);
      else if (sleeping) drawLoRaFishSleeping(display,task);
      else drawLoRaFishAquarium(display,task);
      break;
  }
}

// ---- End of ported code ----

// =============================================================================
// Meck integration. Behaviour follows LoRaGotchi's UITask (its short press,
// Select and Back handlers), with these changes:
//   - the aquarium is a Meck home page; the menus are a screen opened from the
//     Games menu (main pages: Stats, DX visit, Options)
//   - pages Meck has its own version of are left out: clock, BLE PIN, RF
//     monitor, GNSS, guess game, screen timeout, brightness, sounds
//   - the fish never takes over the screen, brightness or screen timeout
// =============================================================================

static UITask* s_task = nullptr;

static const uint8_t kMainPages[] = {1, 4, 5};   // Stats, DX visit, Options
static constexpr uint8_t kMainPageCount = sizeof(kMainPages);

static int mainPageIndex(uint8_t page) {
  for (uint8_t i = 0; i < kMainPageCount; i++) if (kMainPages[i] == page) return i;
  return -1;
}
static void stepMainPage(int dir) {
  int i = mainPageIndex(loRaFishPage);
  if (i < 0) i = 0;
  i = (i + kMainPageCount + dir) % kMainPageCount;
  loRaFishPage = kMainPages[i];
}

static void alert(const char* text, int ms) {
  if (s_task) s_task->showAlert(text, ms);
}

// Display menu list, skipping the rows Meck leaves out.
static void stepDisplayOption(bool previous) {
  const int n = loRaFishDisplayOptionCount();
  for (int tries = 0; tries < n; tries++) {
    loRaFishDisplayOption = (uint8_t)((loRaFishDisplayOption + n + (previous ? -1 : 1)) % n);
    if (!loRaFishDisplayOptionHidden(loRaFishDisplayOption)) break;
  }
}

// True while up/down has something to move: a list, a set of fields, a picker,
// or a notice or the egg search, which every key answers.
static bool hasList() {
  return loRaFishChannelNotice || loRaFishNameNotice || loRaFishAdoptionEligible ||
         loRaFishValuePicker.active() || loRaFishPage == 5 || loRaFishPage == 9 ||
         loRaFishPage == 10 || loRaFishPage == 12 || loRaFishPage == LORAFISH_ADVERT_PAGE ||
         loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE || loRaFishPage == LORAFISH_DX_CONTACT_PAGE ||
         loRaFishPage == LORAFISH_PET_SETUP_PAGE;
}

// A short press: next, or previous (LoRaGotchi HomeScreen::handleInput). All
// short presses navigate; actions and saving belong to Select.
static void press(bool previous) {
  if (dismissLoRaFishNotice()) return;   // the dismissal must not also navigate
  if (loRaFishAdoptionEligible) {
    if (!loRaFishEggApplying && loRaFishEggSession.phase != LoRaFishEgg::Offered)
      selectLoRaFishEggTank(true);
    return;
  }
  if (loRaFishValuePicker.active()) {
    loRaFishValuePicker.move(previous);
    return;
  }
  auto move = [previous](uint8_t value, uint8_t count) -> uint8_t {
    return (value + (previous ? count - 1 : 1)) % count;
  };
  switch (loRaFishPage) {
    case LORAFISH_ADVERT_PAGE:
      if (previous) loRaFishAdvertMenu.previous();
      else loRaFishAdvertMenu.next();
      break;
    case LORAFISH_RESET_CONFIRM_PAGE:
      loRaFishResetConfirmation.toggle();
      break;
    case 5:
      loRaFishWatchStepOption(previous);
      break;
    case 12:
      stepDisplayOption(previous);
      break;
    case LORAFISH_PET_SETUP_PAGE:
      loRaFishPetSetupField = move(loRaFishPetSetupField, 4);
      break;
    case 9:
      loRaFishColorField = move(loRaFishColorField,
          (loRaFishNeedsAppearanceSetup || loRaFishNeedsPetSetup) ? 4 : 3);
      break;
    case 10:
      loRaFishTimeField = move(loRaFishTimeField, LORAFISH_TIME_FIELDS);
      break;
    case LORAFISH_DX_CONTACT_PAGE:
      if (!loragotchiDXHome.away && !loragotchiDXHome.pending && !loragotchiDXHome.committing)
        moveLoRaFishDXContact(previous);
      break;
    default:
      stepMainPage(previous ? -1 : 1);
      break;
  }
}

// DX contact page: send the fish, or recall it (LoRaGotchi handleLongPress).
static void dxSendOrRecall() {
  ContactInfo c;
  uint32_t ack = 0, to = 0;
  if (loragotchiDXHome.away || loragotchiDXHome.committing) {
    // Recall the exact contact used for the visit, even if ARRIVED is still
    // pending. This is the recovery path if the final confirmation is lost.
    if (!loRaFishDXDestination(c)) { alert("Tank not in contacts", 900); return; }
    alert(the_mesh.recallLoRaFish() ? "Recall queued" : "No active visit", 700);
  } else if (!loragotchiDXHome.pending) {
    if (!loRaFishDXGetContact(c)) { alert("No fish tanks", 900); return; }
    // Sleeping fish stay home; recall above stays available at night.
    if (loRaFishIsSleeping()) {
      alert("Fish is sleeping", 900);
      Serial.println("[LFG1 PEER] SEND BLOCKED: local fish is sleeping");
      return;
    }
    if (!loragotchiDXHome.fishid) {
      loragotchiDXHome.fishid =
          0x4C460000UL ^
          ((uint32_t)fish.ageMinutes << 8) ^
          (uint32_t)fish.rfEvents ^
          (uint32_t)millis();
      if (!loragotchiDXHome.fishid) loragotchiDXHome.fishid = 0x4C460001UL;
    }
    loragotchiDXHome.txid = (uint32_t)(millis() ^ loragotchiDXHome.fishid);
    loragotchiDXHome.destinationIndex = loRaFishDXContactIndex;
    LoRaFishText::rawPrefix(loragotchiDXHome.destinationName,
        sizeof(loragotchiDXHome.destinationName), c.name, sizeof(c.name));
    char msg[112]; snprintf(msg, sizeof(msg), "LFG1|REQ|%lu|%lu|%u|%s",
      (unsigned long)loragotchiDXHome.txid, (unsigned long)loragotchiDXHome.fishid,
      (unsigned)fish.evolution, loRaFishProtocolName());
    int r = the_mesh.sendMessage(c, the_mesh.getRTCClock()->getCurrentTime(), 0, msg, ack, to);
    loragotchiDXHome.pending = (r >= 0);
    if (loragotchiDXHome.pending) {
      memcpy(loragotchiDXHome.destinationKey, c.id.pub_key, PUB_KEY_SIZE);
      loragotchiDXHome.destinationKnown = true;
      memcpy(loRaFishDXDestinationKey, c.id.pub_key, PUB_KEY_SIZE);
      loRaFishDXDestinationKnown = true;
      the_mesh.trackLoRaFishRequest(msg, to);
      loragotchiDXSave();
    }
    Serial.printf("[LFG1 M2] REQ TX result=%d to='%s'\n", r, c.name);
    alert(r >= 0 ? "DX request sent" : "DX send failed", 800);
  }
}

// Select: Enter or a long press (LoRaGotchi handleLongPress, menu pages only).
static int select() {
  if (dismissLoRaFishNotice()) return LORAFISH_MENU_STAY;
  if (loRaFishAdoptionEligible) { closeLoRaFishAdoption(); return LORAFISH_MENU_STAY; }
  if (loRaFishValuePicker.active()) { selectLoRaFishValue(); return LORAFISH_MENU_STAY; }

  switch (loRaFishPage) {
    case LORAFISH_ADVERT_PAGE: {
      const uint32_t now = millis();
      const auto result = loRaFishAdvertMenu.activate(now, true,
          [](bool flood) { return the_mesh.loRaFishAdvert(flood); });
      if (result == AdvertMenu::Result::Sent) {
        closeLoRaFishAdvertMenu();
        if (s_task) s_task->notify(UIEventType::ack);
        alert("Advert queued!", 1000);
      } else if (result == AdvertMenu::Result::Back) {
        closeLoRaFishAdvertMenu();
      } else if (result == AdvertMenu::Result::RateLimited) {
        char message[24];
        snprintf(message, sizeof(message), "Wait %lus", (unsigned long)loRaFishAdvertMenu.waitSeconds(now));
        alert(message, 1000);
      } else if (result == AdvertMenu::Result::Failed) {
        alert("Advert failed", 1000);
      }
      break;
    }
    case LORAFISH_RESET_CONFIRM_PAGE: {
      const bool reset = loRaFishResetConfirmation.confirm(true);
      cancelLoRaFishReset();
      if (reset) {
        resetLoRaFishPet();
        loRaFishPage = loRaFishNeedsPetSetup ? LORAFISH_PET_SETUP_PAGE :
            (loRaFishNeedsAppearanceSetup ? 9 : 1);
        loRaFishOption = LoRaFishOption::Advert;
        loRaFishHatchMelodyPlayed = false;
        if (!loRaFishNeedsAppearanceSetup) { alert("New egg!", 900); return LORAFISH_MENU_HOME; }
      }
      break;
    }
    case LORAFISH_PET_SETUP_PAGE:
      if (loRaFishPetSetupField == 0) {
        openLoRaFishValue(LoRaFishSetting::Species);
      } else if (loRaFishPetSetupField == 1) {
        chooseLoRaFishMysteryColours();
      } else if (loRaFishPetSetupField == 2) {
        loRaFishPage = 9;
        loRaFishColorField = 0;
      } else {
        loRaFishNeedsPetSetup = loRaFishNeedsAppearanceSetup = false;
        loRaFishPage = 1;
        loRaFishSyncDXAppearance();
        saveLoRaFishSettings();
        saveLoRaFish();
        return LORAFISH_MENU_HOME;                            // to the aquarium
      }
      break;
    case 9:
      if (loRaFishColorField == 3 && (loRaFishNeedsAppearanceSetup || loRaFishNeedsPetSetup)) {
        loRaFishNeedsAppearanceSetup = false;
        loRaFishSyncDXAppearance();
        saveLoRaFishSettings();
        saveLoRaFish();
        if (loRaFishNeedsPetSetup) {
          loRaFishPage = LORAFISH_PET_SETUP_PAGE;
          alert("Colours ready!", 700);
        } else {
          loRaFishPage = 1;
          alert("Fish ready!", 700);
          return LORAFISH_MENU_HOME;                          // to the aquarium
        }
      } else {
        const LoRaFishSetting fields[] = {LoRaFishSetting::Scales, LoRaFishSetting::Body, LoRaFishSetting::Eyes};
        openLoRaFishValue(fields[loRaFishColorField % 3]);
      }
      break;
    case 10: {
      const LoRaFishSetting fields[] = {LoRaFishSetting::SleepHour, LoRaFishSetting::WakeHour};
      openLoRaFishValue(fields[loRaFishTimeField % LORAFISH_TIME_FIELDS]);
      break;
    }
    case 12:
      if (loRaFishDisplayOption == 2) {
        loRaFishPage = 8;
        openLoRaFishValue(LoRaFishSetting::Theme);
      } else if (loRaFishDisplayOption == 3) {
        loRaFishColorField = 0;
        loRaFishPage = 9;
      } else if (loRaFishDisplayOption == 4) {
        loRaFishPage = 15;
        openLoRaFishValue(LoRaFishSetting::Rendering);
      } else if (loRaFishDisplayOption == 5) {
        loRaFishPage = 15;
        openLoRaFishValue(LoRaFishSetting::Species);
      }
      break;
    case 4:
      // The overview stays in the main-page route; only Select enters contact browsing.
      loRaFishPage = LORAFISH_DX_CONTACT_PAGE;
      break;
    case LORAFISH_DX_CONTACT_PAGE:
      dxSendOrRecall();
      break;
    case 5:
      if (!LoRaFishOptions::available(loRaFishOption, loRaFishHasSound, loRaFishCanBreed(), loragotchiDXGuest.present)) {
        loRaFishOption = LoRaFishOption::Advert;
        break;   // a disappearing optional action must not select a different action
      }
      switch (loRaFishOption) {
        case LoRaFishOption::ReturnGuest:
          alert(the_mesh.returnLoRaFishGuest() ? "Return queued" : "No visitor owner", 1100);
          break;
        case LoRaFishOption::Advert:
          loRaFishAdvertMenu.open();
          loRaFishPage = LORAFISH_ADVERT_PAGE;
          break;
        case LoRaFishOption::Reset:
          loRaFishResetConfirmation.open();
          loRaFishPage = LORAFISH_RESET_CONFIRM_PAGE;
          break;
        case LoRaFishOption::Display:
          loRaFishDisplayOption = 2;                          // first Display row Meck keeps
          loRaFishPage = 12;
          break;
        case LoRaFishOption::Schedule:
          loRaFishTimeField = 0;
          loRaFishPage = 10;
          break;
        case LoRaFishOption::Breed:
          if (breedLoRaFishWithGuest()) {
            loRaFishOption = LoRaFishOption::Advert;
            alert("Egg saved", 1100);
          }
          break;
        default:
          break;
      }
      break;
    default:
      break;   // Stats: nothing to select
  }
  return LORAFISH_MENU_STAY;
}

// Back one level (LoRaGotchi handleDoubleClick).
static int back() {
  if (dismissLoRaFishNotice()) return LORAFISH_MENU_STAY;
  if (loRaFishAdoptionEligible) { closeLoRaFishAdoption(); return LORAFISH_MENU_STAY; }
  if (loRaFishValuePicker.active()) { cancelLoRaFishValue(); return LORAFISH_MENU_STAY; }
  switch (loRaFishPage) {
    case LORAFISH_ADVERT_PAGE:        closeLoRaFishAdvertMenu(); break;
    case LORAFISH_RESET_CONFIRM_PAGE: cancelLoRaFishReset(); break;
    case LORAFISH_DX_CONTACT_PAGE:    loRaFishPage = 4; break;
    case 10: returnToLoRaFishOptions(LoRaFishOption::Schedule); break;
    case 12: returnToLoRaFishOptions(LoRaFishOption::Display); break;
    case 8:  loRaFishDisplayOption = 2; loRaFishPage = 12; break;
    case 9:
      if (loRaFishNeedsPetSetup) loRaFishPage = LORAFISH_PET_SETUP_PAGE;
      else if (loRaFishNeedsAppearanceSetup) return LORAFISH_MENU_EXIT;   // setup waits on the aquarium page
      else { loRaFishDisplayOption = 3; loRaFishPage = 12; }
      break;
    case 15: loRaFishDisplayOption = 4; loRaFishPage = 12; break;
    default: return LORAFISH_MENU_EXIT;                       // main pages, pet setup -> Games menu
  }
  return LORAFISH_MENU_STAY;
}

static void ensureMenuPage() {
  if (loRaFishNeedsPetSetup) { if (loRaFishPage != 9) loRaFishPage = LORAFISH_PET_SETUP_PAGE; return; }
  if (loRaFishNeedsAppearanceSetup) { loRaFishPage = 9; return; }
  switch (loRaFishPage) {
    case 1: case 4: case 5: case 8: case 9: case 10: case 12: case 13: case 15:
    case LORAFISH_RESET_CONFIRM_PAGE: case LORAFISH_ADVERT_PAGE:
    case LORAFISH_DX_CONTACT_PAGE: case LORAFISH_PET_SETUP_PAGE:
      break;
    default:
      loRaFishPage = 1;
      break;
  }
}

// New pet not set up yet: prompt on the aquarium page.
static void drawSetupPrompt() {
  DisplayDriver& d = menu_display;
  drawLoRaFishHeader(d, "NEW FISH", "");
  drawLoRaFishEggSprite(d, d.width() / 2, 22);
  d.setColor(UIColor::secondary_txt);
  d.drawTextCentered(d.width() / 2, 56, "ENTER: SET UP");
}

}  // namespace lfx

// =============================================================================
// Public entry points (LoRaFish.h)
// =============================================================================
using namespace lfx;

// LEG1 egg messages from MyMesh::queueMessage.
bool loragotchiEggMessage(const uint8_t* key, const char* text, char* reply, size_t size) {
  return loRaFishEggHandleMessage(key, text, reply, size);
}

// Panel placement (physical pixels). The aquarium page draws frame rows
// 16-127 (rows 0-15 held only the fish header, left out on Meck) as a
// 240x210 picture centred in the space under Meck's home header; the menus
// draw the whole frame as 240x240 with two lines of key hints underneath.
static constexpr int AQUARIUM_SRC_TOP = 16;
static constexpr int AQUARIUM_Y = 76;
static constexpr int MENU_Y = 40;
static constexpr int MENU_HINT_Y1 = 290;
static constexpr int MENU_HINT_Y2 = 302;

// Full refresh (the black/white flash) every minute while the aquarium stays
// on screen, to clear the residue partial refreshes leave behind.
static constexpr unsigned long AQUARIUM_FULL_REFRESH_MS = 60000UL;   // 1 minute
static unsigned long s_lastAquariumMs = 0;
static unsigned long s_lastAquariumFullMs = 0;

void loRaFishBegin(UITask* task, NodePrefs* prefs, mesh::RTCClock* rtc) {
  s_task = task;
  loRaFishRTC = rtc;
  loRaFishMeshNodeName = prefs ? prefs->node_name : nullptr;
  beginLoRaFishChannelNotice();
  beginLoRaFishNameNotice();
  loRaFishColorDisplay = true;    // colour mode, drawn in greys (see the top of the file)
  loadLoRaFish();
  loadLoRaFishSettings();
  loadLoRaFishEggReceipt();
  applyLoRaFishTheme();
  loRaFishSyncDXAppearance();
  loRaFishBootMs = millis();
  ensureMenuPage();
}

// Game ticks (LoRaGotchi UITask::loop). Sounds are left out.
void loRaFishLoop(bool aquariumOnScreen) {
  const bool menuOnScreen = s_task && s_task->isOnLoRaFishScreen();
  // A hidden prompt must never leave a destructive choice armed, and every
  // exit from the fish screen drops an unsaved picker draft.
  if (!menuOnScreen) {
    if (loRaFishPage == LORAFISH_RESET_CONFIRM_PAGE) cancelLoRaFishReset();
    if (loRaFishPage == LORAFISH_ADVERT_PAGE) closeLoRaFishAdvertMenu();
    if (loRaFishValuePicker.active()) cancelLoRaFishValue();
  }
  serviceLoRaFishAdoption((aquariumOnScreen || menuOnScreen) &&
                          !loRaFishNameNotice && !loRaFishChannelNotice);
  tickLoRaFish();
  loRaFishObserveTravelTransitions();

  // The screen being off does not pause care. Actual meals are limited to one
  // per hour; skipped thresholds never build up a backlog.
  consumeLoRaFishMeals(aquariumOnScreen && !loRaFishFeeding);
  tickLoRaFishPoop();
}

// LoRaGotchi UITask::handleLoRaFishMessage (without jumping to the aquarium).
void loRaFishOnMessage() {
  fish.messages++;
  reactLoRaFish(1);
}

// LoRaGotchi UITask::onDiscoveredContact -> notify(newContactMessage).
void loRaFishOnContactAdvert() {
  reactLoRaFish(3);
}

// LoRaGotchi UITask::notify(ack).
void loRaFishOnAck() {
  fish.txAcks++;
  reactLoRaFish(2);
}

// Save everything before a shutdown or restart (LoRaGotchi UITask::shutdown).
void loRaFishFlush() {
  flushLoRaFish();
}

int loRaFishRenderAquarium(GxEPDDisplay& d) {
  if (!frame.ensure()) return 1000;

  // A gap of more than 5 s since the last aquarium picture is a new visit to
  // the page: start the minute from now.
  const unsigned long now = millis();
  if (s_lastAquariumMs == 0 || now - s_lastAquariumMs > 5000UL) {
    s_lastAquariumFullMs = now;
  } else if (now - s_lastAquariumFullMs >= AQUARIUM_FULL_REFRESH_MS) {
    d.requestFullRefresh();
    s_lastAquariumFullMs = now;
  }
  s_lastAquariumMs = now;

  if (millis() >= fishReactionUntil) fishReaction = 0;
  const bool message = loRaFishChannelNotice || loRaFishNameNotice || loRaFishAdoptionEligible;
  const bool setup = loRaFishNeedsPetSetup || loRaFishNeedsAppearanceSetup;
  menu_display.startFrame();
  if (message) {
    drawLoRaFish(menu_display, s_task);     // a notice, or the egg search
  } else if (setup) {
    drawSetupPrompt();
  } else {
    const uint8_t keep = loRaFishPage;
    loRaFishPage = 0;                       // page 0 = aquarium / egg / sleeping / memorial
    s_noTankHeader = true;
    drawLoRaFish(tank_display, s_task);
    s_noTankHeader = false;
    loRaFishPage = keep;
  }
  frame.push(d, AQUARIUM_SRC_TOP, FRAME_H - AQUARIUM_SRC_TOP, AQUARIUM_Y);
  if (message || setup) return 1000;
  return loRaFishSimulationMode ? 250 : 100;   // LoRaGotchi's mono-board rates
}

int loRaFishRenderMenu(GxEPDDisplay& d) {
  if (!frame.ensure()) return 1000;
  ensureMenuPage();
  menu_display.startFrame();
  drawLoRaFish(menu_display, s_task);
  frame.push(d, 0, FRAME_H, MENU_Y);
  // Key hints. D and A are LoRaGotchi's short press, Enter is its long press.
  static const char* hint1 = "A/D:Prev/Next  W/S:Up/Down";
  static const char* hint2 = "Enter:Select  Q:Back";
  d.drawTextRawTracked((PANEL_W - (int)strlen(hint1) * 6) / 2, MENU_HINT_Y1, hint1, d.rawFgColor());
  d.drawTextRawTracked((PANEL_W - (int)strlen(hint2) * 6) / 2, MENU_HINT_Y2, hint2, d.rawFgColor());
  return (loRaFishPage == 4 || loRaFishPage == LORAFISH_DX_CONTACT_PAGE ||
          loRaFishAdoptionEligible) ? 1000 : 2000;   // these pages update their status
}

bool loRaFishAquariumHold() {
  if (dismissLoRaFishNotice()) return false;
  if (loRaFishAdoptionEligible) { closeLoRaFishAdoption(); return false; }
  if (loRaFishNeedsPetSetup || loRaFishNeedsAppearanceSetup) {
    return true;                            // open the fish screen on the setup page
  }
  if (loRaFishPoopCount > 0 && !loRaFishCleaning) {
    startLoRaFishCleaning();
    alert("Cleaning tank!", 450);
  }
  return false;
}

void loRaFishOpenMenu() {
  loRaFishPage = 1;
  if (loRaFishNeedsAppearanceSetup) loRaFishColorField = 0;
  ensureMenuPage();
}

// Touch on the fish screen, in physical panel pixels: the top of the frame
// (its first 48 rows) = back one level, right half = next, left half =
// previous, as on the watch.
int loRaFishMenuTap(int px, int py) {
  const int fy = ((py - MENU_Y) * 8) / 15;      // frame row under the finger
  if (fy < 48) return back();
  press(px < PANEL_W / 2);
  return LORAFISH_MENU_STAY;
}

int loRaFishMenuSwipe(int dx, int dy) {
  const bool horizontal = abs(dx) > abs(dy);
  const bool overlay = loRaFishChannelNotice || loRaFishNameNotice ||
                       loRaFishAdoptionEligible || loRaFishValuePicker.active();
  if (horizontal && !overlay && mainPageIndex(loRaFishPage) >= 0) {
    stepMainPage(dx < 0 ? 1 : -1);
  } else if (hasList()) {
    press(horizontal ? dx > 0 : dy < 0);
  }
  return LORAFISH_MENU_STAY;
}

int loRaFishMenuHold() { return select(); }

// Keyboard on the fish screen: Q / Shift+Del = back, D = next (tap right),
// A = previous (tap left), W / S = up / down (swipe), Enter = select.
int loRaFishMenuKey(char c) {
  switch (c) {
    case 'q': case KEY_CANCEL: return back();
    case 'd':  press(false); return LORAFISH_MENU_STAY;
    case 'a':  press(true);  return LORAFISH_MENU_STAY;
    case 'w':  if (hasList()) press(true);  return LORAFISH_MENU_STAY;
    case 's':  if (hasList()) press(false); return LORAFISH_MENU_STAY;
    case '\r': return select();
    default:   return LORAFISH_MENU_STAY;
  }
}

#endif  // MECK_LORAFISH