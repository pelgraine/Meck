#pragma once
// =============================================================================
// EpubZipReader.h - Minimal ZIP reader for EPUB files on ESP32-S3
//
// Parses ZIP archives directly from SD card File objects.
// Uses the ESP32 ROM's built-in tinfl decompressor for DEFLATE.
// No external library dependencies.
//
// Supports:
//   - STORED (method 0) entries - direct copy
//   - DEFLATED (method 8) entries - ROM tinfl decompression
//   - ZIP64 is NOT supported (EPUBs don't need it)
//
// Memory: Allocates decompression buffers from PSRAM when available.
// Typical EPUB chapter is 5-50KB, well within ESP32-S3's 8MB PSRAM.
// =============================================================================

#include <SD.h>
#include <FS.h>

// ROM tinfl decompressor - built into ESP32/ESP32-S3 ROM
// If this include fails on your platform, see the fallback note at bottom
#if __has_include(<rom/miniz.h>)
  #include <rom/miniz.h>
  #define HAS_ROM_TINFL 1
#elif __has_include(<esp32s3/rom/miniz.h>)
  #include <esp32s3/rom/miniz.h>
  #define HAS_ROM_TINFL 1
#elif __has_include(<esp32/rom/miniz.h>)
  #include <esp32/rom/miniz.h>
  #define HAS_ROM_TINFL 1
#else
  #warning "ROM miniz not found - DEFLATED entries will not be supported"
  #define HAS_ROM_TINFL 0
#endif

// ---- ZIP format constants ----
#define ZIP_LOCAL_FILE_HEADER_SIG   0x04034b50
#define ZIP_CENTRAL_DIR_SIG         0x02014b50
#define ZIP_END_OF_CENTRAL_DIR_SIG  0x06054b50

#define ZIP_METHOD_STORED   0
#define ZIP_METHOD_DEFLATED 8

// Maximum files we track in a ZIP (EPUBs typically have 20-100 files)
#define ZIP_MAX_ENTRIES 128

// Maximum filename length within the ZIP
#define ZIP_MAX_FILENAME 128

// ---- Data structures ----

struct ZipEntry {
  char     filename[ZIP_MAX_FILENAME];
  uint16_t compressionMethod;   // 0=STORED, 8=DEFLATED
  uint32_t compressedSize;
  uint32_t uncompressedSize;
  uint32_t localHeaderOffset;   // Offset to local file header in ZIP
  uint32_t crc32;
};

// ---- Helper: read little-endian values from a byte buffer ----

static inline uint16_t zipRead16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t zipRead32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// =============================================================================
// EpubZipReader class
// =============================================================================

class EpubZipReader {
public:
  EpubZipReader() : _entryCount(0), _isOpen(false), _entries(nullptr) {
    // Allocate entries array from PSRAM to avoid stack overflow
    // (128 entries × ~146 bytes = ~19KB — too large for 8KB loopTask stack)
#ifdef BOARD_HAS_PSRAM
    _entries = (ZipEntry*)ps_malloc(ZIP_MAX_ENTRIES * sizeof(ZipEntry));
#endif
    if (!_entries) {
      _entries = (ZipEntry*)malloc(ZIP_MAX_ENTRIES * sizeof(ZipEntry));
    }
    if (!_entries) {
      Serial.println("ZipReader: FATAL - failed to allocate entry table");
    }
  }

  ~EpubZipReader() {
    if (_entries) {
      free(_entries);
      _entries = nullptr;
    }
  }

