#pragma once

#include <Arduino.h>
#include <Wire.h>

// TCA8418 Register addresses
#define TCA8418_REG_CFG         0x01
#define TCA8418_REG_INT_STAT    0x02
#define TCA8418_REG_KEY_LCK_EC  0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_KP_GPIO1    0x1D
#define TCA8418_REG_KP_GPIO2    0x1E
#define TCA8418_REG_KP_GPIO3    0x1F
#define TCA8418_REG_DEBOUNCE    0x29
#define TCA8418_REG_GPI_EM1     0x20
#define TCA8418_REG_GPI_EM2     0x21
#define TCA8418_REG_GPI_EM3     0x22

// Key codes for special keys
#define KB_KEY_NONE      0
#define KB_KEY_BACKSPACE '\b'
#define KB_KEY_ENTER     '\r'
#define KB_KEY_SPACE     ' '
#define KB_KEY_EMOJI     0x01   // Non-printable code for $ key (emoji picker)

class TCA8418Keyboard {
private:
  uint8_t _addr;
  TwoWire* _wire;
  bool _initialized;
  bool _shiftActive;   // Sticky shift (one-shot)
  bool _altActive;     // Sticky alt (one-shot)
  bool _symActive;     // Sticky sym (one-shot)
  unsigned long _lastShiftTime;  // For Shift+key combos

  uint8_t readReg(uint8_t reg) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->endTransmission();
    _wire->requestFrom(_addr, (uint8_t)1);
    return _wire->available() ? _wire->read() : 0;
  }

  void writeReg(uint8_t reg, uint8_t val) {
    _wire->beginTransmission(_addr);
    _wire->write(reg);
    _wire->write(val);
    _wire->endTransmission();
  }

  // Map raw key codes to characters (from working reader firmware)
  char getKeyChar(uint8_t keyCode) {
    switch (keyCode) {
      // Row 1 - QWERTYUIOP
      case 10: return 'q';   // Q (was 97 on different hardware)
      case 9:  return 'w';
      case 8:  return 'e';
      case 7:  return 'r';
      case 6:  return 't';
      case 5:  return 'y';
      case 4:  return 'u';
      case 3:  return 'i';
      case 2:  return 'o';
      case 1:  return 'p';
      
      // Row 2 - ASDFGHJKL + Backspace
      case 20: return 'a';   // A (was 98 on different hardware)
      case 19: return 's';
      case 18: return 'd';
      case 17: return 'f';
      case 16: return 'g';
      case 15: return 'h';
      case 14: return 'j';
      case 13: return 'k';
      case 12: return 'l';
      case 11: return '\b'; // Backspace
      
      // Row 3 - Alt ZXCVBNM Sym Enter
      case 30: return 0;    // Alt - handled separately
      case 29: return 'z';
      case 28: return 'x';
      case 27: return 'c';
      case 26: return 'v';
      case 25: return 'b';
      case 24: return 'n';
      case 23: return 'm';
      case 22: return 0;    // Symbol key - handled separately
      case 21: return '\r'; // Enter
      
      // Row 4 - Shift Mic Space Sym Shift
      case 35: return 0;    // Left shift - handled separately
      case 34: return 0;    // Mic
      case 33: return ' ';  // Space
      case 32: return 0;    // Sym - handled separately
      case 31: return 0;    // Right shift - handled separately
      
      default: return 0;
    }
  }

  // Map key with Alt modifier - same as Sym for this keyboard
  char getAltChar(uint8_t keyCode) {
    return getSymChar(keyCode);  // Alt does same as Sym
  }

  // Map key with Sym modifier - based on actual T-Deck Pro keyboard silk-screen
  char getSymChar(uint8_t keyCode) {
    switch (keyCode) {
      // Row 1: Q W E R T Y U I O P
      case 10: return '#';  // Q -> #
      case 9:  return '1';  // W -> 1
      case 8:  return '2';  // E -> 2
      case 7:  return '3';  // R -> 3
      case 6:  return '(';  // T -> (
      case 5:  return ')';  // Y -> )
      case 4:  return '_';  // U -> _
      case 3:  return '-';  // I -> -
      case 2:  return '+';  // O -> +
      case 1:  return '@';  // P -> @
      
      // Row 2: A S D F G H J K L
      case 20: return '*';  // A -> *
      case 19: return '4';  // S -> 4
      case 18: return '5';  // D -> 5
      case 17: return '6';  // F -> 6
      case 16: return '/';  // G -> /
      case 15: return ':';  // H -> :
      case 14: return ';';  // J -> ;
      case 13: return '\''; // K -> '
      case 12: return '"';  // L -> "
      
      // Row 3: Z X C V B N M
      case 29: return '7';  // Z -> 7
      case 28: return '8';  // X -> 8
      case 27: return '9';  // C -> 9
      case 26: return '?';  // V -> ?
      case 25: return '!';  // B -> !
      case 24: return ',';  // N -> ,
      case 23: return '.';  // M -> .
      
      // Row 4: Mic key -> 0
      case 34: return '0';  // Mic -> 0
      
      default: return 0;
    }
  }

