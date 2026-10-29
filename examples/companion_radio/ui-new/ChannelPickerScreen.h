#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "MeckLang.h"
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include "ChannelScreen.h"
#ifdef MECK_UI_V2
  #include "ui2/Ui2Chrome.h"
  #include "ui2/Ui2List.h"
#endif

#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 20
#endif

class UITask;  // Forward declaration
class MyMesh;  // Forward declaration
extern MyMesh the_mesh;

// ---------------------------------------------------------------------------
// ChannelPickerScreen
// ---------------------------------------------------------------------------
// A directory-style screen that lists every group channel + the DM inbox,
// each with an unread-message badge.  Selecting an entry jumps to the channel
// messages screen pre-targeted at that channel.
//
// Replaces the A/D channel-cycling model in ChannelScreen.  Pressing A or D
// from the messages screen now opens the
// picker instead of paging one channel at a time.
//
// Rendering:
//   T-Deck Pro / MAX : vertical list with "> " cursor, unread badge, right-
//                      aligned.  Same highlight/tap convention as Contacts.
//
// Delete history:
//   Press X on a highlighted channel to enter delete confirmation mode.
//   Confirmation overlay asks the user to press Enter to confirm or Shift+Del to
//   cancel.  On confirm, all messages for that channel are invalidated in
//   the circular buffer and persisted to SD.
//
// Navigation signals use a wantsExit() flag (same pattern as PathEditor) --
// UITask is only forward-declared, so the picker cannot call UITask methods
// directly.  main.cpp / UITask.cpp check the flag after injectKey().
// ---------------------------------------------------------------------------

class ChannelPickerScreen : public UIScreen {
  UITask* _task;
  ChannelScreen* _channelScreen;

  // Ordered list of items.
  // Index 0 is always the DM inbox (channel_idx == 0xFF).
  // Remaining entries are populated group channels in ascending slot order.
  uint8_t _items[MAX_GROUP_CHANNELS + 1];
  int _itemCount;

  int _cursor;
  int _scrollTop;  // Scroll offset (T-Deck Pro list only)

  // Delete confirmation sub-menu
  bool _confirmDelete;  // True when showing "Delete history?" overlay

  // Rebuild the items list from MyMesh.  O(20), safe every render.
  void rebuildItems() {
    int n = 0;
    uint8_t tmp[MAX_GROUP_CHANNELS + 1];
    tmp[n++] = 0xFF;  // DM inbox always first
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        if (n < MAX_GROUP_CHANNELS + 1) tmp[n++] = i;
      }
    }
    memcpy(_items, tmp, n);
    _itemCount = n;
    if (_cursor >= _itemCount) _cursor = _itemCount - 1;
    if (_cursor < 0) _cursor = 0;
  }

  void getItemName(int idx, char* buf, size_t bufLen) const {
    if (idx < 0 || idx >= _itemCount || bufLen == 0) { if (bufLen) buf[0] = '\0'; return; }
    uint8_t c = _items[idx];
    if (c == 0xFF) {
      strncpy(buf, MECK_TR("Direct Messages", "Messages priv\xC3\xA9s"), bufLen - 1);
      buf[bufLen - 1] = '\0';
      return;
    }
    ChannelDetails ch;
    if (the_mesh.getChannel(c, ch) && ch.name[0] != '\0') {
      strncpy(buf, ch.name, bufLen - 1);
      buf[bufLen - 1] = '\0';
    } else {
      snprintf(buf, bufLen, MECK_TR("Ch %d", "Canal %d"), (int)c);
    }
  }

  int getItemUnread(int idx) const {
    if (idx < 0 || idx >= _itemCount || !_channelScreen) return 0;
    return _channelScreen->getUnreadForChannel(_items[idx]);
  }

public:
  ChannelPickerScreen(UITask* task)
    : _task(task), _channelScreen(nullptr),
      _itemCount(0), _cursor(0), _scrollTop(0),
      _confirmDelete(false),
      _wantExit(false) {
    _items[0] = 0xFF;
  }

  void setChannelScreen(ChannelScreen* cs) { _channelScreen = cs; }

  // --- wantsExit flag -- checked by main.cpp / UITask after injectKey() ---
  bool _wantExit;
  bool wantsExit() const { return _wantExit; }

  // Called by UITask::gotoChannelPickerScreen().
  void enter(uint8_t currentChannelIdx) {
    rebuildItems();
    _cursor = 0;
    for (int i = 0; i < _itemCount; i++) {
      if (_items[i] == currentChannelIdx) { _cursor = i; break; }
    }
    _scrollTop = 0;
    _confirmDelete = false;
    _wantExit = false;
  }

  bool isConfirmingDelete() const { return _confirmDelete; }

  uint8_t getSelectedChannel() const {
    if (_cursor >= 0 && _cursor < _itemCount) return _items[_cursor];
    return 0xFF;
  }

  // -----------------------------------------------------------------------
  // Render
  // -----------------------------------------------------------------------
