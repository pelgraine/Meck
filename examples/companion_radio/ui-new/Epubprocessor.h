#pragma once
// =============================================================================
// EpubProcessor.h - Convert EPUB files to plain text for TextReaderScreen
//
// Pipeline: EPUB (ZIP) â†’ container.xml â†’ OPF spine â†’ extract chapters â†’
//           strip XHTML tags â†’ concatenated plain text â†’ cached .txt on SD
//
// The resulting .txt file is placed in /books/ and picked up automatically
// by TextReaderScreen's existing pagination, indexing, and bookmarking.
//
// Dependencies: EpubZipReader.h (for ZIP extraction)
// =============================================================================

#include <SD.h>
#include <FS.h>
#include "EpubZipReader.h"
#include "Utf8CP437.h"

// Maximum chapters in spine (most novels have 20-80)
#define EPUB_MAX_CHAPTERS 200

// Maximum manifest items we track
#define EPUB_MAX_MANIFEST 256

// Buffer size for reading OPF/container XML
// (These are small files, typically 1-20KB)
#define EPUB_XML_BUF_SIZE 64

class EpubProcessor {
public:

  // ----------------------------------------------------------
  // Process an EPUB file: extract text and write to SD cache.
  //
  // epubPath:  source, e.g. "/books/The Iliad.epub"
  // txtPath:   output, e.g. "/books/The Iliad by Homer.txt"
  //
  // Returns true if the .txt file was written successfully.
  // If txtPath already exists, returns true immediately (cached).
  // ----------------------------------------------------------
  static bool processToText(const char* epubPath, const char* txtPath) {
    // Check if already cached
    if (SD.exists(txtPath)) {
      Serial.printf("EpubProc: '%s' already cached\n", txtPath);
      return true;
    }

    Serial.printf("EpubProc: Processing '%s'\n", epubPath);
    unsigned long t0 = millis();

    // Open the EPUB (ZIP archive)
    File epubFile = SD.open(epubPath, FILE_READ);
    if (!epubFile) {
      Serial.println("EpubProc: Cannot open EPUB file");
      return false;
    }

    // Heap-allocate zip reader (entries table is ~19KB)
    EpubZipReader* zip = new EpubZipReader();
    if (!zip) {
      epubFile.close();
      Serial.println("EpubProc: Cannot allocate ZipReader");
      return false;
    }

    if (!zip->open(epubFile)) {
      delete zip;
      epubFile.close();
      Serial.println("EpubProc: Cannot parse ZIP structure");
      return false;
    }

    // Step 1: Find OPF path from container.xml
    char opfPath[EPUB_XML_BUF_SIZE];
    opfPath[0] = '\0';
    if (!_findOpfPath(zip, opfPath, sizeof(opfPath))) {
      delete zip;
      epubFile.close();
      Serial.println("EpubProc: Cannot find OPF path");
      return false;
    }
    Serial.printf("EpubProc: OPF at '%s'\n", opfPath);

    // Determine the content base directory (e.g., "OEBPS/")
    char baseDir[EPUB_XML_BUF_SIZE];
    _getDirectory(opfPath, baseDir, sizeof(baseDir));

    // Step 2: Parse OPF to get title and spine chapter order
    char title[128];
    title[0] = '\0';

    // Chapter paths in spine order
    char** chapterPaths = nullptr;
    int chapterCount = 0;

    if (!_parseOpf(zip, opfPath, baseDir, title, sizeof(title),
                   &chapterPaths, &chapterCount)) {
      delete zip;
      epubFile.close();
      Serial.println("EpubProc: Cannot parse OPF");
      return false;
    }

    Serial.printf("EpubProc: Title='%s', %d chapters\n", title, chapterCount);

    // Step 3: Extract each chapter, strip XHTML, write to output .txt
    File outFile = SD.open(txtPath, FILE_WRITE);
    if (!outFile) {
      _freeChapterPaths(chapterPaths, chapterCount);
      delete zip;
      epubFile.close();
      Serial.printf("EpubProc: Cannot create '%s'\n", txtPath);
      return false;
    }

    // Write title as first line
    if (title[0]) {
      outFile.println(title);
      outFile.println();
    }

    int chaptersWritten = 0;
    uint32_t totalBytes = 0;

    for (int i = 0; i < chapterCount; i++) {
      int entryIdx = zip->findEntry(chapterPaths[i]);
      if (entryIdx < 0) {
        Serial.printf("EpubProc: Chapter not found: '%s'\n", chapterPaths[i]);
        continue;
      }

      uint32_t rawSize = 0;
      uint8_t* rawData = zip->extractEntry(entryIdx, &rawSize);
      if (!rawData || rawSize == 0) {
        Serial.printf("EpubProc: Failed to extract chapter %d\n", i);
        if (rawData) free(rawData);
        continue;
      }

      // Strip XHTML tags and write plain text
      uint32_t textLen = 0;
      uint8_t* plainText = _stripXhtml(rawData, rawSize, &textLen);
      free(rawData);

      if (plainText && textLen > 0) {
        outFile.write(plainText, textLen);
        // Add chapter separator
        outFile.print("\n\n");
        totalBytes += textLen + 2;
        chaptersWritten++;
      }
      if (plainText) free(plainText);
    }

    outFile.flush();
    outFile.close();

    // Release SD CS for other SPI users
    digitalWrite(SDCARD_CS, HIGH);

    _freeChapterPaths(chapterPaths, chapterCount);
    delete zip;
    epubFile.close();

    unsigned long elapsed = millis() - t0;
    Serial.printf("EpubProc: Done! %d chapters, %u bytes in %lu ms -> '%s'\n",
                  chaptersWritten, totalBytes, elapsed, txtPath);

    return chaptersWritten > 0;
  }

