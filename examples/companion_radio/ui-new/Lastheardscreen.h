#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/AdvertDataHelpers.h>
#include <MeshCore.h>

extern MyMesh the_mesh;

// ==========================================================================
// Last Heard Screen — passive advert list
// Shows all recently heard nodes from the advert path table, sorted by
// recency. Unlike Discovery (active zero-hop scan), this is purely passive
// — it shows nodes whose adverts have been received over time.
// ==========================================================================
// Display cap — we never need to show all 200 storage entries at once
#define LAST_HEARD_DISPLAY_SIZE 100

class LastHeardScreen : public UIScreen {
  mesh::RTCClock* _rtc;
  int _scrollPos;

  // Local sorted copy of advert paths (PSRAM-allocated, refreshed each render)
  AdvertPath* _entries;
  int _count;

  static char typeChar(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return 'C';
      case ADV_TYPE_REPEATER: return 'R';
      case ADV_TYPE_ROOM:     return 'S';
      case ADV_TYPE_SENSOR:   return 'N';
      default:                return '?';
    }
  }

  // Format age as human-readable string (e.g. "2m", "1h", "3d")
  static void formatAge(uint32_t now, uint32_t timestamp, char* buf, int bufLen) {
    if (timestamp == 0 || now < timestamp) {
      snprintf(buf, bufLen, "---");
      return;
    }
    uint32_t age = now - timestamp;
    if (age < 60)         snprintf(buf, bufLen, "%ds", age);
    else if (age < 3600)  snprintf(buf, bufLen, "%dm", age / 60);
    else if (age < 86400) snprintf(buf, bufLen, "%dh", age / 3600);
    else                  snprintf(buf, bufLen, "%dd", age / 86400);
  }

public:
  LastHeardScreen(mesh::RTCClock* rtc)
    : _rtc(rtc), _scrollPos(0), _count(0) {
    _entries = (AdvertPath*)ps_calloc(LAST_HEARD_DISPLAY_SIZE, sizeof(AdvertPath));
  }

  void resetScroll() { _scrollPos = 0; }

  int getSelectedIdx() const { return _scrollPos; }

  // Check if selected node is already in contacts
  bool isSelectedInContacts() const {
    if (_scrollPos < 0 || _scrollPos >= _count) return false;
    return the_mesh.lookupContactByPubKey(_entries[_scrollPos].pubkey_prefix, 8) != nullptr;
  }

  // Get selected entry (for add/delete operations)
  const AdvertPath* getSelectedEntry() const {
    if (_scrollPos < 0 || _scrollPos >= _count) return nullptr;
    return &_entries[_scrollPos];
  }

  // Tap-to-select: given virtual Y, select row.
  // Returns: 0=miss, 1=moved, 2=tapped current row.
  int selectRowAtVY(int vy) {
    if (_count == 0) return 0;
    const int headerH = 14, footerH = 14, lineH = the_mesh.getNodePrefs()->smallLineH();
#if defined(LilyGo_T5S3_EPaper_Pro)
    const int bodyTop = headerH;
#else
    const int bodyTop = headerH + the_mesh.getNodePrefs()->smallHighlightOff();
#endif
    if (vy < bodyTop || vy >= 128 - footerH) return 0;

    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                              _count - maxVisible));

    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _count) return 0;

    if (tappedRow == _scrollPos) return 2;
    _scrollPos = tappedRow;
    return 1;
  }

  int render(DisplayDriver& display) override {
    // Refresh sorted list from mesh
    _count = the_mesh.getRecentlyHeard(_entries, LAST_HEARD_DISPLAY_SIZE);

    // Filter out empty entries (recv_timestamp == 0)
    int validCount = 0;
    for (int i = 0; i < _count; i++) {
      if (_entries[i].recv_timestamp > 0) validCount++;
      else break;  // sorted by recency, so first zero means rest are empty
    }
    _count = validCount;

    if (_scrollPos >= _count) _scrollPos = max(0, _count - 1);

    uint32_t now = _rtc->getCurrentTime();

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "Last Heard: %d nodes", _count);
    display.print(hdr);

    display.drawRect(0, 11, display.width(), 1);

    // === Body — node rows ===
    display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
    int lineHeight = the_mesh.getNodePrefs()->smallLineH();
    int headerHeight = 14;
    int footerHeight = 14;
    int maxY = display.height() - footerHeight;
    int y = headerHeight;

    if (_count == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(4, 28);
      display.print("No adverts received yet");
      display.setCursor(4, 38);
      display.print("Nodes appear as adverts arrive");
    } else {
      int maxVisible = (maxY - headerHeight) / lineHeight;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                                _count - maxVisible));
      int endIdx = min(_count, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineHeight <= maxY; i++) {
        const AdvertPath& entry = _entries[i];
        bool selected = (i == _scrollPos);

        // Highlight selected row
        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
#if defined(LilyGo_T5S3_EPaper_Pro)
          display.fillRect(0, y, display.width(), lineHeight);
#else
          display.fillRect(0, y + the_mesh.getNodePrefs()->smallHighlightOff(), display.width(), lineHeight);
#endif
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        display.setCursor(0, y);

        // Prefix: cursor + type char
        char prefix[4];
        snprintf(prefix, sizeof(prefix), "%c%c",
                 selected ? '>' : ' ', typeChar(entry.type));
        display.print(prefix);

        // Right side: age + hops + [★] for favourites, [+] for other contacts
        char rightStr[20];
        char ageBuf[8];
        formatAge(now, entry.recv_timestamp, ageBuf, sizeof(ageBuf));

        ContactInfo* ci = the_mesh.lookupContactByPubKey(entry.pubkey_prefix, 8);
        bool inContacts = (ci != nullptr);
        bool isFav = inContacts && (ci->flags & 0x01);
        if (isFav) {
          snprintf(rightStr, sizeof(rightStr), "%s %dh [*]", ageBuf, entry.path_len & 63);
        } else if (inContacts) {
          snprintf(rightStr, sizeof(rightStr), "%s %dh [+]", ageBuf, entry.path_len & 63);
        } else {
          snprintf(rightStr, sizeof(rightStr), "%s %dh", ageBuf, entry.path_len & 63);
        }
        int rightWidth = display.getTextWidth(rightStr) + 2;

        // Name (truncated with ellipsis)
        char filteredName[32];
        display.translateUTF8ToBlocks(filteredName, entry.name, sizeof(filteredName));
        int nameX = display.getTextWidth(prefix) + 2;
        int nameMaxW = display.width() - nameX - rightWidth - 2;
        display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

        // Right-aligned info
        display.setCursor(display.width() - rightWidth, y);
        display.print(rightStr);

        y += lineHeight;
      }
    }

    display.setTextSize(1);

    // === Footer ===
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    display.setCursor(0, footerY);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.print("Swipe:Scroll");
    const char* right = "Tap:Add/Del";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
#else
    display.print("Q:Bk");
    const char* right = "Tap/Ent:Add/Del";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
#endif

    return 5000;  // refresh every 5s to update ages
  }

  bool handleInput(char c) override {
    // Scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_scrollPos > 0) { _scrollPos--; return true; }
      return false;
    }

    // Scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_scrollPos < _count - 1) { _scrollPos++; return true; }
      return false;
    }

    // Enter — handled by main.cpp (needs access to private MyMesh methods)
    // Q — handled by main.cpp (navigation)
    return false;
  }
};