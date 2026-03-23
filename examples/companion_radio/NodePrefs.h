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
};