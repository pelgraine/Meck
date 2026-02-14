#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <SD.h>
#include <vector>
#include "Utf8CP437.h"
#include "EpubProcessor.h"

// Forward declarations
class UITask;

// ============================================================================
// Configuration
// ============================================================================
#define BOOKS_FOLDER      "/books"
#define INDEX_FOLDER      "/.indexes"
#define INDEX_VERSION     5  // v5: UTF-8 aware word wrap (accented char support)
#define PREINDEX_PAGES    100
#define READER_MAX_FILES  50
#define READER_BUF_SIZE   4096

// ============================================================================
// Word Wrap Helper (same algorithm as standalone reader)
// ============================================================================
struct WrapResult {
  int lineEnd;
  int nextStart;
};

inline WrapResult findLineBreak(const char* buffer, int bufLen, int lineStart, int maxChars) {
  WrapResult result;
  result.lineEnd = lineStart;
  result.nextStart = lineStart;

  if (lineStart >= bufLen) return result;

  int charCount = 0;
  int lastBreakPoint = -1;
  bool inWord = false;

  for (int i = lineStart; i < bufLen; i++) {
    char c = buffer[i];

    if (c == '\n') {
      result.lineEnd = i;
      result.nextStart = i + 1;
      if (result.nextStart < bufLen && buffer[result.nextStart] == '\r')
        result.nextStart++;
      return result;
    }
    if (c == '\r') {
      result.lineEnd = i;
      result.nextStart = i + 1;
      if (result.nextStart < bufLen && buffer[result.nextStart] == '\n')
        result.nextStart++;
      return result;
    }

    if (c >= 32) {
      // Skip UTF-8 continuation bytes (0x80-0xBF) - the lead byte already
      // counted as one display character, so don't double-count these.
      if ((uint8_t)c >= 0x80 && (uint8_t)c < 0xC0) continue;

      charCount++;
      if (c == ' ' || c == '\t') {
        if (inWord) {
          lastBreakPoint = i;
          inWord = false;
        }
      } else if (c == '-') {
        if (inWord) {
          lastBreakPoint = i + 1;
        }
      } else {
        inWord = true;
      }

      if (charCount >= maxChars) {
        if (lastBreakPoint > lineStart) {
          result.lineEnd = lastBreakPoint;
          result.nextStart = lastBreakPoint;
          while (result.nextStart < bufLen &&
                 (buffer[result.nextStart] == ' ' || buffer[result.nextStart] == '\t'))
            result.nextStart++;
        } else {
          result.lineEnd = i;
          result.nextStart = i;
        }
        return result;
      }
    }
  }

  result.lineEnd = bufLen;
  result.nextStart = bufLen;
  return result;
}

// ============================================================================
// Page Indexer (word-wrap aware, matches display rendering)
// ============================================================================
inline int indexPagesWordWrap(File& file, long startPos,
                              std::vector<long>& pagePositions,
                              int linesPerPage, int charsPerLine,
                              int maxPages) {
  const int BUF_SIZE = 2048;
  char buffer[BUF_SIZE];

  file.seek(startPos);
  int pagesAdded = 0;
  int lineCount = 0;
  int leftover = 0;
  long chunkFileStart = startPos;

  while (file.available() && (maxPages <= 0 || pagesAdded < maxPages)) {
    int bytesRead = file.readBytes(buffer + leftover, BUF_SIZE - leftover);
    int bufLen = leftover + bytesRead;
    if (bufLen == 0) break;

    int pos = 0;
    while (pos < bufLen) {
      WrapResult wrap = findLineBreak(buffer, bufLen, pos, charsPerLine);
      if (wrap.nextStart <= pos && wrap.lineEnd >= bufLen) break;

      lineCount++;
      pos = wrap.nextStart;

      if (lineCount >= linesPerPage) {
        long pageFilePos = chunkFileStart + pos;
        pagePositions.push_back(pageFilePos);
        pagesAdded++;
        lineCount = 0;
        if (maxPages > 0 && pagesAdded >= maxPages) break;
      }
      if (pos >= bufLen) break;
    }

    leftover = bufLen - pos;
    if (leftover > 0 && leftover < BUF_SIZE) {
      memmove(buffer, buffer + pos, leftover);
    } else {
      leftover = 0;
    }
    chunkFileStart = file.position() - leftover;
  }

  return pagesAdded;
}

// ============================================================================
// TextReaderScreen
// ============================================================================

class TextReaderScreen : public UIScreen {
public:
  enum Mode { FILE_LIST, READING };

  // File cache entry (index + resume position)
  struct FileCache {
    String filename;
    std::vector<long> pagePositions;
    unsigned long fileSize;
    bool fullyIndexed;
    int lastReadPage;
  };

private:
  UITask* _task;
  Mode _mode;
  bool _sdReady;
  bool _initialized;       // Layout metrics calculated
  bool _bootIndexed;       // Boot-time pre-indexing done
  DisplayDriver* _display; // Stored reference for splash screens

  // Display layout (calculated once from display metrics)
  int _charsPerLine;
  int _linesPerPage;
  int _lineHeight;     // virtual coord units per text line
  int _headerHeight;
  int _footerHeight;

  // File list state
  std::vector<String> _fileList;
  std::vector<String> _dirList;    // Subdirectories at current path
  std::vector<FileCache> _fileCache;
  int _selectedFile;
  String _currentPath;             // Current browsed directory

  // Reading state
  File _file;
  String _currentFile;
  bool _fileOpen;
  int _currentPage;
  int _totalPages;
  std::vector<long> _pagePositions;

