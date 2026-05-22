#pragma once
// ---------------------------------------------------------------------------
// MeckImport.h -- Boot-time config import from MeshCore-app-compatible JSON.
//
// Checks for /meshcore/import.json on SD card. If found, parses it and
// applies the sections present:
//   - Identity: replaces device keypair (requires reboot)
//   - Radio: applies frequency, BW, SF, CR, TX power and device settings
//   - Channels: merges by name (skips existing, adds new to empty slots)
//   - Contacts: merges by pub_key (skips duplicates)
//
// After successful import the file is renamed to import_done.json.
// If identity was changed the device reboots automatically.
//
// Compatible with MeshCore companion app config export format.
// Handles both unit systems: freq in MHz or kHz, BW in kHz or Hz.
//
// Usage from main.cpp setup(), after the_mesh.begin():
//   meckImportConfig(the_mesh, sensors.node_lat, sensors.node_lon,
//                    sdCardReady);
// ---------------------------------------------------------------------------

#include <SD.h>
#include <helpers/ContactInfo.h>
#include <helpers/ChannelDetails.h>

// Fallback defines
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

// Parser state machine
enum MeckImportSection : uint8_t {
  IMP_ROOT,
  IMP_RADIO,
  IMP_POSITION,
  IMP_OTHER,
  IMP_AUTOADD,
  IMP_CHANNELS_ARRAY,
  IMP_CHANNEL_OBJ,
  IMP_CONTACTS_ARRAY,
  IMP_CONTACT_OBJ,
};

// Strip leading whitespace from a C string, returning pointer into the buffer.
static char* meck_imp_trim(char* s) {
  while (*s == ' ' || *s == '\t') s++;
  return s;
}

// Extract the JSON key from a line like: "key": value
// Writes the key into keyBuf, returns pointer to the value portion (after ": "),
// or NULL if line doesn't contain a key-value pair.
static char* meck_imp_parse_kv(char* line, char* keyBuf, int keyBufSize) {
  char* q1 = strchr(line, '"');
  if (!q1) return NULL;
  char* q2 = strchr(q1 + 1, '"');
  if (!q2) return NULL;

  int klen = q2 - q1 - 1;
  if (klen >= keyBufSize) klen = keyBufSize - 1;
  memcpy(keyBuf, q1 + 1, klen);
  keyBuf[klen] = '\0';

  char* valStart = q2 + 1;
  while (*valStart == ':' || *valStart == ' ' || *valStart == '\t') valStart++;

  // Strip trailing comma and whitespace
  int vlen = strlen(valStart);
  while (vlen > 0 && (valStart[vlen-1] == ',' || valStart[vlen-1] == '\n' ||
         valStart[vlen-1] == '\r' || valStart[vlen-1] == ' ')) {
    valStart[--vlen] = '\0';
  }

  return valStart;
}

// Extract a string value by stripping surrounding quotes and unescaping.
static void meck_imp_extract_string(char* dest, int destSize, char* val) {
  if (val[0] == '"') val++;
  int slen = strlen(val);
  if (slen > 0 && val[slen-1] == '"') val[slen-1] = '\0';

  int wi = 0;
  for (int ri = 0; val[ri] && wi < destSize - 1; ri++) {
    if (val[ri] == '\\' && val[ri+1]) { ri++; }
    dest[wi++] = val[ri];
  }
  dest[wi] = '\0';
}

