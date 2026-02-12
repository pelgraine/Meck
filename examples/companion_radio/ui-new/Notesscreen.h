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
#define NOTES_FOLDER        "/notes"
#define NOTES_MAX_FILES     30
#define NOTES_BUF_SIZE      16384   // 16KB buffer (PSRAM-backed)
#define NOTES_FILENAME_MAX  40
#define NOTES_RENAME_MAX    32      // Max rename buffer length
#define NOTES_MAX_LINES     1024    // Max visual lines for cursor nav

// ============================================================================
// NotesScreen - Create, view, and edit .txt notes on SD card
// ============================================================================
// Modes:
//   FILE_LIST       - Browse existing notes, create new ones
//   READING         - Paginated read-only view of a note
//   EDITING         - Full text editor with cursor navigation
//   RENAMING        - Rename a file (inline text input)
//   CONFIRM_DELETE  - Delete confirmation dialog
//
// Key bindings:
//   FILE_LIST:  W/S = scroll, Enter = open note (read), Q = exit
//               Shift+Enter = rename selected file
//               Shift+Backspace = delete selected file (with confirmation)
//   READING:    W/S = page nav, Enter = switch to edit, Q = back to list
//               Shift+Backspace = delete note
//   EDITING:    Type = insert at cursor, Backspace = delete before cursor
//               Enter = newline, Shift+WASD = cursor navigation
//               Shift+Backspace = save & exit
//   RENAMING:   Type = edit filename, Backspace = delete char
//               Enter = confirm rename, Q = cancel
//   CONFIRM_DELETE: Enter = confirm delete, Q = cancel
//
// Filenames: RTC timestamp (note_YYYYMMDD_HHMM.txt) or sequential (note_001.txt)
// Buffer: 16KB on PSRAM for longer notes
// ============================================================================

class NotesScreen : public UIScreen {
public:
  enum Mode { FILE_LIST, READING, EDITING, RENAMING, CONFIRM_DELETE };

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

  // Editor-specific layout
  int _editCharsPerLine;
  int _editLineHeight;
  int _editMaxLines;      // Max visible lines in editor area

  // File list state
  std::vector<String> _fileList;
  int _selectedFile;      // 0 = "+ New Note", 1..N = existing files

  // Current note state
  String _currentFile;    // Filename (just name, not full path)
  char* _buf;             // Note content buffer (PSRAM-backed)
  int _bufLen;            // Current content length
  int _cursorPos;         // Cursor byte position in buffer
  bool _dirty;            // Has unsaved changes

  // Reading state (paginated view)
  int _currentPage;
  int _totalPages;
  std::vector<int> _pageOffsets;

  // Editor visual lines (rebuilt on content/cursor change)
  struct EditorLine { int start; int end; };
  EditorLine* _editorLines;
  int _numEditorLines;
  int _editorScrollTop;   // First visible line index

  // Rename state
  char _renameBuf[NOTES_RENAME_MAX];
  int _renameLen;
  String _renameOriginal; // Original filename (for cancel)

  // Delete confirmation state
  String _deleteTarget;   // File to delete

  // RTC timestamp support
  uint32_t _rtcTime;      // Unix timestamp (0 = unavailable)
  int8_t _utcOffset;      // UTC offset in hours

  // ---- Helpers ----

  String getFullPath(const String& filename) {
    return String(NOTES_FOLDER) + "/" + filename;
  }

