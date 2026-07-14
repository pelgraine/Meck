#pragma once

// =============================================================================
// WatchNotesScreen -- short notes for the LilyGo T-Watch S3 / S3 Plus.
//
// The T-Deck Pro's NotesScreen.h is bound to the SD card (list/read/write/
// rename all via SD.h), which the watches do not have. This is a separate,
// much smaller screen that stores notes as .txt files on the LittleFS "maps"
// partition (the same one WatchAlarmScreen uses for /alarms.cfg).
//
// Notes are composed and edited on the TWatchKeyboardScreen (purpose
// TWKB_NOTE), so a note is capped at the keyboard buffer (133 chars). Editing
// opens the keyboard pre-filled with the note text and saving replaces the
// file. Files longer than the view buffer (e.g. copied on by other means) are
// shown truncated, and editing one truncates it to the keyboard cap.
//
// Filenames are auto timestamps, YYMMDD-HHMMSS.txt in local time, so a plain
// descending name sort is newest-first. If the clock is not set yet the name
// falls back to u<millis>.txt.
//
// LIST: tap a row to select it, tap the selected row again (or press
// PWR/Enter) to open it; the [New] footer button composes a new note.
// VIEW: footer [Back] [Edit] [Del]; Del asks once ("Sure?") before deleting;
// tapping the text area scrolls when the note is longer than one screen.
// The top status strip is handled by main.cpp before this screen and returns
// home, matching the other watch screens.
//
// Geometry: the display is 240x240 physical, UI_ZOOM=2, so display.width()
// and display.height() both report 120 logical pixels.
// =============================================================================

// NOTE on include order: this header uses NodePrefs, which lives at
// examples/companion_radio/NodePrefs.h and is not on this variant's -I path.
// UITask.h pulls it in via "../NodePrefs.h", so WatchNotesScreen.h must always
// be included *after* UITask.h. Both call sites (UITask.cpp, main.cpp) do.
// This mirrors WatchAlarmScreen.h.
#include <Arduino.h>
#include <string.h>
#include <LittleFS.h>
#include <MeshCore.h>                    // mesh::RTCClock
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/LGFXDisplay.h>

class UITask;

#define WATCH_NOTES_DIR       "/notes"
#define WATCH_NOTES_MAX       24     // list cap
#define WATCH_NOTE_NAME_LEN   20     // "YYMMDD-HHMMSS.txt" + NUL
#define WATCH_NOTE_PREVIEW    22     // first-line preview chars kept per entry
#define WATCH_NOTE_BUF        512    // view/edit buffer (on-watch notes are <=133)
#define WATCH_NOTE_COLS       19     // wrap width at textSize(1) on 120px
#define WATCH_NOTE_VIEW_LINES 9      // wrapped lines visible above the footer

class WatchNotesScreen : public UIScreen {
public:
  enum Mode { LIST, VIEW };

private:
  UITask*          _task;
  mesh::RTCClock*  _rtc;
  NodePrefs*       _node_prefs;

  Mode  _mode = LIST;
  int   _sel = 0;                     // selected row in LIST
  int   _scroll = 0;                  // first visible row in LIST

  int   _count = 0;
  char  _names[WATCH_NOTES_MAX][WATCH_NOTE_NAME_LEN];
  char  _previews[WATCH_NOTES_MAX][WATCH_NOTE_PREVIEW];

  // VIEW state
  char  _curName[WATCH_NOTE_NAME_LEN];
  char  _noteBuf[WATCH_NOTE_BUF];
  int   _noteLen = 0;
  int   _lineCount = 0;
  int   _viewScroll = 0;              // first visible wrapped line
  bool  _delArmed = false;

  // Keyboard handshake (UITask::loop polls these; UITask is only forward
  // declared here, so this screen never calls it directly)
  bool  _wantKeyboard = false;
  bool  _editPending = false;         // true = prefill keyboard with _noteBuf
  char  _editName[WATCH_NOTE_NAME_LEN];

  int32_t localNow() const {
    uint32_t utc = _rtc->getCurrentTime();
    if (utc < 1700000000) return -1;   // clock not set yet
    return (int32_t)utc + ((int32_t)_node_prefs->utc_offset_hours * 3600);
  }

