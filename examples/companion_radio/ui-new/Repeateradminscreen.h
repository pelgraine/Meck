#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

#define ADMIN_PASSWORD_MAX    32
#define ADMIN_RESPONSE_MAX    512   // CLI responses can be multi-line
#define ADMIN_TIMEOUT_MS      15000 // 15s timeout for login/commands

class RepeaterAdminScreen : public UIScreen {
public:
  enum AdminState {
    STATE_PASSWORD_ENTRY,   // Typing admin password
    STATE_LOGGING_IN,       // Waiting for login response
    STATE_MENU,             // Main admin menu
    STATE_COMMAND_PENDING,  // Waiting for CLI response
    STATE_RESPONSE_VIEW,    // Displaying CLI response
    STATE_ERROR             // Error state (timeout, send fail)
  };

  // Menu items
  enum MenuItem {
    MENU_CLOCK_SYNC = 0,
    MENU_ADVERT,
    MENU_NEIGHBORS,
    MENU_GET_CLOCK,
    MENU_GET_VER,
    MENU_GET_STATUS,
    MENU_COUNT
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;

  AdminState _state;
  int _contactIdx;              // Contact table index of the repeater
  char _repeaterName[32];       // Cached repeater name
  uint8_t _permissions;         // Login permissions (0=guest, 3=admin)
  uint32_t _serverTime;         // Server timestamp from login response

  // Password entry
  char _password[ADMIN_PASSWORD_MAX];
  int _pwdLen;

  // Menu
  int _menuSel;                 // Currently selected menu item

  // Response buffer
  char _response[ADMIN_RESPONSE_MAX];
  int _responseLen;
  int _responseScroll;          // Scroll offset for long responses

  // Timing
  unsigned long _cmdSentAt;     // millis() when command was sent
  bool _waitingForLogin;

  static const char* menuLabel(MenuItem m) {
    switch (m) {
      case MENU_CLOCK_SYNC:  return "Clock Sync";
      case MENU_ADVERT:      return "Send Advert";
      case MENU_NEIGHBORS:   return "Neighbors";
      case MENU_GET_CLOCK:   return "Get Clock";
      case MENU_GET_VER:     return "Version";
      case MENU_GET_STATUS:  return "Get Status";
      default:               return "?";
    }
  }

  static const char* menuCommand(MenuItem m) {
    switch (m) {
      case MENU_CLOCK_SYNC:  return "clock sync";
      case MENU_ADVERT:      return "advert";
      case MENU_NEIGHBORS:   return "neighbors";
      case MENU_GET_CLOCK:   return "clock";
      case MENU_GET_VER:     return "ver";
      case MENU_GET_STATUS:  return "get status";
      default:               return "";
    }
  }

  // Format epoch as HH:MM:SS
  static void formatTime(char* buf, size_t bufLen, uint32_t epoch) {
    if (epoch == 0) {
      strncpy(buf, "--:--:--", bufLen);
      return;
    }
    uint32_t secs = epoch % 60;
    uint32_t mins = (epoch / 60) % 60;
    uint32_t hrs  = (epoch / 3600) % 24;
    snprintf(buf, bufLen, "%02d:%02d:%02d", (int)hrs, (int)mins, (int)secs);
  }

public:
  RepeaterAdminScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _state(STATE_PASSWORD_ENTRY),
      _contactIdx(-1), _permissions(0), _serverTime(0),
      _pwdLen(0), _menuSel(0),
      _responseLen(0), _responseScroll(0),
      _cmdSentAt(0), _waitingForLogin(false) {
    _password[0] = '\0';
    _repeaterName[0] = '\0';
    _response[0] = '\0';
  }

  // Called when entering the screen for a specific repeater contact
  void openForContact(int contactIdx, const char* name) {
    _contactIdx = contactIdx;
    strncpy(_repeaterName, name, sizeof(_repeaterName) - 1);
    _repeaterName[sizeof(_repeaterName) - 1] = '\0';

    // Reset state
    _state = STATE_PASSWORD_ENTRY;
    _pwdLen = 0;
    _password[0] = '\0';
    _menuSel = 0;
    _permissions = 0;
    _serverTime = 0;
    _responseLen = 0;
    _responseScroll = 0;
    _response[0] = '\0';
    _waitingForLogin = false;
  }

