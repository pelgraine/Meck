#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>
#include <Packet.h>

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

// ---------------------------------------------------------------------------
// TraceScreen
// ---------------------------------------------------------------------------
// Standalone trace path tool for the T-Deck Pro.  The user builds a repeater
// chain from the contacts list or by typing comma-separated hash values, sends
// a PAYLOAD_TYPE_TRACE packet direct-routed through the chain, and views
// per-hop SNR results.
//
// Path size (1-byte or 2-byte hashes) follows the device's path_hash_mode
// setting but can be toggled on this screen.
//
// The trace packet is created via Mesh::createTrace() and sent via
// Mesh::sendDirect().  Each repeater in the chain checks if its pub_key
// prefix matches the next hash in the payload; if so, it appends its receive
// SNR*4 to the packet's path field and forwards.  When the packet reaches
// the end of its given path, onTraceRecv() fires on the receiving node.
//
// For round-trip traces the user should build a symmetric path
// (e.g. A,B,C,B,A) and must be able to hear the last repeater directly.
// ---------------------------------------------------------------------------

#define TRACE_MAX_HOPS     16
#define TRACE_TIMEOUT_MS   30000   // 30 second timeout
#define TRACE_EDIT_BUF     80      // Max chars for typed path

class TraceScreen : public UIScreen {
public:
  enum ScreenState {
    STATE_BUILD,      // Building the path
    STATE_PICK_HOP,   // Picking a repeater from contacts
    STATE_RUNNING,    // Trace sent, waiting for response
    STATE_RESULTS     // Showing results
  };

  // Trace result data (filled by onTraceResult callback)
  struct TraceResult {
    uint8_t  hashes[TRACE_MAX_HOPS * 2];  // Hash bytes (1 or 2 per hop)
    int8_t   snrs[TRACE_MAX_HOPS];        // SNR*4 per hop
    int8_t   final_snr;                   // SNR of the response arriving back
    int      hopCount;                    // Number of hops that responded
    int      totalHops;                   // Total hops in the path
    uint32_t duration_ms;                 // Round-trip time
    bool     valid;
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;

  ScreenState _state;

  // Path being built
  uint8_t _pathBuf[TRACE_MAX_HOPS * 2];  // Hash bytes (max 2 bytes per hop)
  int     _hopCount;
  int     _bytesPerHop;                   // 1 or 2

  // Menu navigation (STATE_BUILD)
  int _menuSel;

  // Inline text editor (for Type Path)
  bool _editing;
  char _editBuf[TRACE_EDIT_BUF];
  int  _editPos;

  // Repeater picker (STATE_PICK_HOP)
  static const int MAX_REPEATERS = 200;
  uint16_t* _repIdx;       // Indices into contact table (PSRAM)
  int _repCount;
  int _repSel;
  int _repScroll;

  // Trace state (STATE_RUNNING / STATE_RESULTS)
  uint32_t _traceTag;
  uint32_t _traceAuth;
  unsigned long _traceSentAt;
  TraceResult _result;

  // Results scroll
  int _resultScroll;

  bool _wantExit;
#if defined(MECK_TWATCH)
  bool _wantKeyboard;   // Watch: Type Path tapped -> UITask opens the on-screen keyboard
#endif

  // --- Menu helpers (STATE_BUILD) ---
  // Menu layout:
  //   0: Mode selector (1-byte / 2-byte)
  //   1: Type Path (inline text editor)
  //   2..hopCount+1: each hop
  //   next: + Add repeater (if < TRACE_MAX_HOPS)
  //   next: Remove last (if hopCount > 0)
  //   next: Run Trace (if hopCount > 0)
  //   last: Exit
  enum MenuItem {
    MENU_PATH_SIZE = 0,
    MENU_TYPE_PATH = 1,
    MENU_HOP_BASE = 2,
    MENU_ADD_HOP = 200,
    MENU_REMOVE_LAST,
    MENU_RUN_TRACE,
    MENU_EXIT
  };

  int buildMenuCount() const {
    int count = 2;  // Mode + Type Path
    count += _hopCount;
    if (_hopCount < TRACE_MAX_HOPS) count++;  // Add hop
    if (_hopCount > 0) count++;  // Remove last
    if (_hopCount > 0) count++;  // Run Trace
    count++;  // Exit
    return count;
  }

