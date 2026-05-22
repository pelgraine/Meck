#pragma once
// ---------------------------------------------------------------------------
// MeckExport.h -- Full config export to MeshCore-app-compatible JSON on SD.
//
// Writes a timestamped JSON file to /meshcore/ containing any combination of:
//   - Identity (public + private key)
//   - Radio/device settings (frequency, BW, SF, CR, TX power, position, auto-add)
//   - Channels (name + 16-byte secret)
//   - Contacts (same format as existing exportContactsJSON)
//
// Sections are selected via a bitmask of MECK_EXPORT_* flags.
// The output format is compatible with the MeshCore companion app config export.
//
// Usage from main.cpp:
//   int result = meckExportConfig(the_mesh, MECK_EXPORT_ALL,
//                                 sensors.node_lat, sensors.node_lon,
//                                 rtc_clock, sdCardReady);
// ---------------------------------------------------------------------------

#include <SD.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>

#define MECK_EXPORT_IDENTITY  0x01
#define MECK_EXPORT_CHANNELS  0x02
#define MECK_EXPORT_CONTACTS  0x04
#define MECK_EXPORT_RADIO     0x08
#define MECK_EXPORT_AUTOADD   0x10
#define MECK_EXPORT_ALL       0x1F

// Fallback defines (primary definitions live in SettingsScreen.h / ChannelScreen.h)
#ifndef MAX_GROUP_CHANNELS
  #define MAX_GROUP_CHANNELS 20
#endif
#ifndef AUTO_ADD_OVERWRITE_OLDEST
  #define AUTO_ADD_OVERWRITE_OLDEST (1 << 0)
  #define AUTO_ADD_CHAT             (1 << 1)
  #define AUTO_ADD_REPEATER         (1 << 2)
  #define AUTO_ADD_ROOM_SERVER      (1 << 3)
  #define AUTO_ADD_SENSOR           (1 << 4)
#endif

// JSON-escape a string in-place into dest (handles backslash and double-quote).
// Returns length of escaped string.
static int meck_json_escape(char* dest, int destSize, const char* src) {
  int wi = 0;
  for (int ri = 0; src[ri] && wi < destSize - 2; ri++) {
    if (src[ri] == '"' || src[ri] == '\\') dest[wi++] = '\\';
    dest[wi++] = src[ri];
  }
  dest[wi] = '\0';
  return wi;
}

