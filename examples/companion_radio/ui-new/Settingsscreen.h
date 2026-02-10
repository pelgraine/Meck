#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include "../NodePrefs.h"

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

// ---------------------------------------------------------------------------
// Radio presets
// ---------------------------------------------------------------------------
struct RadioPreset {
  const char* name;
  float freq;
  float bw;
  uint8_t sf;
  uint8_t cr;
  uint8_t tx_power;
};

static const RadioPreset RADIO_PRESETS[] = {
  { "MeshCore Default", 915.0f, 250.0f, 10, 5, 20 },
  { "Long Range",       915.0f, 125.0f, 12, 8, 20 },
  { "Fast/Short",       915.0f, 500.0f,  7, 5, 20 },
  { "EU Default",       869.4f, 250.0f, 10, 5, 14 },
};
#define NUM_RADIO_PRESETS (sizeof(RADIO_PRESETS) / sizeof(RADIO_PRESETS[0]))

// ---------------------------------------------------------------------------
// Settings row types
// ---------------------------------------------------------------------------
enum SettingsRowType : uint8_t {
  ROW_NAME,           // Device name (text editor)
  ROW_RADIO_PRESET,   // Radio preset picker
  ROW_FREQ,           // Frequency (float)
  ROW_BW,             // Bandwidth (float)
  ROW_SF,             // Spreading factor (5-12)
  ROW_CR,             // Coding rate (5-8)
  ROW_TX_POWER,       // TX power (1-20 dBm)
  ROW_UTC_OFFSET,     // UTC offset (-12 to +14)
  ROW_CH_HEADER,      // "--- Channels ---" separator
  ROW_CHANNEL,        // A channel entry (dynamic, index stored separately)
  ROW_ADD_CHANNEL,    // "+ Add Hashtag Channel"
  ROW_INFO_HEADER,    // "--- Info ---" separator
  ROW_PUB_KEY,        // Public key display
  ROW_FIRMWARE,       // Firmware version
};

// ---------------------------------------------------------------------------
// Editing modes
// ---------------------------------------------------------------------------
enum EditMode : uint8_t {
  EDIT_NONE,         // Just browsing
  EDIT_TEXT,         // Typing into a text buffer (name, channel name)
  EDIT_PICKER,       // A/D cycles options (radio preset)
  EDIT_NUMBER,       // W/S adjusts value (freq, BW, SF, CR, TX, UTC)
  EDIT_CONFIRM,      // Confirmation dialog (delete channel, apply radio)
};

// Max rows in the settings list
#define SETTINGS_MAX_ROWS 40
#define SETTINGS_TEXT_BUF  33  // 32 chars + null

class SettingsScreen : public UIScreen {
private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;

  // Row table — rebuilt whenever channels change
  struct Row {
    SettingsRowType type;
    uint8_t param;       // channel index for ROW_CHANNEL, preset index for ROW_RADIO_PRESET
  };
  Row _rows[SETTINGS_MAX_ROWS];
  int _numRows;

  // Cursor & scroll
  int _cursor;        // selected row
  int _scrollTop;     // first visible row

  // Editing state
  EditMode _editMode;
  char _editBuf[SETTINGS_TEXT_BUF];
  int _editPos;
  int _editPickerIdx;       // for preset picker
  float _editFloat;         // for freq/BW editing
  int _editInt;             // for SF/CR/TX/UTC editing
  int _confirmAction;       // 0=none, 1=delete channel, 2=apply radio

  // Onboarding mode
  bool _onboarding;

  // Dirty flag for radio params — prompt to apply
  bool _radioChanged;

  // ---------------------------------------------------------------------------
  // Row table management
  // ---------------------------------------------------------------------------