  MenuItem menuItemAt(int idx) const {
    if (idx == 0) return MENU_PATH_SIZE;
    if (idx == 1) return MENU_TYPE_PATH;
    int pos = 2;
    for (int h = 0; h < _hopCount; h++) {
      if (idx == pos) return (MenuItem)(MENU_HOP_BASE + h);
      pos++;
    }
    if (_hopCount < TRACE_MAX_HOPS) {
      if (idx == pos) return MENU_ADD_HOP;
      pos++;
    }
    if (_hopCount > 0) {
      if (idx == pos) return MENU_REMOVE_LAST;
      pos++;
    }
    if (_hopCount > 0) {
      if (idx == pos) return MENU_RUN_TRACE;
      pos++;
    }
    return MENU_EXIT;
  }

  // Build repeater list from contacts
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

  // Look up contact name from hash prefix
  bool findNameForHash(const uint8_t* hash, int hashLen, char* name, size_t nameLen) const {
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo c;
    // First pass: repeaters only
    for (uint32_t i = 0; i < numContacts; i++) {
      if (the_mesh.getContactByIdx(i, c) && c.type == ADV_TYPE_REPEATER) {
        if (memcmp(c.id.pub_key, hash, hashLen) == 0) {
          strncpy(name, c.name, nameLen);
          name[nameLen - 1] = '\0';
          return true;
        }
      }
    }
    // Second pass: any contact
    for (uint32_t i = 0; i < numContacts; i++) {
      if (the_mesh.getContactByIdx(i, c)) {
        if (memcmp(c.id.pub_key, hash, hashLen) == 0) {
          strncpy(name, c.name, nameLen);
          name[nameLen - 1] = '\0';
          return true;
        }
      }
    }
    return false;
  }

  // Parse comma-separated decimal values from edit buffer into path
  // Returns number of hops parsed, or -1 on error
  int parseTypedPath() {
    if (_editBuf[0] == '\0') return 0;

    uint8_t tmpPath[TRACE_MAX_HOPS * 2];
    int hops = 0;
    const char* p = _editBuf;

    while (*p && hops < TRACE_MAX_HOPS) {
      // Skip whitespace/commas
      while (*p == ',' || *p == ' ') p++;
      if (*p == '\0') break;

      // Parse hex number (companion app uses hex hash values)
      char* end;
      long val = strtol(p, &end, 16);
      if (end == p) return -1;  // No digits found
      p = end;

      if (_bytesPerHop == 1) {
        if (val < 0 || val > 255) return -1;
        tmpPath[hops] = (uint8_t)val;
      } else {
        if (val < 0 || val > 65535) return -1;
        // Big-endian storage: hash display = (pub_key[0] << 8) | pub_key[1]
        // So val >> 8 is pub_key[0], val & 0xFF is pub_key[1]
        tmpPath[hops * 2]     = (uint8_t)((val >> 8) & 0xFF);
        tmpPath[hops * 2 + 1] = (uint8_t)(val & 0xFF);
      }
      hops++;
    }

    if (hops > 0) {
      memcpy(_pathBuf, tmpPath, hops * _bytesPerHop);
      _hopCount = hops;
    }
    return hops;
  }

  // Build display string from current path (for showing in edit field)
  void pathToEditBuf() {
    _editBuf[0] = '\0';
    _editPos = 0;
    for (int i = 0; i < _hopCount; i++) {
      char tmp[8];
      if (_bytesPerHop == 1) {
        snprintf(tmp, sizeof(tmp), "%02X", _pathBuf[i]);
      } else {
        uint16_t val = ((uint16_t)_pathBuf[i * 2] << 8) | _pathBuf[i * 2 + 1];
        snprintf(tmp, sizeof(tmp), "%04X", val);
      }
      if (i > 0) {
        if (_editPos < TRACE_EDIT_BUF - 1) _editBuf[_editPos++] = ',';
      }
      int tlen = strlen(tmp);
      if (_editPos + tlen < TRACE_EDIT_BUF - 1) {
        memcpy(&_editBuf[_editPos], tmp, tlen);
        _editPos += tlen;
      }
    }
    _editBuf[_editPos] = '\0';
  }

  // Truncate long names to maxLen chars + "..." for display
  static void truncateName(char* name, int maxLen = 10) {
    if ((int)strlen(name) > maxLen) {
      name[maxLen] = '\0';
      // Remove trailing space before ellipsis
      while (maxLen > 0 && name[maxLen - 1] == ' ') {
        name[--maxLen] = '\0';
      }
      strcat(name, "...");
    }
  }

