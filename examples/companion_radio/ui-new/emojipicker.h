#pragma once

// Emoji Picker with scrolling grid and scroll bar
// 5 columns, 4 visible rows, scrollable through all 46 emoji
// WASD navigation, Enter to select, $/Q/Backspace to cancel

#include <helpers/ui/DisplayDriver.h>
#include "EmojiSprites.h"

#define EMOJI_PICKER_COLS 5
#define EMOJI_PICKER_VISIBLE_ROWS 4
#define EMOJI_PICKER_TOTAL_ROWS ((EMOJI_COUNT + EMOJI_PICKER_COLS - 1) / EMOJI_PICKER_COLS)

static const char* EMOJI_LABELS[EMOJI_COUNT] = {
  "Lol",    // 0  joy
  "Like",   // 1  thumbsup
  "Sad",    // 2  frown
  "WiFi",   // 3  wireless
  "Inf",    // 4  infinity
  "Rex",    // 5  trex
  "Skul",   // 6  skull
  "Cros",   // 7  cross
  "Bolt",   // 8  lightning
  "Hat",    // 9  tophat
  "Moto",   // 10 motorcycle
  "Leaf",   // 11 seedling
  "AU",     // 12 flag_au
  "Umbr",   // 13 umbrella
  "Eye",    // 14 nazar
  "Glob",   // 15 globe
  "Rad",    // 16 radioactive
  "Cow",    // 17 cow
  "ET",     // 18 alien
  "Inv",    // 19 invader
  "Dagr",   // 20 dagger
  "Grim",   // 21 grimace
  "Mtn",    // 22 mountain
  "End",    // 23 end_arrow
  "Ring",   // 24 hollow_circle
  "Drag",   // 25 dragon
  "Web",    // 26 globe_meridians
  "Eggp",   // 27 eggplant
  "Shld",   // 28 shield
  "Gogl",   // 29 goggles
  "Lzrd",   // 30 lizard
  "Zany",   // 31 zany_face
  "Roo",    // 32 kangaroo
  "Fthr",   // 33 feather
  "Sun",    // 34 bright
  "Wave",   // 35 part_alt
  "Boat",   // 36 motorboat
  "Domi",   // 37 domino
  "Dish",   // 38 satellite
  "Pass",   // 39 customs
  "Cowb",   // 40 cowboy
  "Whl",    // 41 wheel
  "Koal",   // 42 koala
  "Knob",   // 43 control_knobs
  "Pch",    // 44 peach
  "Race",   // 45 racing_car
};

struct EmojiPicker {
  int cursor;
  int scrollRow;
  
  EmojiPicker() : cursor(0), scrollRow(0) {}
  
  void reset() { cursor = 0; scrollRow = 0; }
  
  void ensureVisible() {
    int cursorRow = cursor / EMOJI_PICKER_COLS;
    if (cursorRow < scrollRow) scrollRow = cursorRow;
    else if (cursorRow >= scrollRow + EMOJI_PICKER_VISIBLE_ROWS)
      scrollRow = cursorRow - EMOJI_PICKER_VISIBLE_ROWS + 1;
    int maxScroll = EMOJI_PICKER_TOTAL_ROWS - EMOJI_PICKER_VISIBLE_ROWS;
    if (maxScroll < 0) maxScroll = 0;
    if (scrollRow > maxScroll) scrollRow = maxScroll;
    if (scrollRow < 0) scrollRow = 0;
  }
  
