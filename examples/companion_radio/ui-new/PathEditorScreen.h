#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>
#include <Packet.h>

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

class PathEditorScreen : public UIScreen {
public:
  enum EditorState {
    STATE_MAIN,
    STATE_PICK_HOP
  };

  // Main-state menu items (dynamic, built each render)
  enum MenuItem {
    MENU_MODE = 0,    // "Mode: 1B/hop" or "Mode: 2B/hop"
    // After mode: hop lines (MENU_HOP_BASE + i)
    // Then: action items
    MENU_HOP_BASE = 1,
    // Dynamic items after hops:
    MENU_ADD_HOP = 100,
    MENU_SET_DIRECT,
    MENU_REMOVE_LAST,
    MENU_CLEAR_PATH,
    MENU_SAVE_EXIT
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;

  int _contactIdx;             // Index into contact table
  char _contactName[32];       // Contact name for header

  EditorState _state;
  int _menuSel;                // Selected menu item index (0-based in visible list)
  int _menuCount;              // Total visible menu items

  // Path being edited (working copy)
  uint8_t _pathBuf[MAX_PATH_SIZE];
  uint8_t _pathLen;            // Encoded: bits[7:6]=mode, bits[5:0]=hops
  int _hopCount;               // Decoded hop count
  int _bytesPerHop;            // 1 or 2

  // Repeater picker state
  static const int MAX_REPEATERS = 200;
  uint16_t* _repIdx;           // Indices into contact table (PSRAM)
  int _repCount;               // Number of repeaters found
  int _repSel;                 // Selected repeater in picker
  int _repScroll;              // Scroll offset in picker

  bool _dirty;                 // Path has been modified
  bool _wantExit;              // Set by Save & Exit — caller should navigate back
  bool _directLocked;          // True = path is explicitly set to direct (0 hops, locked)

  // --- helpers ---

  void decodePath() {
    _hopCount = _pathLen & 0x3F;
    uint8_t mode = (_pathLen >> 6) & 0x03;
    _bytesPerHop = mode + 1;
  }

  uint8_t encodePath() const {
    uint8_t mode = (_bytesPerHop - 1) & 0x03;
    return (mode << 6) | (_hopCount & 0x3F);
  }

  void buildRepeaterList() {
    _repCount = 0;
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo c;
    for (uint32_t i = 0; i < numContacts && _repCount < MAX_REPEATERS; i++) {
      if (the_mesh.getContactByIdx(i, c)) {
        if (c.type == ADV_TYPE_REPEATER) {
          _repIdx[_repCount++] = (uint16_t)i;
        }
      }
    }
  }

  // Look up a contact name by matching pub_key prefix bytes
  bool findNameForHop(int hopIndex, char* name, size_t nameLen) const {
    if (hopIndex < 0 || hopIndex >= _hopCount) return false;
    int offset = hopIndex * _bytesPerHop;
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo c;
    for (uint32_t i = 0; i < numContacts; i++) {
      if (the_mesh.getContactByIdx(i, c)) {
        bool match = true;
        for (int b = 0; b < _bytesPerHop; b++) {
          if (c.id.pub_key[b] != _pathBuf[offset + b]) {
            match = false;
            break;
          }
        }
        if (match) {
          strncpy(name, c.name, nameLen);
          name[nameLen - 1] = '\0';
          return true;
        }
      }
    }
    return false;
  }

  // Build the visible menu items list and return count
  // Menu layout:
  //   0: Mode selector
  //   1..hopCount: each hop
  //   hopCount+1: Add hop
  //   hopCount+2: Remove last (only if hops > 0)
  //   hopCount+2 or +3: Clear path (only if custom path flag set or hops > 0)
  //   last: Save & Exit
  int buildMenuCount() const {
    int count = 1; // Mode selector
    count += _hopCount; // One per hop
    if (_hopCount < 8) count++; // Add hop (max 8 hops)
    count++; // Set Direct (always visible)
    if (_hopCount > 0) count++; // Remove last
    if (_hopCount > 0 || _directLocked || isCustomPathSet()) count++; // Clear path
    count++; // Save & Exit
    return count;
  }