  // Draw signal bars (3 bars) based on SNR
  void drawSignalBars(DisplayDriver& display, int x, int y, int8_t snr4) {
    float snr = snr4 / 4.0f;
    // 3 bars: low >= -5, mid >= 3, high >= 8
    int bars = 0;
    if (snr >= -5.0f) bars = 1;
    if (snr >= 3.0f) bars = 2;
    if (snr >= 8.0f) bars = 3;

    int barW = 3;
    int gap = 1;
    int heights[] = { 4, 7, 10 };
    for (int b = 0; b < 3; b++) {
      int bx = x + b * (barW + gap);
      int by = y + 10 - heights[b];
      if (b < bars) {
        display.setColor(DisplayDriver::GREEN);
      } else {
        display.setColor(DisplayDriver::DARK);
      }
      display.fillRect(bx, by, barW, heights[b]);
    }
  }

public:
  TraceScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _state(STATE_BUILD),
      _hopCount(0), _bytesPerHop(2),
      _menuSel(0), _editing(false), _editPos(0),
      _repCount(0), _repSel(0), _repScroll(0),
      _traceTag(0), _traceAuth(0), _traceSentAt(0),
      _resultScroll(0), _wantExit(false) {
    memset(_pathBuf, 0, sizeof(_pathBuf));
    memset(_editBuf, 0, sizeof(_editBuf));
    memset(&_result, 0, sizeof(_result));
  #if defined(ESP32) && defined(BOARD_HAS_PSRAM)
    _repIdx = (uint16_t*)ps_calloc(MAX_REPEATERS, sizeof(uint16_t));
  #else
    _repIdx = new uint16_t[MAX_REPEATERS]();
  #endif
  }

  bool wantsExit() const { return _wantExit; }
  bool isEditing() const { return _editing; }

  // --- Public helpers for T5S3 long-press → virtual keyboard integration ---

  // True if the highlighted menu row is the Type Path entry (STATE_BUILD only).
  bool isOnTypePathRow() const {
    return _state == STATE_BUILD && menuItemAt(_menuSel) == MENU_TYPE_PATH;
  }

  // True if the highlighted menu row is the Mode selector (STATE_BUILD only).
  bool isOnModeRow() const {
    return _state == STATE_BUILD && menuItemAt(_menuSel) == MENU_PATH_SIZE;
  }

#if defined(MECK_TWATCH)
  bool wantsKeyboard() const { return _wantKeyboard; }
  void clearWantKeyboard() { _wantKeyboard = false; }

  // Tap-to-select row mapping for the watch (mirrors PathEditor/Discovery).
  // vy is in the 128-tall virtual touch space. Returns:
  //   0 = miss, 1 = moved the cursor, 2 = tapped the already-selected row.
  int selectRowAtVY(int vy) {
    if (_state == STATE_BUILD)    return selectBuildRowAtVY(vy);
    if (_state == STATE_PICK_HOP) return selectPickerRowAtVY(vy);
    return 0;
  }

  int selectBuildRowAtVY(int vy) {
    int menuCount = buildMenuCount();
    if (menuCount == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = 11;
    const int bodyTop = headerH;   // renderBuild draws row 0 at y=14
    if (vy < bodyTop || vy >= 128 - footerH) return 0;
    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 1) maxVisible = 1;
    // Replicate renderBuild's scroll window so a tap maps to the drawn row.
    int scrollTop = 0;
    if (_menuSel >= scrollTop + maxVisible) scrollTop = _menuSel - maxVisible + 1;
    if (_menuSel < scrollTop) scrollTop = _menuSel;
    int tappedRow = scrollTop + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= menuCount) return 0;
    if (tappedRow == _menuSel) return 2;
    _menuSel = tappedRow;
    return 1;
  }

  int selectPickerRowAtVY(int vy) {
    if (_repCount == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = 11;
    const int bodyTop = headerH;   // renderPicker draws row 0 at y=14
    if (vy < bodyTop || vy >= 128 - footerH) return 0;
    int tappedRow = _repScroll + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _repCount) return 0;
    if (tappedRow == _repSel) return 2;
    _repSel = tappedRow;
    return 1;
  }
#endif

  // Returns the current path formatted as a comma-separated string, suitable
  // for pre-populating an external text editor (e.g. the T5S3 virtual keyboard).
  // The returned pointer references an internal buffer and is valid until the
  // next call to this method or to setTypedPath()/parseTypedPath().
  const char* getCurrentPathAsText() {
    pathToEditBuf();
    return _editBuf;
  }

