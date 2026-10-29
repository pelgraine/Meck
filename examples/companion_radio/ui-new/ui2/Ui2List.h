#pragma once
// =============================================================================
// UI v2 -- list rows, paging, toast, confirm dialog and empty states
//
// Row anatomy (40 px, 1 px bottom rule), left to right:
//   [dot] [16 px icon] title (12B) / subtitle (12)  [star] [signal]
//   right column: value (11) / meta (10) / badge     [toggle] [check] [>]
// Selected rows are inverted with a 2 px bar inside on the left. Pending or
// disabled rows get a dashed outline inset by 3 px; disabled icons are hatched.
// A right value wider than 72 px drops to the subtitle line instead of
// crushing the title.
// =============================================================================

#include "Ui2Canvas.h"

struct Ui2Row {
  Ui2Icon icon = UI2_NO_ICON;
  const char* title = nullptr;
  const char* sub = nullptr;
  const char* right = nullptr;     // value or age ("2h", "Internal")
  const char* right2 = nullptr;    // smaller second line ("0 hops")
  const char* badge = nullptr;     // count pill
  Ui2Icon sig = UI2_NO_ICON;       // signal bars (UI2_IC_SIG0..4)
  int8_t toggle = -1;              // -1 none, 0 off, 1 on
  bool dot = false;                // unread dot before the icon
  bool star = false;
  bool check = false;
  bool chevron = false;
  bool selected = false;
  bool pending = false;
  bool disabled = false;
  bool noRule = false;             // no bottom rule
  int16_t rightMax = 120;          // cap for the right column
  int16_t height = ui2::ROW_H;
};

// Row icon for a MeshCore node type (ADV_TYPE_*: 1 chat, 2 repeater, 3 room, 4 sensor)
inline Ui2Icon ui2NodeIcon(uint8_t type) {
  switch (type) {
    case 2: return UI2_IC_REPEATER;
    case 3: return UI2_IC_ROOMSRV;
    case 4: return UI2_IC_SENSOR;
    default: return UI2_IC_CONTACT;
  }
}

namespace Ui2List {
  void drawRow(Ui2Canvas& c, int16_t y, const Ui2Row& r);

  // Compact age: "45s", "14m", "3h", "2d" (or "-" when unknown/negative)
  void formatAge(char* buf, size_t len, int32_t secs);
  // Label / value line (20 px): label ellipsized, value bold at natural width
  void kvRow(Ui2Canvas& c, int16_t y, const char* label, const char* value);

  // Page-based paging (the design shows "1 / 7" instead of scrolling)
  inline int pageOf(int index, int perPage) { return perPage > 0 ? index / perPage : 0; }
  inline int pageCount(int count, int perPage) { return perPage > 0 ? (count + perPage - 1) / perPage : 1; }
  inline int pageStart(int index, int perPage) { return pageOf(index, perPage) * perPage; }
  // A/D paging: the first row of the page before (dir -1) or after (+1) the
  // one holding index; past either end, the first or last row
  inline int pageJump(int index, int count, int perPage, int dir) {
    int i = pageStart(index, perPage) + dir * perPage;
    if (i > count - 1) i = count - 1;
    return i < 0 ? 0 : i;
  }
  // Writes "p / n" (1-based) into buf; empty when everything fits one page
  void pageLabel(char* buf, size_t len, int index, int count, int perPage);
  // Row under a physical y in a list whose first row starts at `top`; -1 if none
  inline int rowAt(int16_t py, int16_t top, int16_t rowH, int rows) {
    if (py < top) return -1;
    int r = (py - top) / rowH;
    return r < rows ? r : -1;
  }

  // Toast: one inverted 24 px line above the footer (y 268), icon optional.
  // A '\n' in the text makes it a two-line (40 px) toast.
  void toast(Ui2Canvas& c, const char* text, Ui2Icon icon = UI2_NO_ICON);

  // Confirm dialog (208 px wide, 2 px border, 3 px offset shadow): icon and
  // title (2 lines), body (up to 4 lines) and two 32 px buttons, Q = cancel,
  // Enter = confirm.
  void dialog(Ui2Canvas& c, Ui2Icon icon, const char* title, const char* body,
              const char* cancelLabel, const char* confirmLabel);

  // Empty / loading / error state centred in [top, bottom): 32 px icon, one
  // 14B line, up to 2 lines of 12 and an optional action chip ("R Retry").
  void emptyState(Ui2Canvas& c, int16_t top, int16_t bottom, Ui2Icon icon,
                  const char* line, const char* detail = nullptr,
                  const char* actionKey = nullptr, const char* actionLabel = nullptr);

  // Full-screen busy page for splashes drawn outside the normal frame
  // (loading, indexing, saving): clears the screen, then a status header,
  // a 32 px icon, a title, up to 3 lines of detail and, when pct >= 0, a
  // progress bar. withHeader = false leaves the header out (boot).
  void busyPage(Ui2Canvas& c, Ui2Icon icon, const char* title, const char* detail = nullptr,
                int pct = -1, bool withHeader = true);
}