  // ----------------------------------------------------------
  // Extract just the title from an EPUB (for display in file list).
  // Returns false if it can't be determined.
  // ----------------------------------------------------------
  static bool getTitle(const char* epubPath, char* titleBuf, int titleBufSize) {
    File epubFile = SD.open(epubPath, FILE_READ);
    if (!epubFile) return false;

    EpubZipReader* zip = new EpubZipReader();
    if (!zip) { epubFile.close(); return false; }

    if (!zip->open(epubFile)) {
      delete zip; epubFile.close(); return false;
    }

    char opfPath[EPUB_XML_BUF_SIZE];
    if (!_findOpfPath(zip, opfPath, sizeof(opfPath))) {
      delete zip; epubFile.close(); return false;
    }

    // Extract OPF and find <dc:title>
    int opfIdx = zip->findEntry(opfPath);
    if (opfIdx < 0) { delete zip; epubFile.close(); return false; }

    uint32_t opfSize = 0;
    uint8_t* opfData = zip->extractEntry(opfIdx, &opfSize);
    delete zip;
    epubFile.close();

    if (!opfData) return false;

    bool found = _extractTagContent((const char*)opfData, opfSize,
                                     "dc:title", titleBuf, titleBufSize);
    free(opfData);
    return found;
  }

  // ----------------------------------------------------------
  // Build a cache .txt path from an .epub path.
  // e.g., "/books/mybook.epub" -> "/books/.epub_cache/mybook.txt"
  // ----------------------------------------------------------
  static void buildCachePath(const char* epubPath, char* cachePath, int cachePathSize) {
    // Extract filename without extension
    const char* lastSlash = strrchr(epubPath, '/');
    const char* filename = lastSlash ? lastSlash + 1 : epubPath;
    
    // Find the directory part
    char dir[128];
    if (lastSlash) {
      int dirLen = lastSlash - epubPath;
      if (dirLen >= (int)sizeof(dir)) dirLen = sizeof(dir) - 1;
      strncpy(dir, epubPath, dirLen);
      dir[dirLen] = '\0';
    } else {
      strcpy(dir, "/books");
    }

    // Create cache directory if needed
    char cacheDir[160];
    snprintf(cacheDir, sizeof(cacheDir), "%s/.epub_cache", dir);
    if (!SD.exists(cacheDir)) {
      SD.mkdir(cacheDir);
    }

    // Strip .epub extension
    char baseName[128];
    strncpy(baseName, filename, sizeof(baseName) - 1);
    baseName[sizeof(baseName) - 1] = '\0';
    char* dot = strrchr(baseName, '.');
    if (dot) *dot = '\0';

    snprintf(cachePath, cachePathSize, "%s/%s.txt", cacheDir, baseName);
  }

private:

