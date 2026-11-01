#pragma once
// =============================================================================
// SOS screen (docs/sos.md)
//
// Countdown, the running SOS (position, how often it was sent, how many
// repeaters passed it on, the last message that came in) and a small menu.
// The menu starts with nothing selected; the top button moves the selection
// (SOS / I'm OK / Stop, by state), Space confirms and Q backs out, so a
// single stray key never sends anything. While the device is locked the
// lock screen draws this screen without the menu.
// =============================================================================

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckSos.h"
#include "MeckLang.h"
#include "../NodePrefs.h"
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif

extern MeckSos meckSos;
// main.cpp: carry out a menu choice (SosScreen::ITEM_*)
extern void meckSosConfirm(int item);
extern int meckSosBattPct();

class SosScreen : public UIScreen {
public:
  enum Item : uint8_t { ITEM_NONE, ITEM_SOS, ITEM_OK, ITEM_STOP };

  SosScreen(mesh::RTCClock* rtc, NodePrefs* prefs) : _rtc(rtc), _prefs(prefs) {}

  void resetMenu() { _sel = 0; }
  bool wantsExit() const { return _exit; }
  void clearExit() { _exit = false; }

  bool handleInput(char c) override {
    if (meckSos.state() == MeckSos::COUNTDOWN) return true;   // main.cpp cancels it first
    Item items[3];
    int n = menuItems(items);
    if (_sel > n) _sel = 0;
    if (c == KEY_NEXT || c == 's' || c == 'S' || c == 'd' || c == 'D') {
      _sel = (_sel + 1) % (n + 1);            // 0 = nothing selected
      return true;
    }
    if (c == 'w' || c == 'W' || c == 'a' || c == 'A' || c == KEY_PREV) {
      _sel = (_sel + n) % (n + 1);
      return true;
    }
    if (c == ' ') {
      if (_sel == 0) return true;             // nothing selected: nothing happens
      Item it = items[_sel - 1];
      _sel = 0;
      meckSosConfirm(it);
      return true;
    }
    if (c == 'q' || c == 'Q' || c == KEY_CANCEL) {
      if (_sel != 0) _sel = 0;
      else _exit = true;
      return true;
    }
    return true;                              // Enter and the rest: ignored on purpose
  }

  int render(DisplayDriver& display) override {
#ifdef MECK_UI_V2
    return renderV2(display);
#else
    return renderClassic(display);
#endif
  }

  // Locked: drawn by the lock screen, without the menu
  void setLockedView(bool locked) { _locked = locked; }

private:
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  uint8_t _sel = 0;          // 0 = none, else 1 + index into menuItems()
  bool _exit = false;
  bool _locked = false;

  // Menu entries for the current state
  int menuItems(Item* out) const {
    switch (meckSos.state()) {
      case MeckSos::ACTIVE:     out[0] = ITEM_OK;  out[1] = ITEM_STOP; return 2;
      case MeckSos::CANCELLING: out[0] = ITEM_SOS; out[1] = ITEM_STOP; return 2;
      case MeckSos::COUNTDOWN:  return 0;
      default:                  out[0] = ITEM_SOS; out[1] = ITEM_OK;   return 2;
    }
  }

  static const char* itemLabel(Item it) {
    switch (it) {
      case ITEM_SOS:  return "SOS";
      case ITEM_OK:   return MECK_TR("I'm OK", "Tout va bien");
      case ITEM_STOP: return MECK_TR("Stop", "Arr\xC3\xAAter");
      default:        return "";
    }
  }

  // What Space does with the current selection
  const char* selHint(Item it) const {
    switch (it) {
      case ITEM_SOS:  return MECK_TR("Space: SOS countdown (5 s)", "Espace : compte \xC3\xA0 rebours SOS (5 s)");
      case ITEM_OK:   return meckSos.state() == MeckSos::ACTIVE
                               ? MECK_TR("Space: send I'm OK, cancel SOS", "Espace : envoyer OK, annuler SOS")
                               : MECK_TR("Space: send I'm OK once", "Espace : envoyer OK une fois");
      case ITEM_STOP: return MECK_TR("Space: stop without a message", "Espace : arr\xC3\xAAter sans message");
      default:        return MECK_TR("Top button: choose", "Bouton du haut : choisir");
    }
  }

  // "45.12345 N" / "25.12345 E"
  static void fmtCoord(char* out, size_t n, double v, bool lat) {
    char h = lat ? (v < 0 ? 'S' : 'N') : (v < 0 ? 'W' : 'E');
    snprintf(out, n, "%.5f %c", v < 0 ? -v : v, h);
  }