  // Map a menu index to a MenuItem enum
  MenuItem menuItemAt(int idx) const {
    if (idx == 0) return MENU_MODE;
    int pos = 1;
    // Hop lines
    for (int h = 0; h < _hopCount; h++) {
      if (idx == pos) return (MenuItem)(MENU_HOP_BASE + h);
      pos++;
    }
    // Add hop
    if (_hopCount < 8) {
      if (idx == pos) return MENU_ADD_HOP;
      pos++;
    }
    // Set Direct
    if (idx == pos) return MENU_SET_DIRECT;
    pos++;
    // Remove last
    if (_hopCount > 0) {
      if (idx == pos) return MENU_REMOVE_LAST;
      pos++;
    }
    // Clear path
    if (_hopCount > 0 || _directLocked || isCustomPathSet()) {
      if (idx == pos) return MENU_CLEAR_PATH;
      pos++;
    }
    // Save & Exit
    return MENU_SAVE_EXIT;
  }

  bool isCustomPathSet() const {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(_contactIdx, c)) return false;
    return (c.flags & CONTACT_FLAG_CUSTOM_PATH) != 0;
  }

public:
  PathEditorScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _contactIdx(-1), _state(STATE_MAIN),
      _menuSel(0), _menuCount(1), _pathLen(0), _hopCount(0),
      _bytesPerHop(1), _repCount(0), _repSel(0), _repScroll(0),
      _dirty(false), _wantExit(false), _directLocked(false) {
    memset(_contactName, 0, sizeof(_contactName));
    memset(_pathBuf, 0, sizeof(_pathBuf));
  #if defined(ESP32) && defined(BOARD_HAS_PSRAM)
    _repIdx = (uint16_t*)ps_calloc(MAX_REPEATERS, sizeof(uint16_t));
  #else
    _repIdx = new uint16_t[MAX_REPEATERS]();
  #endif
  }

  void openForContact(int contactIdx) {
    _contactIdx = contactIdx;
    _state = STATE_MAIN;
    _menuSel = 0;
    _repSel = 0;
    _repScroll = 0;
    _dirty = false;
    _wantExit = false;
    _directLocked = false;

    // Load contact info
    ContactInfo c;
    if (the_mesh.getContactByIdx(contactIdx, c)) {
      strncpy(_contactName, c.name, sizeof(_contactName) - 1);
      _contactName[sizeof(_contactName) - 1] = '\0';

      // Copy current path
      if (c.out_path_len != OUT_PATH_UNKNOWN) {
        _pathLen = c.out_path_len;
        decodePath();
        int byteLen = _hopCount * _bytesPerHop;
        if (byteLen > MAX_PATH_SIZE) byteLen = MAX_PATH_SIZE;
        memcpy(_pathBuf, c.out_path, byteLen);
        // Detect existing direct-locked path
        if (_hopCount == 0 && (c.flags & CONTACT_FLAG_CUSTOM_PATH)) {
          _directLocked = true;
        }
      } else {
        _pathLen = 0;
        _hopCount = 0;
        _bytesPerHop = 1;
        memset(_pathBuf, 0, sizeof(_pathBuf));
      }
    } else {
      strcpy(_contactName, "Unknown");
      _pathLen = 0;
      _hopCount = 0;
      _bytesPerHop = 1;
    }

    _menuCount = buildMenuCount();
  }

  int render(DisplayDriver& display) override {
    if (_state == STATE_PICK_HOP) {
      return renderPicker(display);
    }
    return renderMain(display);
  }

