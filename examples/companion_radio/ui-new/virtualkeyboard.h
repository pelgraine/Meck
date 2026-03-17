#pragma once
// =============================================================================
// VirtualKeyboard — On-screen QWERTY keyboard for T5S3 (touch-only devices)
//
// Renders in virtual coordinate space (128×128). Touch hit testing converts
// physical GT911 coords (960×540) to virtual coords.
//
// Usage:
//   keyboard.open("To: General", "", 137);  // label, initial text, max len
//   keyboard.render(display);                // in render loop
//   keyboard.handleTap(vx, vy);              // on touch tap (virtual coords)
//   if (keyboard.status() == VKB_SUBMITTED) { ... keyboard.getText() ... }
// =============================================================================

#if defined(LilyGo_T5S3_EPaper_Pro)
#ifndef VIRTUAL_KEYBOARD_H
#define VIRTUAL_KEYBOARD_H

#include <Arduino.h>
#include <helpers/ui/DisplayDriver.h>

enum VKBStatus { VKB_EDITING, VKB_SUBMITTED, VKB_CANCELLED };

// What the keyboard is being used for (dispatch on submit)
enum VKBPurpose {
  VKB_CHANNEL_MSG,    // Send to channel
  VKB_DM,             // Direct message to contact
  VKB_ADMIN_PASSWORD, // Repeater admin login
  VKB_ADMIN_CLI,      // Repeater admin CLI command
  VKB_NOTES,          // Insert text into notes
  VKB_SETTINGS_NAME,  // Edit node name
  VKB_SETTINGS_TEXT,  // Generic settings text edit (channel name, freq, APN)
  VKB_WIFI_PASSWORD,  // WiFi password entry (settings screen)
#ifdef MECK_WEB_READER
  VKB_WEB_URL,        // Web reader URL entry
  VKB_WEB_SEARCH,     // Web reader DuckDuckGo search query
  VKB_WEB_WIFI_PASS,  // Web reader WiFi password
  VKB_WEB_LINK,       // Web reader link number entry
#endif
  VKB_TEXT_PAGE,       // Text reader: go to page number
};

class VirtualKeyboard {
public:
  static const int MAX_TEXT = 140;

  VirtualKeyboard() : _status(VKB_CANCELLED), _purpose(VKB_CHANNEL_MSG),
                      _contextIdx(0), _textLen(0), _shifted(false), _symbols(false) {
    _text[0] = '\0';
    _label[0] = '\0';
  }

  void open(VKBPurpose purpose, const char* label, const char* initial, int maxLen, int contextIdx = 0) {
    _purpose = purpose;
    _contextIdx = contextIdx;
    _status = VKB_EDITING;
    _shifted = false;
    _symbols = false;
    _maxLen = (maxLen > 0 && maxLen < MAX_TEXT) ? maxLen : MAX_TEXT;

    strncpy(_label, label, sizeof(_label) - 1);
    _label[sizeof(_label) - 1] = '\0';

    if (initial && initial[0]) {
      strncpy(_text, initial, _maxLen);
      _text[_maxLen] = '\0';
      _textLen = strlen(_text);
    } else {
      _text[0] = '\0';
      _textLen = 0;
    }
  }

  VKBStatus status() const { return _status; }
  VKBPurpose purpose() const { return _purpose; }
  int contextIdx() const { return _contextIdx; }
  const char* getText() const { return _text; }
  int getTextLen() const { return _textLen; }
  bool isActive() const { return _status == VKB_EDITING; }

  // --- Render keyboard + input field ---
  void render(DisplayDriver& display) {
    // Header label (To: channel, DM: name, etc.)
    display.setTextSize(0);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(2, 0);
    display.print(_label);

    // Input text field
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 10, 128, 18);  // Border

    display.setCursor(2, 12);
    display.setColor(DisplayDriver::LIGHT);

    // Show text with cursor
    char dispBuf[MAX_TEXT + 2];
    snprintf(dispBuf, sizeof(dispBuf), "%s_", _text);
    display.print(dispBuf);