  // ----------------------------------------------------------
  // Open a ZIP file and parse its central directory.
  // Returns true on success, false on error.
  // After open(), entries are available via getEntryCount()/getEntry().
  // ----------------------------------------------------------
  bool open(File& zipFile) {
    _isOpen = false;
    _entryCount = 0;

    if (!_entries) {
      Serial.println("ZipReader: entry table not allocated");
      return false;
    }

    if (!zipFile || !zipFile.available()) {
      Serial.println("ZipReader: file not valid");
      return false;
    }

    _file = zipFile;
    uint32_t fileSize = _file.size();

    if (fileSize < 22) {
      Serial.println("ZipReader: file too small for ZIP");
      return false;
    }

    // ---- Step 1: Find the End of Central Directory record ----
    // EOCD is at least 22 bytes, at end of file.
    // Search backwards from end for the EOCD signature.
    // Comment can be up to 65535 bytes, but EPUBs typically have none.
    uint32_t searchStart = (fileSize > 65557) ? (fileSize - 65557) : 0;
    uint32_t eocdOffset = 0;
    bool foundEocd = false;

    // Read the last chunk into a buffer to search for EOCD signature
    uint32_t searchLen = fileSize - searchStart;
    // Cap search buffer to a reasonable size
    if (searchLen > 1024) {
      searchStart = fileSize - 1024;
      searchLen = 1024;
    }

    uint8_t* searchBuf = (uint8_t*)_allocBuffer(searchLen);
    if (!searchBuf) {
      Serial.println("ZipReader: failed to alloc search buffer");
      return false;
    }

    _file.seek(searchStart);
    if (_file.read(searchBuf, searchLen) != (int)searchLen) {
      free(searchBuf);
      Serial.println("ZipReader: failed to read EOCD area");
      return false;
    }

    // Scan backwards for EOCD signature (0x06054b50)
    for (int i = (int)searchLen - 22; i >= 0; i--) {
      if (zipRead32(&searchBuf[i]) == ZIP_END_OF_CENTRAL_DIR_SIG) {
        eocdOffset = searchStart + i;
        // Parse EOCD fields
        uint16_t totalEntries = zipRead16(&searchBuf[i + 10]);
        uint32_t cdSize       = zipRead32(&searchBuf[i + 12]);
        uint32_t cdOffset     = zipRead32(&searchBuf[i + 16]);

        _cdOffset = cdOffset;
        _cdSize = cdSize;
        _totalEntries = totalEntries;
        foundEocd = true;
        break;
      }
    }
    free(searchBuf);

    if (!foundEocd) {
      Serial.println("ZipReader: EOCD not found - not a valid ZIP");
      return false;
    }

    Serial.printf("ZipReader: EOCD found at %u, %u entries, CD at %u (%u bytes)\n",
                  eocdOffset, _totalEntries, _cdOffset, _cdSize);

    // ---- Step 2: Parse Central Directory entries ----
    if (_cdSize == 0 || _cdSize > 512 * 1024) {
      Serial.println("ZipReader: central directory size unreasonable");
      return false;
    }

    uint8_t* cdBuf = (uint8_t*)_allocBuffer(_cdSize);
    if (!cdBuf) {
      Serial.printf("ZipReader: failed to alloc %u bytes for central directory\n", _cdSize);
      return false;
    }

    _file.seek(_cdOffset);
    if (_file.read(cdBuf, _cdSize) != (int)_cdSize) {
      free(cdBuf);
      Serial.println("ZipReader: failed to read central directory");
      return false;
    }

    uint32_t pos = 0;
    _entryCount = 0;

    while (pos + 46 <= _cdSize && _entryCount < ZIP_MAX_ENTRIES) {
      if (zipRead32(&cdBuf[pos]) != ZIP_CENTRAL_DIR_SIG) {
        break;  // No more central directory entries
      }

      uint16_t method       = zipRead16(&cdBuf[pos + 10]);
      uint32_t crc          = zipRead32(&cdBuf[pos + 16]);
      uint32_t compSize     = zipRead32(&cdBuf[pos + 20]);
      uint32_t uncompSize   = zipRead32(&cdBuf[pos + 24]);
      uint16_t fnLen        = zipRead16(&cdBuf[pos + 28]);
      uint16_t extraLen     = zipRead16(&cdBuf[pos + 30]);
      uint16_t commentLen   = zipRead16(&cdBuf[pos + 32]);
      uint32_t localOffset  = zipRead32(&cdBuf[pos + 42]);

      // Copy filename (truncate if necessary)
      int copyLen = (fnLen < ZIP_MAX_FILENAME - 1) ? fnLen : ZIP_MAX_FILENAME - 1;
      memcpy(_entries[_entryCount].filename, &cdBuf[pos + 46], copyLen);
      _entries[_entryCount].filename[copyLen] = '\0';

      _entries[_entryCount].compressionMethod = method;
      _entries[_entryCount].compressedSize = compSize;
      _entries[_entryCount].uncompressedSize = uncompSize;
      _entries[_entryCount].localHeaderOffset = localOffset;
      _entries[_entryCount].crc32 = crc;

      // Skip directories (filenames ending with '/')
      if (copyLen > 0 && _entries[_entryCount].filename[copyLen - 1] != '/') {
        _entryCount++;
      }

      // Advance past this central directory entry
      pos += 46 + fnLen + extraLen + commentLen;
    }

    free(cdBuf);

    Serial.printf("ZipReader: parsed %d file entries\n", _entryCount);
    _isOpen = true;
    return true;
  }

