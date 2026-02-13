#pragma once

// =============================================================================
// AudiobookPlayerScreen.h - Audiobook player for LilyGo T-Deck Pro
//
// Features:
//   - Browses /audiobooks/ on SD card for .m4b, .m4a, .mp3, .wav files
//   - Parses M4B metadata (title, author, cover art, chapters)
//   - Displays dithered cover art on e-ink (JPEG decode via JPEGDEC)
//   - Audible-style player UI with transport controls
//   - Bookmark persistence per file (auto-save/restore position)
//   - Audio output via I2S to PCM5102A DAC (audio variant only)
//   - Cooperative audio decode loop - yields to mesh stack
//   - Graceful pause during LoRa TX (SPI bus contention)
//
// Keyboard controls:
//   FILE_LIST mode:  W/S = scroll, Enter = open, Q = exit
//   PLAYER mode:     Enter = play/pause, A = -30s, D = +30s,
//                    W = volume up, S = volume down,
//                    [ = prev chapter, ] = next chapter,
//                    Q = leave (audio continues) / close book (if paused)
//
// Library dependencies (add to platformio.ini lib_deps):
//   https://github.com/schreibfaul1/ESP32-audioI2S.git#2.0.6
//   bitbank2/JPEGDEC
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <SD.h>
#include <vector>
#include "M4BMetadata.h"

// Audio library — ESP32-audioI2S by schreibfaul1
#include "Audio.h"

// Pin definitions for I2S DAC (from variant.h)
#include "variant.h"

// JPEG decoder for cover art — JPEGDEC by bitbank2
#include <JPEGDEC.h>

// Forward declarations
class UITask;

// ============================================================================
// Configuration
// ============================================================================
#define AUDIOBOOKS_FOLDER    "/audiobooks"
#define AB_BOOKMARK_FOLDER   "/audiobooks/.bookmarks"
#define AB_MAX_FILES         50
#define AB_COVER_W           40     // Virtual coords (reduced to fit layout)
#define AB_COVER_H           40     // Virtual coords
#define AB_COVER_BUF_SIZE    ((AB_COVER_W + 7) / 8 * AB_COVER_H)
#define AB_DEFAULT_VOLUME    12     // 0-21 range for ESP32-audioI2S
#define AB_SEEK_SECONDS      30     // Skip forward/back amount
#define AB_POSITION_SAVE_INTERVAL  30000  // Auto-save bookmark every 30s

// Supported file extensions
static bool isAudiobookFile(const String& name) {
  String lower = name;
  lower.toLowerCase();
  return lower.endsWith(".m4b") || lower.endsWith(".m4a") ||
         lower.endsWith(".mp3") || lower.endsWith(".wav");
}

// ============================================================================
// 4x4 Bayer ordered dithering matrix (threshold values 0-255)
// ============================================================================
static const uint8_t BAYER4x4[4][4] = {
  {  15, 135,  45, 165 },
  { 195,  75, 225, 105 },
  {  60, 180,  30, 150 },
  { 240, 120, 210,  90 }
};

// ============================================================================
// JPEG decode callback context
// ============================================================================
struct CoverDecodeCtx {
  uint8_t* bitmap;
  int      bitmapW;
  int      bitmapH;
  int      srcW;
  int      srcH;
  int      offsetX;
  int      offsetY;
};

// JPEGDEC draw callback — converts decoded pixels to 1-bit dithered
static int coverDrawCallback(JPEGDRAW* pDraw) {
  CoverDecodeCtx* ctx = (CoverDecodeCtx*)pDraw->pUser;
  if (!ctx || !ctx->bitmap) return 1;

  for (int y = 0; y < pDraw->iHeight; y++) {
    int destY = pDraw->y + y - ctx->offsetY;
    if (destY < 0 || destY >= ctx->bitmapH) continue;

    for (int x = 0; x < pDraw->iWidth; x++) {
      int destX = pDraw->x + x - ctx->offsetX;
      if (destX < 0 || destX >= ctx->bitmapW) continue;

      uint16_t rgb565 = pDraw->pPixels[y * pDraw->iWidth + x];
      uint8_t r = (rgb565 >> 11) << 3;
      uint8_t g = ((rgb565 >> 5) & 0x3F) << 2;
      uint8_t b = (rgb565 & 0x1F) << 3;
      uint8_t gray = (uint8_t)(((uint16_t)r * 77 + (uint16_t)g * 150 + (uint16_t)b * 29) >> 8);

      uint8_t threshold = BAYER4x4[destY & 3][destX & 3];
      bool isBlack = (gray < threshold);

      if (isBlack) {
        int byteIdx = destY * ((ctx->bitmapW + 7) / 8) + (destX / 8);
        uint8_t bitMask = 0x80 >> (destX & 7);
        ctx->bitmap[byteIdx] |= bitMask;
      }
    }
  }
  return 1;
}

