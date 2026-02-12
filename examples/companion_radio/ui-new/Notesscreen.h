#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <SD.h>
#include <vector>
#include "Utf8CP437.h"

// Forward declarations
class UITask;

// ============================================================================
// Configuration
// ============================================================================
#define NOTES_FOLDER      "/notes"
#define NOTES_MAX_FILES   30
#define NOTES_BUF_SIZE    4096   // Max note size in bytes (~4000 chars)
#define NOTES_FILENAME_MAX 32

// ============================================================================
// NotesScreen - Create, view, and edit .txt notes on SD card
// ============================================================================
// Modes:
//   FILE_LIST  - Browse existing notes, create new ones
//   READING    - Paginated read-only view of a note
//   EDITING    - Append-only text editor (type at end, backspace to delete)
//
// Key bindings:
//   FILE_LIST:  W/S = scroll, Enter = open note (read), Q = exit notes
//   READING:    W/S = page nav, Enter = switch to edit mode, Q = back to list
//   EDITING:    Type = append, Backspace = delete last char, Enter = newline
//               Shift+Backspace = save & exit to file list
//
// New notes get auto-generated filenames: note_001.txt, note_002.txt, etc.
// The "+ New Note" option is always at the top of the file list.
// ============================================================================

class NotesScreen : public UIScreen {
public:
  enum Mode { FILE_LIST, READING, EDITING };

private:
  UITask* _task;
  Mode _mode;
  bool _sdReady;
  bool _initialized;
  DisplayDriver* _display;

  // Display layout (calculated once from display metrics)
  int _charsPerLine;
  int _linesPerPage;
  int _lineHeight;
  int _footerHeight;

  // File list state
  std::vector<String> _fileList;
  int _selectedFile;          // 0 = "+ New Note", 1..N = existing files

  // Current note state
  String _currentFile;        // Filename (just name, not full path)
  char _buf[NOTES_BUF_SIZE];  // Note content buffer
  int _bufLen;                // Current content length
  bool _dirty;                // Has unsaved changes

  // Reading state (paginated view)
  int _currentPage;
  int _totalPages;
  std::vector<int> _pageOffsets;  // Byte offsets for each page start

  // ---- Helpers ----

  String getFullPath(const String& filename) {
    return String(NOTES_FOLDER) + "/" + filename;
  }

  // Generate a sequential filename: note_001.txt, note_002.txt, etc.
  String generateFilename() {
    int maxNum = 0;

    // Scan existing files to find the highest note number
    for (int i = 0; i < (int)_fileList.size(); i++) {
      const String& name = _fileList[i];
      // Match pattern: note_NNN.txt
      if (name.startsWith("note_") && name.endsWith(".txt")) {
        String numPart = name.substring(5, name.length() - 4);
        int num = numPart.toInt();
        if (num > maxNum) maxNum = num;
      }
    }

    int nextNum = maxNum + 1;
    char name[NOTES_FILENAME_MAX];
    snprintf(name, sizeof(name), "note_%03d.txt", nextNum);
    return String(name);
  }

  // ---- File Scanning ----

  void scanFiles() {
    _fileList.clear();
    if (!SD.exists(NOTES_FOLDER)) {
      SD.mkdir(NOTES_FOLDER);
      Serial.printf("Notes: Created %s\n", NOTES_FOLDER);
    }

    File root = SD.open(NOTES_FOLDER);
    if (!root || !root.isDirectory()) return;

    File f = root.openNextFile();
    while (f && _fileList.size() < NOTES_MAX_FILES) {
      if (!f.isDirectory()) {
        String name = String(f.name());
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);

        if (!name.startsWith(".") &&
            (name.endsWith(".txt") || name.endsWith(".TXT"))) {
          _fileList.push_back(name);
        }
      }
      f = root.openNextFile();
    }
    root.close();

    // Sort alphabetically (newest timestamp names sort last)
    for (int i = 0; i < (int)_fileList.size() - 1; i++) {
      for (int j = i + 1; j < (int)_fileList.size(); j++) {
        if (_fileList[i] > _fileList[j]) {
          String tmp = _fileList[i];
          _fileList[i] = _fileList[j];
          _fileList[j] = tmp;
        }
      }
    }

