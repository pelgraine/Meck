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

class TCA8418Keyboard {
private:
  uint8_t _addr;
  TwoWire* _wire;
  bool _initialized;
  bool _shiftActive;   // Sticky shift (one-shot)
  bool _altActive;     // Sticky alt (one-shot)
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
      case 22: return 0;    // Symbol/volume
      case 21: return '\r'; // Enter
      
      // Row 4 - Shift Mic Space Sym Shift
      case 35: return 0;    // Left shift - handled separately
      case 34: return 0;    // Mic
      case 33: return ' ';  // Space
      case 32: return 0;    // Sym
      case 31: return 0;    // Right shift - handled separately
      
      default: return 0;
    }
  }

  // Map key with Alt modifier for numbers/symbols
  char getAltChar(uint8_t keyCode) {
    switch (keyCode) {
      // Top row with Alt -> numbers
      case 10: return '1';  // Q -> 1
      case 9:  return '2';  // W -> 2
      case 8:  return '3';  // E -> 3
      case 7:  return '4';  // R -> 4
      case 6:  return '5';  // T -> 5
      case 5:  return '6';  // Y -> 6
      case 4:  return '7';  // U -> 7
      case 3:  return '8';  // I -> 8
      case 2:  return '9';  // O -> 9
      case 1:  return '0';  // P -> 0
      
      // Common symbols with Alt
      case 20: return '@';  // A -> @
      case 19: return '#';  // S -> #
      case 18: return '$';  // D -> $
      case 17: return '%';  // F -> %
      case 16: return '&';  // G -> &
      case 15: return '*';  // H -> *
      case 14: return '-';  // J -> -
      case 13: return '+';  // K -> +
      case 12: return '=';  // L -> =
      
      // More symbols
      case 29: return '!';  // Z -> !
      case 28: return '?';  // X -> ?
      case 27: return ':';  // C -> :
      case 26: return ';';  // V -> ;
      case 25: return '\''; // B -> '
      case 24: return '"';  // N -> "
      case 23: return ',';  // M -> ,
      
      default: return 0;
    }
  }

public:
  TCA8418Keyboard(uint8_t addr = 0x34, TwoWire* wire = &Wire) 
    : _addr(addr), _wire(wire), _initialized(false), 
      _shiftActive(false), _altActive(false), _lastShiftTime(0) {}

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
    if (keyCode == 22 || keyCode == 32 || keyCode == 34) {  // Sym/Mic - ignore
      return 0;
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