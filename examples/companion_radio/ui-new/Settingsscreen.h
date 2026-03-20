#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include "../NodePrefs.h"

// Inline edit hint shown next to values being adjusted
#if defined(LilyGo_T5S3_EPaper_Pro)
  #define EDIT_ADJ_HINT "<Swipe>"
#else
  #define EDIT_ADJ_HINT "<W/S>"
#endif

#ifdef HAS_4G_MODEM
  #include "ModemManager.h"
#endif

#ifdef MECK_WIFI_COMPANION
  #include <WiFi.h>
  #include <SD.h>
#endif

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

// ---------------------------------------------------------------------------
// Auto-add config bitmask (mirrored from MyMesh.cpp for UI access)
// ---------------------------------------------------------------------------
#define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)  // 0x01 - overwrite oldest non-favourite when full
#define AUTO_ADD_CHAT             (1 << 1)  // 0x02 - auto-add Chat (Companion) (ADV_TYPE_CHAT)
#define AUTO_ADD_REPEATER         (1 << 2)  // 0x04 - auto-add Repeater (ADV_TYPE_REPEATER)
#define AUTO_ADD_ROOM_SERVER      (1 << 3)  // 0x08 - auto-add Room Server (ADV_TYPE_ROOM)
#define AUTO_ADD_SENSOR           (1 << 4)  // 0x10 - auto-add Sensor (ADV_TYPE_SENSOR)

// All type bits combined (excludes overwrite flag)
#define AUTO_ADD_ALL_TYPES (AUTO_ADD_CHAT | AUTO_ADD_REPEATER | \
                            AUTO_ADD_ROOM_SERVER | AUTO_ADD_SENSOR)

// Contact mode indices for picker
#define CONTACT_MODE_AUTO_ALL 0  // Add all contacts automatically
#define CONTACT_MODE_CUSTOM   1  // Per-type toggles
#define CONTACT_MODE_MANUAL   2  // No auto-add, companion app only
#define CONTACT_MODE_COUNT    3

// ---------------------------------------------------------------------------
// Radio presets (shared with Serial CLI in MyMesh.cpp)
// ---------------------------------------------------------------------------
#include "RadioPresets.h"

// ---------------------------------------------------------------------------
// GPS baud rate options (shared with UITask GPS home page overlay)
// ---------------------------------------------------------------------------
static const uint32_t GPS_BAUD_OPTIONS[] = { 0, 4800, 9600, 19200, 38400, 57600, 115200 };
#define GPS_BAUD_OPTION_COUNT 7

static inline const char* gpsBaudLabel(uint32_t baud, char* buf, int bufLen) {
  if (baud == 0) return "Default (38400)";
  snprintf(buf, bufLen, "%lu", (unsigned long)baud);
  return buf;
}

static inline int findGpsBaudIndex(uint32_t baud) {
  for (int i = 0; i < GPS_BAUD_OPTION_COUNT; i++) {
    if (GPS_BAUD_OPTIONS[i] == baud) return i;
  }
  return 0;
}

// Auto-lock timeout options (minutes, 0=disabled)
static const uint8_t AUTO_LOCK_OPTIONS[] = { 0, 2, 5, 10, 15, 30 };
#define AUTO_LOCK_OPTION_COUNT 6

static inline const char* autoLockLabel(uint8_t minutes) {
  if (minutes == 0) return "None";
  static char buf[8];
  snprintf(buf, sizeof(buf), "%d min", minutes);
  return buf;
}

static inline int findAutoLockIndex(uint8_t minutes) {
  for (int i = 0; i < AUTO_LOCK_OPTION_COUNT; i++) {
    if (AUTO_LOCK_OPTIONS[i] == minutes) return i;
  }
  return 0;
}

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
  ROW_MSG_NOTIFY,     // Keyboard flash on new msg toggle
  ROW_DARK_MODE,      // Dark mode toggle (inverted display)
#if defined(LilyGo_T5S3_EPaper_Pro)
  ROW_PORTRAIT_MODE,  // Portrait orientation toggle
#endif
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
  ROW_AUTO_LOCK,      // Auto-lock timeout picker (None/2/5/10/15/30 min)
#endif
  ROW_GPS_BAUD,       // GPS baud rate picker (requires reboot)
  ROW_PATH_HASH_SIZE, // Path hash size (1, 2, or 3 bytes per hop)
  #ifdef MECK_WIFI_COMPANION
  ROW_WIFI_SETUP,     // WiFi SSID/password configuration
  ROW_WIFI_TOGGLE,    // WiFi radio on/off toggle
  #endif
  #ifdef HAS_4G_MODEM
  ROW_MODEM_TOGGLE,   // 4G modem enable/disable toggle (4G builds only)
  // ROW_RINGTONE,       // Incoming call ringtone toggle (4G builds only)
  #endif
  ROW_CONTACT_HEADER,  // "--- Contacts ---" separator
  ROW_CONTACT_MODE,    // Contact auto-add mode picker (Auto All / Custom / Manual)
  ROW_AUTOADD_CHAT,    // Toggle: auto-add Chat clients
  ROW_AUTOADD_REPEATER,// Toggle: auto-add Repeaters
  ROW_AUTOADD_ROOM,    // Toggle: auto-add Room Servers
  ROW_AUTOADD_SENSOR,  // Toggle: auto-add Sensors
  ROW_AUTOADD_OVERWRITE, // Toggle: overwrite oldest non-favourite when full
  ROW_CONTACTS_SUBMENU,  // Folder row → enters Contacts sub-screen
  ROW_CHANNELS_SUBMENU,  // Folder row → enters Channels sub-screen
  ROW_CH_HEADER,      // "--- Channels ---" separator
  ROW_CHANNEL,        // A channel entry (dynamic, index stored separately)
  ROW_ADD_CHANNEL,    // "+ Add Hashtag Channel"
  ROW_INFO_HEADER,    // "--- Info ---" separator
  ROW_PUB_KEY,        // Public key display
  ROW_FIRMWARE,       // Firmware version
  #ifdef HAS_4G_MODEM
  ROW_IMEI,           // IMEI display (read-only)
  ROW_OPERATOR_INFO,  // Carrier/operator display (read-only)
  ROW_APN,            // APN setting (editable)
  #endif
};

// ---------------------------------------------------------------------------
// Editing modes
// ---------------------------------------------------------------------------
enum EditMode : uint8_t {
  EDIT_NONE,         // Just browsing
  EDIT_TEXT,         // Typing into a text buffer (name, channel name)
  EDIT_PICKER,       // A/D cycles options (radio preset, contact mode)
  EDIT_NUMBER,       // W/S adjusts value (freq, BW, SF, CR, TX, UTC)
  EDIT_CONFIRM,      // Confirmation dialog (delete channel, apply radio)
  #ifdef MECK_WIFI_COMPANION
  EDIT_WIFI,         // WiFi scan/select/password flow
  #endif
};