  // ----------------------------------------------------------
  // Parse container.xml to find the OPF file path.
  // Returns true if found.
  // ----------------------------------------------------------
  static bool _findOpfPath(EpubZipReader* zip, char* opfPath, int opfPathSize) {
    int idx = zip->findEntry("META-INF/container.xml");
    if (idx < 0) {
      // Fallback: find any .opf file directly
      idx = zip->findEntryBySuffix(".opf");
      if (idx >= 0) {
        const ZipEntry* e = zip->getEntry(idx);
        strncpy(opfPath, e->filename, opfPathSize - 1);
        opfPath[opfPathSize - 1] = '\0';
        return true;
      }
      return false;
    }

    uint32_t size = 0;
    uint8_t* data = zip->extractEntry(idx, &size);
    if (!data) return false;

    // Find: full-path="OEBPS/content.opf"
    bool found = _extractAttribute((const char*)data, size,
                                    "full-path", opfPath, opfPathSize);
    free(data);
    return found;
  }

  // ----------------------------------------------------------
  // Parse OPF to extract title, build manifest, and resolve spine.
  //
  // Populates chapterPaths (heap-allocated array of strings) with
  // full ZIP paths for each chapter in spine order.
  // Caller must free with _freeChapterPaths().
  // ----------------------------------------------------------
  static bool _parseOpf(EpubZipReader* zip, const char* opfPath,
                         const char* baseDir, char* title, int titleSize,
                         char*** outChapterPaths, int* outChapterCount) {
    int opfIdx = zip->findEntry(opfPath);
    if (opfIdx < 0) return false;

    uint32_t opfSize = 0;
    uint8_t* opfData = zip->extractEntry(opfIdx, &opfSize);
    if (!opfData) return false;

    const char* xml = (const char*)opfData;

    // Extract title
    _extractTagContent(xml, opfSize, "dc:title", title, titleSize);

    // Build manifest: map id -> href
    // We use two parallel arrays to avoid complex data structures
    struct ManifestItem {
      char id[64];
      char href[128];
      bool isContent;  // has media-type containing "html" or "xml"
    };

    // Heap-allocate manifest (could be large)
    ManifestItem* manifest = (ManifestItem*)ps_malloc(
        EPUB_MAX_MANIFEST * sizeof(ManifestItem));
    if (!manifest) {
      manifest = (ManifestItem*)malloc(EPUB_MAX_MANIFEST * sizeof(ManifestItem));
    }
    if (!manifest) {
      free(opfData);
      return false;
    }
    int manifestCount = 0;

    // Parse <item> elements from <manifest>
    const char* manifestStart = _findTag(xml, opfSize, "<manifest");
    const char* manifestEnd = manifestStart ?
        _findTag(manifestStart, opfSize - (manifestStart - xml), "</manifest") : nullptr;
    if (!manifestEnd) manifestEnd = xml + opfSize;

    if (manifestStart) {
      const char* pos = manifestStart;
      while (pos < manifestEnd && manifestCount < EPUB_MAX_MANIFEST) {
        pos = _findTag(pos, manifestEnd - pos, "<item");
        if (!pos || pos >= manifestEnd) break;

        // Find the closing > of this <item ... />
        const char* tagEnd = (const char*)memchr(pos, '>', manifestEnd - pos);
        if (!tagEnd) break;
        tagEnd++;

        ManifestItem& item = manifest[manifestCount];
        item.id[0] = '\0';
        item.href[0] = '\0';
        item.isContent = false;

        _extractAttributeFromTag(pos, tagEnd - pos, "id",
                                  item.id, sizeof(item.id));
        _extractAttributeFromTag(pos, tagEnd - pos, "href",
                                  item.href, sizeof(item.href));

        // Check media-type for content files
        char mediaType[64];
        mediaType[0] = '\0';
        _extractAttributeFromTag(pos, tagEnd - pos, "media-type",
                                  mediaType, sizeof(mediaType));
        item.isContent = (strstr(mediaType, "html") != nullptr ||
                          strstr(mediaType, "xml") != nullptr);

        if (item.id[0] && item.href[0]) {
          manifestCount++;
        }

        pos = tagEnd;
      }
    }

    Serial.printf("EpubProc: Manifest has %d items\n", manifestCount);

    // Parse <spine> to get reading order
    // Spine contains <itemref idref="..."/> elements
    const char* spineStart = _findTag(xml, opfSize, "<spine");
    const char* spineEnd = spineStart ?
        _findTag(spineStart, opfSize - (spineStart - xml), "</spine") : nullptr;
    if (!spineEnd) spineEnd = xml + opfSize;

    // Collect spine idrefs
    char** chapterPaths = (char**)ps_malloc(EPUB_MAX_CHAPTERS * sizeof(char*));
    if (!chapterPaths) chapterPaths = (char**)malloc(EPUB_MAX_CHAPTERS * sizeof(char*));
    if (!chapterPaths) {
      free(manifest);
      free(opfData);
      return false;
    }
    int chapterCount = 0;

    if (spineStart) {
      const char* pos = spineStart;
      while (pos < spineEnd && chapterCount < EPUB_MAX_CHAPTERS) {
        pos = _findTag(pos, spineEnd - pos, "<itemref");
        if (!pos || pos >= spineEnd) break;

        const char* tagEnd = (const char*)memchr(pos, '>', spineEnd - pos);
        if (!tagEnd) break;
        tagEnd++;

        char idref[64];
        idref[0] = '\0';
        _extractAttributeFromTag(pos, tagEnd - pos, "idref",
                                  idref, sizeof(idref));

        if (idref[0]) {
          // Look up in manifest
          for (int m = 0; m < manifestCount; m++) {
            if (strcmp(manifest[m].id, idref) == 0 && manifest[m].isContent) {
              // Build full path: baseDir + href
              int pathLen = strlen(baseDir) + strlen(manifest[m].href) + 1;
              char* fullPath = (char*)malloc(pathLen);
              if (fullPath) {
                snprintf(fullPath, pathLen, "%s%s", baseDir, manifest[m].href);
                chapterPaths[chapterCount++] = fullPath;
              }
              break;
            }
          }
        }

        pos = tagEnd;
      }
    }

    free(manifest);
    free(opfData);

    *outChapterPaths = chapterPaths;
    *outChapterCount = chapterCount;

    return chapterCount > 0;
  }

