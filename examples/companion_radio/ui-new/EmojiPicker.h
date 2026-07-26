#pragma once

// Emoji Picker with scrolling grid and scroll bar
// 5 columns, 4 visible rows, scrollable through all 79 emoji
// WASD navigation, Enter to select, $/Shift+Del to cancel

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
  "100",    // 5  hundred_points
  "Grin",   // 6  grinning
  "Cowb",   // 7  cowboy
  // Thumbsup + heart
  "Like",   // 8  thumbsup
  "Love",   // 9  heart
  // Everything else
  "WiFi",   // 10 wireless
  "Inf",    // 11 infinity
  "Rex",    // 12 trex
  "Skul",   // 13 skull
  "Cros",   // 14 cross
  "Bolt",   // 15 lightning
  "Hat",    // 16 tophat
  "Moto",   // 17 motorcycle
  "Leaf",   // 18 seedling
  "AU",     // 19 flag_au
  "Umbr",   // 20 umbrella
  "Eye",    // 21 nazar
  "Glob",   // 22 globe
  "Rad",    // 23 radioactive
  "Cow",    // 24 cow
  "ET",     // 25 alien
  "Inv",    // 26 invader
  "Dagr",   // 27 dagger
  "Mtn",    // 28 mountain
  "End",    // 29 end_arrow
  "Ring",   // 30 hollow_circle
  "Drag",   // 31 dragon
  "Web",    // 32 globe_meridians
  "Eggp",   // 33 eggplant
  "Shld",   // 34 shield
  "Gogl",   // 35 goggles
  "Lzrd",   // 36 lizard
  "Roo",    // 37 kangaroo
  "Fthr",   // 38 feather
  "Sun",    // 39 bright
  "Wave",   // 40 part_alt
  "Boat",   // 41 motorboat
  "Domi",   // 42 domino
  "Dish",   // 43 satellite
  "Pass",   // 44 customs
  "Whl",    // 45 wheel
  "Koal",   // 46 koala
  "Knob",   // 47 control_knobs
  "Pch",    // 48 peach
  "Race",   // 49 racing_car
  "Mous",   // 50 mouse
  "Shrm",   // 51 mushroom
  "Bio",    // 52 biohazard
  "Pnda",   // 53 panda
  "Bang",   // 54 anger
  "DrgF",   // 55 dragon_face
  "Pagr",   // 56 pager
  "Bee",    // 57 bee
  "Bulb",   // 58 bulb
  "Cat",    // 59 cat
  "Flur",   // 60 fleur
  "Moon",   // 61 moon
  "Cafe",   // 62 coffee
  "Toth",   // 63 tooth
  "Prtz",   // 64 pretzel
  "Abac",   // 65 abacus
  "Moai",   // 66 moai
  "Hiii",   // 67 tipping
  "Hedg",   // 68 hedgehog
  "Diam",   // 69 diamond_suit
  "Spde",   // 70 spade_suit
  "Piza",   // 71 pizza
  "Luck",   // 72 four_leaf_clover
  "Cld",    // 73 cloud
  "Rckt",   // 74 rocket
  "HFC",    // 75 passport_control
  "Star",   // 76 eight_spoked_asterisk
  "Sig",    // 77 signal_strength
  "Beer",   // 78 beer
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
      case KEY_CANCEL: case KB_KEY_EMOJI:
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