  // Page content buffer (pre-read from SD before render)
  char _pageBuf[READER_BUF_SIZE];
  int _pageBufLen;
  bool _contentDirty;  // Need to re-read from SD

  // ---- Splash Screen Drawing ----
  // Draw directly to display outside the normal render cycle.
  // Matches the style of the standalone text reader firmware splash.

  // Generic splash screen: title (large green), subtitle (normal), detail (normal)
  void drawSplash(const char* title, const char* subtitle, const char* detail) {
    if (!_display) return;
    _display->startFrame();

    // Title in large text
    _display->setTextSize(2);
    _display->setColor(DisplayDriver::GREEN);
    _display->setCursor(10, 11);
    _display->print(title);

    _display->setTextSize(1);
    _display->setColor(DisplayDriver::LIGHT);

    int y = 35;

    // Subtitle
    if (subtitle && subtitle[0]) {
      _display->setCursor(10, y);
      _display->print(subtitle);
      y += 8;
    }

    // Detail line
    if (detail && detail[0]) {
      _display->setCursor(10, y);
      _display->print(detail);
    }

    _display->endFrame();
  }

  // Word-wrapping splash for opening a large book.
  // Shows: "Indexing / Pages..." (large), word-wrapped filename, "Please wait. / Loading shortly..."
  void drawIndexingSplash(const String& filename) {
    if (!_display) return;
    _display->startFrame();

    // "Indexing" / "Pages..." in large text
    // Original: textSize(2) at real (20, 40) and (20, 65)
    // Virtual: (10, 11) and (10, 21)
    _display->setTextSize(2);
    _display->setColor(DisplayDriver::GREEN);
    _display->setCursor(10, 11);
    _display->print("Indexing");
    _display->setCursor(10, 21);
    _display->print("Pages...");

    // Word-wrapped filename in normal text
    _display->setTextSize(1);
    _display->setColor(DisplayDriver::LIGHT);
    int y = 39;
    int leftMargin = 10;
    // Calculate max chars that fit: (display_width - margin) / char_width
    uint16_t charW = _display->getTextWidth("M");
    int maxChars = charW > 0 ? (_display->width() - leftMargin) / charW : 20;
    if (maxChars < 10) maxChars = 10;
    if (maxChars > 40) maxChars = 40;
    String remaining = filename;

    while (remaining.length() > 0 && y < 80) {
      String line;
      if ((int)remaining.length() <= maxChars) {
        line = remaining;
        remaining = "";
      } else {
        int breakPoint = maxChars;
        for (int i = maxChars; i > 0; i--) {
          if (remaining[i] == ' ' || remaining[i] == '-' || remaining[i] == '_') {
            breakPoint = i;
            break;
          }
        }
        line = remaining.substring(0, breakPoint);
        remaining = remaining.substring(breakPoint);
        remaining.trim();
      }
      _display->setCursor(10, y);
      _display->print(line.c_str());
      y += 5;
    }

    // "Please wait." / "Loading shortly..."
    // Original: textSize(1) at real (20, 230) and (20, 245)
    // Virtual: y=87 and y=93
    _display->setColor(DisplayDriver::LIGHT);
    _display->setCursor(10, 87);
    _display->print("Please wait.");
    _display->setCursor(10, 93);
    _display->print("Loading shortly...");

    _display->endFrame();
  }

  // Boot-time progress splash with file counter.
  // Shows: "Indexing / Pages..." (large), "(2/10)", word-wrapped filename, "Please wait."
  // If current==0 and total==0, skips the progress counter (used for initial scan splash).
  void drawBootSplash(int current, int total, const String& filename) {
    if (!_display) return;
    _display->startFrame();

    // "Indexing" / "Pages..." in large text
    _display->setTextSize(2);
    _display->setColor(DisplayDriver::GREEN);
    _display->setCursor(10, 11);
    _display->print("Indexing");
    _display->setCursor(10, 21);
    _display->print("Pages...");

    _display->setTextSize(1);
    _display->setColor(DisplayDriver::LIGHT);

    int y = 35;

    // Progress counter (skip if both zero)
    if (current > 0 || total > 0) {
      char progress[20];
      sprintf(progress, "(%d/%d)", current, total);
      _display->setCursor(10, y);
      _display->print(progress);
      y += 8;
    }

    // Word-wrapped filename
    int leftMargin = 10;
    uint16_t charW = _display->getTextWidth("M");
    int maxChars = charW > 0 ? (_display->width() - leftMargin) / charW : 20;
    if (maxChars < 10) maxChars = 10;
    if (maxChars > 40) maxChars = 40;
    String remaining = filename;

    while (remaining.length() > 0 && y < 80) {
      String line;
      if ((int)remaining.length() <= maxChars) {
        line = remaining;
        remaining = "";
      } else {
        int breakPoint = maxChars;
        for (int i = maxChars; i > 0; i--) {
          if (remaining[i] == ' ' || remaining[i] == '-' || remaining[i] == '_') {
            breakPoint = i;
            break;
          }
        }
        line = remaining.substring(0, breakPoint);
        remaining = remaining.substring(breakPoint);
        remaining.trim();
      }
      _display->setCursor(10, y);
      _display->print(line.c_str());
      y += 5;
    }

    // "Please wait."
    _display->setCursor(10, 87);
    _display->print("Please wait.");

    _display->endFrame();
  }

  // ---- SD Index I/O ----

  String getIndexPath(const String& filename) {
    return String(INDEX_FOLDER) + "/" + filename + ".idx";
  }