  // Returns emoji escape byte, 0xFF for cancel, 0 for no action
  uint8_t handleInput(char key) {
    int row = cursor / EMOJI_PICKER_COLS;
    int col = cursor % EMOJI_PICKER_COLS;
    
    switch (key) {
      case 'w': case 'W': case 0xF2:
        if (row > 0) cursor -= EMOJI_PICKER_COLS;
        break;
      case 's': case 'S': case 0xF1:
        if (cursor + EMOJI_PICKER_COLS < EMOJI_COUNT)
          cursor += EMOJI_PICKER_COLS;
        else if (row < EMOJI_PICKER_TOTAL_ROWS - 1)
          cursor = EMOJI_COUNT - 1;
        break;
      case 'a': case 'A':
        if (cursor > 0) cursor--;
        break;
      case 'd': case 'D':
        if (cursor + 1 < EMOJI_COUNT) cursor++;
        break;
      case '\r':
        ensureVisible();
        return (uint8_t)(EMOJI_ESCAPE_START + cursor);
      case '\b': case 'q': case 'Q': case '$':
        return 0xFF;
      default:
        return 0;
    }
    ensureVisible();
    return 0;
  }
  
  void draw(DisplayDriver& display) {
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.setColor(DisplayDriver::GREEN);
    display.print("Select Emoji");
    
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);
    
    display.setTextSize(0);
    
    int startY = 14;
    int scrollBarW = 4;
    int gridW = display.width() - scrollBarW - 1;
    int cellW = gridW / EMOJI_PICKER_COLS;
    int footerHeight = 14;
    int gridH = display.height() - startY - footerHeight;
    int cellH = gridH / EMOJI_PICKER_VISIBLE_ROWS;
    
    for (int vr = 0; vr < EMOJI_PICKER_VISIBLE_ROWS; vr++) {
      int absRow = scrollRow + vr;
      if (absRow >= EMOJI_PICKER_TOTAL_ROWS) break;
      
      for (int col = 0; col < EMOJI_PICKER_COLS; col++) {
        int idx = absRow * EMOJI_PICKER_COLS + col;
        if (idx >= EMOJI_COUNT) break;
        
        int cx = col * cellW;
        int cy = startY + vr * cellH;
        
        if (idx == cursor) {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(cx, cy, cellW, cellH);
          display.drawRect(cx + 1, cy + 1, cellW - 2, cellH - 2);
        }
        
        display.setColor(DisplayDriver::LIGHT);
        const uint8_t* sprite = (const uint8_t*)pgm_read_ptr(&EMOJI_SPRITES_LG[idx]);
        if (sprite) {
          int spriteX = cx + (cellW - EMOJI_LG_W) / 2;
          int spriteY = cy + 1;
          display.drawXbm(spriteX, spriteY, sprite, EMOJI_LG_W, EMOJI_LG_H);
        }
        
        display.setColor(DisplayDriver::YELLOW);
        uint16_t labelW = display.getTextWidth(EMOJI_LABELS[idx]);
        int labelX = cx + (cellW - (int)labelW) / 2;
        if (labelX < cx) labelX = cx;
        display.setCursor(labelX, cy + 14);
        display.print(EMOJI_LABELS[idx]);
      }
    }
    
    // Scroll bar
    int sbX = display.width() - scrollBarW;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(sbX, startY, scrollBarW, gridH);
    
    if (EMOJI_PICKER_TOTAL_ROWS > EMOJI_PICKER_VISIBLE_ROWS) {
      int thumbH = (EMOJI_PICKER_VISIBLE_ROWS * gridH) / EMOJI_PICKER_TOTAL_ROWS;
      if (thumbH < 4) thumbH = 4;
      int maxScroll = EMOJI_PICKER_TOTAL_ROWS - EMOJI_PICKER_VISIBLE_ROWS;
      int thumbY = startY + (scrollRow * (gridH - thumbH)) / maxScroll;
      for (int y = thumbY + 1; y < thumbY + thumbH - 1; y++)
        display.drawRect(sbX + 1, y, scrollBarW - 2, 1);
    } else {
      for (int y = startY + 1; y < startY + gridH - 1; y++)
        display.drawRect(sbX + 1, y, scrollBarW - 2, 1);
    }
    
    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("WASD:Nav Ent:Pick");
    const char* ct = "$:Back";
    display.setCursor(display.width() - display.getTextWidth(ct) - 2, footerY);
    display.print(ct);
  }
};