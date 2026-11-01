#pragma once
// =============================================================================
// UI v2 -- screen chrome: status header, title bar and keycap footer
//
//   y   0-23  status header: node name, connections | clock | status icons, battery, %
//   y  24-47  title bar:     title (ellipsized) | chip | right text
//   y 296-319 footer:        up to 4 keycaps, each with a one-word label
//
// The status header reads ui2Status, a snapshot that UITask refreshes before
// every frame, so screens do not need access to the battery, GPS, etc.
// =============================================================================

#include "Ui2Canvas.h"

// Device status shown in the status header
struct Ui2Status {
  const char* nodeName = "";
  bool clockValid = false;
  uint8_t hour = 0, minute = 0;
  int batteryPct = 0;
  bool charging = false;
  bool unread = false;        // unread messages
  bool bleConnected = false;  // companion app connected over Bluetooth
  bool wifiOn = false;        // WiFi radio on
  uint8_t wifiLevel = 0;      // 0 = not joined to a network, 1-3 = signal
  bool gpsOn = false;
  bool gpsFix = false;        // the GPS has a fix (the icon gets its centre dot)
  bool alarmOn = false;
  bool sosActive = false;     // SOS counting down or being sent
  bool audioPlaying = false;
  bool silent = false;
  bool externalAntenna = false;
};
extern Ui2Status ui2Status;
// True when the last frame drew the v2 status header (the screen on show is
// a redesigned one). Touch handling uses it to pick the header tap band.
extern bool ui2FrameHasChrome;

// One footer hint: a keycap (key text or icon) and a one-word label
struct Ui2Key {
  const char* key;      // e.g. "W/S", or nullptr when `icon` is used
  Ui2Icon icon;         // UI2_NO_ICON when `key` is used
  const char* label;    // may be nullptr
  bool active;          // inverted keycap (e.g. Mute on)
  bool nav;             // navigation hint: its label is the first to drop
};

// Convenience constructors
static inline Ui2Key ui2Key(const char* key, const char* label, bool nav = false, bool active = false) {
  return Ui2Key{ key, UI2_NO_ICON, label, active, nav };
}
static inline Ui2Key ui2KeyIcon(Ui2Icon icon, const char* label, bool nav = false, bool active = false) {
  return Ui2Key{ nullptr, icon, label, active, nav };
}

namespace Ui2Chrome {
  // Status header, y 0-23
  void statusHeader(Ui2Canvas& c);
  // Title bar, y 24-47. `right` is a short right-aligned text ("1 / 7");
  // `chip` an optional chip before it.
  void titleBar(Ui2Canvas& c, const char* title, const char* right = nullptr,
                const char* chip = nullptr, Ui2Icon chipIcon = UI2_NO_ICON, bool chipActive = false);
  // Footer, y 296-319. When the hints do not fit, labels are dropped (the
  // keys stay): first those of navigation keys, then from the right.
  void footer(Ui2Canvas& c, const Ui2Key* keys, int count);
  // Header + optional title bar in one call
  inline void frame(Ui2Canvas& c, const char* title, const char* right = nullptr) {
    statusHeader(c);
    if (title) titleBar(c, title, right);
  }
}