  // ----------------------------------------------------------
  // Strip XHTML/HTML tags from raw content, producing plain text.
  //
  // Handles:
  //   - Tag removal (everything between < and >)
  //   - <p>, <br>, <div>, <h1>-<h6> â†’ newlines
  //   - HTML entity decoding (&amp; &lt; &gt; &quot; &apos; &#NNN; &#xHH;)
  //   - Collapse multiple whitespace/newlines
  //   - Skip <head>, <style>, <script> content entirely
  //
  // Returns heap-allocated buffer (caller must free).
  // ----------------------------------------------------------
  static uint8_t* _stripXhtml(const uint8_t* input, uint32_t inputLen,
                               uint32_t* outLen) {
    // Output can't be larger than input
    uint8_t* output = (uint8_t*)ps_malloc(inputLen + 1);
    if (!output) output = (uint8_t*)malloc(inputLen + 1);
    if (!output) { *outLen = 0; return nullptr; }

    uint32_t outPos = 0;
    bool inTag = false;
    bool skipContent = false;  // Inside <head>, <style>, <script>
    char tagName[32];
    int tagNamePos = 0;
    bool tagNameDone = false;
    bool isClosingTag = false;
    bool lastWasNewline = false;
    bool lastWasSpace = false;

    // Skip to <body> if present (ignore everything before it)
    const uint8_t* start = input;
    const uint8_t* inputEnd = input + inputLen;
    const char* bodyStart = _findTagCI((const char*)input, inputLen, "<body");
    if (bodyStart) {
      const char* bodyTagEnd = (const char*)memchr(bodyStart, '>', 
          inputEnd - (const uint8_t*)bodyStart);
      if (bodyTagEnd) {
        start = (const uint8_t*)(bodyTagEnd + 1);
      }
    }
    const uint8_t* end = inputEnd;

    for (const uint8_t* p = start; p < end; p++) {
      char c = (char)*p;

      if (inTag) {
        // Collecting tag name
        if (!tagNameDone) {
          if (tagNamePos == 0 && c == '/') {
            isClosingTag = true;
            continue;
          }
          if (c == '>' || c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '/') {
            tagName[tagNamePos] = '\0';
            tagNameDone = true;
          } else if (tagNamePos < (int)sizeof(tagName) - 1) {
            tagName[tagNamePos++] = (c >= 'A' && c <= 'Z') ? (c + 32) : c;
          }
        }

        if (c == '>') {
          inTag = false;

          // Handle skip regions
          if (!isClosingTag) {
            if (strcmp(tagName, "head") == 0 ||
                strcmp(tagName, "style") == 0 ||
                strcmp(tagName, "script") == 0) {
              skipContent = true;
            }
          } else {
            if (strcmp(tagName, "head") == 0 ||
                strcmp(tagName, "style") == 0 ||
                strcmp(tagName, "script") == 0) {
              skipContent = false;
            }
          }

          if (!skipContent) {
            // Block-level elements produce newlines
            if (strcmp(tagName, "p") == 0 ||
                strcmp(tagName, "div") == 0 ||
                strcmp(tagName, "br") == 0 ||
                strcmp(tagName, "h1") == 0 ||
                strcmp(tagName, "h2") == 0 ||
                strcmp(tagName, "h3") == 0 ||
                strcmp(tagName, "h4") == 0 ||
                strcmp(tagName, "h5") == 0 ||
                strcmp(tagName, "h6") == 0 ||
                strcmp(tagName, "li") == 0 ||
                strcmp(tagName, "tr") == 0 ||
                strcmp(tagName, "blockquote") == 0 ||
                strcmp(tagName, "hr") == 0) {
              if (outPos > 0 && !lastWasNewline) {
                output[outPos++] = '\n';
                lastWasNewline = true;
                lastWasSpace = false;
              }
            }
          }

          continue;
        }
        continue;
      }

      // Not in a tag
      if (c == '<') {
        inTag = true;
        tagNamePos = 0;
        tagNameDone = false;
        isClosingTag = false;
        continue;
      }

      if (skipContent) continue;

      // Handle HTML entities
      if (c == '&') {
        char decoded = _decodeEntity(p, end, &p);
        if (decoded) {
          c = decoded;
          // p now points to the ';' or last char of entity; loop will increment
        }
      }

      // Handle UTF-8 multi-byte sequences (smart quotes, em dashes, accented chars, etc.)
      // These appear as raw bytes in XHTML. Typographic chars are mapped to ASCII;
      // accented Latin chars are preserved as UTF-8 for CP437 rendering on e-ink.
      if ((uint8_t)c >= 0xC0) {
        uint32_t codepoint = 0;
        int extraBytes = 0;

        if (((uint8_t)c & 0xE0) == 0xC0) {
          // 2-byte sequence: 110xxxxx 10xxxxxx
          codepoint = (uint8_t)c & 0x1F;
          extraBytes = 1;
        } else if (((uint8_t)c & 0xF0) == 0xE0) {
          // 3-byte sequence: 1110xxxx 10xxxxxx 10xxxxxx
          codepoint = (uint8_t)c & 0x0F;
          extraBytes = 2;
        } else if (((uint8_t)c & 0xF8) == 0xF0) {
          // 4-byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx
          codepoint = (uint8_t)c & 0x07;
          extraBytes = 3;
        }

        // Read continuation bytes
        bool valid = true;
        for (int b = 0; b < extraBytes && p + 1 + b < end; b++) {
          uint8_t cb = *(p + 1 + b);
          if ((cb & 0xC0) != 0x80) { valid = false; break; }
          codepoint = (codepoint << 6) | (cb & 0x3F);
        }

        if (valid && extraBytes > 0) {
          p += extraBytes;  // Skip continuation bytes (loop increments past lead byte)

          // Map Unicode codepoints to displayable equivalents
          // Typographic chars → ASCII, accented chars → preserved as UTF-8
          char mapped = 0;
          switch (codepoint) {
            case 0x2018: case 0x2019: mapped = '\''; break;  // Smart single quotes
            case 0x201C: case 0x201D: mapped = '"';  break;  // Smart double quotes
            case 0x2013: case 0x2014: mapped = '-';  break;  // En/em dash
            case 0x2026:             mapped = '.';  break;  // Ellipsis
            case 0x2022:             mapped = '*';  break;  // Bullet
            case 0x00A0:             mapped = ' ';  break;  // Non-breaking space
            case 0x00AB: case 0x00BB: mapped = '"'; break;  // Guillemets
            case 0x2032:             mapped = '\''; break;  // Prime
            case 0x2033:             mapped = '"';  break;  // Double prime
            case 0x2010: case 0x2011: mapped = '-'; break;  // Hyphens
            case 0x2012:             mapped = '-';  break;  // Figure dash
            case 0x2015:             mapped = '-';  break;  // Horizontal bar
            case 0x2039: case 0x203A: mapped = '\''; break; // Single guillemets
            default:
              if (codepoint >= 0x20 && codepoint < 0x7F) {
                mapped = (char)codepoint;  // Basic ASCII range
              } else if (unicodeToCP437(codepoint)) {
                // Accented character that the e-ink font can render via CP437.
                // Preserve as UTF-8 in the output; the text reader will decode
                // and map to CP437 at render time.
                if (codepoint <= 0x7FF) {
                  output[outPos++] = 0xC0 | (codepoint >> 6);
                  output[outPos++] = 0x80 | (codepoint & 0x3F);
                } else if (codepoint <= 0xFFFF) {
                  output[outPos++] = 0xE0 | (codepoint >> 12);
                  output[outPos++] = 0x80 | ((codepoint >> 6) & 0x3F);
                  output[outPos++] = 0x80 | (codepoint & 0x3F);
                }
                lastWasNewline = false;
                lastWasSpace = false;
                continue;  // Already wrote to output
              } else {
                continue;  // Skip unmappable characters
              }
              break;
          }
          c = mapped;
        } else {
          continue;  // Skip malformed UTF-8
        }
      } else if ((uint8_t)c >= 0x80) {
        // Stray continuation byte (0x80-0xBF) â€” skip
        continue;
      }

      // Whitespace collapsing
      if (c == '\n' || c == '\r') {
        if (!lastWasNewline && outPos > 0) {
          output[outPos++] = '\n';
          lastWasNewline = true;
          lastWasSpace = false;
        }
        continue;
      }

      if (c == ' ' || c == '\t') {
        if (!lastWasSpace && !lastWasNewline && outPos > 0) {
          output[outPos++] = ' ';
          lastWasSpace = true;
        }
        continue;
      }

      // Regular character
      output[outPos++] = c;
      lastWasNewline = false;
      lastWasSpace = false;
    }

    // Trim trailing whitespace
    while (outPos > 0 && (output[outPos-1] == '\n' || output[outPos-1] == ' ')) {
      outPos--;
    }

    output[outPos] = '\0';
    *outLen = outPos;
    return output;
  }

