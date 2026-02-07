# Text Reader Integration for Meck Firmware

## Overview

This adds a text reader accessible via the **R** key from the home screen.

**Features:**
- Browse `.txt` files from `/books/` folder on SD card
- Word-wrapped text rendering using tiny font (maximum text density)  
- Page navigation with W/S/A/D keys
- Automatic reading position resume (persisted to SD card)
- Index files cached to SD for instant re-opens
- Bookmark indicator (`*`) on files with saved positions
- Compose mode (`C`) still accessible from within reader

**Key Mapping:**
| Context | Key | Action |
|---------|-----|--------|
| Home screen | R | Open text reader |
| File list | W/S | Navigate up/down |
| File list | Enter | Open selected file |
| File list | Q | Back to home screen |
| Reading | W/A | Previous page |
| Reading | S/D/Space/Enter | Next page |
| Reading | Q | Close book → file list |
| Reading | C | Enter compose mode |

---

## Files to Create

### 1. `TextReaderScreen.h`
New file — place alongside `ChannelScreen.h` in your UITask include path.

---

## Files to Modify

### 2. `UITask.h` — 4 additions

**a)** Add screen pointer (after `channel_screen` declaration):
```cpp
UIScreen* text_reader;     // Text reader screen
```

**b)** Add navigation method (in public section):
```cpp
void gotoTextReader();
bool isOnTextReader() const { return curr == text_reader; }
```

**c)** Add accessor (in public section):
```cpp
UIScreen* getTextReaderScreen() const { return text_reader; }
```

### 3. `UITask.cpp` — 3 additions

**a)** Add include (after `#include "ChannelScreen.h"`):
```cpp
#include "TextReaderScreen.h"
```

**b)** In `begin()`, after `channel_screen = new ChannelScreen(this, &rtc_clock);`:
```cpp
text_reader = new TextReaderScreen(this);
```

**c)** Add new method (after `gotoChannelScreen()`):
```cpp
void UITask::gotoTextReader() {
  TextReaderScreen* reader = (TextReaderScreen*)text_reader;
  if (_display != NULL) {
    reader->enter(*_display);
  }
  setCurrScreen(text_reader);
  if (_display != NULL && !_display->isOn()) {
    _display->turnOn();
  }
  _auto_off = millis() + AUTO_OFF_MILLIS;
  _next_refresh = 100;
}
```

### 4. `main.cpp` — 5 changes

**a)** Add includes (near top, within the T-Deck Pro section):
```cpp
#include <SD.h>
#include "TextReaderScreen.h"
extern SPIClass displaySpi;  // From GxEPDDisplay.cpp, shared SPI bus
static bool readerMode = false;
```

**b)** SD card init in `setup()` — add after `initKeyboard()`:
```cpp
#if defined(LilyGo_TDeck_Pro) && defined(HAS_SDCARD)
{
  pinMode(SDCARD_CS, OUTPUT);
  digitalWrite(SDCARD_CS, HIGH);
  if (SD.begin(SDCARD_CS, displaySpi, 4000000)) {
    MESH_DEBUG_PRINTLN("setup() - SD card initialized");
    TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
    if (reader) reader->setSDReady(true);
  } else {
    MESH_DEBUG_PRINTLN("setup() - SD card init failed!");
  }
}
#endif
```

**c)** In `loop()`, track reader state — add after the `ui_task.loop()` block:
```cpp
readerMode = ui_task.isOnTextReader();
```

**d)** In `handleKeyboardInput()`, add reader-mode handler **before** the normal-mode `switch(key)`:
```cpp
// Text reader mode - route keys to reader
if (readerMode) {
  TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
  if (key == 'q' || key == 'Q') {
    if (reader->isReading()) {
      ui_task.injectKey('q');  // Close book → file list
    } else {
      reader->exitReader();
      ui_task.gotoHomeScreen();  // Exit reader → home
    }
    return;
  }
  if (key == 'c' || key == 'C') {
    composeMode = true;
    composeBuffer[0] = '\0';
    composePos = 0;
    drawComposeScreen();
    return;
  }
  ui_task.injectKey(key);  // All other keys → reader
  return;
}
```

**e)** In the normal-mode `switch(key)`, add R key:
```cpp
case 'r':
case 'R':
  Serial.println("Opening text reader");
  ui_task.gotoTextReader();
  break;
```

---

## SD Card Setup

Place `.txt` files in a `/books/` folder on the SD card root. The reader will:
- Auto-create `/books/` if it doesn't exist
- Auto-create `/.indexes/` for page index cache files
- Skip macOS hidden files (`._*`, `.DS_Store`)
- Support up to 50 files

**Index format** is compatible with the standalone reader (version 3), so if you've used the standalone reader previously, bookmarks and indexes will carry over.

---

## Architecture Notes

- The reader renders through the standard `UIScreen::render()` framework, so no special bypass is needed in the main loop (unlike compose mode)
- SD card uses the same HSPI bus as e-ink display and LoRa radio — CS pin management handles contention
- Page content is pre-read from SD into a memory buffer during `handleInput()`, then rendered from buffer during `render()` — this avoids SPI bus conflicts during display refresh
- Layout metrics (chars per line, lines per page) are calculated dynamically from the display driver's font metrics on first entry