#ifdef MECK_UI_V2
  // UI v2 (design B4): one 40 px row per channel with the newest message as
  // preview, its age and the unread count; pages of 6 instead of scrolling.
  int renderV2(DisplayDriver& display) {
    using namespace ui2;
    Ui2Canvas c(display);
    Ui2Chrome::statusHeader(c);
    char right[16];
    Ui2List::pageLabel(right, sizeof(right), _cursor, _itemCount, ROWS_PER_PAGE);
    Ui2Chrome::titleBar(c, MECK_TR("Channels", "Canaux"), right);

    const uint32_t now = the_mesh.getRTCClock()->getCurrentTime();
    const int start = Ui2List::pageStart(_cursor, ROWS_PER_PAGE);
    for (int i = start; i < _itemCount && i < start + ROWS_PER_PAGE; i++) {
      char name[32], age[8], badge[6];
      getItemName(i, name, sizeof(name));
      Ui2Row r;
      r.icon = (_items[i] == 0xFF) ? UI2_IC_MSG : UI2_IC_HASH;
      r.title = name;
      const ChannelScreen::ChannelMessage* m =
          _channelScreen ? _channelScreen->getChannelMsgByRecency(_items[i], 0) : nullptr;
      r.sub = m ? m->text : MECK_TR("No messages yet", "Aucun message");
      if (m && m->timestamp > 1700000000 && now >= m->timestamp) {
        Ui2List::formatAge(age, sizeof(age), (int32_t)(now - m->timestamp));
        r.right = age;
      }
      int unread = getItemUnread(i);
      if (unread > 0) {
        if (unread > 99) strcpy(badge, "99+"); else snprintf(badge, sizeof(badge), "%d", unread);
        r.badge = badge;
      }
      r.rightMax = 40;
      r.selected = (i == _cursor);
      Ui2List::drawRow(c, BODY_Y + (i - start) * ROW_H, r);
    }

    if (_confirmDelete) {
      char name[32];
      getItemName(_cursor, name, sizeof(name));
      Ui2List::dialog(c, UI2_IC_DEL, MECK_TR("Delete message history?", "Supprimer l'historique ?"), name,
                      MECK_TR("Cancel", "Annuler"), MECK_TR("Delete", "Supprimer"));
      const Ui2Key keys[] = { ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Delete", "Supprimer")), ui2Key("Q", MECK_TR("Cancel", "Annuler")) };
      Ui2Chrome::footer(c, keys, 2);
    } else {
      const Ui2Key keys[] = {
        ui2KeyIcon(UI2_IC_ENTER, MECK_TR("Open", "Ouvrir")),
        ui2Key("W/S", MECK_TR("Move", "D\xC3\xA9placer"), true),
        ui2Key("X", MECK_TR("Clear", "Vider")),
        ui2Key("Q", MECK_TR("Back", "Retour")),
        ui2Key("A/D", MECK_TR("Page", "Page"), true),
      };
      Ui2Chrome::footer(c, keys, _itemCount > ROWS_PER_PAGE ? 5 : 4);
    }
    return 5000;
  }

  // Touch hit test in physical pixels. Returns 0 = miss, 1 = cursor moved,
  // 2 = activate (tap on the selected row), like selectAtVxVy().
  int selectAtPx(int px, int py) {
    (void)px;
    if (_confirmDelete) { _confirmDelete = false; return 1; }
    int row = Ui2List::rowAt(py, ui2::BODY_Y, ui2::ROW_H, ui2::ROWS_PER_PAGE);
    if (row < 0) return 0;
    int idx = Ui2List::pageStart(_cursor, ui2::ROWS_PER_PAGE) + row;
    if (idx >= _itemCount) return 0;
    if (idx == _cursor) return 2;
    _cursor = idx;
    return 1;
  }
#endif

  int render(DisplayDriver& display) override {
    rebuildItems();
#ifdef MECK_UI_V2
    return renderV2(display);
#endif

    // === Header ===
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.print(MECK_TR("Channels", "Canaux"));

    int totalUnread = 0;
    for (int i = 0; i < _itemCount; i++) totalUnread += getItemUnread(i);
    char tmp[24];
    if (totalUnread > 0) {
      snprintf(tmp, sizeof(tmp), "*%d", totalUnread);
    } else {
      snprintf(tmp, sizeof(tmp), "[%d]", _itemCount);
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 6, 0);
    display.print(tmp);
    display.drawRect(0, 11, display.width(), 1);

    // =================================================================
    // T-Deck Pro / MAX: Vertical list
    // Uses NodePrefs font helpers for large_font compatibility.
    // =================================================================
    NodePrefs* prefs = the_mesh.getNodePrefs();
    int lineH = prefs->smallLineH();
    const int headerH = 14;
    const int footerH = 14;
    int maxY = display.height() - footerH;
    int y = headerH;
    int maxVisible = (maxY - headerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;

    // Centre scroll window on cursor
    _scrollTop = max(0, min(_cursor - maxVisible / 2, _itemCount - maxVisible));
    if (_scrollTop < 0) _scrollTop = 0;
    int endIdx = min(_itemCount, _scrollTop + maxVisible);

    display.setTextSize(prefs->smallTextSize());

    for (int i = _scrollTop; i < endIdx && y + lineH <= maxY; i++) {
      bool selected = (i == _cursor);
      int unread = getItemUnread(i);

      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + prefs->smallHighlightOff(), display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      display.setCursor(0, y);

      // Prefix: "> " for selected, "  " otherwise.  "*N" badge if unread.
      char prefix[8];
      if (unread > 0) {
        snprintf(prefix, sizeof(prefix), "%s*%d ", selected ? ">" : " ", unread);
      } else {
        snprintf(prefix, sizeof(prefix), "%s  ", selected ? ">" : " ");
      }
      display.print(prefix);

      // Name
      char name[32];
      getItemName(i, name, sizeof(name));
      char filtered[32];
      display.translateUTF8ToBlocks(filtered, name, sizeof(filtered));

      int nameX = display.getTextWidth(prefix) + 2;
      int nameMaxW = display.width() - nameX - 2;
      display.drawTextEllipsized(nameX, y, nameMaxW, filtered);

      y += lineH;
    }

    // Scroll indicator
    if (_itemCount > maxVisible) {
      const int sbW = 3;
      int sbX = display.width() - sbW;
      int sbTop = headerH;
      int sbHeight = maxY - headerH;
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(sbX, sbTop, sbW, sbHeight);
      int thumbH = (maxVisible * sbHeight) / _itemCount;
      if (thumbH < 4) thumbH = 4;
      int maxScroll = _itemCount - maxVisible;
      if (maxScroll < 1) maxScroll = 1;
      int thumbY = sbTop + (_scrollTop * (sbHeight - thumbH)) / maxScroll;
      display.fillRect(sbX + 1, thumbY + 1, sbW - 2, thumbH - 2);
    }

    // =================================================================
    // Delete confirmation overlay
    // Drawn on top of the list when _confirmDelete is active.
    // =================================================================
    if (_confirmDelete) {
      // Clear a centred box and draw a border
      int boxW = display.width() - 16;
      int boxH = 42;
      int boxX = 8;
      int boxY = (display.height() - boxH) / 2;

      // Clear the box area
      display.setColor(DisplayDriver::DARK);
      display.fillRect(boxX, boxY, boxW, boxH);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(boxX, boxY, boxW, boxH);
      display.drawRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2);

      // Channel name
      display.setTextSize(1);
      char name[32];
      getItemName(_cursor, name, sizeof(name));
      char filtered[32];
      display.translateUTF8ToBlocks(filtered, name, sizeof(filtered));

      display.setColor(DisplayDriver::GREEN);
      display.drawTextEllipsized(boxX + 4, boxY + 5, boxW - 8, filtered);

      // "Delete history?" prompt
      display.setColor(DisplayDriver::LIGHT);
      const char* prompt = MECK_TR("Delete message history?", "Supprimer l'historique ?");
      display.setCursor(boxX + 4, boxY + 17);
      display.print(prompt);

      // Key hints
      display.setColor(DisplayDriver::YELLOW);
      const char* hints = MECK_TR("Enter:Yes  Q:Cancel", "Entr\xC3\xA9" "e:Oui  Q:Annuler");
      display.setCursor(boxX + 4, boxY + 29);
      display.print(hints);
    }

    // === Footer ===
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

    if (_confirmDelete) {
      display.print(MECK_TR("Enter:Yes Q:Cancel", "Entr\xC3\xA9" "e:Oui Q:Annuler"));
    } else {
      display.print("W/S:Nav Q:X");
      const char* rt = MECK_TR("Ent:Open", "Ent:Ouvrir");
      display.setCursor(display.width() - display.getTextWidth(rt) - 6, footerY);
      display.print(rt);
    }

#ifdef USE_EINK
    return 5000;
#else
    return 1000;
#endif
  }

  // -----------------------------------------------------------------------
  // Input
  // -----------------------------------------------------------------------
  bool handleInput(char c) override {
    // --- Delete confirmation mode ---
    if (_confirmDelete) {
      // Enter -- confirm deletion
      if (c == '\r' || c == 13 || c == KEY_ENTER || c == KEY_SELECT) {
        if (_channelScreen && _cursor >= 0 && _cursor < _itemCount) {
          int cleared = _channelScreen->clearHistoryForChannel(_items[_cursor]);
          char name[32];
          getItemName(_cursor, name, sizeof(name));
          Serial.printf("ChannelPicker: Deleted %d messages for '%s'\n", cleared, name);
        }
        _confirmDelete = false;
        return true;
      }
      // Shift+Del -- cancel
      if (c == KEY_CANCEL) {
        _confirmDelete = false;
        return true;
      }
      // Consume all other keys while confirmation is showing
      return true;
    }

    // --- Normal picker mode ---

    // W / UP
    if (c == 'w' || c == 'W' || c == 0xF2 || c == KEY_UP) {
      if (_cursor > 0) { _cursor--; return true; }
      return false;
    }

    // S / DOWN
    if (c == 's' || c == 'S' || c == 0xF1 || c == KEY_DOWN) {
      if (_cursor < _itemCount - 1) { _cursor++; return true; }
      return false;
    }

    // A / D -- a page back / forward in UI v2; consumed otherwise (no
    // channel cycling from the picker)
    if (c == 'a' || c == 'A' || c == KEY_LEFT || c == 'd' || c == 'D' || c == KEY_RIGHT) {
#ifdef MECK_UI_V2
      if (_itemCount > 0) {
        bool back = (c == 'a' || c == 'A' || c == KEY_LEFT);
        _cursor = Ui2List::pageJump(_cursor, _itemCount, ui2::ROWS_PER_PAGE, back ? -1 : 1);
      }
#endif
      return true;
    }

    // X -- delete message history for highlighted channel
    if (c == 'x' || c == 'X') {
      if (_cursor >= 0 && _cursor < _itemCount) {
        _confirmDelete = true;
      }
      return true;
    }

    // Enter -- select the highlighted channel and signal exit
    if (c == '\r' || c == 13 || c == KEY_ENTER || c == KEY_SELECT) {
      if (_channelScreen && _cursor >= 0 && _cursor < _itemCount) {
        _channelScreen->setViewChannelIdx(_items[_cursor]);
      }
      _wantExit = true;
      return true;  // Consumed -- caller checks wantsExit() and navigates
    }

    // Shift+Del -- cancel without changing channel, signal exit
    if (c == KEY_CANCEL) {
      _wantExit = true;
      return true;
    }

    return false;
  }

  // -----------------------------------------------------------------------
  // Touch hit test (virtual coordinates)
  // Returns: 0=miss, 1=cursor moved, 2=activate.
  // T-Deck Pro list: 1st tap -> 1 (highlight), 2nd tap same row -> 2.
  // -----------------------------------------------------------------------
  int selectAtVxVy(int vx, int vy) {
    // If delete confirmation is showing:
    //   T-Deck Pro: tap = cancel (dismiss overlay, stay on picker)
    if (_confirmDelete) {
      _confirmDelete = false;
      return 1;  // Cancel — redraw without activating
    }

    // T-Deck Pro / MAX list hit test -- uses NodePrefs for large_font compatibility
    NodePrefs* prefs = the_mesh.getNodePrefs();
    int lineH = prefs->smallLineH();
    const int headerH = 14;
    const int footerH = 14;
    int bodyTop = headerH + prefs->smallHighlightOff();
    if (vy < bodyTop || vy >= 128 - footerH) return 0;
    int maxVisible = (128 - headerH - footerH) / lineH;
    if (maxVisible < 3) maxVisible = 3;
    int startIdx = max(0, min(_cursor - maxVisible / 2, _itemCount - maxVisible));
    if (startIdx < 0) startIdx = 0;
    int tappedRow = startIdx + (vy - bodyTop) / lineH;
    if (tappedRow < 0 || tappedRow >= _itemCount) return 0;
    if (tappedRow == _cursor) return 2;
    _cursor = tappedRow;
    return 1;
  }
};