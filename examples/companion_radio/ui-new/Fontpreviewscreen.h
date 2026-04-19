#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>
#include "../NodePrefs.h"
#include "MeckFonts.h"

// Forward declarations
class UITask;
class MyMesh;
extern MyMesh the_mesh;

// ---------------------------------------------------------------------------
// FontPreviewScreen
// ---------------------------------------------------------------------------
// Subscreen opened from Settings → Font Style.  Shows a live preview of each
// font style (Classic, Noto Sans, Montserrat) with sample body and title text.
// The user cycles through styles with W/S (or swipe on T5S3), previews the
// actual rendering on-screen, and applies with Enter or cancels with Q.
//
// The preview works by temporarily calling display.setFontStyle() for each
// sample block during render(), then restoring the original style.
// ---------------------------------------------------------------------------

class FontPreviewScreen : public UIScreen {
  UITask* _task;
  NodePrefs* _prefs;
  uint8_t _cursor;          // Currently highlighted style (0, 1, 2)
  uint8_t _originalStyle;   // Style when screen was opened (for cancel)
  bool _wantExit;
  bool _applied;            // True if user pressed Enter (vs Q cancel)

public:
  FontPreviewScreen(UITask* task, NodePrefs* prefs)
    : _task(task), _prefs(prefs),
      _cursor(0), _originalStyle(0),
      _wantExit(false), _applied(false) {}

  // Called when entering the screen from Settings
  void enter() {
    _originalStyle = _prefs->ui_font_style;
    _cursor = _originalStyle;
    _wantExit = false;
    _applied = false;
  }

  bool wantsExit() const { return _wantExit; }
  bool wasApplied() const { return _applied; }

  // -----------------------------------------------------------------------
  // Render
  // -----------------------------------------------------------------------
  int render(DisplayDriver& display) override {
    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print("Font Style Preview");
    display.drawRect(0, 11, display.width(), 1);

    // === Size mode indicator ===
    {
      const char* sizeLabel = _prefs->large_font ? "[LARGER]" : "[TINY]";
      display.setCursor(display.width() - display.getTextWidth(sizeLabel) - 2, 0);
      display.print(sizeLabel);
    }

    // === Style preview rows ===
    // Each style gets a block showing: style name + sample text rendered in
    // that style.  The selected style is highlighted.
    const int headerH = 14;
    const int footerH = 14;
    const int bodyH = display.height() - headerH - footerH;
    const int rowH = bodyH / MECK_FONT_STYLE_COUNT;

    uint8_t savedStyle = display.getFontStyle();

    for (int i = 0; i < MECK_FONT_STYLE_COUNT; i++) {
      int y = headerH + i * rowH;
      bool selected = (i == _cursor);

      // Highlight selected row
      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y, display.width(), rowH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      // Style name — rendered in default font (size 1)
      display.setFontStyle(MECK_FONT_CLASSIC);  // Name label always in Classic
      display.setTextSize(1);
      display.setCursor(2, y + 1);
      const char* name = meckFontStyleName(i);

      // Prefix with selection indicator
      char label[32];
      if (selected) {
        snprintf(label, sizeof(label), "> %s", name);
      } else {
        snprintf(label, sizeof(label), "  %s", name);
      }
      display.print(label);

      // Sample text — rendered in the preview style
      display.setFontStyle(i);

      // Body text sample (textSize = smallTextSize based on current size pref)
      display.setTextSize(_prefs->smallTextSize());
      int sampleY = y + (rowH / 2) - 2;
      display.setCursor(4, sampleY);
      display.print("The quick brown fox 0123");

      // Title sample (textSize 3) — only if row is tall enough
      if (rowH > 22) {
        display.setTextSize(3);
        int titleY = y + rowH - 6;
        display.setCursor(4, titleY);
        display.print("Meck");
      }
    }

    // Restore original font style
    display.setFontStyle(savedStyle);

    // === Footer ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::YELLOW);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);

#if defined(LilyGo_T5S3_EPaper_Pro)
    display.print("Swipe:Pick");
    const char* rt = "Boot:Back Tap:Apply";
#else
    display.print("W/S:Pick Q:Back");
    const char* rt = "Enter:Apply";
#endif
    display.setCursor(display.width() - display.getTextWidth(rt) - 2, footerY);
    display.print(rt);

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
    // W / UP — previous style
    if (c == 'w' || c == 'W' || c == 0xF2 || c == KEY_UP) {
      if (_cursor > 0) {
        _cursor--;
        return true;
      }
      return false;
    }

    // S / DOWN — next style
    if (c == 's' || c == 'S' || c == 0xF1 || c == KEY_DOWN) {
      if (_cursor < MECK_FONT_STYLE_COUNT - 1) {
        _cursor++;
        return true;
      }
      return false;
    }

    // Enter — apply selected style and exit
    if (c == '\r' || c == 13 || c == KEY_ENTER || c == KEY_SELECT) {
      _prefs->ui_font_style = _cursor;
      the_mesh.savePrefs();
      _applied = true;
      _wantExit = true;
      Serial.printf("FontPreview: Applied style = %s (%d)\n",
                    meckFontStyleName(_cursor), _cursor);
      return true;
    }

    // Q / backspace — cancel, restore original style
    if (c == 'q' || c == 'Q' || c == '\b' || c == KEY_CANCEL) {
      _prefs->ui_font_style = _originalStyle;
      _wantExit = true;
      return true;
    }

    return false;
  }

  // -----------------------------------------------------------------------
  // Touch hit test (virtual coordinates)
  // Returns: 0=miss, 1=cursor moved, 2=activate (apply).
  // -----------------------------------------------------------------------
  int selectAtVxVy(int vx, int vy) {
    const int headerH = 14;
    const int footerH = 14;
    const int bodyH = 128 - headerH - footerH;  // virtual 128×128
    const int rowH = bodyH / MECK_FONT_STYLE_COUNT;

    if (vy < headerH || vy >= 128 - footerH) return 0;

    int tapped = (vy - headerH) / rowH;
    if (tapped < 0 || tapped >= MECK_FONT_STYLE_COUNT) return 0;

    if (tapped == _cursor) return 2;  // Tap same row = apply
    _cursor = tapped;
    return 1;  // Moved cursor
  }
};