  // Generate filename using RTC timestamp or sequential fallback
  String generateFilename() {
    // Try RTC-based name first
    if (_rtcTime > 1700000000) {
      time_t t = (time_t)_rtcTime + ((int32_t)_utcOffset * 3600);
      struct tm* tm = gmtime(&t);
      if (tm) {
        char name[NOTES_FILENAME_MAX];
        snprintf(name, sizeof(name), "note_%04d%02d%02d_%02d%02d.txt",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min);

        // Check for collision (two notes in same minute)
        String fullPath = getFullPath(String(name));
        if (!SD.exists(fullPath.c_str())) {
          return String(name);
        }
        // Append seconds to disambiguate
        snprintf(name, sizeof(name), "note_%04d%02d%02d_%02d%02d%02d.txt",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                 tm->tm_hour, tm->tm_min, tm->tm_sec);
        return String(name);
      }
    }

    // Fallback: sequential numbering
    int maxNum = 0;
    for (int i = 0; i < (int)_fileList.size(); i++) {
      const String& name = _fileList[i];
      if (name.startsWith("note_") && name.endsWith(".txt")) {
        String numPart = name.substring(5, name.length() - 4);
        int num = numPart.toInt();
        if (num > maxNum) maxNum = num;
      }
    }
    char name[NOTES_FILENAME_MAX];
    snprintf(name, sizeof(name), "note_%03d.txt", maxNum + 1);
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

    // Sort alphabetically
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
    digitalWrite(SDCARD_CS, HIGH);

    _currentFile = filename;
    _cursorPos = _bufLen;
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

  bool renameNote(const String& oldName, const String& newName) {
    String oldPath = getFullPath(oldName);
    String newPath = getFullPath(newName);

    if (!SD.exists(oldPath.c_str())) return false;
    if (SD.exists(newPath.c_str())) {
      Serial.printf("Notes: Rename failed - %s already exists\n", newName.c_str());
      return false;
    }

    // SD library doesn't have rename, so copy + delete
    File src = SD.open(oldPath.c_str(), FILE_READ);
    if (!src) return false;

    File dst = SD.open(newPath.c_str(), FILE_WRITE);
    if (!dst) { src.close(); return false; }

    uint8_t copyBuf[512];
    while (src.available()) {
      int n = src.read(copyBuf, sizeof(copyBuf));
      if (n > 0) dst.write(copyBuf, n);
    }
    dst.close();
    src.close();

    SD.remove(oldPath.c_str());
    digitalWrite(SDCARD_CS, HIGH);

    Serial.printf("Notes: Renamed %s -> %s\n", oldName.c_str(), newName.c_str());
    return true;
  }

  // ---- Pagination for Read Mode ----

  void buildPageIndex() {
    _pageOffsets.clear();
    _pageOffsets.push_back(0);

    int pos = 0;
    int lineCount = 0;

    while (pos < _bufLen) {
      int lineEnd = pos;
      int nextStart = pos;
      int charCount = 0;
      int lastBreak = -1;
      bool inWord = false;

      for (int i = pos; i < _bufLen; i++) {
        char c = _buf[i];
        if (c == '\n') { lineEnd = i; nextStart = i + 1; goto pageLineFound; }
        if (c == '\r') {
          lineEnd = i; nextStart = i + 1;
          if (nextStart < _bufLen && _buf[nextStart] == '\n') nextStart++;
          goto pageLineFound;
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
            goto pageLineFound;
          }
        }
      }
      break;

    pageLineFound:
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

  // ---- Editor Line Building (for cursor navigation) ----

  void buildEditorLines() {
    _numEditorLines = 0;
    int pos = 0;

    while (pos < _bufLen && _numEditorLines < NOTES_MAX_LINES) {
      _editorLines[_numEditorLines].start = pos;

      int lineEnd = pos;
      int nextStart = pos;
      int charCount = 0;
      int lastBreak = -1;
      bool inWord = false;

      for (int i = pos; i < _bufLen; i++) {
        char c = _buf[i];
        if (c == '\n') { lineEnd = i; nextStart = i + 1; goto edLineFound; }
        if (c == '\r') {
          lineEnd = i; nextStart = i + 1;
          if (nextStart < _bufLen && _buf[nextStart] == '\n') nextStart++;
          goto edLineFound;
        }
        if (c >= 32) {
          if ((uint8_t)c >= 0x80 && (uint8_t)c < 0xC0) continue;
          charCount++;
          if (c == ' ' || c == '\t') { if (inWord) { lastBreak = i; inWord = false; } }
          else if (c == '-') { if (inWord) lastBreak = i + 1; }
          else inWord = true;
          if (charCount >= _editCharsPerLine) {
            if (lastBreak > pos) {
              lineEnd = lastBreak; nextStart = lastBreak;
              while (nextStart < _bufLen && (_buf[nextStart] == ' ' || _buf[nextStart] == '\t'))
                nextStart++;
            } else { lineEnd = i; nextStart = i; }
            goto edLineFound;
          }
        }
      }
      lineEnd = _bufLen;
      nextStart = _bufLen;

    edLineFound:
      _editorLines[_numEditorLines].end = lineEnd;
      _numEditorLines++;
      pos = nextStart;
      if (pos >= _bufLen) break;
    }

    // Ensure at least one line (empty buffer)
    if (_numEditorLines == 0) {
      _editorLines[0] = {0, 0};
      _numEditorLines = 1;
    }
  }

  // Find which editor line contains a buffer position
  int lineForPos(int bufPos) {
    for (int i = 0; i < _numEditorLines; i++) {
      int nextStart = (i + 1 < _numEditorLines) ? _editorLines[i + 1].start : _bufLen + 1;
      if (bufPos >= _editorLines[i].start && bufPos < nextStart) return i;
    }
    return max(0, _numEditorLines - 1);
  }

  // Count visual columns from line start to a buffer position
  int colForPos(int bufPos, int lineStart) {
    int col = 0;
    for (int i = lineStart; i < bufPos && i < _bufLen; i++) {
      uint8_t b = (uint8_t)_buf[i];
      if (b >= 0x80 && b < 0xC0) continue;
      if (b == '\n' || b == '\r') break;
      col++;
    }
    return col;
  }

  // Find buffer position for a target column on a given line
  int posForCol(int targetCol, int lineIdx) {
    if (lineIdx < 0 || lineIdx >= _numEditorLines) return _bufLen;
    int start = _editorLines[lineIdx].start;
    int end = _editorLines[lineIdx].end;
    int col = 0;
    for (int i = start; i < end && i < _bufLen; i++) {
      uint8_t b = (uint8_t)_buf[i];
      if (b >= 0x80 && b < 0xC0) continue;
      if (b == '\n' || b == '\r') return i;
      if (col >= targetCol) return i;
      col++;
    }
    return end;
  }

  // Ensure the editor scroll position keeps cursor visible
  void ensureCursorVisible() {
    int cursorLine = lineForPos(_cursorPos);
    if (cursorLine < _editorScrollTop) {
      _editorScrollTop = cursorLine;
    }
    if (cursorLine >= _editorScrollTop + _editMaxLines) {
      _editorScrollTop = cursorLine - _editMaxLines + 1;
    }
    if (_editorScrollTop < 0) _editorScrollTop = 0;
  }

  // ---- Cursor Operations ----

  void insertAtCursor(char c) {
    if (_bufLen >= NOTES_BUF_SIZE - 1) return;
    memmove(&_buf[_cursorPos + 1], &_buf[_cursorPos], _bufLen - _cursorPos);
    _buf[_cursorPos] = c;
    _bufLen++;
    _buf[_bufLen] = '\0';
    _cursorPos++;
    _dirty = true;
  }

  void deleteBeforeCursor() {
    if (_cursorPos <= 0) return;
    memmove(&_buf[_cursorPos - 1], &_buf[_cursorPos], _bufLen - _cursorPos);
    _cursorPos--;
    _bufLen--;
    _buf[_bufLen] = '\0';
    _dirty = true;
  }

  // ---- Rendering ----

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
    display.setTextSize(0);
    int listLineH = 8;
    int startY = 14;
    int totalItems = 1 + (int)_fileList.size();
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
        display.setColor(selected ? DisplayDriver::DARK : DisplayDriver::GREEN);
        display.print(selected ? "> + New Note" : "  + New Note");
      } else {
        String line = selected ? "> " : "  ";
        String name = _fileList[i - 1];
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
      lineEnd = _bufLen;
      nextStart = _bufLen;

    renderLine:
      display.setCursor(0, y);
      char charStr[2] = {0, 0};

      for (int j = pos; j < lineEnd && j < _bufLen;) {
        uint8_t b = (uint8_t)_buf[j];
        if (b < 32) { j++; continue; }
        if (b >= 0x80) {
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
    // Rebuild visual lines and ensure cursor is visible
    buildEditorLines();
    ensureCursorVisible();

    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);

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

    // Text area
    int textAreaTop = 14;
    int textAreaBottom = display.height() - 16;

    display.setTextSize(1);

    // Find cursor line
    int cursorLine = lineForPos(_cursorPos);

    // Render visible lines
    int y = textAreaTop;
    display.setColor(DisplayDriver::LIGHT);

    for (int li = _editorScrollTop;
         li < _numEditorLines && y < textAreaBottom;
         li++) {

      display.setCursor(0, y);
      char charStr[2] = {0, 0};

      int lineStart = _editorLines[li].start;
      int lineEnd = _editorLines[li].end;

      // Render characters, inserting cursor at the right position
      bool cursorDrawn = false;

      for (int j = lineStart; j < lineEnd && j < _bufLen; j++) {
        // Draw cursor before this character if cursor is here
        if (li == cursorLine && j == _cursorPos && !cursorDrawn) {
          display.setColor(DisplayDriver::GREEN);
          display.print("|");
          display.setColor(DisplayDriver::LIGHT);
          cursorDrawn = true;
        }

        uint8_t b = (uint8_t)_buf[j];
        if (b < 32) continue;
        charStr[0] = (char)b;
        display.print(charStr);
      }

      // Cursor at end of this line
      if (li == cursorLine && !cursorDrawn) {
        display.setColor(DisplayDriver::GREEN);
        display.print("|");
        display.setColor(DisplayDriver::LIGHT);
      }

      y += _editLineHeight;
    }

    // If buffer is empty, show cursor at top
    if (_bufLen == 0) {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, textAreaTop);
      display.print("|");
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, footerY - 2, display.width(), 1);

    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

    char status[20];
    int cursorLine = lineForPos(_cursorPos);
    int curPage = (_editMaxLines > 0) ? (cursorLine / _editMaxLines) + 1 : 1;
    int totalPg = (_editMaxLines > 0) ? max(1, (_numEditorLines + _editMaxLines - 1) / _editMaxLines) : 1;
    snprintf(status, sizeof(status), "Pg %d/%d", curPage, totalPg);
    display.print(status);

    const char* right;
    if (_bufLen == 0 || !_dirty) {
      right = "Q:Back";
    } else {
      right = "Sh+Del:Save";
    }
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  void renderRenameDialog(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print("Rename Note");

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    // Show original name
    display.setCursor(0, 20);
    display.setColor(DisplayDriver::LIGHT);
    display.print("From: ");
    display.setTextSize(0);
    String origDisplay = _renameOriginal;
    if (origDisplay.length() > 30) origDisplay = origDisplay.substring(0, 27) + "...";
    display.print(origDisplay.c_str());

    // Show editable name with cursor
    display.setTextSize(1);
    display.setCursor(0, 38);
    display.setColor(DisplayDriver::LIGHT);
    display.print("To:   ");

    display.setTextSize(0);
    display.setColor(DisplayDriver::GREEN);
    char displayName[NOTES_RENAME_MAX + 2];
    snprintf(displayName, sizeof(displayName), "%s|", _renameBuf);
    display.print(displayName);

    // Show .txt extension hint
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(0, 56);
    display.print("(.txt added automatically)");

    // Footer
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("Q:Cancel");

    const char* right = "Ent:Confirm";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  void renderDeleteConfirm(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print("Delete Note?");

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, 11, display.width(), 1);

    display.setCursor(0, 25);
    display.print("File:");

    display.setTextSize(0);
    display.setCursor(0, 38);
    String nameDisplay = _deleteTarget;
    if (nameDisplay.length() > 35) nameDisplay = nameDisplay.substring(0, 32) + "...";
    display.print(nameDisplay.c_str());

    display.setTextSize(1);
    display.setCursor(0, 58);
    display.setColor(DisplayDriver::GREEN);
    display.print("This cannot be undone.");

    // Footer
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
    display.print("Q:Cancel");

    const char* right = "Ent:Delete";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  // ---- Input Handling ----

  bool handleFileListInput(char c) {
    int totalItems = 1 + (int)_fileList.size();

    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_selectedFile > 0) { _selectedFile--; return true; }
      return false;
    }

    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_selectedFile < totalItems - 1) { _selectedFile++; return true; }
      return false;
    }

