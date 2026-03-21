#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>

// Forward declarations
#include "../AbstractUITask.h"
class MyMesh;
extern MyMesh the_mesh;

#define ADMIN_PASSWORD_MAX    32
#define ADMIN_PARAM_MAX       64   // Max length for set-command parameter input
#define ADMIN_RESPONSE_MAX    1024 // CLI responses can be multi-line / large neighbor lists
#define ADMIN_TIMEOUT_MS      15000 // 15s timeout for login/commands
#define KEY_ADMIN_EXIT        0xFE  // Special key: Shift+Backspace exit (injected by main.cpp)

// --- Command definition flags ---
#define CMDF_PARAM            0x01  // Command needs user parameter input appended
#define CMDF_CONFIRM          0x02  // Needs y/n confirmation before send
#define CMDF_EXPECT_TIMEOUT   0x04  // Timeout is expected success (reboot/OTA)
#define CMDF_PARSE_NEIGHBORS  0x08  // Response should be parsed as neighbor list

// --- Single command definition ---
struct AdminCmdDef {
  const char* label;       // Menu display label
  const char* cmd;         // CLI command string (full, or prefix if CMDF_PARAM)
  const char* paramHint;   // Prompt for parameter input (NULL if not needed)
  uint8_t flags;
};

// =====================================================================
// Command tables per category
// =====================================================================

// --- Clock & Adverts ---
static const AdminCmdDef CMD_OPERATIONAL[] = {
  { "Clock Sync",    "clock sync",   nullptr,          0 },
  { "Send Advert",   "advert",       nullptr,          0 },
  { "Get Clock",     "clock",        nullptr,          0 },
};
#define CMD_OPERATIONAL_COUNT 3

// --- Neighbors ---
static const AdminCmdDef CMD_NEIGHBORS[] = {
  { "View Neighbors",  "neighbors",        nullptr,               CMDF_PARSE_NEIGHBORS },
  { "Remove Neighbor", "neighbor.remove ",  "Pubkey hex prefix:",  CMDF_PARAM },
};
#define CMD_NEIGHBORS_COUNT 2

// --- Info ---
static const AdminCmdDef CMD_INFO[] = {
  { "Version",    "ver",    nullptr, 0 },
  { "Board",      "board",  nullptr, 0 },
};
#define CMD_INFO_COUNT 2

// --- Get Config (read-only queries) ---
static const AdminCmdDef CMD_GET_CONFIG[] = {
  { "Get Name",             "get name",                  nullptr, 0 },
  { "Get TX Power",         "get tx",                    nullptr, 0 },
  { "Get AF",               "get af",                    nullptr, 0 },
  { "Get Repeat",           "get repeat",                nullptr, 0 },
  { "Get Radio",            "get radio",                 nullptr, 0 },
  { "Get Flood Max",        "get flood.max",             nullptr, 0 },
  { "Get RX Delay",         "get rxdelay",               nullptr, 0 },
  { "Get TX Delay",         "get txdelay",               nullptr, 0 },
  { "Get Direct TX Delay",  "get direct.txdelay",        nullptr, 0 },
  { "Get Int Thresh",       "get int.thresh",            nullptr, 0 },
  { "Get AGC Reset Int",    "get agc.reset.interval",    nullptr, 0 },
  { "Get Multi Acks",       "get multi.acks",            nullptr, 0 },
  { "Get Advert Int",       "get advert.interval",       nullptr, 0 },
  { "Get Flood Adv Int",    "get flood.advert.interval", nullptr, 0 },
  { "Get Guest Password",   "get guest.password",        nullptr, 0 },
  { "Get Allow R/O",        "get allow.read.only",       nullptr, 0 },
  { "Get ADC Multiplier",   "get adc.multiplier",        nullptr, 0 },
};
#define CMD_GET_CONFIG_COUNT 17

// --- Set Config (write commands, all need parameter input) ---
static const AdminCmdDef CMD_SET_CONFIG[] = {
  { "Set Name",             "set name ",                  "Name:",                 CMDF_PARAM },
  { "Set TX Power",         "set tx ",                    "TX power (dBm):",       CMDF_PARAM },
  { "Set AF",               "set af ",                    "Airtime factor:",       CMDF_PARAM },
  { "Set Repeat",           "set repeat ",                "on/off:",               CMDF_PARAM },
  { "Set Flood Max",        "set flood.max ",             "Max hops (0-64):",      CMDF_PARAM },
  { "Set RX Delay",         "set rxdelay ",               "Base (0=off):",         CMDF_PARAM },
  { "Set TX Delay",         "set txdelay ",               "Factor:",               CMDF_PARAM },
  { "Set Direct TX Delay",  "set direct.txdelay ",        "Factor:",               CMDF_PARAM },
  { "Set Int Thresh",       "set int.thresh ",            "dB (0=off, def 14):",   CMDF_PARAM },
  { "Set AGC Reset Int",    "set agc.reset.interval ",    "Seconds (0=off):",      CMDF_PARAM },
  { "Set Multi Acks",       "set multi.acks ",            "0 or 1:",               CMDF_PARAM },
  { "Set Advert Interval",  "set advert.interval ",       "Minutes (0=off):",      CMDF_PARAM },
  { "Set Flood Adv Int",    "set flood.advert.interval ", "Hours (0=off, 3-48):",  CMDF_PARAM },
  { "Set Guest Password",   "set guest.password ",        "Password:",             CMDF_PARAM },
  { "Set Allow R/O",        "set allow.read.only ",       "on/off:",               CMDF_PARAM },
  { "Set ADC Multiplier",   "set adc.multiplier ",        "Factor (0=default):",   CMDF_PARAM },
  { "Set Radio",            "set radio ",                 "freq,bw,sf,cr:",        CMDF_PARAM },
  { "Temp Radio",           "tempradio ",                 "freq,bw,sf,cr,mins:",   CMDF_PARAM },
  { "Change Admin Pwd",     "password ",                  "New password:",          CMDF_PARAM | CMDF_CONFIRM },
};
#define CMD_SET_CONFIG_COUNT 19