  // Apply a path typed externally (via virtual keyboard submission).
  // Replaces the working path buffer with whatever parses out of `text`
  // and ensures the inline editor flag is cleared so the menu redraws cleanly.
  void setTypedPath(const char* text) {
    if (!text) return;
    strncpy(_editBuf, text, sizeof(_editBuf) - 1);
    _editBuf[sizeof(_editBuf) - 1] = '\0';
    parseTypedPath();
    _editing = false;
  }

  void enter(int pathHashMode) {
    _state = STATE_BUILD;
    _hopCount = 0;
    _menuSel = 0;
    _editing = false;
    _editPos = 0;
    memset(_editBuf, 0, sizeof(_editBuf));
    _repSel = 0;
    _repScroll = 0;
    _wantExit = false;
#if defined(MECK_TWATCH)
    _wantKeyboard = false;
#endif
    _resultScroll = 0;
    memset(_pathBuf, 0, sizeof(_pathBuf));
    memset(&_result, 0, sizeof(_result));

    // Default to device path hash mode (clamped to 1 or 2 for trace)
    _bytesPerHop = (pathHashMode >= 1) ? 2 : 1;
  }

  // Called by MyMesh::onTraceRecv() via UITask
  void onTraceResult(uint32_t tag, uint8_t flags,
                     const uint8_t* path_snrs, const uint8_t* path_hashes,
                     uint8_t path_byte_len, int8_t final_snr) {
    if (_state != STATE_RUNNING) return;
    if (tag != _traceTag) return;  // Not our trace

    uint8_t pathSz = flags & 0x03;
    int numHops = (pathSz > 0) ? (path_byte_len >> pathSz) : path_byte_len;

    _result.valid = true;
    _result.totalHops = numHops;
    _result.final_snr = final_snr;
    _result.duration_ms = millis() - _traceSentAt;

    // Copy hash data
    int copyBytes = path_byte_len;
    if (copyBytes > (int)sizeof(_result.hashes)) copyBytes = sizeof(_result.hashes);
    memcpy(_result.hashes, path_hashes, copyBytes);

    // Count SNR entries (= number of hops that actually forwarded)
    int snrCount = numHops;
    if (snrCount > TRACE_MAX_HOPS) snrCount = TRACE_MAX_HOPS;
    _result.hopCount = snrCount;
    for (int i = 0; i < snrCount; i++) {
      _result.snrs[i] = (int8_t)path_snrs[i];
    }

    _state = STATE_RESULTS;
    _resultScroll = 0;
    Serial.printf("[Trace] Result received: %d hops, %dms\n", numHops, _result.duration_ms);
  }