  int renderMain(DisplayDriver& display) {
    char tmp[64];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    snprintf(tmp, sizeof(tmp), "Path: %s", _contactName);
    // Truncate if too long
    if (display.getTextWidth(tmp) > display.width() - 4) {
      snprintf(tmp, sizeof(tmp), "Path: %.12s..", _contactName);
    }
    display.print(tmp);

    // Show lock icon or dirty indicator on right
    if (_dirty) {
      const char* mod = "[*]";
      display.setCursor(display.width() - display.getTextWidth(mod) - 2, 0);
      display.print(mod);
    } else if (isCustomPathSet()) {
      const char* lock = "[L]";
      display.setCursor(display.width() - display.getTextWidth(lock) - 2, 0);
      display.print(lock);
    }

    display.drawRect(0, 11, display.width(), 1);

    // === Body ===
    display.setTextSize(0);
    int lineH = 9;
    int headerH = 14;
    int footerH = 14;
    int maxY = display.height() - footerH;
    int y = headerH;

    _menuCount = buildMenuCount();

    // Center visible window around selected item
    int maxVisible = (maxY - headerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_menuSel - maxVisible / 2, _menuCount - maxVisible));
    if (startIdx < 0) startIdx = 0;
    int endIdx = min(_menuCount, startIdx + maxVisible);

    for (int i = startIdx; i < endIdx && y + lineH <= maxY; i++) {
      bool selected = (i == _menuSel);
      MenuItem item = menuItemAt(i);

      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.fillRect(0, y, display.width(), lineH);
#else
        display.fillRect(0, y + 5, display.width(), lineH);
#endif
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      display.setCursor(2, y);
      char prefix = selected ? '>' : ' ';

      switch (item) {
        case MENU_MODE:
          if (_directLocked) {
            snprintf(tmp, sizeof(tmp), "%c Mode: DIRECT", prefix);
          } else {
            snprintf(tmp, sizeof(tmp), "%c Mode: %dB/hop", prefix, _bytesPerHop);
          }
          display.print(tmp);
          // Show hint on right
          if (!_directLocked) {
            const char* hint = "(A/D)";
            display.setCursor(display.width() - display.getTextWidth(hint) - 4, y);
            display.print(hint);
          }
          break;

        case MENU_ADD_HOP:
          snprintf(tmp, sizeof(tmp), "%c + Add hop...", prefix);
          display.print(tmp);
          break;

        case MENU_SET_DIRECT:
          if (_directLocked) {
            snprintf(tmp, sizeof(tmp), "%c * Direct (set)", prefix);
          } else {
            snprintf(tmp, sizeof(tmp), "%c * Set Direct", prefix);
          }
          display.print(tmp);
          break;

        case MENU_REMOVE_LAST:
          snprintf(tmp, sizeof(tmp), "%c - Remove last hop", prefix);
          display.print(tmp);
          break;

        case MENU_CLEAR_PATH:
          snprintf(tmp, sizeof(tmp), "%c   Clear custom path", prefix);
          display.print(tmp);
          break;

        case MENU_SAVE_EXIT:
          snprintf(tmp, sizeof(tmp), "%c   Save & Exit", prefix);
          display.print(tmp);
          break;

        default:
          // Hop line: MENU_HOP_BASE + hopIndex
          if (item >= MENU_HOP_BASE && item < MENU_HOP_BASE + 64) {
            int hopIdx = item - MENU_HOP_BASE;
            char hopName[24];
            int offset = hopIdx * _bytesPerHop;

            if (findNameForHop(hopIdx, hopName, sizeof(hopName))) {
              if (_bytesPerHop == 1) {
                snprintf(tmp, sizeof(tmp), "%c %d: %s (%02X)", prefix, hopIdx + 1,
                         hopName, _pathBuf[offset]);
              } else {
                snprintf(tmp, sizeof(tmp), "%c %d: %s (%02X%02X)", prefix, hopIdx + 1,
                         hopName, _pathBuf[offset], _pathBuf[offset + 1]);
              }
            } else {
              if (_bytesPerHop == 1) {
                snprintf(tmp, sizeof(tmp), "%c %d: ??? (%02X)", prefix, hopIdx + 1,
                         _pathBuf[offset]);
              } else {
                snprintf(tmp, sizeof(tmp), "%c %d: ??? (%02X%02X)", prefix, hopIdx + 1,
                         _pathBuf[offset], _pathBuf[offset + 1]);
              }
            }
            display.drawTextEllipsized(2, y, display.width() - 4, tmp);
          }
          break;
      }

      y += lineH;
    }

