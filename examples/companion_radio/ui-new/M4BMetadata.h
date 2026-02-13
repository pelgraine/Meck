#pragma once

// =============================================================================
// M4BMetadata.h - Lightweight MP4/M4B atom parser for metadata extraction
//
// Walks the MP4 atom (box) tree to extract:
//   - Title     (moov/udta/meta/ilst/©nam)
//   - Author    (moov/udta/meta/ilst/©ART)
//   - Cover art (moov/udta/meta/ilst/covr) - JPEG offset+size within file
//   - Duration  (moov/mvhd timescale + duration)
//   - Chapter markers (moov/udta/chpl) - Nero-style chapter list
//
// Designed for embedded use: no dynamic allocation, reads directly from SD
// via Arduino File API, uses a small stack buffer for atom headers.
//
// Usage:
//   M4BMetadata meta;
//   File f = SD.open("/audiobooks/mybook.m4b");
//   if (meta.parse(f)) {
//     Serial.printf("Title: %s\n", meta.title);
//     Serial.printf("Author: %s\n", meta.author);
//     if (meta.hasCoverArt) {
//       // JPEG data is at meta.coverOffset, meta.coverSize bytes
//     }
//   }
//   f.close();
// =============================================================================

#include <SD.h>

// Maximum metadata string lengths (including null terminator)
#define M4B_MAX_TITLE    128
#define M4B_MAX_AUTHOR   64
#define M4B_MAX_CHAPTERS 100

struct M4BChapter {
  uint32_t startMs;    // Chapter start time in milliseconds
  char     name[48];   // Chapter title (truncated to fit)
};

class M4BMetadata {
public:
  // Extracted metadata
  char     title[M4B_MAX_TITLE];
  char     author[M4B_MAX_AUTHOR];
  bool     hasCoverArt;
  uint32_t coverOffset;   // Byte offset of JPEG/PNG data within file
  uint32_t coverSize;     // Size of cover image data in bytes
  uint8_t  coverFormat;   // 13=JPEG, 14=PNG (from MP4 well-known type)
  uint32_t durationMs;    // Total duration in milliseconds
  uint32_t sampleRate;    // Audio sample rate (from audio stsd)
  uint32_t bitrate;       // Approximate bitrate in bps

  // Chapter data
  M4BChapter chapters[M4B_MAX_CHAPTERS];
  int        chapterCount;

  M4BMetadata() { clear(); }

  void clear() {
    title[0] = '\0';
    author[0] = '\0';
    hasCoverArt = false;
    coverOffset = 0;
    coverSize = 0;
    coverFormat = 0;
    durationMs = 0;
    sampleRate = 44100;
    bitrate = 0;
    chapterCount = 0;
  }

  // Parse an open file. Returns true if at least title or duration was found.
  // File position is NOT preserved — caller should seek as needed afterward.
  bool parse(File& file) {
    clear();
    if (!file || file.size() < 8) return false;

    _fileSize = file.size();

    // Walk top-level atoms looking for 'moov'
    uint32_t pos = 0;
    while (pos < _fileSize) {
      AtomHeader hdr;
      if (!readAtomHeader(file, pos, hdr)) break;
      if (hdr.size < 8) break;

      if (hdr.type == ATOM_MOOV) {
        parseMoov(file, hdr.dataOffset, hdr.dataOffset + hdr.dataSize);
        break;  // moov found and parsed, we're done
      }

      // Skip to next top-level atom
      pos += hdr.size;
      if (hdr.size == 0) break;  // size=0 means "extends to EOF"
    }

    return (title[0] != '\0' || durationMs > 0);
  }

  // Get chapter index for a given playback position (milliseconds).
  // Returns -1 if no chapters or position is before first chapter.
  int getChapterForPosition(uint32_t positionMs) const {
    if (chapterCount == 0) return -1;
    int ch = 0;
    for (int i = 1; i < chapterCount; i++) {
      if (chapters[i].startMs > positionMs) break;
      ch = i;
    }
    return ch;
  }

  // Get the start position of the next chapter after the given position.
  // Returns 0 if no next chapter.
  uint32_t getNextChapterMs(uint32_t positionMs) const {
    for (int i = 0; i < chapterCount; i++) {
      if (chapters[i].startMs > positionMs) return chapters[i].startMs;
    }
    return 0;
  }

  // Get the start position of the current or previous chapter.
  uint32_t getPrevChapterMs(uint32_t positionMs) const {
    uint32_t prev = 0;
    for (int i = 0; i < chapterCount; i++) {
      if (chapters[i].startMs >= positionMs) break;
      prev = chapters[i].startMs;
    }
    return prev;
  }

private:
  uint32_t _fileSize;

