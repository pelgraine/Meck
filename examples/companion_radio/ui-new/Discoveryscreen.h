#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/AdvertDataHelpers.h>
#include <MeshCore.h>

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

class DiscoveryScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  int _scrollPos;
  int _rowsPerPage;

  static char typeChar(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return 'C';
      case ADV_TYPE_REPEATER: return 'R';
      case ADV_TYPE_ROOM:     return 'S';
      case ADV_TYPE_SENSOR:   return 'N';
      default:                return '?';
    }
  }

  static const char* typeLabel(uint8_t adv_type) {
    switch (adv_type) {
      case ADV_TYPE_CHAT:     return "Chat";
      case ADV_TYPE_REPEATER: return "Rptr";
      case ADV_TYPE_ROOM:     return "Room";
      case ADV_TYPE_SENSOR:   return "Sens";
      default:                return "?";
    }
  }

public:
  DiscoveryScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _scrollPos(0), _rowsPerPage(5) {}

  void resetScroll() { _scrollPos = 0; }

  int getSelectedIdx() const { return _scrollPos; }

  int render(DisplayDriver& display) override {
    int count = the_mesh.getDiscoveredCount();
    bool active = the_mesh.isDiscoveryActive();

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    char hdr[32];
    if (active) {
      snprintf(hdr, sizeof(hdr), "Scanning... %d found", count);
    } else {
      snprintf(hdr, sizeof(hdr), "Scan done: %d found", count);
    }
    display.print(hdr);

    // Divider
    display.drawRect(0, 11, display.width(), 1);

    // === Body — discovered node rows ===
    display.setTextSize(0);  // tiny font for compact rows
    int lineHeight = 9;
    int headerHeight = 14;
    int footerHeight = 14;
    int maxY = display.height() - footerHeight;
    int y = headerHeight;
    int rowsDrawn = 0;

    if (count == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(4, 28);
      display.print(active ? "Listening for adverts..." : "No nodes found");
      if (!active) {
        display.setCursor(4, 38);
        display.print("F: Scan again  Q: Back");
      }
    } else {
      // Center visible window around selected item
      int maxVisible = (maxY - headerHeight) / lineHeight;
      if (maxVisible < 3) maxVisible = 3;
      int startIdx = max(0, min(_scrollPos - maxVisible / 2,
                                count - maxVisible));
      int endIdx = min(count, startIdx + maxVisible);

      for (int i = startIdx; i < endIdx && y + lineHeight <= maxY; i++) {
        const DiscoveredNode& node = the_mesh.getDiscovered(i);
        bool selected = (i == _scrollPos);

        // Highlight selected row
        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y + 5, display.width(), lineHeight);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        display.setCursor(0, y);

        // Prefix: cursor + type
        char prefix[4];
        if (selected) {
          snprintf(prefix, sizeof(prefix), ">%c", typeChar(node.contact.type));
        } else {
          snprintf(prefix, sizeof(prefix), " %c", typeChar(node.contact.type));
        }
        display.print(prefix);

        // Build right-side info: SNR or hop count + status
        char rightStr[16];
        if (node.snr != 0) {
          // Active discovery result — show SNR in dB (value is ×4 scaled)
          int snr_db = node.snr / 4;
          if (node.already_in_contacts) {
            snprintf(rightStr, sizeof(rightStr), "%ddB [+]", snr_db);
          } else {
            snprintf(rightStr, sizeof(rightStr), "%ddB", snr_db);
          }
        } else {
          // Pre-seeded from cache — show hop count
          if (node.already_in_contacts) {
            snprintf(rightStr, sizeof(rightStr), "%dh [+]", node.path_len);
          } else {
            snprintf(rightStr, sizeof(rightStr), "%dh", node.path_len);
          }
        }
        int rightWidth = display.getTextWidth(rightStr) + 2;

        // Name (truncated with ellipsis)
        char filteredName[32];
        display.translateUTF8ToBlocks(filteredName, node.contact.name, sizeof(filteredName));
        int nameX = display.getTextWidth(prefix) + 2;
        int nameMaxW = display.width() - nameX - rightWidth - 2;
        display.drawTextEllipsized(nameX, y, nameMaxW, filteredName);

        // Right-aligned info
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

    display.setCursor(0, footerY);
    display.print("Q:Back");

    const char* mid = "Ent:Add";
    display.setCursor((display.width() - display.getTextWidth(mid)) / 2, footerY);
    display.print(mid);

    const char* right = "F:Rescan";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);

    // Faster refresh while actively scanning
    return active ? 1000 : 5000;
  }

  bool handleInput(char c) override {
    int count = the_mesh.getDiscoveredCount();

    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_scrollPos > 0) {
        _scrollPos--;
        return true;
      }
    }

    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_scrollPos < count - 1) {
        _scrollPos++;
        return true;
      }
    }

    // F - rescan (handled here as well as in main.cpp for consistency)
    if (c == 'f') {
      the_mesh.startDiscovery();
      _scrollPos = 0;
      return true;
    }

    // Enter - handled by main.cpp for alert feedback

    return false;  // Q/back and Enter handled by main.cpp
  }
};