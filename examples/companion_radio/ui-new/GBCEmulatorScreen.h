#pragma once

// =============================================================================
// GBCEmulatorScreen -- Game Boy / Game Boy Color emulator for Meck (T-Deck)
//
// Build 1: T-Deck Max only. Needs the raw joypad mode in the Max
// TCA8418Keyboard.h; the Pro follows once its keyboard driver has the same.
//
// Two modes. BROWSER lists the .gb/.gbc files in /roms on the SD card
// (W/S to move, Enter to play, Shift+Backspace back to the games menu).
// PLAYING runs the Peanut-GB core in its own FreeRTOS task on core 0 at the
// Game Boy's real 59.73 Hz while this screen shows a black-and-white snapshot
// of the game picture on the e-ink at the UI's normal refresh cadence.
// In-game keys: W/A/S/D d-pad, K = A, J = B, Enter = Start, Space = Select,
// Shift+Backspace = quit (back to the ROM list, save written).
//
// The core and everything that touches it live in GBCEmulatorScreen.cpp so
// the single-header core is compiled exactly once.
// =============================================================================

#if defined(LilyGo_TDeck_Pro_Max)

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>

class UITask;
class GxEPDDisplay;

#define GBC_MAX_ROMS  32
#define GBC_NAME_MAX  48

class GBCEmulatorScreen : public UIScreen {
public:
  enum Mode { BROWSER, PLAYING, STOPPING };

  GBCEmulatorScreen(UITask* task);

  // Called by UITask::gotoGBCScreen(): rescans /roms, clears flags.
  void enter();

  // Set when the user backs out of the ROM list. main.cpp polls this and
  // returns to the games menu, the same way it does for Snake.
  bool wantsExit() const { return _wantsExit; }

  // True while a ROM is running or shutting down. main.cpp holds the CPU
  // boost while this is true.
  bool isRunning() const { return _mode != BROWSER; }

  bool handleInput(char c) override;
  int  render(DisplayDriver& display) override;
  void poll() override;

private:
  void scanRoms();
  bool launch(int idx);
  void finishStop();
  void setStatus(const char* msg);
  int  renderBrowser(DisplayDriver& display);
  int  renderGame(DisplayDriver& display);

  UITask* _task;
  bool    _wantsExit;
  Mode    _mode;
  int     _cursor;
  int     _scroll;
  int     _romCount;
  int     _playing;                          // index into _romNames while running
  char    _romNames[GBC_MAX_ROMS][GBC_NAME_MAX];
  char    _status[48];
  unsigned long _statusUntil;
  GxEPDDisplay* _eink;                       // captured on first render
  bool    _busyHooked;                       // keyboard poll registered with GxEPD2
};

#endif // LilyGo_TDeck_Pro_Max