  bool loadIndex(const String& filename, FileCache& cache) {
    String idxPath = getIndexPath(filename);
    File idxFile = SD.open(idxPath.c_str(), FILE_READ);
    if (!idxFile) return false;

    uint8_t version = 0;
    unsigned long savedSize = 0, pageCount = 0;
    uint8_t fullyFlag = 0;
    int lastRead = 0;

    idxFile.read(&version, 1);
    if (version != INDEX_VERSION) {
      // Wrong version - discard and rebuild
      idxFile.close();
      SD.remove(idxPath.c_str());
      return false;
    }

    idxFile.read((uint8_t*)&savedSize, 4);
    idxFile.read((uint8_t*)&pageCount, 4);
    idxFile.read(&fullyFlag, 1);
    idxFile.read((uint8_t*)&lastRead, 4);

    // Verify file hasn't changed - try current path first, then epub cache
    String fullPath = _currentPath + "/" + filename;
    File txtFile = SD.open(fullPath.c_str(), FILE_READ);
    if (!txtFile) {
      // Fallback: check epub cache directory
      String cachePath = String("/books/.epub_cache/") + filename;
      txtFile = SD.open(cachePath.c_str(), FILE_READ);
    }
    if (!txtFile) { idxFile.close(); return false; }
    unsigned long curSize = txtFile.size();
    txtFile.close();

    if (savedSize != curSize) {
      idxFile.close();
      SD.remove(idxPath.c_str());
      return false;
    }

    cache.filename = filename;
    cache.fileSize = savedSize;
    cache.fullyIndexed = (fullyFlag == 1);
    cache.lastReadPage = lastRead;
    cache.pagePositions.clear();

    for (unsigned long i = 0; i < pageCount; i++) {
      long pos = 0;
      idxFile.read((uint8_t*)&pos, 4);
      cache.pagePositions.push_back(pos);
    }

    idxFile.close();
    return true;
  }

  bool saveIndex(const String& filename, const std::vector<long>& pages,
                 unsigned long fileSize, bool fullyIndexed, int lastReadPage) {
    if (!SD.exists(INDEX_FOLDER)) SD.mkdir(INDEX_FOLDER);

    String idxPath = getIndexPath(filename);
    if (SD.exists(idxPath.c_str())) SD.remove(idxPath.c_str());

    File idxFile = SD.open(idxPath.c_str(), FILE_WRITE);
    if (!idxFile) return false;

    uint8_t version = INDEX_VERSION;
    unsigned long pageCount = pages.size();
    uint8_t fullyFlag = fullyIndexed ? 1 : 0;

    idxFile.write(&version, 1);
    idxFile.write((uint8_t*)&fileSize, 4);
    idxFile.write((uint8_t*)&pageCount, 4);
    idxFile.write(&fullyFlag, 1);
    idxFile.write((uint8_t*)&lastReadPage, 4);

    for (unsigned long i = 0; i < pageCount; i++) {
      long pos = pages[i];
      idxFile.write((uint8_t*)&pos, 4);
    }
    idxFile.close();
    return true;
  }

  bool saveReadingPosition(const String& filename, int page) {
    String idxPath = getIndexPath(filename);
    File idxFile = SD.open(idxPath.c_str(), "r+");
    if (!idxFile) return false;

    uint8_t version = 0;
    idxFile.read(&version, 1);

    if (version != INDEX_VERSION) {
      idxFile.close();
      for (int i = 0; i < (int)_fileCache.size(); i++) {
        if (_fileCache[i].filename == filename) {
          _fileCache[i].lastReadPage = page;
          return saveIndex(filename, _fileCache[i].pagePositions,
                           _fileCache[i].fileSize, _fileCache[i].fullyIndexed, page);
        }
      }
      return false;
    }

    // Seek to lastReadPage field: version(1) + fileSize(4) + pageCount(4) + fullyIndexed(1)
    idxFile.seek(1 + 4 + 4 + 1);
    idxFile.write((uint8_t*)&page, 4);
    idxFile.close();
    return true;
  }

  // ---- File Scanning ----

  // ---- Folder Navigation Helpers ----

  bool isAtBooksRoot() const {
    return _currentPath == String(BOOKS_FOLDER);
  }

  // Number of non-file entries at the start of the visual list
  int dirEntryCount() const {
    int count = _dirList.size();
    if (!isAtBooksRoot()) count++;  // ".." entry
    return count;
  }

  // Total items in the visual list (parent + dirs + files)
  int totalListItems() const {
    return dirEntryCount() + (int)_fileList.size();
  }

  // What type of entry is at visual list index idx?
  // Returns: 0 = ".." parent, 1 = directory, 2 = file
  int itemTypeAt(int idx) const {
    bool hasParent = !isAtBooksRoot();
    if (hasParent && idx == 0) return 0;  // ".."
    int dirStart = hasParent ? 1 : 0;
    if (idx < dirStart + (int)_dirList.size()) return 1;  // directory
    return 2;  // file
  }

  // Get directory name for visual index (only valid when itemTypeAt == 1)
  const String& dirNameAt(int idx) const {
    int dirStart = isAtBooksRoot() ? 0 : 1;
    return _dirList[idx - dirStart];
  }

  // Get file list index for visual index (only valid when itemTypeAt == 2)
  int fileIndexAt(int idx) const {
    return idx - dirEntryCount();
  }

  void navigateToParent() {
    int lastSlash = _currentPath.lastIndexOf('/');
    if (lastSlash > 0) {
      _currentPath = _currentPath.substring(0, lastSlash);
    } else {
      _currentPath = BOOKS_FOLDER;
    }
  }

