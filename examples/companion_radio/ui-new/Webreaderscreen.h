#pragma once

// =============================================================================
// WebReaderScreen.h - Minimal Web Reader ("Reader Mode") for T-Deck Pro
//
// A Lynx-like web page reader that fetches URLs over WiFi, strips HTML to
// readable text, extracts links as numbered references, and paginates
// content for the e-ink display with keyboard navigation.
//
// Requires WiFi capability - wrap includes with appropriate guards.
// Shortcut key: B (Browser) from home screen.
//
// Network backends:
//   - WiFi (default): Uses ESP32 WiFi STA mode. Credentials saved to SD.
//   - 4G/PPP (future): When PPP is established via A7682E modem, the same
//     HTTPClient code works transparently over cellular. To enable this,
//     establish PPP before calling fetchPage() and the ESP networking
//     stack will route through the modem automatically.
//
// Modes:
//   WIFI_SETUP   - Connect to a WiFi network (scan + password entry)
//   HOME         - URL bar, bookmarks, history
//   FETCHING     - Loading indicator while downloading
//   READING      - Paginated text view with numbered [links]
//   LINK_SELECT  - Pick a link by number to follow
// =============================================================================

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include "variant.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/platform.h>
#include <esp_heap_caps.h>
#include <SD.h>
#include <vector>
#include "Utf8CP437.h"

// Forward declarations
class UITask;

// ============================================================================
// PSRAM allocator for mbedTLS
//
// ESP32-S3 internal RAM has only ~30KB largest contiguous block after WiFi
// init, but TLS handshake needs ~32-48KB for I/O buffers. Redirect mbedtls
// allocations to PSRAM (which has plenty of contiguous space) so HTTPS works.
// ============================================================================
static void* _webreader_psram_calloc(size_t num, size_t size) {
  void* ptr = heap_caps_calloc(num, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!ptr) ptr = calloc(num, size);  // Fallback to internal if PSRAM fails
  return ptr;
}

static void _webreader_psram_free(void* ptr) {
  free(ptr);  // Works for both PSRAM and internal allocations
}

static bool _webreader_tls_psram_set = false;

static void ensureTlsUsesPsram() {
#if defined(MBEDTLS_PLATFORM_MEMORY) || defined(CONFIG_MBEDTLS_PLATFORM_MEMORY)
  if (!_webreader_tls_psram_set) {
    mbedtls_platform_set_calloc_free(_webreader_psram_calloc, _webreader_psram_free);
    _webreader_tls_psram_set = true;
    Serial.println("WebReader: mbedTLS allocator redirected to PSRAM");
  }
#else
  if (!_webreader_tls_psram_set) {
    Serial.println("WebReader: WARNING - mbedTLS PSRAM redirect not available");
    _webreader_tls_psram_set = true;
  }
#endif
}

// ============================================================================
// Configuration
// ============================================================================
#define WEB_CACHE_DIR       "/web"
#define WEB_BOOKMARKS_FILE  "/web/bookmarks.txt"
#define WEB_HISTORY_FILE    "/web/history.txt"

// IRC configuration
#define IRC_CONFIG_FILE     "/web/irc.cfg"
#define IRC_MAX_MESSAGES    64       // Circular buffer size
#define IRC_MAX_MSG_LEN     200      // Max display length per message
#define IRC_MAX_NICK_LEN    16
#define IRC_MAX_CHANNEL_LEN 64
#define IRC_MAX_HOST_LEN    128
#define IRC_LINE_BUF_SIZE   512      // Raw IRC protocol line buffer
#define IRC_COMPOSE_MAX     180      // Max compose message length
#define IRC_RECONNECT_MS    10000    // Auto-reconnect delay
#define IRC_PING_TIMEOUT_MS 300000   // 5 min no data = connection dead

struct IRCMessage {
  char nick[IRC_MAX_NICK_LEN];
  char text[IRC_MAX_MSG_LEN];
  bool isSystem;  // true for join/part/server notices
};
#define WEB_MAX_PAGE_SIZE   196608  // Max HTML download size (192KB)
#define WEB_MAX_TEXT_SIZE   98304   // Max extracted text size (96KB)
#define WEB_MAX_LINKS       512     // Max links per page
#define WEB_MAX_URL_LEN     256
#define WEB_MAX_BOOKMARKS   20
#define WEB_MAX_HISTORY     30
#define WEB_MAX_SSIDS       10
#define WEB_WIFI_PASS_LEN   64
#define WEB_USER_AGENT      "Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Mobile Safari/537.36"

// ============================================================================
// Link structure - stores extracted hyperlinks
// ============================================================================
struct WebLink {
  char url[WEB_MAX_URL_LEN];
  char text[48];   // Display text for the link (truncated)
};

// ============================================================================
// Form structures - stores parsed HTML forms for user interaction
// ============================================================================
#define WEB_MAX_FORMS        4
#define WEB_MAX_FORM_FIELDS  16
#define WEB_MAX_FIELD_VALUE  128

struct WebFormField {
  char name[64];                    // name= attribute
  char value[WEB_MAX_FIELD_VALUE];  // Current/default value
  char label[48];                   // Display label (from <label> or placeholder)
  char type;                        // 't'=text, 'p'=password, 'h'=hidden, 's'=submit, 'c'=checkbox
};

struct WebForm {
  char action[WEB_MAX_URL_LEN];    // Form action URL
  bool isPost;                      // true=POST, false=GET
  WebFormField fields[WEB_MAX_FORM_FIELDS];
  int fieldCount;
  int textFieldCount;               // Visible (non-hidden) field count
  int formMarker;                   // Index in text where form marker was placed
};

// ============================================================================
// HTML Parser - minimal tag-stripping reader-mode extractor
// ============================================================================

// Tags whose content should be completely removed (not just the tag itself)
// Note: form/input/button/label are NOT skipped — they're parsed for form support.
// header is NOT skipped — it contains login/navigation links on most sites.
// nav IS skipped — its links are redundant with header and add clutter.
static const char* HTML_SKIP_TAGS[] = {
  "script", "style", "nav", "footer", "aside",
  "iframe", "noscript", "svg", "select", "textarea", nullptr
};

// Tags that produce a paragraph break
static const char* HTML_BLOCK_TAGS[] = {
  "p", "div", "br", "h1", "h2", "h3", "h4", "h5", "h6",
  "tr", "blockquote", "article", "section", "figcaption",
  "dt", "dd", nullptr
};

inline bool tagNameEquals(const char* tag, int tagLen, const char* name) {
  int nameLen = strlen(name);
  if (tagLen != nameLen) return false;
  for (int i = 0; i < nameLen; i++) {
    char c = tag[i];
    if (c >= 'A' && c <= 'Z') c += 32; // tolower
    if (c != name[i]) return false;
  }
  return true;
}

inline bool isSkipTag(const char* tag, int tagLen) {
  for (int i = 0; HTML_SKIP_TAGS[i]; i++) {
    if (tagNameEquals(tag, tagLen, HTML_SKIP_TAGS[i])) return true;
  }
  return false;
}

inline bool isBlockTag(const char* tag, int tagLen) {
  for (int i = 0; HTML_BLOCK_TAGS[i]; i++) {
    if (tagNameEquals(tag, tagLen, HTML_BLOCK_TAGS[i])) return true;
  }
  return false;
}