  // ----------------------------------------------------------
  // Close the reader (does not close the underlying File).
  // ----------------------------------------------------------
  void close() {
    _isOpen = false;
    _entryCount = 0;
  }

  // ----------------------------------------------------------
  // Get entry count and entries
  // ----------------------------------------------------------
  int getEntryCount() const { return _entryCount; }

  const ZipEntry* getEntry(int index) const {
    if (index < 0 || index >= _entryCount) return nullptr;
    return &_entries[index];
  }

  // ----------------------------------------------------------
  // Find an entry by filename (case-sensitive).
  // Returns index, or -1 if not found.
  // ----------------------------------------------------------
  int findEntry(const char* filename) const {
    for (int i = 0; i < _entryCount; i++) {
      if (strcmp(_entries[i].filename, filename) == 0) {
        return i;
      }
    }
    return -1;
  }

  // ----------------------------------------------------------
  // Find an entry by filename suffix (e.g., ".opf", ".ncx").
  // Returns index of first match, or -1 if not found.
  // ----------------------------------------------------------
  int findEntryBySuffix(const char* suffix) const {
    int suffixLen = strlen(suffix);
    for (int i = 0; i < _entryCount; i++) {
      int fnLen = strlen(_entries[i].filename);
      if (fnLen >= suffixLen &&
          strcasecmp(&_entries[i].filename[fnLen - suffixLen], suffix) == 0) {
        return i;
      }
    }
    return -1;
  }

  // ----------------------------------------------------------
  // Find entries matching a path prefix (e.g., "OEBPS/").
  // Fills matchIndices[] up to maxMatches. Returns count found.
  // ----------------------------------------------------------
  int findEntriesByPrefix(const char* prefix, int* matchIndices, int maxMatches) const {
    int count = 0;
    int prefixLen = strlen(prefix);
    for (int i = 0; i < _entryCount && count < maxMatches; i++) {
      if (strncmp(_entries[i].filename, prefix, prefixLen) == 0) {
        matchIndices[count++] = i;
      }
    }
    return count;
  }