// --- Power ---
static const AdminCmdDef CMD_POWER[] = {
  { "Powersaving Status", "powersaving",     nullptr, 0 },
  { "Powersaving On",     "powersaving on",  nullptr, 0 },
  { "Powersaving Off",    "powersaving off", nullptr, 0 },
};
#define CMD_POWER_COUNT 3

// --- System ---
static const AdminCmdDef CMD_SYSTEM[] = {
  { "Reboot",       "reboot",     nullptr, CMDF_CONFIRM | CMDF_EXPECT_TIMEOUT },
  { "Start OTA",    "start ota",  nullptr, CMDF_CONFIRM | CMDF_EXPECT_TIMEOUT },
};
#define CMD_SYSTEM_COUNT 2

// =====================================================================
// Category table
// =====================================================================
enum AdminCategory {
  CAT_OPERATIONAL = 0,
  CAT_NEIGHBORS,
  CAT_GET_CONFIG,
  CAT_SET_CONFIG,
  CAT_POWER,
  CAT_REBOOT_OTA,
  CAT_FW_INFO,
  CAT_COUNT
};

struct AdminCategoryDef {
  const char* label;
  const AdminCmdDef* cmds;
  int count;
};

static const AdminCategoryDef CATEGORIES[CAT_COUNT] = {
  { "Clock & Adverts",      CMD_OPERATIONAL, CMD_OPERATIONAL_COUNT },
  { "Neighbors",            CMD_NEIGHBORS,   CMD_NEIGHBORS_COUNT },
  { "Get Config",           CMD_GET_CONFIG,  CMD_GET_CONFIG_COUNT },
  { "Set Config",           CMD_SET_CONFIG,  CMD_SET_CONFIG_COUNT },
  { "Powersaving",          CMD_POWER,       CMD_POWER_COUNT },
  { "Reboot & Start OTA",  CMD_SYSTEM,      CMD_SYSTEM_COUNT },
  { "Firmware & Device Info", CMD_INFO,      CMD_INFO_COUNT },
};

// =====================================================================
// Screen class
// =====================================================================

class RepeaterAdminScreen : public UIScreen {
public:
  enum AdminState {
    STATE_PASSWORD_ENTRY,   // Typing admin password
    STATE_LOGGING_IN,       // Waiting for login response
    STATE_CATEGORY_MENU,    // Top-level category selection
    STATE_COMMAND_MENU,     // Commands within selected category
    STATE_PARAM_ENTRY,      // Typing parameter for a set command
    STATE_CONFIRM,          // Confirmation prompt for destructive commands
    STATE_COMMAND_PENDING,  // Waiting for CLI response
    STATE_RESPONSE_VIEW,    // Displaying CLI response
    STATE_ERROR             // Error state (timeout, send fail)
  };

private:
  AbstractUITask* _task;
  mesh::RTCClock* _rtc;

  AdminState _state;
  int _contactIdx;              // Contact table index of the repeater
  char _repeaterName[32];       // Cached repeater name
  uint8_t _permissions;         // Login permissions (0=guest, 3=admin)
  uint32_t _serverTime;         // Server timestamp from login response

  // Password entry
  char _password[ADMIN_PASSWORD_MAX];
  int _pwdLen;
  unsigned long _lastCharAt;    // millis() when last char typed (for brief reveal)

  // Menu navigation
  int _catSel;                  // Selected category index
  int _cmdSel;                  // Selected command index within category
  int _scrollOffset;            // Scroll offset for long command lists

  // Parameter entry for set commands
  char _paramBuf[ADMIN_PARAM_MAX];
  int _paramLen;
  const AdminCmdDef* _pendingCmd;  // The command awaiting parameter / confirmation

  // Response buffer
  char _response[ADMIN_RESPONSE_MAX];
  int _responseLen;
  int _responseScroll;
  int _responseTotalLines;

  // Timing
  unsigned long _cmdSentAt;
  unsigned long _loginTimeoutMs;   // computed timeout for login (ms), falls back to ADMIN_TIMEOUT_MS
  bool _waitingForLogin;

  // Password cache
  static const int PWD_CACHE_SIZE = 8;
  struct PwdCacheEntry {
    int contactIdx;
    char password[ADMIN_PASSWORD_MAX];
  };
  PwdCacheEntry _pwdCache[PWD_CACHE_SIZE];
  int _pwdCacheCount;

  // Remote telemetry (from LPP response)
  float _telemVoltage;          // Battery voltage in volts
  float _telemTempC;            // Temperature in celsius
  bool _telemHasVoltage;
  bool _telemHasTemp;
  bool _telemRequested;         // Telemetry request already sent

  const char* getCachedPassword(int contactIdx) {
    for (int i = 0; i < _pwdCacheCount; i++) {
      if (_pwdCache[i].contactIdx == contactIdx) return _pwdCache[i].password;
    }
    return nullptr;
  }