  // MP4 atom type codes (big-endian FourCC)
  static constexpr uint32_t ATOM_MOOV = 0x6D6F6F76;  // 'moov'
  static constexpr uint32_t ATOM_MVHD = 0x6D766864;  // 'mvhd'
  static constexpr uint32_t ATOM_UDTA = 0x75647461;  // 'udta'
  static constexpr uint32_t ATOM_META = 0x6D657461;  // 'meta'
  static constexpr uint32_t ATOM_ILST = 0x696C7374;  // 'ilst'
  static constexpr uint32_t ATOM_NAM  = 0xA96E616D;  // '©nam'
  static constexpr uint32_t ATOM_ART  = 0xA9415254;  // '©ART'
  static constexpr uint32_t ATOM_COVR = 0x636F7672;  // 'covr'
  static constexpr uint32_t ATOM_DATA = 0x64617461;  // 'data'
  static constexpr uint32_t ATOM_CHPL = 0x6368706C;  // 'chpl' (Nero chapters)
  static constexpr uint32_t ATOM_TRAK = 0x7472616B;  // 'trak'
  static constexpr uint32_t ATOM_MDIA = 0x6D646961;  // 'mdia'
  static constexpr uint32_t ATOM_MDHD = 0x6D646864;  // 'mdhd'
  static constexpr uint32_t ATOM_HDLR = 0x68646C72;  // 'hdlr'

  struct AtomHeader {
    uint32_t type;
    uint64_t size;        // Total atom size including header
    uint32_t dataOffset;  // File offset where data begins (after header)
    uint64_t dataSize;    // size - header_length
  };