    // === Footer ===
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

#if defined(LilyGo_T5S3_EPaper_Pro)
    display.setCursor(0, footerY);
    display.print("Swipe:Nav");
    const char* right = "Hold:Select";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
#else
    display.setCursor(0, footerY);
    display.print("Q:Bk W/S:Nav");
    const char* right = "Enter:Sel";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
#endif

    return 5000;
  }

  int renderPicker(DisplayDriver& display) {
    char tmp[64];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    snprintf(tmp, sizeof(tmp), "Select Repeater (%d)", _repCount);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);

    // === Body ===
    display.setTextSize(0);
    int lineH = 9;
    int headerH = 14;
    int footerH = 14;
    int maxY = display.height() - footerH;
    int y = headerH;

    if (_repCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      display.print("No repeaters in contacts");
      display.setCursor(0, y + lineH);
      display.print("Add repeaters first");
    } else {
      int maxVisible = (maxY - headerH) / lineH;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_repSel - maxVisible / 2, _repCount - maxVisible));
      if (startIdx < 0) startIdx = 0;
      int endIdx = min(_repCount, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineH <= maxY; i++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(_repIdx[i], c)) continue;

        bool selected = (i == _repSel);

        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
          display.fillRect(0, y, display.width(), lineH);
#else
          display.fillRect(0, y + 5, display.width(), lineH);
#endif
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        display.setCursor(2, y);
        char prefix = selected ? '>' : ' ';

        if (_bytesPerHop == 1) {
          snprintf(tmp, sizeof(tmp), "%c %s (%02X)", prefix, c.name, c.id.pub_key[0]);
        } else {
          snprintf(tmp, sizeof(tmp), "%c %s (%02X%02X)", prefix, c.name,
                   c.id.pub_key[0], c.id.pub_key[1]);
        }
        display.drawTextEllipsized(2, y, display.width() - 4, tmp);

        y += lineH;
      }
    }

    // === Footer ===
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

#if defined(LilyGo_T5S3_EPaper_Pro)
    display.setCursor(0, footerY);
    display.print("Swipe:Scroll");
    const char* right = "Hold:Add  Back:Cancel";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
#else
    display.setCursor(0, footerY);
    display.print("Q:Cancel W/S:Scroll");
    const char* right = "Enter:Add";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