    // Character count
    {
      char countBuf[12];
      snprintf(countBuf, sizeof(countBuf), "%d/%d", _textLen, _maxLen);
      int cw = display.getTextWidth(countBuf);
      display.setCursor(128 - cw - 2, 0);
      display.setColor(DisplayDriver::LIGHT);
      display.print(countBuf);
    }

    // Separator
    display.drawRect(0, 30, 128, 1);

    // --- Draw keyboard rows ---
    const char* const* layout = getLayout();

    for (int row = 0; row < 3; row++) {
      int numKeys = strlen(layout[row]);
      int rowY = KEY_START_Y + row * (KEY_H + KEY_GAP);

      // Calculate key width and starting X for this row
      int totalW = numKeys * KEY_W + (numKeys - 1) * KEY_GAP;
      int startX = (128 - totalW) / 2;

      for (int k = 0; k < numKeys; k++) {
        int kx = startX + k * (KEY_W + KEY_GAP);
        char ch = layout[row][k];

        // Draw key background (inverted for special keys)
        bool special = (ch == '<' || ch == '^' || ch == '~' || ch == '>' || ch == '\x01');
        if (special) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(kx, rowY + 1, KEY_W, KEY_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(kx, rowY + 1, KEY_W, KEY_H - 1);
        }

        // Draw key label
        char keyLabel[2] = { ch, '\0' };
        // Remap special chars to display labels
        if (ch == '<') keyLabel[0] = '<';   // Backspace
        if (ch == '^') keyLabel[0] = '^';   // Shift
        if (ch == '>') keyLabel[0] = '>';   // Enter

        if (ch == '~') {
          // Space key — don't draw individual label
        } else if (ch == '\x01') {
          // Symbol toggle in row — show "ab" hint
          int lx = kx + KEY_W / 2 - display.getTextWidth("ab") / 2;
          display.setCursor(lx, rowY + 2);
          display.print("ab");
        } else {
          int lx = kx + KEY_W / 2 - display.getTextWidth(keyLabel) / 2;
          display.setCursor(lx, rowY + 2);
          display.print(keyLabel);
        }

        // Restore color
        display.setColor(DisplayDriver::LIGHT);
      }
    }

    // Draw row 4 with variable-width keys
    int r4y = KEY_START_Y + 3 * (KEY_H + KEY_GAP);
    drawRow4(display, r4y);

    // Shift/symbol indicator
    display.setTextSize(0);
    display.setColor(DisplayDriver::GREEN);
    if (_shifted) {
      display.setCursor(2, 126);
      display.print("SHIFT");
    } else if (_symbols) {
      display.setCursor(2, 126);
      display.print("123");
    }
  }

  // --- Handle touch tap (virtual coordinates) ---
  // Returns true if the tap was consumed
  bool handleTap(int vx, int vy) {
    if (_status != VKB_EDITING) return false;

    // Check keyboard rows 0-2
    const char* const* layout = getLayout();

    for (int row = 0; row < 3; row++) {
      int numKeys = strlen(layout[row]);
      int rowY = KEY_START_Y + row * (KEY_H + KEY_GAP);
      if (vy < rowY || vy >= rowY + KEY_H) continue;

      int totalW = numKeys * KEY_W + (numKeys - 1) * KEY_GAP;
      int startX = (128 - totalW) / 2;

      for (int k = 0; k < numKeys; k++) {
        int kx = startX + k * (KEY_W + KEY_GAP);
        if (vx >= kx && vx < kx + KEY_W) {
          char ch = layout[row][k];
          processKey(ch);
          return true;
        }
      }
      return true;  // Tap was in row area but between keys — consume
    }

    // Check row 4 (variable width keys)
    int r4y = KEY_START_Y + 3 * (KEY_H + KEY_GAP);
    if (vy >= r4y && vy < r4y + KEY_H) {
      return handleRow4Tap(vx);
    }

    return false;
  }

  // Swipe up on keyboard = cancel
  void cancel() { _status = VKB_CANCELLED; }