  // days-from-epoch -> civil date (Howard Hinnant's algorithm)
  static void civilFromDays(int32_t z, int& y, int& m, int& d) {
    z += 719468;
    int32_t era = (z >= 0 ? z : z - 146096) / 146097;
    uint32_t doe = (uint32_t)(z - era * 146097);
    uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int32_t yr = (int32_t)yoe + era * 400;
    uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    uint32_t mp = (5 * doy + 2) / 153;
    d = (int)(doy - (153 * mp + 2) / 5 + 1);
    m = (int)(mp < 10 ? mp + 3 : mp - 9);
    y = (int)(yr + (m <= 2));
  }

  void makeName(char* out, size_t outLen) {
    int32_t local = localNow();
    if (local < 0) {
      snprintf(out, outLen, "u%07lu.txt", (unsigned long)(millis() % 10000000UL));
      return;
    }
    int y, mo, d;
    civilFromDays(local / 86400, y, mo, d);
    int hh = (local / 3600) % 24;
    int mi = (local / 60) % 60;
    int ss = local % 60;
    snprintf(out, outLen, "%02d%02d%02d-%02d%02d%02d.txt",
             y % 100, mo, d, hh, mi, ss);
  }

  // "260714-173245.txt" -> "14/07 17:32"; u-names -> "no clock"
  static void dateLabel(const char* name, char* out, size_t outLen) {
    if (name[0] == 'u' || strlen(name) < 17) {
      snprintf(out, outLen, "no clock");
      return;
    }
    snprintf(out, outLen, "%.2s/%.2s %.2s:%.2s",
             name + 4, name + 2, name + 7, name + 9);
  }

  static const char* leafOf(const char* n) {
    const char* s = strrchr(n, '/');
    return s ? s + 1 : n;
  }

  void readPreview(const char* leaf, char* out, size_t outLen) {
    char path[40];
    snprintf(path, sizeof(path), WATCH_NOTES_DIR "/%s", leaf);
    out[0] = 0;
    File f = LittleFS.open(path, "r");
    if (!f) return;
    size_t i = 0;
    while (i < outLen - 1 && f.available()) {
      int ch = f.read();
      if (ch < 0 || ch == '\n' || ch == '\r') break;
      out[i++] = (char)ch;
    }
    out[i] = 0;
    f.close();
  }

  void scanNotes() {
    _count = 0;
    if (!LittleFS.exists(WATCH_NOTES_DIR)) LittleFS.mkdir(WATCH_NOTES_DIR);
    File root = LittleFS.open(WATCH_NOTES_DIR);
    if (!root || !root.isDirectory()) return;
    File f = root.openNextFile();
    while (f && _count < WATCH_NOTES_MAX) {
      if (!f.isDirectory()) {
        const char* leaf = leafOf(f.name());
        size_t n = strlen(leaf);
        if (n > 4 && n < WATCH_NOTE_NAME_LEN &&
            strcmp(leaf + n - 4, ".txt") == 0) {
          // insertion sort, descending name = newest first
          int pos = _count;
          while (pos > 0 && strcmp(leaf, _names[pos - 1]) > 0) pos--;
          for (int k = _count; k > pos; k--) {
            memcpy(_names[k], _names[k - 1], WATCH_NOTE_NAME_LEN);
          }
          strncpy(_names[pos], leaf, WATCH_NOTE_NAME_LEN - 1);
          _names[pos][WATCH_NOTE_NAME_LEN - 1] = 0;
          _count++;
        }
      }
      f = root.openNextFile();
    }
    root.close();
    for (int i = 0; i < _count; i++) readPreview(_names[i], _previews[i], WATCH_NOTE_PREVIEW);
    if (_sel >= _count) _sel = _count ? _count - 1 : 0;
    if (_scroll > _sel) _scroll = _sel;
  }

  void countLines() {
    _lineCount = (_noteLen + WATCH_NOTE_COLS - 1) / WATCH_NOTE_COLS;
    if (_lineCount < 1) _lineCount = 1;
  }

  bool loadNote(const char* leaf) {
    char path[40];
    snprintf(path, sizeof(path), WATCH_NOTES_DIR "/%s", leaf);
    File f = LittleFS.open(path, "r");
    if (!f) return false;
    _noteLen = f.read((uint8_t*)_noteBuf, WATCH_NOTE_BUF - 1);
    if (_noteLen < 0) _noteLen = 0;
    _noteBuf[_noteLen] = 0;
    f.close();
    strncpy(_curName, leaf, WATCH_NOTE_NAME_LEN - 1);
    _curName[WATCH_NOTE_NAME_LEN - 1] = 0;
    countLines();
    _viewScroll = 0;
    _delArmed = false;
    return true;
  }