public:
  TCA8418Keyboard(uint8_t addr = 0x34, TwoWire* wire = &Wire) 
    : _addr(addr), _wire(wire), _initialized(false), 
      _shiftActive(false), _altActive(false), _symActive(false), _lastShiftTime(0) {}

  bool begin() {
    // Check if device responds
    _wire->beginTransmission(_addr);
    if (_wire->endTransmission() != 0) {
      Serial.println("TCA8418: Device not found");
      return false;
    }

    // Configure keyboard matrix (8 rows x 10 cols)
    writeReg(TCA8418_REG_KP_GPIO1, 0xFF);  // Rows 0-7 as keypad
    writeReg(TCA8418_REG_KP_GPIO2, 0xFF);  // Cols 0-7 as keypad
    writeReg(TCA8418_REG_KP_GPIO3, 0x03);  // Cols 8-9 as keypad

    // Enable keypad with FIFO overflow detection
    writeReg(TCA8418_REG_CFG, 0x11);  // KE_IEN + INT_CFG

    // Set debounce
    writeReg(TCA8418_REG_DEBOUNCE, 0x03);

    // Clear any pending interrupts
    writeReg(TCA8418_REG_INT_STAT, 0x1F);

    // Flush the FIFO
    while (readReg(TCA8418_REG_KEY_LCK_EC) & 0x0F) {
      readReg(TCA8418_REG_KEY_EVENT_A);
    }

    _initialized = true;
    Serial.println("TCA8418: Keyboard initialized OK");
    return true;
  }

  // Read a key press - returns character or 0 if no key
  char readKey() {
    if (!_initialized) return 0;

    // Check for key events in FIFO
    uint8_t keyCount = readReg(TCA8418_REG_KEY_LCK_EC) & 0x0F;
    if (keyCount == 0) return 0;

    // Read key event from FIFO
    uint8_t keyEvent = readReg(TCA8418_REG_KEY_EVENT_A);

    // Bit 7: 1 = press, 0 = release
    bool pressed = (keyEvent & 0x80) != 0;
    uint8_t keyCode = keyEvent & 0x7F;

    // Clear interrupt
    writeReg(TCA8418_REG_INT_STAT, 0x1F);

    Serial.printf("KB raw: event=0x%02X code=%d pressed=%d count=%d\n", 
                  keyEvent, keyCode, pressed, keyCount);

    // Only act on key press, not release
    if (!pressed || keyCode == 0) {
      return 0;
    }

    // Handle modifier keys - set sticky state and return 0
    if (keyCode == 35 || keyCode == 31) {  // Shift keys
      _shiftActive = true;
      _lastShiftTime = millis();
      Serial.println("KB: Shift activated");
      return 0;
    }
    if (keyCode == 30) {  // Alt key
      _altActive = true;
      Serial.println("KB: Alt activated");
      return 0;
    }
    if (keyCode == 32) {  // Sym key (bottom row)
      _symActive = true;
      Serial.println("KB: Sym activated");
      return 0;
    }
    
    // Handle dedicated $ key (key code 22, next to M)
    // Bare press = emoji picker, Sym+$ = literal '$'
    if (keyCode == 22) {
      if (_symActive) {
        _symActive = false;
        Serial.println("KB: Sym+$ -> '$'");
        return '$';
      }
      Serial.println("KB: $ key -> emoji");
      return KB_KEY_EMOJI;
    }
    
    // Handle Mic key - produces 0 with Sym, otherwise ignore
    if (keyCode == 34) {
      if (_symActive) {
        _symActive = false;
        Serial.println("KB: Sym+Mic -> '0'");
        return '0';
      }
      return 0;  // Ignore mic without Sym
    }

    // Get the character
    char c = 0;
    
    if (_altActive) {
      c = getAltChar(keyCode);
      _altActive = false;  // Reset sticky alt
      if (c != 0) {
        Serial.printf("KB: Alt+key -> '%c'\n", c);
        return c;
      }
    }
    
    if (_symActive) {
      c = getSymChar(keyCode);
      _symActive = false;  // Reset sticky sym
      if (c != 0) {
        Serial.printf("KB: Sym+key -> '%c'\n", c);
        return c;
      }
    }
    
    c = getKeyChar(keyCode);
    
    if (c != 0 && _shiftActive) {
      // Apply shift - uppercase letters
      if (c >= 'a' && c <= 'z') {
        c = c - 'a' + 'A';
      }
      _shiftActive = false;  // Reset sticky shift
    }
    
    if (c != 0) {
      Serial.printf("KB: code %d -> '%c' (0x%02X)\n", keyCode, c >= 32 ? c : '?', c);
    } else {
      Serial.printf("KB: code %d -> UNMAPPED\n", keyCode);
    }
    
    return c;
  }

  bool isReady() const { return _initialized; }
  
  // Check if shift was pressed within the last N milliseconds
  bool wasShiftRecentlyPressed(unsigned long withinMs = 500) const {
    return (millis() - _lastShiftTime) < withinMs;
  }
};