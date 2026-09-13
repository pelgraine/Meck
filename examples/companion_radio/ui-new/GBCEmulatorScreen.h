#pragma once

// =============================================================================
// GBCEmulatorScreen -- Game Boy / Game Boy Color emulator for Meck (T-Deck)
//
// T-Deck Pro and Max (both define LilyGo_TDeck_Pro). Needs the raw joypad
// mode in the variant's TCA8418Keyboard.h; the T5S3 is excluded (different
// display class).
//
// Two modes. BROWSER lists the .gb/.gbc files in /roms on the SD card
// (W/S to move, Enter to play, Shift+Backspace back to the games menu).
// PLAYING runs the Peanut-GB core in its own FreeRTOS task on core 0 at the
// Game Boy's real 59.73 Hz while this screen shows a black-and-white snapshot
// of the game picture on the e-ink at the UI's normal refresh cadence.
// In-game keys: W/A/S/D d-pad, K = A, J = B, Enter = Start, Space = Select,
// Q or Shift+Backspace = quit (back to the ROM list, save written).
//
// The core and everything that touches it live in GBCEmulatorScreen.cpp so
// the single-header core is compiled exactly once.
// =============================================================================

#if defined(LilyGo_TDeck_Pro)

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>

class UITask;
class GxEPDDisplay;

#define GBC_MAX_ROMS  32
#define GBC_NAME_MAX  48

class GBCEmulatorScreen : public UIScreen {
public:
  enum Mode { BROWSER, LOADING, PLAYING, STOPPING };

  GBCEmulatorScreen(UITask* task);

  // Called by UITask::gotoGBCScreen(): rescans /roms, clears flags.
  void enter();

  // Set when the user backs out of the ROM list. main.cpp polls this and
  // returns to the games menu, the same way it does for Snake.
  bool wantsExit() const { return _wantsExit; }

  // True while a ROM is loading, running or shutting down. main.cpp holds
  // the CPU boost while this is true.
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
  int     _pendingLaunch;                    // ROM chosen; launched once "Loading..." is on the panel
  bool    _loadingDrawn;                     // set by renderBrowser() while LOADING
  char    _romNames[GBC_MAX_ROMS][GBC_NAME_MAX];
  char    _status[48];
  unsigned long _statusUntil;
  GxEPDDisplay* _eink;                       // captured on first render
  bool    _busyHooked;                       // keyboard poll registered with GxEPD2
  bool    _releaseKbAfterDraw;               // quit: keep raw mode until the ROM list is drawn
  bool    _browserDrawn;                     // set by renderBrowser()
};

#endif // LilyGo_TDeck_Pro