  // --- Render ---
  int render(DisplayDriver& display) override {
    // Header
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.print("Trace Path");
    display.drawRect(0, 11, display.width(), 1);

    if (_state == STATE_BUILD) {
      return renderBuild(display);
    } else if (_state == STATE_PICK_HOP) {
      return renderPicker(display);
    } else if (_state == STATE_RUNNING) {
      return renderRunning(display);
    } else {
      return renderResults(display);
    }
  }

private:
  int renderBuild(DisplayDriver& display) {
    char tmp[TRACE_EDIT_BUF + 16];
    int y = 14;
    int lineH = 11;
    int menuCount = buildMenuCount();
    int maxVisible = (display.height() - y - 14) / lineH;
    if (maxVisible < 1) maxVisible = 1;

    // Scroll window
    int scrollTop = 0;
    if (_menuSel >= scrollTop + maxVisible) scrollTop = _menuSel - maxVisible + 1;
    if (_menuSel < scrollTop) scrollTop = _menuSel;

    for (int vi = 0; vi < maxVisible && (scrollTop + vi) < menuCount; vi++) {
      int idx = scrollTop + vi;
      MenuItem item = menuItemAt(idx);
      char prefix = (idx == _menuSel) ? '>' : ' ';

      display.setCursor(0, y);
      display.setColor(DisplayDriver::LIGHT);

      switch (item) {
        case MENU_PATH_SIZE:
          snprintf(tmp, sizeof(tmp), "%c Mode: %d-byte", prefix, _bytesPerHop);
          display.print(tmp);
          if (idx == _menuSel) {
            const char* hint = "(A/D)";
            display.setCursor(display.width() - display.getTextWidth(hint) - 4, y);
            display.print(hint);
          }
          break;

        case MENU_TYPE_PATH:
          if (_editing) {
            // Active text editor with cursor
            display.setColor(DisplayDriver::GREEN);
            snprintf(tmp, sizeof(tmp), "  Path: %s_", _editBuf);
            display.print(tmp);
          } else if (_hopCount > 0) {
            // Show current path as decimal values
            char pathStr[TRACE_EDIT_BUF];
            pathStr[0] = '\0';
            int pos = 0;
            for (int i = 0; i < _hopCount && pos < (int)sizeof(pathStr) - 8; i++) {
              if (i > 0) pathStr[pos++] = ',';
              if (_bytesPerHop == 1) {
                pos += snprintf(&pathStr[pos], sizeof(pathStr) - pos, "%02X", _pathBuf[i]);
              } else {
                uint16_t val = ((uint16_t)_pathBuf[i * 2] << 8) | _pathBuf[i * 2 + 1];
                pos += snprintf(&pathStr[pos], sizeof(pathStr) - pos, "%04X", val);
              }
            }
            snprintf(tmp, sizeof(tmp), "%c Path: %s", prefix, pathStr);
            display.print(tmp);
          } else {
#if defined(LilyGo_T5S3_EPaper_Pro)
            snprintf(tmp, sizeof(tmp), "%c Type Path: [Long press]", prefix);
#elif defined(MECK_TWATCH)
            snprintf(tmp, sizeof(tmp), "%c Type Path: [Tap]", prefix);
#else
            snprintf(tmp, sizeof(tmp), "%c Type Path: [Press Enter]", prefix);
#endif
            display.print(tmp);
          }
          break;

        case MENU_ADD_HOP:
          display.setColor(DisplayDriver::GREEN);
          snprintf(tmp, sizeof(tmp), "%c + Add repeater...", prefix);
          display.print(tmp);
          break;

        case MENU_REMOVE_LAST:
          snprintf(tmp, sizeof(tmp), "%c - Remove last", prefix);
          display.print(tmp);
          break;

        case MENU_RUN_TRACE:
          display.setColor(DisplayDriver::YELLOW);
          snprintf(tmp, sizeof(tmp), "%c   Run Trace", prefix);
          display.print(tmp);
          break;

        case MENU_EXIT:
          snprintf(tmp, sizeof(tmp), "%c   Exit", prefix);
          display.print(tmp);
          break;

        default:
          // Hop line
          if (item >= MENU_HOP_BASE && item < MENU_HOP_BASE + TRACE_MAX_HOPS) {
            int hopIdx = item - MENU_HOP_BASE;
            int offset = hopIdx * _bytesPerHop;
            char hopName[24];
            uint16_t hashVal;
            if (_bytesPerHop == 1) {
              hashVal = _pathBuf[offset];
            } else {
              hashVal = ((uint16_t)_pathBuf[offset] << 8) | _pathBuf[offset + 1];
            }
            if (findNameForHash(&_pathBuf[offset], _bytesPerHop, hopName, sizeof(hopName))) {
              truncateName(hopName);
              display.setColor(DisplayDriver::GREEN);
              snprintf(tmp, sizeof(tmp), "%c%d: %s (%X)", prefix, hopIdx + 1,
                       hopName, hashVal);
            } else {
              snprintf(tmp, sizeof(tmp), "%c%d: (%X)", prefix, hopIdx + 1, hashVal);
            }
            display.print(tmp);
          }
          break;
      }
      y += lineH;
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
    if (_editing) {
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.print("Boot:Cancel  Tap:Apply");
#else
      display.print("Sh+Del:Cancel Enter:Apply");
#endif
    } else {
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.print("Boot:Exit  Tap:Sel");
#elif defined(MECK_TWATCH)
      display.print("Tap:Sel  Hold:Go");
#else
      display.print("Sh+Del:Exit W/S:Nav Ent:Sel");
#endif
    }

    return 5000;
  }

  int renderPicker(DisplayDriver& display) {
    char tmp[48];
    int y = 14;
    int lineH = 11;
    int maxVisible = (display.height() - y - 14) / lineH;
    if (maxVisible < 1) maxVisible = 1;

    if (_repCount == 0) {
      display.setCursor(0, y);
      display.setColor(DisplayDriver::RED);
      display.print("No repeaters in contacts");
      y += lineH;
      display.setColor(DisplayDriver::LIGHT);
      display.print("Press Q to go back");
    } else {
      // Clamp scroll
      if (_repSel >= _repCount) _repSel = _repCount - 1;
      if (_repSel < 0) _repSel = 0;
      if (_repSel < _repScroll) _repScroll = _repSel;
      if (_repSel >= _repScroll + maxVisible) _repScroll = _repSel - maxVisible + 1;

      for (int vi = 0; vi < maxVisible && (_repScroll + vi) < _repCount; vi++) {
        int idx = _repScroll + vi;
        uint16_t contactIdx = _repIdx[idx];
        ContactInfo c;
        if (!the_mesh.getContactByIdx(contactIdx, c)) continue;

        char prefix = (idx == _repSel) ? '>' : ' ';
        display.setCursor(0, y);

        // Show name + decimal hash value
        char filteredName[24];
        display.translateUTF8ToBlocks(filteredName, c.name, sizeof(filteredName));
        truncateName(filteredName, 14);  // Picker has more room
        uint16_t hashVal;
        if (_bytesPerHop == 1) {
          hashVal = c.id.pub_key[0];
        } else {
          hashVal = ((uint16_t)c.id.pub_key[0] << 8) | c.id.pub_key[1];
        }
        snprintf(tmp, sizeof(tmp), "%c %s (%X)", prefix, filteredName, hashVal);
        display.setColor((idx == _repSel) ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
        display.print(tmp);
        y += lineH;
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.print("Boot:Back  Tap:Add");
#elif defined(MECK_TWATCH)
    display.print("Tap:Sel  Hold:Add");
#else
    display.print("Sh+Del:Back W/S:Scroll Ent:Add");
#endif

    return 5000;
  }

  int renderRunning(DisplayDriver& display) {
    int y = 14;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);
    display.print("Tracing...");
    y += 14;

    // Show path summary
    display.setColor(DisplayDriver::LIGHT);
    char tmp[48];
    snprintf(tmp, sizeof(tmp), "%d hops, %d-byte mode", _hopCount, _bytesPerHop);
    display.setCursor(0, y);
    display.print(tmp);
    y += 14;

    // Elapsed time
    unsigned long elapsed = millis() - _traceSentAt;
    snprintf(tmp, sizeof(tmp), "Elapsed: %lu ms", elapsed);
    display.setCursor(0, y);
    display.print(tmp);
    y += 14;

    // Timeout bar
    int barW = display.width() - 20;
    int barH = 4;
    int barX = 10;
    display.setColor(DisplayDriver::DARK);
    display.drawRect(barX, y, barW, barH);
    int fill = (int)((unsigned long)barW * elapsed / TRACE_TIMEOUT_MS);
    if (fill > barW) fill = barW;
    display.setColor(DisplayDriver::GREEN);
    display.fillRect(barX, y, fill, barH);

    // Check timeout
    if (elapsed >= TRACE_TIMEOUT_MS) {
      _state = STATE_RESULTS;
      _result.valid = false;
      _result.duration_ms = TRACE_TIMEOUT_MS;
      Serial.println("[Trace] Timeout");
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.print("Boot:Cancel");
#else
    display.print("Sh+Del:Cancel");
#endif

    return 500;  // Fast refresh for elapsed timer
  }

  int renderResults(DisplayDriver& display) {
    char tmp[48];
    int y = 14;
    int lineH = 12;

    if (!_result.valid) {
      display.setColor(DisplayDriver::RED);
      display.setCursor(0, y);
      display.print("Trace timed out");
      y += lineH;
      display.setColor(DisplayDriver::LIGHT);
      snprintf(tmp, sizeof(tmp), "No response after %ds", TRACE_TIMEOUT_MS / 1000);
      display.setCursor(0, y);
      display.print(tmp);
    } else {
      // Duration header
      display.setColor(DisplayDriver::GREEN);
      snprintf(tmp, sizeof(tmp), "Complete: %dms", (int)_result.duration_ms);
      display.setCursor(0, y);
      display.print(tmp);
      y += lineH + 2;

      int maxVisible = (display.height() - y - 14) / lineH;
      if (maxVisible < 1) maxVisible = 1;

      // Clamp scroll
      int totalItems = _result.hopCount + 1;  // hops + final SNR line
      if (_resultScroll > totalItems - maxVisible) _resultScroll = totalItems - maxVisible;
      if (_resultScroll < 0) _resultScroll = 0;

      for (int vi = 0; vi < maxVisible && (_resultScroll + vi) < totalItems; vi++) {
        int idx = _resultScroll + vi;
        display.setCursor(0, y);

        if (idx < _result.hopCount) {
          // Hop entry
          int offset = idx * _bytesPerHop;
          char hopName[20];
          bool resolved = findNameForHash(&_result.hashes[offset], _bytesPerHop,
                                          hopName, sizeof(hopName));
          if (resolved) truncateName(hopName);

          float snr = _result.snrs[idx] / 4.0f;

          display.setColor(resolved ? DisplayDriver::GREEN : DisplayDriver::LIGHT);
          if (resolved) {
            snprintf(tmp, sizeof(tmp), "%d: %s", idx + 1, hopName);
          } else {
            uint16_t hashVal;
            if (_bytesPerHop == 1) {
              hashVal = _result.hashes[offset];
            } else {
              hashVal = ((uint16_t)_result.hashes[offset] << 8) | _result.hashes[offset + 1];
            }
            snprintf(tmp, sizeof(tmp), "%d: (%X)", idx + 1, hashVal);
          }
          display.print(tmp);

          // SNR value on right
          snprintf(tmp, sizeof(tmp), "%.1fdB", snr);
          int snrW = display.getTextWidth(tmp);
          int barsW = 14;
          display.setCursor(display.width() - snrW - barsW - 4, y);
          display.setColor(DisplayDriver::LIGHT);
          display.print(tmp);

          // Signal bars
          drawSignalBars(display, display.width() - barsW - 1, y, _result.snrs[idx]);

        } else {
          // Final SNR (response arriving back at this node)
          float snr = _result.final_snr / 4.0f;
          display.setColor(DisplayDriver::YELLOW);
          snprintf(tmp, sizeof(tmp), "Return SNR: %.1fdB", snr);
          display.print(tmp);
          drawSignalBars(display, display.width() - 15, y, _result.final_snr);
        }
        y += lineH;
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, footerY);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.print("Boot:Back  Tap:New Trace");
#elif defined(MECK_TWATCH)
    display.print("Hold: New Trace");
#else
    display.print("Sh+Del:Back  Ent:New Trace");
#endif

    return 5000;
  }

public:
  // --- Input handling ---
  bool handleInput(char c) override {
    // Text editing mode consumes all keys
    if (_editing) {
      return handleEditInput(c);
    }

    switch (_state) {
      case STATE_BUILD:   return handleBuildInput(c);
      case STATE_PICK_HOP: return handlePickerInput(c);
      case STATE_RUNNING: return handleRunningInput(c);
      case STATE_RESULTS: return handleResultsInput(c);
    }
    return false;
  }

private:
  // --- Text editor for typed path ---
  bool handleEditInput(char c) {
    // Enter: apply typed path
    if (c == '\r' || c == 13) {
      int parsed = parseTypedPath();
      if (parsed < 0) {
        Serial.println("[Trace] Failed to parse typed path");
        // Stay in edit mode -- user can fix
      } else {
        Serial.printf("[Trace] Parsed %d hops from typed path\n", parsed);
        _editing = false;
      }
      return true;
    }
    // Shift+Del: cancel edit
    if (c == KEY_CANCEL) {
      _editing = false;
      return true;
    }
    // Backspace
    if (c == '\b') {
      if (_editPos > 0) {
        _editPos--;
        _editBuf[_editPos] = '\0';
      }
      return true;
    }
    // Accept hex digits, commas, spaces
    if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')
        || c == ',' || c == ' ') {
      if (_editPos < TRACE_EDIT_BUF - 1) {
        _editBuf[_editPos++] = c;
        _editBuf[_editPos] = '\0';
      }
      return true;
    }
    return true;  // Consume all keys in edit mode
  }

  bool handleBuildInput(char c) {
    int menuCount = buildMenuCount();

    // W - up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_menuSel > 0) _menuSel--;
      return true;
    }
    // S - down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_menuSel < menuCount - 1) _menuSel++;
      return true;
    }
    // A/D - toggle mode on path size row
    if ((c == 'a' || c == 'A' || c == 'd' || c == 'D') && menuItemAt(_menuSel) == MENU_PATH_SIZE) {
      _bytesPerHop = (_bytesPerHop == 1) ? 2 : 1;
      // Changing mode clears path (byte layout is different)
      _hopCount = 0;
      memset(_pathBuf, 0, sizeof(_pathBuf));
      return true;
    }
    // Shift+Del - exit
    if (c == KEY_CANCEL) {
      _wantExit = true;
      return true;
    }
    // Enter - select
    if (c == '\r' || c == 13) {
      MenuItem item = menuItemAt(_menuSel);
      switch (item) {
        case MENU_TYPE_PATH:
          pathToEditBuf();
#if defined(MECK_TWATCH)
          // Watch: no physical keys -- request the on-screen keyboard. The main
          // loop polls wantsKeyboard() and opens it, pre-filled with the path.
          _wantKeyboard = true;
#else
          // Enter inline edit mode -- pre-fill with current path if any
          _editing = true;
#endif
          return true;

        case MENU_ADD_HOP:
          buildRepeaterList();
          _repSel = 0;
          _repScroll = 0;
          _state = STATE_PICK_HOP;
          return true;

        case MENU_REMOVE_LAST:
          if (_hopCount > 0) {
            _hopCount--;
            if (_menuSel >= buildMenuCount()) _menuSel = buildMenuCount() - 1;
          }
          return true;

        case MENU_RUN_TRACE:
          return sendTrace();

        case MENU_EXIT:
          _wantExit = true;
          return true;

        default:
          break;
      }
      return true;
    }
    return false;
  }

  bool handlePickerInput(char c) {
    // W - up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_repSel > 0) _repSel--;
      return true;
    }
    // S - down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_repSel < _repCount - 1) _repSel++;
      return true;
    }
    // Shift+Del - back to build
    if (c == KEY_CANCEL) {
      _state = STATE_BUILD;
      return true;
    }
    // Enter - add selected repeater
    if (c == '\r' || c == 13) {
      if (_repCount > 0 && _repSel >= 0 && _repSel < _repCount) {
        ContactInfo contact;
        if (the_mesh.getContactByIdx(_repIdx[_repSel], contact)) {
          int offset = _hopCount * _bytesPerHop;
          memcpy(&_pathBuf[offset], contact.id.pub_key, _bytesPerHop);
          _hopCount++;
          uint16_t hashVal = ((uint16_t)contact.id.pub_key[0] << 8)
                           | contact.id.pub_key[1];
          Serial.printf("[Trace] Added hop %d: %s (%X)\n",
                        _hopCount, contact.name, hashVal);
        }
        _state = STATE_BUILD;
        _menuSel = _hopCount + 1;  // Point to row after last hop
      }
      return true;
    }
    return false;
  }

