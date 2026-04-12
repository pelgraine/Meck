#pragma once

// Emoji Picker with scrolling grid and scroll bar
// 5 columns, 4 visible rows, scrollable through all 76 emoji
// WASD navigation, Enter to select, $/Q/Backspace to cancel

#include <helpers/ui/DisplayDriver.h>
#include "EmojiSprites.h"

#define EMOJI_PICKER_COLS 5
#define EMOJI_PICKER_VISIBLE_ROWS 4
#define EMOJI_PICKER_TOTAL_ROWS ((EMOJI_COUNT + EMOJI_PICKER_COLS - 1) / EMOJI_PICKER_COLS)

static const char* EMOJI_LABELS[EMOJI_COUNT] = {
  // Faces/emotion
  "Lol",    // 0  joy
  "Sad",    // 1  frown
  "Cry",    // 2  loudly_crying
  "Grim",   // 3  grimace
  "Zany",   // 4  zany_face
  "Cowb",   // 5  cowboy
  // Thumbsup + heart
  "Like",   // 6  thumbsup
  "Love",   // 7  heart
  // Everything else
  "WiFi",   // 8  wireless
  "Inf",    // 9  infinity
  "Rex",    // 10 trex
  "Skul",   // 11 skull
  "Cros",   // 12 cross
  "Bolt",   // 13 lightning
  "Hat",    // 14 tophat
  "Moto",   // 15 motorcycle
  "Leaf",   // 16 seedling
  "AU",     // 17 flag_au
  "Umbr",   // 18 umbrella
  "Eye",    // 19 nazar
  "Glob",   // 20 globe
  "Rad",    // 21 radioactive
  "Cow",    // 22 cow
  "ET",     // 23 alien
  "Inv",    // 24 invader
  "Dagr",   // 25 dagger
  "Mtn",    // 26 mountain
  "End",    // 27 end_arrow
  "Ring",   // 28 hollow_circle
  "Drag",   // 29 dragon
  "Web",    // 30 globe_meridians
  "Eggp",   // 31 eggplant
  "Shld",   // 32 shield
  "Gogl",   // 33 goggles
  "Lzrd",   // 34 lizard
  "Roo",    // 35 kangaroo
  "Fthr",   // 36 feather
  "Sun",    // 37 bright
  "Wave",   // 38 part_alt
  "Boat",   // 39 motorboat
  "Domi",   // 40 domino
  "Dish",   // 41 satellite
  "Pass",   // 42 customs
  "Whl",    // 43 wheel
  "Koal",   // 44 koala
  "Knob",   // 45 control_knobs
  "Pch",    // 46 peach
  "Race",   // 47 racing_car
  "Mous",   // 48 mouse
  "Shrm",   // 49 mushroom
  "Bio",    // 50 biohazard
  "Pnda",   // 51 panda
  "Bang",   // 52 anger
  "DrgF",   // 53 dragon_face
  "Pagr",   // 54 pager
  "Bee",    // 55 bee
  "Bulb",   // 56 bulb
  "Cat",    // 57 cat
  "Flur",   // 58 fleur
  "Moon",   // 59 moon
  "Cafe",   // 60 coffee
  "Toth",   // 61 tooth
  "Prtz",   // 62 pretzel
  "Abac",   // 63 abacus
  "Moai",   // 64 moai
  "Hiii",   // 65 tipping
  "Hedg",   // 66 hedgehog
  "Diam",   // 67 diamond_suit
  "Spde",   // 68 spade_suit
  "Piza",   // 69 pizza
  "Luck",   // 70 four_leaf_clover
  "Cld",    // 71 cloud
  "Rckt",   // 72 rocket
  "HFC",    // 73 passport_control
  "Star",   // 74 eight_spoked_asterisk
  "Sig",    // 75 signal_strength
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
        if (row > 0) {
          cursor -= EMOJI_PICKER_COLS;
        } else {
          // Wrap to last row, same column
          int target = (EMOJI_PICKER_TOTAL_ROWS - 1) * EMOJI_PICKER_COLS + col;
          cursor = (target >= EMOJI_COUNT) ? EMOJI_COUNT - 1 : target;
        }
        break;
      case 's': case 'S': case 0xF1:
        if (cursor + EMOJI_PICKER_COLS < EMOJI_COUNT) {
          cursor += EMOJI_PICKER_COLS;
        } else if (row < EMOJI_PICKER_TOTAL_ROWS - 1) {
          cursor = EMOJI_COUNT - 1;
        } else {
          // Wrap to first row, same column
          cursor = col;
        }
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
      case '\b': case 'q': case 'Q': case KB_KEY_EMOJI:
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