    // Enter - open selected item
    if (c == '\r' || c == 13) {
      if (_selectedFile == 0) {
        createNewNote();
        return true;
      } else {
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
    if (c == 'w' || c == 'W' || c == 'a' || c == 'A' || c == 0xF2) {
      if (_currentPage > 0) { _currentPage--; return true; }
      return false;
    }

    if (c == 's' || c == 'S' || c == 'd' || c == 'D' || c == ' ' || c == 0xF1) {
      if (_currentPage < _totalPages - 1) { _currentPage++; return true; }
      return false;
    }

    // Enter - switch to edit mode
    if (c == '\r' || c == 13) {
      _cursorPos = _bufLen;
      _editorScrollTop = 0;
      _mode = EDITING;
      Serial.printf("Notes: Editing %s (%d bytes)\n", _currentFile.c_str(), _bufLen);
      return true;
    }

    if (c == 'q' || c == 'Q') {
      _mode = FILE_LIST;
      return true;
    }

    return false;
  }

  bool handleEditInput(char c) {
    // Backspace - delete before cursor
    if (c == '\b') {
      deleteBeforeCursor();
      return _dirty;
    }

    // Enter - insert newline at cursor
    if (c == '\r' || c == 13) {
      if (_bufLen < NOTES_BUF_SIZE - 2) {
        insertAtCursor('\n');
        return true;
      }
      return false;
    }

    // Regular printable character - insert at cursor
    if (c >= 32 && c < 127 && _bufLen < NOTES_BUF_SIZE - 1) {
      insertAtCursor(c);
      return true;
    }

    return false;
  }

  bool handleRenameInput(char c) {
    // Q - cancel rename
    if (c == 'q' || c == 'Q') {
      _mode = FILE_LIST;
      Serial.println("Notes: Rename cancelled");
      return true;
    }

    // Enter - confirm rename
    if (c == '\r' || c == 13) {
      if (_renameLen > 0) {
        String newName = String(_renameBuf) + ".txt";
        if (newName != _renameOriginal) {
          drawBriefSplash("Renaming...");
          if (renameNote(_renameOriginal, newName)) {
            if (_currentFile == _renameOriginal) {
              _currentFile = newName;
            }
          }
        }
        scanFiles();
      }
      _mode = FILE_LIST;
      return true;
    }

    // Backspace
    if (c == '\b') {
      if (_renameLen > 0) {
        _renameLen--;
        _renameBuf[_renameLen] = '\0';
        return true;
      }
      return false;
    }

    // Printable characters (restrict to filename-safe chars)
    if (c >= 32 && c < 127 && _renameLen < NOTES_RENAME_MAX - 2) {
      if (c == '/' || c == '\\' || c == ':' || c == '*' ||
          c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
        return false;
      }
      _renameBuf[_renameLen++] = c;
      _renameBuf[_renameLen] = '\0';
      return true;
    }

    return false;
  }

  bool handleDeleteConfirmInput(char c) {
    // Enter - confirm delete
    if (c == '\r' || c == 13) {
      drawBriefSplash("Deleting...");
      deleteNote(_deleteTarget);
      _deleteTarget = "";
      _selectedFile = 0;
      _mode = FILE_LIST;
      scanFiles();
      return true;
    }

    // Q or backspace - cancel
    if (c == 'q' || c == 'Q' || c == '\b') {
      _deleteTarget = "";
      _mode = FILE_LIST;
      return true;
    }

    return false;
  }

  // ---- Note Creation ----

  void createNewNote() {
    _currentFile = generateFilename();
    _buf[0] = '\0';
    _bufLen = 0;
    _cursorPos = 0;
    _editorScrollTop = 0;
    _dirty = true;
    _mode = EDITING;
    Serial.printf("Notes: Created new note %s\n", _currentFile.c_str());
  }

public:
  NotesScreen(UITask* task)
    : _task(task), _mode(FILE_LIST),
      _sdReady(false), _initialized(false), _display(nullptr),
      _charsPerLine(38), _linesPerPage(22), _lineHeight(5), _footerHeight(14),
      _editCharsPerLine(20), _editLineHeight(12), _editMaxLines(8),
      _selectedFile(0), _buf(nullptr), _bufLen(0), _cursorPos(0),
      _dirty(false), _currentPage(0), _totalPages(0),
      _editorLines(nullptr), _numEditorLines(0), _editorScrollTop(0),
      _renameLen(0), _rtcTime(0), _utcOffset(0) {

    // Allocate main buffer on PSRAM if available
    #ifdef BOARD_HAS_PSRAM
      _buf = (char*)ps_malloc(NOTES_BUF_SIZE);
    #else
      _buf = (char*)malloc(NOTES_BUF_SIZE);
    #endif
    if (_buf) _buf[0] = '\0';
    else Serial.println("Notes: FATAL - buffer allocation failed!");

    // Allocate editor lines array
    #ifdef BOARD_HAS_PSRAM
      _editorLines = (EditorLine*)ps_malloc(sizeof(EditorLine) * NOTES_MAX_LINES);
    #else
      _editorLines = (EditorLine*)malloc(sizeof(EditorLine) * NOTES_MAX_LINES);
    #endif

    _renameBuf[0] = '\0';
  }

  ~NotesScreen() {
    if (_buf) free(_buf);
    if (_editorLines) free(_editorLines);
  }

  // ---- Layout Init ----

  void initLayout(DisplayDriver& display) {
    if (_initialized) return;
    _display = &display;

    // Tiny font metrics (for read mode)
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

    // Size 1 font metrics (for edit mode)
    display.setTextSize(1);
    uint16_t charW = display.getTextWidth("M");
    _editCharsPerLine = charW > 0 ? display.width() / charW : 20;
    if (_editCharsPerLine < 10) _editCharsPerLine = 10;
    if (_editCharsPerLine > 40) _editCharsPerLine = 40;

    _editLineHeight = 12;
    int editTextAreaH = display.height() - 14 - 16;  // Header + footer
    _editMaxLines = editTextAreaH / _editLineHeight;
    if (_editMaxLines < 3) _editMaxLines = 3;

    display.setTextSize(1);
    _initialized = true;

    Serial.printf("Notes layout: %d chars/line, %d lines/page, lineH=%d (edit: %d cpl, %d lines)\n",
                  _charsPerLine, _linesPerPage, _lineHeight,
                  _editCharsPerLine, _editMaxLines);
  }

  // ---- Public Interface ----

  void setSDReady(bool ready) { _sdReady = ready; }
  bool isSDReady() const { return _sdReady; }

  void setTimestamp(uint32_t rtcTime, int8_t utcOffset) {
    _rtcTime = rtcTime;
    _utcOffset = utcOffset;
  }

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
  bool isRenaming() const { return _mode == RENAMING; }
  bool isConfirmingDelete() const { return _mode == CONFIRM_DELETE; }
  bool isDirty() const { return _dirty; }
  bool isEmpty() const { return _bufLen == 0; }

  // ---- Cursor Navigation (called from main.cpp) ----

  void moveCursorLeft() {
    if (_cursorPos > 0) {
      _cursorPos--;
      while (_cursorPos > 0 && (uint8_t)_buf[_cursorPos] >= 0x80 &&
             (uint8_t)_buf[_cursorPos] < 0xC0) {
        _cursorPos--;
      }
    }
  }

  void moveCursorRight() {
    if (_cursorPos < _bufLen) {
      _cursorPos++;
      while (_cursorPos < _bufLen && (uint8_t)_buf[_cursorPos] >= 0x80 &&
             (uint8_t)_buf[_cursorPos] < 0xC0) {
        _cursorPos++;
      }
    }
  }

  void moveCursorUp() {
    buildEditorLines();
    int curLine = lineForPos(_cursorPos);
    if (curLine > 0) {
      int col = colForPos(_cursorPos, _editorLines[curLine].start);
      _cursorPos = posForCol(col, curLine - 1);
    }
  }

  void moveCursorDown() {
    buildEditorLines();
    int curLine = lineForPos(_cursorPos);
    if (curLine < _numEditorLines - 1) {
      int col = colForPos(_cursorPos, _editorLines[curLine].start);
      _cursorPos = posForCol(col, curLine + 1);
    }
  }

  // ---- File List Actions (called from main.cpp) ----

  bool startRename() {
    if (_selectedFile < 1 || _selectedFile > (int)_fileList.size()) return false;

    _renameOriginal = _fileList[_selectedFile - 1];

    // Strip .txt extension for editing
    String base = _renameOriginal;
    if (base.endsWith(".txt") || base.endsWith(".TXT")) {
      base = base.substring(0, base.length() - 4);
    }

    int len = min((int)base.length(), NOTES_RENAME_MAX - 2);
    memcpy(_renameBuf, base.c_str(), len);
    _renameBuf[len] = '\0';
    _renameLen = len;

    _mode = RENAMING;
    Serial.printf("Notes: Renaming %s\n", _renameOriginal.c_str());
    return true;
  }

  bool startDeleteFromList() {
    if (_selectedFile < 1 || _selectedFile > (int)_fileList.size()) return false;
    _deleteTarget = _fileList[_selectedFile - 1];
    _mode = CONFIRM_DELETE;
    Serial.printf("Notes: Confirm delete %s\n", _deleteTarget.c_str());
    return true;
  }

  void saveAndExit() {
    if (_dirty && _currentFile.length() > 0) {
      if (_bufLen > 0) {
        drawBriefSplash("Saving...");
        saveNote();
      } else if (_dirty) {
        Serial.printf("Notes: Skipping empty note %s\n", _currentFile.c_str());
      }
    }
    _mode = FILE_LIST;
    _dirty = false;
    scanFiles();
  }

  void discardAndExit() {
    _dirty = false;
    _mode = FILE_LIST;
    scanFiles();
  }

  void deleteCurrentNote() {
    if (_currentFile.length() > 0) {
      drawBriefSplash("Deleting...");
      deleteNote(_currentFile);
    }
    _dirty = false;
    _currentFile = "";
    _bufLen = 0;
    _buf[0] = '\0';
    _cursorPos = 0;
    _mode = FILE_LIST;
    _selectedFile = 0;
    scanFiles();
  }

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

    switch (_mode) {
      case FILE_LIST:       renderFileList(display); break;
      case READING:         renderReadPage(display); break;
      case EDITING:         renderEditor(display); break;
      case RENAMING:        renderRenameDialog(display); break;
      case CONFIRM_DELETE:  renderDeleteConfirm(display); break;
    }

    return 5000;
  }

  bool handleInput(char c) override {
    switch (_mode) {
      case FILE_LIST:       return handleFileListInput(c);
      case READING:         return handleReadInput(c);
      case EDITING:         return handleEditInput(c);
      case RENAMING:        return handleRenameInput(c);
      case CONFIRM_DELETE:  return handleDeleteConfirmInput(c);
    }
    return false;
  }
};