  // Local clock time ms from now, "14:35" ("" without a valid clock)
  void fmtClockIn(char* out, size_t n, uint32_t ms) const {
    out[0] = 0;
    uint32_t now = _rtc ? _rtc->getCurrentTime() : 0;
    if (now < 1700000000) return;
    int32_t local = (int32_t)(now + ms / 1000) + (int32_t)_prefs->utc_offset_hours * 3600;
    snprintf(out, n, "%02d:%02d", (int)(((local / 3600) % 24 + 24) % 24), (int)(((local / 60) % 60 + 60) % 60));
  }

  // "3x, next 14:35" / "3x, next in 4 min"
  void fmtSends(char* out, size_t n, uint16_t count, uint32_t now) const {
    char at[8];
    uint32_t in = meckSos.nextSendIn(now);
    fmtClockIn(at, sizeof(at), in);
    if (in == 0) snprintf(out, n, MECK_TR("%ux, sending", "%ux, envoi"), (unsigned)count);
    else if (at[0]) snprintf(out, n, MECK_TR("%ux, next %s", "%ux, suivant %s"), (unsigned)count, at);
    else snprintf(out, n, MECK_TR("%ux, next in %u min", "%ux, suivant dans %u min"), (unsigned)count, (unsigned)((in + 59999) / 60000));
  }

  static void fmtHeard(char* out, size_t n, uint8_t k) {
    if (k == 0) snprintf(out, n, "%s", MECK_TR("nobody yet", "personne"));
    else snprintf(out, n, k == 1 ? MECK_TR("%u repeater", "%u r\xC3\xA9p\xC3\xA9teur")
                                 : MECK_TR("%u repeaters", "%u r\xC3\xA9p\xC3\xA9teurs"), (unsigned)k);
  }

  // Fix line: "GPS fix 2m ago", "GPS searching...", "No GPS fix"
  void fmtFix(char* out, size_t n, uint32_t now) const {
    char age[12];
    if (meckSos.hasFix()) {
      MeckSos::formatAge(age, sizeof(age), meckSos.fixAgeMs(now));
      if (meckSos.gpsOn() && !meckSos.fixFresh(now))
        snprintf(out, n, MECK_TR("Fix %s ago, GPS searching", "Fix il y a %s, recherche GPS"), age);
      else
        snprintf(out, n, MECK_TR("GPS fix %s ago", "Fix GPS il y a %s"), age);
    } else {
      snprintf(out, n, "%s", meckSos.gpsOn() ? MECK_TR("GPS searching...", "Recherche GPS...")
                                             : MECK_TR("No GPS fix yet", "Pas encore de fix GPS"));
    }
  }

#ifdef MECK_UI_V2
  int renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    uint32_t now = millis();
    MeckSos::State st = meckSos.state();

    if (st == MeckSos::COUNTDOWN) {
      Ui2Chrome::statusHeader(c);
      c.fill(0, HDR_H, SCREEN_W, SCREEN_H - HDR_H);
      c.icon((SCREEN_W - 32) / 2, 44, UI2_IC_WARN, 32, false);
      c.textFit(PAD, 104, SCREEN_W - 2 * PAD, MECK_TR("SENDING SOS IN", "ENVOI SOS DANS"), UI2_F_HEAD18, false, UI2_CENTER);
      char num[6];
      snprintf(num, sizeof(num), "%d", meckSos.countdownSecs(now));
      c.textFit(0, 172, SCREEN_W, num, UI2_F_NUM40, false, UI2_CENTER);
      c.wrap(PAD, 214, SCREEN_W - 2 * PAD, 18, 3,
             MECK_TR("Press any key or the top button to cancel",
                     "Appuyez sur une touche ou le bouton du haut pour annuler"),
             UI2_F_LABEL, false, true, nullptr, UI2_CENTER);
      return 1000;
    }

    Ui2Chrome::statusHeader(c);
    const char* chip = st == MeckSos::ACTIVE ? MECK_TR("ACTIVE", "ACTIF")
                     : st == MeckSos::CANCELLING ? MECK_TR("CANCELLING", "ANNULATION") : nullptr;
    Ui2Chrome::titleBar(c, "SOS", nullptr, chip, UI2_IC_WARN, st != MeckSos::IDLE);