// Decode common HTML entities: &amp; &lt; &gt; &quot; &apos; &nbsp; &#NNN; &#xHH;
inline int decodeHtmlEntity(const char* src, int srcLen, int pos, char* outChar) {
  if (pos >= srcLen || src[pos] != '&') return 0;

  // Find the semicolon
  int end = pos + 1;
  int maxSearch = pos + 10; // entities are short
  if (maxSearch > srcLen) maxSearch = srcLen;
  while (end < maxSearch && src[end] != ';' && src[end] != '&' && src[end] != '<') end++;
  if (end >= maxSearch || src[end] != ';') return 0;

  int entLen = end - pos - 1; // length between & and ;
  const char* ent = src + pos + 1;

  if (entLen == 3 && memcmp(ent, "amp", 3) == 0) { *outChar = '&'; return end - pos + 1; }
  if (entLen == 2 && memcmp(ent, "lt", 2) == 0) { *outChar = '<'; return end - pos + 1; }
  if (entLen == 2 && memcmp(ent, "gt", 2) == 0) { *outChar = '>'; return end - pos + 1; }
  if (entLen == 4 && memcmp(ent, "quot", 4) == 0) { *outChar = '"'; return end - pos + 1; }
  if (entLen == 4 && memcmp(ent, "apos", 4) == 0) { *outChar = '\''; return end - pos + 1; }
  if (entLen == 4 && memcmp(ent, "nbsp", 4) == 0) { *outChar = ' '; return end - pos + 1; }

  // Numeric: &#NNN; or &#xHH;
  if (entLen >= 2 && ent[0] == '#') {
    uint32_t cp = 0;
    if (ent[1] == 'x' || ent[1] == 'X') {
      for (int i = 2; i < entLen; i++) {
        char c = ent[i];
        if (c >= '0' && c <= '9') cp = cp * 16 + (c - '0');
        else if (c >= 'a' && c <= 'f') cp = cp * 16 + (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') cp = cp * 16 + (c - 'A' + 10);
        else break;
      }
    } else {
      for (int i = 1; i < entLen; i++) {
        if (ent[i] >= '0' && ent[i] <= '9') cp = cp * 10 + (ent[i] - '0');
        else break;
      }
    }
    if (cp < 128) {
      *outChar = (char)cp;
    } else {
      // Try CP437 mapping for common chars
      uint8_t glyph = unicodeToCP437(cp);
      *outChar = glyph ? (char)glyph : '?';
    }
    return end - pos + 1;
  }

  return 0; // Unknown entity
}

// Extract the tag name from inside a < > bracket.
// Returns length of tag name, and sets isClosing if it starts with /
inline int extractTagName(const char* inside, int insideLen, bool& isClosing) {
  int i = 0;
  isClosing = false;
  while (i < insideLen && (inside[i] == ' ' || inside[i] == '\t')) i++;
  if (i < insideLen && inside[i] == '/') { isClosing = true; i++; }
  int start = i;
  while (i < insideLen && inside[i] != ' ' && inside[i] != '/' &&
         inside[i] != '>' && inside[i] != '\t' && inside[i] != '\n') i++;
  return i - start; // tagName starts at inside+start, length is return value
}

// Extract href attribute value from inside an <a ...> tag
inline bool extractHref(const char* tagContent, int tagLen, char* hrefOut, int hrefMax) {
  // Search for href= (case insensitive)
  for (int i = 0; i < tagLen - 5; i++) {
    char c0 = tagContent[i]; if (c0 >= 'A' && c0 <= 'Z') c0 += 32;
    char c1 = tagContent[i+1]; if (c1 >= 'A' && c1 <= 'Z') c1 += 32;
    char c2 = tagContent[i+2]; if (c2 >= 'A' && c2 <= 'Z') c2 += 32;
    char c3 = tagContent[i+3]; if (c3 >= 'A' && c3 <= 'Z') c3 += 32;

    if (c0 == 'h' && c1 == 'r' && c2 == 'e' && c3 == 'f') {
      int j = i + 4;
      while (j < tagLen && tagContent[j] == ' ') j++;
      if (j < tagLen && tagContent[j] == '=') {
        j++;
        while (j < tagLen && tagContent[j] == ' ') j++;
        char quote = 0;
        if (j < tagLen && (tagContent[j] == '"' || tagContent[j] == '\'')) {
          quote = tagContent[j]; j++;
        }
        int start = j;
        if (quote) {
          while (j < tagLen && tagContent[j] != quote) j++;
        } else {
          while (j < tagLen && tagContent[j] != ' ' && tagContent[j] != '>') j++;
        }
        int len = j - start;
        if (len >= hrefMax) len = hrefMax - 1;
        memcpy(hrefOut, tagContent + start, len);
        hrefOut[len] = '\0';
        return len > 0;
      }
    }
  }
  return false;
}

// Encode spaces in URL as %20 (in-place, buffer must have room)
inline void encodeUrlSpaces(char* url, int maxLen) {
  int len = strlen(url);
  // Count spaces to check if result fits
  int spaces = 0;
  for (int i = 0; i < len; i++) {
    if (url[i] == ' ') spaces++;
  }
  int newLen = len + spaces * 2;  // Each space becomes 3 chars (%20) instead of 1
  if (newLen >= maxLen) return;    // Won't fit, leave as-is

  // Work backwards to encode in-place
  url[newLen] = '\0';
  int dst = newLen - 1;
  for (int src = len - 1; src >= 0; src--) {
    if (url[src] == ' ') {
      url[dst--] = '0';
      url[dst--] = '2';
      url[dst--] = '%';
    } else {
      url[dst--] = url[src];
    }
  }
}

// Resolve a relative URL against a base URL
inline void resolveUrl(const char* base, const char* relative, char* out, int outMax) {
  if (!relative || !relative[0]) {
    strncpy(out, base, outMax - 1);
    out[outMax - 1] = '\0';
    return;
  }

  // Already absolute
  if (strncmp(relative, "http://", 7) == 0 || strncmp(relative, "https://", 8) == 0) {
    strncpy(out, relative, outMax - 1);
    out[outMax - 1] = '\0';
    return;
  }

  // Protocol-relative //example.com/...
  if (relative[0] == '/' && relative[1] == '/') {
    snprintf(out, outMax, "https:%s", relative);
    return;
  }

  // Find scheme + host from base
  const char* schemeEnd = strstr(base, "://");
  if (!schemeEnd) {
    strncpy(out, relative, outMax - 1);
    out[outMax - 1] = '\0';
    return;
  }
  const char* hostStart = schemeEnd + 3;
  const char* pathStart = strchr(hostStart, '/');

  if (relative[0] == '/') {
    // Absolute path
    int hostLen = pathStart ? (pathStart - base) : strlen(base);
    snprintf(out, outMax, "%.*s%s", hostLen, base, relative);
  } else {
    // Relative path - append to base directory
    if (pathStart) {
      const char* lastSlash = strrchr(pathStart, '/');
      int baseLen = lastSlash ? (lastSlash - base + 1) : strlen(base);
      snprintf(out, outMax, "%.*s%s", baseLen, base, relative);
    } else {
      snprintf(out, outMax, "%s/%s", base, relative);
    }
  }
}


// Extract a named attribute value from inside a tag
inline bool extractAttr(const char* tag, int tagLen, const char* attrName,
                        char* out, int outMax) {
  int nameLen = strlen(attrName);
  for (int i = 0; i < tagLen - nameLen; i++) {
    bool match = true;
    for (int j = 0; j < nameLen && match; j++) {
      char c = tag[i + j];
      if (c >= 'A' && c <= 'Z') c += 32;
      if (c != attrName[j]) match = false;
    }
    if (!match) continue;
    int j = i + nameLen;
    while (j < tagLen && tag[j] == ' ') j++;
    if (j >= tagLen || tag[j] != '=') continue;
    j++;
    while (j < tagLen && tag[j] == ' ') j++;
    char quote = 0;
    if (j < tagLen && (tag[j] == '"' || tag[j] == '\'')) { quote = tag[j]; j++; }
    int start = j;
    if (quote) { while (j < tagLen && tag[j] != quote) j++; }
    else { while (j < tagLen && tag[j] != ' ' && tag[j] != '>' && tag[j] != '/') j++; }
    int len = j - start;
    if (len >= outMax) len = outMax - 1;
    memcpy(out, tag + start, len);
    out[len] = '\0';
    return len > 0;
  }
  return false;
}

// ============================================================================
// Main HTML-to-text parser
//
// Strips HTML tags, extracts text content, collects links and forms.
// Outputs clean text with paragraph breaks as double newlines.
// Links are inserted as [N] markers in the text flow.
// Forms are inserted as {FN} markers with visible fields.
// ============================================================================

struct ParseResult {
  int textLen;
  int linkCount;
  int formCount;
};

inline ParseResult parseHtml(const char* html, int htmlLen,
                             char* textOut, int textMax,
                             WebLink* links, int maxLinks,
                             WebForm* forms, int maxForms,
                             const char* baseUrl) {
  ParseResult result = {0, 0, 0};
  int ti = 0;       // text output index
  int hi = 0;       // html input index
  int skipDepth = 0; // depth inside skip tags
  bool inTag = false;
  bool inAnchor = false;
  int anchorTextStart = 0;
  char currentHref[WEB_MAX_URL_LEN] = {0};
  bool lastWasBreak = true; // Track if we just emitted a paragraph break (avoid doubles)
  bool lastWasSpace = false;

  // Form parsing state
  bool inForm = false;
  int currentForm = -1;
  char pendingLabel[48] = {0};
  bool inLabel = false;
  int labelTextStart = 0;

  // Find <body> tag to skip <head> section
  for (int i = 0; i < htmlLen - 6; i++) {
    char c = html[i];
    if (c == '<') {
      // Check for <body
      char b1 = html[i+1]; if (b1 >= 'A' && b1 <= 'Z') b1 += 32;
      char b2 = html[i+2]; if (b2 >= 'A' && b2 <= 'Z') b2 += 32;
      char b3 = html[i+3]; if (b3 >= 'A' && b3 <= 'Z') b3 += 32;
      char b4 = html[i+4]; if (b4 >= 'A' && b4 <= 'Z') b4 += 32;
      if (b1 == 'b' && b2 == 'o' && b3 == 'd' && b4 == 'y') {
        // Skip to after the >
        while (i < htmlLen && html[i] != '>') i++;
        hi = i + 1;
        break;
      }
    }
  }

  while (hi < htmlLen && ti < textMax - 4) {
    char c = html[hi];

    if (c == '<') {
      // Start of a tag
      int tagStart = hi + 1;
      int tagEnd = tagStart;
      // Find closing >
      while (tagEnd < htmlLen && html[tagEnd] != '>') tagEnd++;
      if (tagEnd >= htmlLen) break;

      int insideLen = tagEnd - tagStart;
      const char* inside = html + tagStart;

      // Extract tag name
      bool isClosing = false;
      int nameStart = 0;
      while (nameStart < insideLen && (inside[nameStart] == ' ' || inside[nameStart] == '\t'))
        nameStart++;
      if (nameStart < insideLen && inside[nameStart] == '/') {
        isClosing = true;
        nameStart++;
      }
      int nameEnd = nameStart;
      while (nameEnd < insideLen && inside[nameEnd] != ' ' && inside[nameEnd] != '/' &&
             inside[nameEnd] != '>' && inside[nameEnd] != '\t' && inside[nameEnd] != '\n')
        nameEnd++;

      const char* tagName = inside + nameStart;
      int tagNameLen = nameEnd - nameStart;

      // Check for skip tags (script, style, nav, etc.)
      if (isSkipTag(tagName, tagNameLen)) {
        if (isClosing) {
          if (skipDepth > 0) skipDepth--;
        } else {
          // Check if self-closing
          bool selfClose = (insideLen > 0 && inside[insideLen - 1] == '/');
          if (!selfClose) skipDepth++;
        }
        hi = tagEnd + 1;
        continue;
      }

      if (skipDepth > 0) {
        hi = tagEnd + 1;
        continue;
      }

      // Handle block tags - emit paragraph break
      if (isBlockTag(tagName, tagNameLen)) {
        if (!lastWasBreak && ti > 0) {
          textOut[ti++] = '\n';
          if (ti < textMax - 2) textOut[ti++] = '\n';
          lastWasBreak = true;
          lastWasSpace = false;
        }
      }

      // Handle <h1>-<h6> opening: add a visual marker
      if (!isClosing && tagNameLen == 2 && tagName[0] == 'h' &&
          tagName[1] >= '1' && tagName[1] <= '6') {
        // Emit section header marker
        if (ti < textMax - 6) {
          if (!lastWasBreak) {
            textOut[ti++] = '\n';
            textOut[ti++] = '\n';
          }
          textOut[ti++] = '=';
          textOut[ti++] = '=';
          textOut[ti++] = ' ';
          lastWasBreak = false;
          lastWasSpace = false;
        }
      }

      // Handle closing </h1>-</h6>: add trailing marker
      if (isClosing && tagNameLen == 2 && tagName[0] == 'h' &&
          tagName[1] >= '1' && tagName[1] <= '6') {
        if (ti < textMax - 6) {
          textOut[ti++] = ' ';
          textOut[ti++] = '=';
          textOut[ti++] = '=';
          textOut[ti++] = '\n';
          textOut[ti++] = '\n';
          lastWasBreak = true;
          lastWasSpace = false;
        }
      }

      // Handle <a href="..."> - collect link
      if (!isClosing && tagNameLen == 1 && (tagName[0] == 'a' || tagName[0] == 'A')) {
        char href[WEB_MAX_URL_LEN] = {0};
        if (extractHref(inside, insideLen, href, WEB_MAX_URL_LEN)) {
          // Skip javascript:, mailto:, and # fragment-only links
          if (strncmp(href, "javascript:", 11) != 0 &&
              strncmp(href, "mailto:", 7) != 0 &&
              href[0] != '#') {
            resolveUrl(baseUrl, href, currentHref, WEB_MAX_URL_LEN);
            inAnchor = true;
            anchorTextStart = ti;
          }
        }
      }

      // Handle </a> - finalize link
      if (isClosing && tagNameLen == 1 && (tagName[0] == 'a' || tagName[0] == 'A')) {
        if (inAnchor && currentHref[0] && result.linkCount < maxLinks) {
          WebLink& link = links[result.linkCount];
          strncpy(link.url, currentHref, WEB_MAX_URL_LEN - 1);
          link.url[WEB_MAX_URL_LEN - 1] = '\0';

          // Extract link display text from what was accumulated
          int linkTextLen = ti - anchorTextStart;
          if (linkTextLen > (int)sizeof(link.text) - 1)
            linkTextLen = sizeof(link.text) - 1;
          if (linkTextLen > 0) {
            memcpy(link.text, textOut + anchorTextStart, linkTextLen);
          }
          link.text[linkTextLen] = '\0';

          // Append link number marker: [N]
          result.linkCount++;
          if (ti < textMax - 8) {
            int n = result.linkCount;
            textOut[ti++] = '[';
            if (n >= 100) textOut[ti++] = '0' + (n / 100);
            if (n >= 10) textOut[ti++] = '0' + ((n / 10) % 10);
            textOut[ti++] = '0' + (n % 10);
            textOut[ti++] = ']';
            lastWasSpace = false;
            lastWasBreak = false;
          }
        }
        inAnchor = false;
        currentHref[0] = '\0';
      }

      // Handle <li> - single newline + bullet marker
      if (!isClosing && tagNameLen == 2 && tagName[0] == 'l' && tagName[1] == 'i') {
        if (ti < textMax - 5) {
          if (!lastWasBreak && ti > 0) {
            textOut[ti++] = '\n';
            lastWasBreak = true;
          }
          textOut[ti++] = ' ';
          textOut[ti++] = '*';
          textOut[ti++] = ' ';
          lastWasSpace = false;
          lastWasBreak = false;
        }
      }

      // ---- Form handling ----

      // <form action="..." method="...">
      if (!isClosing && tagNameLen == 4 &&
          tagName[0] == 'f' && tagName[1] == 'o' && tagName[2] == 'r' && tagName[3] == 'm') {
        if (result.formCount < maxForms) {
          currentForm = result.formCount;
          WebForm& f = forms[currentForm];
          memset(&f, 0, sizeof(WebForm));
          char actionBuf[WEB_MAX_URL_LEN] = {0};
          extractAttr(inside, insideLen, "action", actionBuf, WEB_MAX_URL_LEN);
          if (actionBuf[0]) {
            resolveUrl(baseUrl, actionBuf, f.action, WEB_MAX_URL_LEN);
          } else {
            strncpy(f.action, baseUrl, WEB_MAX_URL_LEN - 1);
          }
          char methodBuf[8] = {0};
          extractAttr(inside, insideLen, "method", methodBuf, sizeof(methodBuf));
          // tolower
          for (int m = 0; methodBuf[m]; m++) {
            if (methodBuf[m] >= 'A' && methodBuf[m] <= 'Z') methodBuf[m] += 32;
          }
          f.isPost = (strcmp(methodBuf, "post") == 0);
          inForm = true;
          // Emit form marker in text
          if (!lastWasBreak && ti > 0) { textOut[ti++] = '\n'; textOut[ti++] = '\n'; }
          f.formMarker = ti;
          if (ti < textMax - 12) {
            textOut[ti++] = '-'; textOut[ti++] = '-';
            textOut[ti++] = ' '; textOut[ti++] = 'F';
            textOut[ti++] = 'o'; textOut[ti++] = 'r';
            textOut[ti++] = 'm'; textOut[ti++] = ' ';
            textOut[ti++] = '{'; textOut[ti++] = 'F';
            textOut[ti++] = '0' + (result.formCount + 1);
            textOut[ti++] = '}';
            textOut[ti++] = ' '; textOut[ti++] = '-'; textOut[ti++] = '-';
            textOut[ti++] = '\n';
          }
          lastWasBreak = false; lastWasSpace = false;
        }
      }

      // </form>
      if (isClosing && tagNameLen == 4 &&
          tagName[0] == 'f' && tagName[1] == 'o' && tagName[2] == 'r' && tagName[3] == 'm') {
        if (inForm && currentForm >= 0) {
          // Emit submit hint if form has visible fields
          WebForm& f = forms[currentForm];
          if (f.textFieldCount > 0 && ti < textMax - 20) {
            textOut[ti++] = '\n';
            textOut[ti++] = '['; textOut[ti++] = 'f';
            textOut[ti++] = ':'; textOut[ti++] = ' ';
            textOut[ti++] = 'F'; textOut[ti++] = 'i';
            textOut[ti++] = 'l'; textOut[ti++] = 'l';
            textOut[ti++] = ' '; textOut[ti++] = 'f';
            textOut[ti++] = 'o'; textOut[ti++] = 'r';
            textOut[ti++] = 'm'; textOut[ti++] = ']';
            textOut[ti++] = '\n'; textOut[ti++] = '\n';
          }
          result.formCount++;
          lastWasBreak = true;
        }
        inForm = false;
        currentForm = -1;
      }

      // <input type="..." name="..." value="...">
      if (!isClosing && tagNameLen == 5 &&
          tagName[0] == 'i' && tagName[1] == 'n' && tagName[2] == 'p' &&
          tagName[3] == 'u' && tagName[4] == 't') {
        if (inForm && currentForm >= 0) {
          WebForm& f = forms[currentForm];
          if (f.fieldCount < WEB_MAX_FORM_FIELDS) {
            WebFormField& fld = f.fields[f.fieldCount];
            memset(&fld, 0, sizeof(WebFormField));
            char typeBuf[16] = "text";
            extractAttr(inside, insideLen, "type", typeBuf, sizeof(typeBuf));
            for (int m = 0; typeBuf[m]; m++) {
              if (typeBuf[m] >= 'A' && typeBuf[m] <= 'Z') typeBuf[m] += 32;
            }
            extractAttr(inside, insideLen, "name", fld.name, sizeof(fld.name));
            extractAttr(inside, insideLen, "value", fld.value, sizeof(fld.value));

            if (strcmp(typeBuf, "hidden") == 0) {
              fld.type = 'h';
              // type 'h' marks it as hidden — no display needed
            } else if (strcmp(typeBuf, "password") == 0) {
              fld.type = 'p';
              // Use pending label or placeholder
              if (pendingLabel[0]) { strncpy(fld.label, pendingLabel, sizeof(fld.label)-1); pendingLabel[0] = 0; }
              else extractAttr(inside, insideLen, "placeholder", fld.label, sizeof(fld.label));
              if (!fld.label[0]) strncpy(fld.label, "Password", sizeof(fld.label)-1);
              f.textFieldCount++;
              // Emit field display
              if (ti < textMax - 40) {
                int w = snprintf(textOut + ti, textMax - ti, "%s: [****]\n", fld.label);
                if (w > 0) ti += w;
              }
              lastWasBreak = false; lastWasSpace = false;
            } else if (strcmp(typeBuf, "submit") == 0) {
              fld.type = 's';
              if (!fld.value[0]) strncpy(fld.value, "Submit", sizeof(fld.value)-1);
              strncpy(fld.label, fld.value, sizeof(fld.label)-1);
            } else if (strcmp(typeBuf, "checkbox") == 0) {
              fld.type = 'c';
              if (pendingLabel[0]) { strncpy(fld.label, pendingLabel, sizeof(fld.label)-1); pendingLabel[0] = 0; }
            } else {
              // text, email, search, etc — treat as text input
              fld.type = 't';
              if (pendingLabel[0]) { strncpy(fld.label, pendingLabel, sizeof(fld.label)-1); pendingLabel[0] = 0; }
              else extractAttr(inside, insideLen, "placeholder", fld.label, sizeof(fld.label));
              if (!fld.label[0]) {
                // Use name as fallback label
                strncpy(fld.label, fld.name, sizeof(fld.label)-1);
              }
              f.textFieldCount++;
              // Emit field display
              if (ti < textMax - 40) {
                int w = snprintf(textOut + ti, textMax - ti, "%s: [___]\n", fld.label);
                if (w > 0) ti += w;
              }
              lastWasBreak = false; lastWasSpace = false;
            }
            f.fieldCount++;
          }
        }
      }

      // <label> / </label> - capture text for next input
      if (tagNameLen == 5 &&
          tagName[0] == 'l' && tagName[1] == 'a' && tagName[2] == 'b' &&
          tagName[3] == 'e' && tagName[4] == 'l') {
        if (!isClosing) {
          inLabel = true;
          labelTextStart = ti;
        } else if (inLabel) {
          // Capture label text
          int labelLen = ti - labelTextStart;
          if (labelLen > (int)sizeof(pendingLabel) - 1)
            labelLen = sizeof(pendingLabel) - 1;
          if (labelLen > 0)
            memcpy(pendingLabel, textOut + labelTextStart, labelLen);
          pendingLabel[labelLen] = '\0';
          // Remove trailing colon/space
          while (labelLen > 0 && (pendingLabel[labelLen-1] == ':' || pendingLabel[labelLen-1] == ' '))
            pendingLabel[--labelLen] = '\0';
          // Rewind text output — label text is used for form field display only,
          // not shown in reading view (avoids duplicate: label text + field marker)
          ti = labelTextStart;
          lastWasBreak = (ti == 0 || textOut[ti-1] == '\n');
          lastWasSpace = (ti > 0 && textOut[ti-1] == ' ');
          inLabel = false;
        }
      }

      // <button> - render content inline (don't skip, as buttons may wrap links)
      // Form submit buttons are handled separately in form rendering.

      hi = tagEnd + 1;
      continue;
    }

    // Skip content inside skip tags
    if (skipDepth > 0) {
      hi++;
      continue;
    }

    // HTML entity
    if (c == '&') {
      char decoded;
      int consumed = decodeHtmlEntity(html, htmlLen, hi, &decoded);
      if (consumed > 0) {
        if (decoded == ' ') {
          if (!lastWasSpace && !lastWasBreak) {
            textOut[ti++] = ' ';
            lastWasSpace = true;
          }
        } else {
          textOut[ti++] = decoded;
          lastWasSpace = false;
          lastWasBreak = false;
        }
        hi += consumed;
        continue;
      }
    }

    // Whitespace collapsing
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
      if (!lastWasSpace && !lastWasBreak && ti > 0) {
        textOut[ti++] = ' ';
        lastWasSpace = true;
      }
      hi++;
      continue;
    }

    // Regular character
    textOut[ti++] = c;
    lastWasSpace = false;
    lastWasBreak = false;
    hi++;
  }

  textOut[ti] = '\0';
  result.textLen = ti;
  return result;
}


// ============================================================================
// Word Wrap - reuse same algorithm as TextReaderScreen
// (included from TextReaderScreen.h via findLineBreak, or duplicated here
//  for standalone compilation)
// ============================================================================
#ifndef WEBREADER_WORD_WRAP
#define WEBREADER_WORD_WRAP
struct WebWrapResult {
  int lineEnd;
  int nextStart;
};