  // Read a 32-bit big-endian value from file at current position
  static uint32_t readU32BE(File& file) {
    uint8_t buf[4];
    file.read(buf, 4);
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8)  | buf[3];
  }

  // Read a 64-bit big-endian value
  static uint64_t readU64BE(File& file) {
    uint32_t hi = readU32BE(file);
    uint32_t lo = readU32BE(file);
    return ((uint64_t)hi << 32) | lo;
  }

  // Read a 16-bit big-endian value
  static uint16_t readU16BE(File& file) {
    uint8_t buf[2];
    file.read(buf, 2);
    return ((uint16_t)buf[0] << 8) | buf[1];
  }

  // Read atom header at given file offset
  bool readAtomHeader(File& file, uint32_t offset, AtomHeader& hdr) {
    if (offset + 8 > _fileSize) return false;

    file.seek(offset);
    uint32_t size32 = readU32BE(file);
    hdr.type = readU32BE(file);

    if (size32 == 1) {
      // 64-bit extended size
      if (offset + 16 > _fileSize) return false;
      hdr.size = readU64BE(file);
      hdr.dataOffset = offset + 16;
      hdr.dataSize = (hdr.size > 16) ? hdr.size - 16 : 0;
    } else if (size32 == 0) {
      // Atom extends to end of file
      hdr.size = _fileSize - offset;
      hdr.dataOffset = offset + 8;
      hdr.dataSize = hdr.size - 8;
    } else {
      hdr.size = size32;
      hdr.dataOffset = offset + 8;
      hdr.dataSize = (size32 > 8) ? size32 - 8 : 0;
    }

    return true;
  }

  // Parse the moov container atom
  void parseMoov(File& file, uint32_t start, uint32_t end) {
    uint32_t pos = start;
    while (pos < end) {
      AtomHeader hdr;
      if (!readAtomHeader(file, pos, hdr)) break;
      if (hdr.size < 8) break;

      switch (hdr.type) {
        case ATOM_MVHD:
          parseMvhd(file, hdr.dataOffset, (uint32_t)hdr.dataSize);
          break;
        case ATOM_UDTA:
          parseUdta(file, hdr.dataOffset, hdr.dataOffset + (uint32_t)hdr.dataSize);
          break;
        case ATOM_TRAK:
          break;
      }

      pos += (uint32_t)hdr.size;
    }
  }

  // Parse mvhd (movie header) for duration
  void parseMvhd(File& file, uint32_t offset, uint32_t size) {
    file.seek(offset);
    uint8_t version = file.read();

    if (version == 0) {
      file.seek(offset + 4);   // skip version(1) + flags(3)
      /* create_time */ readU32BE(file);
      /* modify_time */ readU32BE(file);
      uint32_t timescale = readU32BE(file);
      uint32_t duration = readU32BE(file);
      if (timescale > 0) {
        durationMs = (uint32_t)((uint64_t)duration * 1000 / timescale);
      }
    } else if (version == 1) {
      file.seek(offset + 4);
      /* create_time */ readU64BE(file);
      /* modify_time */ readU64BE(file);
      uint32_t timescale = readU32BE(file);
      uint64_t duration = readU64BE(file);
      if (timescale > 0) {
        durationMs = (uint32_t)(duration * 1000 / timescale);
      }
    }
  }

  // Parse udta container — contains meta and/or chpl
  void parseUdta(File& file, uint32_t start, uint32_t end) {
    uint32_t pos = start;
    while (pos < end) {
      AtomHeader hdr;
      if (!readAtomHeader(file, pos, hdr)) break;
      if (hdr.size < 8) break;

      if (hdr.type == ATOM_META) {
        parseMeta(file, hdr.dataOffset + 4,
                  hdr.dataOffset + (uint32_t)hdr.dataSize);
      } else if (hdr.type == ATOM_CHPL) {
        parseChpl(file, hdr.dataOffset, (uint32_t)hdr.dataSize);
      }

      pos += (uint32_t)hdr.size;
    }
  }

  // Parse meta container — contains hdlr + ilst
  void parseMeta(File& file, uint32_t start, uint32_t end) {
    uint32_t pos = start;
    while (pos < end) {
      AtomHeader hdr;
      if (!readAtomHeader(file, pos, hdr)) break;
      if (hdr.size < 8) break;

      if (hdr.type == ATOM_ILST) {
        parseIlst(file, hdr.dataOffset, hdr.dataOffset + (uint32_t)hdr.dataSize);
      }

      pos += (uint32_t)hdr.size;
    }
  }

  // Parse ilst (iTunes metadata list) — contains ©nam, ©ART, covr etc.
  void parseIlst(File& file, uint32_t start, uint32_t end) {
    uint32_t pos = start;
    while (pos < end) {
      AtomHeader hdr;
      if (!readAtomHeader(file, pos, hdr)) break;
      if (hdr.size < 8) break;

      switch (hdr.type) {
        case ATOM_NAM:
          extractTextData(file, hdr.dataOffset,
                          hdr.dataOffset + (uint32_t)hdr.dataSize,
                          title, M4B_MAX_TITLE);
          break;
        case ATOM_ART:
          extractTextData(file, hdr.dataOffset,
                          hdr.dataOffset + (uint32_t)hdr.dataSize,
                          author, M4B_MAX_AUTHOR);
          break;
        case ATOM_COVR:
          extractCoverData(file, hdr.dataOffset,
                           hdr.dataOffset + (uint32_t)hdr.dataSize);
          break;
      }

      pos += (uint32_t)hdr.size;
    }
  }

  // Extract text from a 'data' sub-atom within an ilst entry.
  void extractTextData(File& file, uint32_t start, uint32_t end,
                       char* dest, int maxLen) {
    uint32_t pos = start;
    while (pos < end) {
      AtomHeader hdr;
      if (!readAtomHeader(file, pos, hdr)) break;
      if (hdr.size < 8) break;

      if (hdr.type == ATOM_DATA && hdr.dataSize > 8) {
        uint32_t textOffset = hdr.dataOffset + 8;
        uint32_t textLen = (uint32_t)hdr.dataSize - 8;
        if (textLen > (uint32_t)(maxLen - 1)) textLen = maxLen - 1;

        file.seek(textOffset);
        file.read((uint8_t*)dest, textLen);
        dest[textLen] = '\0';
        return;
      }

      pos += (uint32_t)hdr.size;
    }
  }

  // Extract cover art location from 'data' sub-atom within covr.
  void extractCoverData(File& file, uint32_t start, uint32_t end) {
    uint32_t pos = start;
    while (pos < end) {
      AtomHeader hdr;
      if (!readAtomHeader(file, pos, hdr)) break;
      if (hdr.size < 8) break;

      if (hdr.type == ATOM_DATA && hdr.dataSize > 8) {
        file.seek(hdr.dataOffset);
        uint32_t typeIndicator = readU32BE(file);
        uint8_t wellKnownType = typeIndicator & 0xFF;

        coverOffset = hdr.dataOffset + 8;
        coverSize = (uint32_t)hdr.dataSize - 8;
        coverFormat = wellKnownType;  // 13=JPEG, 14=PNG
        hasCoverArt = (coverSize > 0);

        Serial.printf("M4B: Cover art found - %s, %u bytes at offset %u\n",
                      wellKnownType == 13 ? "JPEG" :
                      wellKnownType == 14 ? "PNG" : "unknown",
                      coverSize, coverOffset);
        return;
      }

      pos += (uint32_t)hdr.size;
    }
  }

  // Parse Nero-style chapter list (chpl atom).
  void parseChpl(File& file, uint32_t offset, uint32_t size) {
    if (size < 9) return;

    file.seek(offset);
    uint8_t version = file.read();
    file.read();  // flags byte 1
    file.read();  // flags byte 2
    file.read();  // flags byte 3

    file.read();  // reserved

    uint32_t count;
    if (version == 1) {
      count = readU32BE(file);
    } else {
      count = file.read();
    }

    if (count > M4B_MAX_CHAPTERS) count = M4B_MAX_CHAPTERS;

    chapterCount = 0;
    for (uint32_t i = 0; i < count; i++) {
      if (!file.available()) break;

      uint64_t timestamp = readU64BE(file);
      uint32_t startMs = (uint32_t)(timestamp / 10000);  // 100ns -> ms

      uint8_t nameLen = file.read();
      if (nameLen == 0 || !file.available()) break;

      M4BChapter& ch = chapters[chapterCount];
      ch.startMs = startMs;

      uint8_t readLen = (nameLen < sizeof(ch.name) - 1) ? nameLen : sizeof(ch.name) - 1;
      file.read((uint8_t*)ch.name, readLen);
      ch.name[readLen] = '\0';

      if (nameLen > readLen) {
        file.seek(file.position() + (nameLen - readLen));
      }

      chapterCount++;
    }

    Serial.printf("M4B: Found %d chapters\n", chapterCount);
  }
};