// Export device config to a timestamped JSON file on SD card.
// flags:    bitmask of MECK_EXPORT_* sections to include
// node_lat: device latitude  (double, from sensors.node_lat)
// node_lon: device longitude (double, from sensors.node_lon)
// clock:    RTC clock for timestamp
// sdReady:  whether SD card is mounted
// outPath:  if non-NULL, receives the output filepath (up to outPathSize chars)
//
// Returns number of contacts written (0 if contacts not selected), or -1 on error.
static int meckExportConfig(MyMesh& mesh, uint8_t flags,
                            double node_lat, double node_lon,
                            mesh::RTCClock& clock, bool sdReady,
                            char* outPath = nullptr, int outPathSize = 0) {
  if (!sdReady) {
    Serial.println("Config Export: SD card not ready");
    return -1;
  }

  if (!SD.exists("/meshcore")) SD.mkdir("/meshcore");

  // Build timestamped filename
  char jsonPath[64];
  NodePrefs* prefs = mesh.getNodePrefs();
  uint32_t epoch = clock.getCurrentTime();
  int8_t utcOff = prefs->utc_offset_hours;
  time_t localEpoch = (time_t)epoch + (utcOff * 3600);
  struct tm tmBuf;
  gmtime_r(&localEpoch, &tmBuf);
  snprintf(jsonPath, sizeof(jsonPath),
           "/meshcore/meshcore_config_%04d%02d%02d_%02d%02d.json",
           tmBuf.tm_year + 1900, tmBuf.tm_mon + 1, tmBuf.tm_mday,
           tmBuf.tm_hour, tmBuf.tm_min);

  // Copy filepath to caller if requested
  if (outPath && outPathSize > 0) {
    strncpy(outPath, jsonPath, outPathSize - 1);
    outPath[outPathSize - 1] = '\0';
  }

  File f = SD.open(jsonPath, "w", true);
  if (!f) {
    Serial.printf("Config Export: failed to open %s\n", jsonPath);
    digitalWrite(SDCARD_CS, HIGH);
    return -1;
  }

  // Track whether we need a comma before the next top-level key
  bool needComma = false;

  f.print("{\n");

  // --- Name (always emitted) ---
  {
    char safeName[80];
    meck_json_escape(safeName, sizeof(safeName), prefs->node_name);
    f.printf("  \"name\": \"%s\"", safeName);
    needComma = true;
  }

  // --- Identity ---
  if (flags & MECK_EXPORT_IDENTITY) {
    // pub_key is public on Identity base class
    char pubHex[PUB_KEY_SIZE * 2 + 1];
    mesh::Utils::toHex(pubHex, mesh.self_id.pub_key, PUB_KEY_SIZE);

    // prv_key is private -- extract via writeTo(buffer)
    // writeTo writes: prv_key[64] then pub_key[32] = 96 bytes
    uint8_t idBuf[PRV_KEY_SIZE + PUB_KEY_SIZE];
    size_t idLen = mesh.self_id.writeTo(idBuf, sizeof(idBuf));

    char prvHex[PRV_KEY_SIZE * 2 + 1];
    prvHex[0] = '\0';
    if (idLen >= PRV_KEY_SIZE) {
      mesh::Utils::toHex(prvHex, idBuf, PRV_KEY_SIZE);
    }

    if (needComma) f.print(",\n"); needComma = true;
    f.printf("  \"public_key\": \"%s\",\n", pubHex);
    f.printf("  \"private_key\": \"%s\"", prvHex);
  }

  // --- Radio / device settings ---
  if (flags & MECK_EXPORT_RADIO) {
    // Radio settings -- convert to MeshCore app units (freq kHz, BW Hz)
    if (needComma) f.print(",\n"); needComma = true;
    f.print("  \"radio_settings\": {\n");
    f.printf("    \"frequency\": %lu,\n", (unsigned long)(prefs->freq * 1000.0f + 0.5f));
    f.printf("    \"bandwidth\": %lu,\n", (unsigned long)(prefs->bw * 1000.0f + 0.5f));
    f.printf("    \"spreading_factor\": %d,\n", prefs->sf);
    f.printf("    \"coding_rate\": %d,\n", prefs->cr);
    f.printf("    \"tx_power\": %d\n", prefs->tx_power_dbm);
    f.print("  }");

    // Position settings
    f.print(",\n  \"position_settings\": {\n");
    char latStr[16], lonStr[16];
    snprintf(latStr, sizeof(latStr), "%.6f", node_lat);
    snprintf(lonStr, sizeof(lonStr), "%.6f", node_lon);
    f.printf("    \"latitude\": \"%s\",\n", latStr);
    f.printf("    \"longitude\": \"%s\"\n", lonStr);
    f.print("  }");
  }

  // --- Contact auto-add preferences ---
  if (flags & MECK_EXPORT_AUTOADD) {
    if (needComma) f.print(",\n"); needComma = true;
    f.print("  \"other_settings\": {\n");
    f.printf("    \"manual_add_contacts\": %d,\n", prefs->manual_add_contacts);
    f.printf("    \"advert_location_policy\": %d\n", prefs->advert_loc_policy);
    f.print("  }");

    f.print(",\n  \"auto_add_settings\": {\n");
    f.printf("    \"auto_add_chat\": %s,\n",
             (prefs->autoadd_config & AUTO_ADD_CHAT) ? "true" : "false");
    f.printf("    \"auto_add_repeater\": %s,\n",
             (prefs->autoadd_config & AUTO_ADD_REPEATER) ? "true" : "false");
    f.printf("    \"auto_add_room_server\": %s,\n",
             (prefs->autoadd_config & AUTO_ADD_ROOM_SERVER) ? "true" : "false");
    f.printf("    \"auto_add_sensor\": %s,\n",
             (prefs->autoadd_config & AUTO_ADD_SENSOR) ? "true" : "false");
    f.printf("    \"overwrite_oldest\": %s,\n",
             (prefs->autoadd_config & AUTO_ADD_OVERWRITE_OLDEST) ? "true" : "false");
    if (prefs->autoadd_max_hops == 0) {
      f.print("    \"auto_add_max_hops\": null\n");
    } else {
      f.printf("    \"auto_add_max_hops\": %d\n", prefs->autoadd_max_hops);
    }
    f.print("  }");
  }

  // --- Channels ---
  if (flags & MECK_EXPORT_CHANNELS) {
    if (needComma) f.print(",\n"); needComma = true;
    f.print("  \"channels\": [\n");

    int chWritten = 0;
    ChannelDetails ch;
    for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (!mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;

      if (chWritten > 0) f.print(",\n");

      char safeName[80];
      meck_json_escape(safeName, sizeof(safeName), ch.name);

      // Export first CIPHER_KEY_SIZE (16) bytes of secret as hex
      char secHex[CIPHER_KEY_SIZE * 2 + 1];
      mesh::Utils::toHex(secHex, ch.channel.secret, CIPHER_KEY_SIZE);

      f.print("    {\n");
      f.printf("      \"name\": \"%s\",\n", safeName);
      f.printf("      \"secret\": \"%s\"\n", secHex);
      f.print("    }");
      chWritten++;
    }

    f.print("\n  ]");
    Serial.printf("Config Export: %d channels\n", chWritten);
  }

  // --- Contacts ---
  int contactsWritten = 0;
  if (flags & MECK_EXPORT_CONTACTS) {
    if (needComma) f.print(",\n"); needComma = true;
    f.print("  \"contacts\": [\n");

    uint32_t total = mesh.getNumContacts();
    for (uint32_t i = 0; i < total; i++) {
      ContactInfo c;
      if (!mesh.getContactByIdx(i, c)) continue;

      if (contactsWritten > 0) f.print(",\n");

      char hexKey[PUB_KEY_SIZE * 2 + 1];
      mesh::Utils::toHex(hexKey, c.id.pub_key, PUB_KEY_SIZE);

      char latStr[16], lonStr[16];
      snprintf(latStr, sizeof(latStr), "%.6f", (double)c.gps_lat / 1000000.0);
      snprintf(lonStr, sizeof(lonStr), "%.6f", (double)c.gps_lon / 1000000.0);

      char safeName[80];
      meck_json_escape(safeName, sizeof(safeName), c.name);

      f.print("    {\n");
      f.printf("      \"type\": %d,\n", c.type);
      f.printf("      \"name\": \"%s\",\n", safeName);
      f.printf("      \"custom_name\": null,\n");
      f.printf("      \"public_key\": \"%s\",\n", hexKey);
      f.printf("      \"flags\": %d,\n", c.flags);
      f.printf("      \"latitude\": \"%s\",\n", latStr);
      f.printf("      \"longitude\": \"%s\",\n", lonStr);
      f.printf("      \"last_advert\": %lu,\n", (unsigned long)c.last_advert_timestamp);
      f.printf("      \"last_modified\": %lu,\n", (unsigned long)c.lastmod);
      f.printf("      \"out_path_list\": null\n");
      f.print("    }");

      contactsWritten++;
    }

    f.print("\n  ]");
  }

  f.print("\n}\n");
  f.close();
  digitalWrite(SDCARD_CS, HIGH);

  Serial.printf("Config Export: wrote %s (flags=0x%02X, %d contacts)\n",
                jsonPath, flags, contactsWritten);
  return contactsWritten;
}