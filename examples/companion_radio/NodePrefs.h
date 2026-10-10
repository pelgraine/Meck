#pragma once
#include <cstdint> // For uint8_t, uint32_t

#define TELEM_MODE_DENY            0
#define TELEM_MODE_ALLOW_FLAGS     1     // use contact.flags
#define TELEM_MODE_ALLOW_ALL       2

#define ADVERT_LOC_NONE       0
#define ADVERT_LOC_SHARE      1

// Per-channel notification preferences (stored in channel_notif[])
#define NOTIF_ALL       0   // Notify on all messages (default)
#define NOTIF_MENTIONS  1   // Notify only when @nodename appears in message
#define NOTIF_NONE      2   // No notifications (muted)

// Canned messages: slot count and per-slot buffer size. CANNED_MSG_LEN is
// the 133-char channel message cap plus a NUL terminator.
#define CANNED_MSG_SLOTS  10
#define CANNED_MSG_LEN    134

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
  uint8_t auto_lock_minutes;      // 0=disabled, 2/5/10/15/30=auto-lock after idle
  uint8_t hint_shown;             // 0=show nav hint on boot, 1=already shown (dismiss permanently)
  uint8_t large_font;             // 0=tiny (built-in 6x8), 1=larger (FreeSans9pt) — T-Deck Pro only
  uint8_t ui_font_style;          // 0=Classic (FreeSans), 1=Noto Sans, 2=Montserrat
  uint8_t tx_fail_reset_threshold;  // 0=disabled, 1-10, default 3
  uint8_t rx_fail_reboot_threshold; // 0=disabled, 1-10, default 3

  // --- Region scope (MeshCore v1.15+ compatibility) ---
  // Device-wide default region for flood messages.
  // Empty default_scope_name = unscoped (legacy flood, reaches all repeaters).
  // When set, all flood sends use ROUTE_TYPE_TRANSPORT_FLOOD with transport
  // codes derived from this key.  Per-channel scope (in ChannelDetails) takes
  // priority when set.
  char default_scope_name[31];     // e.g. "au-nsw", empty = unscoped
  uint8_t default_scope_key[16];   // TransportKey derived from "#" + name

  // --- Per-channel notification preferences ---
  // Index 0..MAX_GROUP_CHANNELS-1 for group channels, index MAX_GROUP_CHANNELS for DMs.
  // Values: NOTIF_ALL (0), NOTIF_MENTIONS (1), NOTIF_NONE (2).
  // Defaults to NOTIF_ALL for all channels.
  uint8_t channel_notif[21];       // 20 group channels + 1 DM slot

  // --- LoRa antenna selection (T-Deck Pro MAX only) ---
  // 0 = internal antenna (XL9555 LORA_SEL HIGH), 1 = external (LORA_SEL LOW).
  // Applied at boot in main.cpp and live on toggle in SettingsScreen.
  uint8_t lora_antenna;            // 0 = internal (default), 1 = external

  // --- Backlight (e-ink frontlight) brightness, T-Deck Pro MAX only ---
  // Percentage (5..100) the heart button toggles the frontlight on to.
  // 0 = unset/legacy; treated as the default 100 on load.
  uint8_t backlight_brightness_pct; // 5..100, default 100

  // --- Keyboard LED brightness, T-Deck Pro MAX only ---
  // Percentage (5..100) the both-shifts toggle turns the keyboard backlight on to.
  // Takes effect on next toggle. Default 50.
  uint8_t kb_backlight_pct;        // 5..100, default 50

  // --- Canned messages ---
  // Ten pre-written messages, edited in Settings -> Canned Messages and sent
  // from the channel screen (Max: speech-bubble capacitive button). Each slot
  // holds up to 133 chars + NUL; an empty string marks an unused slot. Older
  // prefs files short-read this field and keep the all-empty default.
  char canned_msgs[CANNED_MSG_SLOTS][CANNED_MSG_LEN];

  // --- Timezones home page (world clock) ---
  // UTC offsets (-12..+14) of the page's two extra zones; its Home row is
  // utc_offset_hours. Older prefs files short-read these fields and
  // keep the default 0 (UTC+0).
  int8_t clock_slot_a;             // Zone 1 UTC offset, default 0
  int8_t clock_slot_b;             // Zone 2 UTC offset, default 0

  // --- Experimental Features ---
  // 1 = "Change Backlight to Alt+B" is on: the Max's heart touch key no
  // longer toggles the frontlight, leaving Alt+B. Older prefs files
  // short-read this field and keep the default 0 (heart key on).
  uint8_t backlight_alt_b_only;    // default 0

  // UI language (Experimental Features): 0 = English, 1 = French
  // (ui-new/MeckLang.h). Older prefs files short-read this field
  // and keep the default 0 (English).
  uint8_t ui_lang;                 // default 0

  // RX boosted gain (Experimental Features): 1 = boosted (SX126x boosted
  // gain mode), 0 = power-saving gain. Re-applied every 500 ms by the main
  // loop. Older prefs files short-read this field and keep the default 1.
  uint8_t rx_boosted_gain;         // default 1

  // AGC reset interval (Experimental Features), in seconds / 4 as upstream
  // stores it (0 = off, max 255 = 1020 s). Older prefs files short-read this
  // final field and keep the default 0 (off).
  uint8_t agc_reset_interval;      // default 0

  // --- Font helpers (inline, no overhead) ---
  // Returns the DisplayDriver text-size index for "small/body" text.
  // T-Deck Pro: 0 = built-in 6×8 (or 7pt with custom fonts), 1 = 9pt.
  inline uint8_t smallTextSize() const {
    return large_font ? 1 : 0;
  }

  // Returns the virtual-coordinate line height matching smallTextSize().
  // T-Deck Pro size 0 → 9 (6×8 + 1px gap), size 1 → 11 (9pt ascent+descent).
  // With custom fonts (Noto 7pt, Montserrat 7pt), size 0 is slightly taller
  // than built-in 6×8 but fits within the 9-unit virtual grid.
  inline int smallLineH() const {
    return large_font ? 11 : 9;
  }

  // Returns the Y offset for selection highlight fillRect (T-Deck Pro only).
  // Size 0 (built-in font): cursor positions at top-left, +5 offset in
  //   setCursor places text below → fillRect at y+5 aligns with text.
  // Size 0 (custom 7pt fonts): baseline fonts, same behaviour as size 1.
  // Size 1 (9pt): cursor positions at baseline, ascenders render
  //   upward → fillRect must start above baseline to cover ascenders.
  inline int smallHighlightOff() const {
    // Custom 7pt fonts at textSize 0 use GFXfont (baseline rendering), not built-in.
    // Offset 0: highlight starts at row top, covers 7pt ascenders without
    // bleeding into the row above (-2 was calibrated for taller 9pt ascenders).
    if (ui_font_style > 0 && !large_font) return 0;
    return large_font ? -2 : 5;
  }
};