// ---------------------------------------------------------------------------
// Settings sub-screens (collapsible sections)
// ---------------------------------------------------------------------------
enum SubScreen : uint8_t {
  SUB_NONE,        // Top-level settings list
  SUB_CONTACTS,    // Contacts settings sub-screen
  SUB_CHANNELS,    // Channels management sub-screen
};

// Max rows in the settings list (increased for contact sub-toggles + WiFi)
#if defined(HAS_4G_MODEM) && defined(MECK_WIFI_COMPANION)
#define SETTINGS_MAX_ROWS 56  // Extra rows for IMEI, Carrier, APN, contacts, WiFi
#elif defined(HAS_4G_MODEM)
#define SETTINGS_MAX_ROWS 54  // Extra rows for IMEI, Carrier, APN + contacts
#elif defined(MECK_WIFI_COMPANION)
#define SETTINGS_MAX_ROWS 50  // Extra rows for contacts + WiFi
#else
#define SETTINGS_MAX_ROWS 48  // Contacts section
#endif
#define SETTINGS_TEXT_BUF  33  // 32 chars + null

class SettingsScreen : public UIScreen {
private:
  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;

  // Row table Ã¢â‚¬â€ rebuilt whenever channels change
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
  int _editPickerIdx;       // for preset picker / contact mode picker
  float _editFloat;         // for freq/BW editing
  int _editInt;             // for SF/CR/TX/UTC editing
  int _confirmAction;       // 0=none, 1=delete channel, 2=apply radio

  // Onboarding mode
  bool _onboarding;

  // Sub-screen navigation
  SubScreen _subScreen;
  int _savedTopCursor;  // cursor position to restore when leaving sub-screen

  // Dirty flag for radio params Ã¢â‚¬â€ prompt to apply
  bool _radioChanged;

  // 4G modem state (runtime cache of config)
  #ifdef HAS_4G_MODEM
  bool _modemEnabled;
  #endif

  #ifdef MECK_WIFI_COMPANION
  // WiFi setup sub-screen state
  enum WifiSetupPhase : uint8_t {
    WIFI_PHASE_IDLE,
    WIFI_PHASE_SCANNING,
    WIFI_PHASE_SELECT,      // W/S to pick SSID, Enter to select
    WIFI_PHASE_PASSWORD,    // Type password, Enter to connect
    WIFI_PHASE_CONNECTING,
  };
  WifiSetupPhase _wifiPhase;
  String _wifiSSIDs[10];
  int _wifiSSIDCount;
  int _wifiSSIDSelected;
  char _wifiPassBuf[64];
  int _wifiPassLen;
  unsigned long _wifiFormLastChar;  // For brief password reveal
#if defined(LilyGo_T5S3_EPaper_Pro)
  bool _wifiNeedsVKB;              // T5S3: signal UITask to open VKB for password
#endif
  #endif

  // ---------------------------------------------------------------------------
  // Contact mode helpers
  // ---------------------------------------------------------------------------

  // Determine current contact mode from prefs
  int getContactMode() const {
    if ((_prefs->manual_add_contacts & 1) == 0) {
      return CONTACT_MODE_AUTO_ALL;
    }
    // manual_add_contacts bit 0 is set — check if any type bits are enabled
    if ((_prefs->autoadd_config & AUTO_ADD_ALL_TYPES) != 0) {
      return CONTACT_MODE_CUSTOM;
    }
    return CONTACT_MODE_MANUAL;
  }

  // Get display label for a contact mode
  static const char* contactModeLabel(int mode) {
    switch (mode) {
      case CONTACT_MODE_AUTO_ALL: return "Auto All";
      case CONTACT_MODE_CUSTOM:   return "Custom";
      case CONTACT_MODE_MANUAL:   return "Manual Only";
      default:                    return "?";
    }
  }

  // Apply a contact mode selection from picker
  void applyContactMode(int mode) {
    switch (mode) {
      case CONTACT_MODE_AUTO_ALL:
        _prefs->manual_add_contacts &= ~1;  // clear bit 0 → auto all
        break;
      case CONTACT_MODE_CUSTOM:
        _prefs->manual_add_contacts |= 1;   // set bit 0 → selective
        // If no type bits are set, default to all types enabled
        if ((_prefs->autoadd_config & AUTO_ADD_ALL_TYPES) == 0) {
          _prefs->autoadd_config |= AUTO_ADD_ALL_TYPES;
        }
        break;
      case CONTACT_MODE_MANUAL:
        _prefs->manual_add_contacts |= 1;   // set bit 0 → selective
        _prefs->autoadd_config &= ~AUTO_ADD_ALL_TYPES;  // clear all type bits
        // Note: keeps AUTO_ADD_OVERWRITE_OLDEST bit unchanged
        break;
    }
    the_mesh.savePrefs();
    rebuildRows();  // show/hide sub-toggles
    Serial.printf("Settings: Contact mode = %s (manual=%d, autoadd=0x%02X)\n",
                  contactModeLabel(mode), _prefs->manual_add_contacts, _prefs->autoadd_config);
  }

  // ---------------------------------------------------------------------------
  // Row table management
  // ---------------------------------------------------------------------------