inline WebWrapResult webFindLineBreak(const char* buffer, int bufLen,
                                       int lineStart, int maxChars) {
  WebWrapResult result;
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
      if ((uint8_t)c >= 0x80 && (uint8_t)c < 0xC0) continue;

      charCount++;
      if (c == ' ' || c == '\t') {
        if (inWord) {
          lastBreakPoint = i;
          inWord = false;
        }
      } else if (c == '-') {
        if (inWord) lastBreakPoint = i + 1;
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
#endif

// ============================================================================
// WebReaderScreen
// ============================================================================

class WebReaderScreen : public UIScreen {
public:
  enum Mode {
    WIFI_SETUP,    // Connect to WiFi
    HOME,          // URL entry + bookmarks
    FETCHING,      // Loading page
    READING,       // Viewing extracted text
    LINK_SELECT,   // Choosing a link number
    FORM_FILL,     // Filling in a form
    IRC_SETUP,     // IRC server/nick/channel configuration
    IRC_CHAT       // Live IRC chat view
  };

  enum WifiState {
    WIFI_IDLE,
    WIFI_SCANNING,
    WIFI_SCAN_DONE,
    WIFI_ENTERING_PASS,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_FAILED
  };

private:
  UITask* _task;
  Mode _mode;
  bool _initialized;
  DisplayDriver* _display;

  // Display layout (calculated once)
  int _charsPerLine;
  int _linesPerPage;
  int _lineHeight;
  int _footerHeight;

  // WiFi state
  WifiState _wifiState;
  String _ssidList[WEB_MAX_SSIDS];
  int _ssidCount;
  int _selectedSSID;
  char _wifiPass[WEB_WIFI_PASS_LEN];
  int _wifiPassLen;
  unsigned long _wifiTimeout;
  String _connectedSSID;

  // URL entry
  char _urlBuffer[WEB_MAX_URL_LEN];
  int _urlLen;
  int _urlCursor;  // Cursor position within URL

  // Page content
  char* _textBuffer;       // PSRAM allocated - extracted text
  int _textLen;
  WebLink* _links;         // PSRAM allocated - extracted links
  int _linkCount;
  char _pageTitle[64];     // Page title (from <title> tag)
  char _currentUrl[WEB_MAX_URL_LEN];
  std::vector<String> _backHistory;  // URL stack for back navigation

  // Pagination
  std::vector<int> _pageOffsets;  // Byte offset of each page start
  int _currentPage;
  int _totalPages;

  // Bookmarks & History
  std::vector<String> _bookmarks;
  std::vector<String> _history;
  int _homeSelected;  // Selected item in home view (0=URL bar, then bookmarks, then history)
  bool _urlEditing;   // True when URL bar is active for text entry

  // Link selection
  int _linkInput;     // Accumulated link number digits
  bool _linkInputActive;

  // Brief toast notification
  char _toastMsg[32];
  unsigned long _toastTime;  // millis() when toast was set

  // Forms
  WebForm _forms[WEB_MAX_FORMS];
  int _formCount;
  int _activeForm;      // Which form is being filled (-1 = none)
  int _activeField;     // Which field in the active form (index into visible fields)
  bool _formFieldEditing; // True when typing into a form field
  char _formEditBuf[WEB_MAX_FIELD_VALUE]; // Edit buffer for current field
  int _formEditLen;
  unsigned long _formLastCharAt; // millis() of last char typed (for brief password reveal)

  // Cookies (simple key=value store per domain)
  #define WEB_MAX_COOKIES 16
  struct Cookie {
    char domain[64];
    char name[64];
    char value[512]; // AO3 session cookies are 300+ chars of base64
  };
  Cookie _cookies[WEB_MAX_COOKIES];
  int _cookieCount;

  // Fetch state
  unsigned long _fetchStartTime;
  int _fetchProgress;    // Bytes received so far
  String _fetchError;

  // ---- IRC State ----
  WiFiClient* _ircClient;       // WiFiClient (plain) or WiFiClientSecure (TLS)
  bool _ircUseTLS;              // true when connected via TLS
  bool _ircConnected;
  bool _ircRegistered;       // Received 001 RPL_WELCOME
  bool _ircJoined;           // Successfully joined channel

  char _ircHost[IRC_MAX_HOST_LEN];
  uint16_t _ircPort;
  char _ircNick[IRC_MAX_NICK_LEN];
  char _ircChannel[IRC_MAX_CHANNEL_LEN];

  // Message circular buffer
  IRCMessage* _ircMessages;  // PSRAM allocated
  int _ircMsgHead;           // Next write position
  int _ircMsgCount;          // Total messages stored

  // Protocol line buffer (partial line accumulation)
  char _ircLineBuf[IRC_LINE_BUF_SIZE];
  int _ircLineLen;

  // Compose buffer
  char _ircCompose[IRC_COMPOSE_MAX];
  int _ircComposeLen;
  bool _ircComposing;        // true when typing a message

  // Display state
  int _ircScrollPos;         // Scroll offset from bottom (0 = newest)
  int _ircLinesPerPage;

  // Setup screen state
  int _ircSetupField;        // 0=host, 1=port, 2=nick, 3=channel, 4=connect button
  char _ircSetupBuf[IRC_MAX_HOST_LEN]; // Edit buffer for setup fields
  int _ircSetupBufLen;
  bool _ircSetupEditing;     // true when typing into a field

  // Connection management
  unsigned long _ircLastDataTime;
  unsigned long _ircReconnectAt;  // 0 = no reconnect pending
  bool _ircDirty;                 // New messages need rendering
  unsigned long _ircLastRender;   // Throttle directRedraw from poll()

  // ---- Memory Management ----

  bool allocateBuffers() {
    if (!_textBuffer) {
      _textBuffer = (char*)ps_malloc(WEB_MAX_TEXT_SIZE);
      if (!_textBuffer) {
        Serial.println("WebReader: Failed to allocate text buffer from PSRAM");
        return false;
      }
    }
    if (!_links) {
      _links = (WebLink*)ps_malloc(sizeof(WebLink) * WEB_MAX_LINKS);
      if (!_links) {
        Serial.println("WebReader: Failed to allocate links buffer from PSRAM");
        return false;
      }
    }
    return true;
  }

  void freeBuffers() {
    if (_textBuffer) { free(_textBuffer); _textBuffer = nullptr; }
    if (_links) { free(_links); _links = nullptr; }
    _textLen = 0;
    _linkCount = 0;
    _formCount = 0;
    _pageOffsets.clear();
  }

  // ---- Domain extraction from URL ----
  static void extractDomain(const char* url, char* domain, int domainMax) {
    const char* host = strstr(url, "://");
    if (host) host += 3; else host = url;
    int i = 0;
    while (host[i] && host[i] != '/' && host[i] != ':' && host[i] != '?' && host[i] != '#' && i < domainMax - 1) {
      domain[i] = host[i]; i++;
    }
    domain[i] = '\0';
  }

  // ---- Cookie Management ----
  void setCookie(const char* domain, const char* name, const char* value) {
    // Update existing cookie
    for (int i = 0; i < _cookieCount; i++) {
      if (strcmp(_cookies[i].domain, domain) == 0 &&
          strcmp(_cookies[i].name, name) == 0) {
        strncpy(_cookies[i].value, value, sizeof(_cookies[i].value) - 1);
        return;
      }
    }
    // Add new cookie
    if (_cookieCount < WEB_MAX_COOKIES) {
      Cookie& c = _cookies[_cookieCount++];
      strncpy(c.domain, domain, sizeof(c.domain) - 1);
      strncpy(c.name, name, sizeof(c.name) - 1);
      strncpy(c.value, value, sizeof(c.value) - 1);
    }
  }

  // Build Cookie header value for a domain
  String buildCookieHeader(const char* domain) {
    String result;
    for (int i = 0; i < _cookieCount; i++) {
      // Match domain (simple suffix match)
      if (strstr(domain, _cookies[i].domain) ||
          strcmp(domain, _cookies[i].domain) == 0) {
        if (result.length() > 0) result += "; ";
        result += _cookies[i].name;
        result += "=";
        result += _cookies[i].value;
      }
    }
    return result;
  }

  // Parse Set-Cookie header(s) from HTTP response
  void parseSetCookie(const String& headerVal, const char* domain) {
    // Format: name=value; Path=/; ...
    int eq = headerVal.indexOf('=');
    if (eq < 1) return;
    String name = headerVal.substring(0, eq);
    name.trim();
    int semi = headerVal.indexOf(';', eq);
    String value = (semi > 0) ? headerVal.substring(eq + 1, semi)
                              : headerVal.substring(eq + 1);
    value.trim();

    // Check for domain in cookie attributes
    char cookieDomain[64];
    strncpy(cookieDomain, domain, sizeof(cookieDomain) - 1);
    int domIdx = headerVal.indexOf("domain=");
    if (domIdx < 0) domIdx = headerVal.indexOf("Domain=");
    if (domIdx >= 0) {
      int dStart = domIdx + 7;
      int dEnd = headerVal.indexOf(';', dStart);
      String d = (dEnd > 0) ? headerVal.substring(dStart, dEnd) : headerVal.substring(dStart);
      d.trim();
      if (d.startsWith(".")) d = d.substring(1);
      strncpy(cookieDomain, d.c_str(), sizeof(cookieDomain) - 1);
    }
    setCookie(cookieDomain, name.c_str(), value.c_str());
    Serial.printf("Cookie SET: %s=%s (domain=%s)\n", name.c_str(), value.c_str(), cookieDomain);
  }

  // Capture all Set-Cookie headers from response using index iteration.
  // ESP32 HTTPClient's header("Set-Cookie") may only return the first/last
  // when multiple Set-Cookie headers exist. Iterating by index catches all.
  void captureResponseCookies(HTTPClient& http, const char* domain) {
    int hCount = http.headers();
    Serial.printf("Cookie capture: %d header slots, domain=%s\n", hCount, domain);
    int found = 0;
    for (int h = 0; h < hCount; h++) {
      String name = http.headerName(h);
      String val = http.header(h);
      Serial.printf("  Slot[%d] '%s' (%d chars) = '%.200s%s'\n", h, name.c_str(),
                    val.length(), val.c_str(), val.length() > 200 ? "..." : "");
      if (name.equalsIgnoreCase("Set-Cookie")) {
        // A single header entry might still contain multiple cookies
        // concatenated by ESP32 with comma. Try to split them.
        int start = 0;
        while (start < (int)val.length()) {
          int comma = val.indexOf(", ", start);
          String single;
          if (comma >= 0) {
            // Check if what follows looks like a new cookie (name=value before ;)
            String rest = val.substring(comma + 2);
            int eq = rest.indexOf('=');
            int semi = rest.indexOf(';');
            if (eq > 0 && (semi < 0 || eq < semi)) {
              single = val.substring(start, comma);
              start = comma + 2;
            } else {
              // Comma is part of a cookie value (e.g. date), skip
              comma = val.indexOf(", ", comma + 2);
              if (comma >= 0) {
                single = val.substring(start, comma);
                start = comma + 2;
              } else {
                single = val.substring(start);
                start = val.length();
              }
            }
          } else {
            single = val.substring(start);
            start = val.length();
          }
          parseSetCookie(single, domain);
          found++;
        }
      }
    }
    // Also try the name-based lookup in case index iteration missed something
    if (found == 0 && http.hasHeader("Set-Cookie")) {
      String sc = http.header("Set-Cookie");
      if (sc.length() > 0) {
        Serial.printf("Cookie: fallback name-based: %.80s\n", sc.c_str());
        parseSetCookie(sc, domain);
      }
    }
    if (found == 0 && !http.hasHeader("Set-Cookie")) {
      Serial.println("Cookie capture: NO Set-Cookie headers in response at all");
    }
    Serial.printf("Cookie jar now has %d cookies for domain %s\n", _cookieCount, domain);
  }

  // ---- URL Encoding ----
  static void urlEncode(const char* src, char* dst, int dstMax) {
    static const char hex[] = "0123456789ABCDEF";
    int di = 0;
    for (int i = 0; src[i] && di < dstMax - 4; i++) {
      char c = src[i];
      if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
          (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
        dst[di++] = c;
      } else if (c == ' ') {
        dst[di++] = '+';
      } else {
        dst[di++] = '%';
        dst[di++] = hex[(uint8_t)c >> 4];
        dst[di++] = hex[(uint8_t)c & 0x0F];
      }
    }
    dst[di] = '\0';
  }

  // Build form POST body from form fields
  String buildFormBody(const WebForm& form) {
    String body;
    for (int i = 0; i < form.fieldCount; i++) {
      const WebFormField& f = form.fields[i];
      if (!f.name[0]) continue;  // Skip unnamed fields
      if (body.length() > 0) body += "&";
      char encName[128], encVal[256];
      urlEncode(f.name, encName, sizeof(encName));
      urlEncode(f.value, encVal, sizeof(encVal));
      body += encName;
      body += "=";
      body += encVal;
    }
    return body;
  }

  // ---- WiFi Management ----
  // Note: On the BLE variant, WiFi and BLE coexist on the ESP32-S3 radio.
  // Some throughput reduction is normal. WiFi is started on-demand and stays
  // connected until explicitly disconnected or the device sleeps.
  // On the 4G variant, if PPP is active (modem providing network), WiFi is
  // not needed — HTTPClient routes through the PPP interface automatically.

  bool isWiFiConnected() {
    return WiFi.status() == WL_CONNECTED;
  }

  // Generic network check: returns true if any network interface is up.
  // Works for WiFi and will also detect PPP (4G modem) connections.
  bool isNetworkAvailable() {
    if (WiFi.status() == WL_CONNECTED) return true;
#ifdef HAS_4G_MODEM
    // When PPP is active via the A7682E modem, the ESP32 gets an IP on the
    // ppp netif. Check if we have a non-zero IP on any interface.
    // TODO: implement PPP status check when modem PPP driver is added
#endif
    return false;
  }

  void startWifiScan() {
    _wifiState = WIFI_SCANNING;

    // Show scanning splash before the blocking scan (3-5 seconds)
    if (_display) {
      _display->startFrame();
      _display->setColor(DisplayDriver::GREEN);
      _display->setTextSize(1);
      _display->setCursor(0, 0);
      _display->print("WiFi Setup");
      _display->drawRect(0, 11, _display->width(), 1);
      _display->setColor(DisplayDriver::LIGHT);
      _display->setTextSize(0);
      _display->setCursor(0, 18);
      _display->print("Scanning for networks...");
      _display->endFrame();
    }

    Serial.printf("WebReader: Starting WiFi scan (mode=%d, status=%d)\n",
                  WiFi.getMode(), WiFi.status());

    // Use blocking scan — takes 3-5 seconds, which is fine for e-ink.
    // Async scan has race conditions with WiFi.disconnect() on ESP32-S3.
    int n = WiFi.scanNetworks(false, false, false, 300);  // blocking, no hidden, active, 300ms/ch
    Serial.printf("WebReader: Scan complete, found %d networks\n", n);

    if (n > 0) {
      _ssidCount = min(n, WEB_MAX_SSIDS);
      for (int i = 0; i < _ssidCount; i++) {
        _ssidList[i] = WiFi.SSID(i);
        Serial.printf("  [%d] %s (RSSI %d)\n", i, _ssidList[i].c_str(), WiFi.RSSI(i));
      }
      WiFi.scanDelete();
      _selectedSSID = 0;
      _wifiState = WIFI_SCAN_DONE;
    } else if (n == 0) {
      _wifiState = WIFI_FAILED;
      _fetchError = "No networks found";
    } else {
      _wifiState = WIFI_FAILED;
      _fetchError = "Scan failed (err " + String(n) + ")";
      Serial.printf("WebReader: Scan error code: %d\n", n);
    }
  }

  void checkWifiScan() {
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      if (millis() > _wifiTimeout) {
        Serial.println("WebReader: scan timeout");
        _wifiState = WIFI_FAILED;
        _fetchError = "Scan timeout";
      }
      return;
    }
    if (n == WIFI_SCAN_FAILED || n < 0) {
      Serial.printf("WebReader: scanComplete returned %d (failed)\n", n);
      _wifiState = WIFI_FAILED;
      _fetchError = "Scan failed";
      return;
    }
    if (n == 0) {
      Serial.println("WebReader: scan found 0 networks");
      _wifiState = WIFI_FAILED;
      _fetchError = "No networks found";
      return;
    }

    Serial.printf("WebReader: scan found %d networks\n", n);

    _ssidCount = min(n, WEB_MAX_SSIDS);
    for (int i = 0; i < _ssidCount; i++) {
      _ssidList[i] = WiFi.SSID(i);
    }
    WiFi.scanDelete();
    _selectedSSID = 0;
    _wifiState = WIFI_SCAN_DONE;
  }

  void connectToSSID(const String& ssid, const char* password) {
    _wifiState = WIFI_CONNECTING;
    WiFi.begin(ssid.c_str(), password);
    _wifiTimeout = millis() + 15000;
    _connectedSSID = ssid;
  }

  void checkWifiConnect() {
    if (WiFi.status() == WL_CONNECTED) {
      _wifiState = WIFI_CONNECTED;
      Serial.printf("WebReader: WiFi connected to %s, IP: %s\n",
                    _connectedSSID.c_str(), WiFi.localIP().toString().c_str());
      // Save credentials to SD for auto-reconnect
      saveWifiCredentials(_connectedSSID.c_str(), _wifiPass);
      return;
    }
    if (millis() > _wifiTimeout) {
      _wifiState = WIFI_FAILED;
      _fetchError = "Connection timeout";
    }
  }

  // Show a brief "Connected!" confirmation splash, then transition to HOME.
  // Called after successful WiFi connection (auto or manual).
  void showConnectedAndGoHome() {
    if (_display) {
      _display->startFrame();
      _display->setColor(DisplayDriver::GREEN);
      _display->setTextSize(1);
      _display->setCursor(0, 0);
      _display->print("Web Reader");
      _display->drawRect(0, 11, _display->width(), 1);

      _display->setTextSize(0);
      _display->setCursor(0, 18);
      _display->print("Connected!");
      _display->setCursor(0, 30);
      _display->setColor(DisplayDriver::LIGHT);
      char ipBuf[48];
      snprintf(ipBuf, sizeof(ipBuf), "SSID: %s", _connectedSSID.c_str());
      _display->print(ipBuf);
      _display->setCursor(0, 40);
      snprintf(ipBuf, sizeof(ipBuf), "IP:   %s", WiFi.localIP().toString().c_str());
      _display->print(ipBuf);
      _display->endFrame();
    }
    delay(1500);  // Brief pause so user sees the confirmation
    _mode = HOME;
    directRedraw();  // Now show the URL entry page
  }

  void saveWifiCredentials(const char* ssid, const char* pass) {
    if (!SD.exists(WEB_CACHE_DIR)) SD.mkdir(WEB_CACHE_DIR);
    File f = SD.open("/web/wifi.cfg", FILE_WRITE);
    if (f) {
      f.println(ssid);
      f.println(pass);
      f.close();
    }
    digitalWrite(SDCARD_CS, HIGH);
  }

  bool loadAndAutoConnect() {
    File f = SD.open("/web/wifi.cfg", FILE_READ);
    if (!f) { digitalWrite(SDCARD_CS, HIGH); return false; }

    String ssid = f.readStringUntil('\n');
    String pass = f.readStringUntil('\n');
    f.close();
    digitalWrite(SDCARD_CS, HIGH);

    ssid.trim();
    pass.trim();

    if (ssid.length() == 0) return false;

    Serial.printf("WebReader: Auto-connecting to %s\n", ssid.c_str());
    // WiFi STA mode already set by main.cpp before enter()
    WiFi.begin(ssid.c_str(), pass.c_str());

    // Brief blocking wait (up to 5 seconds) during init
    unsigned long timeout = millis() + 5000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      _connectedSSID = ssid;
      _wifiState = WIFI_CONNECTED;
      Serial.printf("WebReader: Auto-connected, IP: %s\n",
                    WiFi.localIP().toString().c_str());
      return true;
    }
    return false;
  }

  // Read HTTP response body, handling both chunked and fixed-length transfers.
  // Chunked transfer encoding embeds chunk size headers in the stream:
  //   <hex-size>\r\n<data>\r\n ... 0\r\n\r\n
  // If we read raw from getStreamPtr(), those headers corrupt our HTML.
  int readResponseBody(HTTPClient& http, char* buffer, int maxLen) {
    int contentLen = http.getSize();
    WiFiClient* stream = http.getStreamPtr();
    int totalRead = 0;
    unsigned long lastSplash = millis();

    if (contentLen > 0) {
      // Known content length — read directly (no chunking)
      int toRead = min(contentLen, maxLen - 1);
      while (totalRead < toRead) {
        if (!stream->available()) {
          unsigned long waitStart = millis();
          while (!stream->available() && (millis() - waitStart) < 5000) {
            delay(10);
            yield();
          }
          if (!stream->available()) break;
        }
        int chunk = stream->readBytes(buffer + totalRead,
                                      min(1024, toRead - totalRead));
        if (chunk <= 0) break;
        totalRead += chunk;
        _fetchProgress = totalRead;
        if (_display && (millis() - lastSplash) >= 2000) {
          _display->startFrame();
          renderFetching(*_display);
          _display->endFrame();
          lastSplash = millis();
        }
        yield();
      }
    } else {
      // Chunked transfer or unknown length (-1)
      // Read chunk by chunk: each chunk starts with hex size + \r\n
      Serial.println("WebReader: Chunked transfer encoding detected");
      while (totalRead < maxLen - 1) {
        // Read chunk size line (hex + \r\n)
        String sizeLine = stream->readStringUntil('\n');
        sizeLine.trim(); // Remove \r
        if (sizeLine.length() == 0) {
          // Empty line, try to continue
          if (!stream->available()) break;
          continue;
        }

        // Parse hex chunk size
        int chunkSize = (int)strtol(sizeLine.c_str(), nullptr, 16);
        if (chunkSize == 0) break; // Final chunk — we're done

        // Read chunk data
        int chunkRead = 0;
        while (chunkRead < chunkSize && totalRead < maxLen - 1) {
          if (!stream->available()) {
            unsigned long waitStart = millis();
            while (!stream->available() && (millis() - waitStart) < 5000) {
              delay(10);
              yield();
            }
            if (!stream->available()) break;
          }
          int toGet = min(1024, min(chunkSize - chunkRead, maxLen - 1 - totalRead));
          int got = stream->readBytes(buffer + totalRead, toGet);
          if (got <= 0) break;
          totalRead += got;
          chunkRead += got;
          _fetchProgress = totalRead;
          if (_display && (millis() - lastSplash) >= 2000) {
            _display->startFrame();
            renderFetching(*_display);
            _display->endFrame();
            lastSplash = millis();
          }
          yield();
        }

        // Consume trailing \r\n after chunk data
        if (stream->available()) {
          char cr = stream->read(); // \r
          if (stream->available() && cr == '\r') stream->read(); // \n
        }
      }
    }
    return totalRead;
  }

  // ---- HTTP Fetch ----
  // Uses ESP32 HTTPClient which works over any active network interface
  // (WiFi STA, PPP via 4G modem, etc). The caller is responsible for
  // ensuring network connectivity before calling fetchPage().

  // Fetch a page with a self-referrer (origin of the URL) for Cloudflare compatibility
  bool fetchWithSelfRef(const char* url) {
    char selfRef[256];
    const char* schEnd = strstr(url, "://");
    if (schEnd) {
      const char* pathStart = strchr(schEnd + 3, '/');
      int oLen = pathStart ? (pathStart - url) : strlen(url);
      if (oLen > 254) oLen = 254;
      memcpy(selfRef, url, oLen);
      selfRef[oLen] = '/';
      selfRef[oLen + 1] = '\0';
    } else {
      strncpy(selfRef, url, 255);
      selfRef[255] = '\0';
    }
    return fetchPage(url, nullptr, nullptr, selfRef);
  }

  // Translate ESP32 HTTPClient error codes to readable strings
  static String httpErrorString(int code) {
    if (code == 525) return "SSL error (Cloudflare)";
    if (code == 403) return "Blocked (403)";
    if (code == 503) return "Unavailable (503)";
    if (code > 0) return "HTTP " + String(code);
    switch (code) {
      case -1:  return "Connection refused";
      case -2:  return "Send header failed";
      case -3:  return "Send payload failed";
      case -4:  return "Not connected";
      case -5:  return "Connection lost";
      case -6:  return "No stream";
      case -7:  return "No HTTP server";
      case -8:  return "Out of RAM";
      case -9:  return "Encoding error";
      case -10: return "Stream write error";
      case -11: return "Read timeout";
      default:  return "Error " + String(code);
    }
  }

  bool fetchPage(const char* url, const char* postBody = nullptr,
                 const char* contentType = nullptr,
                 const char* referer = nullptr) {
    Serial.printf("WebReader: fetchPage('%s', post=%s, ref=%s)\n",
                  url, postBody ? "yes" : "no", referer ? referer : "(null)");
    if (!allocateBuffers()) {
      _fetchError = "Out of memory";
      _mode = HOME;
      return false;
    }

    _mode = FETCHING;
    _fetchStartTime = millis();
    _fetchProgress = 0;
    _fetchError = "";

    // Push current URL to back stack (if we have one)
    if (_currentUrl[0] != '\0') {
      _backHistory.push_back(String(_currentUrl));
      if (_backHistory.size() > 20) _backHistory.erase(_backHistory.begin());
    }

    // Show the loading screen before blocking fetch
    if (_display) {
      _display->startFrame();
      renderFetching(*_display);
      _display->endFrame();
    }

    // Download HTML
    char* htmlBuffer = (char*)ps_malloc(WEB_MAX_PAGE_SIZE);
    if (!htmlBuffer) {
      _fetchError = "Out of memory (HTML)";
      _mode = HOME;
      return false;
    }

    int htmlLen = 0;
    bool success = false;

    // Extract domain for cookies
    char domain[64];
    extractDomain(url, domain, sizeof(domain));
    String cookieHeader = buildCookieHeader(domain);

    // Headers we want to capture from response
    const char* collectHeaderNames[] = {"Set-Cookie", "Location"};

    // Manual redirect loop — we handle redirects ourselves to capture
    // Set-Cookie headers at each hop. We reuse the TLS client for
    // same-host redirects to avoid repeated 5-8s TLS handshakes.
    String currentUrl = url;
    int redirectCount = 0;
    const int maxRedirects = 5;
    bool isPost = (postBody != nullptr);

    // Pre-create TLS client outside loop for connection reuse
    ensureTlsUsesPsram();
    WiFiClientSecure* tlsClient = new WiFiClientSecure();
    tlsClient->setInsecure();
    tlsClient->setHandshakeTimeout(30);
    String lastHost = ""; // Track host for connection reuse
    int connRetries = 0;  // Connection failure retries (max 1)
    int serverRetries = 0; // 5xx error retries (max 1)

    while (redirectCount <= maxRedirects) {
      // Update domain and cookies for current URL
      extractDomain(currentUrl.c_str(), domain, sizeof(domain));
      cookieHeader = buildCookieHeader(domain);

      Serial.printf("WebReader: %s %s (redirect #%d)\n",
                    isPost ? "POST" : "GET", currentUrl.c_str(), redirectCount);
      Serial.printf("WebReader: heap: %d, largest: %d\n",
                    ESP.getFreeHeap(), ESP.getMaxAllocHeap());

      bool isHttps = currentUrl.startsWith("https://");

      HTTPClient http;
      http.setUserAgent(WEB_USER_AGENT);
      http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
      http.setTimeout(30000);
      http.setReuse(true); // Keep connection alive for redirects

      bool beginOk;
      if (isHttps) {
        // Check if we need a fresh TLS client (different host)
        String currentHost = currentUrl.substring(8); // skip "https://"
        int slashIdx = currentHost.indexOf('/');
        if (slashIdx > 0) currentHost = currentHost.substring(0, slashIdx);
        
        if (lastHost.length() > 0 && lastHost != currentHost) {
          // Different host — need fresh TLS client
          delete tlsClient;
          tlsClient = new WiFiClientSecure();
          tlsClient->setInsecure();
          tlsClient->setHandshakeTimeout(30);
        }
        lastHost = currentHost;
        
        beginOk = http.begin(*tlsClient, currentUrl);
      } else {
        beginOk = http.begin(currentUrl);
      }

      if (!beginOk) {
        _fetchError = "Connection failed";
        break;
      }

      // MUST be after begin() — begin() resets collected headers
      http.collectHeaders(collectHeaderNames, 2);

      if (cookieHeader.length() > 0) {
        http.addHeader("Cookie", cookieHeader);
        Serial.printf("WebReader: Cookies (%d chars)\n", cookieHeader.length());
      }

      // Standard browser headers
      http.addHeader("Accept", "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8");
      http.addHeader("Accept-Language", "en-US,en;q=0.9");
      http.addHeader("Upgrade-Insecure-Requests", "1");
      // Cache-busting: tell Cloudflare to revalidate with origin.
      // Using max-age=0 (browser standard) instead of no-cache to avoid
      // Cloudflare 525 SSL errors on some origins.
      http.addHeader("Cache-Control", "max-age=0");

      // Re-render splash before blocking call
      if (_display) {
        _display->startFrame();
        renderFetching(*_display);
        _display->endFrame();
      }

      int httpCode;
      if (isPost) {
        http.addHeader("Content-Type", contentType ? contentType
                       : "application/x-www-form-urlencoded");
        // Referer = page that contained the form; Origin = scheme+host
        const char* ref = referer ? referer : currentUrl.c_str();
        http.addHeader("Referer", ref);
        char origin[128];
        const char* schEnd = strstr(ref, "://");
        if (schEnd) {
          const char* pathStart = strchr(schEnd + 3, '/');
          int oLen = pathStart ? (pathStart - ref) : strlen(ref);
          if (oLen > (int)sizeof(origin) - 1) oLen = sizeof(origin) - 1;
          memcpy(origin, ref, oLen);
          origin[oLen] = '\0';
        } else {
          strncpy(origin, ref, sizeof(origin) - 1);
          origin[sizeof(origin) - 1] = '\0';
        }
        http.addHeader("Origin", origin);
        // Sec-Fetch headers for form submissions
        http.addHeader("Sec-Fetch-Dest", "document");
        http.addHeader("Sec-Fetch-Mode", "navigate");
        http.addHeader("Sec-Fetch-Site", "same-origin");
        http.addHeader("Sec-Fetch-User", "?1");
        httpCode = http.POST((uint8_t*)postBody, strlen(postBody));
        Serial.printf("WebReader: POST -> %d (Referer: %s)\n", httpCode, ref);
      } else {
        // Send Referer on GET too — helps with Cloudflare bot detection
        if (referer && referer[0]) {
          http.addHeader("Referer", referer);
        }
        httpCode = http.GET();
        Serial.printf("WebReader: GET -> %d\n", httpCode);
      }

      // Capture all Set-Cookie headers from this response
      captureResponseCookies(http, domain);

      // Connection-level failure (timeout, TLS error, etc) — retry once
      if (httpCode < 0 && connRetries < 1) {
        http.end();
        // Destroy and recreate TLS client — old connection state is broken
        delete tlsClient;
        tlsClient = new WiFiClientSecure();
        tlsClient->setInsecure();
        tlsClient->setHandshakeTimeout(30);
        lastHost = "";  // Force fresh connection
        connRetries++;
        redirectCount++;
        Serial.printf("WebReader: Connection error %d, retrying...\n", httpCode);
        delay(2000);
        continue;
      }
      if (httpCode < 0) {
        _fetchError = httpErrorString(httpCode);
        http.end();
        break;
      }

      // Handle redirects
      if (httpCode == 301 || httpCode == 302 || httpCode == 303 || httpCode == 307) {
        String location = http.header("Location");
        // Don't call http.end() for same-host redirects — preserve connection
        String body = http.getString(); // consume response body to free connection
        if (location.length() == 0) {
          http.end();
          _fetchError = "Redirect with no Location";
          break;
        }
        char resolved[WEB_MAX_URL_LEN];
        resolveUrl(currentUrl.c_str(), location.c_str(), resolved, WEB_MAX_URL_LEN);
        currentUrl = resolved;
        if (httpCode == 302 || httpCode == 303) isPost = false;
        redirectCount++;
        Serial.printf("WebReader: Redirect -> %s\n", currentUrl.c_str());
        continue;
      }

      if (httpCode == HTTP_CODE_OK) {
        htmlLen = readResponseBody(http, htmlBuffer, WEB_MAX_PAGE_SIZE);
        success = (htmlLen > 0);
        http.end();
        break;
      } else if (httpCode >= 500 && httpCode < 600 && serverRetries < 1) {
        // Server error (e.g. Cloudflare 525) — retry once after brief delay
        String body = http.getString();
        http.end();
        serverRetries++;
        redirectCount++;
        Serial.printf("WebReader: Server error %d, retrying...\n", httpCode);
        delay(1500);
        continue;
      } else {
        _fetchError = httpErrorString(httpCode);
        http.end();
        break;
      }
    } // end redirect loop

    delete tlsClient;

    if (redirectCount > maxRedirects && !success) {
      _fetchError = "Too many redirects";
    }

    if (!success) {
      free(htmlBuffer);
      Serial.printf("WebReader: Fetch failed: %s\n", _fetchError.c_str());
      _mode = HOME;
      // Show error briefly then return to URL entry
      if (_display) {
        _display->startFrame();
        _display->setColor(DisplayDriver::GREEN);
        _display->setTextSize(1);
        _display->setCursor(0, 0);
        _display->print("Web Reader");
        _display->drawRect(0, 11, _display->width(), 1);
        _display->setColor(DisplayDriver::YELLOW);
        _display->setTextSize(0);
        _display->setCursor(0, 18);
        _display->print("Fetch failed:");
        _display->setColor(DisplayDriver::LIGHT);
        _display->setCursor(0, 30);
        // Word-wrap error message
        String errMsg = _fetchError;
        int y2 = 30;
        while (errMsg.length() > 0 && y2 < 60) {
          String line = errMsg.substring(0, _charsPerLine);
          errMsg = errMsg.substring(line.length());
          _display->setCursor(0, y2);
          _display->print(line.c_str());
          y2 += 8;
        }
        _display->setCursor(0, 70);
        _display->print(_urlBuffer);
        _display->setCursor(0, 90);
        _display->setColor(DisplayDriver::GREEN);
        _display->print("Returning to URL entry...");
        _display->endFrame();
      }
      delay(2500);
      return false;
    }

    htmlBuffer[htmlLen] = '\0';

    // Extract <title> if present
    _pageTitle[0] = '\0';
    const char* titleStart = strcasestr(htmlBuffer, "<title>");
    if (titleStart) {
      titleStart += 7;
      const char* titleEnd = strcasestr(titleStart, "</title>");
      if (titleEnd) {
        int titleLen = titleEnd - titleStart;
        if (titleLen > (int)sizeof(_pageTitle) - 1)
          titleLen = sizeof(_pageTitle) - 1;
        memcpy(_pageTitle, titleStart, titleLen);
        _pageTitle[titleLen] = '\0';
      }
    }

    // Parse HTML to text
    ParseResult pr = parseHtml(htmlBuffer, htmlLen, _textBuffer, WEB_MAX_TEXT_SIZE,
                               _links, WEB_MAX_LINKS,
                               _forms, WEB_MAX_FORMS, currentUrl.c_str());
    _textLen = pr.textLen;
    _linkCount = pr.linkCount;
    _formCount = pr.formCount;
    Serial.printf("WebReader: Parsed %d chars, %d links, %d forms\n",
                  _textLen, _linkCount, _formCount);

    free(htmlBuffer);

    // Update URL bar with final URL (may differ from original after redirects)
    strncpy(_urlBuffer, currentUrl.c_str(), WEB_MAX_URL_LEN - 1);
    _urlLen = strlen(_urlBuffer);

    // Store current URL (final URL after any redirects)
    strncpy(_currentUrl, currentUrl.c_str(), WEB_MAX_URL_LEN - 1);
    _currentUrl[WEB_MAX_URL_LEN - 1] = '\0';
    Serial.printf("WebReader: _currentUrl set to: %s\n", _currentUrl);

    // Add to history (final URL after redirects)
    addToHistory(currentUrl.c_str());

    // Paginate
    paginateText();

    Serial.printf("WebReader: Fetched %s - %d chars text, %d links, %d forms, %d pages\n",
                  currentUrl.c_str(), _textLen, _linkCount, _formCount, _totalPages);

    _mode = READING;
    _currentPage = 0;
    return true;
  }

  // Submit a form via GET or POST
  bool submitForm(int formIdx) {
    if (formIdx < 0 || formIdx >= _formCount) return false;
    WebForm& form = _forms[formIdx];

    Serial.printf("WebReader: Submitting form %d (%s) to %s\n",
                  formIdx, form.isPost ? "POST" : "GET", form.action);
    Serial.printf("WebReader: Referer will be: %s\n", _currentUrl);
    Serial.printf("WebReader: Cookie jar has %d cookies:\n", _cookieCount);
    for (int c = 0; c < _cookieCount; c++) {
      Serial.printf("  [%d] %s = %.30s... (domain=%s)\n",
                    c, _cookies[c].name, _cookies[c].value, _cookies[c].domain);
    }

    if (form.isPost) {
      // Save user-entered field values before first POST attempt.
      // After fetchPage(), the form structures get overwritten by the new page.
      // We need these to retry if CSRF fails (stale token from cached page).
      struct SavedField { char name[32]; char value[WEB_MAX_FIELD_VALUE]; };
      SavedField savedFields[WEB_MAX_FORM_FIELDS];
      int savedCount = form.fieldCount;
      char savedAction[WEB_MAX_URL_LEN];
      char savedReferer[WEB_MAX_URL_LEN];
      strncpy(savedAction, form.action, WEB_MAX_URL_LEN - 1);
      savedAction[WEB_MAX_URL_LEN - 1] = '\0';
      strncpy(savedReferer, _currentUrl, WEB_MAX_URL_LEN - 1);
      savedReferer[WEB_MAX_URL_LEN - 1] = '\0';
      for (int f = 0; f < form.fieldCount && f < WEB_MAX_FORM_FIELDS; f++) {
        strncpy(savedFields[f].name, form.fields[f].name, 31);
        savedFields[f].name[31] = '\0';
        strncpy(savedFields[f].value, form.fields[f].value, WEB_MAX_FIELD_VALUE - 1);
        savedFields[f].value[WEB_MAX_FIELD_VALUE - 1] = '\0';
      }

      String body = buildFormBody(form);
      Serial.printf("WebReader: POST body (%d bytes): %s\n",
                    body.length(), body.c_str());
      strncpy(_urlBuffer, form.action, WEB_MAX_URL_LEN - 1);
      _urlLen = strlen(_urlBuffer);

      // --- First POST attempt ---
      // On Cloudflare-cached sites, this may fail because the CSRF token
      // came from a cached page with no session. But the 302 response
      // WILL set _otwarchive_session, creating the session we need.
      bool result = fetchPage(form.action, body.c_str(), nullptr, _currentUrl);

      // Check if we got redirected to an auth error page
      if (strstr(_currentUrl, "auth_error") || strstr(_currentUrl, "session_expired")) {
        Serial.println("WebReader: Auth error detected — CSRF token was stale (cached page)");
        Serial.println("WebReader: Retrying with fresh session cookie...");

        // Show retry status on display
        if (_display) {
          _display->startFrame();
          _display->setColor(DisplayDriver::GREEN);
          _display->setTextSize(2);
          _display->setCursor(10, 20);
          _display->print("Logging in...");
          _display->setTextSize(0);
          _display->setColor(DisplayDriver::LIGHT);
          _display->setCursor(10, 45);
          _display->print("Refreshing session...");
          _display->endFrame();
        }

        // Re-fetch the original form page.
        // Now that we have _otwarchive_session cookie, Cloudflare should
        // bypass its cache and serve a fresh page from AO3's origin with
        // a CSRF token that matches our session.
        Serial.printf("WebReader: Re-fetching form page: %s\n", savedReferer);
        result = fetchWithSelfRef(savedReferer);

        if (result && _formCount > 0) {
          // Find the login form and update its fields with saved user data
          int retryForm = -1;
          for (int fi = 0; fi < _formCount; fi++) {
            // Match by action URL
            if (strstr(_forms[fi].action, "login") || strstr(_forms[fi].action, "session")) {
              retryForm = fi;
              break;
            }
          }
          if (retryForm < 0) retryForm = 0; // fallback to first form

          WebForm& newForm = _forms[retryForm];
          Serial.printf("WebReader: Found retry form %d with %d fields, action: %s\n",
                        retryForm, newForm.fieldCount, newForm.action);

          // Copy saved user values into matching fields of new form.
          // Skip CSRF tokens (they have fresh values from the re-fetch).
          for (int sf = 0; sf < savedCount; sf++) {
            // Skip CSRF-type fields — new form already has fresh token
            if (strcmp(savedFields[sf].name, "authenticity_token") == 0 ||
                strcmp(savedFields[sf].name, "csrf_token") == 0 ||
                strcmp(savedFields[sf].name, "_token") == 0 ||
                strcmp(savedFields[sf].name, "commit") == 0) {
              continue;
            }
            // Find matching field in new form
            for (int nf = 0; nf < newForm.fieldCount; nf++) {
              if (strcmp(newForm.fields[nf].name, savedFields[sf].name) == 0) {
                strncpy(newForm.fields[nf].value, savedFields[sf].value,
                        WEB_MAX_FIELD_VALUE - 1);
                Serial.printf("WebReader: Restored field '%s'\n", savedFields[sf].name);
                break;
              }
            }
          }

          // Build new POST body with fresh CSRF token + saved user data
          String retryBody = buildFormBody(newForm);
          Serial.printf("WebReader: Retry POST body (%d bytes): %s\n",
                        retryBody.length(), retryBody.c_str());
          Serial.printf("WebReader: Cookie jar has %d cookies:\n", _cookieCount);
          for (int c = 0; c < _cookieCount; c++) {
            Serial.printf("  [%d] %s = %.30s... (domain=%s)\n",
                          c, _cookies[c].name, _cookies[c].value, _cookies[c].domain);
          }

          strncpy(_urlBuffer, newForm.action, WEB_MAX_URL_LEN - 1);
          _urlLen = strlen(_urlBuffer);
          result = fetchPage(newForm.action, retryBody.c_str(), nullptr, savedReferer);
        } else {
          Serial.println("WebReader: Re-fetch failed or no forms found for retry");
        }
      }
      return result;
    } else {
      // GET - append form data as query string
      String getUrl = form.action;
      String body = buildFormBody(form);
      if (body.length() > 0) {
        getUrl += (getUrl.indexOf('?') >= 0) ? "&" : "?";
        getUrl += body;
      }
      strncpy(_urlBuffer, getUrl.c_str(), WEB_MAX_URL_LEN - 1);
      _urlLen = strlen(_urlBuffer);
      return fetchPage(getUrl.c_str(), nullptr, nullptr, _currentUrl);
    }
  }

  // strcasestr implementation (not always available)
  static const char* strcasestr(const char* haystack, const char* needle) {
    if (!needle[0]) return haystack;
    for (const char* p = haystack; *p; p++) {
      const char* h = p;
      const char* n = needle;
      while (*h && *n) {
        char hc = *h; if (hc >= 'A' && hc <= 'Z') hc += 32;
        char nc = *n; if (nc >= 'A' && nc <= 'Z') nc += 32;
        if (hc != nc) break;
        h++; n++;
      }
      if (!*n) return p;
    }
    return nullptr;
  }

  // ---- Pagination ----

  void paginateText() {
    _pageOffsets.clear();
    _pageOffsets.push_back(0);

    int pos = 0;
    while (pos < _textLen) {
      int lineCount = 0;
      while (lineCount < _linesPerPage && pos < _textLen) {
        WebWrapResult wrap = webFindLineBreak(_textBuffer, _textLen, pos, _charsPerLine);
        if (wrap.nextStart <= pos) {
          pos = _textLen; // Safety: prevent infinite loop
          break;
        }
        pos = wrap.nextStart;
        lineCount++;
      }
      if (pos < _textLen) {
        _pageOffsets.push_back(pos);
      }
    }

    _totalPages = _pageOffsets.size();
    _currentPage = 0;
  }

  // ---- Bookmarks & History ----

  void loadBookmarks() {
    _bookmarks.clear();
    File f = SD.open(WEB_BOOKMARKS_FILE, FILE_READ);
    if (!f) { digitalWrite(SDCARD_CS, HIGH); return; }
    while (f.available() && _bookmarks.size() < WEB_MAX_BOOKMARKS) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) _bookmarks.push_back(line);
    }
    f.close();
    digitalWrite(SDCARD_CS, HIGH);
  }

  void saveBookmarks() {
    if (!SD.exists(WEB_CACHE_DIR)) SD.mkdir(WEB_CACHE_DIR);
    File f = SD.open(WEB_BOOKMARKS_FILE, FILE_WRITE);
    if (!f) return;
    for (auto& b : _bookmarks) {
      f.println(b);
    }
    f.close();
    digitalWrite(SDCARD_CS, HIGH);
  }

  void addBookmark(const char* url) {
    // Remove if already exists
    for (int i = 0; i < (int)_bookmarks.size(); i++) {
      if (_bookmarks[i] == url) {
        _bookmarks.erase(_bookmarks.begin() + i);
        break;
      }
    }
    // Add at front
    _bookmarks.insert(_bookmarks.begin(), String(url));
    if (_bookmarks.size() > WEB_MAX_BOOKMARKS) _bookmarks.pop_back();
    saveBookmarks();
  }

  void loadHistory() {
    _history.clear();
    File f = SD.open(WEB_HISTORY_FILE, FILE_READ);
    if (!f) { digitalWrite(SDCARD_CS, HIGH); return; }
    while (f.available() && _history.size() < WEB_MAX_HISTORY) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) _history.push_back(line);
    }
    f.close();
    digitalWrite(SDCARD_CS, HIGH);
  }

  void saveHistory() {
    if (!SD.exists(WEB_CACHE_DIR)) SD.mkdir(WEB_CACHE_DIR);
    File f = SD.open(WEB_HISTORY_FILE, FILE_WRITE);
    if (!f) return;
    for (auto& h : _history) {
      f.println(h);
    }
    f.close();
    digitalWrite(SDCARD_CS, HIGH);
  }

  void addToHistory(const char* url) {
    // Remove duplicates
    for (int i = 0; i < (int)_history.size(); i++) {
      if (_history[i] == url) {
        _history.erase(_history.begin() + i);
        break;
      }
    }
    _history.insert(_history.begin(), String(url));
    if (_history.size() > WEB_MAX_HISTORY) _history.pop_back();
    saveHistory();
  }

  // ---- Rendering Helpers ----

  void renderWifiSetup(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("WiFi Setup");
    display.drawRect(0, 11, display.width(), 1);

    display.setColor(DisplayDriver::LIGHT);
    display.setTextSize(0);

    if (_wifiState == WIFI_SCANNING) {
      display.setCursor(0, 18);
      display.print("Scanning for networks...");
    } else if (_wifiState == WIFI_SCAN_DONE) {
      int y = 14;
      int listLineH = 8;
      for (int i = 0; i < _ssidCount && y < display.height() - 24; i++) {
        bool selected = (i == _selectedSSID);
        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y + 5, display.width(), listLineH);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(0, y);
        String line = selected ? "> " : "  ";
        line += _ssidList[i];
        if ((int)line.length() > _charsPerLine)
          line = line.substring(0, _charsPerLine - 3) + "...";
        display.print(line.c_str());
        y += listLineH;
      }
    } else if (_wifiState == WIFI_ENTERING_PASS) {
      int y = 14;
      display.setCursor(0, y);
      display.setColor(DisplayDriver::LIGHT);
      char tmp[64];
      snprintf(tmp, sizeof(tmp), "SSID: %s", _ssidList[_selectedSSID].c_str());
      display.print(tmp);
      y += 12;
      display.setCursor(0, y);
      display.print("Password:");
      y += 10;
      display.setCursor(0, y);
      // Show masked password with brief reveal of last char
      char passBuf[WEB_WIFI_PASS_LEN + 2];
      for (int pi = 0; pi < _wifiPassLen; pi++) {
        passBuf[pi] = '*';
      }
      // Brief reveal: show last char for ~800ms after typing
      if (_wifiPassLen > 0 && _formLastCharAt > 0 &&
          (millis() - _formLastCharAt) < 800) {
        passBuf[_wifiPassLen - 1] = _wifiPass[_wifiPassLen - 1];
      }
      passBuf[_wifiPassLen] = '_'; // Cursor
      passBuf[_wifiPassLen + 1] = '\0';
      display.print(passBuf);
    } else if (_wifiState == WIFI_CONNECTING) {
      display.setCursor(0, 18);
      display.print("Connecting...");
      display.setCursor(0, 30);
      char tmp[48];
      snprintf(tmp, sizeof(tmp), "SSID: %s", _connectedSSID.c_str());
      display.print(tmp);
    } else if (_wifiState == WIFI_FAILED) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, 18);
      display.print("WiFi Error:");
      display.setCursor(0, 30);
      display.setColor(DisplayDriver::LIGHT);
      // Word-wrap the error message
      String err = _fetchError;
      int y2 = 30;
      while (err.length() > 0 && y2 < 70) {
        String line;
        if ((int)err.length() <= _charsPerLine) {
          line = err; err = "";
        } else {
          line = err.substring(0, _charsPerLine);
          err = err.substring(_charsPerLine);
        }
        display.setCursor(0, y2);
        display.print(line.c_str());
        y2 += 8;
      }
      display.setCursor(0, 80);
      display.setColor(DisplayDriver::GREEN);
      display.print("Enter: Retry  Q: Back");
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("Q:Back W/S:Nav Ent:Select");
  }

  void renderHome(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);
    display.setCursor(0, 0);

    if (isNetworkAvailable()) {
      display.print("Web Reader");
      // Show connection indicator on right
      display.setTextSize(0);
      display.setColor(DisplayDriver::GREEN);
      if (isWiFiConnected()) {
        IPAddress ip = WiFi.localIP();
        char ipStr[20];
        snprintf(ipStr, sizeof(ipStr), "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
        display.setCursor(display.width() - display.getTextWidth(ipStr) - 2, -3);
        display.print(ipStr);
      } else {
        // PPP/cellular connection (future)
        const char* netStr = "4G";
        display.setCursor(display.width() - display.getTextWidth(netStr) - 2, -3);
        display.print(netStr);
      }
    } else {
      display.print("Web Reader (Offline)");
    }

    display.setTextSize(1);
    display.drawRect(0, 11, display.width(), 1);

    display.setTextSize(0);
    int y = 14;
    int listLineH = 8;
    int itemIdx = 0;

    // IRC Chat (item 0)
    {
      bool selected = (_homeSelected == itemIdx);
      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + 5, display.width(), listLineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::GREEN);
      }
      display.setCursor(0, y);
      if (_ircConnected && _ircJoined) {
        char ircLabel[80];
        snprintf(ircLabel, sizeof(ircLabel), "IRC: %s [connected]", _ircChannel);
        display.print(ircLabel);
      } else if (_ircConnected) {
        display.print("IRC: connecting...");
      } else {
        char ircLabel[80];
        snprintf(ircLabel, sizeof(ircLabel), "IRC: %s:%d", _ircHost, _ircPort);
        display.print(ircLabel);
      }
      y += listLineH + 2;
      itemIdx++;
    }

    // Separator between IRC and Web sections
    display.setColor(DisplayDriver::GREEN);
    display.drawRect(0, y + 5, display.width(), 1);
    y += 8;

    // URL bar (item 1)
    {
      bool selected = (_homeSelected == itemIdx);
      if (selected) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + 5, display.width(), listLineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.setCursor(0, y);
      if (_urlEditing) {
        // Show URL with cursor
        char urlDisp[WEB_MAX_URL_LEN + 2];
        int maxShow = _charsPerLine - 5;
        int start = 0;
        if (_urlLen > maxShow) start = _urlLen - maxShow;
        snprintf(urlDisp, sizeof(urlDisp), "Web: %s_", _urlBuffer + start);
        display.print(urlDisp);
      } else if (_urlLen > 0) {
        char urlDisp[80];
        snprintf(urlDisp, sizeof(urlDisp), "Web: %s",
                 _urlLen > _charsPerLine - 5 ?
                   (_urlBuffer + _urlLen - _charsPerLine + 5) : _urlBuffer);
        display.print(urlDisp);
      } else {
        display.print("Web: [Enter URL]");
      }
      y += listLineH + 2;
      itemIdx++;
    }

    // Bookmarks section
    if (_bookmarks.size() > 0) {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, y);
      display.print("-- Bookmarks --");
      y += listLineH;

      for (int i = 0; i < (int)_bookmarks.size() && y < display.height() - 35; i++) {
        bool selected = (_homeSelected == itemIdx);
        const char* url = _bookmarks[i].c_str();
        int urlLen = strlen(url);
        int maxChars = _charsPerLine - 2;

        int numLines = (urlLen + maxChars - 1) / maxChars;
        if (numLines < 1) numLines = 1;

        if (selected) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y + 5, display.width(), listLineH * numLines);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        int off = 0;
        for (int ln = 0; ln < numLines && y < display.height() - 35; ln++) {
          display.setCursor(0, y);
          char lineBuf[128];
          const char* prefix = (ln == 0) ? (selected ? "> " : "  ") : "  ";
          int charsThisLine = maxChars;
          if (urlLen - off < charsThisLine) charsThisLine = urlLen - off;
          snprintf(lineBuf, sizeof(lineBuf), "%s%.*s", prefix, charsThisLine, url + off);
          display.print(lineBuf);
          off += charsThisLine;
          y += listLineH;
        }
        itemIdx++;
      }
    }

    // History section
    if (_history.size() > 0 && y < display.height() - 24) {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, y);
      display.print("-- History --");
      y += listLineH;

      for (int i = 0; i < (int)_history.size() && y < display.height() - 24; i++) {
        bool selected = (_homeSelected == itemIdx);
        const char* url = _history[i].c_str();
        int urlLen = strlen(url);
        int maxChars = _charsPerLine - 2;  // Account for "> " prefix

        // Calculate how many lines this URL needs
        int numLines = (urlLen + maxChars - 1) / maxChars;
        if (numLines < 1) numLines = 1;

        if (selected) {
          // Multi-line highlight
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y + 5, display.width(), listLineH * numLines);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        // Render URL across multiple lines
        int off = 0;
        for (int ln = 0; ln < numLines && y < display.height() - 24; ln++) {
          display.setCursor(0, y);
          char lineBuf[128];
          const char* prefix = (ln == 0) ? (selected ? "> " : "  ") : "  ";
          int charsThisLine = maxChars;
          if (urlLen - off < charsThisLine) charsThisLine = urlLen - off;
          snprintf(lineBuf, sizeof(lineBuf), "%s%.*s", prefix, charsThisLine, url + off);
          display.print(lineBuf);
          off += charsThisLine;
          y += listLineH;
        }
        itemIdx++;
      }
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    if (_urlEditing) {
      display.print("Type URL  Ent:Go");
    } else {
      char footerBuf[48];
      bool hasData = (_cookieCount > 0 || !_history.empty());
      if (hasData)
        snprintf(footerBuf, sizeof(footerBuf), "Q:Bk W/S Ent:Go X:Clr");
      else
        snprintf(footerBuf, sizeof(footerBuf), "Q:Bk W/S:Nav Ent:Go");
      display.print(footerBuf);
    }
  }

  void renderFetching(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(2);
    display.setCursor(10, 20);
    display.print("Loading...");

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(10, 45);

    // Show truncated URL
    char urlDisp[40];
    strncpy(urlDisp, _urlBuffer, 38);
    urlDisp[38] = '\0';
    display.print(urlDisp);

    display.setCursor(10, 60);
    char progBuf[40];
    int elapsed = (int)((millis() - _fetchStartTime) / 1000);
    if (_fetchProgress > 0) {
      snprintf(progBuf, sizeof(progBuf), "%d bytes  (%ds)", _fetchProgress, elapsed);
    } else if (elapsed >= 2) {
      snprintf(progBuf, sizeof(progBuf), "Connecting... %ds", elapsed);
    } else {
      snprintf(progBuf, sizeof(progBuf), "Connecting...");
    }
    display.print(progBuf);
  }

  void renderReading(DisplayDriver& display) {
    if (!_textBuffer || _textLen == 0) {
      display.setCursor(0, 20);
      display.setColor(DisplayDriver::LIGHT);
      display.print("No content");
      return;
    }

    display.setTextSize(0);
    display.setColor(DisplayDriver::LIGHT);

    // Determine page bounds
    int pageStart = (_currentPage < (int)_pageOffsets.size()) ?
                    _pageOffsets[_currentPage] : 0;
    int pageEnd = (_currentPage + 1 < (int)_pageOffsets.size()) ?
                  _pageOffsets[_currentPage + 1] : _textLen;

    int y = 0;
    int lineCount = 0;
    int pos = pageStart;
    int maxY = display.height() - _footerHeight - _lineHeight;

    while (pos < pageEnd && lineCount < _linesPerPage && y <= maxY) {
      int oldPos = pos;
      WebWrapResult wrap = webFindLineBreak(_textBuffer, pageEnd, pos, _charsPerLine);

      if (wrap.nextStart <= oldPos && wrap.lineEnd >= pageEnd) break;

      display.setCursor(0, y);

      // Render characters with UTF-8/CP437 handling
      char charStr[2] = {0, 0};
      int j = pos;
      while (j < wrap.lineEnd && j < pageEnd) {
        uint8_t b = (uint8_t)_textBuffer[j];

        if (b < 32) { j++; continue; }

        // Detect link markers [N] and render in different color
        if (b == '[' && j + 1 < pageEnd) {
          // Check if this is a link number [N] or [NN]
          int k = j + 1;
          bool isNum = true;
          while (k < pageEnd && _textBuffer[k] >= '0' && _textBuffer[k] <= '9') k++;
          if (k > j + 1 && k < pageEnd && _textBuffer[k] == ']') {
            // It's a link marker - render in highlight color
            display.setColor(DisplayDriver::GREEN);
            while (j <= k) {
              charStr[0] = _textBuffer[j++];
              display.print(charStr);
            }
            display.setColor(DisplayDriver::LIGHT);
            continue;
          }
          // Check for form hint [f: ...]
          if (_textBuffer[j+1] == 'f' && j + 2 < pageEnd && _textBuffer[j+2] == ':') {
            display.setColor(DisplayDriver::GREEN);
            while (j < pageEnd && _textBuffer[j] != ']') {
              charStr[0] = _textBuffer[j++];
              display.print(charStr);
            }
            if (j < pageEnd) { charStr[0] = ']'; display.print(charStr); j++; }
            display.setColor(DisplayDriver::LIGHT);
            continue;
          }
        }

        // Detect form markers {FN} and render highlighted
        if (b == '{' && j + 1 < pageEnd && _textBuffer[j+1] == 'F') {
          int k = j + 2;
          while (k < pageEnd && _textBuffer[k] >= '0' && _textBuffer[k] <= '9') k++;
          if (k > j + 2 && k < pageEnd && _textBuffer[k] == '}') {
            display.setColor(DisplayDriver::YELLOW);
            while (j <= k) {
              charStr[0] = _textBuffer[j++];
              display.print(charStr);
            }
            display.setColor(DisplayDriver::LIGHT);
            continue;
          }
        }

        if (b < 0x80) {
          charStr[0] = (char)b;
          display.print(charStr);
          j++;
        } else if (b >= 0xC0) {
          uint32_t cp = decodeUtf8Char(_textBuffer, wrap.lineEnd, &j);
          uint8_t glyph = unicodeToCP437(cp);
          if (glyph) {
            charStr[0] = (char)glyph;
            display.print(charStr);
          }
        } else {
          charStr[0] = (char)b;
          display.print(charStr);
          j++;
        }
      }

      y += _lineHeight;
      lineCount++;
      pos = wrap.nextStart;
      if (pos >= pageEnd) break;
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);

    // Page counter on left
    char pageBuf[20];
    snprintf(pageBuf, sizeof(pageBuf), "%d/%d", _currentPage + 1, _totalPages);
    display.setCursor(0, footerY);
    display.print(pageBuf);

    // Navigation hint on right (compact to fit setTextSize 1)
    const char* hint;
    char linkBuf[32];
    if (_linkInputActive) {
      snprintf(linkBuf, sizeof(linkBuf), "#%d_ Ent:Go", _linkInput);
      hint = linkBuf;
    } else if (_formCount > 0 && _linkCount > 0) {
      hint = "L:Lnk F:Frm B:Bk Q:X";
    } else if (_formCount > 0) {
      hint = "F:Frm B:Bk Q:X";
    } else if (_linkCount > 0) {
      hint = "L:Lnk B:Bk Q:X";
    } else {
      hint = "B:Bk Q:X";
    }
    display.setCursor(display.width() - display.getTextWidth(hint) - 2, footerY);
    display.print(hint);

    // Toast notification overlay
    if (_toastMsg[0] && (millis() - _toastTime < 1500)) {
      display.setTextSize(1);
      int tw = display.getTextWidth(_toastMsg);
      int bw = tw + 16;
      int bh = 20;
      int bx = (display.width() - bw) / 2;
      int by = (display.height() - bh) / 2;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, 1);
      display.drawRect(bx, by + bh - 1, bw, 1);
      display.drawRect(bx, by, 1, bh);
      display.drawRect(bx + bw - 1, by, 1, bh);
      display.setCursor(bx + 8, by + 5);
      display.print(_toastMsg);
    } else if (_toastMsg[0]) {
      _toastMsg[0] = '\0';  // Clear expired toast
    }
  }

  // ---- Layout Initialization ----

  void initLayout(DisplayDriver& display) {
    if (_initialized) return;

    display.setTextSize(0);
    uint16_t mWidth = display.getTextWidth("M");
    if (mWidth > 0) {
      _charsPerLine = display.width() / mWidth;
      _lineHeight = max(3, (int)((mWidth * 7 * 12) / (6 * 10)));
    } else {
      _charsPerLine = 40;
      _lineHeight = 5;
    }

    _footerHeight = 14;
    int textAreaHeight = display.height() - _footerHeight;
    _linesPerPage = textAreaHeight / _lineHeight;
    if (_linesPerPage < 5) _linesPerPage = 5;
    if (_linesPerPage > 40) _linesPerPage = 40;

    display.setTextSize(1);
    _initialized = true;

    Serial.printf("WebReader layout: %d chars/line, %d lines/page, lineH=%d\n",
                  _charsPerLine, _linesPerPage, _lineHeight);
  }

  // ---- Input Handlers ----

  bool handleWifiInput(char c) {
    if (_wifiState == WIFI_SCAN_DONE) {
      if (c == 'w' || c == 'W' || c == 0xF2) {
        if (_selectedSSID > 0) _selectedSSID--;
        return true;
      }
      if (c == 's' || c == 'S' || c == 0xF1) {
        if (_selectedSSID < _ssidCount - 1) _selectedSSID++;
        return true;
      }
      if (c == '\r' || c == 13) {
        _wifiState = WIFI_ENTERING_PASS;
        _wifiPassLen = 0;
        _wifiPass[0] = '\0';
        return true;
      }
    } else if (_wifiState == WIFI_ENTERING_PASS) {
      // Text entry for password
      if (c == '\r' || c == 13) {
        // Connect
        connectToSSID(_ssidList[_selectedSSID], _wifiPass);
        return true;
      }
      if (c == '\b' || c == 127) {
        if (_wifiPassLen > 0) {
          _wifiPass[--_wifiPassLen] = '\0';
          _formLastCharAt = 0;  // Clear reveal on delete
        }
        return true;
      }
      if (c >= 32 && c < 127 && _wifiPassLen < WEB_WIFI_PASS_LEN - 1) {
        _wifiPass[_wifiPassLen++] = c;
        _wifiPass[_wifiPassLen] = '\0';
        _formLastCharAt = millis();  // Brief reveal timer
        return true;
      }
    } else if (_wifiState == WIFI_FAILED) {
      if (c == '\r' || c == 13) {
        startWifiScan();
        return true;
      }
    } else if (_wifiState == WIFI_CONNECTED) {
      // Any key goes to home
      _mode = HOME;
      return true;
    }

    // Q - back to home (if possible) or exit
    if (c == 'q' || c == 'Q') {
      if (_wifiState == WIFI_ENTERING_PASS) {
        _wifiState = WIFI_SCAN_DONE;
      } else {
        _mode = HOME;
      }
      return true;
    }

    return false;
  }

  bool handleHomeInput(char c) {
    int totalItems = 2 + _bookmarks.size() + _history.size(); // IRC + URL + bookmarks + history

    if (_urlEditing) {
      // URL text entry mode
      if (c == '\r' || c == 13) {
        if (_urlLen > 0) {
          // Auto-add https:// if no scheme
          if (strncmp(_urlBuffer, "http://", 7) != 0 &&
              strncmp(_urlBuffer, "https://", 8) != 0) {
            char tmp[WEB_MAX_URL_LEN];
            snprintf(tmp, WEB_MAX_URL_LEN, "https://%s", _urlBuffer);
            strncpy(_urlBuffer, tmp, WEB_MAX_URL_LEN - 1);
            _urlLen = strlen(_urlBuffer);
          }
          // Encode spaces as %20
          encodeUrlSpaces(_urlBuffer, WEB_MAX_URL_LEN);
          _urlLen = strlen(_urlBuffer);
          _urlEditing = false;
          if (!isNetworkAvailable()) {
            _mode = WIFI_SETUP;
            if (!loadAndAutoConnect()) {
              startWifiScan();
            } else {
              fetchWithSelfRef(_urlBuffer);
            }
          } else {
            fetchWithSelfRef(_urlBuffer);
          }
        }
        return true;
      }
      if (c == '\b' || c == 127) {
        if (_urlLen > 0) {
          _urlBuffer[--_urlLen] = '\0';
        }
        return true;
      }
      if (c == 'q' && _urlLen == 0) {
        // Q exits URL editing when empty
        _urlEditing = false;
        return true;
      }
      // Escape URL editing mode
      if (c == 0x1B) { // ESC
        _urlEditing = false;
        return true;
      }
      if (c >= 32 && c < 127 && _urlLen < WEB_MAX_URL_LEN - 1) {
        _urlBuffer[_urlLen++] = c;
        _urlBuffer[_urlLen] = '\0';
        return true;
      }
      return true; // Consume all keys in editing mode
    }

    // Normal navigation
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_homeSelected > 0) _homeSelected--;
      return true;
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_homeSelected < totalItems - 1) _homeSelected++;
      return true;
    }
    if (c == '\r' || c == 13) {
      if (_homeSelected == 0) {
        // IRC Chat selected
        if (_ircConnected) {
          // Already connected — go straight to chat
          _mode = IRC_CHAT;
          _ircComposing = false;
          _ircScrollPos = 0;
        } else {
          // Open IRC setup
          _mode = IRC_SETUP;
          _ircSetupField = 0;
          _ircSetupEditing = false;
        }
        return true;
      }
      if (_homeSelected == 1) {
        // Activate URL editing
        _urlEditing = true;
        return true;
      }
      // Bookmark or history item selected (offset by 2 for IRC + URL)
      const char* selectedUrl = nullptr;
      int bmIdx = _homeSelected - 2;
      if (bmIdx < (int)_bookmarks.size()) {
        selectedUrl = _bookmarks[bmIdx].c_str();
      } else {
        int histIdx = bmIdx - _bookmarks.size();
        if (histIdx < (int)_history.size()) {
          selectedUrl = _history[histIdx].c_str();
        }
      }
      if (selectedUrl) {
        strncpy(_urlBuffer, selectedUrl, WEB_MAX_URL_LEN - 1);
        _urlLen = strlen(_urlBuffer);
        if (!isNetworkAvailable()) {
          _mode = WIFI_SETUP;
          if (!loadAndAutoConnect()) {
            startWifiScan();
          } else {
            fetchWithSelfRef(_urlBuffer);
          }
        } else {
          fetchWithSelfRef(_urlBuffer);
        }
      }
      return true;
    }

    // X - clear all cookies
    if (c == 'x' || c == 'X') {
      bool hadData = (_cookieCount > 0 || !_history.empty());
      _cookieCount = 0;
      memset(_cookies, 0, sizeof(_cookies));
      _history.clear();
      saveHistory();
      Serial.println("WebReader: Cookies and history cleared");
      return hadData;
    }

    return false;
  }

  bool handleReadingInput(char c) {
    // Link number input mode - accumulate digits, Enter to follow
    if (_linkInputActive) {
      if (c >= '0' && c <= '9') {
        int newVal = _linkInput * 10 + (c - '0');
        if (newVal <= 999) {
          _linkInput = newVal;
        }
        return true;
      }
      if (c == '\r' || c == 13) {
        // Enter confirms the link
        if (_linkInput > 0 && _linkInput <= _linkCount) {
          _linkInputActive = false;
          WebLink& link = _links[_linkInput - 1];
          strncpy(_urlBuffer, link.url, WEB_MAX_URL_LEN - 1);
          _urlLen = strlen(_urlBuffer);
          fetchPage(_urlBuffer, nullptr, nullptr, _currentUrl);
        } else {
          _linkInputActive = false;
          _linkInput = 0;
        }
        return true;
      }
      if (c == '\b' || c == 127) {
        // Backspace removes last digit
        _linkInput /= 10;
        if (_linkInput == 0) {
          _linkInputActive = false;
        }
        return true;
      }
      // Any other key cancels link input
      _linkInputActive = false;
      _linkInput = 0;
      return true;
    }

    // W/A - previous page
    if (c == 'w' || c == 'W' || c == 'a' || c == 'A' || c == 0xF2) {
      if (_currentPage > 0) {
        _currentPage--;
        return true;
      }
      return false;
    }

    // S/D/Space - next page
    if (c == 's' || c == 'S' || c == 'd' || c == 'D' || c == ' ' || c == 0xF1) {
      if (_currentPage < _totalPages - 1) {
        _currentPage++;
        return true;
      }
      return false;
    }

    // L - enter link selection mode
    if (c == 'l' || c == 'L') {
      if (_linkCount > 0) {
        _linkInputActive = true;
        _linkInput = 0;
        return true;
      }
      return false;
    }

    // G - go to URL (back to home to enter a new URL)
    if (c == 'g' || c == 'G') {
      _mode = HOME;
      _homeSelected = 0;
      return true;
    }

    // K - add current page to bookmarks
    if (c == 'k' || c == 'K') {
      if (_currentUrl[0]) {
        addBookmark(_currentUrl);
        strncpy(_toastMsg, "Bookmarked!", sizeof(_toastMsg));
        _toastTime = millis();
        return true;
      }
      return false;
    }

    // B - back to previous page
    if (c == 'b' || c == 'B') {
      if (!_backHistory.empty()) {
        String prevUrl = _backHistory.back();
        _backHistory.pop_back();
        // Temporarily clear _currentUrl so fetchPage doesn't re-push it
        _currentUrl[0] = '\0';
        strncpy(_urlBuffer, prevUrl.c_str(), WEB_MAX_URL_LEN - 1);
        _urlLen = strlen(_urlBuffer);
        fetchWithSelfRef(_urlBuffer);
      } else {
        _mode = HOME;
        _homeSelected = 0;
      }
      return true;
    }

    // Q - exit to home
    if (c == 'q' || c == 'Q') {
      _mode = HOME;
      _homeSelected = 0;
      return true;
    }

    // F - enter form fill mode (if page has forms)
    if (c == 'f' || c == 'F') {
      if (_formCount > 0) {
        // If only one form, select it directly
        _activeForm = 0;
        _activeField = 0;
        _formFieldEditing = false;
        _mode = FORM_FILL;
        return true;
      }
      return false;
    }

    // Enter - if links exist, enter link mode (same as L)
    if ((c == '\r' || c == 13) && _linkCount > 0) {
      _linkInputActive = true;
      _linkInput = 0;
      return true;
    }

    return false;
  }

  // Get the actual field index for the n-th visible (non-hidden) field
  int getVisibleFieldIdx(const WebForm& form, int visIdx) {
    int vis = 0;
    for (int i = 0; i < form.fieldCount; i++) {
      if (form.fields[i].type != 'h') {
        if (vis == visIdx) return i;
        vis++;
      }
    }
    return -1;
  }

  int getVisibleFieldCount(const WebForm& form) {
    int count = 0;
    for (int i = 0; i < form.fieldCount; i++) {
      if (form.fields[i].type != 'h') count++;
    }
    return count;
  }

  void renderFormFill(DisplayDriver& display) {
    if (_activeForm < 0 || _activeForm >= _formCount) return;
    WebForm& form = _forms[_activeForm];

    display.setTextSize(0);

    // Header
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    // Show form number if multiple forms
    if (_formCount > 1) {
      char hdr[40];
      snprintf(hdr, sizeof(hdr), "Form %d/%d", _activeForm + 1, _formCount);
      display.print(hdr);
    } else {
      display.print("Form");
    }

    // Show form action domain on right
    char actionDomain[32];
    extractDomain(form.action, actionDomain, sizeof(actionDomain));
    display.setCursor(display.width() - display.getTextWidth(actionDomain) - 2, 0);
    display.print(actionDomain);

    display.drawRect(0, 9, display.width(), 1);

    int y = 12;
    int lineH = 10; // Taller lines for form fields
    int visCount = getVisibleFieldCount(form);

    // Render each visible field
    int visIdx = 0;
    for (int i = 0; i < form.fieldCount && y < display.height() - 24; i++) {
      WebFormField& fld = form.fields[i];
      if (fld.type == 'h') continue; // Skip hidden fields

      bool isActive = (visIdx == _activeField);

      // Label
      display.setColor(isActive ? DisplayDriver::GREEN : DisplayDriver::YELLOW);
      display.setCursor(0, y);
      display.print(fld.label);
      y += 8;

      // Field value
      if (isActive) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + 4, display.width(), 9);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.setCursor(2, y);

      if (isActive && _formFieldEditing) {
        // Show edit buffer with cursor
        if (fld.type == 'p') {
          // Password - show last char briefly (800ms), rest as dots
          bool revealing = (_formEditLen > 0 && (millis() - _formLastCharAt) < 800);
          char masked[WEB_MAX_FIELD_VALUE + 2];
          for (int m = 0; m < _formEditLen; m++) {
            if (m == _formEditLen - 1 && revealing)
              masked[m] = _formEditBuf[m]; // Show last char
            else
              masked[m] = '*';
          }
          masked[_formEditLen] = '_'; // Cursor
          masked[_formEditLen + 1] = '\0';
          display.print(masked);
        } else {
          // Show text with cursor
          int maxShow = _charsPerLine - 2;
          int start = 0;
          if (_formEditLen > maxShow) start = _formEditLen - maxShow;
          char disp[WEB_MAX_FIELD_VALUE + 2];
          snprintf(disp, sizeof(disp), "%s_", _formEditBuf + start);
          display.print(disp);
        }
      } else if (fld.type == 's') {
        // Submit button
        display.setColor(isActive ? DisplayDriver::DARK : DisplayDriver::GREEN);
        char btn[60];
        snprintf(btn, sizeof(btn), "[ %s ]", fld.value[0] ? fld.value : "Submit");
        display.print(btn);
      } else if (fld.type == 'c') {
        // Checkbox
        display.print(fld.value[0] && strcmp(fld.value, "0") != 0 ? "[X]" : "[ ]");
      } else {
        // Text/password display (not editing)
        if (fld.type == 'p' && fld.value[0]) {
          String masked;
          int len = strlen(fld.value);
          for (int m = 0; m < len; m++) masked += '*';
          display.print(masked.c_str());
        } else if (fld.value[0]) {
          display.print(fld.value);
        } else {
          display.setColor(isActive ? DisplayDriver::DARK : DisplayDriver::YELLOW);
          display.print("(empty)");
        }
      }

      y += lineH;
      visIdx++;
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);

    if (_formFieldEditing) {
      display.print("Type text  Ent:Next Q:Undo");
    } else {
      const char* hint;
      if (_formCount > 1)
        hint = "W/S:Nav Ent:Edit </>:Form Q:Back";
      else
        hint = "W/S:Nav Ent:Edit/Go Q:Back";
      display.print(hint);
    }
  }

  bool handleFormFillInput(char c) {
    if (_activeForm < 0 || _activeForm >= _formCount) {
      _mode = READING;
      return true;
    }
    WebForm& form = _forms[_activeForm];
    int visCount = getVisibleFieldCount(form);
    if (visCount == 0) {
      _mode = READING;
      return true;
    }

    // --- Field editing mode ---
    if (_formFieldEditing) {
      int realIdx = getVisibleFieldIdx(form, _activeField);
      if (realIdx < 0) { _formFieldEditing = false; return true; }
      WebFormField& fld = form.fields[realIdx];

      // Enter - save field and advance to next field
      if (c == '\r' || c == 13) {
        memcpy(fld.value, _formEditBuf, _formEditLen);
        fld.value[_formEditLen] = '\0';
        _formFieldEditing = false;
        _formLastCharAt = 0;
        // Auto-advance to next field
        if (_activeField < visCount - 1) _activeField++;
        return true;
      }

      // Backspace
      if (c == 8 || c == 127 || c == 0xF3) {
        if (_formEditLen > 0) _formEditLen--;
        _formEditBuf[_formEditLen] = '\0';
        _formLastCharAt = 0;  // No reveal after delete
        return true;
      }

      // Q as cancel — discard edits, restore original value
      if ((c == 'q' || c == 'Q') && _formEditLen == 0) {
        _formFieldEditing = false;
        _formLastCharAt = 0;
        return true;
      }

      // Printable character
      if (c >= 32 && c < 127 && _formEditLen < WEB_MAX_FIELD_VALUE - 1) {
        _formEditBuf[_formEditLen++] = c;
        _formEditBuf[_formEditLen] = '\0';
        _formLastCharAt = millis();  // Start brief reveal
        return true;
      }
      return false;
    }

    // --- Navigation mode ---

    // W/Up - previous field
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_activeField > 0) _activeField--;
      return true;
    }

    // S/Down - next field
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_activeField < visCount - 1) _activeField++;
      return true;
    }

    // Enter - edit current field or submit
    if (c == '\r' || c == 13) {
      int realIdx = getVisibleFieldIdx(form, _activeField);
      if (realIdx < 0) return false;
      WebFormField& fld = form.fields[realIdx];

      if (fld.type == 's') {
        // Submit button - submit the form
        submitForm(_activeForm);
        return true;
      } else if (fld.type == 'c') {
        // Toggle checkbox
        if (fld.value[0] && strcmp(fld.value, "0") != 0)
          strcpy(fld.value, "0");
        else
          strcpy(fld.value, "1");
        return true;
      } else {
        // Text/password - enter editing mode
        _formFieldEditing = true;
        _formEditLen = strlen(fld.value);
        memcpy(_formEditBuf, fld.value, _formEditLen);
        _formEditBuf[_formEditLen] = '\0';
        return true;
      }
    }

    // < > or , . to switch between forms (when multiple)
    if ((c == ',' || c == '<') && _formCount > 1) {
      if (_activeForm > 0) {
        _activeForm--;
        _activeField = 0;
        _formFieldEditing = false;
      }
      return true;
    }
    if ((c == '.' || c == '>') && _formCount > 1) {
      if (_activeForm < _formCount - 1) {
        _activeForm++;
        _activeField = 0;
        _formFieldEditing = false;
      }
      return true;
    }

    // Q - back to reading
    if (c == 'q' || c == 'Q') {
      _mode = READING;
      _formFieldEditing = false;
      return true;
    }

    return false;
  }

  // ==========================================================================
  // IRC Client Implementation
  // ==========================================================================

  void loadIRCConfig() {
    // Default values
    strncpy(_ircHost, "irc.eastmesh.au", IRC_MAX_HOST_LEN);
    _ircPort = 6697;
    strncpy(_ircNick, "meck-user", IRC_MAX_NICK_LEN);
    _ircChannel[0] = '\0';  // No default channel — user must configure

    File f = SD.open(IRC_CONFIG_FILE, FILE_READ);
    if (!f) { digitalWrite(SDCARD_CS, HIGH); return; }

    String host = f.readStringUntil('\n'); host.trim();
    String port = f.readStringUntil('\n'); port.trim();
    String nick = f.readStringUntil('\n'); nick.trim();
    String chan = f.readStringUntil('\n'); chan.trim();
    f.close();
    digitalWrite(SDCARD_CS, HIGH);

    if (host.length() > 0) strncpy(_ircHost, host.c_str(), IRC_MAX_HOST_LEN - 1);
    if (port.length() > 0) _ircPort = port.toInt();
    if (nick.length() > 0) strncpy(_ircNick, nick.c_str(), IRC_MAX_NICK_LEN - 1);
    if (chan.length() > 0) strncpy(_ircChannel, chan.c_str(), IRC_MAX_CHANNEL_LEN - 1);

    Serial.printf("IRC: Config loaded - %s:%d nick=%s chan=%s\n",
                  _ircHost, _ircPort, _ircNick, _ircChannel);
  }

  void saveIRCConfig() {
    if (!SD.exists(WEB_CACHE_DIR)) SD.mkdir(WEB_CACHE_DIR);
    File f = SD.open(IRC_CONFIG_FILE, FILE_WRITE);
    if (f) {
      f.println(_ircHost);
      f.println(_ircPort);
      f.println(_ircNick);
      f.println(_ircChannel);
      f.close();
    }
    digitalWrite(SDCARD_CS, HIGH);
  }

  bool allocateIRCBuffers() {
    if (!_ircMessages) {
      _ircMessages = (IRCMessage*)ps_calloc(IRC_MAX_MESSAGES, sizeof(IRCMessage));
      if (!_ircMessages) {
        Serial.println("IRC: Failed to allocate message buffer");
        return false;
      }
    }
    return true;
  }

  void addIRCMessage(const char* nick, const char* text, bool isSystem = false) {
    if (!_ircMessages) return;
    IRCMessage& msg = _ircMessages[_ircMsgHead];
    strncpy(msg.nick, nick, IRC_MAX_NICK_LEN - 1);
    msg.nick[IRC_MAX_NICK_LEN - 1] = '\0';

    // Convert UTF-8 text to CP437 for proper e-ink rendering
    int srcLen = strlen(text);
    int dstIdx = 0;
    int srcPos = 0;
    while (srcPos < srcLen && dstIdx < IRC_MAX_MSG_LEN - 1) {
      uint32_t cp = decodeUtf8Char(text, srcLen, &srcPos);
      if (cp == 0 || cp == 0xFFFD) continue;  // Skip invalid
      if (cp < 128) {
        msg.text[dstIdx++] = (char)cp;
      } else {
        uint8_t glyph = unicodeToCP437(cp);
        msg.text[dstIdx++] = glyph ? (char)glyph : '?';
      }
    }
    msg.text[dstIdx] = '\0';

    msg.isSystem = isSystem;
    _ircMsgHead = (_ircMsgHead + 1) % IRC_MAX_MESSAGES;
    if (_ircMsgCount < IRC_MAX_MESSAGES) _ircMsgCount++;
    _ircDirty = true;
  }

  // Get message by index from oldest (0) to newest (count-1)
  IRCMessage* getIRCMessage(int idx) {
    if (!_ircMessages || idx < 0 || idx >= _ircMsgCount) return nullptr;
    int start = (_ircMsgHead - _ircMsgCount + IRC_MAX_MESSAGES) % IRC_MAX_MESSAGES;
    int actual = (start + idx) % IRC_MAX_MESSAGES;
    return &_ircMessages[actual];
  }

  void ircSendRaw(const char* line) {
    if (!_ircClient || !_ircClient->connected()) return;
    _ircClient->print(line);
    _ircClient->print("\r\n");
    Serial.printf("IRC TX: %s\n", line);
  }

  bool connectIRC() {
    if (!allocateIRCBuffers()) return false;

    addIRCMessage("*", "Connecting...", true);

    if (_ircClient) {
      _ircClient->stop();
      delete _ircClient;
    }

    _ircUseTLS = (_ircPort == 6697);

    if (_ircUseTLS) {
      // Redirect mbedTLS allocations to PSRAM — internal heap doesn't have
      // enough contiguous space (~32-48KB) for TLS handshake buffers.
      ensureTlsUsesPsram();
      auto* secClient = new WiFiClientSecure();
      secClient->setInsecure();  // Accept any cert (for self-signed IRC servers)
      _ircClient = secClient;
      Serial.printf("IRC: Connecting to %s:%d (TLS)...\n", _ircHost, _ircPort);
    } else {
      _ircClient = new WiFiClient();
      Serial.printf("IRC: Connecting to %s:%d (plain)...\n", _ircHost, _ircPort);
    }

    if (!_ircClient->connect(_ircHost, _ircPort, 10000)) {
      Serial.println("IRC: Connection failed");
      addIRCMessage("*", "Connection failed!", true);
      delete _ircClient;
      _ircClient = nullptr;
      _ircConnected = false;
      return false;
    }

    _ircConnected = true;
    _ircRegistered = false;
    _ircJoined = false;
    _ircLineLen = 0;
    _ircLastDataTime = millis();

    // Send registration
    char buf[256];
    snprintf(buf, sizeof(buf), "NICK %s", _ircNick);
    ircSendRaw(buf);
    snprintf(buf, sizeof(buf), "USER %s 0 * :Meck T-Deck Pro", _ircNick);
    ircSendRaw(buf);

    addIRCMessage("*", "Registering...", true);
    return true;
  }

  void disconnectIRC() {
    if (_ircClient) {
      if (_ircClient->connected()) {
        ircSendRaw("QUIT :Meck signing off");
      }
      _ircClient->stop();
      delete _ircClient;
      _ircClient = nullptr;
    }
    _ircConnected = false;
    _ircRegistered = false;
    _ircJoined = false;
    addIRCMessage("*", "Disconnected", true);
  }

  void parseIRCLine(const char* line) {
    Serial.printf("IRC RX: %s\n", line);
    _ircLastDataTime = millis();

    // PING :xxx → respond with PONG :xxx
    if (strncmp(line, "PING ", 5) == 0) {
      char pong[IRC_LINE_BUF_SIZE];
      snprintf(pong, sizeof(pong), "PONG %s", line + 5);
      ircSendRaw(pong);
      return;
    }

    // Lines starting with : are server/user messages
    if (line[0] != ':') return;

    // Parse prefix and command
    const char* p = line + 1;
    const char* prefixEnd = strchr(p, ' ');
    if (!prefixEnd) return;

    // Extract nick from prefix (nick!user@host)
    char senderNick[IRC_MAX_NICK_LEN] = {0};
    {
      int nLen = 0;
      const char* excl = strchr(p, '!');
      const char* end = excl ? excl : prefixEnd;
      int maxLen = end - p;
      if (maxLen > IRC_MAX_NICK_LEN - 1) maxLen = IRC_MAX_NICK_LEN - 1;
      memcpy(senderNick, p, maxLen);
      senderNick[maxLen] = '\0';
    }

    // Skip to command
    const char* cmd = prefixEnd + 1;
    while (*cmd == ' ') cmd++;

    // Check numeric replies
    if (cmd[0] >= '0' && cmd[0] <= '9' && cmd[1] >= '0' && cmd[1] <= '9') {
      int numeric = atoi(cmd);

      if (numeric == 1) {
        // RPL_WELCOME - registration complete
        _ircRegistered = true;

        // Only join if a channel is configured
        if (_ircChannel[0] == '\0') {
          addIRCMessage("*", "Registered! Use /join #channel", true);
          return;
        }
        // Auto-prefix with # if missing
        if (_ircChannel[0] != '#' && _ircChannel[0] != '&' && _ircChannel[0] != '+') {
          char tmp[IRC_MAX_CHANNEL_LEN];
          snprintf(tmp, sizeof(tmp), "#%s", _ircChannel);
          strncpy(_ircChannel, tmp, IRC_MAX_CHANNEL_LEN - 1);
        }

        char statusBuf[128];
        snprintf(statusBuf, sizeof(statusBuf), "Registered! Joining %s...", _ircChannel);
        addIRCMessage("*", statusBuf, true);

        char join[128];
        snprintf(join, sizeof(join), "JOIN %s", _ircChannel);
        ircSendRaw(join);
        return;
      }

      if (numeric == 433) {
        // Nick already in use — cycle last character (respects server NICKLEN)
        int nLen = strlen(_ircNick);
        if (nLen > 0) {
          char last = _ircNick[nLen - 1];
          if (last >= '0' && last < '9') {
            _ircNick[nLen - 1] = last + 1;  // meck-user1 -> meck-user2
          } else if (last == '9') {
            _ircNick[nLen - 1] = '0';        // wrap around
          } else {
            _ircNick[nLen - 1] = '1';         // meck-user -> meck-use1
          }
          char buf[128];
          snprintf(buf, sizeof(buf), "NICK %s", _ircNick);
          ircSendRaw(buf);
          char msgBuf[64];
          snprintf(msgBuf, sizeof(msgBuf), "Nick taken, trying %s", _ircNick);
          addIRCMessage("*", msgBuf, true);
        }
        return;
      }

      // MOTD and other informational numerics - show select ones
      if (numeric == 372 || numeric == 375 || numeric == 376) {
        // MOTD lines - extract text after the last :
        const char* text = strrchr(cmd, ':');
        if (text) {
          addIRCMessage("*", text + 1, true);
        }
        return;
      }

      // Topic (332) and names (353) - show as system messages
      if (numeric == 332 || numeric == 353) {
        const char* text = strrchr(cmd, ':');
        if (text) {
          addIRCMessage("*", text + 1, true);
        }
        return;
      }

      // Error numerics (400-599) - show as system messages
      if (numeric >= 400 && numeric < 600) {
        const char* text = strrchr(cmd, ':');
        if (text) {
          char errBuf[IRC_MAX_MSG_LEN];
          snprintf(errBuf, sizeof(errBuf), "Error %d: %s", numeric, text + 1);
          addIRCMessage("*", errBuf, true);
        }
        return;
      }

      return; // Skip other numerics
    }

    // PRIVMSG #channel :message
    if (strncmp(cmd, "PRIVMSG ", 8) == 0) {
      const char* target = cmd + 8;
      const char* msgText = strchr(target, ':');
      if (msgText) {
        msgText++; // skip the :

        // Check for CTCP ACTION (\001ACTION text\001)
        if (strncmp(msgText, "\001ACTION ", 8) == 0) {
          char actionBuf[IRC_MAX_MSG_LEN];
          const char* actionEnd = strchr(msgText + 8, '\001');
          int aLen = actionEnd ? (actionEnd - msgText - 8) : strlen(msgText + 8);
          if (aLen > IRC_MAX_MSG_LEN - 4) aLen = IRC_MAX_MSG_LEN - 4;
          snprintf(actionBuf, sizeof(actionBuf), "* %.*s", aLen, msgText + 8);
          addIRCMessage(senderNick, actionBuf);
        } else {
          addIRCMessage(senderNick, msgText);
        }
      }
      return;
    }

    // JOIN
    if (strncmp(cmd, "JOIN", 4) == 0) {
      if (strcmp(senderNick, _ircNick) == 0) {
        _ircJoined = true;
        char buf[128];
        snprintf(buf, sizeof(buf), "Joined %s", _ircChannel);
        addIRCMessage("*", buf, true);
      } else {
        char buf[128];
        snprintf(buf, sizeof(buf), "%s joined", senderNick);
        addIRCMessage("*", buf, true);
      }
      return;
    }

    // PART
    if (strncmp(cmd, "PART", 4) == 0) {
      char buf[128];
      snprintf(buf, sizeof(buf), "%s left", senderNick);
      addIRCMessage("*", buf, true);
      return;
    }

    // QUIT
    if (strncmp(cmd, "QUIT", 4) == 0) {
      char buf[128];
      const char* reason = strchr(cmd + 5, ':');
      if (reason) {
        snprintf(buf, sizeof(buf), "%s quit (%s)", senderNick, reason + 1);
      } else {
        snprintf(buf, sizeof(buf), "%s quit", senderNick);
      }
      addIRCMessage("*", buf, true);
      return;
    }

    // NICK change
    if (strncmp(cmd, "NICK", 4) == 0) {
      const char* newNick = strchr(cmd + 5, ':');
      if (!newNick) newNick = cmd + 5;
      else newNick++;
      char buf[128];
      snprintf(buf, sizeof(buf), "%s is now %s", senderNick, newNick);
      addIRCMessage("*", buf, true);

      // Update our own nick if it was us
      if (strcmp(senderNick, _ircNick) == 0) {
        strncpy(_ircNick, newNick, IRC_MAX_NICK_LEN - 1);
      }
      return;
    }
  }

  void pollIRC() {
    if (!_ircClient || !_ircConnected) {
      // Check for reconnect
      if (_ircReconnectAt > 0 && millis() > _ircReconnectAt) {
        _ircReconnectAt = 0;
        connectIRC();
      }
      return;
    }

    if (!_ircClient->connected()) {
      Serial.println("IRC: Connection lost");
      addIRCMessage("*", "Connection lost. Reconnecting...", true);
      _ircConnected = false;
      _ircRegistered = false;
      _ircJoined = false;
      delete _ircClient;
      _ircClient = nullptr;
      _ircReconnectAt = millis() + IRC_RECONNECT_MS;
      return;
    }

    // Check for ping timeout
    if (millis() - _ircLastDataTime > IRC_PING_TIMEOUT_MS) {
      Serial.println("IRC: Ping timeout");
      addIRCMessage("*", "Ping timeout. Reconnecting...", true);
      disconnectIRC();
      _ircReconnectAt = millis() + IRC_RECONNECT_MS;
      return;
    }

    // Read available data and accumulate lines
    while (_ircClient->available()) {
      char c = _ircClient->read();
      if (c == '\r') continue;  // skip CR
      if (c == '\n') {
        // Complete line received
        _ircLineBuf[_ircLineLen] = '\0';
        if (_ircLineLen > 0) {
          parseIRCLine(_ircLineBuf);
        }
        _ircLineLen = 0;
        continue;
      }
      if (_ircLineLen < IRC_LINE_BUF_SIZE - 1) {
        _ircLineBuf[_ircLineLen++] = c;
      }
    }
  }

  void sendIRCMessage() {
    if (!_ircConnected || _ircComposeLen == 0) return;

    _ircCompose[_ircComposeLen] = '\0';

    // Check for /commands (allowed even before joining a channel)
    if (_ircCompose[0] == '/') {
      if (strncmp(_ircCompose, "/me ", 4) == 0) {
        if (!_ircJoined) { addIRCMessage("*", "Not in a channel", true); }
        else {
          char buf[IRC_LINE_BUF_SIZE];
          snprintf(buf, sizeof(buf), "PRIVMSG %s :\001ACTION %s\001",
                   _ircChannel, _ircCompose + 4);
          ircSendRaw(buf);
          char dispBuf[IRC_MAX_MSG_LEN];
          snprintf(dispBuf, sizeof(dispBuf), "* %s", _ircCompose + 4);
          addIRCMessage(_ircNick, dispBuf);
        }
      } else if (strncmp(_ircCompose, "/nick ", 6) == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "NICK %s", _ircCompose + 6);
        ircSendRaw(buf);
      } else if (strncmp(_ircCompose, "/join ", 6) == 0) {
        const char* chan = _ircCompose + 6;
        // Auto-prefix with # if missing
        if (chan[0] != '#' && chan[0] != '&' && chan[0] != '+') {
          char tmp[IRC_MAX_CHANNEL_LEN];
          snprintf(tmp, sizeof(tmp), "#%s", chan);
          strncpy(_ircChannel, tmp, IRC_MAX_CHANNEL_LEN - 1);
        } else {
          strncpy(_ircChannel, chan, IRC_MAX_CHANNEL_LEN - 1);
        }
        char buf[128];
        snprintf(buf, sizeof(buf), "JOIN %s", _ircChannel);
        ircSendRaw(buf);
        _ircJoined = false;
      } else if (strcmp(_ircCompose, "/quit") == 0) {
        disconnectIRC();
        _mode = HOME;
        _homeSelected = 0;
      } else {
        // Send as raw IRC command (strip the /)
        ircSendRaw(_ircCompose + 1);
      }
    } else {
      // Normal message - requires being in a channel
      if (!_ircJoined) {
        addIRCMessage("*", "Not in a channel. Use /join #channel", true);
      } else {
        char buf[IRC_LINE_BUF_SIZE];
        snprintf(buf, sizeof(buf), "PRIVMSG %s :%s", _ircChannel, _ircCompose);
        ircSendRaw(buf);
        addIRCMessage(_ircNick, _ircCompose);
      }
    }

    _ircComposeLen = 0;
    _ircCompose[0] = '\0';
    _ircScrollPos = 0;  // Snap to bottom on send
  }

  // ---- IRC Setup Screen ----

  void renderIRCSetup(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("IRC Setup");
    display.drawRect(0, 11, display.width(), 1);

    display.setTextSize(0);
    int y = 16;
    int lineH = 10;

    const char* labels[] = {"Server:", "Port:", "Nick:", "Channel:", "[ Connect ]"};
    const char* chanDisp = (_ircChannel[0] != '\0') ? _ircChannel : "(none)";
    const char* values[] = {_ircHost, nullptr, _ircNick, chanDisp, nullptr};
    char portStr[8];
    snprintf(portStr, sizeof(portStr), "%d", _ircPort);

    for (int i = 0; i < 5; i++) {
      bool sel = (_ircSetupField == i);
      if (sel) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + 4, display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.setCursor(2, y);

      if (i == 4) {
        // Connect button
        display.setCursor(display.width() / 2 - 30, y);
        display.print(sel ? "> Connect <" : "[ Connect ]");
      } else if (i == 1) {
        // Port
        char buf[64];
        if (_ircSetupEditing && sel) {
          snprintf(buf, sizeof(buf), "%s %s_", labels[i], _ircSetupBuf);
        } else {
          snprintf(buf, sizeof(buf), "%s %s", labels[i], portStr);
        }
        display.print(buf);
      } else {
        char buf[128];
        if (_ircSetupEditing && sel) {
          snprintf(buf, sizeof(buf), "%s %s_", labels[i], _ircSetupBuf);
        } else {
          snprintf(buf, sizeof(buf), "%s %s", labels[i], values[i]);
        }
        display.print(buf);
      }
      y += lineH;
    }

    // Status
    y += 6;
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(2, y);
    if (_ircConnected) {
      display.print(_ircJoined ? "Connected & joined" : "Connected...");
    } else {
      display.print("Not connected");
    }

    // Footer
    display.setTextSize(1);
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setCursor(0, footerY);
    display.setColor(DisplayDriver::YELLOW);
    display.print("W/S:Nav Ent:Edit/Go Q:Back");
  }

  bool handleIRCSetupInput(char c) {
    if (_ircSetupEditing) {
      if (c == '\r' || c == 13) {
        // Save the field
        _ircSetupBuf[_ircSetupBufLen] = '\0';
        switch (_ircSetupField) {
          case 0: strncpy(_ircHost, _ircSetupBuf, IRC_MAX_HOST_LEN - 1); break;
          case 1: _ircPort = atoi(_ircSetupBuf); if (_ircPort == 0) _ircPort = 6697; break;
          case 2: strncpy(_ircNick, _ircSetupBuf, IRC_MAX_NICK_LEN - 1); break;
          case 3: strncpy(_ircChannel, _ircSetupBuf, IRC_MAX_CHANNEL_LEN - 1); break;
        }
        _ircSetupEditing = false;
        return true;
      }
      if (c == '\b' || c == 127) {
        if (_ircSetupBufLen > 0) _ircSetupBuf[--_ircSetupBufLen] = '\0';
        return true;
      }
      if (c == 0x1B) { _ircSetupEditing = false; return true; }
      if (c >= 32 && c < 127 && _ircSetupBufLen < (int)sizeof(_ircSetupBuf) - 1) {
        _ircSetupBuf[_ircSetupBufLen++] = c;
        _ircSetupBuf[_ircSetupBufLen] = '\0';
        return true;
      }
      return true;
    }

    // Navigation
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_ircSetupField > 0) _ircSetupField--;
      return true;
    }
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_ircSetupField < 4) _ircSetupField++;
      return true;
    }
    if (c == '\r' || c == 13) {
      if (_ircSetupField == 4) {
        // Connect button
        saveIRCConfig();
        if (!isNetworkAvailable()) {
          addIRCMessage("*", "No network! Connect WiFi first.", true);
          _mode = WIFI_SETUP;
          startWifiScan();
          return true;
        }
        if (connectIRC()) {
          _mode = IRC_CHAT;
          _ircComposing = false;
          _ircComposeLen = 0;
          _ircCompose[0] = '\0';
          _ircScrollPos = 0;
        }
        return true;
      }
      // Start editing the selected field
      _ircSetupEditing = true;
      switch (_ircSetupField) {
        case 0: strncpy(_ircSetupBuf, _ircHost, sizeof(_ircSetupBuf)); break;
        case 1: snprintf(_ircSetupBuf, sizeof(_ircSetupBuf), "%d", _ircPort); break;
        case 2: strncpy(_ircSetupBuf, _ircNick, sizeof(_ircSetupBuf)); break;
        case 3: strncpy(_ircSetupBuf, _ircChannel, sizeof(_ircSetupBuf)); break;
      }
      _ircSetupBufLen = strlen(_ircSetupBuf);
      return true;
    }
    if (c == 'q' || c == 'Q') {
      _mode = HOME;
      _homeSelected = 0;
      return true;
    }
    return false;
  }

  // ---- IRC Chat Screen ----

  void renderIRCChat(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);
    display.setCursor(0, 0);

    // Header: channel name + connection status
    char header[64];
    snprintf(header, sizeof(header), "%s", _ircChannel);
    display.print(header);

    // Connection indicator on right
    display.setTextSize(0);
    if (!_ircConnected) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 42, -3);
      display.print("DISCONN");
    } else if (!_ircJoined) {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(display.width() - 36, -3);
      display.print("joining");
    } else {
      display.setColor(DisplayDriver::GREEN);
      const char* nickDisp = _ircNick;
      display.setCursor(display.width() - display.getTextWidth(nickDisp) - 2, -3);
      display.print(nickDisp);
    }

    display.setTextSize(1);
    display.drawRect(0, 11, display.width(), 1);

    // Footer area
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setTextSize(1);

    if (_ircComposing) {
      // Compose text just above separator (tiny font to match messages)
      display.setTextSize(0);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, footerY - 12);
      char compDisp[IRC_COMPOSE_MAX + 4];
      int maxShow = _charsPerLine - 2;
      int start = 0;
      if (_ircComposeLen > maxShow) start = _ircComposeLen - maxShow;
      snprintf(compDisp, sizeof(compDisp), "> %s_", _ircCompose + start);
      display.print(compDisp);

      // Hint on footer line (large font)
      display.setTextSize(1);
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, footerY);
      display.print("Ent:Send Del:Exit");
    } else {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, footerY);
      display.print("Ent:Msg W/S:Scrl Q:Bk");
    }

    // Message area
    display.setTextSize(0);
    int msgAreaTop = 14;
    int msgAreaBottom = _ircComposing ? footerY - 16 : footerY - 4;
    int lineH = 8;
    int scrollBarW = 4;
    int lineW = _charsPerLine - 1;  // Reserve space for scroll bar
    _ircLinesPerPage = (msgAreaBottom - msgAreaTop) / lineH;

    if (_ircMsgCount == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(4, msgAreaTop + 10);
      display.print("No messages yet...");

      // Draw empty scrollbar track
      int sbX = display.width() - scrollBarW;
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(sbX, msgAreaTop, scrollBarW, msgAreaBottom - msgAreaTop);
      return;
    }

    // Calculate which messages to show (from bottom, with scroll offset)
    int startMsg = _ircMsgCount - _ircLinesPerPage - _ircScrollPos;
    if (startMsg < 0) startMsg = 0;
    int endMsg = startMsg + _ircLinesPerPage;
    if (endMsg > _ircMsgCount) endMsg = _ircMsgCount;

    int y = msgAreaTop;
    for (int i = startMsg; i < endMsg && y < msgAreaBottom; i++) {
      IRCMessage* msg = getIRCMessage(i);
      if (!msg) continue;

      display.setCursor(0, y);
      if (msg->isSystem) {
        display.setColor(DisplayDriver::YELLOW);
        char line[IRC_MAX_MSG_LEN + IRC_MAX_NICK_LEN + 4];
        snprintf(line, sizeof(line), "-- %s", msg->text);
        // Truncate to screen width (minus scrollbar)
        if ((int)strlen(line) > lineW)
          line[lineW] = '\0';
        display.print(line);
      } else {
        // Nick in green, message in light
        display.setColor(DisplayDriver::GREEN);
        char nickBuf[IRC_MAX_NICK_LEN + 2];
        snprintf(nickBuf, sizeof(nickBuf), "%s: ", msg->nick);
        display.print(nickBuf);

        display.setColor(DisplayDriver::LIGHT);
        int nickW = display.getTextWidth(nickBuf);
        int remainChars = lineW - strlen(nickBuf);
        if (remainChars > 0) {
          char textBuf[IRC_MAX_MSG_LEN];
          strncpy(textBuf, msg->text, remainChars);
          textBuf[remainChars] = '\0';
          display.print(textBuf);

          // If message is longer, wrap to next line
          int msgLen = strlen(msg->text);
          if (msgLen > remainChars && y + lineH < msgAreaBottom) {
            y += lineH;
            display.setCursor(0, y);
            int off = remainChars;
            while (off < msgLen && y < msgAreaBottom) {
              char wrapBuf[256];
              int wrapLen = lineW;
              if (msgLen - off < wrapLen) wrapLen = msgLen - off;
              memcpy(wrapBuf, msg->text + off, wrapLen);
              wrapBuf[wrapLen] = '\0';
              display.print(wrapBuf);
              off += wrapLen;
              if (off < msgLen) {
                y += lineH;
                display.setCursor(0, y);
              }
            }
          }
        }
      }
      y += lineH;
    }

    // --- Scroll bar (channel screen style) ---
    int sbX = display.width() - scrollBarW;
    int sbTop = msgAreaTop;
    int sbHeight = msgAreaBottom - msgAreaTop;

    // Draw track outline
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(sbX, sbTop, scrollBarW, sbHeight);

    if (_ircMsgCount > _ircLinesPerPage) {
      // Scrollable: draw proportional thumb
      int maxScroll = _ircMsgCount - _ircLinesPerPage;
      if (maxScroll < 1) maxScroll = 1;
      int thumbH = (_ircLinesPerPage * sbHeight) / _ircMsgCount;
      if (thumbH < 4) thumbH = 4;
      // _ircScrollPos=0 is newest (bottom), so invert for thumb position
      int thumbY = sbTop + ((maxScroll - _ircScrollPos) * (sbHeight - thumbH)) / maxScroll;
      for (int ty = thumbY + 1; ty < thumbY + thumbH - 1; ty++)
        display.drawRect(sbX + 1, ty, scrollBarW - 2, 1);
    } else {
      // All messages fit: fill entire track
      for (int ty = sbTop + 1; ty < sbTop + sbHeight - 1; ty++)
        display.drawRect(sbX + 1, ty, scrollBarW - 2, 1);
    }
  }

  bool handleIRCChatInput(char c) {
    if (_ircComposing) {
      if (c == '\r' || c == 13) {
        if (_ircComposeLen > 0) {
          sendIRCMessage();
        }
        // Always exit compose on Enter (after send or when empty)
        _ircComposing = false;
        return true;
      }
      if (c == '\b' || c == 127) {
        if (_ircComposeLen > 0) {
          _ircCompose[--_ircComposeLen] = '\0';
        } else {
          // Backspace on empty compose exits compose mode
          _ircComposing = false;
        }
        return true;
      }
      if (c == 0x1B) { // ESC exits compose
        _ircComposing = false;
        return true;
      }
      if (c >= 32 && c < 127 && _ircComposeLen < IRC_COMPOSE_MAX - 1) {
        _ircCompose[_ircComposeLen++] = c;
        _ircCompose[_ircComposeLen] = '\0';
        return true;
      }
      return true; // Consume all keys while composing
    }

    // Non-composing mode
    if (c == '\r' || c == 13) {
      _ircComposing = true;
      _ircComposeLen = 0;
      _ircCompose[0] = '\0';
      return true;
    }

    // W - scroll up (older messages)
    if (c == 'w' || c == 'W' || c == 0xF2) {
      int maxScroll = _ircMsgCount - _ircLinesPerPage;
      if (maxScroll < 0) maxScroll = 0;
      if (_ircScrollPos < maxScroll) _ircScrollPos++;
      return true;
    }

    // S - scroll down (newer messages)
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_ircScrollPos > 0) _ircScrollPos--;
      return true;
    }

    // Q - back to home (keep connection alive)
    if (c == 'q' || c == 'Q') {
      _mode = HOME;
      _homeSelected = 0;
      return true;
    }

    // X - disconnect
    if (c == 'x' || c == 'X') {
      disconnectIRC();
      _mode = HOME;
      _homeSelected = 0;
      return true;
    }

    return false;
  }

  bool isIRCSetupEditing() const {
    return _mode == IRC_SETUP && _ircSetupEditing;
  }

  bool isIRCComposing() const {
    return _mode == IRC_CHAT && _ircComposing;
  }