  void cachePassword(int contactIdx, const char* pwd) {
    for (int i = 0; i < _pwdCacheCount; i++) {
      if (_pwdCache[i].contactIdx == contactIdx) {
        strncpy(_pwdCache[i].password, pwd, ADMIN_PASSWORD_MAX - 1);
        _pwdCache[i].password[ADMIN_PASSWORD_MAX - 1] = '\0';
        return;
      }
    }
    if (_pwdCacheCount < PWD_CACHE_SIZE) {
      int slot = _pwdCacheCount++;
      _pwdCache[slot].contactIdx = contactIdx;
      strncpy(_pwdCache[slot].password, pwd, ADMIN_PASSWORD_MAX - 1);
      _pwdCache[slot].password[ADMIN_PASSWORD_MAX - 1] = '\0';
    } else {
      for (int i = 0; i < PWD_CACHE_SIZE - 1; i++) _pwdCache[i] = _pwdCache[i + 1];
      _pwdCache[PWD_CACHE_SIZE - 1].contactIdx = contactIdx;
      strncpy(_pwdCache[PWD_CACHE_SIZE - 1].password, pwd, ADMIN_PASSWORD_MAX - 1);
      _pwdCache[PWD_CACHE_SIZE - 1].password[ADMIN_PASSWORD_MAX - 1] = '\0';
    }
  }

  // --- LPP telemetry parser ---
  // Walks a CayenneLPP buffer extracting voltage and temperature.
  // Format per entry: [channel 1B][type 1B][data varies]
  void parseLPPTelemetry(const uint8_t* data, uint8_t len) {
    const uint8_t TELEM_TYPE_VOLTAGE     = 0x74;  // 2 bytes, uint16 / 100 = V
    const uint8_t TELEM_TYPE_TEMPERATURE = 0x67;  // 2 bytes, int16 / 10 = C

    int pos = 0;
    while (pos + 2 < len) {
      // uint8_t channel = data[pos];  // not needed
      uint8_t type = data[pos + 1];
      pos += 2;  // skip channel + type

      // Determine data size for this type
      int dataSize = 0;
      switch (type) {
        case TELEM_TYPE_VOLTAGE:     dataSize = 2; break;
        case TELEM_TYPE_TEMPERATURE: dataSize = 2; break;
        case 0x88:            dataSize = 9; break;  // GPS
        case 0x75:            dataSize = 2; break;  // Current
        case 0x68:            dataSize = 1; break;  // Humidity
        case 0x73:            dataSize = 2; break;  // Pressure
        case 0x76:            dataSize = 2; break;  // Altitude
        case 0x80:            dataSize = 2; break;  // Power
        default:              return;                // Unknown type, stop parsing
      }

      if (pos + dataSize > len) break;

      if (type == TELEM_TYPE_VOLTAGE) {
        uint16_t raw = ((uint16_t)data[pos] << 8) | data[pos + 1];
        _telemVoltage = raw / 100.0f;
        _telemHasVoltage = true;
      } else if (type == TELEM_TYPE_TEMPERATURE) {
        int16_t raw = ((int16_t)data[pos] << 8) | data[pos + 1];
        _telemTempC = raw / 10.0f;
        _telemHasTemp = true;
      }

      pos += dataSize;
    }
  }

  // --- Helpers ---

  static void formatTime(char* buf, size_t bufLen, uint32_t epoch) {
    if (epoch == 0) { strncpy(buf, "--:--:--", bufLen); return; }
    uint32_t secs = epoch % 60;
    uint32_t mins = (epoch / 60) % 60;
    uint32_t hrs  = (epoch / 3600) % 24;
    snprintf(buf, bufLen, "%02d:%02d:%02d", (int)hrs, (int)mins, (int)secs);
  }

  static void formatRelativeTime(char* buf, size_t bufLen, uint32_t seconds) {
    if (seconds < 60) {
      snprintf(buf, bufLen, "%ds ago", (int)seconds);
    } else if (seconds < 3600) {
      snprintf(buf, bufLen, "%dm ago", (int)(seconds / 60));
    } else if (seconds < 86400) {
      int h = seconds / 3600;
      int m = (seconds % 3600) / 60;
      if (m > 0) snprintf(buf, bufLen, "%dh%dm ago", h, m);
      else       snprintf(buf, bufLen, "%dh ago", h);
    } else {
      snprintf(buf, bufLen, "%dd%dh ago", (int)(seconds / 86400), (int)((seconds % 86400) / 3600));
    }
  }

  int countLines(const char* text) {
    int lines = 0;
    const char* p = text;
    while (*p) {
      while (*p && *p != '\n') p++;
      if (*p == '\n') p++;
      lines++;
    }
    return lines;
  }

