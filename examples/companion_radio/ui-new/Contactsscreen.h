#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

class ContactsScreen : public UIScreen {
public:
  // Filter modes for contact type
  enum FilterMode {
    FILTER_ALL = 0,
    FILTER_CHAT,       // Companions / Chat nodes
    FILTER_REPEATER,
    FILTER_ROOM,       // Room servers
    FILTER_SENSOR,
    FILTER_COUNT       // keep last
  };

private:
  UITask* _task;
  mesh::RTCClock* _rtc;

  int _scrollPos;        // Index into filtered list (top visible row)
  FilterMode _filter;    // Current filter mode

  // Cached filtered contact indices for efficient scrolling
  // We rebuild this on filter change or when entering the screen
  static const int MAX_VISIBLE = 400;  // matches MAX_CONTACTS build flag
  uint16_t _filteredIdx[MAX_VISIBLE];  // indices into contact table
  uint32_t _filteredTs[MAX_VISIBLE];   // cached last_advert_timestamp for sorting
  int _filteredCount;                  // how many contacts match current filter
  bool _cacheValid;

  // How many rows fit on screen (computed during render)
  int _rowsPerPage;

  // --- helpers ---

  static const char* filterLabel(FilterMode f) {
    switch (f) {
      case FILTER_ALL:       return "All";
      case FILTER_CHAT:      return "Chat";
      case FILTER_REPEATER:  return "Rptr";
      case FILTER_ROOM:      return "Room";
      case FILTER_SENSOR:    return "Sens";
      default:               return "?";
    }
  }

  static char typeChar(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return 'C';
      case ADV_TYPE_REPEATER: return 'R';
      case ADV_TYPE_ROOM:     return 'S';  // Server
      default:                return '?';
    }
  }

  bool matchesFilter(uint8_t adv_type) const {
    switch (_filter) {
      case FILTER_ALL:       return true;
      case FILTER_CHAT:      return adv_type == ADV_TYPE_CHAT;
      case FILTER_REPEATER:  return adv_type == ADV_TYPE_REPEATER;
      case FILTER_ROOM:      return adv_type == ADV_TYPE_ROOM;
      case FILTER_SENSOR:    return (adv_type != ADV_TYPE_CHAT &&
                                     adv_type != ADV_TYPE_REPEATER &&
                                     adv_type != ADV_TYPE_ROOM);
      default:               return true;
    }
  }

  void rebuildCache() {
    _filteredCount = 0;
    uint32_t numContacts = the_mesh.getNumContacts();
    ContactInfo contact;
    for (uint32_t i = 0; i < numContacts && _filteredCount < MAX_VISIBLE; i++) {
      if (the_mesh.getContactByIdx(i, contact)) {
        if (matchesFilter(contact.type)) {
          _filteredIdx[_filteredCount] = (uint16_t)i;
          _filteredTs[_filteredCount] = contact.last_advert_timestamp;
          _filteredCount++;
        }
      }
    }
    // Sort by last_advert_timestamp descending (most recently seen first)
    // Simple insertion sort — fine for up to 400 entries on ESP32
    for (int i = 1; i < _filteredCount; i++) {
      uint16_t tmpIdx = _filteredIdx[i];
      uint32_t tmpTs  = _filteredTs[i];
      int j = i - 1;
      while (j >= 0 && _filteredTs[j] < tmpTs) {
        _filteredIdx[j + 1] = _filteredIdx[j];
        _filteredTs[j + 1]  = _filteredTs[j];
        j--;
      }
      _filteredIdx[j + 1] = tmpIdx;
      _filteredTs[j + 1]  = tmpTs;
    }
    _cacheValid = true;
    // Clamp scroll position
    if (_scrollPos >= _filteredCount) {
      _scrollPos = (_filteredCount > 0) ? _filteredCount - 1 : 0;
    }
  }

  // Format seconds-ago as compact string: "3s" "5m" "2h" "4d" "??"
  static void formatAge(char* buf, size_t bufLen, uint32_t now, uint32_t timestamp) {
    if (timestamp == 0) {
      strncpy(buf, "--", bufLen);
      return;
    }
    int secs = (int)(now - timestamp);
    if (secs < 0) secs = 0;
    if (secs < 60) {
      snprintf(buf, bufLen, "%ds", secs);
    } else if (secs < 3600) {
      snprintf(buf, bufLen, "%dm", secs / 60);
    } else if (secs < 86400) {
      snprintf(buf, bufLen, "%dh", secs / 3600);
    } else {
      snprintf(buf, bufLen, "%dd", secs / 86400);
    }
  }

