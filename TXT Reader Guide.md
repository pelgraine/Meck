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