  void rebuildRows() {
    _numRows = 0;

    if (_subScreen == SUB_CONTACTS) {
      // --- Contacts sub-screen: only contact-related rows ---
      addRow(ROW_CONTACT_MODE);
      if (getContactMode() == CONTACT_MODE_CUSTOM) {
        addRow(ROW_AUTOADD_CHAT);
        addRow(ROW_AUTOADD_REPEATER);
        addRow(ROW_AUTOADD_ROOM);
        addRow(ROW_AUTOADD_SENSOR);
        addRow(ROW_AUTOADD_OVERWRITE);
      }
    } else if (_subScreen == SUB_CHANNELS) {
      // --- Channels sub-screen: only channel-related rows ---
      for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
          addRow(ROW_CHANNEL, i);
        } else {
          break;
        }
      }
      addRow(ROW_ADD_CHANNEL);
    } else {
      // --- Top-level settings list ---
      addRow(ROW_NAME);
      addRow(ROW_RADIO_PRESET);
      addRow(ROW_FREQ);
      addRow(ROW_BW);
      addRow(ROW_SF);
      addRow(ROW_CR);
      addRow(ROW_TX_POWER);
      addRow(ROW_UTC_OFFSET);
      addRow(ROW_MSG_NOTIFY);
      addRow(ROW_GPS_BAUD);
      addRow(ROW_PATH_HASH_SIZE);
      addRow(ROW_DARK_MODE);
#if defined(LilyGo_T5S3_EPaper_Pro)
      addRow(ROW_PORTRAIT_MODE);
#endif
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
      addRow(ROW_AUTO_LOCK);
#endif
      #ifdef MECK_WIFI_COMPANION
      addRow(ROW_WIFI_SETUP);
      addRow(ROW_WIFI_TOGGLE);
      #endif
      #ifdef HAS_4G_MODEM
      addRow(ROW_MODEM_TOGGLE);
      #endif

      // Folder rows for sub-screens
      addRow(ROW_CONTACTS_SUBMENU);
      addRow(ROW_CHANNELS_SUBMENU);

      // Info section (stays at top level)
      addRow(ROW_INFO_HEADER);
      addRow(ROW_PUB_KEY);
      addRow(ROW_FIRMWARE);

      #ifdef HAS_4G_MODEM
      addRow(ROW_IMEI);
      addRow(ROW_OPERATOR_INFO);
      addRow(ROW_APN);
      #endif
    }

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
    return t != ROW_CH_HEADER && t != ROW_INFO_HEADER && t != ROW_CONTACT_HEADER
    #ifdef HAS_4G_MODEM
      && t != ROW_IMEI && t != ROW_OPERATOR_INFO
    #endif
    ;  // ROW_CONTACTS_SUBMENU and ROW_CHANNELS_SUBMENU ARE selectable
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

    // SHA-256 the channel name Ã¢â€ â€™ first 16 bytes become the secret
    uint8_t hash[32];
    mesh::Utils::sha256(hash, 32, (const uint8_t*)chanName, strlen(chanName));
    memcpy(newCh.channel.secret, hash, 16);
    // Upper 16 bytes left as zero Ã¢â€ â€™ setChannel uses 128-bit mode

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
      _onboarding(false), _subScreen(SUB_NONE), _savedTopCursor(0),
      _radioChanged(false) {
    memset(_editBuf, 0, sizeof(_editBuf));
  }

  void enter() {
    _editMode = EDIT_NONE;
    _subScreen = SUB_NONE;
    _savedTopCursor = 0;
    _cursor = 0;
    _scrollTop = 0;
    _radioChanged = false;
    #ifdef HAS_4G_MODEM
    _modemEnabled = ModemManager::loadEnabledConfig();
    #endif
    #ifdef MECK_WIFI_COMPANION
    _wifiPhase = WIFI_PHASE_IDLE;
    _wifiSSIDCount = 0;
    _wifiSSIDSelected = 0;
    _wifiPassLen = 0;
    memset(_wifiPassBuf, 0, sizeof(_wifiPassBuf));
    _wifiFormLastChar = 0;
  #if defined(LilyGo_T5S3_EPaper_Pro)
    _wifiNeedsVKB = false;
  #endif
    #endif
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
  bool isOnChannelsSubScreen() const { return _subScreen == SUB_CHANNELS; }
  bool isOnDeletableChannel() const {
    return _subScreen == SUB_CHANNELS &&
           _cursor >= 0 && _cursor < _numRows &&
           _rows[_cursor].type == ROW_CHANNEL &&
           _rows[_cursor].param > 0;
  }

  // Tap-to-select: given a virtual Y coordinate, compute which row was tapped
  // and move cursor there. Returns: 0=miss, 1=moved to new row, 2=tapped current row.
  int selectRowAtVY(int vy) {
    if (_editMode != EDIT_NONE) return 0;  // Don't change cursor while editing
    const int headerH = 14, footerH = 14, lineH = 9;
    // T-Deck Pro render offsets fillRect by +5 (GxEPD baseline compensation),
    // so visual rows start 5 units below headerH. T5S3 renders at y directly.
#if defined(LilyGo_T5S3_EPaper_Pro)
    const int bodyTop = headerH;
#else
    const int bodyTop = headerH + 5;
#endif
    if (vy < bodyTop || vy >= 128 - footerH) return 0;  // Outside body area

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_cursor - maxVisible / 2, _numRows - maxVisible));

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _numRows) return 0;

    // Skip non-selectable rows (headers/separators)
    if (!isSelectable(tappedRow)) return 0;

    if (tappedRow == _cursor) return 2;  // Same row — activate
    _cursor = tappedRow;
    return 1;  // Moved to new row
  }

  // ---------------------------------------------------------------------------
  // WiFi scan helpers
  // ---------------------------------------------------------------------------

  #ifdef MECK_WIFI_COMPANION
  // Perform a blocking WiFi scan. Populates _wifiSSIDs/_wifiSSIDCount and
  // advances _wifiPhase to SELECT (even on zero results, so the overlay
  // stays visible and the user can rescan with 'r').
  void performWifiScan() {
    _wifiPhase = WIFI_PHASE_SCANNING;
    _wifiSSIDCount = 0;
    _wifiSSIDSelected = 0;

    // Disconnect any active WiFi connection first — the ESP32 driver
    // returns -2 (WIFI_SCAN_FAILED) if the radio is busy with an
    // existing connection or the TCP companion server socket.
    WiFi.disconnect(false);   // false = don't turn off WiFi radio
    delay(100);               // let the driver settle
    WiFi.mode(WIFI_STA);

    // 500ms per-channel dwell helps detect phone hotspots that are slow
    // to respond to probe requests (default 300ms often misses them).
    int n = WiFi.scanNetworks(false, false, false, 500);
    Serial.printf("Settings: WiFi scan found %d networks\n", n);

    if (n > 0) {
      _wifiSSIDCount = min(n, 10);
      for (int si = 0; si < _wifiSSIDCount; si++) {
        _wifiSSIDs[si] = WiFi.SSID(si);
        Serial.printf("  [%d] %s (RSSI %d)\n", si,
                      _wifiSSIDs[si].c_str(), WiFi.RSSI(si));
      }
    } else if (n < 0) {
      Serial.printf("Settings: WiFi scan error %d\n", n);
    }
    WiFi.scanDelete();
    _wifiPhase = WIFI_PHASE_SELECT;  // always show overlay (even if 0)
  }

  // After WiFi setup exits (connect success or user quit), try to
  // reconnect to saved credentials so the companion TCP server works.
  void wifiReconnectSaved() {
    File f = SD.open("/web/wifi.cfg", FILE_READ);
    if (f) {
      String ssid = f.readStringUntil('\n'); ssid.trim();
      String pass = f.readStringUntil('\n'); pass.trim();
      f.close();
      digitalWrite(SDCARD_CS, HIGH);
      if (ssid.length() > 0) {
        Serial.printf("Settings: Reconnecting to saved WiFi '%s'\n", ssid.c_str());
        WiFi.begin(ssid.c_str(), pass.c_str());
      }
    } else {
      digitalWrite(SDCARD_CS, HIGH);
    }
  }