    Serial.printf("Notes: Found %d files\n", _fileList.size());
  }

  // ---- File I/O ----

  bool loadNote(const String& filename) {
    String path = getFullPath(filename);
    File file = SD.open(path.c_str(), FILE_READ);
    if (!file) {
      Serial.printf("Notes: Failed to open %s\n", filename.c_str());
      return false;
    }

    unsigned long size = file.size();
    int toRead = min((unsigned long)(NOTES_BUF_SIZE - 1), size);
    _bufLen = file.readBytes(_buf, toRead);
    _buf[_bufLen] = '\0';
    file.close();

    // Deselect SD to free SPI bus
    digitalWrite(SDCARD_CS, HIGH);

    _currentFile = filename;
    _dirty = false;

    if (size >= NOTES_BUF_SIZE) {
      Serial.printf("Notes: Warning - %s truncated (%lu > %d)\n",
                    filename.c_str(), size, NOTES_BUF_SIZE - 1);
    }

    Serial.printf("Notes: Loaded %s (%d bytes)\n", filename.c_str(), _bufLen);
    return true;
  }

  bool saveNote() {
    if (_currentFile.length() == 0) return false;

    String path = getFullPath(_currentFile);

    // Remove existing file before writing (SD lib quirk)
    if (SD.exists(path.c_str())) {
      SD.remove(path.c_str());
    }

    File file = SD.open(path.c_str(), FILE_WRITE);
    if (!file) {
      Serial.printf("Notes: Failed to save %s\n", _currentFile.c_str());
      return false;
    }

    file.write((uint8_t*)_buf, _bufLen);
    file.close();

    // Deselect SD to free SPI bus
    digitalWrite(SDCARD_CS, HIGH);

    _dirty = false;
    Serial.printf("Notes: Saved %s (%d bytes)\n", _currentFile.c_str(), _bufLen);
    return true;
  }

  bool deleteNote(const String& filename) {
    String path = getFullPath(filename);
    if (SD.exists(path.c_str())) {
      SD.remove(path.c_str());
      Serial.printf("Notes: Deleted %s\n", filename.c_str());
      return true;
    }
    return false;
  }

  // ---- Pagination for Read Mode ----
  // Reuses the same word-wrap logic as TextReaderScreen but operates
  // on the in-memory buffer instead of streaming from SD.

  void buildPageIndex() {
    _pageOffsets.clear();
    _pageOffsets.push_back(0);

    int pos = 0;
    int lineCount = 0;

    while (pos < _bufLen) {
      // Find end of this line using word wrap
      int lineEnd = pos;
      int nextStart = pos;
      int charCount = 0;
      int lastBreak = -1;
      bool inWord = false;

      for (int i = pos; i < _bufLen; i++) {
        char c = _buf[i];

        if (c == '\n') {
          lineEnd = i;
          nextStart = i + 1;
          goto lineFound;
        }
        if (c == '\r') {
          lineEnd = i;
          nextStart = i + 1;
          if (nextStart < _bufLen && _buf[nextStart] == '\n') nextStart++;
          goto lineFound;
        }

        if (c >= 32) {
          // Skip UTF-8 continuation bytes
          if ((uint8_t)c >= 0x80 && (uint8_t)c < 0xC0) continue;

          charCount++;
          if (c == ' ' || c == '\t') {
            if (inWord) { lastBreak = i; inWord = false; }
          } else if (c == '-') {
            if (inWord) lastBreak = i + 1;
          } else {
            inWord = true;
          }

          if (charCount >= _charsPerLine) {
            if (lastBreak > pos) {
              lineEnd = lastBreak;
              nextStart = lastBreak;
              while (nextStart < _bufLen &&
                     (_buf[nextStart] == ' ' || _buf[nextStart] == '\t'))
                nextStart++;
            } else {
              lineEnd = i;
              nextStart = i;
            }
            goto lineFound;
          }
        }
      }
      // Reached end of buffer
      break;

    lineFound:
      lineCount++;
      pos = nextStart;

      if (lineCount >= _linesPerPage) {
        _pageOffsets.push_back(pos);
        lineCount = 0;
      }

      if (pos >= _bufLen) break;
    }

    _totalPages = _pageOffsets.size();
    if (_currentPage >= _totalPages) {
      _currentPage = max(0, _totalPages - 1);
    }
  }

  // ---- Rendering ----

  // Quick feedback splash (shown briefly during SD operations)
  void drawBriefSplash(const char* message) {
    if (!_display) return;
    _display->startFrame();
    _display->setTextSize(2);
    _display->setColor(DisplayDriver::GREEN);
    _display->setCursor(10, 40);
    _display->print(message);
    _display->setTextSize(1);
    _display->endFrame();
  }

  void renderFileList(DisplayDriver& display) {
    char tmp[40];

    // Header
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.print("Notes");

    snprintf(tmp, sizeof(tmp), "[%d]", (int)_fileList.size());
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);

    // File list with "+ New Note" at index 0
    display.setTextSize(0);  // Tiny font
    int listLineH = 8;
    int startY = 14;
    int totalItems = 1 + (int)_fileList.size();  // +1 for "New Note"
    int maxVisible = (display.height() - startY - _footerHeight) / listLineH;
    if (maxVisible < 3) maxVisible = 3;
    if (maxVisible > 15) maxVisible = 15;

    int startIdx = max(0, min(_selectedFile - maxVisible / 2,
                              totalItems - maxVisible));
    int endIdx = min(totalItems, startIdx + maxVisible);

    int y = startY;
    for (int i = startIdx; i < endIdx; i++) {
      bool selected = (i == _selectedFile);

      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + 5, display.width(), listLineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      display.setCursor(0, y);

      if (i == 0) {
        // "+ New Note" option
        display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
        display.print(selected ? "> + New Note" : "  + New Note");
      } else {
        // Existing file
        String line = selected ? "> " : "  ";
        String name = _fileList[i - 1];

        // Truncate if needed
        int maxLen = _charsPerLine - 4;
        if ((int)name.length() > maxLen) {
          name = name.substring(0, maxLen - 3) + "...";
        }
        line += name;
        display.print(line.c_str());
      }

      y += listLineH;
    }
    display.setTextSize(1);

    // Footer
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("Q:Back W/S:Nav");

    const char* right = "Ent:Open";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  void renderReadPage(DisplayDriver& display) {
    if (_totalPages == 0) {
      display.setCursor(0, 14);
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.print("(empty note)");

      // Still show footer
      int footerY = display.height() - 12;
      display.drawRect(0, footerY - 2, display.width(), 1);
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, footerY);
      display.print("Q:Bck Ent:Edit");

      const char* right = "Sh+Del:Del";
      display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
      display.print(right);
      return;
    }

    // Render current page using tiny font
    display.setTextSize(0);
    display.setColor(DisplayDriver::LIGHT);

    int pageStart = _pageOffsets[_currentPage];
    int pageEnd = (_currentPage + 1 < _totalPages)
                  ? _pageOffsets[_currentPage + 1]
                  : _bufLen;

    int y = 0;
    int lineCount = 0;
    int pos = pageStart;
    int maxY = display.height() - _footerHeight - _lineHeight;

    while (pos < pageEnd && pos < _bufLen && lineCount < _linesPerPage && y <= maxY) {
      // Find line end with word wrap
      int lineEnd = pos;
      int nextStart = pos;
      int charCount = 0;
      int lastBreak = -1;
      bool inWord = false;

      for (int i = pos; i < pageEnd && i < _bufLen; i++) {
        char c = _buf[i];
        if (c == '\n') { lineEnd = i; nextStart = i + 1; goto renderLine; }
        if (c == '\r') {
          lineEnd = i; nextStart = i + 1;
          if (nextStart < _bufLen && _buf[nextStart] == '\n') nextStart++;
          goto renderLine;
        }
        if (c >= 32) {
          if ((uint8_t)c >= 0x80 && (uint8_t)c < 0xC0) continue;
          charCount++;
          if (c == ' ' || c == '\t') { if (inWord) { lastBreak = i; inWord = false; } }
          else if (c == '-') { if (inWord) lastBreak = i + 1; }
          else inWord = true;
          if (charCount >= _charsPerLine) {
            if (lastBreak > pos) {
              lineEnd = lastBreak; nextStart = lastBreak;
              while (nextStart < _bufLen && (_buf[nextStart] == ' ' || _buf[nextStart] == '\t'))
                nextStart++;
            } else { lineEnd = i; nextStart = i; }
            goto renderLine;
          }
        }
      }
      // End of page data
      lineEnd = min(pageEnd, _bufLen);
      nextStart = lineEnd;

    renderLine:
      display.setCursor(0, y);

      // Print characters with UTF-8 -> CP437 mapping
      char charStr[2] = {0, 0};
      int j = pos;
      while (j < lineEnd && j < _bufLen) {
        uint8_t b = (uint8_t)_buf[j];
        if (b < 32) { j++; continue; }
        if (b < 0x80) {
          charStr[0] = (char)b;
          display.print(charStr);
          j++;
        } else if (b >= 0xC0) {
          uint32_t cp = decodeUtf8Char(_buf, lineEnd, &j);
          uint8_t glyph = unicodeToCP437(cp);
          if (glyph) { charStr[0] = (char)glyph; display.print(charStr); }
        } else {
          charStr[0] = (char)b;
          display.print(charStr);
          j++;
        }
      }

      y += _lineHeight;
      lineCount++;
      pos = nextStart;
      if (pos >= pageEnd) break;
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    display.setCursor(0, footerY);
    display.print("Q:Bck Ent:Edit");

    const char* right = "Sh+Del:Del";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  void renderEditor(DisplayDriver& display) {
    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

    // Show filename (truncated) + editing indicator
    char header[40];
    String shortName = _currentFile;
    if (shortName.length() > 18) {
      shortName = shortName.substring(0, 15) + "...";
    }
    snprintf(header, sizeof(header), "Edit: %s%s",
             shortName.c_str(), _dirty ? "*" : "");
    display.print(header);

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    // Render the TAIL of the buffer (most recent text) with word wrap.
    // We want the cursor (end of text) to be visible, so we render
    // backwards from the end to fill available screen lines.

    int textAreaTop = 14;
    int textAreaBottom = display.height() - 16;  // Leave room for footer
    int maxLines = (textAreaBottom - textAreaTop) / 12;  // ~12 virtual units per line at size 1
    if (maxLines < 3) maxLines = 3;

    // Simple approach: find how many characters fit on screen by working
    // backwards from buffer end to find the screen start position.
    // Use size 1 font for the editor (bigger than tiny, easier to read while typing).
    display.setTextSize(1);

    // Measure char width for word wrap
    uint16_t charW = display.getTextWidth("M");
    int editCharsPerLine = charW > 0 ? display.width() / charW : 20;
    if (editCharsPerLine < 10) editCharsPerLine = 10;
    if (editCharsPerLine > 40) editCharsPerLine = 40;

    // Build visible lines from the buffer (last N lines)
    // We'll collect lines in reverse and then display them.
    struct Line { int start; int end; };
    Line lines[32];  // More than enough for any screen
    int lineCount = 0;

    // Forward pass to find all line breaks
    int tmpLines = 0;
    int pos = 0;
    int allLineStarts[512];
    int allLineEnds[512];

    while (pos < _bufLen && tmpLines < 512) {
      allLineStarts[tmpLines] = pos;

      // Find line end
      int lineEnd = pos;
      int nextStart = pos;
      int charCount = 0;
      int lastBreak = -1;
      bool inWord = false;

      for (int i = pos; i < _bufLen; i++) {
        char c = _buf[i];
        if (c == '\n') { lineEnd = i; nextStart = i + 1; goto editorLineFound; }
        if (c == '\r') {
          lineEnd = i; nextStart = i + 1;
          if (nextStart < _bufLen && _buf[nextStart] == '\n') nextStart++;
          goto editorLineFound;
        }
        if (c >= 32) {
          if ((uint8_t)c >= 0x80 && (uint8_t)c < 0xC0) continue;
          charCount++;
          if (c == ' ' || c == '\t') { if (inWord) { lastBreak = i; inWord = false; } }
          else if (c == '-') { if (inWord) lastBreak = i + 1; }
          else inWord = true;
          if (charCount >= editCharsPerLine) {
            if (lastBreak > pos) {
              lineEnd = lastBreak; nextStart = lastBreak;
              while (nextStart < _bufLen && (_buf[nextStart] == ' ' || _buf[nextStart] == '\t'))
                nextStart++;
            } else { lineEnd = i; nextStart = i; }
            goto editorLineFound;
          }
        }
      }
      lineEnd = _bufLen;
      nextStart = _bufLen;

    editorLineFound:
      allLineEnds[tmpLines] = lineEnd;
      tmpLines++;
      pos = nextStart;
      if (pos >= _bufLen) break;
    }

    // If buffer is empty, we have 0 lines - that's fine, cursor still shows

    // Take the last maxLines lines
    int firstVisible = max(0, tmpLines - maxLines);
    lineCount = tmpLines - firstVisible;

    // Render visible lines
    int y = textAreaTop;
    display.setColor(DisplayDriver::LIGHT);

    for (int li = firstVisible; li < tmpLines; li++) {
      display.setCursor(0, y);
      char charStr[2] = {0, 0};
      for (int j = allLineStarts[li]; j < allLineEnds[li] && j < _bufLen; j++) {
        uint8_t b = (uint8_t)_buf[j];
        if (b < 32) continue;
        charStr[0] = (char)b;
        display.print(charStr);
      }
      y += 12;
    }

    // Show cursor at end of last line (or at start if empty)
    if (tmpLines == 0 || lineCount == 0) {
      display.setCursor(0, textAreaTop);
    }
    display.print("_");

    // Footer / status bar
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, footerY - 2, display.width(), 1);

    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

    char status[40];
    snprintf(status, sizeof(status), "%d/%d", _bufLen, NOTES_BUF_SIZE - 1);
    display.print(status);

    // Show Q:Back when there's nothing to lose, Sh+Del:Save when there is
    const char* right;
    if (_bufLen == 0 || !_dirty) {
      right = "Q:Back";
    } else {
      right = "Sh+Del:Save";
    }
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  // ---- Input Handling ----

  bool handleFileListInput(char c) {
    int totalItems = 1 + (int)_fileList.size();

    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_selectedFile > 0) { _selectedFile--; return true; }
      return false;
    }

    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_selectedFile < totalItems - 1) { _selectedFile++; return true; }
      return false;
    }

    // Enter - open selected item
    if (c == '\r' || c == 13) {
      if (_selectedFile == 0) {
        // Create new note
        createNewNote();
        return true;
      } else {
        // Open existing note
        int fileIdx = _selectedFile - 1;
        if (fileIdx >= 0 && fileIdx < (int)_fileList.size()) {
          if (loadNote(_fileList[fileIdx])) {
            buildPageIndex();
            _currentPage = 0;
            _mode = READING;
          }
          return true;
        }
      }
      return false;
    }

    return false;
  }

  bool handleReadInput(char c) {
    // W/A - previous page
    if (c == 'w' || c == 'W' || c == 'a' || c == 'A' || c == 0xF2) {
      if (_currentPage > 0) { _currentPage--; return true; }
      return false;
    }

    // S/D/Space - next page
    if (c == 's' || c == 'S' || c == 'd' || c == 'D' || c == ' ' || c == 0xF1) {
      if (_currentPage < _totalPages - 1) { _currentPage++; return true; }
      return false;
    }

    // Enter - switch to edit mode (jump to end of buffer)
    if (c == '\r' || c == 13) {
      _mode = EDITING;
      Serial.printf("Notes: Editing %s (%d bytes)\n", _currentFile.c_str(), _bufLen);
      return true;
    }

    // Q - back to file list
    if (c == 'q' || c == 'Q') {
      _mode = FILE_LIST;
      return true;
    }

    return false;
  }

  // Returns: true if screen needs refresh, false otherwise.
  // Special return via _mode change: if Shift+Backspace detected externally
  // (in main.cpp notesMode handler), caller saves and exits.
  bool handleEditInput(char c) {
    // Backspace
    if (c == '\b') {
      if (_bufLen > 0) {
        _bufLen--;
        _buf[_bufLen] = '\0';
        _dirty = true;
        return true;  // Will be debounced by caller
      }
      return false;
    }

    // Enter - insert newline
    if (c == '\r' || c == 13) {
      if (_bufLen < NOTES_BUF_SIZE - 2) {
        _buf[_bufLen++] = '\n';
        _buf[_bufLen] = '\0';
        _dirty = true;
        return true;
      }
      return false;
    }

    // Regular printable character
    if (c >= 32 && c < 127 && _bufLen < NOTES_BUF_SIZE - 1) {
      _buf[_bufLen++] = c;
      _buf[_bufLen] = '\0';
      _dirty = true;
      return true;
    }

    return false;
  }

  // ---- Note Creation ----

  void createNewNote() {
    _currentFile = generateFilename();
    _buf[0] = '\0';
    _bufLen = 0;
    _dirty = true;
    _mode = EDITING;
    Serial.printf("Notes: Created new note %s\n", _currentFile.c_str());
  }