  // ----------------------------------------------------------
  // Extract a file entry to a newly allocated buffer.
  //
  // On success, returns a malloc'd buffer (caller must free!)
  // and sets *outSize to the uncompressed size.
  //
  // On failure, returns nullptr.
  //
  // The buffer is allocated from PSRAM if available.
  // ----------------------------------------------------------
  uint8_t* extractEntry(int index, uint32_t* outSize) {
    if (!_isOpen || index < 0 || index >= _entryCount) {
      return nullptr;
    }

    const ZipEntry& entry = _entries[index];

    // ---- Read the local file header to get actual data offset ----
    // Local header: 30 bytes fixed + variable filename + extra field
    uint8_t localHeader[30];
    _file.seek(entry.localHeaderOffset);
    if (_file.read(localHeader, 30) != 30) {
      Serial.println("ZipReader: failed to read local header");
      return nullptr;
    }

    if (zipRead32(localHeader) != ZIP_LOCAL_FILE_HEADER_SIG) {
      Serial.println("ZipReader: bad local header signature");
      return nullptr;
    }

    uint16_t localFnLen    = zipRead16(&localHeader[26]);
    uint16_t localExtraLen = zipRead16(&localHeader[28]);
    uint32_t dataOffset = entry.localHeaderOffset + 30 + localFnLen + localExtraLen;

    // ---- Handle based on compression method ----
    if (entry.compressionMethod == ZIP_METHOD_STORED) {
      return _extractStored(dataOffset, entry.uncompressedSize, outSize);
    }
    else if (entry.compressionMethod == ZIP_METHOD_DEFLATED) {
      return _extractDeflated(dataOffset, entry.compressedSize,
                              entry.uncompressedSize, outSize);
    }
    else {
      Serial.printf("ZipReader: unsupported compression method %d for %s\n",
                    entry.compressionMethod, entry.filename);
      return nullptr;
    }
  }

  // ----------------------------------------------------------
  // Extract a file entry by filename.
  // Convenience wrapper around findEntry() + extractEntry().
  // ----------------------------------------------------------
  uint8_t* extractByName(const char* filename, uint32_t* outSize) {
    int idx = findEntry(filename);
    if (idx < 0) return nullptr;
    return extractEntry(idx, outSize);
  }

  // ----------------------------------------------------------
  // Check if reader is open and valid
  // ----------------------------------------------------------
  bool isOpen() const { return _isOpen; }

  // ----------------------------------------------------------
  // Debug: print all entries
  // ----------------------------------------------------------
  void printEntries() const {
    Serial.printf("ZIP contains %d files:\n", _entryCount);
    for (int i = 0; i < _entryCount; i++) {
      const ZipEntry& e = _entries[i];
      Serial.printf("  [%d] %s (%s, %u -> %u bytes)\n",
                    i, e.filename,
                    e.compressionMethod == 0 ? "STORED" : "DEFLATED",
                    e.compressedSize, e.uncompressedSize);
    }
  }

private:
  File       _file;
  ZipEntry*  _entries;           // Heap-allocated (PSRAM) entry table
  int        _entryCount;
  bool       _isOpen;
  uint32_t   _cdOffset;
  uint32_t   _cdSize;
  uint16_t   _totalEntries;

  // ----------------------------------------------------------
  // Allocate buffer, preferring PSRAM if available
  // ----------------------------------------------------------
  void* _allocBuffer(size_t size) {
    void* buf = nullptr;
#ifdef BOARD_HAS_PSRAM
    buf = ps_malloc(size);
#endif
    if (!buf) {
      buf = malloc(size);
    }
    return buf;
  }

  // ----------------------------------------------------------
  // Extract a STORED (uncompressed) entry
  // ----------------------------------------------------------
  uint8_t* _extractStored(uint32_t dataOffset, uint32_t size, uint32_t* outSize) {
    uint8_t* buf = (uint8_t*)_allocBuffer(size + 1);  // +1 for null terminator
    if (!buf) {
      Serial.printf("ZipReader: failed to alloc %u bytes for stored entry\n", size);
      return nullptr;
    }

    _file.seek(dataOffset);
    uint32_t bytesRead = _file.read(buf, size);
    if (bytesRead != size) {
      Serial.printf("ZipReader: short read (got %u, expected %u)\n", bytesRead, size);
      free(buf);
      return nullptr;
    }

    buf[size] = '\0';  // Null-terminate for text files
    *outSize = size;

    // Release SD CS pin for other SPI users
    digitalWrite(SDCARD_CS, HIGH);

    return buf;
  }

