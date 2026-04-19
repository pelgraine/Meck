#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ChannelDetails.h>
#include <MeshCore.h>
#include "ChannelScreen.h"

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
// (or swiping left/right on T5S3) from the messages screen now opens the
// picker instead of paging one channel at a time.
//
// Rendering:
//   T5S3 E-Paper Pro : grid of "bubble" tiles (3 cols), with channel name
//                      centered and unread badge.  1-tap opens the channel.
//   T-Deck Pro / MAX : vertical list with "> " cursor, unread badge, right-
//                      aligned.  Same highlight/tap convention as Contacts.
//
// Navigation signals use a wantsExit() flag (same pattern as PathEditor) —
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

  // Grid layout cache (T5S3) — set in render(), consumed by touch hit test
  int _cellW;
  int _cellH;
  int _gridTop;
  int _gridCols;

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
      strncpy(buf, "Direct Messages", bufLen - 1);
      buf[bufLen - 1] = '\0';
      return;
    }
    ChannelDetails ch;
    if (the_mesh.getChannel(c, ch) && ch.name[0] != '\0') {
      strncpy(buf, ch.name, bufLen - 1);
      buf[bufLen - 1] = '\0';
    } else {
      snprintf(buf, bufLen, "Ch %d", (int)c);
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
      _cellW(40), _cellH(12), _gridTop(14), _gridCols(3),
      _wantExit(false) {
    _items[0] = 0xFF;
  }

  void setChannelScreen(ChannelScreen* cs) { _channelScreen = cs; }

  // --- wantsExit flag — checked by main.cpp / UITask after injectKey() ---
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
    _wantExit = false;
  }

  uint8_t getSelectedChannel() const {
    if (_cursor >= 0 && _cursor < _itemCount) return _items[_cursor];
    return 0xFF;
  }

  // -----------------------------------------------------------------------
  // Render
  // -----------------------------------------------------------------------
  int render(DisplayDriver& display) override {
    rebuildItems();

    // === Header ===
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.print("Channels");

    int totalUnread = 0;
    for (int i = 0; i < _itemCount; i++) totalUnread += getItemUnread(i);
    char tmp[24];
    if (totalUnread > 0) {
      snprintf(tmp, sizeof(tmp), "*%d", totalUnread);
    } else {
      snprintf(tmp, sizeof(tmp), "[%d]", _itemCount);
    }
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);
    display.drawRect(0, 11, display.width(), 1);

#if defined(LilyGo_T5S3_EPaper_Pro)
    // =================================================================
    // T5S3: Bubble grid (works in both landscape and portrait)
    // Virtual coords are always 128×128; display driver handles scaling.
    // =================================================================
    const int headerH = 14;
    const int footerH = 14;
    const int cols = 3;
    int rows = (_itemCount + cols - 1) / cols;
    if (rows < 1) rows = 1;
    if (rows > 10) rows = 10;  // Safety cap

    int gridY = headerH;
    int gridH = display.height() - headerH - footerH;
    int cellW = display.width() / cols;
    int cellH = gridH / rows;
    if (cellH > 16) cellH = 16;  // Don't make cells absurdly tall when only 1-2 channels
    if (cellH < 10) cellH = 10;

    _cellW = cellW;
    _cellH = cellH;
    _gridTop = gridY;
    _gridCols = cols;

    const int pad = 2;

    for (int i = 0; i < _itemCount; i++) {
      int col = i % cols;
      int row = i / cols;
      int x = col * cellW + pad;
      int y = gridY + row * cellH + pad;
      int w = cellW - 2 * pad;
      int h = cellH - 2 * pad;

      bool selected = (i == _cursor);
      int unread = getItemUnread(i);

      // Bubble outline / fill
      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(x, y, w, h);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(x, y, w, h);
      }

      // Channel name
      char name[32];
      getItemName(i, name, sizeof(name));
      char filtered[32];
      display.translateUTF8ToBlocks(filtered, name, sizeof(filtered));

      int textY = y + (h - 9) / 2;
      if (textY < y + 1) textY = y + 1;

      // Badge width reservation
      int badgeW = 0;
      char badge[8];
      if (unread > 0) {
        if (unread > 99) snprintf(badge, sizeof(badge), "99+");
        else snprintf(badge, sizeof(badge), "*%d", unread);
        badgeW = display.getTextWidth(badge) + 2;
      }
      int textMaxW = w - 4 - badgeW;
      if (textMaxW < 8) textMaxW = 8;

      // Centre name in space left of badge
      int nameW = display.getTextWidth(filtered);
      if (nameW <= textMaxW) {
        int nameX = x + (w - badgeW - nameW) / 2;
        if (nameX < x + 2) nameX = x + 2;
        display.setCursor(nameX, textY);
        display.print(filtered);
      } else {
        display.drawTextEllipsized(x + 2, textY, textMaxW, filtered);
      }

      // Unread badge
      if (unread > 0) {
        int bx = x + w - badgeW - 1;
        display.setCursor(bx + 1, textY);
        display.print(badge);
      }

      display.setColor(DisplayDriver::LIGHT);
    }

