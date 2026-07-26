#pragma once

// =============================================================================
// SnakeScreen -- Classic Nokia-style Snake for Meck e-ink devices
//
// T-Deck Pro: 8x8 pixel cells on 240x320 display
// T5S3:       4x4 pixel cells on 128x128 virtual display
//
// The 800ms partial refresh floor naturally produces Nokia-era tick speed.
// Snake body stored as circular buffer -- ~1KB, no PSRAM needed.
// Game state persists when switching screens (auto-pause on exit).
//
// High scores: top 10 scores with dates stored to /games/snake_hi.dat on SD.
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <SD.h>

// Forward declarations
class UITask;

// -- Grid sizing per platform --
#if defined(LilyGo_T5S3_EPaper_Pro)
  #define SNAKE_CELL    4
  #define SNAKE_HDR    14
  #define SNAKE_FTR    10
#else
  #define SNAKE_CELL    8
  #define SNAKE_HDR    14
  #define SNAKE_FTR    14
#endif

#define SNAKE_MAX_LEN    512
#define SNAKE_HI_COUNT   10
#define SNAKE_HI_PATH    "/games/snake_hi.dat"
#define SNAKE_HI_VERSION 1

// Tick cadence: the watch LCD has no e-ink refresh limit, so run a touch faster.
  #define SNAKE_TICK_MS  500

class SnakeScreen : public UIScreen {
public:
  enum GameState { READY, PLAYING, GAME_OVER };
  enum Direction { UP, DOWN, LEFT, RIGHT };

  struct HiScoreEntry {
    uint16_t score;
    uint32_t timestamp;  // Unix epoch from RTC
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  bool _wantsExit;

  // Grid dimensions (computed once from display size on first render)
  int _gridW, _gridH;
  int _offsetX, _offsetY;

  // Snake body circular buffer
  struct Cell { uint8_t x, y; };
  Cell _body[SNAKE_MAX_LEN];
  int _headIdx;
  int _length;

  // Game state
  GameState _state;
  Direction _dir;
  Direction _pendingDir;
  Cell _food;
  int _score;
  unsigned long _lastTick;
  unsigned long _tickInterval;

  // High scores
  HiScoreEntry _hiScores[SNAKE_HI_COUNT];
  int _hiCount;
  bool _newHiScore;
  int _newHiRank;   // 0-based rank of newly inserted score (-1 if none)

  // Simple xorshift PRNG
  uint16_t _rngState;

  uint16_t rng() {
    _rngState ^= _rngState << 7;
    _rngState ^= _rngState >> 9;
    _rngState ^= _rngState << 8;
    return _rngState;
  }

  void spawnFood() {
    for (int attempt = 0; attempt < 50; attempt++) {
      int fx = rng() % _gridW;
      int fy = rng() % _gridH;
      if (!isSnakeAt(fx, fy)) {
        _food.x = fx;
        _food.y = fy;
        return;
      }
    }
    for (int gy = 0; gy < _gridH; gy++) {
      for (int gx = 0; gx < _gridW; gx++) {
        if (!isSnakeAt(gx, gy)) {
          _food.x = gx;
          _food.y = gy;
          return;
        }
      }
    }
  }

  bool isSnakeAt(int x, int y) const {
    for (int i = 0; i < _length; i++) {
      int idx = (_headIdx - i + SNAKE_MAX_LEN) % SNAKE_MAX_LEN;
      if (_body[idx].x == x && _body[idx].y == y) return true;
    }
    return false;
  }

  Cell getHead() const { return _body[_headIdx]; }

  void resetGame() {
    int cx = _gridW / 2;
    int cy = _gridH / 2;
    _length = 3;
    _headIdx = 2;
    _body[0] = { (uint8_t)(cx - 2), (uint8_t)cy };
    _body[1] = { (uint8_t)(cx - 1), (uint8_t)cy };
    _body[2] = { (uint8_t)cx,       (uint8_t)cy };
    _dir = RIGHT;
    _pendingDir = RIGHT;
    _score = 0;
    _tickInterval = SNAKE_TICK_MS;
    _newHiScore = false;
    _newHiRank = -1;
    _rngState = (uint16_t)(millis() ^ 0xA5A5);
    spawnFood();
  }

