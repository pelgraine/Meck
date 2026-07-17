#pragma once

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <MeshCore.h>

class UITask;            // forward decl -- used only to navigate back to Settings
extern MyMesh the_mesh;

// ==========================================================================
// Rx Log Screen -- on-device packet sniffer view, mirrors the MeshCore app's
// Rx Log. Reads the capture ring in MyMesh (filled by logRx()) and renders
// each received packet as an app-style block: route + payload type, time,
// size, hash, path, channel hash/name or From/To, the decoded line (for
// decryptable channels), and SNR. Entries are shown newest-first; W/S scroll
// by entry, Shift+Del returns to Settings (where the screen is opened from).
// ==========================================================================

class RxLogScreen : public UIScreen {
  UITask* _task;
  mesh::RTCClock* _rtc;
  int _scrollPos;   // display index of the top visible entry (0 = newest)

  static const char* typeName(uint8_t t) {
    switch (t) {
      case PAYLOAD_TYPE_REQ:        return "REQUEST";
      case PAYLOAD_TYPE_RESPONSE:   return "RESPONSE";
      case PAYLOAD_TYPE_TXT_MSG:    return "TEXT";
      case PAYLOAD_TYPE_ACK:        return "ACK";
      case PAYLOAD_TYPE_ADVERT:     return "ADVERT";
      case PAYLOAD_TYPE_GRP_TXT:    return "GROUP_TEXT";
      case PAYLOAD_TYPE_GRP_DATA:   return "GRP_DATA";
      case PAYLOAD_TYPE_ANON_REQ:   return "ANON_REQ";
      case PAYLOAD_TYPE_PATH:       return "PATH";
      case PAYLOAD_TYPE_TRACE:      return "TRACE";
      case PAYLOAD_TYPE_MULTIPART:  return "MULTIPART";
      case PAYLOAD_TYPE_CONTROL:    return "CONTROL";
      case PAYLOAD_TYPE_RAW_CUSTOM: return "RAW";
      default:                      return "?";
    }
  }

  // Render one entry block starting at y; returns the y after the block.
  int renderEntry(DisplayDriver& display, const RxLogEntry& e, int y, int lineH, int maxY) {
    uint8_t route = e.header & 0x03;                 // PH_ROUTE_MASK
    bool flood = (route == 0x00 || route == 0x01);   // TRANSPORT_FLOOD or FLOOD
    uint8_t ptype = (e.header >> 2) & 0x0F;          // PH_TYPE_SHIFT / PH_TYPE_MASK

    display.setColor(DisplayDriver::LIGHT);

    // Line 1: route + payload type (left), SNR (right)
    char l1[40];
    snprintf(l1, sizeof(l1), "%s %s", flood ? "FLOOD" : "DIRECT", typeName(ptype));
    display.setCursor(0, y);
    display.print(l1);
    char snrbuf[16];
    snprintf(snrbuf, sizeof(snrbuf), "%.2fdB", e.snr / 4.0f);
    display.setCursor(display.width() - display.getTextWidth(snrbuf) - 2, y);
    display.print(snrbuf);
    y += lineH;
    if (y + lineH > maxY) return y;

    // Line 2: time (HH:MM:SS, device UTC offset) + size
    int32_t local = (int32_t)e.timestamp + ((int32_t)the_mesh.getNodePrefs()->utc_offset_hours * 3600);
    int hrs = (local / 3600) % 24;
    int mins = (local / 60) % 60;
    int secs = local % 60;
    if (hrs < 0) hrs += 24;
    char l2[40];
    snprintf(l2, sizeof(l2), "%02d:%02d:%02d  %u bytes", hrs, mins, secs, (unsigned)e.size);
    display.setCursor(0, y);
    display.print(l2);
    y += lineH;
    if (y + lineH > maxY) return y;

    // Line 3: packet hash
    char hashbuf[2 * MAX_HASH_SIZE + 8];
    int p = snprintf(hashbuf, sizeof(hashbuf), "Hash: ");
    for (int i = 0; i < MAX_HASH_SIZE && p < (int)sizeof(hashbuf) - 3; i++) {
      p += snprintf(hashbuf + p, sizeof(hashbuf) - p, "%02X", e.hash[i]);
    }
    display.setCursor(0, y);
    display.print(hashbuf);
    y += lineH;
    if (y + lineH > maxY) return y;

    // Line 4: path (hop count + hop hashes, per the bytes-per-hop mode)
    uint8_t hops = e.path_len & 63;
    uint8_t bph = (e.path_len >> 6) + 1;
    char pathbuf[96];
    int q = snprintf(pathbuf, sizeof(pathbuf), "Path: %d hops", hops);
    if (hops > 0) {
      q += snprintf(pathbuf + q, sizeof(pathbuf) - q, " [");
      for (int h = 0; h < hops && q < (int)sizeof(pathbuf) - 8; h++) {
        for (int b = 0; b < bph; b++) {
          q += snprintf(pathbuf + q, sizeof(pathbuf) - q, "%02x", e.path[h * bph + b]);
        }
        if (h < hops - 1) q += snprintf(pathbuf + q, sizeof(pathbuf) - q, ",");
      }
      q += snprintf(pathbuf + q, sizeof(pathbuf) - q, "]");
    }
    display.drawTextEllipsized(0, y, display.width(), pathbuf);
    y += lineH;
    if (y + lineH > maxY) return y;

    // Line 5: channel hash/name (group) OR From/To (addressed)
    char l5[48];
    bool haveL5 = false;
    if (ptype == PAYLOAD_TYPE_GRP_TXT || ptype == PAYLOAD_TYPE_GRP_DATA) {
      if (e.channel_name[0]) snprintf(l5, sizeof(l5), "Ch %02x  #%s", e.payload0, e.channel_name);
      else                   snprintf(l5, sizeof(l5), "Ch %02x", e.payload0);
      haveL5 = true;
    } else if (ptype == PAYLOAD_TYPE_REQ || ptype == PAYLOAD_TYPE_RESPONSE
               || ptype == PAYLOAD_TYPE_TXT_MSG || ptype == PAYLOAD_TYPE_PATH) {
      snprintf(l5, sizeof(l5), "From %02x  To %02x", e.payload1, e.payload0);
      haveL5 = true;
    }
    if (haveL5) {
      display.drawTextEllipsized(0, y, display.width(), l5);
      y += lineH;
      if (y + lineH > maxY) return y;
    }

    // Line 6: decoded "sender: message" (only present for decryptable channels)
    if (e.text[0]) {
      char tb[RXLOG_TEXT_LEN + 4];
      display.translateUTF8ToBlocks(tb, e.text, sizeof(tb));
      display.drawTextEllipsized(0, y, display.width(), tb);
      y += lineH;
    }

    return y;
  }

public:
  RxLogScreen(UITask* task, mesh::RTCClock* rtc)
    : _task(task), _rtc(rtc), _scrollPos(0) {}