#if defined(LilyGo_T5S3_EPaper_Pro)
  // T5S3 VKB integration — UITask polls this to open the virtual keyboard
  // when settings enters WiFi password phase (no physical keyboard available).
  bool needsWifiVKB() const { return _wifiNeedsVKB; }
  void clearWifiNeedsVKB() { _wifiNeedsVKB = false; }

  // Called by UITask::onVKBSubmit with the password text from VKB.
  // Fills the password buffer and triggers the WiFi connect sequence.
  void submitWifiPassword(const char* pass) {
    _wifiNeedsVKB = false;
    int len = strlen(pass);
    if (len > 63) len = 63;
    memcpy(_wifiPassBuf, pass, len);
    _wifiPassBuf[len] = '\0';
    _wifiPassLen = len;

    // Trigger the same connect sequence as pressing Enter in password phase
    _wifiPhase = WIFI_PHASE_CONNECTING;

    // Save credentials to SD (so web reader can reuse them)
    if (SD.exists("/web") || SD.mkdir("/web")) {
      File f = SD.open("/web/wifi.cfg", FILE_WRITE);
      if (f) {
        f.println(_wifiSSIDs[_wifiSSIDSelected]);
        f.println(_wifiPassBuf);
        f.close();
      }
      digitalWrite(SDCARD_CS, HIGH);
    }

    WiFi.disconnect(false);
    WiFi.begin(_wifiSSIDs[_wifiSSIDSelected].c_str(), _wifiPassBuf);

    // Brief blocking wait — fine for e-ink
    unsigned long timeout = millis() + 8000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("Settings VKB: WiFi connected to %s, IP: %s\n",
                    _wifiSSIDs[_wifiSSIDSelected].c_str(),
                    WiFi.localIP().toString().c_str());
      _editMode = EDIT_NONE;
      _wifiPhase = WIFI_PHASE_IDLE;
      if (_onboarding) _onboarding = false;
    } else {
      Serial.println("Settings VKB: WiFi connection failed");
      _wifiPhase = WIFI_PHASE_SELECT;  // Back to SSID list to retry
    }
  }
