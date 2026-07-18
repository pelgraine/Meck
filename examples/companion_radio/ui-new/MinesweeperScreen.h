#pragma once

// =============================================================================
// MinesweeperScreen -- Classic Minesweeper for Meck e-ink devices
//
// 9x9 grid, 10 mines (classic Beginner difficulty) on T-Deck Pro.
// 14x14 grid, 25 mines on T5S3 (fills the larger virtual display).
// First reveal is always safe -- mines are placed after the first click.
// Fully turn-based: no tick timer, renders only on input. Perfect for e-ink.
//
// T-Deck Pro: 14x14 pixel cells (126x126 grid area on 240x320 display)
// T5S3:       14x14 grid at 8x8 pixel cells (112x112 on 128x128 virtual display)
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>

// Forward declarations
class UITask;

// -- Grid and cell parameters per platform --
#if defined(LilyGo_T5S3_EPaper_Pro)
  #define MINE_GRID_W   14
  #define MINE_GRID_H   14
  #define MINE_COUNT    25
  #define MINE_CELL_W    8
  #define MINE_CELL_H    8
  #define MINE_HDR      14
  #define MINE_FTR      10
#elif defined(LilyGo_TDeck_Pro_Max)
  // MAX renders into a 128x128 virtual canvas with non-uniform scaling
  // (X 1.875, Y 2.5) and a (2,5) virtual offset applied by GxEPDDisplay.
  // Cells are 8x6 virtual so they land square on the panel (8*1.875 = 6*2.5
  // = 15 px). The grid is sized to the usable area, and the display offset is
  // subtracted when centring so nothing clips at the bottom/right edge.
  #define MINE_GRID_W   15
  #define MINE_GRID_H   20
  #define MINE_COUNT    50
  #define MINE_CELL_W    8
  #define MINE_CELL_H    6
  #define MINE_HDR      14
  #define MINE_FTR      14
  #define MINE_OFFX_ADJ  2
  #define MINE_OFFY_ADJ  5
#else
  #define MINE_GRID_W    9
  #define MINE_GRID_H    9
  #define MINE_COUNT    10
  #define MINE_CELL_W   14
  #define MINE_CELL_H   14
  #define MINE_HDR      14
  #define MINE_FTR      14
#endif
#ifndef MINE_OFFX_ADJ
  #define MINE_OFFX_ADJ  0
#endif
#ifndef MINE_OFFY_ADJ
  #define MINE_OFFY_ADJ  0
#endif
// Cell-text size + vertical nudge. GxEPDDisplay::setCursor adds a +5 baseline
// fudge sized for the tall FreeSans fonts; for the small built-in (size 0)
// font on MAX that lands the glyph at the cell's bottom edge, so it needs a
// negative nudge to sit centred. T5S3 (FastEPD, no fudge) and Pro are unchanged.
#if defined(LilyGo_TDeck_Pro_Max)
  #define MINE_TEXT_SIZE  0
  #define MINE_TEXT_DY   (-3)
#elif defined(LilyGo_T5S3_EPaper_Pro)
  #define MINE_TEXT_SIZE  0
  #define MINE_TEXT_DY    1
#else
  #define MINE_TEXT_SIZE  1
  #define MINE_TEXT_DY    3
#endif
#define MINE_TOTAL    (MINE_GRID_W * MINE_GRID_H)

#define MINE_VALUE  9   // Content value indicating a mine

class MinesweeperScreen : public UIScreen {
public:
  enum GameState { READY, PLAYING, WON, LOST };
  enum CellState : uint8_t { CELL_HIDDEN, CELL_REVEALED, CELL_FLAGGED };

private:
  UITask* _task;
  bool _wantsExit;

  // Grid
  uint8_t _content[MINE_TOTAL];    // 0-8 = adjacent mine count, MINE_VALUE = mine
  CellState _cellState[MINE_TOTAL]; // Hidden, revealed, or flagged

  // Game state
  GameState _state;
  bool _minesPlaced;    // False until first reveal (first-click safety)
  int _cursorX, _cursorY;
  int _flagCount;
  int _revealedCount;

  // Display layout (recomputed each render based on game state)
  int _offsetX, _offsetY;
  bool _cursorBlink;     // Toggles each render for slow blink cursor

  // Simple xorshift PRNG
  uint16_t _rngState;

  uint16_t rng() {
    _rngState ^= _rngState << 7;
    _rngState ^= _rngState >> 9;
    _rngState ^= _rngState << 8;
    return _rngState;
  }

  // -- Grid helpers --