  void navigateToChild(const String& dirName) {
    _currentPath = _currentPath + "/" + dirName;
  }

  // ---- File Scanning ----

  void scanFiles() {
    _fileList.clear();
    _dirList.clear();
    if (!SD.exists(BOOKS_FOLDER)) {
      SD.mkdir(BOOKS_FOLDER);
      Serial.printf("TextReader: Created %s\n", BOOKS_FOLDER);
    }

    File root = SD.open(_currentPath.c_str());
    if (!root || !root.isDirectory()) return;

    File f = root.openNextFile();
    while (f && (_fileList.size() + _dirList.size()) < READER_MAX_FILES) {
      String name = String(f.name());
      int slash = name.lastIndexOf('/');
      if (slash >= 0) name = name.substring(slash + 1);

      // Skip hidden files/dirs
      if (name.startsWith(".")) {
        f = root.openNextFile();
        continue;
      }

      if (f.isDirectory()) {
        _dirList.push_back(name);
      } else if (name.endsWith(".txt") || name.endsWith(".TXT") ||
                 name.endsWith(".epub") || name.endsWith(".EPUB")) {
        _fileList.push_back(name);
      }
      f = root.openNextFile();
    }
    root.close();
    Serial.printf("TextReader: %s — %d dirs, %d files\n",
                  _currentPath.c_str(), (int)_dirList.size(), (int)_fileList.size());
  }

  // ---- Book Open/Close ----

  void openBook(const String& filename) {
    if (_fileOpen) closeBook();

    // ---- EPUB auto-conversion ----
    String actualFilename = filename;
    String actualFullPath = _currentPath + "/" + filename;
    bool isEpub = filename.endsWith(".epub") || filename.endsWith(".EPUB");

    if (isEpub) {
      // Build cache path for this EPUB
      char cachePath[160];
      EpubProcessor::buildCachePath(actualFullPath.c_str(), cachePath, sizeof(cachePath));

      // Check if already converted
      digitalWrite(SDCARD_CS, LOW);
      bool cached = SD.exists(cachePath);
      digitalWrite(SDCARD_CS, HIGH);

      if (!cached) {
        // Show conversion splash on e-ink
        char shortName[28];
        if (filename.length() > 24) {
          strncpy(shortName, filename.c_str(), 21);
          shortName[21] = '\0';
          strcat(shortName, "...");
        } else {
          strncpy(shortName, filename.c_str(), sizeof(shortName) - 1);
          shortName[sizeof(shortName) - 1] = '\0';
        }
        drawSplash("Converting EPUB...", "Please wait", shortName);

        Serial.printf("TextReader: Converting EPUB '%s'\n", filename.c_str());
        unsigned long t0 = millis();

        digitalWrite(SDCARD_CS, LOW);
        bool ok = EpubProcessor::processToText(actualFullPath.c_str(), cachePath);
        digitalWrite(SDCARD_CS, HIGH);

        if (!ok) {
          Serial.println("TextReader: EPUB conversion failed!");
          drawSplash("Convert failed!", "", shortName);
          delay(2000);
          return;  // Stay in file list
        }
        Serial.printf("TextReader: EPUB converted in %lu ms\n", millis() - t0);
      } else {
        Serial.printf("TextReader: EPUB cache hit for '%s'\n", filename.c_str());
      }

      // Redirect to the cached .txt
      actualFullPath = String(cachePath);
      const char* lastSlash = strrchr(cachePath, '/');
      actualFilename = String(lastSlash ? lastSlash + 1 : cachePath);
    }
    // ---- End EPUB auto-conversion ----

    // Find cached index for this file
    FileCache* cache = nullptr;
    for (int i = 0; i < (int)_fileCache.size(); i++) {
      if (_fileCache[i].filename == actualFilename) {
        cache = &_fileCache[i];
        break;
      }
    }

    _file = SD.open(actualFullPath.c_str(), FILE_READ);

    // Fallback: try epub cache dir (for files discovered during boot scan)
    if (!_file && !isEpub) {
      String cacheFallback = String("/books/.epub_cache/") + actualFilename;
      _file = SD.open(cacheFallback.c_str(), FILE_READ);
      if (_file) {
        actualFullPath = cacheFallback;
        Serial.printf("TextReader: Opened from epub cache: %s\n", actualFilename.c_str());
      }
    }

    if (!_file) {
      Serial.printf("TextReader: Failed to open %s\n", actualFilename.c_str());
      return;
    }

    _currentFile = actualFilename;
    _fileOpen = true;
    _currentPage = 0;
    _pagePositions.clear();

    if (cache) {
      for (int i = 0; i < (int)cache->pagePositions.size(); i++) {
        _pagePositions.push_back(cache->pagePositions[i]);
      }
      if (cache->lastReadPage > 0 && cache->lastReadPage < (int)cache->pagePositions.size()) {
        _currentPage = cache->lastReadPage;
      }

      // Already fully indexed â€” open immediately
      if (cache->fullyIndexed) {
        _totalPages = _pagePositions.size();
        _mode = READING;
        loadPageContent();
        Serial.printf("TextReader: Opened %s, %d pages, resume pg %d\n",
                      actualFilename.c_str(), _totalPages, _currentPage + 1);
        return;
      }

      // Partially indexed â€” finish indexing with splash
      Serial.printf("TextReader: Finishing index for %s (have %d pages so far)\n",
                    actualFilename.c_str(), (int)_pagePositions.size());

      char shortName[28];
      if (actualFilename.length() > 24) {
        strncpy(shortName, actualFilename.c_str(), 21);
        shortName[21] = '\0';
        strcat(shortName, "...");
      } else {
        strncpy(shortName, actualFilename.c_str(), sizeof(shortName) - 1);
        shortName[sizeof(shortName) - 1] = '\0';
      }
      drawSplash("Indexing...", "Please wait", shortName);

      if (_pagePositions.empty()) {
        // Cache had no pages (e.g. dummy entry) â€” full index from scratch
        _pagePositions.push_back(0);
        indexPagesWordWrap(_file, 0, _pagePositions,
                           _linesPerPage, _charsPerLine, 0);
      } else {
        long lastPos = cache->pagePositions.back();
        indexPagesWordWrap(_file, lastPos, _pagePositions,
                           _linesPerPage, _charsPerLine, 0);
      }
    } else {
      // No cache â€” full index from scratch
      Serial.printf("TextReader: Full index for %s\n", actualFilename.c_str());

      char shortName[28];
      if (actualFilename.length() > 24) {
        strncpy(shortName, actualFilename.c_str(), 21);
        shortName[21] = '\0';
        strcat(shortName, "...");
      } else {
        strncpy(shortName, actualFilename.c_str(), sizeof(shortName) - 1);
        shortName[sizeof(shortName) - 1] = '\0';
      }
      drawSplash("Indexing...", "Please wait", shortName);

      _pagePositions.push_back(0);
      indexPagesWordWrap(_file, 0, _pagePositions,
                         _linesPerPage, _charsPerLine, 0);
    }

    // Save complete index
    _totalPages = _pagePositions.size();

    // Update or create cache entry
    bool foundCache = false;
    for (int i = 0; i < (int)_fileCache.size(); i++) {
      if (_fileCache[i].filename == actualFilename) {
        _fileCache[i].pagePositions = _pagePositions;
        _fileCache[i].fullyIndexed = true;
        _fileCache[i].fileSize = _file.size();
        foundCache = true;
        break;
      }
    }
    if (!foundCache) {
      FileCache newCache;
      newCache.filename = actualFilename;
      newCache.fileSize = _file.size();
      newCache.fullyIndexed = true;
      newCache.lastReadPage = _currentPage;
      newCache.pagePositions = _pagePositions;
      _fileCache.push_back(newCache);
    }

    saveIndex(actualFilename, _pagePositions, _file.size(), true, _currentPage);

    _mode = READING;
    loadPageContent();
    Serial.printf("TextReader: Opened %s, %d pages\n",
                  actualFilename.c_str(), _totalPages);
  }