#endif

  #endif

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
    } else if (_subScreen == SUB_CONTACTS) {
      display.print("Settings > Contacts");
    } else if (_subScreen == SUB_CHANNELS) {
      display.print("Settings > Channels");
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
#if defined(LilyGo_T5S3_EPaper_Pro)
        // FreeSans12pt: baseline at (y+5)*scale_y, ascent ~17px above.
        // Highlight needs to start above the baseline to cover ascenders.
        display.fillRect(0, y, display.width(), lineHeight);
#else
        display.fillRect(0, y + 5, display.width(), lineHeight);
#endif
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
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "Freq: %s_ MHz", _editBuf);
          } else {
            snprintf(tmp, sizeof(tmp), "Freq: %.3f MHz", _prefs->freq);
          }
          display.print(tmp);
          break;

        case ROW_BW:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "BW: %.1f " EDIT_ADJ_HINT, _editFloat);
          } else {
            snprintf(tmp, sizeof(tmp), "BW: %.1f kHz", _prefs->bw);
          }
          display.print(tmp);
          break;

        case ROW_SF:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "SF: %d " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "SF: %d", _prefs->sf);
          }
          display.print(tmp);
          break;

        case ROW_CR:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "CR: %d " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "CR: %d", _prefs->cr);
          }
          display.print(tmp);
          break;

        case ROW_TX_POWER:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "TX: %d dBm " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "TX: %d dBm", _prefs->tx_power_dbm);
          }
          display.print(tmp);
          break;

        case ROW_UTC_OFFSET:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "UTC: %+d " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "UTC Offset: %+d", _prefs->utc_offset_hours);
          }
          display.print(tmp);
          break;

        case ROW_MSG_NOTIFY:
          snprintf(tmp, sizeof(tmp), "Msg Rcvd LED Light Pulse: %s",
                   _prefs->kb_flash_notify ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_PATH_HASH_SIZE:
          if (editing && _editMode == EDIT_NUMBER) {
            snprintf(tmp, sizeof(tmp), "Path Hash Size: %d-byte " EDIT_ADJ_HINT, _editInt);
          } else {
            snprintf(tmp, sizeof(tmp), "Path Hash Size: %d-byte", _prefs->path_hash_mode + 1);
          }
          display.print(tmp);
          break;

        case ROW_GPS_BAUD: {
          char baudStr[16];
          if (editing && _editMode == EDIT_PICKER) {
            snprintf(tmp, sizeof(tmp), "< GPS Baud: %s > *",
                     gpsBaudLabel(GPS_BAUD_OPTIONS[_editPickerIdx], baudStr, sizeof(baudStr)));
          } else {
            snprintf(tmp, sizeof(tmp), "GPS Baud: %s *",
                     gpsBaudLabel(_prefs->gps_baudrate, baudStr, sizeof(baudStr)));
          }
          display.print(tmp);
          break;
        }

        case ROW_DARK_MODE:
          snprintf(tmp, sizeof(tmp), "Dark Mode: %s",
                   _prefs->dark_mode ? "ON" : "OFF");
          display.print(tmp);
          break;

#if defined(LilyGo_T5S3_EPaper_Pro)
        case ROW_PORTRAIT_MODE:
          snprintf(tmp, sizeof(tmp), "Portrait Mode: %s",
                   _prefs->portrait_mode ? "ON" : "OFF");
          display.print(tmp);
          break;
#endif

#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
        case ROW_AUTO_LOCK:
          if (editing && _editMode == EDIT_PICKER) {
            snprintf(tmp, sizeof(tmp), "< Auto Lock: %s >",
                     autoLockLabel(AUTO_LOCK_OPTIONS[_editPickerIdx]));
          } else {
            snprintf(tmp, sizeof(tmp), "Auto Lock: %s",
                     autoLockLabel(_prefs->auto_lock_minutes));
          }
          display.print(tmp);
          break;
#endif

        #ifdef MECK_WIFI_COMPANION
        case ROW_WIFI_SETUP:
          if (WiFi.status() == WL_CONNECTED) {
            snprintf(tmp, sizeof(tmp), "WiFi: %s", WiFi.SSID().c_str());
          } else {
            strcpy(tmp, "WiFi: (not connected)");
          }
          display.print(tmp);
          break;
        case ROW_WIFI_TOGGLE:
          snprintf(tmp, sizeof(tmp), "WiFi Radio: %s",
                   (WiFi.getMode() != WIFI_OFF) ? "ON" : "OFF");
          display.print(tmp);
          break;
        #endif

        #ifdef HAS_4G_MODEM
        case ROW_MODEM_TOGGLE:
          snprintf(tmp, sizeof(tmp), "4G Modem: %s",
                   _modemEnabled ? "ON" : "OFF");
          display.print(tmp);
          break;

        //case ROW_RINGTONE:
        //  snprintf(tmp, sizeof(tmp), "Incoming Call Ring: %s",
        //           _prefs->ringtone_enabled ? "ON" : "OFF");
       //   display.print(tmp);
        //  break;
        #endif

        // --- Submenu folder rows ---
        case ROW_CONTACTS_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Contacts >>");
          break;

        case ROW_CHANNELS_SUBMENU:
          display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
          display.print("Channels >>");
          break;

        // --- Contacts section ---
        case ROW_CONTACT_HEADER:
          display.setColor(DisplayDriver::YELLOW);
          display.print("--- Contacts ---");
          break;

        case ROW_CONTACT_MODE:
          if (editing && _editMode == EDIT_PICKER) {
            snprintf(tmp, sizeof(tmp), "< Add Mode: %s >",
                     contactModeLabel(_editPickerIdx));
          } else {
            snprintf(tmp, sizeof(tmp), "Add Mode: %s",
                     contactModeLabel(getContactMode()));
          }
          display.print(tmp);
          break;

        case ROW_AUTOADD_CHAT:
          snprintf(tmp, sizeof(tmp), "  Chat: %s",
                   (_prefs->autoadd_config & AUTO_ADD_CHAT) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_REPEATER:
          snprintf(tmp, sizeof(tmp), "  Repeater: %s",
                   (_prefs->autoadd_config & AUTO_ADD_REPEATER) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_ROOM:
          snprintf(tmp, sizeof(tmp), "  Room Server: %s",
                   (_prefs->autoadd_config & AUTO_ADD_ROOM_SERVER) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_SENSOR:
          snprintf(tmp, sizeof(tmp), "  Sensor: %s",
                   (_prefs->autoadd_config & AUTO_ADD_SENSOR) ? "ON" : "OFF");
          display.print(tmp);
          break;

        case ROW_AUTOADD_OVERWRITE:
          snprintf(tmp, sizeof(tmp), "  Overwrite Oldest: %s",
                   (_prefs->autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) ? "ON" : "OFF");
          display.print(tmp);
          break;

        // --- Channels section ---
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
              #if defined(LilyGo_T5S3_EPaper_Pro)
                const char* hint = "Hold:Del";
              #else
                const char* hint = "X:Del";
              #endif
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
          snprintf(tmp, sizeof(tmp), "Node ID: %s", hexBuf);
          display.print(tmp);
          break;
        }

        case ROW_FIRMWARE:
          snprintf(tmp, sizeof(tmp), "FW: %s", FIRMWARE_VERSION);
          display.print(tmp);
          break;

        #ifdef HAS_4G_MODEM
        case ROW_IMEI: {
          const char* imei = modemManager.getIMEI();
          snprintf(tmp, sizeof(tmp), "IMEI: %s", imei[0] ? imei : "(unavailable)");
          display.print(tmp);
          break;
        }

        case ROW_OPERATOR_INFO: {
          const char* op = modemManager.getOperator();
          int bars = modemManager.getSignalBars();
          if (op[0]) {
            // Show carrier name with signal bar count
            snprintf(tmp, sizeof(tmp), "Carrier: %s (%d/5)", op, bars);
          } else {
            snprintf(tmp, sizeof(tmp), "Carrier: (searching)");
          }
          display.print(tmp);
          break;
        }

        case ROW_APN: {
          if (editing && _editMode == EDIT_TEXT) {
            snprintf(tmp, sizeof(tmp), "APN: %s_", _editBuf);
          } else {
            const char* apn = modemManager.getAPN();
            const char* src = modemManager.getAPNSource();
            if (apn[0]) {
              // Truncate APN to fit: "APN: " (5) + apn (max 28) + " [x]" (4) = ~37 chars
              char apnShort[29];
              strncpy(apnShort, apn, 28);
              apnShort[28] = '\0';
              // Abbreviate source: auto→A, network→N, user→U, none→?
              char srcChar = '?';
              if (strcmp(src, "auto") == 0) srcChar = 'A';
              else if (strcmp(src, "network") == 0) srcChar = 'N';
              else if (strcmp(src, "user") == 0) srcChar = 'U';
              snprintf(tmp, sizeof(tmp), "APN: %s [%c]", apnShort, srcChar);
            } else {
              snprintf(tmp, sizeof(tmp), "APN: (none)");
            }
          }
          display.print(tmp);
          break;
        }
        #endif
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
    #if defined(LilyGo_T5S3_EPaper_Pro)
      display.drawTextCentered(display.width() / 2, by + bh - 14, "Tap:Yes  Boot:No");
    #else
      display.drawTextCentered(display.width() / 2, by + bh - 14, "Enter:Yes  Q:No");
    #endif
      display.setTextSize(1);
    }

    #ifdef MECK_WIFI_COMPANION
    // === WiFi setup overlay ===
    if (_editMode == EDIT_WIFI) {
      int bx = 2, by = 14, bw = display.width() - 4;
      int bh = display.height() - 28;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);

      display.setTextSize(0);
      int wy = by + 4;

      if (_wifiPhase == WIFI_PHASE_SCANNING) {
        display.drawTextCentered(display.width() / 2, wy, "Scanning for networks...");

      } else if (_wifiPhase == WIFI_PHASE_SELECT) {
        if (_wifiSSIDCount == 0) {
          // No networks found — show message with rescan prompt
          display.setCursor(bx + 4, wy);
          display.print("No networks found.");
          wy += 12;
          display.setCursor(bx + 4, wy);
          display.print("Check your hotspot is on");
          wy += 8;
          display.setCursor(bx + 4, wy);
          display.print("and set to 2.4GHz.");
          wy += 12;
          display.setCursor(bx + 4, wy);
          display.print("Press R or Enter to rescan.");
        } else {
        display.setCursor(bx + 4, wy);
        display.print("Select network:");
        wy += 10;
        for (int wi = 0; wi < _wifiSSIDCount && wy < by + bh - 16; wi++) {
          bool sel = (wi == _wifiSSIDSelected);
          if (sel) {
            display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
            display.fillRect(bx + 2, wy, bw - 4, 8);
#else
            display.fillRect(bx + 2, wy + 5, bw - 4, 8);
#endif
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(bx + 4, wy);
          char ssidLine[40];
          if (sel) {
            snprintf(ssidLine, sizeof(ssidLine), "> %.33s", _wifiSSIDs[wi].c_str());
          } else {
            snprintf(ssidLine, sizeof(ssidLine), "  %.33s", _wifiSSIDs[wi].c_str());
          }
          display.print(ssidLine);
          wy += 8;
        }
        }

      } else if (_wifiPhase == WIFI_PHASE_PASSWORD) {
        display.setCursor(bx + 4, wy);
        snprintf(tmp, sizeof(tmp), "SSID: %s", _wifiSSIDs[_wifiSSIDSelected].c_str());
        display.print(tmp);
        wy += 12;
        display.setCursor(bx + 4, wy);
        display.print("Password:");
        wy += 10;
        display.setCursor(bx + 4, wy);
        // Masked password with brief reveal of last char
        char passBuf[66];
        for (int pi = 0; pi < _wifiPassLen; pi++) passBuf[pi] = '*';
        if (_wifiPassLen > 0 && _wifiFormLastChar > 0 &&
            (millis() - _wifiFormLastChar) < 800) {
          passBuf[_wifiPassLen - 1] = _wifiPassBuf[_wifiPassLen - 1];
        }
        passBuf[_wifiPassLen] = '_';
        passBuf[_wifiPassLen + 1] = '\0';
        display.print(passBuf);

      } else if (_wifiPhase == WIFI_PHASE_CONNECTING) {
        display.drawTextCentered(display.width() / 2, wy + 10, "Connecting...");
      }
      display.setTextSize(1);
    }
    #endif

    // === Footer ===
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

#if defined(LilyGo_T5S3_EPaper_Pro)
    if (_editMode == EDIT_NONE) {
      if (_subScreen != SUB_NONE) {
        display.print("Boot:Back");
        const char* r = (_subScreen == SUB_CHANNELS) ? "Tap:Select  Hold:Del" : "Tap:Toggle  Hold:Edit";
        display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
        display.print(r);
      } else {
        display.print("Swipe:Scroll");
        const char* r = "Tap:Toggle  Hold:Edit";
        display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
        display.print(r);
      }
    } else if (_editMode == EDIT_NUMBER) {
      display.print("Swipe:Adjust");
      const char* r = "Tap:OK  Boot:Cancel";
      display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
      display.print(r);
    } else if (_editMode == EDIT_PICKER) {
      display.print("Swipe:Choose");
      const char* r = "Tap:OK  Boot:Cancel";
      display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
      display.print(r);
    } else if (_editMode == EDIT_CONFIRM) {
      display.print("Boot:Cancel");
      const char* r = "Tap:Confirm";
      display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
      display.print(r);
    #ifdef MECK_WIFI_COMPANION
    } else if (_editMode == EDIT_WIFI) {
      if (_wifiPhase == WIFI_PHASE_SELECT) {
        display.print("Swipe:Pick");
        const char* r = "Tap:Select  Boot:Back";
        display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
        display.print(r);
      } else {
        display.print("Please wait...");
      }
    #endif
    } else if (_editMode == EDIT_TEXT) {
      display.print("Hold:Type");
      const char* r = "Tap:OK  Boot:Cancel";
      display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
      display.print(r);
    } else {
      display.print("Editing...");
    }
#else
    if (_editMode == EDIT_TEXT) {
      display.print("Type, Enter:Ok Q:Cancel");
    #ifdef MECK_WIFI_COMPANION
    } else if (_editMode == EDIT_WIFI) {
      if (_wifiPhase == WIFI_PHASE_SELECT) {
        if (_wifiSSIDCount == 0) {
          display.print("R/Enter:Rescan Q:Back");
        } else {
          display.print("W/S:Pick Enter:Sel R:Rescan");
        }
      } else if (_wifiPhase == WIFI_PHASE_PASSWORD) {
        display.print("Type, Enter:Connect Q:Bck");
      } else {
        display.print("Please wait...");
      }
    #endif
    } else if (_editMode == EDIT_PICKER) {
      display.print("A/D:Choose Enter:Ok");
    } else if (_editMode == EDIT_NUMBER) {
      display.print("W/S:Adj Enter:Ok Q:Cancel");
    } else if (_editMode == EDIT_CONFIRM) {
      // Footer already covered by overlay
    } else {
      if (_subScreen != SUB_NONE) {
        display.print("Q:Back");
      } else {
        display.print("Q:Bk");
      }
      const char* r = "Tap/Ent:Edit";
      display.setCursor(display.width() - display.getTextWidth(r) - 2, footerY);
      display.print(r);
    }
#endif

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

    #ifdef MECK_WIFI_COMPANION
    // --- WiFi setup flow ---
    if (_editMode == EDIT_WIFI) {
      if (_wifiPhase == WIFI_PHASE_SELECT) {
        if (c == 'w' || c == 'W') {
          if (_wifiSSIDSelected > 0) _wifiSSIDSelected--;
          return true;
        }
        if (c == 's' || c == 'S') {
          if (_wifiSSIDSelected < _wifiSSIDCount - 1) _wifiSSIDSelected++;
          return true;
        }
        if (c == 'r' || c == 'R') {
          // Rescan — lets user toggle hotspot on then retry
          performWifiScan();
          return true;
        }
        if (c == '\r' || c == 13) {
          if (_wifiSSIDCount == 0) {
            // No networks — Enter rescans (same as R)
            performWifiScan();
            return true;
          }
          // Selected an SSID — move to password entry
          _wifiPhase = WIFI_PHASE_PASSWORD;
          _wifiPassLen = 0;
          memset(_wifiPassBuf, 0, sizeof(_wifiPassBuf));
          _wifiFormLastChar = 0;
#if defined(LilyGo_T5S3_EPaper_Pro)
          _wifiNeedsVKB = true;  // Signal UITask to open virtual keyboard
#endif
          return true;
        }
        if (c == 'q' || c == 'Q') {
          _editMode = EDIT_NONE;
          _wifiPhase = WIFI_PHASE_IDLE;
          if (_onboarding) _onboarding = false;  // Skip WiFi, finish onboarding
          wifiReconnectSaved();  // Restore connection after scan disconnect
          return true;
        }
        return true;
      }

      if (_wifiPhase == WIFI_PHASE_PASSWORD) {
        if (c == '\r' || c == 13) {
          // Attempt connection
          _wifiPassBuf[_wifiPassLen] = '\0';
          _wifiPhase = WIFI_PHASE_CONNECTING;

          // Save credentials to SD first (so web reader can reuse them)
          if (SD.exists("/web") || SD.mkdir("/web")) {
            File f = SD.open("/web/wifi.cfg", FILE_WRITE);
            if (f) {
              f.println(_wifiSSIDs[_wifiSSIDSelected]);
              f.println(_wifiPassBuf);
              f.close();
            }
            digitalWrite(SDCARD_CS, HIGH);
          }

          WiFi.disconnect(false);
          WiFi.begin(_wifiSSIDs[_wifiSSIDSelected].c_str(), _wifiPassBuf);

          // Brief blocking wait — fine for e-ink (screen won't update during this anyway)
          unsigned long timeout = millis() + 8000;
          while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
            delay(100);
          }

          if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("Settings: WiFi connected to %s, IP: %s\n",
                          _wifiSSIDs[_wifiSSIDSelected].c_str(),
                          WiFi.localIP().toString().c_str());
            _editMode = EDIT_NONE;
            _wifiPhase = WIFI_PHASE_IDLE;
            if (_onboarding) _onboarding = false;  // Finish onboarding
          } else {
            Serial.println("Settings: WiFi connection failed");
            // Go back to SSID selection so user can retry
            _wifiPhase = WIFI_PHASE_SELECT;
          }
          return true;
        }
        if (c == 'q' || c == 'Q') {
          // Back to SSID selection
          _wifiPhase = WIFI_PHASE_SELECT;
          return true;
        }
        if (c == '\b') {
          if (_wifiPassLen > 0) {
            _wifiPassLen--;
            _wifiPassBuf[_wifiPassLen] = '\0';
          }
          return true;
        }
        // Printable character
        if (c >= 32 && c < 127 && _wifiPassLen < 63) {
          _wifiPassBuf[_wifiPassLen++] = c;
          _wifiPassBuf[_wifiPassLen] = '\0';
          _wifiFormLastChar = millis();
          return true;
        }
        return true;
      }

      // Scanning and connecting phases consume all keys
      return true;
    }
    #endif

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
        } else if (type == ROW_FREQ) {
          if (_editPos > 0) {
            float f = strtof(_editBuf, nullptr);
            f = constrain(f, 400.0f, 2500.0f);
            _prefs->freq = f;
            _radioChanged = true;
            Serial.printf("Settings: Freq typed to %.3f\n", f);
          }
          _editMode = EDIT_NONE;
        } else if (type == ROW_ADD_CHANNEL) {
          if (_editPos > 0) {
            createHashtagChannel(_editBuf);
            rebuildRows();
          }
          _editMode = EDIT_NONE;
        }
        #ifdef HAS_4G_MODEM
        else if (type == ROW_APN) {
          // Save the edited APN (even if empty — clears user override)
          if (_editPos > 0) {
            modemManager.setAPN(_editBuf);
            Serial.printf("Settings: APN set to '%s'\n", _editBuf);
          } else {
            // Empty APN: remove user override, revert to auto-detection
            ModemManager::saveAPNConfig("");
            Serial.println("Settings: APN cleared (will auto-detect on next boot)");
          }
          _editMode = EDIT_NONE;
        }
        #endif
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

    // --- Picker mode (radio preset or contact mode) ---
    if (_editMode == EDIT_PICKER) {
      SettingsRowType type = _rows[_cursor].type;

      if (c == 'a' || c == 'A') {
        if (type == ROW_CONTACT_MODE) {
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = CONTACT_MODE_COUNT - 1;
        } else if (type == ROW_GPS_BAUD) {
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = GPS_BAUD_OPTION_COUNT - 1;
        } else if (type == ROW_AUTO_LOCK) {
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = AUTO_LOCK_OPTION_COUNT - 1;
        } else {
          // Radio preset
          _editPickerIdx--;
          if (_editPickerIdx < 0) _editPickerIdx = (int)NUM_RADIO_PRESETS - 1;
        }
        return true;
      }
      if (c == 'd' || c == 'D') {
        if (type == ROW_CONTACT_MODE) {
          _editPickerIdx++;
          if (_editPickerIdx >= CONTACT_MODE_COUNT) _editPickerIdx = 0;
        } else if (type == ROW_GPS_BAUD) {
          _editPickerIdx++;
          if (_editPickerIdx >= GPS_BAUD_OPTION_COUNT) _editPickerIdx = 0;
        } else if (type == ROW_AUTO_LOCK) {
          _editPickerIdx++;
          if (_editPickerIdx >= AUTO_LOCK_OPTION_COUNT) _editPickerIdx = 0;
        } else {
          // Radio preset
          _editPickerIdx++;
          if (_editPickerIdx >= (int)NUM_RADIO_PRESETS) _editPickerIdx = 0;
        }
        return true;
      }
      if (c == '\r' || c == 13) {
        if (type == ROW_CONTACT_MODE) {
          applyContactMode(_editPickerIdx);
          _editMode = EDIT_NONE;
        } else if (type == ROW_GPS_BAUD) {
          _prefs->gps_baudrate = GPS_BAUD_OPTIONS[_editPickerIdx];
          the_mesh.savePrefs();
          _editMode = EDIT_NONE;
          Serial.printf("Settings: GPS baud set to %lu (reboot to apply)\n",
                        (unsigned long)_prefs->gps_baudrate);
        } else if (type == ROW_AUTO_LOCK) {
          _prefs->auto_lock_minutes = AUTO_LOCK_OPTIONS[_editPickerIdx];
          the_mesh.savePrefs();
          _editMode = EDIT_NONE;
          Serial.printf("Settings: Auto lock = %s\n",
                        autoLockLabel(_prefs->auto_lock_minutes));
        } else {
          // Apply radio preset
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
            applyRadioParams();
          #ifdef MECK_WIFI_COMPANION
            // Move to WiFi setup before finishing onboarding
            for (int r = 0; r < _numRows; r++) {
              if (_rows[r].type == ROW_WIFI_SETUP) {
                _cursor = r;
                break;
              }
            }
            // Auto-launch the WiFi scan
            _editMode = EDIT_WIFI;
            performWifiScan();
          #else
            _onboarding = false;
          #endif
          }
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
          case ROW_PATH_HASH_SIZE: if (_editInt < 3) _editInt++; break;
          default: break;
        }
        return true;
      }
      if (c == 's' || c == 'S') {
        switch (type) {
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
          case ROW_PATH_HASH_SIZE: if (_editInt > 1) _editInt--; break;
          default: break;
        }
        return true;
      }
      if (c == '\r' || c == 13) {
        // Confirm number edit
        switch (type) {
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
          case ROW_PATH_HASH_SIZE:
            _prefs->path_hash_mode = (uint8_t)constrain(_editInt - 1, 0, 2);  // display 1-3, store 0-2
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
        case ROW_FREQ: {
          // Use text input so user can type exact frequencies like 916.575
          char freqStr[16];
          snprintf(freqStr, sizeof(freqStr), "%.3f", _prefs->freq);
          startEditText(freqStr);
          break;
        }
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
        case ROW_MSG_NOTIFY:
          _prefs->kb_flash_notify = _prefs->kb_flash_notify ? 0 : 1;
          the_mesh.savePrefs();
          Serial.printf("Settings: Msg flash notify = %s\n",
                        _prefs->kb_flash_notify ? "ON" : "OFF");
          break;
        case ROW_PATH_HASH_SIZE:
          startEditInt(_prefs->path_hash_mode + 1);  // display as 1-3
          break;
        case ROW_GPS_BAUD:
          startEditPicker(findGpsBaudIndex(_prefs->gps_baudrate));
          break;
        case ROW_DARK_MODE:
          _prefs->dark_mode = _prefs->dark_mode ? 0 : 1;
          the_mesh.savePrefs();
          Serial.printf("Settings: Dark mode = %s\n",
                        _prefs->dark_mode ? "ON" : "OFF");
          break;
#if defined(LilyGo_T5S3_EPaper_Pro)
        case ROW_PORTRAIT_MODE:
          _prefs->portrait_mode = _prefs->portrait_mode ? 0 : 1;
          the_mesh.savePrefs();
          Serial.printf("Settings: Portrait mode = %s\n",
                        _prefs->portrait_mode ? "ON" : "OFF");
          break;
#endif
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
        case ROW_AUTO_LOCK:
          startEditPicker(findAutoLockIndex(_prefs->auto_lock_minutes));
          break;
#endif
        #ifdef MECK_WIFI_COMPANION
        case ROW_WIFI_SETUP: {
          // Launch WiFi scan → select → password → connect flow
          _editMode = EDIT_WIFI;
          performWifiScan();
          break;
        }
        case ROW_WIFI_TOGGLE:
          if (WiFi.getMode() != WIFI_OFF) {
            // Turn WiFi OFF
            WiFi.disconnect(true);
            WiFi.mode(WIFI_OFF);
            Serial.println("Settings: WiFi radio OFF");
          } else {
            // Turn WiFi ON — reconnect using saved credentials
            WiFi.mode(WIFI_STA);
            if (SD.exists("/web/wifi.cfg")) {
              File f = SD.open("/web/wifi.cfg", FILE_READ);
              if (f) {
                String ssid = f.readStringUntil('\n'); ssid.trim();
                String pass = f.readStringUntil('\n'); pass.trim();
                f.close();
                digitalWrite(SDCARD_CS, HIGH);
                if (ssid.length() > 0) {
                  WiFi.begin(ssid.c_str(), pass.c_str());
                  unsigned long timeout = millis() + 8000;
                  while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
                    delay(100);
                  }
                  if (WiFi.status() == WL_CONNECTED) {
                    Serial.printf("Settings: WiFi ON, connected to %s\n", ssid.c_str());
                  } else {
                    Serial.println("Settings: WiFi ON, but connection failed");
                  }
                }
              } else {
                digitalWrite(SDCARD_CS, HIGH);
              }
            }
            Serial.println("Settings: WiFi radio ON");
          }
          break;
        #endif
        #ifdef HAS_4G_MODEM
        case ROW_MODEM_TOGGLE:
          _modemEnabled = !_modemEnabled;
          ModemManager::saveEnabledConfig(_modemEnabled);
          if (_modemEnabled) {
            modemManager.begin();
            Serial.println("Settings: 4G modem ENABLED (started)");
          } else {
            modemManager.shutdown();
            Serial.println("Settings: 4G modem DISABLED (shutdown)");
          }
          break;
       // case ROW_RINGTONE:
       //   _prefs->ringtone_enabled = _prefs->ringtone_enabled ? 0 : 1;
       //   modemManager.setRingtoneEnabled(_prefs->ringtone_enabled);
        //  the_mesh.savePrefs();
       //   Serial.printf("Settings: Ringtone = %s\n",
      //                  _prefs->ringtone_enabled ? "ON" : "OFF");
       //   break;
        case ROW_APN: {
          // Start text editing with current APN as initial value
          const char* currentApn = modemManager.getAPN();
          startEditText(currentApn);
          break;
        }
        #endif

        // --- Contact mode picker ---
        case ROW_CONTACT_MODE:
          startEditPicker(getContactMode());
          break;

        // --- Contact sub-toggles (flip bit and save) ---
        case ROW_AUTOADD_CHAT:
          _prefs->autoadd_config ^= AUTO_ADD_CHAT;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Chat = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_CHAT) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_REPEATER:
          _prefs->autoadd_config ^= AUTO_ADD_REPEATER;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Repeater = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_REPEATER) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_ROOM:
          _prefs->autoadd_config ^= AUTO_ADD_ROOM_SERVER;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Room = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_ROOM_SERVER) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_SENSOR:
          _prefs->autoadd_config ^= AUTO_ADD_SENSOR;
          the_mesh.savePrefs();
          Serial.printf("Settings: Auto-add Sensor = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_SENSOR) ? "ON" : "OFF");
          break;
        case ROW_AUTOADD_OVERWRITE:
          _prefs->autoadd_config ^= AUTO_ADD_OVERWRITE_OLDEST;
          the_mesh.savePrefs();
          Serial.printf("Settings: Overwrite oldest = %s\n",
                        (_prefs->autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) ? "ON" : "OFF");
          break;

        // --- Submenu folder rows ---
        case ROW_CONTACTS_SUBMENU:
          _savedTopCursor = _cursor;
          _subScreen = SUB_CONTACTS;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Contacts sub-screen");
          break;
        case ROW_CHANNELS_SUBMENU:
          _savedTopCursor = _cursor;
          _subScreen = SUB_CHANNELS;
          _cursor = 0;
          _scrollTop = 0;
          rebuildRows();
          Serial.println("Settings: entered Channels sub-screen");
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

    // Q: back -- if in sub-screen, return to top level; else exit settings
    if (c == 'q' || c == 'Q') {
      if (_subScreen != SUB_NONE) {
        // Return to top-level settings list
        _subScreen = SUB_NONE;
        rebuildRows();
        _cursor = _savedTopCursor;
        if (_cursor >= _numRows) _cursor = _numRows - 1;
        skipNonSelectable(1);
        Serial.println("Settings: back to top level");
        return true;
      }
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