#else
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
#endif

    // === Footer ===
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

#if defined(LilyGo_T5S3_EPaper_Pro)
    display.print("Tap:Open");
    const char* rt = "Boot:Back";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);
#else
    display.print("W/S:Nav Q:Back");
    const char* rt = "Ent:Open";
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);
#endif

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
    // W / UP
    if (c == 'w' || c == 'W' || c == 0xF2 || c == KEY_UP) {
#if defined(LilyGo_T5S3_EPaper_Pro)
      if (_cursor - _gridCols >= 0) { _cursor -= _gridCols; return true; }
#else
      if (_cursor > 0) { _cursor--; return true; }
#endif
      return false;
    }

    // S / DOWN
    if (c == 's' || c == 'S' || c == 0xF1 || c == KEY_DOWN) {
#if defined(LilyGo_T5S3_EPaper_Pro)
      if (_cursor + _gridCols < _itemCount) { _cursor += _gridCols; return true; }
#else
      if (_cursor < _itemCount - 1) { _cursor++; return true; }
#endif
      return false;
    }

    // A / D — grid column navigation on T5S3
    if (c == 'a' || c == 'A' || c == KEY_LEFT) {
#if defined(LilyGo_T5S3_EPaper_Pro)
      if (_cursor > 0 && (_cursor % _gridCols) > 0) { _cursor--; return true; }
#endif
      return true;  // Consume — never cycles off this screen
    }
    if (c == 'd' || c == 'D' || c == KEY_RIGHT) {
#if defined(LilyGo_T5S3_EPaper_Pro)
      if (_cursor < _itemCount - 1 && (_cursor % _gridCols) < _gridCols - 1) {
        _cursor++; return true;
      }
#endif
      return true;
    }

    // Enter — select the highlighted channel and signal exit
    if (c == '\r' || c == 13 || c == KEY_ENTER || c == KEY_SELECT) {
      if (_channelScreen && _cursor >= 0 && _cursor < _itemCount) {
        _channelScreen->setViewChannelIdx(_items[_cursor]);
      }
      _wantExit = true;
      return true;  // Consumed — caller checks wantsExit() and navigates
    }

    // Q / backspace — cancel without changing channel, signal exit
    if (c == 'q' || c == 'Q' || c == '\b' || c == KEY_CANCEL) {
      _wantExit = true;
      return true;
    }

    return false;
  }

  // -----------------------------------------------------------------------
  // Touch hit test (virtual coordinates)
  // Returns: 0=miss, 1=cursor moved, 2=activate.
  // T5S3 bubbles: any tap on a bubble → 2 (direct open).
  // T-Deck Pro list: 1st tap → 1 (highlight), 2nd tap same row → 2.
  // -----------------------------------------------------------------------
  int selectAtVxVy(int vx, int vy) {
#if defined(LilyGo_T5S3_EPaper_Pro)
    // Bubble grid hit test — works in both landscape and portrait (virtual 128×128)
    if (vy < _gridTop || _gridCols == 0 || _cellW == 0 || _cellH == 0) return 0;
    if (vx < 0 || vx >= _gridCols * _cellW) return 0;
    int col = vx / _cellW;
    int row = (vy - _gridTop) / _cellH;
    int idx = row * _gridCols + col;
    if (idx < 0 || idx >= _itemCount) return 0;
    _cursor = idx;
    return 2;  // Direct open
#else
    // T-Deck Pro / MAX list hit test — uses NodePrefs for large_font compatibility
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
#endif
  }
};