  bool tick() {
    _dir = _pendingDir;
    Cell head = getHead();
    int nx = head.x;
    int ny = head.y;
    switch (_dir) {
      case UP:    ny--; break;
      case DOWN:  ny++; break;
      case LEFT:  nx--; break;
      case RIGHT: nx++; break;
    }

    if (nx < 0 || nx >= _gridW || ny < 0 || ny >= _gridH) {
      onDeath();
      return false;
    }

    bool eating = (nx == _food.x && ny == _food.y);
    int checkLen = eating ? _length : (_length - 1);
    for (int i = 0; i < checkLen; i++) {
      int idx = (_headIdx - i + SNAKE_MAX_LEN) % SNAKE_MAX_LEN;
      if (_body[idx].x == nx && _body[idx].y == ny) {
        onDeath();
        return false;
      }
    }

    _headIdx = (_headIdx + 1) % SNAKE_MAX_LEN;
    _body[_headIdx] = { (uint8_t)nx, (uint8_t)ny };

    if (eating) {
      _length++;
      if (_length >= SNAKE_MAX_LEN) _length = SNAKE_MAX_LEN;
      _score += 10;
      spawnFood();
    }
    return true;
  }

  void onDeath() {
    _state = GAME_OVER;
    if (_score > 0) {
      _newHiRank = insertHiScore(_score);
      _newHiScore = (_newHiRank >= 0);
      if (_newHiScore) saveHiScores();
    }
  }

  void drawCellFilled(DisplayDriver& display, int gx, int gy) const {
    int px = _offsetX + gx * SNAKE_CELL;
    int py = _offsetY + gy * SNAKE_CELL;
    display.fillRect(px, py, SNAKE_CELL, SNAKE_CELL);
  }

  void drawCellOutline(DisplayDriver& display, int gx, int gy) const {
    int px = _offsetX + gx * SNAKE_CELL;
    int py = _offsetY + gy * SNAKE_CELL;
    display.drawRect(px, py, SNAKE_CELL, SNAKE_CELL);
  }

  // --- High score persistence ---

  void loadHiScores() {
    _hiCount = 0;
    memset(_hiScores, 0, sizeof(_hiScores));
    if (!SD.exists(SNAKE_HI_PATH)) return;

    File f = SD.open(SNAKE_HI_PATH, FILE_READ);
    if (!f) return;

    uint8_t ver = 0;
    if (f.read(&ver, 1) != 1 || ver != SNAKE_HI_VERSION) { f.close(); return; }

    uint8_t count = 0;
    if (f.read(&count, 1) != 1) { f.close(); return; }
    if (count > SNAKE_HI_COUNT) count = SNAKE_HI_COUNT;

    for (int i = 0; i < count; i++) {
      uint8_t buf[6];
      if (f.read(buf, 6) != 6) break;
      _hiScores[i].score     = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
      _hiScores[i].timestamp = (uint32_t)buf[2] | ((uint32_t)buf[3] << 8)
                             | ((uint32_t)buf[4] << 16) | ((uint32_t)buf[5] << 24);
      _hiCount++;
    }
    f.close();
  }

  void saveHiScores() {
    if (!SD.exists("/games")) SD.mkdir("/games");
    if (SD.exists(SNAKE_HI_PATH)) SD.remove(SNAKE_HI_PATH);

    File f = SD.open(SNAKE_HI_PATH, FILE_WRITE);
    if (!f) return;

    uint8_t ver = SNAKE_HI_VERSION;
    f.write(&ver, 1);
    uint8_t count = (uint8_t)_hiCount;
    f.write(&count, 1);

    for (int i = 0; i < _hiCount; i++) {
      uint8_t buf[6];
      buf[0] = _hiScores[i].score & 0xFF;
      buf[1] = (_hiScores[i].score >> 8) & 0xFF;
      buf[2] = _hiScores[i].timestamp & 0xFF;
      buf[3] = (_hiScores[i].timestamp >> 8) & 0xFF;
      buf[4] = (_hiScores[i].timestamp >> 16) & 0xFF;
      buf[5] = (_hiScores[i].timestamp >> 24) & 0xFF;
      f.write(buf, 6);
    }
    f.close();
  }

  int insertHiScore(int score) {
    uint32_t now = (_rtc != nullptr) ? _rtc->getCurrentTime() : 0;
    int pos = _hiCount;
    for (int i = 0; i < _hiCount; i++) {
      if ((uint16_t)score > _hiScores[i].score) { pos = i; break; }
    }
    if (pos >= SNAKE_HI_COUNT) return -1;

    int newCount = _hiCount + 1;
    if (newCount > SNAKE_HI_COUNT) newCount = SNAKE_HI_COUNT;
    for (int i = newCount - 1; i > pos; i--) _hiScores[i] = _hiScores[i - 1];

    _hiScores[pos].score = (uint16_t)score;
    _hiScores[pos].timestamp = now;
    _hiCount = newCount;
    return pos;
  }