  void closeBook() {
    if (!_fileOpen) return;
    saveReadingPosition(_currentFile, _currentPage);

    for (int i = 0; i < (int)_fileCache.size(); i++) {
      if (_fileCache[i].filename == _currentFile) {
        _fileCache[i].lastReadPage = _currentPage;
        break;
      }
    }

    _file.close();
    _fileOpen = false;
    _pagePositions.clear();
    _pagePositions.shrink_to_fit();
    // Deselect SD to free SPI bus
    digitalWrite(SDCARD_CS, HIGH);
    Serial.printf("TextReader: Closed, saved at page %d\n", _currentPage + 1);
  }

  // ---- Page Content Loading ----
  // Read exact span between indexed page positions so renderer gets
  // exactly the bytes the indexer counted for this page.

  void loadPageContent() {
    if (!_fileOpen || _currentPage >= _totalPages) {
      _pageBufLen = 0;
      return;
    }

    long pageStart = _pagePositions[_currentPage];
    long pageEnd;
    if (_currentPage + 1 < _totalPages) {
      pageEnd = _pagePositions[_currentPage + 1];
    } else {
      // Last page - read remaining file content
      pageEnd = _file.size();
    }

    long pageSpan = pageEnd - pageStart;
    int toRead = (int)min((long)(READER_BUF_SIZE - 1), pageSpan);

    _file.seek(pageStart);
    _pageBufLen = _file.readBytes(_pageBuf, toRead);
    _pageBuf[_pageBufLen] = '\0';
    _contentDirty = false;

    // Deselect SD to free SPI bus for display
    digitalWrite(SDCARD_CS, HIGH);
  }

  // ---- Rendering Helpers ----