// Check for /meshcore/import.json and apply config if present.
// mesh:       initialised MyMesh (after begin())
// node_lat/lon: references to update position (sensors.node_lat/lon)
// sdReady:    whether SD card is mounted
//
// Requires MyMesh::saveMainIdentity() public method (add to MyMesh.h if missing):
//   void saveMainIdentity() { _store->saveMainIdentity(self_id); }
//
// Returns: 0 = no import file found
//          1 = imported successfully (will reboot if identity changed)
//         -1 = error during import
static int meckImportConfig(MyMesh& mesh,
                            double& node_lat, double& node_lon,
                            bool sdReady) {
  if (!sdReady) return 0;

  const char* importPath = "/meshcore/import.json";
  const char* donePath   = "/meshcore/import_done.json";

  if (!SD.exists(importPath)) return 0;

  Serial.printf("Config Import: found %s\n", importPath);
  File f = SD.open(importPath, "r");
  if (!f) {
    Serial.println("Config Import: failed to open file");
    return -1;
  }

  NodePrefs* prefs = mesh.getNodePrefs();

  // Accumulated state
  bool identityChanged = false;
  bool prefsChanged = false;
  bool contactsChanged = false;

  // Identity buffers
  uint8_t imp_pub[PUB_KEY_SIZE];
  uint8_t imp_prv[PRV_KEY_SIZE];
  bool gotPub = false, gotPrv = false;

  // Channel accumulator
  char ch_name[32];
  uint8_t ch_secret[PUB_KEY_SIZE];
  bool ch_gotName = false, ch_gotSecret = false;
  int channelsAdded = 0, channelsSkipped = 0;

  // Contact accumulator
  ContactInfo ct;
  uint8_t ct_pubkey[PUB_KEY_SIZE];
  bool ct_gotPubkey = false, ct_gotType = false;
  int contactsAdded = 0, contactsSkipped = 0;

  MeckImportSection section = IMP_ROOT;
  char lineBuf[256];
  char key[48];

  while (f.available()) {
    // Read one line
    int len = 0;
    while (f.available() && len < (int)sizeof(lineBuf) - 1) {
      char ch = f.read();
      if (ch == '\n') break;
      lineBuf[len++] = ch;
    }
    lineBuf[len] = '\0';
    char* line = meck_imp_trim(lineBuf);

    // --- Bracket / section transitions ---

    // Closing bracket: pop section
    if (line[0] == '}' || (line[0] == '}' && line[1] == ',')) {
      if (section == IMP_CHANNEL_OBJ) {
        // End of channel object -- try to add
        if (ch_gotName && ch_gotSecret) {
          // Check for existing channel with same name
          bool exists = false;
          ChannelDetails existing;
          for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
            if (mesh.getChannel(i, existing) && existing.name[0] != '\0') {
              if (strcmp(existing.name, ch_name) == 0) {
                exists = true;
                break;
              }
            }
          }
          if (exists) {
            channelsSkipped++;
          } else {
            // Find first empty slot
            bool added = false;
            for (uint8_t i = 0; i < MAX_GROUP_CHANNELS; i++) {
              ChannelDetails slot;
              if (!mesh.getChannel(i, slot) || slot.name[0] == '\0') {
                ChannelDetails newCh;
                memset(&newCh, 0, sizeof(newCh));
                strncpy(newCh.name, ch_name, sizeof(newCh.name));
                newCh.name[31] = '\0';
                memcpy(newCh.channel.secret, ch_secret, PUB_KEY_SIZE);
                if (mesh.setChannel(i, newCh)) {
                  channelsAdded++;
                  added = true;
                }
                break;
              }
            }
            if (!added) {
              Serial.println("Config Import: no empty channel slots");
            }
          }
        }
        ch_gotName = ch_gotSecret = false;
        section = IMP_CHANNELS_ARRAY;
        continue;
      }
      if (section == IMP_CONTACT_OBJ) {
        // End of contact object -- try to add
        if (ct_gotPubkey && ct_gotType) {
          ct.id = mesh::Identity(ct_pubkey);
          if (mesh.lookupContactByPubKey(ct_pubkey, PUB_KEY_SIZE) != NULL) {
            contactsSkipped++;
          } else if (mesh.addContact(ct)) {
            contactsAdded++;
            contactsChanged = true;
          } else {
            Serial.printf("Config Import: contact table full after %d added\n", contactsAdded);
          }
        }
        ct_gotPubkey = ct_gotType = false;
        section = IMP_CONTACTS_ARRAY;
        continue;
      }
      if (section == IMP_RADIO || section == IMP_POSITION ||
          section == IMP_OTHER || section == IMP_AUTOADD) {
        section = IMP_ROOT;
        continue;
      }
      continue;
    }

    // Closing array bracket
    if (line[0] == ']' || (line[0] == ']' && line[1] == ',')) {
      if (section == IMP_CHANNELS_ARRAY || section == IMP_CONTACTS_ARRAY) {
        section = IMP_ROOT;
      }
      continue;
    }

    // Opening brace within an array: start new object
    if (line[0] == '{' && section == IMP_CHANNELS_ARRAY) {
      memset(ch_name, 0, sizeof(ch_name));
      memset(ch_secret, 0, sizeof(ch_secret));
      ch_gotName = ch_gotSecret = false;
      section = IMP_CHANNEL_OBJ;
      continue;
    }
    if (line[0] == '{' && section == IMP_CONTACTS_ARRAY) {
      memset(&ct, 0, sizeof(ct));
      ct_gotPubkey = ct_gotType = false;
      ct.out_path_len = OUT_PATH_UNKNOWN;
      ct.shared_secret_valid = false;
      section = IMP_CONTACT_OBJ;
      continue;
    }

    // --- Key-value parsing ---
    char* val = meck_imp_parse_kv(line, key, sizeof(key));
    if (!val) continue;

    // Check for section-opening keys at root level
    if (section == IMP_ROOT) {
      if (strcmp(key, "name") == 0) {
        char importName[32];
        meck_imp_extract_string(importName, sizeof(importName), val);
        strncpy(prefs->node_name, importName, sizeof(prefs->node_name));
        prefs->node_name[31] = '\0';
        prefsChanged = true;
        Serial.printf("Config Import: name = %s\n", prefs->node_name);
      }
      else if (strcmp(key, "public_key") == 0) {
        char hex[PUB_KEY_SIZE * 2 + 2];
        meck_imp_extract_string(hex, sizeof(hex), val);
        if (mesh::Utils::fromHex(imp_pub, PUB_KEY_SIZE, hex)) {
          gotPub = true;
        }
      }
      else if (strcmp(key, "private_key") == 0) {
        char hex[PRV_KEY_SIZE * 2 + 2];
        meck_imp_extract_string(hex, sizeof(hex), val);
        if (mesh::Utils::fromHex(imp_prv, PRV_KEY_SIZE, hex)) {
          gotPrv = true;
        }
      }
      else if (strcmp(key, "radio_settings") == 0) {
        // Value should contain '{' — section opens
        if (strchr(val, '{')) section = IMP_RADIO;
      }
      else if (strcmp(key, "position_settings") == 0) {
        if (strchr(val, '{')) section = IMP_POSITION;
      }
      else if (strcmp(key, "other_settings") == 0) {
        if (strchr(val, '{')) section = IMP_OTHER;
      }
      else if (strcmp(key, "auto_add_settings") == 0) {
        if (strchr(val, '{')) section = IMP_AUTOADD;
      }
      else if (strcmp(key, "channels") == 0) {
        if (strchr(val, '[')) section = IMP_CHANNELS_ARRAY;
      }
      else if (strcmp(key, "contacts") == 0) {
        if (strchr(val, '[')) section = IMP_CONTACTS_ARRAY;
      }
      continue;
    }

    // --- Radio settings ---
    if (section == IMP_RADIO) {
      if (strcmp(key, "frequency") == 0) {
        double freq = atof(val);
        // Auto-detect units: >3000 = kHz, <=3000 = MHz
        if (freq > 3000.0) freq /= 1000.0;
        prefs->freq = (float)freq;
        prefsChanged = true;
      }
      else if (strcmp(key, "bandwidth") == 0) {
        double bw = atof(val);
        // Auto-detect units: >1000 = Hz, <=1000 = kHz
        if (bw > 1000.0) bw /= 1000.0;
        prefs->bw = (float)bw;
        prefsChanged = true;
      }
      else if (strcmp(key, "spreading_factor") == 0) {
        prefs->sf = (uint8_t)atoi(val);
        prefsChanged = true;
      }
      else if (strcmp(key, "coding_rate") == 0) {
        prefs->cr = (uint8_t)atoi(val);
        prefsChanged = true;
      }
      else if (strcmp(key, "tx_power") == 0) {
        prefs->tx_power_dbm = (uint8_t)atoi(val);
        prefsChanged = true;
      }
      continue;
    }

    // --- Position settings ---
    if (section == IMP_POSITION) {
      if (strcmp(key, "latitude") == 0) {
        char str[16];
        meck_imp_extract_string(str, sizeof(str), val);
        node_lat = atof(str);
        prefsChanged = true;
      }
      else if (strcmp(key, "longitude") == 0) {
        char str[16];
        meck_imp_extract_string(str, sizeof(str), val);
        node_lon = atof(str);
        prefsChanged = true;
      }
      continue;
    }

    // --- Other settings ---
    if (section == IMP_OTHER) {
      if (strcmp(key, "manual_add_contacts") == 0) {
        prefs->manual_add_contacts = (uint8_t)atoi(val);
        prefsChanged = true;
      }
      else if (strcmp(key, "advert_location_policy") == 0) {
        prefs->advert_loc_policy = (uint8_t)atoi(val);
        prefsChanged = true;
      }
      continue;
    }

    // --- Auto-add settings ---
    if (section == IMP_AUTOADD) {
      if (strcmp(key, "auto_add_chat") == 0) {
        if (strstr(val, "true")) prefs->autoadd_config |= AUTO_ADD_CHAT;
        else prefs->autoadd_config &= ~AUTO_ADD_CHAT;
        prefsChanged = true;
      }
      else if (strcmp(key, "auto_add_repeater") == 0) {
        if (strstr(val, "true")) prefs->autoadd_config |= AUTO_ADD_REPEATER;
        else prefs->autoadd_config &= ~AUTO_ADD_REPEATER;
        prefsChanged = true;
      }
      else if (strcmp(key, "auto_add_room_server") == 0) {
        if (strstr(val, "true")) prefs->autoadd_config |= AUTO_ADD_ROOM_SERVER;
        else prefs->autoadd_config &= ~AUTO_ADD_ROOM_SERVER;
        prefsChanged = true;
      }
      else if (strcmp(key, "auto_add_sensor") == 0) {
        if (strstr(val, "true")) prefs->autoadd_config |= AUTO_ADD_SENSOR;
        else prefs->autoadd_config &= ~AUTO_ADD_SENSOR;
        prefsChanged = true;
      }
      else if (strcmp(key, "overwrite_oldest") == 0) {
        if (strstr(val, "true")) prefs->autoadd_config |= AUTO_ADD_OVERWRITE_OLDEST;
        else prefs->autoadd_config &= ~AUTO_ADD_OVERWRITE_OLDEST;
        prefsChanged = true;
      }
      else if (strcmp(key, "auto_add_max_hops") == 0) {
        if (strstr(val, "null")) {
          prefs->autoadd_max_hops = 0;
        } else {
          prefs->autoadd_max_hops = (uint8_t)atoi(val);
        }
        prefsChanged = true;
      }
      continue;
    }

    // --- Channel object ---
    if (section == IMP_CHANNEL_OBJ) {
      if (strcmp(key, "name") == 0) {
        meck_imp_extract_string(ch_name, sizeof(ch_name), val);
        ch_gotName = true;
      }
      else if (strcmp(key, "secret") == 0) {
        char hex[PUB_KEY_SIZE * 2 + 2];
        meck_imp_extract_string(hex, sizeof(hex), val);
        // Import handles both 16-byte (32 hex) and 32-byte (64 hex) secrets
        int hexLen = strlen(hex);
        if (hexLen >= CIPHER_KEY_SIZE * 2) {
          memset(ch_secret, 0, sizeof(ch_secret));
          mesh::Utils::fromHex(ch_secret, hexLen / 2, hex);
          ch_gotSecret = true;
        }
      }
      continue;
    }

    // --- Contact object ---
    if (section == IMP_CONTACT_OBJ) {
      if (strcmp(key, "type") == 0) {
        ct.type = (uint8_t)atoi(val);
        ct_gotType = true;
      }
      else if (strcmp(key, "name") == 0) {
        meck_imp_extract_string(ct.name, sizeof(ct.name), val);
      }
      else if (strcmp(key, "public_key") == 0) {
        char hex[PUB_KEY_SIZE * 2 + 2];
        meck_imp_extract_string(hex, sizeof(hex), val);
        if (mesh::Utils::fromHex(ct_pubkey, PUB_KEY_SIZE, hex)) {
          ct_gotPubkey = true;
        }
      }
      else if (strcmp(key, "flags") == 0) {
        ct.flags = (uint8_t)atoi(val);
      }
      else if (strcmp(key, "latitude") == 0) {
        char str[16];
        meck_imp_extract_string(str, sizeof(str), val);
        ct.gps_lat = (int32_t)(atof(str) * 1000000.0);
      }
      else if (strcmp(key, "longitude") == 0) {
        char str[16];
        meck_imp_extract_string(str, sizeof(str), val);
        ct.gps_lon = (int32_t)(atof(str) * 1000000.0);
      }
      else if (strcmp(key, "last_advert") == 0) {
        ct.last_advert_timestamp = (uint32_t)strtoul(val, NULL, 10);
      }
      else if (strcmp(key, "last_modified") == 0) {
        ct.lastmod = (uint32_t)strtoul(val, NULL, 10);
      }
      // custom_name, out_path_list -- ignored
      continue;
    }

  } // end while

  f.close();
  digitalWrite(SDCARD_CS, HIGH);

  // --- Apply identity ---
  if (gotPub && gotPrv) {
    // Reconstruct LocalIdentity: readFrom expects prv_key[64] + pub_key[32]
    uint8_t idBuf[PRV_KEY_SIZE + PUB_KEY_SIZE];
    memcpy(idBuf, imp_prv, PRV_KEY_SIZE);
    memcpy(&idBuf[PRV_KEY_SIZE], imp_pub, PUB_KEY_SIZE);
    mesh.self_id.readFrom(idBuf, sizeof(idBuf));
    mesh.saveMainIdentity();
    identityChanged = true;
    Serial.println("Config Import: identity replaced");
  }

  // --- Save prefs ---
  if (prefsChanged) {
    mesh.savePrefs();
    Serial.println("Config Import: prefs saved");
  }

  // --- Save channels ---
  if (channelsAdded > 0) {
    mesh.saveChannels();
  }
  Serial.printf("Config Import: channels %d added, %d skipped\n",
                channelsAdded, channelsSkipped);

  // --- Save contacts ---
  if (contactsChanged) {
    mesh.saveContacts();
  }
  Serial.printf("Config Import: contacts %d added, %d skipped, %d total\n",
                contactsAdded, contactsSkipped, (int)mesh.getNumContacts());

  // --- Rename import file ---
  if (SD.exists(donePath)) SD.remove(donePath);
  SD.rename(importPath, donePath);
  Serial.printf("Config Import: renamed to %s\n", donePath);

  // If identity changed, reboot to apply cleanly
  if (identityChanged) {
    Serial.println("Config Import: identity changed -- rebooting in 2s...");
    delay(2000);
    ESP.restart();
    // Does not return
  }

  return 1;
}