  static void formatDate(uint32_t ts, char* buf, int bufLen) {
    if (ts == 0) { snprintf(buf, bufLen, "--"); return; }
    uint32_t days = ts / 86400;
    int year = 1970;
    while (true) {
      int diy = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0) ? 366 : 365;
      if (days < (uint32_t)diy) break;
      days -= diy;
      year++;
    }
    static const int dim[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    static const char* mn[] = {"Jan","Feb","Mar","Apr","May","Jun",
                               "Jul","Aug","Sep","Oct","Nov","Dec"};
    bool leap = ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0);
    int month = 0;
    for (month = 0; month < 12; month++) {
      int d = dim[month];
      if (month == 1 && leap) d = 29;
      if (days < (uint32_t)d) break;
      days -= d;
    }
    if (month > 11) month = 11;
    snprintf(buf, bufLen, "%d %s %d", (int)(days + 1), mn[month], year);
  }

public:
  SnakeScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _wantsExit(false), _gridW(0), _gridH(0),
      _offsetX(0), _offsetY(0), _headIdx(0), _length(0),
      _state(READY), _dir(RIGHT), _pendingDir(RIGHT),
      _score(0), _lastTick(0), _tickInterval(SNAKE_TICK_MS), _rngState(0xBEEF),
      _hiCount(0), _newHiScore(false), _newHiRank(-1) {
    _food = {0, 0};
    memset(_body, 0, sizeof(_body));
    memset(_hiScores, 0, sizeof(_hiScores));
  }

  bool wantsExit() const { return _wantsExit; }
  void clearExit() { _wantsExit = false; }
  GameState getState() const { return _state; }

  void enter() {
    _wantsExit = false;
    loadHiScores();
    _lastTick = millis();
  }

  bool handleInput(char c) override {
    switch (_state) {
      case READY:
        if (c == '\r') { _state = PLAYING; _lastTick = millis(); return true; }
        if (c == KEY_CANCEL) { _wantsExit = true; return true; }
        return false;
      case PLAYING:
        switch (c) {
          case 'w': case 'W': if (_dir != DOWN)  _pendingDir = UP;    return true;
          case 's': case 'S': if (_dir != UP)    _pendingDir = DOWN;  return true;
          case 'a': case 'A': if (_dir != RIGHT) _pendingDir = LEFT;  return true;
          case 'd': case 'D': if (_dir != LEFT)  _pendingDir = RIGHT; return true;
          case KEY_CANCEL: _wantsExit = true; return true;
          default: return false;
        }
      case GAME_OVER:
        if (c == '\r') { resetGame(); _state = PLAYING; _lastTick = millis(); return true; }
        if (c == KEY_CANCEL) { _wantsExit = true; return true; }
        return false;
    }
    return false;
  }

  // Touch control (watch): a tap starts/restarts the game, or -- while playing --
  // steers by tapping the up/down/left/right zone relative to the board centre.
  // x/y are logical (render-space) coordinates, matching the board geometry.
  void handleTap(int x, int y) {
    if (_state != PLAYING) {
      if (_state == GAME_OVER) resetGame();   // mirror Enter: restart on game over
      _state = PLAYING;
      _lastTick = millis();
      return;
    }
    int cx = _offsetX + (_gridW * SNAKE_CELL) / 2;
    int cy = _offsetY + (_gridH * SNAKE_CELL) / 2;
    int dx = x - cx;
    int dy = y - cy;
    int adx = (dx < 0) ? -dx : dx;
    int ady = (dy < 0) ? -dy : dy;
    if (adx > ady) {                          // horizontal tap dominates
      if (dx > 0) { if (_dir != LEFT)  _pendingDir = RIGHT; }
      else        { if (_dir != RIGHT) _pendingDir = LEFT;  }
    } else {                                  // vertical tap dominates
      if (dy > 0) { if (_dir != UP)    _pendingDir = DOWN;  }
      else        { if (_dir != DOWN)  _pendingDir = UP;    }
    }
  }

  int render(DisplayDriver& display) override {
    if (_gridW == 0) {
      int usableW = display.width();
      int usableH = display.height() - SNAKE_HDR - SNAKE_FTR;
      _gridW = usableW / SNAKE_CELL;
      _gridH = usableH / SNAKE_CELL;
      _offsetX = (usableW - _gridW * SNAKE_CELL) / 2;
      _offsetY = SNAKE_HDR + (usableH - _gridH * SNAKE_CELL) / 2;
      resetGame();
    }

    display.startFrame();
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.drawTextCentered(display.width() / 2, 2, "Snake");
#else
    display.setCursor(2, 2);
    display.print("Snake");
    char scoreBuf[16];
    snprintf(scoreBuf, sizeof(scoreBuf), "Score: %d", _score);
    int sw = display.getTextWidth(scoreBuf);
    display.setCursor(display.width() - sw - 2, 2);
    display.print(scoreBuf);
#endif
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, SNAKE_HDR - 2, display.width(), 1);