    int16_t y = BODY_Y + 4;
    // Banner: what is going on
    char line[96];
    if (st == MeckSos::ACTIVE || st == MeckSos::CANCELLING) {
      c.fill(PAD, y, SCREEN_W - 2 * PAD, 26);
      c.textFit(PAD + 4, Ui2Canvas::boxBaseline(UI2_F_HEAD16, y, 26), SCREEN_W - 2 * PAD - 8,
                st == MeckSos::ACTIVE ? MECK_TR("SOS is being sent", "SOS en cours d'envoi")
                                      : MECK_TR("Sending I'm OK", "Envoi de Tout va bien"),
                UI2_F_HEAD16, false, UI2_CENTER);
      y += 32;
    } else {
      if (_prefs->sos_btn)
        snprintf(line, sizeof(line), "%s", MECK_TR("Hold the top button 3 s to send an SOS with your position.",
                                                   "Maintenez le bouton du haut 3 s pour envoyer un SOS avec votre position."));
      else
        snprintf(line, sizeof(line), "%s", MECK_TR("SOS button is off in Settings. Use the menu below.",
                                                   "Bouton SOS d\xC3\xA9sactiv\xC3\xA9. Utilisez le menu."));
      int lines = c.wrap(PAD, y + 12, SCREEN_W - 2 * PAD, 15, 3, line, UI2_F_BODY);
      y += lines * 15 + 6;
      const char* out = nullptr;
      switch (meckSos.outcome()) {
        case MeckSos::OUT_OK_CONFIRMED:        out = MECK_TR("Last SOS: I'm OK was heard", "Dernier SOS : OK entendu"); break;
        case MeckSos::OUT_STOPPED:             out = MECK_TR("Last SOS: stopped", "Dernier SOS : arr\xC3\xAAt\xC3\xA9"); break;
        case MeckSos::OUT_COUNTDOWN_CANCELLED: out = MECK_TR("Last SOS: cancelled in countdown", "Dernier SOS : annul\xC3\xA9"); break;
        default: break;
      }
      if (out) { c.textFit(PAD, y + 10, SCREEN_W - 2 * PAD, out, UI2_F_META); y += 16; }
    }

    // Position, large, to read out to 112 / Salvamont once there is signal
    if (meckSos.hasFix()) {
      char a[24], b[24];
      fmtCoord(a, sizeof(a), meckSos.lat(), true);
      fmtCoord(b, sizeof(b), meckSos.lon(), false);
      c.textFit(PAD, y + 18, SCREEN_W - 2 * PAD, a, UI2_F_HEAD18, true, UI2_CENTER);
      c.textFit(PAD, y + 42, SCREEN_W - 2 * PAD, b, UI2_F_HEAD18, true, UI2_CENTER);
      y += 48;
    } else {
      c.textFit(PAD, y + 18, SCREEN_W - 2 * PAD, MECK_TR("Position unknown", "Position inconnue"), UI2_F_HEAD16, true, UI2_CENTER);
      y += 24;
    }
    fmtFix(line, sizeof(line), now);
    c.textFit(PAD, y + 10, SCREEN_W - 2 * PAD, line, UI2_F_META, true, UI2_CENTER);
    y += 18;

    char v[40];
    if (st == MeckSos::ACTIVE) {
      fmtSends(v, sizeof(v), meckSos.sends(), now);
      Ui2List::kvRow(c, y, MECK_TR("Sent", "Envoy\xC3\xA9"), v); y += 20;
      fmtHeard(v, sizeof(v), meckSos.heardCount());
      Ui2List::kvRow(c, y, MECK_TR("Heard by", "Entendu par"), v); y += 20;
    } else if (st == MeckSos::CANCELLING) {
      fmtSends(v, sizeof(v), meckSos.okSends(), now);
      Ui2List::kvRow(c, y, MECK_TR("I'm OK sent", "OK envoy\xC3\xA9"), v); y += 20;
      if (meckSos.okHeardCount() == 0 && meckSos.okSends() > 0) {
        Ui2List::kvRow(c, y, MECK_TR("Heard by", "Entendu par"), MECK_TR("NOT confirmed", "NON confirm\xC3\xA9")); y += 20;
      } else {
        fmtHeard(v, sizeof(v), meckSos.okHeardCount());
        Ui2List::kvRow(c, y, MECK_TR("Heard by", "Entendu par"), v); y += 20;
      }
    }
    snprintf(v, sizeof(v), "%d%%", meckSosBattPct());
    if (st != MeckSos::IDLE) { Ui2List::kvRow(c, y, MECK_TR("Battery", "Batterie"), v); y += 20; }

    // The last message that came in while the SOS runs: a reply
    int16_t menuTop = _locked ? FOOTER_Y - 2 : FOOTER_Y - 44;
    if (st != MeckSos::IDLE && meckSos.reply()[0] && y + 30 < menuTop) {
      c.sectionHeader(y + 2, MECK_TR("LAST MESSAGE", "DERNIER MESSAGE"));
      c.wrap(PAD, y + 30, SCREEN_W - 2 * PAD, 14, (menuTop - y - 20) / 14, meckSos.reply(), UI2_F_BODY);
    }

