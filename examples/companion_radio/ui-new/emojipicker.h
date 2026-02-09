#pragma once

// Emoji Picker for compose mode
// Shows a grid of available emoji sprites, navigable with WASD + Enter to select
// Requires EmojiSprites.h to be included before this file

#include <helpers/ui/DisplayDriver.h>
#include "EmojiSprites.h"

// Grid layout: 5 columns x 4 rows = 20 emojis
#define EMOJI_PICKER_COLS 5
#define EMOJI_PICKER_ROWS 4

// Short labels shown alongside sprites for identification
static const char* EMOJI_LABELS[EMOJI_COUNT] = {
  "WiFi",   // 0  🛜
  "Inf",    // 1  ♾️
  "Rex",    // 2  🦖
  "Skul",   // 3  ☠️
  "Cros",   // 4  ✝️
  "Bolt",   // 5  ⚡
  "Hat",    // 6  🎩
  "Moto",   // 7  🏍️
  "Leaf",   // 8  🌱
  "AU",     // 9  🇦🇺
  "Umbr",   // 10 ☂️
  "Eye",    // 11 🧿
  "Glob",   // 12 🌏
  "Rad",    // 13 ☢️
  "Cow",    // 14 🐄
  "ET",     // 15 👽
  "Inv",    // 16 👾
  "Dagr",   // 17 🗡️
  "Grim",   // 18 😬
  "Fone",   // 19 ☎️
};

struct EmojiPicker {
  int cursor;  // 0 to EMOJI_COUNT-1
  
  EmojiPicker() : cursor(0) {}
  
  void reset() { cursor = 0; }
  
  // Returns the emoji escape byte for the selected emoji, or 0 if cancelled
  // Navigate: W=up, S=down, A=left, D=right, Enter=select, Backspace/Q/$=cancel
  uint8_t handleInput(char key) {
    int col = cursor % EMOJI_PICKER_COLS;
    int row = cursor / EMOJI_PICKER_COLS;
    
    switch (key) {
      case 'w': case 'W': case 0xF2:  // Up
        if (row > 0) cursor -= EMOJI_PICKER_COLS;
        return 0;
      case 's': case 'S': case 0xF1:  // Down
        if (row < EMOJI_PICKER_ROWS - 1) cursor += EMOJI_PICKER_COLS;
        return 0;
      case 'a': case 'A':  // Left
        if (col > 0) cursor--;
        else if (row > 0) cursor -= 1;  // Wrap to end of previous row
        return 0;
      case 'd': case 'D':  // Right
        if (cursor + 1 < EMOJI_COUNT) cursor++;
        return 0;
      case '\r':  // Enter - select
        return (uint8_t)(EMOJI_ESCAPE_START + cursor);
      case '\b': case 'q': case 'Q': case '$':  // Cancel
        return 0xFF;  // Sentinel for "cancelled"
      default:
        return 0;  // No action
    }
  }
  
  void draw(DisplayDriver& display) {
    // Header
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.setColor(DisplayDriver::GREEN);
    display.print("Select Emoji");
    
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);
    
    // Grid area
    display.setTextSize(0);  // Tiny font for labels
    
    int startY = 14;
    int cellW = display.width() / EMOJI_PICKER_COLS;
    int cellH = (display.height() - startY - 14) / EMOJI_PICKER_ROWS;  // Leave room for footer
    
    for (int i = 0; i < EMOJI_COUNT; i++) {
      int col = i % EMOJI_PICKER_COLS;
      int row = i / EMOJI_PICKER_COLS;
      int cx = col * cellW;
      int cy = startY + row * cellH;
      
      // Draw selection border around highlighted cell
      if (i == cursor) {
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(cx, cy, cellW, cellH);
        display.drawRect(cx + 1, cy + 1, cellW - 2, cellH - 2);  // Double border for visibility
      }
      
      // Draw sprite centered in cell
      display.setColor(DisplayDriver::LIGHT);
      const uint8_t* sprite = (const uint8_t*)pgm_read_ptr(&EMOJI_SPRITES_LG[i]);
      if (sprite) {
        int spriteX = cx + (cellW - EMOJI_LG_W) / 2;
        int spriteY = cy + 1;
        display.drawXbm(spriteX, spriteY, sprite, EMOJI_LG_W, EMOJI_LG_H);
      }
      
      // Label below sprite
      display.setColor(DisplayDriver::YELLOW);
      
      // Center label text in cell
      uint16_t labelW = display.getTextWidth(EMOJI_LABELS[i]);
      int labelX = cx + (cellW * 7 - labelW) / (7 * 2);  // Approximate centering
      display.setCursor(labelX, cy + 12);
      display.print(EMOJI_LABELS[i]);
    }
    
    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("WASD:Nav Ent:Pick");
    
    const char* cancelText = "$:Back";
    display.setCursor(display.width() - display.getTextWidth(cancelText) - 2, footerY);
    display.print(cancelText);
  }
};