#endif

    return 5000;
  }

  bool handleInput(char c) override {
    if (_state == STATE_PICK_HOP) {
      return handlePickerInput(c);
    }
    return handleMainInput(c);
  }

  bool handleMainInput(char c) {
    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_menuSel > 0) {
        _menuSel--;
        return true;
      }
      return false;
    }

    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_menuSel < _menuCount - 1) {
        _menuSel++;
        return true;
      }
      return false;
    }

    // A/D — toggle mode (only when Mode item is selected and not direct-locked)
    if (c == 'a' || c == 'A' || c == 'd' || c == 'D') {
      MenuItem item = menuItemAt(_menuSel);
      if (item == MENU_MODE && !_directLocked) {
        // Toggle between 1-byte and 2-byte
        if (_bytesPerHop == 1) {
          switchMode(2);
        } else {
          switchMode(1);
        }
        _dirty = true;
        _menuCount = buildMenuCount();
        return true;
      }
      return false;
    }

    // Enter - select
    if (c == 13 || c == KEY_ENTER || c == '\r') {
      MenuItem item = menuItemAt(_menuSel);

      switch (item) {
        case MENU_MODE:
          // Toggle mode on Enter too (no-op if direct locked)
          if (!_directLocked) {
            if (_bytesPerHop == 1) {
              switchMode(2);
            } else {
              switchMode(1);
            }
            _dirty = true;
            _menuCount = buildMenuCount();
          }
          return true;

        case MENU_ADD_HOP:
          // Enter picker mode — adding a hop clears direct lock
          _directLocked = false;
          buildRepeaterList();
          _repSel = 0;
          _repScroll = 0;
          _state = STATE_PICK_HOP;
          return true;

        case MENU_SET_DIRECT:
          // Set path to direct (0 hops, locked)
          _hopCount = 0;
          _pathLen = 0;
          memset(_pathBuf, 0, sizeof(_pathBuf));
          _directLocked = true;
          _dirty = true;
          _menuCount = buildMenuCount();
          return true;

        case MENU_REMOVE_LAST:
          if (_hopCount > 0) {
            _hopCount--;
            _pathLen = encodePath();
            _dirty = true;
            _menuCount = buildMenuCount();
            // Clamp selection
            if (_menuSel >= _menuCount) _menuSel = _menuCount - 1;
          }
          return true;

        case MENU_CLEAR_PATH:
          _hopCount = 0;
          _pathLen = 0;
          _directLocked = false;
          memset(_pathBuf, 0, sizeof(_pathBuf));
          _dirty = true;
          _menuCount = buildMenuCount();
          _menuSel = 0;
          return true;

        case MENU_SAVE_EXIT:
          savePath();
          _wantExit = true;  // Signal to main.cpp to navigate back to contacts
          return true;

        default:
          // Hop line — no action (could add remove-specific-hop later)
          break;
      }
      return true;
    }

    // Q - back (discard changes or prompt?)
    // For simplicity, just go back without saving
    if (c == 'q' || c == 'Q') {
      // Return to contacts screen without saving
      // The UITask will handle this via the key falling through
      return false;  // Let UITask handle Q as back
    }

    return false;
  }

  bool handlePickerInput(char c) {
    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_repSel > 0) {
        _repSel--;
        return true;
      }
      return false;
    }

    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_repSel < _repCount - 1) {
        _repSel++;
        return true;
      }
      return false;
    }

    // Enter - add selected repeater as hop
    if (c == 13 || c == KEY_ENTER || c == '\r') {
      if (_repCount > 0 && _repSel >= 0 && _repSel < _repCount) {
        addHopFromContact(_repIdx[_repSel]);
      }
      _state = STATE_MAIN;
      _menuCount = buildMenuCount();
      return true;
    }

    // Q - cancel picker, return to main
    if (c == 'q' || c == 'Q') {
      _state = STATE_MAIN;
      return true;
    }

    return false;
  }

  // Tap-to-select for T5S3 touch
  int selectRowAtVY(int vy) {
    if (_state == STATE_PICK_HOP) {
      return selectPickerRowAtVY(vy);
    }
    return selectMainRowAtVY(vy);
  }

  int selectMainRowAtVY(int vy) {
    if (_menuCount == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = 9;
#if defined(LilyGo_T5S3_EPaper_Pro)
    const int bodyTop = headerH;
#else
    const int bodyTop = headerH + 5;
#endif
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_menuSel - maxVisible / 2, _menuCount - maxVisible));
    if (startIdx < 0) startIdx = 0;

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _menuCount) return 0;
    if (tappedRow == _menuSel) return 2;
    _menuSel = tappedRow;
    return 1;
  }

  int selectPickerRowAtVY(int vy) {
    if (_repCount == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = 9;
#if defined(LilyGo_T5S3_EPaper_Pro)
    const int bodyTop = headerH;
#else
    const int bodyTop = headerH + 5;
#endif
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_repSel - maxVisible / 2, _repCount - maxVisible));
    if (startIdx < 0) startIdx = 0;

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _repCount) return 0;
    if (tappedRow == _repSel) return 2;
    _repSel = tappedRow;
    return 1;
  }

  EditorState getState() const { return _state; }
  bool isDirty() const { return _dirty; }
  bool wantsExit() const { return _wantExit; }