public:
  ContactsScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _scrollPos(0), _filter(FILTER_ALL),
      _filteredCount(0), _cacheValid(false), _rowsPerPage(5) {}

  void invalidateCache() { _cacheValid = false; }

  void resetScroll() {
    _scrollPos = 0;
    _cacheValid = false;
  }

  FilterMode getFilter() const { return _filter; }

  // Get the raw contact table index for the currently highlighted item
  // Returns -1 if no valid selection
  int getSelectedContactIdx() const {
    if (_filteredCount == 0) return -1;
    return _filteredIdx[_scrollPos];
  }

  int render(DisplayDriver& display) override {
    if (!_cacheValid) rebuildCache();

    char tmp[48];

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    snprintf(tmp, sizeof(tmp), "Contacts [%s]", filterLabel(_filter));
    display.print(tmp);

    // Count on right
    snprintf(tmp, sizeof(tmp), "%d/%d", _filteredCount, (int)the_mesh.getNumContacts());
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    // Divider
    display.drawRect(0, 11, display.width(), 1);

    // === Body - contact rows ===
    display.setTextSize(0);  // tiny font for compact rows
    int lineHeight = 9;      // 8px font + 1px gap
    int headerHeight = 14;
    int footerHeight = 14;
    int maxY = display.height() - footerHeight;
    int y = headerHeight;

    uint32_t now = _rtc->getCurrentTime();
    int rowsDrawn = 0;

    if (_filteredCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, y);
      display.print("No contacts");
      display.setCursor(0, y + lineHeight);
      display.print("A/D: Change filter");
    } else {
      // Center visible window around selected item (TextReaderScreen pattern)
      int maxVisible = (maxY - headerHeight) / lineHeight;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                                _filteredCount - maxVisible));
      int endIdx = min(_filteredCount, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineHeight <= maxY; i++) {
        ContactInfo contact;
        if (!the_mesh.getContactByIdx(_filteredIdx[i], contact)) continue;

        bool selected = (i == _scrollPos);

        // Highlight: fill LIGHT rect first, then draw DARK text on top
        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y + 5, display.width(), lineHeight);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        // Set cursor AFTER fillRect so text draws on top of highlight
        display.setCursor(0, y);

        // Prefix: "> " for selected, type char + space for others
        char prefix[4];
        if (selected) {
          snprintf(prefix, sizeof(prefix), ">%c", typeChar(contact.type));
        } else {
          snprintf(prefix, sizeof(prefix), " %c", typeChar(contact.type));
        }
        display.print(prefix);

        // Contact name (truncated to fit)
        char filteredName[32];
        display.translateUTF8ToBlocks(filteredName, contact.name, sizeof(filteredName));

        // Reserve space for hops + age on right side
        char hopStr[6];
        if (contact.out_path_len == 0xFF || contact.out_path_len == 0) {
          strcpy(hopStr, "D");  // direct
        } else {
          snprintf(hopStr, sizeof(hopStr), "%d", contact.out_path_len);
        }

        char ageStr[6];
        formatAge(ageStr, sizeof(ageStr), now, contact.last_advert_timestamp);

        // Build right-side string: "hops age"
        char rightStr[14];
        snprintf(rightStr, sizeof(rightStr), "%sh %s", hopStr, ageStr);
        int rightWidth = display.getTextWidth(rightStr) + 2;

        // Name region: after prefix + small gap, before right info
        int nameX = display.getTextWidth(prefix) + 2;
        int nameMaxW = display.width() - nameX - rightWidth - 2;
        display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

        // Right-aligned: hops + age
        display.setCursor(display.width() - rightWidth, y);
        display.print(rightStr);

        y += lineHeight;
        rowsDrawn++;
      }
      _rowsPerPage = (rowsDrawn > 0) ? rowsDrawn : 1;
    }

    display.setTextSize(1);  // restore for footer

    // === Footer ===
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    // Left: Q:Back
    display.setCursor(0, footerY);
    display.print("Q:Back");

    // Center: A/D:Filter
    const char* mid = "A/D:Filtr";
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);

    // Right: W/S:Scroll
    const char* right = "W/S:Scrll";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);

    return 5000;  // e-ink: next render after 5s
  }

  bool handleInput(char c) override {
    // W - scroll up (previous contact)
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_scrollPos > 0) {
        _scrollPos--;
        return true;
      }
    }

    // S - scroll down (next contact)
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_scrollPos < _filteredCount - 1) {
        _scrollPos++;
        return true;
      }
    }

    // A - previous filter
    if (c == 'a' || c == 'A') {
      _filter = (FilterMode)(((int)_filter + FILTER_COUNT - 1) % FILTER_COUNT);
      _scrollPos = 0;
      _cacheValid = false;
      return true;
    }

    // D - next filter
    if (c == 'd' || c == 'D') {
      _filter = (FilterMode)(((int)_filter + 1) % FILTER_COUNT);
      _scrollPos = 0;
      _cacheValid = false;
      return true;
    }

    // Enter - select contact (future: open RepeaterAdmin for repeaters)
    if (c == 13 || c == KEY_ENTER) {
      // TODO Phase 3: if selected contact is a repeater, open RepeaterAdminScreen
      // For now, just acknowledge the selection
      return true;
    }

    return false;
  }
};