  // ----------------------------------------------------------
  // Decode an HTML entity starting at '&'.
  // Advances *pos to the last character consumed.
  // Returns the decoded character, or '&' if not recognized.
  // ----------------------------------------------------------
  static char _decodeEntity(const uint8_t* p, const uint8_t* end,
                             const uint8_t** outPos) {
    // Look for ';' within a reasonable range
    const uint8_t* semi = p + 1;
    int maxLen = 10;
    while (semi < end && semi < p + maxLen && *semi != ';') semi++;

    if (*semi != ';' || semi >= end) {
      *outPos = p;  // Not an entity, return '&' literal
      return '&';
    }

    int entityLen = semi - p - 1;  // Length between & and ;
    const char* entity = (const char*)(p + 1);

    *outPos = semi;  // Skip past ';'

    // Named entities
    if (entityLen == 3 && strncmp(entity, "amp", 3) == 0) return '&';
    if (entityLen == 2 && strncmp(entity, "lt", 2) == 0) return '<';
    if (entityLen == 2 && strncmp(entity, "gt", 2) == 0) return '>';
    if (entityLen == 4 && strncmp(entity, "quot", 4) == 0) return '"';
    if (entityLen == 4 && strncmp(entity, "apos", 4) == 0) return '\'';
    if (entityLen == 4 && strncmp(entity, "nbsp", 4) == 0) return ' ';
    if (entityLen == 5 && strncmp(entity, "mdash", 5) == 0) return '-';
    if (entityLen == 5 && strncmp(entity, "ndash", 5) == 0) return '-';
    if (entityLen == 6 && strncmp(entity, "hellip", 6) == 0) return '.';
    if (entityLen == 5 && strncmp(entity, "lsquo", 5) == 0) return '\'';
    if (entityLen == 5 && strncmp(entity, "rsquo", 5) == 0) return '\'';
    if (entityLen == 5 && strncmp(entity, "ldquo", 5) == 0) return '"';
    if (entityLen == 5 && strncmp(entity, "rdquo", 5) == 0) return '"';

    // Common accented character entities → CP437 bytes for built-in font
    if (entityLen == 6 && strncmp(entity, "eacute", 6) == 0) return (char)0x82;  // é
    if (entityLen == 6 && strncmp(entity, "egrave", 6) == 0) return (char)0x8A;  // è
    if (entityLen == 5 && strncmp(entity, "ecirc", 5) == 0) return (char)0x88;   // ê
    if (entityLen == 4 && strncmp(entity, "euml", 4) == 0) return (char)0x89;    // ë
    if (entityLen == 6 && strncmp(entity, "agrave", 6) == 0) return (char)0x85;  // à
    if (entityLen == 6 && strncmp(entity, "aacute", 6) == 0) return (char)0xA0;  // á
    if (entityLen == 5 && strncmp(entity, "acirc", 5) == 0) return (char)0x83;   // â
    if (entityLen == 4 && strncmp(entity, "auml", 4) == 0) return (char)0x84;    // ä
    if (entityLen == 6 && strncmp(entity, "ccedil", 6) == 0) return (char)0x87;  // ç
    if (entityLen == 6 && strncmp(entity, "iacute", 6) == 0) return (char)0xA1;  // í
    if (entityLen == 5 && strncmp(entity, "icirc", 5) == 0) return (char)0x8C;   // î
    if (entityLen == 4 && strncmp(entity, "iuml", 4) == 0) return (char)0x8B;    // ï
    if (entityLen == 6 && strncmp(entity, "igrave", 6) == 0) return (char)0x8D;  // ì
    if (entityLen == 6 && strncmp(entity, "oacute", 6) == 0) return (char)0xA2;  // ó
    if (entityLen == 5 && strncmp(entity, "ocirc", 5) == 0) return (char)0x93;   // ô
    if (entityLen == 4 && strncmp(entity, "ouml", 4) == 0) return (char)0x94;    // ö
    if (entityLen == 6 && strncmp(entity, "ograve", 6) == 0) return (char)0x95;  // ò
    if (entityLen == 6 && strncmp(entity, "uacute", 6) == 0) return (char)0xA3;  // ú
    if (entityLen == 5 && strncmp(entity, "ucirc", 5) == 0) return (char)0x96;   // û
    if (entityLen == 4 && strncmp(entity, "uuml", 4) == 0) return (char)0x81;    // ü
    if (entityLen == 6 && strncmp(entity, "ugrave", 6) == 0) return (char)0x97;  // ù
    if (entityLen == 6 && strncmp(entity, "ntilde", 6) == 0) return (char)0xA4;  // ñ
    if (entityLen == 6 && strncmp(entity, "Eacute", 6) == 0) return (char)0x90;  // É
    if (entityLen == 6 && strncmp(entity, "Ccedil", 6) == 0) return (char)0x80;  // Ç
    if (entityLen == 6 && strncmp(entity, "Ntilde", 6) == 0) return (char)0xA5;  // Ñ
    if (entityLen == 4 && strncmp(entity, "Auml", 4) == 0) return (char)0x8E;    // Ä
    if (entityLen == 4 && strncmp(entity, "Ouml", 4) == 0) return (char)0x99;    // Ö
    if (entityLen == 4 && strncmp(entity, "Uuml", 4) == 0) return (char)0x9A;    // Ü
    if (entityLen == 5 && strncmp(entity, "szlig", 5) == 0) return (char)0xE1;   // ß

    // Numeric entities: &#NNN; or &#xHH;
    if (entityLen >= 2 && entity[0] == '#') {
      int codepoint = 0;
      if (entity[1] == 'x' || entity[1] == 'X') {
        // Hex
        for (int i = 2; i < entityLen; i++) {
          char ch = entity[i];
          if (ch >= '0' && ch <= '9') codepoint = codepoint * 16 + (ch - '0');
          else if (ch >= 'a' && ch <= 'f') codepoint = codepoint * 16 + (ch - 'a' + 10);
          else if (ch >= 'A' && ch <= 'F') codepoint = codepoint * 16 + (ch - 'A' + 10);
        }
      } else {
        // Decimal
        for (int i = 1; i < entityLen; i++) {
          char ch = entity[i];
          if (ch >= '0' && ch <= '9') codepoint = codepoint * 10 + (ch - '0');
        }
      }
      // Map to displayable character (best effort)
      if (codepoint >= 32 && codepoint < 127) return (char)codepoint;
      if (codepoint == 160) return ' ';   // non-breaking space
      // Try CP437 mapping for accented characters.
      // The byte value will be passed through to the built-in font.
      uint8_t cp437 = unicodeToCP437(codepoint);
      if (cp437) return (char)cp437;
      // Unknown codepoint > 127: skip it
      return ' ';
    }

    // Unknown entity - output as space
    return ' ';
  }