private:
  void switchMode(int newBytesPerHop) {
    if (newBytesPerHop == _bytesPerHop) return;

    if (_hopCount > 0) {
      // Rebuild path buffer for new mode
      // We need the full pub_keys to re-extract the right prefix bytes
      uint8_t newBuf[MAX_PATH_SIZE];
      memset(newBuf, 0, sizeof(newBuf));
      int newHopCount = 0;

      for (int h = 0; h < _hopCount && newHopCount < 8; h++) {
        int oldOffset = h * _bytesPerHop;
        // Try to find the contact that matches this hop
        uint32_t numContacts = the_mesh.getNumContacts();
        ContactInfo c;
        bool found = false;
        for (uint32_t i = 0; i < numContacts; i++) {
          if (the_mesh.getContactByIdx(i, c)) {
            bool match = true;
            for (int b = 0; b < _bytesPerHop; b++) {
              if (c.id.pub_key[b] != _pathBuf[oldOffset + b]) {
                match = false;
                break;
              }
            }
            if (match) {
              // Found the contact — copy new prefix size
              int newOffset = newHopCount * newBytesPerHop;
              for (int b = 0; b < newBytesPerHop; b++) {
                newBuf[newOffset + b] = c.id.pub_key[b];
              }
              newHopCount++;
              found = true;
              break;
            }
          }
        }
        if (!found) {
          // Contact not found — copy what we can
          int newOffset = newHopCount * newBytesPerHop;
          int oldOff = h * _bytesPerHop;
          for (int b = 0; b < newBytesPerHop; b++) {
            if (b < _bytesPerHop) {
              newBuf[newOffset + b] = _pathBuf[oldOff + b];
            } else {
              newBuf[newOffset + b] = 0x00; // pad with zero
            }
          }
          newHopCount++;
        }
      }

      _hopCount = newHopCount;
      memcpy(_pathBuf, newBuf, sizeof(newBuf));
    }

    _bytesPerHop = newBytesPerHop;
    _pathLen = encodePath();
  }

  void addHopFromContact(uint16_t contactTableIdx) {
    if (_hopCount >= 8) return;
    ContactInfo c;
    if (!the_mesh.getContactByIdx(contactTableIdx, c)) return;

    int offset = _hopCount * _bytesPerHop;
    if (offset + _bytesPerHop > MAX_PATH_SIZE) return;

    for (int b = 0; b < _bytesPerHop; b++) {
      _pathBuf[offset + b] = c.id.pub_key[b];
    }
    _hopCount++;
    _pathLen = encodePath();
    _dirty = true;
  }

  void savePath() {
    if (_contactIdx < 0) return;

    if (_directLocked) {
      // Set as direct (0 hops) with lock — prevents flood routing
      the_mesh.setCustomPath(_contactIdx, _pathBuf, 0, true);
      Serial.printf("PathEditor: set DIRECT path for contact %d (%s)\n",
                     _contactIdx, _contactName);
    } else if (_hopCount > 0) {
      // Set custom path with lock
      the_mesh.setCustomPath(_contactIdx, _pathBuf, encodePath(), true);
      Serial.printf("PathEditor: saved %d-hop %dB/hop path for contact %d (%s)\n",
                     _hopCount, _bytesPerHop, _contactIdx, _contactName);
    } else {
      // Clear custom path — revert to auto-discovery
      the_mesh.clearCustomPath(_contactIdx);
      Serial.printf("PathEditor: cleared custom path for contact %d (%s)\n",
                     _contactIdx, _contactName);
    }

    // Trigger contact save to SD
    the_mesh.saveContacts();
    _dirty = false;
  }
};