    if (_locked) {
      c.textFit(PAD, FOOTER_Y + 16, SCREEN_W - 2 * PAD,
                MECK_TR("Hold the top button 3 s for the menu", "Bouton du haut 3 s : menu"), UI2_F_META, true, UI2_CENTER);
    } else {
      // Menu: chips, nothing selected until the top button is pressed
      Item items[3];
      int n = menuItems(items);
      int16_t w = 0;
      for (int i = 0; i < n; i++) w += Ui2Canvas::chipWidth(itemLabel(items[i])) + (i ? 10 : 0);
      int16_t x = (SCREEN_W - w) / 2;
      int16_t cy = FOOTER_Y - 40;
      for (int i = 0; i < n; i++) {
        x += c.chip(x, cy, itemLabel(items[i]), UI2_NO_ICON, _sel == i + 1) + 10;
      }
      c.textFit(PAD, FOOTER_Y - 10, SCREEN_W - 2 * PAD, selHint(_sel ? items[_sel - 1] : ITEM_NONE),
                UI2_F_META, true, UI2_CENTER);
      Ui2Key keys[3] = { ui2Key("Btn", MECK_TR("Choose", "Choisir")),
                         ui2Key("Space", MECK_TR("Confirm", "Valider")),
                         ui2Key("Q", MECK_TR("Back", "Retour"), true) };
      Ui2Chrome::footer(c, keys, 3);
    }
    return st == MeckSos::IDLE ? 30000 : 60000;
  }
#else
  int renderClassic(DisplayDriver& d) {
    uint32_t now = millis();
    int cx = d.width() / 2;
    char line[64];
    d.setTextSize(1);
    d.setColor(DisplayDriver::LIGHT);
    MeckSos::State st = meckSos.state();
    if (st == MeckSos::COUNTDOWN) {
      d.drawTextCentered(cx, 20, MECK_TR("SENDING SOS IN", "ENVOI SOS DANS"));
      snprintf(line, sizeof(line), "%d", meckSos.countdownSecs(now));
      d.setTextSize(3);
      d.drawTextCentered(cx, 45, line);
      d.setTextSize(1);
      d.drawTextCentered(cx, 95, MECK_TR("Any key: cancel", "Une touche : annuler"));
      return 1000;
    }
    d.drawTextCentered(cx, 2, st == MeckSos::ACTIVE ? MECK_TR("SOS ACTIVE", "SOS ACTIF")
                             : st == MeckSos::CANCELLING ? MECK_TR("SENDING I'M OK", "ENVOI OK") : "SOS");
    int y = 16;
    if (meckSos.hasFix()) {
      fmtCoord(line, sizeof(line), meckSos.lat(), true);  d.drawTextCentered(cx, y, line); y += 11;
      fmtCoord(line, sizeof(line), meckSos.lon(), false); d.drawTextCentered(cx, y, line); y += 11;
    } else {
      d.drawTextCentered(cx, y, MECK_TR("Position unknown", "Position inconnue")); y += 11;
    }
    fmtFix(line, sizeof(line), now);
    d.drawTextEllipsized(2, y, d.width() - 4, line); y += 13;
    char v[40];
    if (st == MeckSos::ACTIVE || st == MeckSos::CANCELLING) {
      bool act = st == MeckSos::ACTIVE;
      fmtSends(v, sizeof(v), act ? meckSos.sends() : meckSos.okSends(), now);
      snprintf(line, sizeof(line), "%s %s", act ? MECK_TR("Sent", "Envoy\xC3\xA9") : "OK:", v);
      d.drawTextEllipsized(2, y, d.width() - 4, line); y += 11;
      fmtHeard(v, sizeof(v), act ? meckSos.heardCount() : meckSos.okHeardCount());
      snprintf(line, sizeof(line), MECK_TR("Heard by %s", "Entendu par %s"), v);
      d.drawTextEllipsized(2, y, d.width() - 4, line); y += 11;
      if (meckSos.reply()[0]) { d.drawTextEllipsized(2, y, d.width() - 4, meckSos.reply()); y += 11; }
    }
    if (!_locked) {
      Item items[3];
      int n = menuItems(items);
      line[0] = 0;
      for (int i = 0; i < n; i++) {
        size_t l = strlen(line);
        snprintf(line + l, sizeof(line) - l, _sel == i + 1 ? "[%s] " : " %s  ", itemLabel(items[i]));
      }
      d.drawTextCentered(cx, d.height() - 24, line);
      d.drawTextEllipsized(2, d.height() - 12, d.width() - 4, selHint(_sel ? items[_sel - 1] : ITEM_NONE));
    }
    return st == MeckSos::IDLE ? 30000 : 60000;
  }
#endif
};