// ============================================================================
// File entry with cached bookmark state (avoid SD.exists during render)
// ============================================================================
struct AudiobookFileEntry {
  String name;
  bool   hasBookmark;
};

// ============================================================================
// AudiobookPlayerScreen
// ============================================================================
class AudiobookPlayerScreen : public UIScreen {
public:
  enum Mode { FILE_LIST, PLAYER };

  struct Bookmark {
    char     filename[64];
    uint32_t positionSec;
    uint8_t  volume;
  };

private:
  UITask*  _task;
  Audio*   _audio;
  Mode     _mode;
  bool     _sdReady;
  bool     _i2sInitialized;    // Track whether setPinout has been called
  bool     _dacPowered;        // Track GPIO 41 DAC power state

  // File browser state
  std::vector<AudiobookFileEntry> _fileList;
  int _selectedFile;
  int _scrollOffset;

  // Current book state
  String      _currentFile;
  M4BMetadata _metadata;
  bool        _bookOpen;
  bool        _isPlaying;
  bool        _isPaused;
  uint8_t     _volume;

  // Cover art bitmap
  uint8_t*    _coverBitmap;
  int         _coverW;
  int         _coverH;
  bool        _hasCover;

  // Playback tracking
  uint32_t    _currentPosSec;
  uint32_t    _durationSec;
  int         _currentChapter;
  unsigned long _lastPositionSave;
  unsigned long _lastPosUpdate;

  // Deferred seek — applied after audio library reports stream ready
  uint32_t    _pendingSeekSec;    // 0 = no pending seek
  bool        _streamReady;       // Set true once library reports duration

  // M4B rename workaround — the audio library only recognises .m4a,
  // so we temporarily rename .m4b files on the SD card for playback.
  bool        _m4bRenamed;        // true if file was renamed for playback
  String      _m4bOrigPath;       // original path (with .m4b extension)
  String      _m4bTempPath;       // temporary path (with .m4a extension)

  // UI state
  int  _transportSel;
  bool _showingInfo;

  // Power on the PCM5102A DAC via GPIO 41 (BOARD_6609_EN).
  // On the audio variant, this pin supplies power to the DAC circuit.
  // TDeckBoard::begin() sets it LOW ("disable modem") which starves the DAC.
  void enableDAC() {
    pinMode(41, OUTPUT);
    digitalWrite(41, HIGH);
    if (!_dacPowered) {
      delay(50);
    }
    _dacPowered = true;
  }

  void disableDAC() {
    digitalWrite(41, LOW);
    _dacPowered = false;
  }

  // Restore an M4B file that was temporarily renamed to .m4a for playback.
  void restoreM4bRename() {
    if (!_m4bRenamed) return;
    if (SD.rename(_m4bTempPath.c_str(), _m4bOrigPath.c_str())) {
      Serial.printf("AB: Restored '%s' -> '%s'\n",
                    _m4bTempPath.c_str(), _m4bOrigPath.c_str());
    } else {
      Serial.printf("AB: Warning - failed to restore '%s' to '%s'\n",
                    _m4bTempPath.c_str(), _m4bOrigPath.c_str());
    }
    _m4bRenamed = false;
    _m4bOrigPath = "";
    _m4bTempPath = "";
  }