  bool handleRunningInput(char c) {
    // Shift+Del - cancel
    if (c == KEY_CANCEL) {
      _state = STATE_BUILD;
      return true;
    }
    return true;  // Consume all keys while running
  }

  bool handleResultsInput(char c) {
    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_resultScroll > 0) _resultScroll--;
      return true;
    }
    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      _resultScroll++;
      return true;
    }
    // Shift+Del - back to build screen (keep path)
    if (c == KEY_CANCEL) {
      _state = STATE_BUILD;
      _menuSel = 0;
      return true;
    }
    // Enter - new trace (re-run with same path)
    if (c == '\r' || c == 13) {
      return sendTrace();
    }
    return false;
  }

  // --- Send trace ---
  bool sendTrace() {
    if (_hopCount <= 0) return true;

    // Generate random tag and auth code
    the_mesh.getRNG()->random((uint8_t*)&_traceTag, 4);
    the_mesh.getRNG()->random((uint8_t*)&_traceAuth, 4);

    // flags: lower 2 bits = path_sz
    // path_sz 0 = 1-byte hashes, path_sz 1 = 2-byte hashes
    uint8_t pathSz = (_bytesPerHop == 2) ? 1 : 0;
    uint8_t flags = pathSz;

    mesh::Packet* pkt = the_mesh.createTrace(_traceTag, _traceAuth, flags);
    if (!pkt) {
      Serial.println("[Trace] Failed to create trace packet (pool empty)");
      return true;
    }

    // Path bytes to send
    uint8_t pathByteLen = _hopCount * _bytesPerHop;

    // sendDirect for TRACE appends path to payload and sets path_len=0
    the_mesh.sendDirect(pkt, _pathBuf, pathByteLen);

    _traceSentAt = millis();
    _state = STATE_RUNNING;
    memset(&_result, 0, sizeof(_result));

    Serial.printf("[Trace] Sent: tag=0x%08X, %d hops, %d-byte, %d path bytes\n",
                  _traceTag, _hopCount, _bytesPerHop, pathByteLen);
    Serial.printf("[Trace] Path hex:");
    for (int i = 0; i < pathByteLen; i++) {
      Serial.printf(" %02X", _pathBuf[i]);
    }
    Serial.println();
    return true;
  }
};