  void resetScroll() { _scrollPos = 0; }

  int render(DisplayDriver& display) override {
    int count = the_mesh.getRxLogCount();
#if defined(MECK_TWATCH)
    // Watch: no keyboard or touch scroll, so cycle through entries automatically.
    if (count > 1) { _scrollPos++; if (_scrollPos >= count) _scrollPos = 0; }
#endif
    if (_scrollPos < 0) _scrollPos = 0;
    if (_scrollPos > count - 1) _scrollPos = (count > 0) ? count - 1 : 0;

    // === Header ===
    display.setTextSize(1);
    display.setColor(DisplayDriver::GREEN);
    display.setCursor(0, 0);
    char hdr[32];
    snprintf(hdr, sizeof(hdr), "Rx Log: %d pkts", count);
    display.print(hdr);
    display.drawRect(0, 11, display.width(), 1);

    int headerHeight = 14;
#if defined(MECK_TWATCH)
    int footerHeight = 2;    // no footer on the watch
#else
    int footerHeight = 14;
#endif
    int maxY = display.height() - footerHeight;
    int y = headerHeight;

    if (count == 0) {
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(4, 28);
      display.print("No packets received yet");
      display.setCursor(4, 38);
      display.print("Packets appear as they arrive");
    } else {
      display.setTextSize(the_mesh.getNodePrefs()->smallTextSize());
      int lineH = the_mesh.getNodePrefs()->smallLineH();

      // Render blocks newest-first, starting at display index _scrollPos,
      // until the screen is full.
      for (int d = _scrollPos; d < count && y + lineH <= maxY; d++) {
        const RxLogEntry* e = the_mesh.getRxLogEntry(count - 1 - d);
        if (!e) break;
        y = renderEntry(display, *e, y, lineH, maxY);
        y += 3;  // gap between entry blocks
      }
    }

    display.setTextSize(1);

    // === Footer ===
#if !defined(MECK_TWATCH)
    int footerY = display.height() - 12;
    display.drawRect(0, footerY - 2, display.width(), 1);
    display.setColor(DisplayDriver::YELLOW);
    display.setCursor(0, footerY);
#if defined(LilyGo_T5S3_EPaper_Pro)
    display.print("Swipe:Scroll");
#else
    display.print("Sh+Del:Bk  W/S:Scroll");
#endif
#endif

#if defined(MECK_TWATCH)
    return 3000;  // watch: advance to the next packet every 3s
#else
    return 5000;  // refresh every 5s to pick up newly received packets
#endif
  }

  bool handleInput(char c) override {
    int count = the_mesh.getRxLogCount();

    // Scroll up (toward newest)
    if (c == 'w' || c == 'W' || c == 0xF2) {
      if (_scrollPos > 0) { _scrollPos--; return true; }
      return false;
    }
    // Scroll down (toward oldest)
    if (c == 's' || c == 'S' || c == 0xF1) {
      if (_scrollPos < count - 1) { _scrollPos++; return true; }
      return false;
    }
    // Back to Settings
    if (c == KEY_CANCEL) {
      if (_task) _task->gotoSettingsScreen();
      return true;
    }
    return false;
  }
};