  // ----------------------------------------------------------
  // Find a tag in XML data (case-sensitive, e.g., "<manifest").
  // Returns pointer to '<' of found tag, or nullptr.
  // ----------------------------------------------------------
  static const char* _findTag(const char* data, int dataLen, const char* tag) {
    int tagLen = strlen(tag);
    const char* end = data + dataLen - tagLen;
    for (const char* p = data; p <= end; p++) {
      if (memcmp(p, tag, tagLen) == 0) return p;
    }
    return nullptr;
  }

  // ----------------------------------------------------------
  // Find a tag case-insensitively (for <body>, <BODY>, etc.).
  // ----------------------------------------------------------
  static const char* _findTagCI(const char* data, int dataLen, const char* tag) {
    int tagLen = strlen(tag);
    const char* end = data + dataLen - tagLen;
    for (const char* p = data; p <= end; p++) {
      if (strncasecmp(p, tag, tagLen) == 0) return p;
    }
    return nullptr;
  }

  // ----------------------------------------------------------
  // Extract an attribute value from a region of XML.
  // Scans for attr="value" and copies value to outBuf.
  // ----------------------------------------------------------
  static bool _extractAttribute(const char* data, int dataLen,
                                 const char* attrName, char* outBuf, int outBufSize) {
    int nameLen = strlen(attrName);
    const char* end = data + dataLen;
    for (const char* p = data; p < end - nameLen - 2; p++) {
      if (strncmp(p, attrName, nameLen) == 0 && p[nameLen] == '=') {
        p += nameLen + 1;
        char quote = *p;
        if (quote != '"' && quote != '\'') continue;
        p++;
        const char* valEnd = (const char*)memchr(p, quote, end - p);
        if (!valEnd) continue;
        int valLen = valEnd - p;
        if (valLen >= outBufSize) valLen = outBufSize - 1;
        memcpy(outBuf, p, valLen);
        outBuf[valLen] = '\0';
        return true;
      }
    }
    return false;
  }