  void renderFileList(DisplayDriver& display) {
    char tmp[40];

    // Header
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    if (isAtBooksRoot()) {
      display.print("Text Reader");
    } else {
      // Show current subfolder name
      int lastSlash = _currentPath.lastIndexOf('/');
      String folderName = (lastSlash >= 0) ? _currentPath.substring(lastSlash + 1) : _currentPath;
      char hdrBuf[20];
      strncpy(hdrBuf, folderName.c_str(), 17);
      hdrBuf[17] = '\0';
      display.print(hdrBuf);
    }

    int totalItems = totalListItems();
    sprintf(tmp, "[%d]", totalItems);
    display.setCursor(display.width() - display.getTextWidth(tmp) - 2, 0);
    display.print(tmp);

    display.drawRect(0, 11, display.width(), 1);

    if (totalItems == 0) {
      display.setCursor(0, 18);
      display.setColor(DisplayDriver::LIGHT);
      display.print("No files found");
      display.setCursor(0, 30);
      display.print("Add .txt or .epub to");
      display.setCursor(0, 42);
      display.print("/books/ on SD card");
    } else {
      display.setTextSize(0);  // Tiny font for file list
      int listLineH = 8;  // Approximate tiny font line height in virtual coords
      int startY = 14;
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
          // setCursor adds +5 to y internally, but fillRect does not.
          // Offset fillRect by +5 to align highlight bar with text.
          display.fillRect(0, y + 5, display.width(), listLineH);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        // Set cursor AFTER fillRect so text draws on top of highlight
        display.setCursor(0, y);

        int type = itemTypeAt(i);
        String line = selected ? "> " : "  ";

        if (type == 0) {
          // ".." parent directory
          line += ".. (up)";
        } else if (type == 1) {
          // Subdirectory
          line += "/" + dirNameAt(i);
          // Truncate if needed
          if ((int)line.length() > _charsPerLine) {
            line = line.substring(0, _charsPerLine - 3) + "...";
          }
        } else {
          // File
          int fi = fileIndexAt(i);
          String name = _fileList[fi];

          // Check for resume indicator
          String suffix = "";
          if (fi < (int)_fileCache.size()) {
            if (_fileCache[fi].filename == name && _fileCache[fi].lastReadPage > 0) {
              suffix = " *";
            }
          }

          // Truncate if needed
          int maxLen = _charsPerLine - 4 - suffix.length();
          if ((int)name.length() > maxLen) {
            name = name.substring(0, maxLen - 3) + "...";
          }
          line += name + suffix;
        }

        display.print(line.c_str());
        y += listLineH;
      }
      display.setTextSize(1);  // Restore
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("Q:Back W/S:Nav");

    const char* right = "Ent:Open";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  void renderPage(DisplayDriver& display) {
    // Use tiny font for maximum text density
    display.setTextSize(0);
    display.setColor(DisplayDriver::LIGHT);

    int y = 0;
    int lineCount = 0;
    int pos = 0;
    int maxY = display.height() - _footerHeight - _lineHeight;

    // Render all lines in the page buffer using word wrap.
    // The buffer contains exactly the bytes for this page (from indexed positions),
    // so we render everything in it.
    while (pos < _pageBufLen && lineCount < _linesPerPage && y <= maxY) {
      int oldPos = pos;
      WrapResult wrap = findLineBreak(_pageBuf, _pageBufLen, pos, _charsPerLine);

      // Safety: stop if findLineBreak made no progress (stuck at end of buffer)
      if (wrap.nextStart <= oldPos && wrap.lineEnd >= _pageBufLen) break;

      display.setCursor(0, y);
      // Print line with UTF-8 decoding: multi-byte sequences are decoded
      // to Unicode codepoints, then mapped to CP437 for the built-in font.
      char charStr[2] = {0, 0};
      int j = pos;
      while (j < wrap.lineEnd && j < _pageBufLen) {
        uint8_t b = (uint8_t)_pageBuf[j];

        if (b < 32) {
          // Control character — skip
          j++;
          continue;
        }

        if (b < 0x80) {
          // Plain ASCII — print directly
          charStr[0] = (char)b;
          display.print(charStr);
          j++;
        } else if (b >= 0xC0) {
          // UTF-8 lead byte — decode full sequence and map to CP437
          int savedJ = j;
          uint32_t cp = decodeUtf8Char(_pageBuf, wrap.lineEnd, &j);
          uint8_t glyph = unicodeToCP437(cp);
          if (glyph) {
            charStr[0] = (char)glyph;
            display.print(charStr);
          }
          // If unmappable (glyph==0), just skip the character
        } else {
          // Standalone byte 0x80-0xBF: not a valid UTF-8 lead byte.
          // Treat as CP437 pass-through (e.g. from EPUB numeric entity decoding).
          charStr[0] = (char)b;
          display.print(charStr);
          j++;
        }
      }

      y += _lineHeight;
      lineCount++;
      pos = wrap.nextStart;
      if (pos >= _pageBufLen) break;
    }

    // Restore text size for footer
    display.setTextSize(1);

    // Footer: page info on left, navigation hints on right
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    char status[30];
    int pct = _totalPages > 1 ? (_currentPage * 100) / (_totalPages - 1) : 100;
    sprintf(status, "%d/%d %d%%", _currentPage + 1, _totalPages, pct);
    display.setCursor(0, footerY);
    display.print(status);

    const char* right = "W/S:Nav Q:Back";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

public:
  TextReaderScreen(UITask* task)
    : _task(task), _mode(FILE_LIST), _sdReady(false), _initialized(false),
      _bootIndexed(false), _display(nullptr),
      _charsPerLine(38), _linesPerPage(22), _lineHeight(5),
      _headerHeight(14), _footerHeight(14),
      _selectedFile(0), _currentPath(BOOKS_FOLDER),
      _fileOpen(false), _currentPage(0), _totalPages(0),
      _pageBufLen(0), _contentDirty(true) {
  }

  // Call once after display is available to calculate layout metrics
  void initLayout(DisplayDriver& display) {
    if (_initialized) return;

    // Store display reference for splash screens during openBook
    _display = &display;

    // Measure tiny font metrics using the display driver
    display.setTextSize(0);

    // Measure character width: use 10 M's to get accurate average
    uint16_t tenCharsW = display.getTextWidth("MMMMMMMMMM");
    if (tenCharsW > 0) {
      _charsPerLine = (display.width() * 10) / tenCharsW;
    }
    if (_charsPerLine < 15) _charsPerLine = 15;
    if (_charsPerLine > 60) _charsPerLine = 60;

    // Line height for built-in 6x8 font:
    // setCursor adds +5 to y, so effective text top = (y+5)*scale_y
    // The font is ~8px tall in real coords. In virtual coords: 8/scale_y ~ 3.2 units
    // We derive from measured char width since we can't measure height directly.
    // Built-in font: 6px wide, 8px tall -> height ~ width * 7/6
    // Then add ~20% for spacing
    uint16_t mWidth = display.getTextWidth("M");
    if (mWidth > 0) {
      _lineHeight = max(3, (int)((mWidth * 7 * 12) / (6 * 10)));
    } else {
      _lineHeight = 5;  // Safe fallback
    }

    _headerHeight = 0;  // No header in reading mode (maximize text area)
    _footerHeight = 14;
    int textAreaHeight = display.height() - _headerHeight - _footerHeight;
    _linesPerPage = textAreaHeight / _lineHeight;
    if (_linesPerPage < 5) _linesPerPage = 5;
    if (_linesPerPage > 40) _linesPerPage = 40;

    display.setTextSize(1);  // Restore
    _initialized = true;

    Serial.printf("TextReader layout: %d chars/line, %d lines/page, lineH=%d (display %dx%d)\n",
                  _charsPerLine, _linesPerPage, _lineHeight, display.width(), display.height());
  }

  // ---- Boot-time Indexing ----
  // Called from setup() after SD card init. Scans files, pre-indexes first
  // 100 pages of each, and shows progress on the e-ink display.

  void bootIndex(DisplayDriver& display) {
    if (!_sdReady) return;

    // Calculate layout metrics first (needed for indexing)
    initLayout(display);

    // Show initial splash
    drawBootSplash(0, 0, "Scanning...");
    Serial.println("TextReader: Boot indexing started");

    // Scan for files (includes .txt and .epub)
    scanFiles();

    // Also pick up previously converted EPUB cache files
    if (SD.exists("/books/.epub_cache")) {
      File cacheDir = SD.open("/books/.epub_cache");
      if (cacheDir && cacheDir.isDirectory()) {
        File f = cacheDir.openNextFile();
        while (f && _fileList.size() < READER_MAX_FILES) {
          if (!f.isDirectory()) {
            String name = String(f.name());
            int slash = name.lastIndexOf('/');
            if (slash >= 0) name = name.substring(slash + 1);
            if (name.endsWith(".txt") || name.endsWith(".TXT")) {
              // Avoid duplicates
              bool dup = false;
              for (int i = 0; i < (int)_fileList.size(); i++) {
                if (_fileList[i] == name) { dup = true; break; }
              }
              if (!dup) {
                _fileList.push_back(name);
                Serial.printf("TextReader: Found cached EPUB txt: %s\n", name.c_str());
              }
            }
          }
          f = cacheDir.openNextFile();
        }
        cacheDir.close();
      }
    }

    if (_fileList.size() == 0) {
      Serial.println("TextReader: No files to index");
      _bootIndexed = true;
      return;
    }

    // --- Pass 1: Fast cache load (no per-file splash screens) ---
    // Try to load existing .idx files from SD for every file.
    // This is just SD reads â€” no indexing, no e-ink refreshes.
    _fileCache.clear();
    _fileCache.resize(_fileList.size());  // Pre-allocate slots to maintain alignment with _fileList

    int cachedCount = 0;
    int needsIndexCount = 0;

    for (int i = 0; i < (int)_fileList.size(); i++) {
      if (loadIndex(_fileList[i], _fileCache[i])) {
        Serial.printf("TextReader: %s - cached %d pages (resume pg %d)\n",
                      _fileList[i].c_str(), _fileCache[i].pagePositions.size(),
                      _fileCache[i].lastReadPage + 1);
        cachedCount++;
      } else {
        // Mark as needing indexing (filename will be empty)
        _fileCache[i].filename = "";
        needsIndexCount++;
      }
    }

    Serial.printf("TextReader: %d cached, %d need indexing\n", cachedCount, needsIndexCount);

    // --- Pass 2: Index only new/changed files (with splash screens) ---
    if (needsIndexCount > 0) {
      int indexProgress = 0;
      for (int i = 0; i < (int)_fileList.size(); i++) {
        // Skip files that loaded from cache
        if (_fileCache[i].filename.length() > 0) continue;

        // Skip .epub files â€” they'll be converted on first open via openBook()
        if (_fileList[i].endsWith(".epub") || _fileList[i].endsWith(".EPUB")) {
          needsIndexCount--;  // Don't count epubs in progress display
          continue;
        }

        indexProgress++;
        drawBootSplash(indexProgress, needsIndexCount, _fileList[i]);

        // Try current path first, then epub cache fallback
        String fullPath = _currentPath + "/" + _fileList[i];
        File file = SD.open(fullPath.c_str(), FILE_READ);
        if (!file) {
          String cacheFallback = String("/books/.epub_cache/") + _fileList[i];
          file = SD.open(cacheFallback.c_str(), FILE_READ);
        }
        if (!file) continue;

        FileCache& cache = _fileCache[i];
        cache.filename = _fileList[i];
        cache.fileSize = file.size();
        cache.fullyIndexed = false;
        cache.lastReadPage = 0;
        cache.pagePositions.clear();
        cache.pagePositions.push_back(0);

        int added = indexPagesWordWrap(file, 0, cache.pagePositions,
                                       _linesPerPage, _charsPerLine,
                                       PREINDEX_PAGES - 1);
        cache.fullyIndexed = !file.available();
        file.close();

        saveIndex(cache.filename, cache.pagePositions, cache.fileSize,
                  cache.fullyIndexed, 0);

        Serial.printf("TextReader: %s - indexed %d pages%s\n",
                      _fileList[i].c_str(), (int)cache.pagePositions.size(),
                      cache.fullyIndexed ? " (complete)" : "");
      }
    }

    // Deselect SD to free SPI bus
    digitalWrite(SDCARD_CS, HIGH);

    _bootIndexed = true;
    Serial.printf("TextReader: Boot indexing complete, %d files (%d cached, %d newly indexed)\n",
                  (int)_fileList.size(), cachedCount, needsIndexCount);
  }

  // ---- Public Interface ----

  void setSDReady(bool ready) { _sdReady = ready; }
  bool isSDReady() const { return _sdReady; }

  // Called when entering the reader screen (press R).
  // If boot indexing already ran, this is lightweight.
  void enter(DisplayDriver& display) {
    initLayout(display);

    if (_sdReady && !_bootIndexed) {
      // Boot indexing didn't run (shouldn't happen, but safety fallback)
      bootIndex(display);
    }

    if (!_fileOpen) {
      _selectedFile = 0;
      _mode = FILE_LIST;
    } else {
      _mode = READING;
      loadPageContent();
    }
  }

  // Are we currently reading a book? (for key routing in main.cpp)
  bool isReading() const { return _mode == READING; }
  bool isInFileList() const { return _mode == FILE_LIST; }

  int render(DisplayDriver& display) override {
    if (!_sdReady) {
      display.setCursor(0, 20);
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.print("SD card not found");
      display.setCursor(0, 35);
      display.print("Insert SD with /books/");
      return 5000;
    }

    if (_mode == FILE_LIST) {
      renderFileList(display);
    } else if (_mode == READING) {
      renderPage(display);
    }

    return 5000;  // E-ink refresh interval
  }

  bool handleInput(char c) override {
    if (_mode == FILE_LIST) {
      return handleFileListInput(c);
    } else if (_mode == READING) {
      return handleReadingInput(c);
    }
    return false;
  }

  bool handleFileListInput(char c) {
    int total = totalListItems();

    // W - scroll up
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_selectedFile > 0) {
        _selectedFile--;
        return true;
      }
      return false;
    }

    // S - scroll down
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_selectedFile < total - 1) {
        _selectedFile++;
        return true;
      }
      return false;
    }

    // Enter - open selected item (directory or file)
    if (c == '\r' || c == 13) {
      if (total == 0 || _selectedFile >= total) return false;

      int type = itemTypeAt(_selectedFile);

      if (type == 0) {
        // ".." — navigate to parent
        navigateToParent();
        rescanAndIndex();
        return true;
      } else if (type == 1) {
        // Subdirectory — navigate into it
        navigateToChild(dirNameAt(_selectedFile));
        rescanAndIndex();
        return true;
      } else {
        // File — open it
        int fi = fileIndexAt(_selectedFile);
        if (fi >= 0 && fi < (int)_fileList.size()) {
          openBook(_fileList[fi]);
          return true;
        }
      }
      return false;
    }

    return false;
  }

  // Rescan current directory and re-index its files.
  // Called when navigating into or out of a subfolder.
  void rescanAndIndex() {
    scanFiles();
    _selectedFile = 0;

    // Rebuild file cache for the new directory's files
    _fileCache.clear();
    _fileCache.resize(_fileList.size());

    for (int i = 0; i < (int)_fileList.size(); i++) {
      if (!loadIndex(_fileList[i], _fileCache[i])) {
        // Not cached — skip EPUB auto-indexing here (it happens on open)
        // For .txt files, index now
        if (!(_fileList[i].endsWith(".epub") || _fileList[i].endsWith(".EPUB"))) {
          String fullPath = _currentPath + "/" + _fileList[i];
          File file = SD.open(fullPath.c_str(), FILE_READ);
          if (!file) {
            // Try epub cache fallback
            String cacheFallback = String("/books/.epub_cache/") + _fileList[i];
            file = SD.open(cacheFallback.c_str(), FILE_READ);
          }
          if (file) {
            FileCache& cache = _fileCache[i];
            cache.filename = _fileList[i];
            cache.fileSize = file.size();
            cache.fullyIndexed = false;
            cache.lastReadPage = 0;
            cache.pagePositions.clear();
            cache.pagePositions.push_back(0);
            indexPagesWordWrap(file, 0, cache.pagePositions,
                               _linesPerPage, _charsPerLine,
                               PREINDEX_PAGES - 1);
            cache.fullyIndexed = !file.available();
            file.close();
            saveIndex(cache.filename, cache.pagePositions, cache.fileSize,
                      cache.fullyIndexed, 0);
          }
        } else {
          _fileCache[i].filename = "";
        }
      }
      yield();  // Feed WDT between files
    }
    digitalWrite(SDCARD_CS, HIGH);
  }

  bool handleReadingInput(char c) {
    // W/A - previous page
    if (c == 'w' || c == 'W' || c == 'a' || c == 'A' || c == 0xF2) {
      if (_currentPage > 0) {
        _currentPage--;
        loadPageContent();
        return true;
      }
      return false;
    }

    // S/D/Space/Enter - next page
    if (c == 's' || c == 'S' || c == 'd' || c == 'D' ||
        c == ' ' || c == '\r' || c == 13 || c == 0xF1) {
      if (_currentPage < _totalPages - 1) {
        _currentPage++;
        loadPageContent();
        return true;
      }
      return false;
    }

    // Q - close book, back to file list
    if (c == 'q' || c == 'Q') {
      closeBook();
      _mode = FILE_LIST;
      return true;
    }

    return false;
  }

  // External close (called when leaving reader screen entirely)
  void exitReader() {
    if (_fileOpen) closeBook();
    _mode = FILE_LIST;
  }
};