  // Parse raw neighbor response into formatted display text
  void parseNeighborResponse(const char* raw) {
    char* dp = _response;
    int remaining = ADMIN_RESPONSE_MAX - 1;
    uint32_t now = _rtc->getCurrentTime();
    int count = 0;

    const char* p = raw;
    while (*p && remaining > 80) {
      char line[128];
      int li = 0;
      while (*p && *p != '\n' && li < 127) line[li++] = *p++;
      line[li] = '\0';
      if (*p == '\n') p++;
      if (li == 0) continue;

      // Parse: {hex}:{timestamp}:{snr*4}
      char hexPart[20] = {0};
      uint32_t timestamp = 0;
      int snrX4 = 0;

      char* sep1 = strchr(line, ':');
      if (!sep1) {
        int n = snprintf(dp, remaining, "%s\n", line);
        dp += n; remaining -= n; count++;
        continue;
      }

      int hexLen = sep1 - line;
      if (hexLen > 19) hexLen = 19;
      memcpy(hexPart, line, hexLen);
      hexPart[hexLen] = '\0';

      char* afterSep1 = sep1 + 1;
      char* sep2 = strchr(afterSep1, ':');
      if (sep2) {
        *sep2 = '\0';
        timestamp = strtoul(afterSep1, NULL, 10);
        snrX4 = atoi(sep2 + 1);
      } else {
        timestamp = strtoul(afterSep1, NULL, 10);
      }

      // Resolve hex prefix to contact name
      const char* nodeName = NULL;
      uint8_t pubkeyBytes[PUB_KEY_SIZE];
      int byteLen = hexLen / 2;
      if (byteLen > 0 && byteLen <= PUB_KEY_SIZE) {
        if (mesh::Utils::fromHex(pubkeyBytes, byteLen, hexPart)) {
          ContactInfo* contact = the_mesh.lookupContactByPubKey(pubkeyBytes, byteLen);
          if (contact && contact->name[0] != '\0') nodeName = contact->name;
        }
      }

      // Format SNR (snr*4 encoded)
      char snrStr[12];
      if (snrX4 % 4 == 0) {
        snprintf(snrStr, sizeof(snrStr), "%ddB", snrX4 / 4);
      } else {
        snprintf(snrStr, sizeof(snrStr), "%d.%ddB", snrX4 / 4, ((abs(snrX4) % 4) * 10) / 4);
      }

      // Format relative time
      char timeStr[24];
      if (timestamp > 0 && now > timestamp) {
        formatRelativeTime(timeStr, sizeof(timeStr), now - timestamp);
      } else if (timestamp > 0) {
        strncpy(timeStr, "just now", sizeof(timeStr));
      } else {
        strncpy(timeStr, "?", sizeof(timeStr));
      }

      int n;
      if (nodeName) {
        n = snprintf(dp, remaining, "%.18s %s %s\n", nodeName, snrStr, timeStr);
      } else {
        n = snprintf(dp, remaining, "<%s> %s %s\n", hexPart, snrStr, timeStr);
      }
      dp += n; remaining -= n; count++;
    }

    // Remove trailing newline
    if (dp > _response && *(dp - 1) == '\n') { *(dp - 1) = '\0'; dp--; }

    // Prepend count header
    char header[48];
    snprintf(header, sizeof(header), "Neighbors: %d\n", count);
    int headerLen = strlen(header);
    int contentLen = dp - _response;
    if (headerLen + contentLen < ADMIN_RESPONSE_MAX - 1) {
      memmove(_response + headerLen, _response, contentLen + 1);
      memcpy(_response, header, headerLen);
      _responseLen = headerLen + contentLen;
    } else {
      _responseLen = contentLen;
    }
  }

public:
  RepeaterAdminScreen(AbstractUITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _state(STATE_PASSWORD_ENTRY),
      _contactIdx(-1), _permissions(0), _serverTime(0),
      _pwdLen(0), _lastCharAt(0),
      _catSel(0), _cmdSel(0), _scrollOffset(0),
      _paramLen(0), _pendingCmd(nullptr),
      _responseLen(0), _responseScroll(0), _responseTotalLines(0),
      _cmdSentAt(0), _loginTimeoutMs(ADMIN_TIMEOUT_MS), _waitingForLogin(false), _pwdCacheCount(0),
      _telemVoltage(0), _telemTempC(0),
      _telemHasVoltage(false), _telemHasTemp(false), _telemRequested(false) {
    _password[0] = '\0';
    _repeaterName[0] = '\0';
    _response[0] = '\0';
    _paramBuf[0] = '\0';
  }

  void openForContact(int contactIdx, const char* name) {
    _contactIdx = contactIdx;
    strncpy(_repeaterName, name, sizeof(_repeaterName) - 1);
    _repeaterName[sizeof(_repeaterName) - 1] = '\0';

    _state = STATE_PASSWORD_ENTRY;
    _lastCharAt = 0;
    _catSel = 0;
    _cmdSel = 0;
    _scrollOffset = 0;
    _permissions = 0;
    _serverTime = 0;
    _responseLen = 0;
    _responseScroll = 0;
    _responseTotalLines = 0;
    _response[0] = '\0';
    _waitingForLogin = false;
    _pendingCmd = nullptr;
    _paramLen = 0;
    _paramBuf[0] = '\0';
    _telemHasVoltage = false;
    _telemHasTemp = false;
    _telemRequested = false;

    const char* cached = getCachedPassword(contactIdx);
    if (cached) {
      strncpy(_password, cached, ADMIN_PASSWORD_MAX - 1);
      _password[ADMIN_PASSWORD_MAX - 1] = '\0';
      _pwdLen = strlen(_password);
    } else {
      _pwdLen = 0;
      _password[0] = '\0';
    }
  }

  int getContactIdx() const { return _contactIdx; }
  AdminState getState() const { return _state; }

  void onLoginResult(bool success, uint8_t permissions, uint32_t server_time) {
    _waitingForLogin = false;
    if (success) {
      _permissions = permissions;
      _serverTime = server_time;
      _state = STATE_CATEGORY_MENU;
      cachePassword(_contactIdx, _password);

      // Auto-request telemetry (battery & temperature) after login
      if (!_telemRequested) {
        _telemRequested = true;
        bool sent = the_mesh.uiSendTelemetryRequest(_contactIdx);
        Serial.printf("[Admin] Telemetry request %s for contact idx %d\n",
                      sent ? "sent" : "FAILED", _contactIdx);
      }
    } else {
      snprintf(_response, sizeof(_response), "Login failed.\nCheck password.");
      _responseLen = strlen(_response);
      _state = STATE_ERROR;
    }
  }

  void onCliResponse(const char* text) {
    if (_state != STATE_COMMAND_PENDING) return;

    // Special parsing for neighbor responses
    if (_pendingCmd && (_pendingCmd->flags & CMDF_PARSE_NEIGHBORS)) {
      parseNeighborResponse(text);
      _responseScroll = 0;
      _responseTotalLines = countLines(_response);
      _state = STATE_RESPONSE_VIEW;
      return;
    }

    int tlen = strlen(text);
    if (tlen >= ADMIN_RESPONSE_MAX) tlen = ADMIN_RESPONSE_MAX - 1;
    memcpy(_response, text, tlen);
    _response[tlen] = '\0';
    _responseLen = tlen;
    _responseScroll = 0;
    _responseTotalLines = countLines(_response);
    _state = STATE_RESPONSE_VIEW;
  }

  void onTelemetryResult(const uint8_t* data, uint8_t len) {
    Serial.printf("[Admin] Telemetry response received, %d bytes:", len);
    for (int i = 0; i < len && i < 32; i++) Serial.printf(" %02X", data[i]);
    Serial.println();
    parseLPPTelemetry(data, len);
    Serial.printf("[Admin] Parsed: hasVoltage=%d (%.2fV) hasTemp=%d (%.1fC)\n",
                  _telemHasVoltage, _telemVoltage, _telemHasTemp, _telemTempC);
  }

  void poll() override {
    if (_cmdSentAt > 0) {
      unsigned long elapsed = millis() - _cmdSentAt;
      unsigned long timeout = (_state == STATE_LOGGING_IN) ? _loginTimeoutMs : ADMIN_TIMEOUT_MS;

      if ((_state == STATE_LOGGING_IN || _state == STATE_COMMAND_PENDING) && elapsed > timeout) {
        if (_pendingCmd && (_pendingCmd->flags & CMDF_EXPECT_TIMEOUT)) {
          snprintf(_response, sizeof(_response), "Command sent.\nTimeout is expected\n(device is rebooting/updating).");
          _responseLen = strlen(_response);
          _responseTotalLines = countLines(_response);
          _state = STATE_RESPONSE_VIEW;
        } else {
          snprintf(_response, sizeof(_response), "Timeout - no response.");
          _responseLen = strlen(_response);
          _state = STATE_ERROR;
        }
        _task->forceRefresh();  // Immediate redraw on state change
      }
    }
  }

  // =====================================================================
  // Render
  // =====================================================================

  int render(DisplayDriver& display) override {
    char tmp[64];

    // --- Header ---
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    const char* hdrPrefix = (_state == STATE_PASSWORD_ENTRY || _state == STATE_LOGGING_IN)
                             ? "Login" : "Admin";
    snprintf(tmp, sizeof(tmp), "%s: %.16s", hdrPrefix, _repeaterName);
    display.print(tmp);

    if (_state >= STATE_CATEGORY_MENU && _state <= STATE_RESPONSE_VIEW) {
      const char* perm = (_permissions & 0x03) >= 3 ? "ADM" :
                         (_permissions & 0x03) >= 2 ? "R/W" : "R/O";
      display.setCursor(display.width() - display.getTextWidth(perm) - 2, 0);
      display.print(perm);
    }

    display.drawRect(0, 11, display.width(), 1);

    int bodyY = 14;
    int footerY = display.height() - 12;
    int bodyHeight = footerY - bodyY - 4;

    // --- Body ---
    switch (_state) {
      case STATE_PASSWORD_ENTRY:  renderPasswordEntry(display, bodyY); break;
      case STATE_LOGGING_IN:      renderWaiting(display, bodyY, "Logging in..."); break;
      case STATE_CATEGORY_MENU:   renderCategoryMenu(display, bodyY, bodyHeight); break;
      case STATE_COMMAND_MENU:    renderCommandMenu(display, bodyY, bodyHeight); break;
      case STATE_PARAM_ENTRY:     renderParamEntry(display, bodyY); break;
      case STATE_CONFIRM:         renderConfirm(display, bodyY); break;
      case STATE_COMMAND_PENDING: renderWaiting(display, bodyY, "Waiting..."); break;
      case STATE_RESPONSE_VIEW:   renderResponse(display, bodyY, bodyHeight); break;
      case STATE_ERROR:           renderResponse(display, bodyY, bodyHeight); break;
    }

    // --- Footer ---
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setTextSize(1);
    display.setCursor(0, footerY);

    switch (_state) {
      case STATE_PASSWORD_ENTRY:
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Boot:Exit");
        renderFooterRight(display, footerY, "Hold:Type");
#else
        display.print("Sh+Del:Exit");
        renderFooterRight(display, footerY, "Ent:Login");
#endif
        break;

      case STATE_LOGGING_IN:
      case STATE_COMMAND_PENDING:
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Boot:Cancel");
#else
        display.print("Sh+Del:Cancel");
#endif
        break;

      case STATE_CATEGORY_MENU:
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Boot:Exit");
        renderFooterMidRight(display, footerY, "Back:Exit", "Tap:Open", "Swipe:Sel");
#else
        display.print("Sh+Del:Exit");
        renderFooterMidRight(display, footerY, "Sh+Del:Exit", "Ent:Open", "W/S:Sel");
#endif
        break;

      case STATE_COMMAND_MENU:
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Boot:Back");
        renderFooterMidRight(display, footerY, "Back:Back", "Tap:Run", "Swipe:Sel");
#else
        display.print("Sh+Del:Back");
        renderFooterMidRight(display, footerY, "Sh+Del:Back", "Ent:Run", "W/S:Sel");
#endif
        break;

      case STATE_PARAM_ENTRY:
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Boot:Cancel");
        renderFooterRight(display, footerY, "Tap:Send");
#else
        display.print("Sh+Del:Cancel");
        renderFooterRight(display, footerY, "Ent:Send");
#endif
        break;

      case STATE_CONFIRM:
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Boot:No");
        renderFooterRight(display, footerY, "Tap:Yes");
#else
        display.print("Sh+Del:No");
        renderFooterRight(display, footerY, "Ent:Yes");
#endif
        break;

      case STATE_RESPONSE_VIEW:
      case STATE_ERROR:
#if defined(LilyGo_T5S3_EPaper_Pro)
        display.print("Boot:Back");
        if (_responseTotalLines > bodyHeight / 9) {
          renderFooterRight(display, footerY, "Swipe:Scroll");
        }
#else
        display.print("Sh+Del:Back");
        if (_responseTotalLines > bodyHeight / 9) {
          renderFooterRight(display, footerY, "W/S:Scrll");
        }
#endif
        break;
    }

    if (_state == STATE_LOGGING_IN || _state == STATE_COMMAND_PENDING) return 30000;  // static text; poll()/callbacks force refresh on state change
    if (_state == STATE_PASSWORD_ENTRY && _lastCharAt > 0 && (millis() - _lastCharAt) < 800) {
      return _lastCharAt + 800 - millis() + 50;
    }
    return 5000;
  }

  // =====================================================================
  // Input dispatch
  // =====================================================================

  bool handleInput(char c) override {
    switch (_state) {
      case STATE_PASSWORD_ENTRY:  return handlePasswordInput(c);
      case STATE_LOGGING_IN:
      case STATE_COMMAND_PENDING:
        if (c == KEY_ADMIN_EXIT) {
          _state = (_state == STATE_LOGGING_IN) ? STATE_PASSWORD_ENTRY : STATE_COMMAND_MENU;
          return true;
        }
        return false;
      case STATE_CATEGORY_MENU:   return handleCategoryInput(c);
      case STATE_COMMAND_MENU:    return handleCommandInput(c);
      case STATE_PARAM_ENTRY:     return handleParamInput(c);
      case STATE_CONFIRM:         return handleConfirmInput(c);
      case STATE_RESPONSE_VIEW:
      case STATE_ERROR:           return handleResponseInput(c);
    }
    return false;
  }

private:

  // --- Footer helpers ---
  void renderFooterRight(DisplayDriver& display, int footerY, const char* text) {
    display.setCursor(display.width() - display.getTextWidth(text) - 2, footerY);
    display.print(text);
  }

  void renderFooterMidRight(DisplayDriver& display, int footerY,
                            const char* left, const char* right, const char* mid) {
    int leftEnd = display.getTextWidth(left) + 2;
    int rightStart = display.width() - display.getTextWidth(right) - 2;
    int midX = leftEnd + (rightStart - leftEnd - display.getTextWidth(mid)) / 2;
    display.setCursor(midX, footerY);
    display.print(mid);
    display.setCursor(rightStart, footerY);
    display.print(right);
  }

  // =====================================================================
  // Password Entry
  // =====================================================================

  void renderPasswordEntry(DisplayDriver& display, int y) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    display.print("Password:");

    y += 14;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);