  void openSelected() {
    if (_sel < 0 || _sel >= _count) return;
    if (loadNote(_names[_sel])) _mode = VIEW;
  }

  void deleteCurrent() {
    char path[40];
    snprintf(path, sizeof(path), WATCH_NOTES_DIR "/%s", _curName);
    LittleFS.remove(path);
    _delArmed = false;
    _mode = LIST;
    scanNotes();
  }

  // ---------------------------------------------------------------------------
  // Rendering. 120x120 logical.
  // ---------------------------------------------------------------------------
  static const int ROW_Y0 = 18;
  static const int ROW_H  = 20;
  static const int ROWS_VISIBLE = 4;
  static const int FOOTER_Y = 104;

  void ensureVisible() {
    if (_sel < _scroll) _scroll = _sel;
    if (_sel >= _scroll + ROWS_VISIBLE) _scroll = _sel - ROWS_VISIBLE + 1;
  }

  void renderList(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    display.drawTextCentered(display.width() / 2, 6, "Notes");

    if (_count == 0) {
      d->setRawColor(0x7BEF);
      display.drawTextCentered(display.width() / 2, 50, "No notes yet");
    }

    char buf[16];
    for (int i = 0; i < ROWS_VISIBLE; i++) {
      int idx = _scroll + i;
      if (idx >= _count) break;
      int y = ROW_Y0 + i * ROW_H;

      if (idx == _sel) {
        d->setRawColor(0x231D);                     // same blue as the home tile
        d->drawRoundRect(1, y, display.width() - 2, ROW_H - 2, 3);
      }

      d->setRawColor(0xFFFF);
      display.setCursor(5, y + 2);
      display.print(_previews[idx][0] ? _previews[idx] : "(empty)");

      dateLabel(_names[idx], buf, sizeof(buf));
      d->setRawColor(0x7BEF);
      d->printSmallFont(5, y + 13, buf);
    }

    d->setRawColor(0x231D);
    d->drawRoundRect(31, FOOTER_Y, 58, 14, 3);
    d->setRawColor(0xFFFF);
    d->printSmallFont(52, FOOTER_Y + 8, "New");
  }