  int getContactIdx() const { return _contactIdx; }
  AdminState getState() const { return _state; }

  // Called by UITask when a login response is received
  void onLoginResult(bool success, uint8_t permissions, uint32_t server_time) {
    _waitingForLogin = false;
    if (success) {
      _permissions = permissions;
      _serverTime = server_time;
      _state = STATE_MENU;
    } else {
      snprintf(_response, sizeof(_response), "Login failed.\nCheck password.");
      _responseLen = strlen(_response);
      _state = STATE_ERROR;
    }
  }

  // Called by UITask when a CLI response is received
  void onCliResponse(const char* text) {
    if (_state != STATE_COMMAND_PENDING) return;

    int tlen = strlen(text);
    if (tlen >= ADMIN_RESPONSE_MAX) tlen = ADMIN_RESPONSE_MAX - 1;
    memcpy(_response, text, tlen);
    _response[tlen] = '\0';
    _responseLen = tlen;
    _responseScroll = 0;
    _state = STATE_RESPONSE_VIEW;
  }

  // Poll for timeouts
  void poll() override {
    if ((_state == STATE_LOGGING_IN || _state == STATE_COMMAND_PENDING) &&
        _cmdSentAt > 0 && (millis() - _cmdSentAt) > ADMIN_TIMEOUT_MS) {
      snprintf(_response, sizeof(_response), "Timeout - no response.");
      _responseLen = strlen(_response);
      _state = STATE_ERROR;
    }
  }

  int render(DisplayDriver& display) override {
    char tmp[64];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    // Truncate name if needed to fit header
    snprintf(tmp, sizeof(tmp), "Admin: %.16s", _repeaterName);
    display.print(tmp);

    // Show permissions if logged in
    if (_state >= STATE_MENU && _state <= STATE_RESPONSE_VIEW) {
      const char* perm = (_permissions & 0x03) >= 3 ? "ADM" : 
                         (_permissions & 0x03) >= 2 ? "R/W" : "R/O";
      display.setCursor(display.width() - display.getTextWidth(perm) - 2, 0);
      display.print(perm);
    }

    display.drawRect(0, 11, display.width(), 1);  // divider

    // === Body - depends on state ===
    int bodyY = 14;
    int footerY = display.height() - 12;
    int bodyHeight = footerY - bodyY - 4;

    switch (_state) {
      case STATE_PASSWORD_ENTRY:
        renderPasswordEntry(display, bodyY);
        break;
      case STATE_LOGGING_IN:
        renderWaiting(display, bodyY, "Logging in...");
        break;
      case STATE_MENU:
        renderMenu(display, bodyY, bodyHeight);
        break;
      case STATE_COMMAND_PENDING:
        renderWaiting(display, bodyY, "Waiting...");
        break;
      case STATE_RESPONSE_VIEW:
        renderResponse(display, bodyY, bodyHeight);
        break;
      case STATE_ERROR:
        renderResponse(display, bodyY, bodyHeight);  // reuse response renderer for errors
        break;
    }

    // === Footer ===
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setTextSize(1);
    display.setCursor(0, footerY);

    switch (_state) {
      case STATE_PASSWORD_ENTRY:
        display.print("Q:Back");
        {
          const char* right = "Enter:Login";
          display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
          display.print(right);
        }
        break;
      case STATE_LOGGING_IN:
      case STATE_COMMAND_PENDING:
        display.print("Q:Cancel");
        break;
      case STATE_MENU:
        display.print("Q:Back");
        {
          const char* mid = "W/S:Sel";
          display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
          display.print(mid);
          const char* right = "Ent:Run";
          display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
          display.print(right);
        }
        break;
      case STATE_RESPONSE_VIEW:
      case STATE_ERROR:
        display.print("Q:Menu");
        if (_responseLen > bodyHeight / 9) {  // if scrollable
          const char* right = "W/S:Scrll";
          display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
          display.print(right);
        }
        break;
    }

    return (_state == STATE_LOGGING_IN || _state == STATE_COMMAND_PENDING) ? 1000 : 5000;
  }