  int idx(int x, int y) const { return y * MINE_GRID_W + x; }
  bool inBounds(int x, int y) const { return x >= 0 && x < MINE_GRID_W && y >= 0 && y < MINE_GRID_H; }

  void resetGrid() {
    memset(_content, 0, sizeof(_content));
    for (int i = 0; i < MINE_TOTAL; i++) _cellState[i] = CELL_HIDDEN;
    _minesPlaced = false;
    _flagCount = 0;
    _revealedCount = 0;
    _cursorX = MINE_GRID_W / 2;
    _cursorY = MINE_GRID_H / 2;
  }

  // Place mines randomly, excluding the first-clicked cell and its neighbours
  void placeMines(int safeX, int safeY) {
    _rngState = (uint16_t)(millis() ^ 0xC0DE);
    int placed = 0;
    while (placed < MINE_COUNT) {
      int x = rng() % MINE_GRID_W;
      int y = rng() % MINE_GRID_H;
      // Skip safe zone (first click + 8 neighbours)
      if (abs(x - safeX) <= 1 && abs(y - safeY) <= 1) continue;
      // Skip if already a mine
      if (_content[idx(x, y)] == MINE_VALUE) continue;
      _content[idx(x, y)] = MINE_VALUE;
      placed++;
    }
    // Compute adjacency counts
    for (int cy = 0; cy < MINE_GRID_H; cy++) {
      for (int cx = 0; cx < MINE_GRID_W; cx++) {
        if (_content[idx(cx, cy)] == MINE_VALUE) continue;
        int count = 0;
        for (int dy = -1; dy <= 1; dy++) {
          for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = cx + dx, ny = cy + dy;
            if (inBounds(nx, ny) && _content[idx(nx, ny)] == MINE_VALUE) count++;
          }
        }
        _content[idx(cx, cy)] = count;
      }
    }
    _minesPlaced = true;
  }

  // Flood-fill reveal from (x,y). Reveals empty cells and their numbered borders.
  void floodReveal(int x, int y) {
    if (!inBounds(x, y)) return;
    int i = idx(x, y);
    if (_cellState[i] != CELL_HIDDEN) return;
    if (_content[i] == MINE_VALUE) return;

    _cellState[i] = CELL_REVEALED;
    _revealedCount++;

    // If this cell is 0 (no adjacent mines), reveal all neighbours
    if (_content[i] == 0) {
      for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
          if (dx == 0 && dy == 0) continue;
          floodReveal(x + dx, y + dy);
        }
      }
    }
  }

  // Reveal a cell. Returns false if mine was hit.
  bool revealCell(int x, int y) {
    int i = idx(x, y);
    if (_cellState[i] != CELL_HIDDEN) return true;  // Already revealed or flagged

    // First click: place mines safely
    if (!_minesPlaced) {
      placeMines(x, y);
    }

    if (_content[i] == MINE_VALUE) {
      // Hit a mine -- game over
      _cellState[i] = CELL_REVEALED;
      _state = LOST;
      revealAllMines();
      return false;
    }

    floodReveal(x, y);
    checkWin();
    return true;
  }

  void toggleFlag(int x, int y) {
    int i = idx(x, y);
    if (_cellState[i] == CELL_HIDDEN) {
      _cellState[i] = CELL_FLAGGED;
      _flagCount++;
    } else if (_cellState[i] == CELL_FLAGGED) {
      _cellState[i] = CELL_HIDDEN;
      _flagCount--;
    }
    // Can't flag revealed cells
  }

  void checkWin() {
    // Win when all non-mine cells are revealed
    if (_revealedCount == MINE_TOTAL - MINE_COUNT) {
      _state = WON;
      // Auto-flag remaining mines
      for (int i = 0; i < MINE_TOTAL; i++) {
        if (_content[i] == MINE_VALUE && _cellState[i] == CELL_HIDDEN) {
          _cellState[i] = CELL_FLAGGED;
          _flagCount++;
        }
      }
    }
  }

  void revealAllMines() {
    for (int i = 0; i < MINE_TOTAL; i++) {
      if (_content[i] == MINE_VALUE) {
        _cellState[i] = CELL_REVEALED;
      }
    }
  }

  // -- Drawing helpers --

  void drawCell(DisplayDriver& display, int gx, int gy) const {
    int px = _offsetX + gx * MINE_CELL_W;
    int py = _offsetY + gy * MINE_CELL_H;
    int i = idx(gx, gy);
    bool isCursor = (gx == _cursorX && gy == _cursorY && _state == PLAYING);

    if (_cellState[i] == CELL_HIDDEN) {
      if (isCursor && !_cursorBlink) {
        // Cursor blink OFF phase: outline only (visible gap in the solid grid)
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(px, py, MINE_CELL_W, MINE_CELL_H);
        display.drawRect(px + 1, py + 1, MINE_CELL_W - 2, MINE_CELL_H - 2);
      } else {
        // Solid filled with 1px inset (preserves grid lines between cells)
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(px + 1, py + 1, MINE_CELL_W - 2, MINE_CELL_H - 2);
      }
    } else if (_cellState[i] == CELL_FLAGGED) {
      if (isCursor && !_cursorBlink) {
        // Cursor blink OFF phase: outline with F
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(px, py, MINE_CELL_W, MINE_CELL_H);
        display.drawRect(px + 1, py + 1, MINE_CELL_W - 2, MINE_CELL_H - 2);
      } else {
        // Solid fill with 1px inset
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(px + 1, py + 1, MINE_CELL_W - 2, MINE_CELL_H - 2);
      }
      // F overlay
      display.setColor((isCursor && !_cursorBlink) ? DisplayDriver::LIGHT : DisplayDriver::DARK);
      display.setTextSize(MINE_TEXT_SIZE);
      display.drawTextCentered(px + MINE_CELL_W / 2, py + MINE_TEXT_DY, "F");
    } else {
      // Revealed cell: thin border
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(px, py, MINE_CELL_W, MINE_CELL_H);

      if (_content[i] == MINE_VALUE) {
        // Mine: solid dot in centre
        int dotR = (MINE_CELL_W < MINE_CELL_H ? MINE_CELL_W : MINE_CELL_H) / 4;
        if (dotR < 2) dotR = 2;
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(px + MINE_CELL_W/2 - dotR, py + MINE_CELL_H/2 - dotR, dotR*2, dotR*2);
      } else if (_content[i] > 0) {
        // Number 1-8
        char num[2] = { (char)('0' + _content[i]), '\0' };
        display.setColor(DisplayDriver::GREEN);
        display.setTextSize(MINE_TEXT_SIZE);
        display.drawTextCentered(px + MINE_CELL_W / 2, py + MINE_TEXT_DY, num);
      }
      // Revealed 0: just the border (already drawn)

      // Cursor on revealed cell: green double border
      if (isCursor) {
        display.setColor(DisplayDriver::GREEN);
        display.drawRect(px, py, MINE_CELL_W, MINE_CELL_H);
        display.drawRect(px + 1, py + 1, MINE_CELL_W - 2, MINE_CELL_H - 2);
      }
    }
  }

