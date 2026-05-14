#pragma once

// =============================================================================
// GamesMenuScreen -- Game launcher menu for Meck
//
// Lists available games. W/S to navigate, Enter to launch, Q to exit.
// Uses wantsExit() and wantsLaunch() flags for navigation -- same pattern
// as ChannelPickerScreen.
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>

// Forward declarations
class UITask;

// Game identifiers -- add new entries here as games are added
enum GameID {
  GAME_NONE = 0,
  GAME_SNAKE,
  // GAME_MINESWEEPER,
  // GAME_2048,
  GAME_COUNT   // Must be last -- used for array sizing
};

class GamesMenuScreen : public UIScreen {
private:
  UITask* _task;
  bool _wantsExit;
  bool _wantsLaunch;
  int _cursor;
  GameID _selectedGame;

  // Game registry -- add new games here
  struct GameEntry {
    GameID id;
    const char* name;
    const char* description;
  };

  static constexpr int NUM_GAMES = 1;  // Increment as games are added

  static const GameEntry* getGames() {
    static const GameEntry games[NUM_GAMES] = {
      { GAME_SNAKE,       "Snake",       "Classic Nokia-style" },
      // { GAME_MINESWEEPER, "Minesweeper", "Tap to reveal" },
      // { GAME_2048,        "2048",        "Slide and merge" },
    };
    return games;
  }

public:
  GamesMenuScreen(UITask* task)
    : _task(task), _wantsExit(false), _wantsLaunch(false),
      _cursor(0), _selectedGame(GAME_NONE) {}

  bool wantsExit() const { return _wantsExit; }
  bool wantsLaunch() const { return _wantsLaunch; }
  GameID selectedGame() const { return _selectedGame; }

  void enter() {
    _wantsExit = false;
    _wantsLaunch = false;
    _selectedGame = GAME_NONE;
    // Preserve cursor position so returning from a game stays on the same entry
  }

  void clearFlags() {
    _wantsExit = false;
    _wantsLaunch = false;
    _selectedGame = GAME_NONE;
  }

  // ------- Input -------
  bool handleInput(char c) override {
    switch (c) {
      case 'w': case 'W':
        if (_cursor > 0) _cursor--;
        return true;
      case 's': case 'S':
        if (_cursor < NUM_GAMES - 1) _cursor++;
        return true;
      case '\r':
        _selectedGame = getGames()[_cursor].id;
        _wantsLaunch = true;
        return true;
      case 'q': case 'Q':
        _wantsExit = true;
        return true;
      default:
        return false;
    }
  }

  // ------- Render -------
  int render(DisplayDriver& display) override {
    display.startFrame();
    display.setTextSize(1);

    // --- Header ---
    display.setColor(DisplayDriver::GREEN);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.drawTextCentered(display.width() / 2, 2, "Games");
#else
    display.setCursor(2, 2);
    display.print("Games");
#endif
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 12, display.width(), 1);

    // --- Game list ---
    int y = 18;
#if defined(LilyGo_T5S3_EPaper_Pro)
    int lineH = 16;
#else
    int lineH = 20;
#endif

    for (int i = 0; i < NUM_GAMES; i++) {
      bool selected = (i == _cursor);

      if (selected) {
        // Highlight bar
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y - 1, display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

#if defined(LilyGo_T5S3_EPaper_Pro)
      // T5S3: name centred, description below
      display.drawTextCentered(display.width() / 2, y + 1, getGames()[i].name);
      if (!selected) display.setColor(DisplayDriver::GREEN);
      // Description on next line at smaller size
      display.setTextSize(0);
      display.drawTextCentered(display.width() / 2, y + 9, getGames()[i].description);
      display.setTextSize(1);
      lineH = 20;  // Accommodate two-line entries
#else
      // T-Deck Pro: name + description on one line
      char buf[48];
      snprintf(buf, sizeof(buf), "%s", getGames()[i].name);
      display.setCursor(6, y + 2);
      display.print(buf);

      // Description in green (or dark if highlighted)
      if (!selected) display.setColor(DisplayDriver::GREEN);
      int nameW = display.getTextWidth(buf);
      display.setCursor(6 + nameW + 8, y + 2);
      display.print(getGames()[i].description);
#endif

      y += lineH;
    }

    // --- Footer ---
    display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.setTextSize(0);
    display.drawTextCentered(display.width() / 2, display.height() - 8, "Tap to select");
    display.setTextSize(1);
#else
    display.setTextSize(1);
    int fy = display.height() - 12;
    display.drawRect(0, fy - 2, display.width(), 1);
    display.setCursor(2, fy);
    display.print("Enter:Play  Q:Back");
#endif

    return 5000;  // Static menu -- slow refresh
  }

  // --- T5S3 touch: tap to select game entry ---
  int selectRowAtVY(int vy) {
    int y = 18;
#if defined(LilyGo_T5S3_EPaper_Pro)
    int lineH = 20;
#else
    int lineH = 20;
#endif
    if (vy < y) return 0;  // Above list
    int row = (vy - y) / lineH;
    if (row >= NUM_GAMES) return 0;  // Below list

    if (row == _cursor) {
      // Tapped current selection -- launch
      _selectedGame = getGames()[_cursor].id;
      _wantsLaunch = true;
      return 2;
    }
    _cursor = row;
    return 1;  // Moved cursor
  }
};