    char masked[ADMIN_PASSWORD_MAX];
    bool revealing = (_pwdLen > 0 && (millis() - _lastCharAt) < 800);
    int revealIdx = revealing ? _pwdLen - 1 : -1;
    int i;
    for (i = 0; i < _pwdLen && i < ADMIN_PASSWORD_MAX - 1; i++) {
      masked[i] = (i == revealIdx) ? _password[i] : '*';
    }
    masked[i] = '\0';
    display.print(masked);
    display.print("_");
  }

  bool handlePasswordInput(char c) {
    if (c == KEY_ADMIN_EXIT) return false;  // exit screen
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      if (_pwdLen > 0) return doLogin();
      return true;
    }
    if (c == 0x08 || c == 0x7F) {
      if (_pwdLen > 0) { _pwdLen--; _password[_pwdLen] = '\0'; _lastCharAt = 0; }
      return true;
    }
    if (c >= 32 && c < 127 && _pwdLen < ADMIN_PASSWORD_MAX - 1) {
      _password[_pwdLen++] = c;
      _password[_pwdLen] = '\0';
      _lastCharAt = millis();
      return true;
    }
    return false;
  }

  bool doLogin();

  // =====================================================================
  // Category Menu (top level)
  // =====================================================================

  void renderCategoryMenu(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(0);
    int lineHeight = 9;

    // Clock drift info line
    if (_serverTime > 0) {
      char ourTime[12], srvTime[12];
      uint32_t now = _rtc->getCurrentTime();
      formatTime(ourTime, sizeof(ourTime), now);
      formatTime(srvTime, sizeof(srvTime), _serverTime);
      int drift = (int)(now - _serverTime);
      char driftStr[24];
      if (abs(drift) < 2) snprintf(driftStr, sizeof(driftStr), "Synced");
      else                snprintf(driftStr, sizeof(driftStr), "Drift:%+ds", drift);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      char info[48];
      snprintf(info, sizeof(info), "Rpt:%s Us:%s %s", srvTime, ourTime, driftStr);
      display.print(info);
      y += lineHeight + 2;
    }

    // Remote telemetry info line (battery & temperature)
    if (_telemHasVoltage || _telemHasTemp) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      char telem[48];
      int tpos = 0;
      if (_telemHasVoltage) {
        tpos += snprintf(telem + tpos, sizeof(telem) - tpos, "Batt:%.2fV", _telemVoltage);
      }
      if (_telemHasTemp) {
        if (tpos > 0) tpos += snprintf(telem + tpos, sizeof(telem) - tpos, " ");
        tpos += snprintf(telem + tpos, sizeof(telem) - tpos, "Temp:%.1fC", _telemTempC);
      }
      display.print(telem);
      y += lineHeight + 2;
    } else if (_telemRequested) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      display.print("Telemetry: requesting...");
      y += lineHeight + 2;
    }

    // Render categories
    for (int i = 0; i < CAT_COUNT && y + lineHeight <= display.height() - 16; i++) {
      bool isSystem = (i == CAT_REBOOT_OTA);
      renderMenuItem(display, y, lineHeight, i == _catSel, CATEGORIES[i].label, isSystem && (i != _catSel));
      y += lineHeight;
    }
    display.setTextSize(1);
  }

  bool handleCategoryInput(char c) {
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_catSel > 0) _catSel--;
      return true;
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_catSel < CAT_COUNT - 1) _catSel++;
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      _cmdSel = 0;
      _scrollOffset = 0;
      _state = STATE_COMMAND_MENU;
      return true;
    }
    if (c == KEY_ADMIN_EXIT) return false;  // exit screen
    // Number keys 1-7
    if (c >= '1' && c <= '0' + CAT_COUNT) {
      _catSel = c - '1';
      _cmdSel = 0;
      _scrollOffset = 0;
      _state = STATE_COMMAND_MENU;
      return true;
    }
    return false;
  }

  // =====================================================================
  // Command Menu (within a category)
  // =====================================================================

  void renderCommandMenu(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(0);
    int lineHeight = 9;
    const AdminCategoryDef& cat = CATEGORIES[_catSel];

    // Category title
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, y);
    display.print(cat.label);
    y += lineHeight + 2;

    // Compute visible window
    int maxVisible = (display.height() - 16 - y) / lineHeight;
    if (maxVisible < 1) maxVisible = 1;
    if (_cmdSel < _scrollOffset) _scrollOffset = _cmdSel;
    if (_cmdSel >= _scrollOffset + maxVisible) _scrollOffset = _cmdSel - maxVisible + 1;

    bool isSystemCat = (_catSel == CAT_REBOOT_OTA);

    for (int i = _scrollOffset; i < cat.count && (y + lineHeight <= display.height() - 16); i++) {
      bool warn = isSystemCat || (cat.cmds[i].flags & (CMDF_CONFIRM | CMDF_EXPECT_TIMEOUT));
      renderMenuItem(display, y, lineHeight, i == _cmdSel, cat.cmds[i].label, warn && (i != _cmdSel));
      y += lineHeight;
    }

    // Scroll indicators
    if (_scrollOffset > 0) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, 14 + lineHeight + 2);
      display.print("^");
    }
    if (_scrollOffset + maxVisible < cat.count) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, display.height() - 16 - lineHeight);
      display.print("v");
    }

    display.setTextSize(1);
  }

  bool handleCommandInput(char c) {
    int count = CATEGORIES[_catSel].count;
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_cmdSel > 0) _cmdSel--;
      return true;
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_cmdSel < count - 1) _cmdSel++;
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      return initiateCommand(&CATEGORIES[_catSel].cmds[_cmdSel]);
    }
    if (c == KEY_ADMIN_EXIT) {
      _state = STATE_CATEGORY_MENU;
      return true;
    }
    // Number keys for quick select (1-9 within category)
    if (c >= '1' && c <= '9') {
      int idx = c - '1';
      if (idx < count) {
        _cmdSel = idx;
        return initiateCommand(&CATEGORIES[_catSel].cmds[_cmdSel]);
      }
    }
    return false;
  }

  // Decide what to do when a command is selected
  bool initiateCommand(const AdminCmdDef* cmd) {
    _pendingCmd = cmd;

    // If it needs parameter input, go to param entry first
    if (cmd->flags & CMDF_PARAM) {
      _paramLen = 0;
      _paramBuf[0] = '\0';
      _state = STATE_PARAM_ENTRY;
      return true;
    }

    // If it needs confirmation (but no param), show confirm dialog
    if (cmd->flags & CMDF_CONFIRM) {
      _state = STATE_CONFIRM;
      return true;
    }

    // Otherwise send immediately
    return sendCommand(cmd->cmd);
  }

  // =====================================================================
  // Parameter Entry
  // =====================================================================

  void renderParamEntry(DisplayDriver& display, int y) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    if (_pendingCmd) display.print(_pendingCmd->label);

    y += 14;
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    if (_pendingCmd && _pendingCmd->paramHint) display.print(_pendingCmd->paramHint);

    y += 14;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);
    display.print(_paramBuf);
    display.print("_");
  }

  bool handleParamInput(char c) {
    if (c == KEY_ADMIN_EXIT) {
      _state = STATE_COMMAND_MENU;
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      if (_paramLen == 0) return true;  // don't send empty

      // If this command also needs confirmation, go to confirm now
      if (_pendingCmd && (_pendingCmd->flags & CMDF_CONFIRM)) {
        _state = STATE_CONFIRM;
        return true;
      }

      // Build and send: prefix + param
      return sendParamCommand();
    }
    if (c == 0x08 || c == 0x7F) {
      if (_paramLen > 0) { _paramLen--; _paramBuf[_paramLen] = '\0'; }
      return true;
    }
    if (c >= 32 && c < 127 && _paramLen < ADMIN_PARAM_MAX - 1) {
      _paramBuf[_paramLen++] = c;
      _paramBuf[_paramLen] = '\0';
      return true;
    }
    return false;
  }

  bool sendParamCommand() {
    if (!_pendingCmd || _contactIdx < 0) return false;
    char fullCmd[160];
    snprintf(fullCmd, sizeof(fullCmd), "%s%s", _pendingCmd->cmd, _paramBuf);
    return sendCommand(fullCmd);
  }

  // =====================================================================
  // Confirmation
  // =====================================================================

  void renderConfirm(DisplayDriver& display, int y) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);
    display.print("Confirm:");

    y += 16;
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    if (_pendingCmd) display.print(_pendingCmd->label);

    y += 14;
    display.setTextSize(0);
    display.setCursor(0, y);

    // Show the param value if one was collected
    if (_pendingCmd && (_pendingCmd->flags & CMDF_PARAM) && _paramLen > 0) {
      char preview[80];
      snprintf(preview, sizeof(preview), "Value: %s", _paramBuf);
      display.print(preview);
      y += 10;
      display.setCursor(0, y);
    }

    if (_pendingCmd && (_pendingCmd->flags & CMDF_EXPECT_TIMEOUT)) {
      display.print("Timeout response is normal.");
    } else {
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.print("Tap=Yes  Back=No");
#else
      display.print("Enter=Yes  Sh+Del=No");
#endif
    }

    display.setTextSize(1);
  }

  bool handleConfirmInput(char c) {
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      if (_pendingCmd && (_pendingCmd->flags & CMDF_PARAM) && _paramLen > 0) {
        return sendParamCommand();
      } else if (_pendingCmd) {
        return sendCommand(_pendingCmd->cmd);
      }
      return false;
    }
    if (c == KEY_ADMIN_EXIT || c == 'n' || c == 'N') {
      _state = STATE_COMMAND_MENU;
      return true;
    }
    return false;
  }

  // =====================================================================
  // Response View
  // =====================================================================

  void renderResponse(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(0);
    int lineHeight = 9;

    display.setColor((_state == STATE_ERROR) ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);

    int maxLines = bodyHeight / lineHeight;
    int lineCount = 0;
    int skipLines = _responseScroll;

    const char* p = _response;
    char lineBuf[80];

    while (*p && lineCount < maxLines + skipLines) {
      int i = 0;
      while (*p && *p != '\n' && i < 79) lineBuf[i++] = *p++;
      lineBuf[i] = '\0';
      if (*p == '\n') p++;

      if (lineCount >= skipLines && lineCount < skipLines + maxLines) {
        display.setCursor(2, y);
        display.print(lineBuf);
        y += lineHeight;
      }
      lineCount++;
    }

    // Scroll indicators
    if (_responseScroll > 0) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, 14);
      display.print("^");
    }
    if (lineCount > skipLines + maxLines) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 8, y - lineHeight);
      display.print("v");
    }

    display.setTextSize(1);
  }

  bool handleResponseInput(char c) {
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_responseScroll > 0) { _responseScroll--; return true; }
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      _responseScroll++; return true;
    }
    if (c == KEY_ADMIN_EXIT) {
      if (_state == STATE_ERROR && _permissions == 0) {
        _state = STATE_PASSWORD_ENTRY;
      } else {
        _state = STATE_COMMAND_MENU;
      }
      return true;
    }
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      _state = STATE_COMMAND_MENU;
      return true;
    }
    return false;
  }

  // =====================================================================
  // Waiting
  // =====================================================================

  void renderWaiting(DisplayDriver& display, int y, const char* msg) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    int cx = (display.width() - display.getTextWidth(msg)) / 2;
    int cy = y + 20;
    display.setCursor(cx, cy);
    display.print(msg);

    if (_cmdSentAt > 0) {
      char elapsed[16];
      snprintf(elapsed, sizeof(elapsed), "%lus", (millis() - _cmdSentAt) / 1000);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor((display.width() - display.getTextWidth(elapsed)) / 2, cy + 14);
      display.print(elapsed);
    }
  }

  // =====================================================================
  // Shared menu item renderer
  // =====================================================================

  void renderMenuItem(DisplayDriver& display, int y, int lineHeight,
                      bool selected, const char* label, bool warn) {
    if (selected) {
      display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
      display.fillRect(0, y, display.width(), lineHeight);
#else
      display.fillRect(0, y + 5, display.width(), lineHeight);
#endif
      display.setColor(DisplayDriver::DARK);
    } else if (warn) {
      display.setColor(DisplayDriver::YELLOW);
    } else {
      display.setColor(DisplayDriver::LIGHT);
    }

    display.setCursor(2, y);
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %s", selected ? ">" : " ", label);
    display.print(buf);
  }

  // =====================================================================
  // Send command to repeater
  // =====================================================================

  bool sendCommand(const char* cmd) {
    if (_contactIdx < 0 || !cmd || cmd[0] == '\0') return false;

    if (the_mesh.uiSendCliCommand(_contactIdx, cmd)) {
      _state = STATE_COMMAND_PENDING;
      _cmdSentAt = millis();
      _response[0] = '\0';
      _responseLen = 0;
      _responseScroll = 0;
      _responseTotalLines = 0;
      return true;
    } else {
      snprintf(_response, sizeof(_response), "Send failed.");
      _responseLen = strlen(_response);
      _state = STATE_ERROR;
      return true;
    }
  }
};

// =====================================================================
// Login implementation (requires MyMesh)
// =====================================================================

inline bool RepeaterAdminScreen::doLogin() {
  if (_contactIdx < 0 || _pwdLen == 0) return false;

  uint32_t timeout_ms = 0;
  if (the_mesh.uiLoginToRepeater(_contactIdx, _password, timeout_ms)) {
    _state = STATE_LOGGING_IN;
    _cmdSentAt = millis();
    // Add a 5s buffer over the mesh estimate to account for blocking e-ink
    // refreshes (FastEPD ~1-2s per frame, VKB dismiss + login render = 2-3 frames).
    // Fall back to ADMIN_TIMEOUT_MS if the estimate came back zero.
    _loginTimeoutMs = (timeout_ms > 0) ? timeout_ms + 5000 : ADMIN_TIMEOUT_MS;
    _waitingForLogin = true;
    return true;
  } else {
    snprintf(_response, sizeof(_response), "Send failed.\nCheck contact path.");
    _responseLen = strlen(_response);
    _state = STATE_ERROR;
    return true;
  }
}