  // ----------------------------------------------------------
  // Extract a DEFLATED entry using ROM tinfl
  // ----------------------------------------------------------
  uint8_t* _extractDeflated(uint32_t dataOffset, uint32_t compSize,
                            uint32_t uncompSize, uint32_t* outSize) {
#if HAS_ROM_TINFL
    // Allocate compressed data buffer (from PSRAM)
    uint8_t* compBuf = (uint8_t*)_allocBuffer(compSize);
    if (!compBuf) {
      Serial.printf("ZipReader: failed to alloc %u bytes for compressed data\n", compSize);
      return nullptr;
    }

    // Allocate output buffer (+1 for null terminator)
    uint8_t* outBuf = (uint8_t*)_allocBuffer(uncompSize + 1);
    if (!outBuf) {
      Serial.printf("ZipReader: failed to alloc %u bytes for decompressed data\n", uncompSize);
      free(compBuf);
      return nullptr;
    }

    // Heap-allocate the decompressor (~11KB struct - too large for 8KB loopTask stack!)
    tinfl_decompressor* decomp = (tinfl_decompressor*)_allocBuffer(sizeof(tinfl_decompressor));
    if (!decomp) {
      Serial.printf("ZipReader: failed to alloc tinfl_decompressor (%u bytes)\n",
                    (uint32_t)sizeof(tinfl_decompressor));
      free(compBuf);
      free(outBuf);
      return nullptr;
    }

    // Read compressed data from file
    _file.seek(dataOffset);
    if (_file.read(compBuf, compSize) != (int)compSize) {
      Serial.println("ZipReader: failed to read compressed data");
      free(decomp);
      free(compBuf);
      free(outBuf);
      return nullptr;
    }

    // Release SD CS pin for other SPI users
    digitalWrite(SDCARD_CS, HIGH);

    // Decompress using ROM tinfl (low-level API to avoid stack allocation)
    // ZIP DEFLATE is raw deflate (no zlib header).
    tinfl_init(decomp);

    size_t inBytes = compSize;
    size_t outBytes = uncompSize;
    tinfl_status status = tinfl_decompress(
      decomp,
      (const mz_uint8*)compBuf,    // compressed input
      &inBytes,                     // in: available, out: consumed
      outBuf,                       // output buffer base
      outBuf,                       // current output position
      &outBytes,                    // in: available, out: produced
      TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF  // raw deflate, single-shot
    );

    free(decomp);
    free(compBuf);

    if (status != TINFL_STATUS_DONE) {
      Serial.printf("ZipReader: DEFLATE failed (status %d)\n", (int)status);
      free(outBuf);
      return nullptr;
    }

    outBuf[outBytes] = '\0';  // Null-terminate for text files
    *outSize = (uint32_t)outBytes;

    if (outBytes != uncompSize) {
      Serial.printf("ZipReader: decompressed %u bytes, expected %u\n",
                    (uint32_t)outBytes, uncompSize);
    }

    return outBuf;

#else
    // No ROM tinfl available
    Serial.println("ZipReader: DEFLATE not supported (no ROM tinfl)");
    *outSize = 0;
    return nullptr;
#endif
  }
};

// =============================================================================
// FALLBACK NOTE:
//
// If the ROM tinfl includes fail to compile on your ESP32 variant, you have
// two options:
//
// 1. Install lbernstone/miniz-esp32 from PlatformIO:
//      lib_deps = https://github.com/lbernstone/miniz-esp32.git
//    Then change the includes above to: #include <miniz.h>
//
// 2. Copy just the tinfl source (~550 lines) from:
//      https://github.com/richgel999/miniz/blob/master/miniz_tinfl.c
//    into your project. Only tinfl_decompress_mem_to_mem() is needed.
//
// =============================================================================