  void rebuildRows() {
    _numRows = 0;

    addRow(ROW_NAME);
    addRow(ROW_RADIO_PRESET);
    addRow(ROW_FREQ);
    addRow(ROW_BW);
    addRow(ROW_SF);
    addRow(ROW_CR);
    addRow(ROW_TX_POWER);
    addRow(ROW_UTC_OFFSET);
    addRow(ROW_CH_HEADER);

    // Enumerate current channels
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        addRow(ROW_CHANNEL, i);
      } else {
        break;  // channels are contiguous
      }
    }

    addRow(ROW_ADD_CHANNEL);
    addRow(ROW_INFO_HEADER);
    addRow(ROW_PUB_KEY);
    addRow(ROW_FIRMWARE);

    // Clamp cursor
    if (_cursor >= _numRows) _cursor = _numRows - 1;
    if (_cursor < 0) _cursor = 0;
    skipNonSelectable(1);
  }

  void addRow(SettingsRowType type, uint8_t param = 0) {
    if (_numRows < SETTINGS_MAX_ROWS) {
      _rows[_numRows].type = type;
      _rows[_numRows].param = param;
      _numRows++;
    }
  }

  bool isSelectable(int idx) const {
    if (idx < 0 || idx >= _numRows) return false;
    SettingsRowType t = _rows[idx].type;
    return t != ROW_CH_HEADER && t != ROW_INFO_HEADER;
  }

  void skipNonSelectable(int dir) {
    while (_cursor >= 0 && _cursor < _numRows && !isSelectable(_cursor)) {
      _cursor += dir;
    }
    if (_cursor < 0) _cursor = 0;
    if (_cursor >= _numRows) _cursor = _numRows - 1;
  }

  // ---------------------------------------------------------------------------
  // Radio preset detection
  // ---------------------------------------------------------------------------

  int detectCurrentPreset() const {
    for (int i = 0; i < (int)NUM_RADIO_PRESETS; i++) {
      const RadioPreset& p = RADIO_PRESETS[i];
      if (fabsf(_prefs->freq - p.freq) < 0.01f &&
          fabsf(_prefs->bw - p.bw) < 0.01f &&
          _prefs->sf == p.sf &&
          _prefs->cr == p.cr &&
          _prefs->tx_power_dbm == p.tx_power) {
        return i;
      }
    }
    return -1;  // Custom
  }

  // ---------------------------------------------------------------------------
  // Hashtag channel creation
  // ---------------------------------------------------------------------------

  void createHashtagChannel(const char* name) {
    // Build channel name with # prefix if not already present
    char chanName[32];
    if (name[0] == '#') {
      strncpy(chanName, name, sizeof(chanName));
    } else {
      chanName[0] = '#';
      strncpy(&chanName[1], name, sizeof(chanName) - 1);
    }
    chanName[31] = '\0';

    // Generate 128-bit PSK from SHA-256 of channel name
    ChannelDetails newCh;
    memset(&newCh, 0, sizeof(newCh));
    strncpy(newCh.name, chanName, sizeof(newCh.name));
    newCh.name[31] = '\0';

    // SHA-256 the channel name → first 16 bytes become the secret
    uint8_t hash[32];
    mesh::Utils::sha256(hash, 32, (const uint8_t*)chanName, strlen(chanName));
    memcpy(newCh.channel.secret, hash, 16);
    // Upper 16 bytes left as zero → setChannel uses 128-bit mode

    // Find next empty slot
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails existing;
      if (!the_mesh.getChannel(i, existing) || existing.name[0] == '\0') {
        if (the_mesh.setChannel(i, newCh)) {
          the_mesh.saveChannels();
          Serial.printf("Settings: Created hashtag channel '%s' at idx %d\n", chanName, i);
        }
        break;
      }
    }
  }

  void deleteChannel(uint8_t idx) {
    // Clear the channel by writing an empty ChannelDetails
    // Then compact: shift all channels above it down by one
    ChannelDetails empty;
    memset(&empty, 0, sizeof(empty));

    // Find total channel count
    int total = 0;
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        total = i + 1;
      } else {
        break;
      }
    }

    // Shift channels down
    for (int i = idx; i < total - 1; i++) {
      ChannelDetails next;
      if (the_mesh.getChannel(i + 1, next)) {
        the_mesh.setChannel(i, next);
      }
    }
    // Clear the last slot
    the_mesh.setChannel(total - 1, empty);
    the_mesh.saveChannels();
    Serial.printf("Settings: Deleted channel at idx %d, compacted %d channels\n", idx, total);
  }

  // ---------------------------------------------------------------------------
  // Apply radio parameters live
  // ---------------------------------------------------------------------------

  void applyRadioParams() {
    radio_set_params(_prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr);
    radio_set_tx_power(_prefs->tx_power_dbm);
    the_mesh.savePrefs();
    _radioChanged = false;
    Serial.printf("Settings: Radio params applied - %.3f/%g/%d/%d TX:%d\n",
                  _prefs->freq, _prefs->bw, _prefs->sf, _prefs->cr, _prefs->tx_power_dbm);
  }