  void renderView(DisplayDriver& display) {
    LGFXDisplay* d = (LGFXDisplay*)&display;
    char buf[16];

    dateLabel(_curName, buf, sizeof(buf));
    d->setRawColor(0x7BEF);
    d->printSmallFont(4, 8, buf);

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(1);
    int maxScroll = _lineCount - WATCH_NOTE_VIEW_LINES;
    if (maxScroll < 0) maxScroll = 0;
    if (_viewScroll > maxScroll) _viewScroll = maxScroll;
    char line[WATCH_NOTE_COLS + 1];
    for (int i = 0; i < WATCH_NOTE_VIEW_LINES; i++) {
      int ln = _viewScroll + i;
      int off = ln * WATCH_NOTE_COLS;
      if (off >= _noteLen) break;
      int n = _noteLen - off;
      if (n > WATCH_NOTE_COLS) n = WATCH_NOTE_COLS;
      memcpy(line, _noteBuf + off, n);
      line[n] = 0;
      d->setRawColor(0xFFFF);
      display.setCursor(2, 14 + i * 10);
      display.print(line);
    }

    // Footer: [Back] [Edit] [Del]
    d->setRawColor(0x231D);
    d->drawRoundRect(1, FOOTER_Y, 38, 14, 3);
    d->drawRoundRect(41, FOOTER_Y, 38, 14, 3);
    d->setRawColor(_delArmed ? 0xF800 : 0x231D);
    d->drawRoundRect(81, FOOTER_Y, 38, 14, 3);
    d->setRawColor(0xC618);
    d->printSmallFont(9, FOOTER_Y + 8, "Back");
    d->printSmallFont(49, FOOTER_Y + 8, "Edit");
    d->setRawColor(_delArmed ? 0xF800 : 0xC618);
    d->printSmallFont(87, FOOTER_Y + 8, _delArmed ? "Sure?" : "Del");
  }

public:
  WatchNotesScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* node_prefs)
    : _task(task), _rtc(rtc), _node_prefs(node_prefs) {
    _curName[0] = 0;
    _editName[0] = 0;
  }

  void enter() {
    _mode = LIST;
    _delArmed = false;
    _wantKeyboard = false;
    _editPending = false;
    scanNotes();
    _sel = 0;
    _scroll = 0;
  }

  Mode getMode() const { return _mode; }

  // --- Keyboard handshake, polled from UITask::loop ---
  bool wantsKeyboard() const { return _wantKeyboard; }
  void clearWantKeyboard() { _wantKeyboard = false; }
  bool editPending() const { return _editPending; }
  const char* getEditText() const { return _noteBuf; }

  // Result of a TWKB_NOTE keyboard session. Returns NULL on success or an
  // error message for UITask to show; the caller returns to this screen.
  const char* applyComposedNote(const char* text) {
    if (text == nullptr || text[0] == 0) { _editPending = false; return "Empty note"; }
    char name[WATCH_NOTE_NAME_LEN];
    if (_editPending && _editName[0]) {
      strncpy(name, _editName, sizeof(name) - 1);
      name[sizeof(name) - 1] = 0;
    } else {
      makeName(name, sizeof(name));
    }
    _editPending = false;
    _editName[0] = 0;
    char path[40];
    snprintf(path, sizeof(path), WATCH_NOTES_DIR "/%s", name);
    if (!LittleFS.exists(WATCH_NOTES_DIR)) LittleFS.mkdir(WATCH_NOTES_DIR);
    File f = LittleFS.open(path, "w");
    if (!f) return "Save failed";
    f.print(text);
    f.close();
    scanNotes();
    if (loadNote(name)) _mode = VIEW; else _mode = LIST;
    return nullptr;
  }

  // Physical tap in logical (display.width()) coordinates. Returns true if the
  // tap was consumed. The status-bar strip is handled by main.cpp before this.
  bool handleTap(int lx, int ly) {
    if (_mode == LIST) {
      if (ly >= FOOTER_Y) {                          // footer: New
        if (lx >= 31 && lx < 89) {
          _editPending = false;
          _editName[0] = 0;
          _wantKeyboard = true;
        }
        return true;
      }
      if (ly >= ROW_Y0) {
        int row = (ly - ROW_Y0) / ROW_H;
        int idx = _scroll + row;
        if (row >= 0 && row < ROWS_VISIBLE && idx < _count) {
          if (idx == _sel) openSelected();           // second tap opens
          else { _sel = idx; ensureVisible(); }      // first tap selects
        }
        return true;
      }
      return false;
    }

    // VIEW
    if (ly >= FOOTER_Y) {
      if (lx < 40) { _delArmed = false; _mode = LIST; }          // Back
      else if (lx < 80) {                                        // Edit
        _delArmed = false;
        strncpy(_editName, _curName, sizeof(_editName) - 1);
        _editName[sizeof(_editName) - 1] = 0;
        _editPending = true;
        _wantKeyboard = true;
      } else {                                                   // Del
        if (_delArmed) deleteCurrent();
        else _delArmed = true;
      }
      return true;
    }
    _delArmed = false;
    if (_lineCount > WATCH_NOTE_VIEW_LINES) {                    // scroll zones
      if (ly < FOOTER_Y / 2) { if (_viewScroll > 0) _viewScroll--; }
      else { if (_viewScroll < _lineCount - WATCH_NOTE_VIEW_LINES) _viewScroll++; }
    }
    return true;
  }

  bool handleInput(char c) override {
    // The PMU short press (S3) / user button arrives as KEY_ENTER.
    if (_mode == LIST) {
      if (c == KEY_ENTER) { openSelected(); return true; }
      return false;                       // KEY_CANCEL falls through -> home
    }
    // VIEW
    if (c == KEY_ENTER) {                 // button = Edit, matching the footer
      _delArmed = false;
      strncpy(_editName, _curName, sizeof(_editName) - 1);
      _editName[sizeof(_editName) - 1] = 0;
      _editPending = true;
      _wantKeyboard = true;
      return true;
    }
    if (c == KEY_CANCEL) { _delArmed = false; _mode = LIST; return true; }
    return true;                          // swallow the rest while viewing
  }

  int render(DisplayDriver& display) override {
    switch (_mode) {
      case LIST: renderList(display); return 500;
      case VIEW: renderView(display); return 500;
    }
    return 500;
  }
};