private:
  VKBStatus _status;
  VKBPurpose _purpose;
  int _contextIdx;
  char _text[MAX_TEXT + 1];
  int _textLen;
  int _maxLen;
  char _label[40];
  bool _shifted;
  bool _symbols;

  // Layout constants (virtual coords)
  static const int KEY_W = 11;
  static const int KEY_H = 19;
  static const int KEY_GAP = 1;
  static const int KEY_START_Y = 34;

  // Key layouts — rows 0-2 as char arrays
  // Special: ^ = shift, < = backspace, # = symbols, > = enter, ~ = space
  const char* const* getLayout() const {
    static const char* const lower[3] = { "qwertyuiop", "asdfghjkl", "^zxcvbnm<" };
    static const char* const upper[3] = { "QWERTYUIOP", "ASDFGHJKL", "^ZXCVBNM<" };
    static const char* const syms[3]  = { "1234567890", "-/:;()@$&#", "\x01.,?!'\"_<" };
    return _symbols ? syms : (_shifted ? upper : lower);
  }

  // Row 4: variable-width keys [#/ABC] [,] [SPACE] [.] [Enter]
  // Defined by physical zones, not the char-array approach
  struct R4Key { int x; int w; char ch; const char* label; };

  void drawRow4(DisplayDriver& display, int y) {
    // # or ABC toggle: x=4, w=20
    // comma: x=26, w=11
    // space: x=39, w=50
    // period: x=91, w=11
    // enter: x=104, w=20
    const R4Key keys[] = {
      { 4,  20, '\x01', _symbols ? "ABC" : "123" },
      { 26, 11, ',', "," },
      { 39, 50, '~', "space" },
      { 91, 11, '.', "." },
      { 104, 20, '>', "Send" }
    };

    for (int i = 0; i < 5; i++) {
      bool special = (keys[i].ch == '\x01' || keys[i].ch == '>');
      if (special) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(keys[i].x, y + 1, keys[i].w, KEY_H - 1);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(keys[i].x, y + 1, keys[i].w, KEY_H - 1);
      }

      // Center label in key
      display.setTextSize(0);
      int lw = display.getTextWidth(keys[i].label);
      int lx = keys[i].x + (keys[i].w - lw) / 2;
      display.setCursor(lx, y + 2);
      display.print(keys[i].label);
      display.setColor(DisplayDriver::LIGHT);
    }
  }

  bool handleRow4Tap(int vx) {
    const R4Key keys[] = {
      { 4,  20, '\x01', nullptr },
      { 26, 11, ',', nullptr },
      { 39, 50, '~', nullptr },
      { 91, 11, '.', nullptr },
      { 104, 20, '>', nullptr }
    };
    for (int i = 0; i < 5; i++) {
      if (vx >= keys[i].x && vx < keys[i].x + keys[i].w) {
        processKey(keys[i].ch);
        return true;
      }
    }
    return true;  // Consume tap in row area
  }

  void processKey(char ch) {
    if (ch == '^') {
      // Shift toggle
      _shifted = !_shifted;
      _symbols = false;
    } else if (ch == '\x01') {
      // Symbol/letter toggle
      _symbols = !_symbols;
      _shifted = false;
    } else if (ch == '<') {
      // Backspace
      if (_textLen > 0) {
        _textLen--;
        _text[_textLen] = '\0';
      }
    } else if (ch == '>') {
      // Enter/Send
      _status = VKB_SUBMITTED;
    } else if (ch == '~') {
      // Space
      if (_textLen < _maxLen) {
        _text[_textLen++] = ' ';
        _text[_textLen] = '\0';
      }
    } else {
      // Regular character
      if (_textLen < _maxLen) {
        _text[_textLen++] = ch;
        _text[_textLen] = '\0';
        // Auto-unshift after typing one character
        if (_shifted) _shifted = false;
      }
    }
  }
};

#endif // VIRTUAL_KEYBOARD_H
#endif // LilyGo_T5S3_EPaper_Pro