public:
  NotesScreen(UITask* task)
    : _task(task), _mode(FILE_LIST), _sdReady(false), _initialized(false),
      _display(nullptr),
      _charsPerLine(38), _linesPerPage(22), _lineHeight(5), _footerHeight(14),
      _selectedFile(0), _bufLen(0), _dirty(false),
      _currentPage(0), _totalPages(0) {
    _buf[0] = '\0';
  }

  // ---- Layout Init (call once after display available) ----

  void initLayout(DisplayDriver& display) {
    if (_initialized) return;
    _display = &display;

    // Measure tiny font metrics (matches TextReaderScreen)
    display.setTextSize(0);
    uint16_t tenCharsW = display.getTextWidth("MMMMMMMMMM");
    if (tenCharsW > 0) {
      _charsPerLine = (display.width() * 10) / tenCharsW;
    }
    if (_charsPerLine < 15) _charsPerLine = 15;
    if (_charsPerLine > 60) _charsPerLine = 60;

    uint16_t mWidth = display.getTextWidth("M");
    if (mWidth > 0) {
      _lineHeight = max(3, (int)((mWidth * 7 * 12) / (6 * 10)));
    } else {
      _lineHeight = 5;
    }

    _footerHeight = 14;
    int textAreaHeight = display.height() - _footerHeight;
    _linesPerPage = textAreaHeight / _lineHeight;
    if (_linesPerPage < 5) _linesPerPage = 5;

    display.setTextSize(1);  // Restore
    _initialized = true;

    Serial.printf("Notes layout: %d chars/line, %d lines/page, lineH=%d\n",
                  _charsPerLine, _linesPerPage, _lineHeight);
  }

  // ---- Public Interface ----

  void setSDReady(bool ready) { _sdReady = ready; }
  bool isSDReady() const { return _sdReady; }

  void enter(DisplayDriver& display) {
    initLayout(display);
    scanFiles();
    if (_mode != EDITING) {
      _selectedFile = 0;
      _mode = FILE_LIST;
    }
  }

  Mode getMode() const { return _mode; }
  bool isEditing() const { return _mode == EDITING; }
  bool isReading() const { return _mode == READING; }
  bool isInFileList() const { return _mode == FILE_LIST; }
  bool isDirty() const { return _dirty; }
  bool isEmpty() const { return _bufLen == 0; }

  // Save current note and return to file list
  void saveAndExit() {
    if (_dirty && _currentFile.length() > 0) {
      // Don't save empty new notes (user created then immediately exited)
      if (_bufLen > 0) {
        drawBriefSplash("Saving...");
        saveNote();
      } else if (_dirty) {
        // New note with no content - skip saving
        Serial.printf("Notes: Skipping empty note %s\n", _currentFile.c_str());
      }
    }
    _mode = FILE_LIST;
    _dirty = false;
    scanFiles();  // Refresh list to show new/updated file
  }

  // Discard changes and return to file list
  void discardAndExit() {
    _dirty = false;
    _mode = FILE_LIST;
    scanFiles();
  }

  // Delete the currently open note and return to file list
  void deleteCurrentNote() {
    if (_currentFile.length() > 0) {
      drawBriefSplash("Deleting...");
      deleteNote(_currentFile);
    }
    _dirty = false;
    _currentFile = "";
    _bufLen = 0;
    _buf[0] = '\0';
    _mode = FILE_LIST;
    _selectedFile = 0;
    scanFiles();
  }

  // Exit notes screen entirely
  void exitNotes() {
    if (_dirty && _bufLen > 0) {
      saveNote();
    }
    _mode = FILE_LIST;
    _dirty = false;
  }

  // ---- UIScreen overrides ----

  int render(DisplayDriver& display) override {
    if (!_sdReady) {
      display.setCursor(0, 20);
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.print("SD card not found");
      display.setCursor(0, 35);
      display.print("Insert SD card");
      return 5000;
    }

    if (_mode == FILE_LIST) {
      renderFileList(display);
    } else if (_mode == READING) {
      renderReadPage(display);
    } else if (_mode == EDITING) {
      renderEditor(display);
    }

    return 5000;
  }

  bool handleInput(char c) override {
    if (_mode == FILE_LIST) {
      return handleFileListInput(c);
    } else if (_mode == READING) {
      return handleReadInput(c);
    } else if (_mode == EDITING) {
      return handleEditInput(c);
    }
    return false;
  }
};