  void ensureI2SInit() {
    if (!_i2sInitialized && _audio) {
      bool ok = _audio->setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT, 0);
      if (!ok) {
        ok = _audio->setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT);
      }
      if (!ok) Serial.println("AB: setPinout FAILED");
      _i2sInitialized = true;
    }
  }

  // ---- Cover Art Decoding ----

  bool decodeCoverArt(File& file) {
    freeCoverBitmap();

    if (!_metadata.hasCoverArt || _metadata.coverSize == 0) return false;
    if (_metadata.coverFormat != 13) {
      Serial.printf("AB: Cover format %d not supported (JPEG only)\n", _metadata.coverFormat);
      return false;
    }

    uint8_t* jpegBuf = (uint8_t*)ps_malloc(_metadata.coverSize);
    if (!jpegBuf) {
      Serial.println("AB: Failed to allocate JPEG buffer in PSRAM");
      return false;
    }

    file.seek(_metadata.coverOffset);
    int bytesRead = file.read(jpegBuf, _metadata.coverSize);
    if (bytesRead != (int)_metadata.coverSize) {
      Serial.printf("AB: Cover read failed (%d/%u bytes)\n", bytesRead, _metadata.coverSize);
      free(jpegBuf);
      return false;
    }
    digitalWrite(SDCARD_CS, HIGH);

    _coverW = AB_COVER_W;
    _coverH = AB_COVER_H;
    int bitmapBytes = ((_coverW + 7) / 8) * _coverH;
    _coverBitmap = (uint8_t*)ps_calloc(1, bitmapBytes);
    if (!_coverBitmap) {
      Serial.println("AB: Failed to allocate cover bitmap");
      free(jpegBuf);
      return false;
    }

    JPEGDEC* jpeg = new JPEGDEC();
    if (!jpeg) {
      Serial.println("AB: Failed to allocate JPEGDEC");
      free(jpegBuf);
      freeCoverBitmap();
      return false;
    }
    CoverDecodeCtx ctx;
    ctx.bitmap  = _coverBitmap;
    ctx.bitmapW = _coverW;
    ctx.bitmapH = _coverH;

    if (!jpeg->openRAM(jpegBuf, _metadata.coverSize, coverDrawCallback)) {
      Serial.println("AB: JPEGDEC failed to open cover image");
      delete jpeg;
      free(jpegBuf);
      freeCoverBitmap();
      return false;
    }

    int srcW = jpeg->getWidth();
    int srcH = jpeg->getHeight();
    int scale = 0;

    if (srcW > _coverW * 6 || srcH > _coverH * 6) scale = 3;
    else if (srcW > _coverW * 3 || srcH > _coverH * 3) scale = 2;
    else if (srcW > _coverW * 1.5 || srcH > _coverH * 1.5) scale = 1;

    int divider = 1 << scale;
    int scaledW = srcW / divider;
    int scaledH = srcH / divider;

    ctx.srcW = scaledW;
    ctx.srcH = scaledH;
    ctx.offsetX = (scaledW > _coverW) ? (scaledW - _coverW) / 2 : 0;
    ctx.offsetY = (scaledH > _coverH) ? (scaledH - _coverH) / 2 : 0;

    jpeg->setUserPointer(&ctx);
    jpeg->setPixelType(RGB565_BIG_ENDIAN);

    int scaleFlags[] = { JPEG_SCALE_HALF, JPEG_SCALE_QUARTER, JPEG_SCALE_EIGHTH };
    if (scale > 0) {
      jpeg->decode(0, 0, scaleFlags[scale - 1]);
    } else {
      jpeg->decode(0, 0, 0);
    }

    jpeg->close();
    delete jpeg;
    free(jpegBuf);

    _hasCover = true;
    Serial.printf("AB: Cover decoded %dx%d (source %dx%d, scale 1/%d)\n",
                  _coverW, _coverH, srcW, srcH, divider);
    return true;
  }

  void freeCoverBitmap() {
    if (_coverBitmap) {
      free(_coverBitmap);
      _coverBitmap = nullptr;
    }
    _hasCover = false;
    _coverW = 0;
    _coverH = 0;
  }

  // ---- Bookmark Persistence ----

  String getBookmarkPath(const String& filename) {
    String base = filename;
    int slash = base.lastIndexOf('/');
    if (slash >= 0) base = base.substring(slash + 1);
    int dot = base.lastIndexOf('.');
    if (dot > 0) base = base.substring(0, dot);
    return String(AB_BOOKMARK_FOLDER) + "/" + base + ".bmk";
  }

  void loadBookmark() {
    String path = getBookmarkPath(_currentFile);
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) return;

    Bookmark bm;
    if (f.read((uint8_t*)&bm, sizeof(bm)) == sizeof(bm)) {
      _currentPosSec = bm.positionSec;
      _volume = bm.volume;
      if (_volume > 21) _volume = AB_DEFAULT_VOLUME;
      Serial.printf("AB: Loaded bookmark - pos %us, vol %d\n", _currentPosSec, _volume);
    }
    f.close();
    digitalWrite(SDCARD_CS, HIGH);
  }

  void saveBookmark() {
    if (!_bookOpen || _currentFile.length() == 0) return;

    if (!SD.exists(AB_BOOKMARK_FOLDER)) {
      SD.mkdir(AB_BOOKMARK_FOLDER);
    }

    String path = getBookmarkPath(_currentFile);
    if (SD.exists(path.c_str())) SD.remove(path.c_str());

    File f = SD.open(path.c_str(), FILE_WRITE);
    if (!f) return;

    Bookmark bm;
    memset(&bm, 0, sizeof(bm));
    strncpy(bm.filename, _currentFile.c_str(), sizeof(bm.filename) - 1);
    bm.positionSec = _currentPosSec;
    bm.volume = _volume;

    f.write((uint8_t*)&bm, sizeof(bm));
    f.close();
    digitalWrite(SDCARD_CS, HIGH);

    Serial.printf("AB: Saved bookmark - pos %us\n", _currentPosSec);
  }

  // ---- File Scanning ----

  void scanFiles() {
    _fileList.clear();
    if (!SD.exists(AUDIOBOOKS_FOLDER)) {
      SD.mkdir(AUDIOBOOKS_FOLDER);
      Serial.printf("AB: Created %s\n", AUDIOBOOKS_FOLDER);
    }

    File root = SD.open(AUDIOBOOKS_FOLDER);
    if (!root || !root.isDirectory()) return;

    File f = root.openNextFile();
    while (f && _fileList.size() < AB_MAX_FILES) {
      if (!f.isDirectory()) {
        String name = String(f.name());
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (isAudiobookFile(name) && !name.startsWith("._")) {
          AudiobookFileEntry entry;
          entry.name = name;
          // Cache bookmark existence NOW — avoid SD.exists() during render
          String bmkPath = getBookmarkPath(name);
          entry.hasBookmark = SD.exists(bmkPath.c_str());
          _fileList.push_back(entry);
        }
      }
      f = root.openNextFile();
    }
    root.close();
    digitalWrite(SDCARD_CS, HIGH);

    Serial.printf("AB: Found %d audiobook files\n", (int)_fileList.size());
  }

  // ---- Book Open / Close ----

  void openBook(const String& filename, DisplayDriver* display) {
    // Show loading splash
    if (display) {
      display->startFrame();
      display->setTextSize(1);
      display->setColor(DisplayDriver::GREEN);
      display->setCursor(10, 11);
      display->print("Loading...");
      display->setTextSize(1);
      display->setColor(DisplayDriver::LIGHT);
      display->setCursor(10, 30);

      String dispName = filename;
      int dot = dispName.lastIndexOf('.');
      if (dot > 0) dispName = dispName.substring(0, dot);
      if (dispName.length() > 22) dispName = dispName.substring(0, 19) + "...";
      display->print(dispName.c_str());
      display->endFrame();
    }

    _currentFile = filename;
    _bookOpen = true;
    _isPlaying = false;
    _isPaused = false;
    _currentPosSec = 0;
    _durationSec = 0;
    _currentChapter = -1;
    _lastPositionSave = millis();
    _lastPosUpdate = 0;
    _transportSel = 2;
    _pendingSeekSec = 0;
    _streamReady = false;

    yield();  // Feed WDT between heavy operations

    // Parse metadata
    String fullPath = String(AUDIOBOOKS_FOLDER) + "/" + filename;
    File file = SD.open(fullPath.c_str(), FILE_READ);
    if (file) {
      String lower = filename;
      lower.toLowerCase();
      if (lower.endsWith(".m4b") || lower.endsWith(".m4a")) {
        _metadata.parse(file);
        yield();  // Feed WDT after metadata parse
        decodeCoverArt(file);
        yield();  // Feed WDT after cover decode
      } else if (lower.endsWith(".mp3")) {
        _metadata.parseID3v2(file);
        yield();  // Feed WDT after metadata parse
        decodeCoverArt(file);
        yield();  // Feed WDT after cover decode
        // Fall back to filename for title if ID3 had none
        if (_metadata.title[0] == '\0') {
          String base = filename;
          int dot = base.lastIndexOf('.');
          if (dot > 0) base = base.substring(0, dot);
          strncpy(_metadata.title, base.c_str(), M4B_MAX_TITLE - 1);
        }
      } else {
        // Other audio formats — use filename as title
        _metadata.clear();
        String base = filename;
        int dot = base.lastIndexOf('.');
        if (dot > 0) base = base.substring(0, dot);
        strncpy(_metadata.title, base.c_str(), M4B_MAX_TITLE - 1);
      }
      file.close();
    }
    digitalWrite(SDCARD_CS, HIGH);

    yield();  // Feed WDT before bookmark load

    // Load saved bookmark position
    loadBookmark();

    // Set volume
    if (_audio) {
      _audio->setVolume(_volume);
    }

    if (_metadata.durationMs > 0) {
      _durationSec = _metadata.durationMs / 1000;
    }

    _mode = PLAYER;
    Serial.printf("AB: Opened '%s' -- %s by %s, %us, %d chapters\n",
                  filename.c_str(), _metadata.title, _metadata.author,
                  _durationSec, _metadata.chapterCount);
  }

  void closeBook() {
    if (_isPlaying || _isPaused) {
      stopPlayback();
    }
    restoreM4bRename();  // Safety: ensure rename is restored even if state was odd
    saveBookmark();
    freeCoverBitmap();
    _metadata.clear();
    _bookOpen = false;
    _currentFile = "";
    _mode = FILE_LIST;
  }

  // ---- Playback Control ----

  void startPlayback() {
    if (!_audio || _currentFile.length() == 0) return;

    // Ensure DAC has power (must be re-enabled after each stop)
    enableDAC();

    // Ensure I2S is configured (once only, before first connecttoFS)
    ensureI2SInit();

    String fullPath = String(AUDIOBOOKS_FOLDER) + "/" + _currentFile;

    // M4B workaround: the ESP32-audioI2S library only recognises .m4a
    // for MP4/AAC container parsing. M4B is identical but the extension
    // isn't checked, so the library treats it as raw AAC and fails.
    // Temporarily rename the file on the SD card (FAT32 rename is instant).
    _m4bRenamed = false;
    String lower = _currentFile;
    lower.toLowerCase();
    if (lower.endsWith(".m4b")) {
      String m4aFile = _currentFile.substring(0, _currentFile.length() - 1) + "a";
      _m4bOrigPath = fullPath;
      _m4bTempPath = String(AUDIOBOOKS_FOLDER) + "/" + m4aFile;

      if (SD.rename(_m4bOrigPath.c_str(), _m4bTempPath.c_str())) {
        Serial.printf("AB: Renamed '%s' -> '%s' for playback\n",
                      _m4bOrigPath.c_str(), _m4bTempPath.c_str());
        fullPath = _m4bTempPath;
        _m4bRenamed = true;
      } else {
        Serial.println("AB: Warning - failed to rename .m4b to .m4a");
      }
    }

    // Connect to file — library parses headers asynchronously via loop()
    _audio->connecttoFS(SD, fullPath.c_str());
    _audio->setVolume(_volume);

    // DON'T seek immediately — the library hasn't parsed headers yet.
    // Store pending seek; apply once stream reports ready (has duration).
    _streamReady = false;
    if (_currentPosSec > 5) {
      _pendingSeekSec = (_currentPosSec > 3) ? _currentPosSec - 3 : 0;
    } else {
      _pendingSeekSec = 0;
    }

    _isPlaying = true;
    _isPaused = false;
    _lastPositionSave = millis();

    Serial.printf("AB: Playing '%s'\n", _currentFile.c_str());
  }

  void stopPlayback() {
    if (_audio) {
      uint32_t pos = _audio->getAudioCurrentTime();
      if (pos > 0) _currentPosSec = pos;
      _audio->stopSong();
    }
    _isPlaying = false;
    _isPaused = false;
    _pendingSeekSec = 0;
    _streamReady = false;
    saveBookmark();

    // Restore .m4b filename if we renamed it for playback
    restoreM4bRename();

    // Power down the PCM5102A DAC to save battery
    disableDAC();

    // Force I2S re-init for next file (sample rate may differ)
    _i2sInitialized = false;

    Serial.println("AB: Stopped");
  }

  void togglePause() {
    if (!_audio) return;

    if (_isPlaying && !_isPaused) {
      _audio->pauseResume();
      _isPaused = true;
      saveBookmark();
    } else if (_isPaused) {
      _audio->pauseResume();
      _isPaused = false;
    } else {
      // Not playing yet — start from bookmark
      startPlayback();
    }
  }

  void seekRelative(int seconds) {
    if (!_audio || !_isPlaying) return;

    uint32_t current = _audio->getAudioCurrentTime();
    int32_t target = (int32_t)current + seconds;
    if (target < 0) target = 0;
    if (_durationSec > 0 && (uint32_t)target > _durationSec) {
      target = _durationSec;
    }

    _audio->setTimeOffset((uint32_t)target);
    _currentPosSec = (uint32_t)target;
  }

  void seekToChapter(int chapterIdx) {
    if (chapterIdx < 0 || chapterIdx >= _metadata.chapterCount) return;

    uint32_t targetMs = _metadata.chapters[chapterIdx].startMs;
    uint32_t targetSec = targetMs / 1000;

    if (_audio && _isPlaying) {
      _audio->setTimeOffset(targetSec);
    }
    _currentPosSec = targetSec;
    _currentChapter = chapterIdx;
  }

  // ---- Rendering Helpers ----

  static void formatTime(uint32_t totalSec, char* buf, int bufLen) {
    uint32_t h = totalSec / 3600;
    uint32_t m = (totalSec % 3600) / 60;
    uint32_t s = totalSec % 60;
    if (h > 0) {
      snprintf(buf, bufLen, "%u:%02u:%02u", h, m, s);
    } else {
      snprintf(buf, bufLen, "%u:%02u", m, s);
    }
  }

  // ---- Standard footer (matching ChannelScreen / TextReaderScreen) ----
  // All screens use: setTextSize(1), footerY = height-12, separator at footerY-2

  void drawFooter(DisplayDriver& display, const char* left, const char* right) {
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, footerY - 2, display.width(), 1);  // Separator line
    display.setColor(DisplayDriver::YELLOW);

    display.setCursor(0, footerY);
    display.print(left);

    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }

  // ---- Render: File List ----
  void renderFileList(DisplayDriver& display) {
    // Header
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    display.print("Audiobooks");

    display.setColor(DisplayDriver::LIGHT);

    if (_fileList.size() == 0) {
      display.setCursor(0, 20);
      display.print("No audiobooks found.");
      display.setCursor(0, 30);
      display.print("Place .m4b/.mp3 in");
      display.setCursor(0, 38);
      display.print("/audiobooks/ on SD");

      drawFooter(display, "0 files", "Q:Back");
      return;
    }

    // Calculate visible items — reserve footerHeight=14 at bottom
    int itemHeight = 10;
    int listTop = 13;
    int listBottom = display.height() - 14;
    int visibleItems = (listBottom - listTop) / itemHeight;

    // Keep selection visible
    if (_selectedFile < _scrollOffset) {
      _scrollOffset = _selectedFile;
    } else if (_selectedFile >= _scrollOffset + visibleItems) {
      _scrollOffset = _selectedFile - visibleItems + 1;
    }

    // Draw file list
    for (int i = 0; i < visibleItems && (_scrollOffset + i) < (int)_fileList.size(); i++) {
      int fileIdx = _scrollOffset + i;
      int y = listTop + i * itemHeight;

      if (fileIdx == _selectedFile) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y - 1, display.width(), itemHeight - 1);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }

      // Display filename without extension
      String name = _fileList[fileIdx].name;
      int dot = name.lastIndexOf('.');
      if (dot > 0) name = name.substring(0, dot);
      if (name.length() > 20) {
        name = name.substring(0, 17) + "...";
      }

      display.setCursor(2, y);
      display.print(name.c_str());

      // Bookmark indicator (cached from scanFiles — no SD access)
      if (_fileList[fileIdx].hasBookmark) {
        display.setCursor(display.width() - 8, y);
        display.print(">");
      }
    }

    // Scrollbar
    if ((int)_fileList.size() > visibleItems) {
      int barH = listBottom - listTop;
      int thumbH = max(4, barH * visibleItems / (int)_fileList.size());
      int thumbY = listTop + (barH - thumbH) * _scrollOffset /
                   max(1, (int)_fileList.size() - visibleItems);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(display.width() - 1, listTop, 1, barH);
      display.fillRect(display.width() - 1, thumbY, 1, thumbH);
    }

    // Footer
    char leftBuf[20];
    snprintf(leftBuf, sizeof(leftBuf), "%d files", (int)_fileList.size());
    drawFooter(display, leftBuf, "W/S:Nav Enter:Open");
  }

  // ---- Render: Player ----
  void renderPlayer(DisplayDriver& display) {
    // Layout budget: 128 total - 14 footer = 114 usable virtual units
    // With cover: 1+40+1 = 42 for art, leaves 72 for text+controls
    // Without cover: full 114 for text+controls
    int y = 0;

    // ---- Cover Art (only for M4B with embedded art) ----
    if (_hasCover && _coverBitmap) {
      int coverX = (display.width() - _coverW) / 2;
      int coverY = y + 1;
      display.drawXbm(coverX, coverY, _coverBitmap, _coverW, _coverH);
      y = coverY + _coverH + 1;  // y = 42
    } else {
      y = 2;  // No placeholder — start with title near top
    }

    // ---- Title ----
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    {
      char titleBuf[24];
      const char* src = _metadata.title[0] ? _metadata.title : _currentFile.c_str();
      strncpy(titleBuf, src, sizeof(titleBuf) - 1);
      titleBuf[sizeof(titleBuf) - 1] = '\0';
      display.drawTextCentered(display.width() / 2, y, titleBuf);
    }
    y += 10;

    // ---- Author ----
    if (_metadata.author[0]) {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      char authBuf[24];
      strncpy(authBuf, _metadata.author, sizeof(authBuf) - 1);
      authBuf[sizeof(authBuf) - 1] = '\0';
      display.drawTextCentered(display.width() / 2, y, authBuf);
      y += 10;
    }

    // ---- Chapter Info ----
    if (_metadata.chapterCount > 0 && _currentChapter >= 0) {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      char chBuf[24];
      snprintf(chBuf, sizeof(chBuf), "Ch %d/%d",
               _currentChapter + 1, _metadata.chapterCount);
      display.drawTextCentered(display.width() / 2, y, chBuf);
      y += 10;
    }

    // ---- Playback State + Volume ----
    {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      const char* stateStr = _isPlaying ? (_isPaused ? "Paused" : "Playing") : "Stopped";
      char stateBuf[24];
      snprintf(stateBuf, sizeof(stateBuf), "%s  Vol:%d", stateStr, _volume);
      display.drawTextCentered(display.width() / 2, y, stateBuf);
    }
    y += 10;

    // ---- Progress Bar ----
    int barX = 6;
    int barW = display.width() - 12;
    int barH = 4;

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(barX, y, barW, barH);

    if (_durationSec > 0 && _currentPosSec > 0) {
      int fillW = (int)((uint64_t)_currentPosSec * (barW - 2) / _durationSec);
      if (fillW > barW - 2) fillW = barW - 2;
      if (fillW > 0) {
        display.fillRect(barX + 1, y + 1, fillW, barH - 2);
      }
    }
    y += barH + 2;

    // ---- Time Display ----
    {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      char timeBuf[32];
      char curStr[12], totStr[12];
      formatTime(_currentPosSec, curStr, sizeof(curStr));
      formatTime(_durationSec, totStr, sizeof(totStr));
      snprintf(timeBuf, sizeof(timeBuf), "%s / %s", curStr, totStr);
      display.drawTextCentered(display.width() / 2, y, timeBuf);
    }
    y += 10;

    // ---- Hint Text (replaces visual transport controls) ----
    {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, y, "Enter: Play/Pause");
      y += 10;

      // Only show second hint when there's space (no cover art)
      if (y < display.height() - 26) {
        display.setColor(DisplayDriver::YELLOW);
        if (_isPlaying && !_isPaused) {
          display.drawTextCentered(display.width() / 2, y,
                                   "Screen updates on keypress");
        } else if (_metadata.chapterCount > 0) {
          display.drawTextCentered(display.width() / 2, y,
                                   "[/]: Prev/Next Chapter");
        }
      }
    }
    // Transport controls drawn — footer is at fixed position below

    // ---- Footer Nav Bar ----
    drawFooter(display, "A/D:Seek W/S:Vol", (_isPlaying && !_isPaused) ? "Q:Leave" : "Q:Close");
  }

