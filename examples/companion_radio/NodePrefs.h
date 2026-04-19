#pragma once
#include <cstdint> // For uint8_t, uint32_t

#define TELEM_MODE_DENY            0
#define TELEM_MODE_ALLOW_FLAGS     1     // use contact.flags
#define TELEM_MODE_ALLOW_ALL       2

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1

struct NodePrefs {  // persisted to file
  float airtime_factor;
  char node_name[32];
  float freq;
  uint8_t sf;
  uint8_t cr;
  uint8_t multi_acks;
  uint8_t manual_add_contacts;
  float bw;
  uint8_t tx_power_dbm;
  uint8_t telemetry_mode_base;
  uint8_t telemetry_mode_loc;
  uint8_t telemetry_mode_env;
  float rx_delay_base;
  uint32_t ble_pin;
  uint8_t  advert_loc_policy;
  uint8_t  buzzer_quiet;
  uint8_t  gps_enabled;      // GPS enabled flag (0=disabled, 1=enabled)
  uint32_t gps_interval;     // GPS read interval in seconds
  uint8_t autoadd_config;    // bitmask for auto-add contacts config
  int8_t  utc_offset_hours;  // UTC offset in hours (-12 to +14), default 0
  uint8_t kb_flash_notify;   // Keyboard backlight flash on new message (0=off, 1=on)
  uint8_t ringtone_enabled;  // Ringtone on incoming call (0=off, 1=on) — 4G only
  uint8_t path_hash_mode;    // 0=1-byte (legacy), 1=2-byte, 2=3-byte path hashes
  uint8_t autoadd_max_hops;  // 0=no limit, N=up to N-1 hops (max 64)
  uint32_t gps_baudrate;     // GPS baud rate (0 = use compile-time GPS_BAUDRATE default)
  uint8_t interference_threshold; // Interference threshold in dB (0=disabled, 14+=enabled)
  uint8_t dark_mode;              // 0=off (white bg), 1=on (black bg)
  uint8_t portrait_mode;          // 0=landscape, 1=portrait — T5S3 only
  uint8_t auto_lock_minutes;      // 0=disabled, 2/5/10/15/30=auto-lock after idle
  uint8_t hint_shown;             // 0=show nav hint on boot, 1=already shown (dismiss permanently)
  uint8_t large_font;             // 0=tiny (built-in 6x8), 1=larger (FreeSans9pt) — T-Deck Pro only
  uint8_t ui_font_style;          // 0=Classic (FreeSans), 1=Noto Sans, 2=Montserrat
  uint8_t tx_fail_reset_threshold;  // 0=disabled, 1-10, default 3
  uint8_t rx_fail_reboot_threshold; // 0=disabled, 1-10, default 3

  // --- Font helpers (inline, no overhead) ---
  // Returns the DisplayDriver text-size index for "small/body" text.
  // T-Deck Pro: 0 = built-in 6×8 (or 7pt with custom fonts), 1 = 9pt.
  // T5S3: both 0 and 1 are 12pt fonts (regular vs bold) with identical line
  //        height, so large_font has no layout effect there.
  inline uint8_t smallTextSize() const {
    return large_font ? 1 : 0;
  }

  // Returns the virtual-coordinate line height matching smallTextSize().
  // T-Deck Pro size 0 → 9 (6×8 + 1px gap), size 1 → 11 (9pt ascent+descent).
  // With custom fonts (Noto 7pt, Montserrat 7pt), size 0 is slightly taller
  // than built-in 6×8 but fits within the 9-unit virtual grid.
  // T5S3 size 0/1 → same 12pt height → always 9 in virtual coords.
  inline int smallLineH() const {
#if defined(LilyGo_T5S3_EPaper_Pro)
    return 9;
#else
    return large_font ? 11 : 9;
#endif
  }

  // Returns the Y offset for selection highlight fillRect (T-Deck Pro only).
  // Size 0 (built-in font): cursor positions at top-left, +5 offset in
  //   setCursor places text below → fillRect at y+5 aligns with text.
  // Size 0 (custom 7pt fonts): baseline fonts, same behaviour as size 1.
  // Size 1 (9pt): cursor positions at baseline, ascenders render
  //   upward → fillRect must start above baseline to cover ascenders.
  // T5S3: always 0 (both sizes use baseline fonts with highlight at y).
  inline int smallHighlightOff() const {
#if defined(LilyGo_T5S3_EPaper_Pro)
    return 0;
#else
    // Custom 7pt fonts at textSize 0 use GFXfont (baseline rendering), not built-in
    if (ui_font_style > 0 && !large_font) return -2;
    return large_font ? -2 : 5;
#endif
  }
};