  // ----------------------------------------------------------
  // Extract an attribute value from within a single tag string.
  // (More targeted version for parsing <item id="x" href="y"/>)
  // ----------------------------------------------------------
  static bool _extractAttributeFromTag(const char* tag, int tagLen,
                                        const char* attrName,
                                        char* outBuf, int outBufSize) {
    return _extractAttribute(tag, tagLen, attrName, outBuf, outBufSize);
  }

  // ----------------------------------------------------------
  // Extract text content between <tagName>...</tagName>.
  // Works for simple cases like <dc:title>The Iliad</dc:title>.
  // ----------------------------------------------------------
  static bool _extractTagContent(const char* data, int dataLen,
                                  const char* tagName, char* outBuf, int outBufSize) {
    // Build open tag pattern: "<dc:title" (without >)
    char openTag[64];
    snprintf(openTag, sizeof(openTag), "<%s", tagName);

    const char* start = _findTag(data, dataLen, openTag);
    if (!start) return false;

    // Find the > that closes the opening tag
    const char* end = data + dataLen;
    const char* contentStart = (const char*)memchr(start, '>', end - start);
    if (!contentStart) return false;
    contentStart++;  // Skip past '>'

    // Find closing tag
    char closeTag[64];
    snprintf(closeTag, sizeof(closeTag), "</%s>", tagName);
    const char* contentEnd = _findTag(contentStart, end - contentStart, closeTag);
    if (!contentEnd) return false;

    int len = contentEnd - contentStart;
    if (len >= outBufSize) len = outBufSize - 1;
    memcpy(outBuf, contentStart, len);
    outBuf[len] = '\0';
    return true;
  }

  // ----------------------------------------------------------
  // Get directory portion of a path.
  // "OEBPS/content.opf" -> "OEBPS/"
  // "content.opf" -> ""
  // ----------------------------------------------------------
  static void _getDirectory(const char* path, char* dirBuf, int dirBufSize) {
    const char* lastSlash = strrchr(path, '/');
    if (lastSlash) {
      int len = lastSlash - path + 1;  // Include trailing /
      if (len >= dirBufSize) len = dirBufSize - 1;
      memcpy(dirBuf, path, len);
      dirBuf[len] = '\0';
    } else {
      dirBuf[0] = '\0';
    }
  }

  // ----------------------------------------------------------
  // Free the chapter paths array allocated by _parseOpf().
  // ----------------------------------------------------------
  static void _freeChapterPaths(char** paths, int count) {
    if (paths) {
      for (int i = 0; i < count; i++) {
        if (paths[i]) free(paths[i]);
      }
      free(paths);
    }
  }
};