public:
  MinesweeperScreen(UITask* task)
    : _task(task), _wantsExit(false), _state(READY), _minesPlaced(false),
      _cursorX(MINE_GRID_W / 2), _cursorY(MINE_GRID_H / 2),
      _flagCount(0), _revealedCount(0),
      _offsetX(0), _offsetY(0), _cursorBlink(false), _rngState(0xBEEF) {
    memset(_content, 0, sizeof(_content));
    for (int i = 0; i < MINE_TOTAL; i++) _cellState[i] = CELL_HIDDEN;
  }

  bool wantsExit() const { return _wantsExit; }
  void clearExit() { _wantsExit = false; }
  GameState getState() const { return _state; }

  void enter() {
    _wantsExit = false;
    // If game was PLAYING, resume where we left off
    // If READY, WON, or LOST, show that state as-is
  }

  // ------- Input -------
  bool handleInput(char c) override {
    switch (_state) {
      case READY:
        if (c == '\r') {
          resetGrid();
          _state = PLAYING;
          return true;
        }
        if (c == KEY_CANCEL) { _wantsExit = true; return true; }
        return false;

      case PLAYING:
        switch (c) {
          case 'w': case 'W': if (_cursorY > 0) _cursorY--; return true;
          case 's': case 'S': if (_cursorY < MINE_GRID_H - 1) _cursorY++; return true;
          case 'a': case 'A': if (_cursorX > 0) _cursorX--; return true;
          case 'd': case 'D': if (_cursorX < MINE_GRID_W - 1) _cursorX++; return true;
          case '\r':
            revealCell(_cursorX, _cursorY);
            return true;
          case 'f': case 'F':
            toggleFlag(_cursorX, _cursorY);
            return true;
          case KEY_CANCEL:
            _wantsExit = true;
            return true;
          default: return false;
        }

      case WON:
      case LOST:
        if (c == '\r') {
          resetGrid();
          _state = PLAYING;
          return true;
        }
        if (c == KEY_CANCEL) { _wantsExit = true; return true; }
        return false;
    }
    return false;
  }

  // ------- Render -------
  int render(DisplayDriver& display) override {
    // Compute grid offset based on state
    // PLAYING: full screen (no header/footer) for clean grid
    // Other states: header + footer visible
    int gridPixW = MINE_GRID_W * MINE_CELL_W;
    int gridPixH = MINE_GRID_H * MINE_CELL_H;
    if (_state == PLAYING) {
      _offsetX = (display.width() - gridPixW) / 2 - MINE_OFFX_ADJ;
      _offsetY = (display.height() - gridPixH) / 2 - MINE_OFFY_ADJ;
    } else {
      int usableH = display.height() - MINE_HDR - MINE_FTR;
      _offsetX = (display.width() - gridPixW) / 2 - MINE_OFFX_ADJ;
      _offsetY = MINE_HDR + (usableH - gridPixH) / 2 - MINE_OFFY_ADJ;
    }

    // Toggle cursor blink each render cycle
    if (_state == PLAYING) _cursorBlink = !_cursorBlink;

    display.startFrame();
    display.setTextSize(1);

    if (_state == READY) {
      // --- READY: header + instructions + footer ---
      display.setColor(DisplayDriver::GREEN);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(display.width() / 2, 2, "Minesweeper");
#else
      display.setCursor(2, 2);
      display.print("Minesweeper");
#endif
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(0, MINE_HDR - 2, display.width(), 1);

      int cx = display.width() / 2;
      int y = MINE_HDR + 10;
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(cx, y, "Minesweeper");
      y += 16;
      display.setColor(DisplayDriver::GREEN);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(cx, y, "Swipe to move cursor");
      y += 11;
      display.drawTextCentered(cx, y, "Tap to reveal");
      y += 11;
      display.drawTextCentered(cx, y, "Long press to flag");
#else
      display.drawTextCentered(cx, y, "W/S/A/D to move cursor");
      y += 11;
      display.drawTextCentered(cx, y, "Enter to reveal a cell");
      y += 11;
      display.drawTextCentered(cx, y, "F to flag a mine");
#endif
      y += 16;
      display.setColor(DisplayDriver::LIGHT);
      char info[32];
      snprintf(info, sizeof(info), "%dx%d grid, %d mines", MINE_GRID_W, MINE_GRID_H, MINE_COUNT);
      display.drawTextCentered(cx, y, info);
      y += 16;
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(cx, y, "Tap to start");
#else
      display.drawTextCentered(cx, y, "Press Enter to start");
#endif

      // Footer
      display.setColor(DisplayDriver::LIGHT);
#if !defined(LilyGo_T5S3_EPaper_Pro)
      int fy = display.height() - 12;
      display.drawRect(0, fy - 2, display.width(), 1);
      display.setCursor(2, fy);
      display.print("Enter:Start  Q:Back");
#endif
      return 5000;

    } else if (_state == PLAYING) {
      // --- PLAYING: full screen grid, no header/footer ---

      // Draw grid border
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(_offsetX - 1, _offsetY - 1, gridPixW + 2, gridPixH + 2);

      // Draw all cells
      for (int gy = 0; gy < MINE_GRID_H; gy++) {
        for (int gx = 0; gx < MINE_GRID_W; gx++) {
          drawCell(display, gx, gy);
        }
      }

      return 100;  // Blink cycle -- clamped to 800ms by render floor

    } else {
      // --- WON / LOST: grid + overlay ---

      // Draw grid border
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(_offsetX - 1, _offsetY - 1, gridPixW + 2, gridPixH + 2);

      // Draw all cells (no cursor blink in end state)
      for (int gy = 0; gy < MINE_GRID_H; gy++) {
        for (int gx = 0; gx < MINE_GRID_W; gx++) {
          drawCell(display, gx, gy);
        }
      }

      // Overlay
      int cx = display.width() / 2;
      int cy = display.height() / 2;
      int boxW = display.width() * 3 / 4;
      int boxH = 50;
      int boxX = cx - boxW / 2;
      int boxY = cy - boxH / 2;

      display.setColor(DisplayDriver::DARK);
      display.fillRect(boxX, boxY, boxW, boxH);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(boxX, boxY, boxW, boxH);

      int ty = boxY + 10;
      if (_state == WON) {
        display.setColor(DisplayDriver::YELLOW);
        display.drawTextCentered(cx, ty, "Cleared!");
      } else {
        display.drawTextCentered(cx, ty, "Boom!");
      }
      ty += 16;
      display.setColor(DisplayDriver::GREEN);
      display.drawTextCentered(cx, ty, "Enter:Retry  Q:Back");

      return 5000;
    }
  }
};