  bool handleInput(char c) override {
    switch (_state) {
      case STATE_PASSWORD_ENTRY:
        return handlePasswordInput(c);
      case STATE_LOGGING_IN:
      case STATE_COMMAND_PENDING:
        // Q to cancel and go back
        if (c == 'q' || c == 'Q') {
          _state = (_state == STATE_LOGGING_IN) ? STATE_PASSWORD_ENTRY : STATE_MENU;
          return true;
        }
        return false;
      case STATE_MENU:
        return handleMenuInput(c);
      case STATE_RESPONSE_VIEW:
      case STATE_ERROR:
        return handleResponseInput(c);
    }
    return false;
  }

private:
  // --- Password Entry ---
  void renderPasswordEntry(DisplayDriver& display, int y) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, y);
    display.print("Password:");

    y += 14;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, y);

    // Show asterisks for password characters
    char masked[ADMIN_PASSWORD_MAX];
    int i;
    for (i = 0; i < _pwdLen && i < ADMIN_PASSWORD_MAX - 1; i++) {
      masked[i] = '*';
    }
    masked[i] = '\0';
    display.print(masked);

    // Cursor indicator
    display.print("_");
  }

  bool handlePasswordInput(char c) {
    // Q without any password typed = go back (return false to signal "not handled")
    if ((c == 'q' || c == 'Q') && _pwdLen == 0) {
      return false;
    }

    // Enter to submit
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      if (_pwdLen > 0) {
        return doLogin();
      }
      return true;
    }

    // Backspace
    if (c == 0x08 || c == 0x7F) {
      if (_pwdLen > 0) {
        _pwdLen--;
        _password[_pwdLen] = '\0';
      }
      return true;
    }

    // Printable character
    if (c >= 32 && c < 127 && _pwdLen < ADMIN_PASSWORD_MAX - 1) {
      _password[_pwdLen++] = c;
      _password[_pwdLen] = '\0';
      return true;
    }

    return false;
  }

  bool doLogin();  // Defined below, calls into MyMesh

  // --- Menu ---
  void renderMenu(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(0);  // tiny font for compact rows
    int lineHeight = 9;

    // Show server time comparison if available
    if (_serverTime > 0) {
      char ourTime[12], srvTime[12];
      uint32_t now = _rtc->getCurrentTime();
      formatTime(ourTime, sizeof(ourTime), now);
      formatTime(srvTime, sizeof(srvTime), _serverTime);
      
      int drift = (int)(now - _serverTime);
      char driftStr[24];
      if (abs(drift) < 2) {
        snprintf(driftStr, sizeof(driftStr), "Synced");
      } else {
        snprintf(driftStr, sizeof(driftStr), "Drift:%+ds", drift);
      }
      
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      char info[48];
      snprintf(info, sizeof(info), "Rpt:%s Us:%s %s", srvTime, ourTime, driftStr);
      display.print(info);
      y += lineHeight + 2;
    }

    // Menu items
    for (int i = 0; i < MENU_COUNT && y + lineHeight <= display.height() - 16; i++) {
      bool selected = (i == _menuSel);

      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + 5, display.width(), lineHeight);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      display.setCursor(2, y);
      char label[32];
      snprintf(label, sizeof(label), "%s %s", selected ? ">" : " ", menuLabel((MenuItem)i));
      display.print(label);

      y += lineHeight;
    }

    display.setTextSize(1);
  }

  bool handleMenuInput(char c) {
    // W/up - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_menuSel > 0) _menuSel--;
      return true;
    }
    // S/down - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_menuSel < MENU_COUNT - 1) _menuSel++;
      return true;
    }
    // Enter - execute selected command
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      return executeMenuCommand((MenuItem)_menuSel);
    }
    // Q - back to contacts
    if (c == 'q' || c == 'Q') {
      return false;  // let UITask handle back navigation
    }
    // Number keys for quick selection
    if (c >= '1' && c <= '0' + MENU_COUNT) {
      _menuSel = c - '1';
      return executeMenuCommand((MenuItem)_menuSel);
    }
    return false;
  }

  bool executeMenuCommand(MenuItem item);  // Defined below, calls into MyMesh

  // --- Response View ---
  void renderResponse(DisplayDriver& display, int y, int bodyHeight) {
    display.setTextSize(0);  // tiny font for more content
    int lineHeight = 9;

    display.setColor((_state == STATE_ERROR) ? DisplayDriver::YELLOW : DisplayDriver::LIGHT);

    // Render response text with word wrapping and scroll support
    int maxLines = bodyHeight / lineHeight;
    int lineCount = 0;
    int skipLines = _responseScroll;
    
    const char* p = _response;
    char lineBuf[80];
    int lineWidth = display.width() - 4;

    while (*p && lineCount < maxLines + skipLines) {
      // Extract next line (up to newline or screen width)
      int i = 0;
      while (*p && *p != '\n' && i < 79) {
        lineBuf[i++] = *p++;
      }
      lineBuf[i] = '\0';
      if (*p == '\n') p++;

      if (lineCount >= skipLines && lineCount < skipLines + maxLines) {
        display.setCursor(2, y);
        display.print(lineBuf);
        y += lineHeight;
      }
      lineCount++;
    }

    display.setTextSize(1);
  }

  bool handleResponseInput(char c) {
    // W/up - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_responseScroll > 0) {
        _responseScroll--;
        return true;
      }
    }
    // S/down - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      _responseScroll++;
      return true;
    }
    // Q - back to menu (or back to password on error)
    if (c == 'q' || c == 'Q') {
      if (_state == STATE_ERROR && _permissions == 0) {
        // Not yet logged in, go back to password
        _state = STATE_PASSWORD_ENTRY;
      } else {
        _state = STATE_MENU;
      }
      return true;
    }
    // Enter - also go back to menu
    if (c == '\r' || c == '\n' || c == KEY_ENTER) {
      _state = STATE_MENU;
      return true;
    }
    return false;
  }

  // --- Waiting spinner ---
  void renderWaiting(DisplayDriver& display, int y, const char* msg) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);

    int cx = (display.width() - display.getTextWidth(msg)) / 2;
    int cy = y + 20;
    display.setCursor(cx, cy);
    display.print(msg);

    // Show elapsed time
    if (_cmdSentAt > 0) {
      char elapsed[16];
      unsigned long secs = (millis() - _cmdSentAt) / 1000;
      snprintf(elapsed, sizeof(elapsed), "%lus", secs);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor((display.width() - display.getTextWidth(elapsed)) / 2, cy + 14);
      display.print(elapsed);
    }
  }
};

// --- Implementations that require MyMesh (the_mesh is declared extern above) ---

inline bool RepeaterAdminScreen::doLogin() {
  if (_contactIdx < 0 || _pwdLen == 0) return false;

  if (the_mesh.uiLoginToRepeater(_contactIdx, _password)) {
    _state = STATE_LOGGING_IN;
    _cmdSentAt = millis();
    _waitingForLogin = true;
    return true;
  } else {
    snprintf(_response, sizeof(_response), "Send failed.\nCheck contact path.");
    _responseLen = strlen(_response);
    _state = STATE_ERROR;
    return true;
  }
}

inline bool RepeaterAdminScreen::executeMenuCommand(MenuItem item) {
  if (_contactIdx < 0) return false;

  const char* cmd = menuCommand(item);
  if (cmd[0] == '\0') return false;

  if (the_mesh.uiSendCliCommand(_contactIdx, cmd)) {
    _state = STATE_COMMAND_PENDING;
    _cmdSentAt = millis();
    _response[0] = '\0';
    _responseLen = 0;
    _responseScroll = 0;
    return true;
  } else {
    snprintf(_response, sizeof(_response), "Send failed.");
    _responseLen = strlen(_response);
    _state = STATE_ERROR;
    return true;
  }
}