    if (_state == READY) {
      int cx = display.width() / 2;
      int y = SNAKE_HDR + 6;
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(cx, y, "Classic Snake");
      y += 14;
      display.setColor(DisplayDriver::GREEN);
#if   defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(cx, y, "Swipe to steer");
#else
      display.drawTextCentered(cx, y, "W/S/A/D to steer");
#endif
      y += 11;
      display.drawTextCentered(cx, y, "Eat food to grow");
      y += 11;
      display.drawTextCentered(cx, y, "Steer clear of walls");
      y += 16;

      if (_hiCount > 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(cx, y, "-- High Scores --");
        y += 12;
        display.setColor(DisplayDriver::GREEN);
#if defined(LilyGo_T5S3_EPaper_Pro)
        int showCount = (_hiCount < 5) ? _hiCount : 5;
#else
        int showCount = (_hiCount < 10) ? _hiCount : 10;
#endif
        for (int i = 0; i < showCount; i++) {
          char dateBuf[16];
          formatDate(_hiScores[i].timestamp, dateBuf, sizeof(dateBuf));
          char line[48];
          snprintf(line, sizeof(line), "%d. %d  %s", i + 1, _hiScores[i].score, dateBuf);
          display.drawTextCentered(cx, y, line);
          y += 10;
        }
        y += 4;
      } else {
        y += 8;
      }

      display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(cx, y, "Tap to start");
#else
      display.drawTextCentered(cx, y, "Press Enter to start");
#endif
    } else {
      if (_state == PLAYING) {
        unsigned long now = millis();
        if (now - _lastTick >= _tickInterval) { tick(); _lastTick = now; }
      }

      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(_offsetX - 1, _offsetY - 1,
                       _gridW * SNAKE_CELL + 2, _gridH * SNAKE_CELL + 2);

      display.setColor(DisplayDriver::GREEN);
      drawCellFilled(display, _food.x, _food.y);

      display.setColor(DisplayDriver::LIGHT);
      for (int i = 0; i < _length; i++) {
        int idx = (_headIdx - i + SNAKE_MAX_LEN) % SNAKE_MAX_LEN;
        if (i == 0) drawCellFilled(display, _body[idx].x, _body[idx].y);
        else        drawCellOutline(display, _body[idx].x, _body[idx].y);
      }

      if (_state == GAME_OVER) {
        int cx = display.width() / 2;
        int cy = display.height() / 2;
        int boxW = display.width() * 3 / 4;
        int boxH = _newHiScore ? 60 : 50;
        int boxX = cx - boxW / 2;
        int boxY = cy - boxH / 2;

        display.setColor(DisplayDriver::DARK);
        display.fillRect(boxX, boxY, boxW, boxH);
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(boxX, boxY, boxW, boxH);

        int ty = boxY + 8;
        display.drawTextCentered(cx, ty, "Game Over");
        ty += 14;
        char finalScore[24];
        snprintf(finalScore, sizeof(finalScore), "Score: %d", _score);
        display.drawTextCentered(cx, ty, finalScore);
        ty += 12;
        if (_newHiScore) {
          display.setColor(DisplayDriver::YELLOW);
          char rankBuf[32];
          snprintf(rankBuf, sizeof(rankBuf), "New #%d High Score!", _newHiRank + 1);
          display.drawTextCentered(cx, ty, rankBuf);
          ty += 12;
        }
        display.setColor(DisplayDriver::GREEN);
        display.drawTextCentered(cx, ty, "Enter:Retry  Sh+Del:Back");
      }
    }

    display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
    char footBuf[32];
    snprintf(footBuf, sizeof(footBuf), "Score: %d", _score);
    display.setTextSize(0);
    display.drawTextCentered(display.width() / 2, display.height() - 8, footBuf);
    display.setTextSize(1);
#else
    display.setTextSize(1);
    int fy = display.height() - 12;
    display.drawRect(0, fy - 2, display.width(), 1);
    if (_state == PLAYING) {
      display.setCursor(2, fy);
      display.print("Sh+Del:Back");
    } else if (_state == READY) {
      display.setCursor(2, fy);
      display.print("Enter:Start  Sh+Del:Back");
    }
#endif

    if (_state == PLAYING) return 100;
    return 5000;
  }
};