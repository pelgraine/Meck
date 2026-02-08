# Text & EPUB Reader Integration for Meck Firmware

## Overview

This adds a text reader accessible via the **R** key from the home screen.

**Features:**
- Browse `.txt` and `.epub` files from `/books/` folder on SD card
- Automatic EPUB-to-text conversion on first open (cached for instant re-opens)
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

Place `.txt` or `.epub` files in a `/books/` folder on the SD card root. The reader will:
- Auto-create `/books/` if it doesn't exist
- Auto-create `/.indexes/` for page index cache files
- Auto-create `/books/.epub_cache/` for converted EPUB text
- Skip macOS hidden files (`._*`, `.DS_Store`)
- Support up to 50 files

**Index format** is compatible with the standalone reader (version 4), so if you've used the standalone reader previously, bookmarks and indexes will carry over.

---

## EPUB Support

### How It Works

EPUB files are transparently converted to plain text on first open. The conversion pipeline is:

1. **File list** — `scanFiles()` picks up both `.txt` and `.epub` files from `/books/`
2. **First open** — `openBook()` detects the `.epub` extension and triggers conversion:
   - Shows a "Converting EPUB..." splash screen
   - Extracts the ZIP structure using ESP32-S3's built-in ROM `tinfl` decompressor (no external library needed)
   - Parses `META-INF/container.xml` → finds the OPF file
   - Parses the OPF manifest and spine to get chapters in reading order
   - Extracts each XHTML chapter, strips tags, decodes HTML entities
   - Writes concatenated plain text to `/books/.epub_cache/<filename>.txt`
3. **Subsequent opens** — the cached `.txt` is found immediately and opened like any regular text file

### Cache Structure

```
/books/
  MyBook.epub              ← original EPUB (untouched)
  SomeStory.txt            ← regular text file
  .epub_cache/
    MyBook.txt             ← auto-generated from MyBook.epub
/.indexes/
  MyBook.txt.idx           ← page index for the converted text
```

- The original `.epub` file is never modified
- Deleting a cached `.txt` from `.epub_cache/` forces re-conversion on next open
- Index files (`.idx`) work identically for both regular and EPUB-derived text files
- Boot scan picks up previously cached EPUB text files so they appear in the file list even before the EPUB is re-opened

### EPUB Processing Details

The conversion is handled by three components:

| Component | Role |
|-----------|------|
| `EpubZipReader.h` | ZIP central directory parsing + `tinfl` decompression (supports Store and Deflate) |
| `EpubProcessor.h` | EPUB structure parsing (container.xml → OPF → spine) and XHTML tag stripping |
| `TextReaderScreen.h` | Integration: detects `.epub`, triggers conversion, redirects to cached `.txt` |

**XHTML stripping handles:**
- Tag removal with block-element newlines (`<p>`, `<br>`, `<div>`, `<h1>`–`<h6>`, `<li>`, etc.)
- `<head>`, `<style>`, `<script>` content skipped entirely
- HTML entity decoding: named (`&amp;`, `&mdash;`, `&ldquo;`, etc.) and numeric (`&#8212;`, `&#x2014;`)
- Smart quote / em-dash / ellipsis → ASCII equivalents (e-ink font is ASCII-only)
- Whitespace collapsing and cleanup

**Limits:**
- Max 200 chapters in spine (`EPUB_MAX_CHAPTERS`)
- Max 256 manifest items (`EPUB_MAX_MANIFEST`)
- Manifest and chapter data are heap-allocated in PSRAM where available
- Typical conversion time: 2–10 seconds depending on book size

### Troubleshooting

| Symptom | Likely Cause |
|---------|-------------|
| "Convert failed!" splash | EPUB may be DRM-protected, corrupted, or use an unusual structure |
| EPUB appears in list but opens as blank | Check serial output for `EpubProc:` messages; chapter count may be 0 |
| Stale content after replacing an EPUB | Delete the matching `.txt` from `/books/.epub_cache/` to force re-conversion |

---

## Architecture Notes

- The reader renders through the standard `UIScreen::render()` framework, so no special bypass is needed in the main loop (unlike compose mode)
- SD card uses the same HSPI bus as e-ink display and LoRa radio — CS pin management handles contention
- Page content is pre-read from SD into a memory buffer during `handleInput()`, then rendered from buffer during `render()` — this avoids SPI bus conflicts during display refresh
- Layout metrics (chars per line, lines per page) are calculated dynamically from the display driver's font metrics on first entry
- EPUB conversion runs synchronously in `openBook()` — the e-ink splash screen keeps the user informed while the ESP32 processes the archive
- ZIP extraction uses the ESP32-S3's hardware-optimised ROM `tinfl` inflate, avoiding external compression library dependencies and the linker conflicts they cause