public:
  SettingsScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _prefs(prefs),
      _numRows(0), _cursor(0), _scrollTop(0),
      _editMode(EDIT_NONE), _editPos(0), _editPickerIdx(0),
      _editFloat(0), _editInt(0), _confirmAction(0),
      _onboarding(false), _radioChanged(false) {
    memset(_editBuf, 0, sizeof(_editBuf));
  }

  void enter() {
    _editMode = EDIT_NONE;
    _cursor = 0;
    _scrollTop = 0;
    _radioChanged = false;
    rebuildRows();
  }

  void enterOnboarding() {
    enter();
    _onboarding = true;
    // Start editing the device name immediately
    _cursor = 0;  // ROW_NAME
    startEditText(_prefs->node_name);
  }

  bool isOnboarding() const { return _onboarding; }
  bool isEditing() const { return _editMode != EDIT_NONE; }
  bool hasRadioChanges() const { return _radioChanged; }

  // ---------------------------------------------------------------------------
  // Edit mode starters
  // ---------------------------------------------------------------------------

  void startEditText(const char* initial) {
    _editMode = EDIT_TEXT;
    strncpy(_editBuf, initial, SETTINGS_TEXT_BUF - 1);
    _editBuf[SETTINGS_TEXT_BUF - 1] = '\0';
    _editPos = strlen(_editBuf);
  }

  void startEditPicker(int initialIdx) {
    _editMode = EDIT_PICKER;
    _editPickerIdx = initialIdx;
  }

  void startEditFloat(float initial) {
    _editMode = EDIT_NUMBER;
    _editFloat = initial;
  }

  void startEditInt(int initial) {
    _editMode = EDIT_NUMBER;
    _editInt = initial;
  }

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------

  int render(DisplayDriver& display) override {
    char tmp[64];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    if (_onboarding) {
      display.print("Welcome! Setup");
    } else {
      display.print("Settings");
    }

    // Right side: row indicator
    snprintf(tmp, sizeof(tmp), "%d/%d", _cursor + 1, _numRows);
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);

    // === Body ===
    display.setTextSize(0);  // tiny font
    int lineHeight = 9;
    int headerH = 14;
    int footerH = 14;
    int maxY = display.height() - footerH;

    // Center scroll window around cursor
    int maxVisible = (maxY - headerH) / lineHeight;
    if (maxVisible < 3) maxVisible = 3;
    _scrollTop = max(0, min(_cursor - maxVisible / 2, _numRows - maxVisible));
    int endIdx = min(_numRows, _scrollTop + maxVisible);

    int y = headerH;

    for (int i = _scrollTop; i < endIdx && y + lineHeight <= maxY; i++) {
      bool selected = (i == _cursor);
      bool editing = selected && (_editMode != EDIT_NONE);

      // Selection highlight
      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + 5, display.width(), lineHeight);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      display.setCursor(0, y);

      switch (_rows[i].type) {
        case ROW_NAME:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "Name: %s_", _editBuf);
          } else {
            snprintf(tmp, sizeof(tmp), "Name: %s", _prefs->node_name);
          }
          display.print(tmp);
          break;

        case ROW_RADIO_PRESET: {
          int preset = detectCurrentPreset();
          if (editing && _editMode == EDIT_PICKER) {
            if (_editPickerIdx >= 0 && _editPickerIdx < (int)NUM_RADIO_PRESETS) {
              snprintf(tmp, sizeof(tmp), "< %s >", RADIO_PRESETS[_editPickerIdx].name);
            } else {
              strcpy(tmp, "< Custom >");
            }
          } else {
            if (preset >= 0) {
              snprintf(tmp, sizeof(tmp), "Preset: %s", RADIO_PRESETS[preset].name);
            } else {
              strcpy(tmp, "Preset: Custom");
            }
          }
          display.print(tmp);
          break;
        }

        case ROW_FREQ:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "Freq: %.3f <W/S>", _editFloat);
          } else {
            snprintf(tmp, sizeof(tmp), "Freq: %.3f MHz", _prefs->freq);
          }
          display.print(tmp);
          break;

        case ROW_BW:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "BW: %.1f <W/S>", _editFloat);
          } else {
            snprintf(tmp, sizeof(tmp), "BW: %.1f kHz", _prefs->bw);
          }
          display.print(tmp);
          break;

        case ROW_SF:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "SF: %d <W/S>", _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "SF: %d", _prefs->sf);
          }
          display.print(tmp);
          break;

        case ROW_CR:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "CR: %d <W/S>", _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "CR: %d", _prefs->cr);
          }
          display.print(tmp);
          break;

        case ROW_TX_POWER:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "TX: %d dBm <W/S>", _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "TX: %d dBm", _prefs->tx_power_dbm);
          }
          display.print(tmp);
          break;

        case ROW_UTC_OFFSET:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "UTC: %+d <W/S>", _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "UTC Offset: %+d", _prefs->utc_offset_hours);
          }
          display.print(tmp);
          break;

        case ROW_CH_HEADER:
          display.setColor(DisplayDriver::YELLOW);
          display.print("--- Channels ---");
          break;

        case ROW_CHANNEL: {
          uint8_t chIdx = _rows[i].param;
          ChannelDetails ch;
          if (the_mesh.getChannel(chIdx, ch)) {
            if (chIdx == 0) {
              // Public channel - not deletable
              snprintf(tmp, sizeof(tmp), " %s", ch.name);
            } else {
              snprintf(tmp, sizeof(tmp), " %s", ch.name);
              if (selected) {
                // Show delete hint on right
                const char* hint = "Del:X";
                int hintW = display.getTextWidth(hint);
                display.setCursor(display.width() - hintW - 2, y);
                display.print(hint);
                display.setCursor(0, y);
              }
            }
          } else {
            snprintf(tmp, sizeof(tmp), " (empty)");
          }
          display.print(tmp);
          break;
        }

        case ROW_ADD_CHANNEL:
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "# %s_", _editBuf);
          } else {
            display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
            strcpy(tmp, "+ Add Hashtag Channel");
          }
          display.print(tmp);
          break;

        case ROW_INFO_HEADER:
          display.setColor(DisplayDriver::YELLOW);
          display.print("--- Device Info ---");
          break;

        case ROW_PUB_KEY: {
          // Show first 8 bytes of pub key as hex (16 chars)
          char hexBuf[17];
          mesh::Utils::toHex(hexBuf, the_mesh.self_id.pub_key, 8);
          snprintf(tmp, sizeof(tmp), "ID: %s", hexBuf);
          display.print(tmp);
          break;
        }

        case ROW_FIRMWARE:
          snprintf(tmp, sizeof(tmp), "FW: %s", FIRMWARE_VERSION);
          display.print(tmp);
          break;
      }

      y += lineHeight;
    }

    display.setTextSize(1);

    // === Confirmation overlay ===
    if (_editMode == EDIT_CONFIRM) {
      int bx = 4, by = 30, bw = display.width() - 8, bh = 36;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(0);
      if (_confirmAction == 1) {
        uint8_t chIdx = _rows[_cursor].param;
        ChannelDetails ch;
        the_mesh.getChannel(chIdx, ch);
        snprintf(tmp, sizeof(tmp), "Delete %s?", ch.name);
        display.drawTextCentered(display.width() / 2, by + 4, tmp);
      } else if (_confirmAction == 2) {
        display.drawTextCentered(display.width() / 2, by + 4, "Apply radio changes?");
      }
      display.drawTextCentered(display.width() / 2, by + bh - 14, "Enter:Yes  Q:No");
      display.setTextSize(1);
    }

    // === Footer ===
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

    if (_editMode == EDIT_TEXT) {
      display.print("Type, Enter:Ok Q:Cancel");
    } else if (_editMode == EDIT_PICKER) {
      display.print("A/D:Choose Enter:Ok");
    } else if (_editMode == EDIT_NUMBER) {
      display.print("W/S:Adj Enter:Ok Q:Cancel");
    } else if (_editMode == EDIT_CONFIRM) {
      // Footer already covered by overlay
    } else {
      display.print("Q:Bck");
      const char* r = "W/S:Up/Dwn Entr:Chng";
      display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
      display.print(r);
    }

    return _editMode != EDIT_NONE ? 700 : 1000;
  }

  // ---------------------------------------------------------------------------
  // Input handling
  // ---------------------------------------------------------------------------

  // Handle a keyboard character. Returns true if the screen consumed the input.
  bool handleKeyInput(char c) {
    // --- Confirmation dialog ---
    if (_editMode == EDIT_CONFIRM) {
      if (c == '\r' || c == 13) {
        if (_confirmAction == 1) {
          // Delete channel
          uint8_t chIdx = _rows[_cursor].param;
          deleteChannel(chIdx);
          rebuildRows();
        } else if (_confirmAction == 2) {
          applyRadioParams();
        }
        _editMode = EDIT_NONE;
        _confirmAction = 0;
        return true;
      }
      if (c == 'q' || c == 'Q') {
        _editMode = EDIT_NONE;
        _confirmAction = 0;
        return true;
      }
      return true;  // consume all keys in confirm mode
    }

    // --- Text editing mode ---
    if (_editMode == EDIT_TEXT) {
      if (c == '\r' || c == 13) {
        // Confirm text edit
        SettingsRowType type = _rows[_cursor].type;
        if (type == ROW_NAME) {
          if (_editPos > 0) {
            strncpy(_prefs->node_name, _editBuf, sizeof(_prefs->node_name));
            _prefs->node_name[31] = '\0';
            the_mesh.savePrefs();
            Serial.printf("Settings: Name set to '%s'\n", _prefs->node_name);
          }
          _editMode = EDIT_NONE;
          if (_onboarding) {
            // Move to radio preset selection
            _cursor = 1;  // ROW_RADIO_PRESET
            startEditPicker(max(0, detectCurrentPreset()));
          }
        } else if (type == ROW_ADD_CHANNEL) {
          if (_editPos > 0) {
            createHashtagChannel(_editBuf);
            rebuildRows();
          }
          _editMode = EDIT_NONE;
        }
        return true;
      }
      if (c == 'q' || c == 'Q' || c == 27) {
        _editMode = EDIT_NONE;
        return true;
      }
      if (c == '\b') {
        if (_editPos > 0) {
          _editPos--;
          _editBuf[_editPos] = '\0';
        }
        return true;
      }
      // Printable character
      if (c >= 32 && c < 127 && _editPos < SETTINGS_TEXT_BUF - 1) {
        _editBuf[_editPos++] = c;
        _editBuf[_editPos] = '\0';
        return true;
      }
      return true;  // consume all keys in text edit
    }

    // --- Picker mode (radio preset) ---
    if (_editMode == EDIT_PICKER) {
      if (c == 'a' || c == 'A') {
        _editPickerIdx--;
        if (_editPickerIdx < 0) _editPickerIdx = (int)NUM_RADIO_PRESETS - 1;
        return true;
      }
      if (c == 'd' || c == 'D') {
        _editPickerIdx++;
        if (_editPickerIdx >= (int)NUM_RADIO_PRESETS) _editPickerIdx = 0;
        return true;
      }
      if (c == '\r' || c == 13) {
        // Apply preset
        if (_editPickerIdx >= 0 && _editPickerIdx < (int)NUM_RADIO_PRESETS) {
          const RadioPreset& p = RADIO_PRESETS[_editPickerIdx];
          _prefs->freq = p.freq;
          _prefs->bw = p.bw;
          _prefs->sf = p.sf;
          _prefs->cr = p.cr;
          _prefs->tx_power_dbm = p.tx_power;
          _radioChanged = true;
        }
        _editMode = EDIT_NONE;
        if (_onboarding) {
          // Apply and finish onboarding
          applyRadioParams();
          _onboarding = false;
        }
        return true;
      }
      if (c == 'q' || c == 'Q') {
        _editMode = EDIT_NONE;
        return true;
      }
      return true;
    }

    // --- Number editing mode ---
    if (_editMode == EDIT_NUMBER) {
      SettingsRowType type = _rows[_cursor].type;

      if (c == 'w' || c == 'W') {
        switch (type) {
          case ROW_FREQ:    _editFloat += 0.1f; break;
          case ROW_BW:
            // Cycle through common bandwidths
            if (_editFloat < 31.25f) _editFloat = 31.25f;
            else if (_editFloat < 62.5f) _editFloat = 62.5f;
            else if (_editFloat < 125.0f) _editFloat = 125.0f;
            else if (_editFloat < 250.0f) _editFloat = 250.0f;
            else _editFloat = 500.0f;
            break;
          case ROW_SF:      if (_editInt < 12) _editInt++; break;
          case ROW_CR:      if (_editInt < 8)  _editInt++; break;
          case ROW_TX_POWER: if (_editInt < MAX_LORA_TX_POWER) _editInt++; break;
          case ROW_UTC_OFFSET: if (_editInt < 14) _editInt++; break;
          default: break;
        }
        return true;
      }
      if (c == 's' || c == 'S') {
        switch (type) {
          case ROW_FREQ:    _editFloat -= 0.1f; break;
          case ROW_BW:
            if (_editFloat > 250.0f) _editFloat = 250.0f;
            else if (_editFloat > 125.0f) _editFloat = 125.0f;
            else if (_editFloat > 62.5f) _editFloat = 62.5f;
            else _editFloat = 31.25f;
            break;
          case ROW_SF:      if (_editInt > 5)  _editInt--; break;
          case ROW_CR:      if (_editInt > 5)  _editInt--; break;
          case ROW_TX_POWER: if (_editInt > 1)  _editInt--; break;
          case ROW_UTC_OFFSET: if (_editInt > -12) _editInt--; break;
          default: break;
        }
        return true;
      }
      if (c == '\r' || c == 13) {
        // Confirm number edit
        switch (type) {
          case ROW_FREQ:
            _prefs->freq = constrain(_editFloat, 400.0f, 2500.0f);
            _radioChanged = true;
            break;
          case ROW_BW:
            _prefs->bw = _editFloat;
            _radioChanged = true;
            break;
          case ROW_SF:
            _prefs->sf = (uint8_t)constrain(_editInt, 5, 12);
            _radioChanged = true;
            break;
          case ROW_CR:
            _prefs->cr = (uint8_t)constrain(_editInt, 5, 8);
            _radioChanged = true;
            break;
          case ROW_TX_POWER:
            _prefs->tx_power_dbm = (uint8_t)constrain(_editInt, 1, MAX_LORA_TX_POWER);
            _radioChanged = true;
            break;
          case ROW_UTC_OFFSET:
            _prefs->utc_offset_hours = (int8_t)constrain(_editInt, -12, 14);
            the_mesh.savePrefs();
            break;
          default: break;
        }
        _editMode = EDIT_NONE;
        return true;
      }
      if (c == 'q' || c == 'Q') {
        _editMode = EDIT_NONE;
        return true;
      }
      return true;
    }

    // --- Normal browsing mode ---

    // W/S: navigate
    if (c == 'w' || c == 'W') {
      if (_cursor > 0) {
        _cursor--;
        skipNonSelectable(-1);
      }
      Serial.printf("Settings: cursor=%d/%d row=%d\n", _cursor, _numRows, _rows[_cursor].type);
      return true;
    }
    if (c == 's' || c == 'S') {
      if (_cursor < _numRows - 1) {
        _cursor++;
        skipNonSelectable(1);
      }
      Serial.printf("Settings: cursor=%d/%d row=%d\n", _cursor, _numRows, _rows[_cursor].type);
      return true;
    }

    // Enter: start editing the selected row
    if (c == '\r' || c == 13) {
      SettingsRowType type = _rows[_cursor].type;
      switch (type) {
        case ROW_NAME:
          startEditText(_prefs->node_name);
          break;
        case ROW_RADIO_PRESET:
          startEditPicker(max(0, detectCurrentPreset()));
          break;
        case ROW_FREQ:
          startEditFloat(_prefs->freq);
          break;
        case ROW_BW:
          startEditFloat(_prefs->bw);
          break;
        case ROW_SF:
          startEditInt(_prefs->sf);
          break;
        case ROW_CR:
          startEditInt(_prefs->cr);
          break;
        case ROW_TX_POWER:
          startEditInt(_prefs->tx_power_dbm);
          break;
        case ROW_UTC_OFFSET:
          startEditInt(_prefs->utc_offset_hours);
          break;
        case ROW_ADD_CHANNEL:
          startEditText("");
          break;
        case ROW_CHANNEL:
        case ROW_PUB_KEY:
        case ROW_FIRMWARE:
          // Not directly editable on Enter
          break;
        default:
          break;
      }
      return true;
    }

    // X: delete channel (when on a channel row, idx > 0)
    if (c == 'x' || c == 'X') {
      if (_rows[_cursor].type == ROW_CHANNEL && _rows[_cursor].param > 0) {
        _editMode = EDIT_CONFIRM;
        _confirmAction = 1;
        return true;
      }
    }

    // Q: back — if radio changed, prompt to apply first
    if (c == 'q' || c == 'Q') {
      if (_radioChanged) {
        _editMode = EDIT_CONFIRM;
        _confirmAction = 2;
        return true;
      }
      _onboarding = false;
      return false;  // Let the caller handle navigation back
    }

    return true;  // Consume all other keys (don't let caller exit)
  }

  // Override handleInput for UIScreen compatibility (used by injectKey)
  bool handleInput(char c) override {
    return handleKeyInput(c);
  }
};