public:
  WebReaderScreen(UITask* task)
    : _task(task), _mode(HOME), _initialized(false), _display(nullptr),
      _charsPerLine(40), _linesPerPage(15), _lineHeight(5), _footerHeight(14),
      _wifiState(WIFI_IDLE), _ssidCount(0), _selectedSSID(0), _wifiPassLen(0),
      _urlLen(0), _urlCursor(0),
      _textBuffer(nullptr), _textLen(0), _links(nullptr), _linkCount(0),
      _currentPage(0), _totalPages(0),
      _homeSelected(0), _urlEditing(false),
      _linkInput(0), _linkInputActive(false),
      _formCount(0), _activeForm(-1), _activeField(0),
      _formFieldEditing(false), _formEditLen(0), _formLastCharAt(0), _cookieCount(0),
      _fetchStartTime(0), _fetchProgress(0),
      _ircClient(nullptr), _ircUseTLS(false), _ircConnected(false), _ircRegistered(false),
      _ircJoined(false), _ircPort(6697), _ircMessages(nullptr),
      _ircMsgHead(0), _ircMsgCount(0), _ircLineLen(0),
      _ircComposeLen(0), _ircComposing(false), _ircScrollPos(0), _ircLinesPerPage(12),
      _ircSetupField(0), _ircSetupBufLen(0), _ircSetupEditing(false),
      _ircLastDataTime(0), _ircReconnectAt(0),
      _ircDirty(false), _ircLastRender(0) {
    _urlBuffer[0] = '\0';
    _wifiPass[0] = '\0';
    _pageTitle[0] = '\0';
    _currentUrl[0] = '\0';
    _formEditBuf[0] = '\0';
    _ircHost[0] = '\0';
    _ircNick[0] = '\0';
    _ircChannel[0] = '\0';
    _ircCompose[0] = '\0';
    _ircSetupBuf[0] = '\0';
    _ircLineBuf[0] = '\0';
    memset(_forms, 0, sizeof(_forms));
    memset(_cookies, 0, sizeof(_cookies));
    loadIRCConfig();
  }

  ~WebReaderScreen() {
    freeBuffers();
    if (_ircClient) {
      if (_ircClient->connected()) _ircClient->stop();
      delete _ircClient;
      _ircClient = nullptr;
    }
    if (_ircMessages) { free(_ircMessages); _ircMessages = nullptr; }
  }

  // Called when entering the web reader screen
  void enter(DisplayDriver& display) {
    _display = &display;
    initLayout(display);
    loadBookmarks();
    loadHistory();

    // Check if already connected to a network
    if (isNetworkAvailable()) {
      _wifiState = WIFI_CONNECTED;
      _mode = (_textLen > 0) ? READING : HOME;
      Serial.printf("WebReader enter: already connected, mode=%d\n", _mode);
      return;
    }

    // Not connected — try auto-connect from saved credentials first.
    // Show a status screen during the blocking wait (up to 5s).
    Serial.println("WebReader enter: not connected, trying auto-connect");
    if (_display) {
      _display->startFrame();
      _display->setColor(DisplayDriver::GREEN);
      _display->setTextSize(1);
      _display->setCursor(0, 0);
      _display->print("Web Reader");
      _display->drawRect(0, 11, _display->width(), 1);
      _display->setColor(DisplayDriver::LIGHT);
      _display->setTextSize(0);
      _display->setCursor(0, 18);
      _display->print("Connecting to WiFi...");
      _display->endFrame();
    }
    if (loadAndAutoConnect()) {
      Serial.printf("WebReader enter: auto-connect OK\n");
      showConnectedAndGoHome();
      return;
    }

    // No saved credentials or auto-connect failed — prompt user for WiFi setup.
    // This must happen BEFORE showing the URL entry page so the user isn't
    // asked to type a URL they can't fetch.
    Serial.println("WebReader enter: auto-connect failed, starting WiFi setup");

    _mode = WIFI_SETUP;
    startWifiScan();  // Shows its own "Scanning..." splash, then blocks
    directRedraw();   // Replace splash with scan results or error screen
    Serial.printf("WebReader enter: mode=WIFI_SETUP, wifiState=%d\n", _wifiState);
  }

  // Called when leaving the screen
  void exitReader() {
    // Don't disconnect WiFi - keep it available
    // Don't free buffers - keep page content for when user returns
  }

  bool isReading() const { return _mode == READING; }
  bool isHome() const { return _mode == HOME; }
  bool isUrlEditing() const { return _urlEditing && _mode == HOME; }
  bool isWifiSetup() const { return _mode == WIFI_SETUP; }
  bool isPasswordEntry() const {
    return _mode == WIFI_SETUP && _wifiState == WIFI_ENTERING_PASS;
  }
  bool isFormFilling() const {
    return _mode == FORM_FILL && _formFieldEditing;
  }
  bool isIRCMode() const { return _mode == IRC_CHAT || _mode == IRC_SETUP; }
  bool isIRCTextEntry() const {
    return (_mode == IRC_CHAT && _ircComposing) ||
           (_mode == IRC_SETUP && _ircSetupEditing);
  }
  // Returns true if a password reveal is active and needs a refresh after expiry
  bool needsRevealRefresh() const {
    if (_formLastCharAt > 0 && (millis() - _formLastCharAt) < 900) {
      if (_mode == FORM_FILL && _formFieldEditing) return true;
      if (_mode == WIFI_SETUP && _wifiState == WIFI_ENTERING_PASS) return true;
    }
    return false;
  }

  // Direct render — bypasses UITask's render scheduling.
  // Called from main.cpp during URL/password text entry for responsive typing.
  void directRedraw() {
    if (!_display) return;
    _display->startFrame();
    render(*_display);
    _display->endFrame();
    _ircDirty = false;
    _ircLastRender = millis();
  }

  int render(DisplayDriver& display) override {
    switch (_mode) {
      case WIFI_SETUP:
        renderWifiSetup(display);
        return 1000;
      case HOME:
        renderHome(display);
        return 5000;
      case FETCHING:
        renderFetching(display);
        return 500;
      case READING:
        renderReading(display);
        return 5000;
      case LINK_SELECT:
        renderReading(display);
        return 1000;
      case FORM_FILL:
        renderFormFill(display);
        return 1000;
      case IRC_SETUP:
        renderIRCSetup(display);
        return 1000;
      case IRC_CHAT:
        renderIRCChat(display);
        return 500;  // Fast refresh for live chat
      default:
        return 5000;
    }
  }

  void poll() override {
    // Poll IRC connection (regardless of current mode, to keep connection alive)
    if (_ircConnected || _ircReconnectAt > 0) {
      pollIRC();
    }

    // Auto-render when new IRC messages arrive (e-ink throttled to ~1s)
    if (_ircDirty && _mode == IRC_CHAT && _display) {
      unsigned long now = millis();
      if (now - _ircLastRender >= 900) {
        _display->startFrame();
        render(*_display);
        _display->endFrame();
        _ircLastRender = now;
        _ircDirty = false;
      }
    }

    // Handle async WiFi operations
    if (_mode == WIFI_SETUP) {
      if (_wifiState == WIFI_SCANNING) {
        checkWifiScan();
      } else if (_wifiState == WIFI_CONNECTING) {
        checkWifiConnect();
        if (_wifiState == WIFI_CONNECTED) {
          // Show "Connected!" confirmation then go to URL entry
          if (_urlLen > 0) {
            // URL was pending — fetch it directly
            fetchWithSelfRef(_urlBuffer);
          } else {
            showConnectedAndGoHome();
          }
        }
      }
    }
  }

  bool handleInput(char c) override {
    switch (_mode) {
      case WIFI_SETUP:
        return handleWifiInput(c);
      case HOME:
        return handleHomeInput(c);
      case READING:
      case LINK_SELECT:
        return handleReadingInput(c);
      case FORM_FILL:
        return handleFormFillInput(c);
      case IRC_SETUP:
        return handleIRCSetupInput(c);
      case IRC_CHAT:
        return handleIRCChatInput(c);
      case FETCHING:
        // Q to cancel fetch (can't actually cancel HTTP mid-stream, but
        // go back to home)
        if (c == 'q' || c == 'Q') {
          _mode = HOME;
          return true;
        }
        return false;
      default:
        return false;
    }
  }
};