public:
  AudiobookPlayerScreen(UITask* task, Audio* audio)
    : _task(task), _audio(audio), _mode(FILE_LIST),
      _sdReady(false), _i2sInitialized(false), _dacPowered(false),
      _selectedFile(0), _scrollOffset(0),
      _bookOpen(false), _isPlaying(false), _isPaused(false),
      _volume(AB_DEFAULT_VOLUME),
      _coverBitmap(nullptr), _coverW(0), _coverH(0), _hasCover(false),
      _currentPosSec(0), _durationSec(0), _currentChapter(-1),
      _lastPositionSave(0), _lastPosUpdate(0),
      _pendingSeekSec(0), _streamReady(false),
      _m4bRenamed(false),
      _transportSel(2), _showingInfo(false) {}

  ~AudiobookPlayerScreen() {
    freeCoverBitmap();
  }

  void setSDReady(bool ready) { _sdReady = ready; }

  // ---- Audio Tick ----
  // Called from main loop() every iteration for uninterrupted playback.
  void audioTick() {
    if (!_audio || !_isPlaying) return;

    // Feed the audio decode pipeline (skip when paused)
    if (!_isPaused) {
      _audio->loop();
    }

    // Throttle position/duration reads to every 500ms
    if (millis() - _lastPosUpdate > 500) {
      uint32_t pos = _audio->getAudioCurrentTime();
      if (pos > 0) _currentPosSec = pos;

      if (_durationSec == 0) {
        uint32_t dur = _audio->getAudioFileDuration();
        if (dur > 0) _durationSec = dur;
      }

      // Apply deferred seek once stream is ready
      if (!_streamReady && _durationSec > 0) {
        _streamReady = true;
        if (_pendingSeekSec > 0) {
          _audio->setTimeOffset(_pendingSeekSec);
          _currentPosSec = _pendingSeekSec;
          _pendingSeekSec = 0;
        }
      }

      // Update chapter tracking
      if (_metadata.chapterCount > 0) {
        uint32_t posMs = _currentPosSec * 1000;
        _currentChapter = _metadata.getChapterForPosition(posMs);
      }

      _lastPosUpdate = millis();
    }

    // Auto-save bookmark periodically
    if (millis() - _lastPositionSave > AB_POSITION_SAVE_INTERVAL) {
      saveBookmark();
      _lastPositionSave = millis();
    }
  }

  bool isAudioActive() const { return _isPlaying && !_isPaused; }
  bool isPaused() const { return _isPaused; }
  bool isBookOpenAndPaused() const { return _bookOpen && (_isPaused || !_isPlaying); }

  // Public method to close the current book (stops playback, saves bookmark,
  // returns to file list). Used by main.cpp when user presses Q while paused.
  void closeCurrentBook() {
    if (_bookOpen) {
      closeBook();
    }
  }

  void enter(DisplayDriver& display) {
    if (!_bookOpen) {
      scanFiles();
      _selectedFile = 0;
      _scrollOffset = 0;
      _mode = FILE_LIST;
    } else {
      _mode = PLAYER;
    }
  }

  void exitPlayer() {
    if (_bookOpen) closeBook();
    _mode = FILE_LIST;
  }

  bool isInFileList() const { return _mode == FILE_LIST; }
  bool isBookOpen() const { return _bookOpen; }
  bool isPlaying() const { return _isPlaying; }

  // ---- UIScreen Interface ----

  int render(DisplayDriver& display) override {
    if (!_sdReady) {
      display.setCursor(0, 20);
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.print("SD card not found");
      display.setCursor(0, 35);
      display.print("Insert SD with");
      display.setCursor(0, 43);
      display.print("/audiobooks/ folder");
      return 5000;
    }

    if (_mode == FILE_LIST) {
      renderFileList(display);
    } else if (_mode == PLAYER) {
      renderPlayer(display);
    }

    // E-ink refresh takes ~648ms during which audio.loop() can't run
    // (SPI bus shared between display and SD card). This causes audible
    // glitches during playback. Solution: during active playback, only
    // auto-refresh once per minute (for time/progress updates). Key presses
    // always trigger immediate refresh regardless of this interval.
    // When paused or stopped, refresh every 5s as normal.
    if (_isPlaying && !_isPaused) return 60000;  // 1 min between auto-refreshes
    return 5000;
  }

  bool handleInput(char c) override {
    if (_mode == FILE_LIST) {
      return handleFileListInput(c);
    } else if (_mode == PLAYER) {
      return handlePlayerInput(c);
    }
    return false;
  }

  bool handleFileListInput(char c) {
    // W - scroll up
    if (c == 'w' || c == 0xF2) {
      if (_selectedFile > 0) {
        _selectedFile--;
        return true;
      }
      return false;
    }

    // S - scroll down
    if (c == 's' || c == 0xF1) {
      if (_selectedFile < (int)_fileList.size() - 1) {
        _selectedFile++;
        return true;
      }
      return false;
    }

    // Enter - open selected audiobook
    if (c == '\r' || c == 13) {
      if (_fileList.size() > 0 && _selectedFile < (int)_fileList.size()) {
        openBook(_fileList[_selectedFile].name, nullptr);
        return true;
      }
      return false;
    }

    return false;
  }

  bool handlePlayerInput(char c) {
    // Enter - play/pause
    if (c == '\r' || c == 13) {
      togglePause();
      return true;
    }

    // A - seek backward
    if (c == 'a') {
      seekRelative(-AB_SEEK_SECONDS);
      return true;
    }

    // D - seek forward
    if (c == 'd') {
      seekRelative(AB_SEEK_SECONDS);
      return true;
    }

    // W - volume up
    if (c == 'w' || c == 0xF2) {
      if (_volume < 21) {
        _volume++;
        if (_audio) _audio->setVolume(_volume);
      }
      return true;  // Always consume & refresh (show current volume)
    }

    // S - volume down
    if (c == 's' || c == 0xF1) {
      if (_volume > 0) {
        _volume--;
        if (_audio) _audio->setVolume(_volume);
      }
      return true;  // Always consume & refresh
    }

    // [ - previous chapter
    if (c == '[') {
      if (_metadata.chapterCount > 0 && _currentChapter > 0) {
        seekToChapter(_currentChapter - 1);
        return true;
      }
      return false;
    }

    // ] - next chapter
    if (c == ']') {
      if (_metadata.chapterCount > 0 && _currentChapter < _metadata.chapterCount - 1) {
        seekToChapter(_currentChapter + 1);
        return true;
      }
      return false;
    }

    // Q - handled by main.cpp (leave screen or close book depending on play state)
    // Not handled here - main.cpp intercepts Q before it reaches the player

    return false;
  }

  void poll() override {
    audioTick();
  }
};