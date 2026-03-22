#include <Arduino.h>
#include "DataStore.h"

#if defined(EXTRAFS) || defined(QSPIFLASH)
  #define MAX_BLOBRECS 100
#else
  #define MAX_BLOBRECS 20
#endif

DataStore::DataStore(FILESYSTEM& fs, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(nullptr), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}

#if defined(EXTRAFS) || defined(QSPIFLASH)
DataStore::DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(&fsExtra), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}
#endif

static File openWrite(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(filename);
  return fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "w");
#else
  return fs->open(filename, "w", true);
#endif
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  static uint32_t _ContactsChannelsTotalBlocks = 0;
#endif

void DataStore::begin() {
#if defined(RP2040_PLATFORM)
  identity_store.begin();
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _ContactsChannelsTotalBlocks = _getContactsChannelsFS()->_getFS()->cfg->block_count;
  checkAdvBlobFile();
  #if defined(EXTRAFS) || defined(QSPIFLASH)
  migrateToSecondaryFS();
  #endif
#else
  // init 'blob store' support
  _fs->mkdir("/bl");
#endif
}

#if defined(ESP32)
  #include <SPIFFS.h>
  #include <nvs_flash.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
  #elif defined(EXTRAFS)
    #include <CustomLFS.h>
  #else 
    #include <InternalFileSystem.h>
  #endif
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
int _countLfsBlock(void *p, lfs_block_t block){
      if (block > _ContactsChannelsTotalBlocks) {
        MESH_DEBUG_PRINTLN("ERROR: Block %d exceeds filesystem bounds - CORRUPTION DETECTED!", block);
        return LFS_ERR_CORRUPT;  // return error to abort lfs_traverse() gracefully
    }
  lfs_size_t *size = (lfs_size_t*) p;
  *size += 1;
    return 0;
}

lfs_ssize_t _getLfsUsedBlockCount(FILESYSTEM* fs) {
  lfs_size_t size = 0;
  int err = lfs_traverse(fs->_getFS(), _countLfsBlock, &size);
  if (err) {
    MESH_DEBUG_PRINTLN("ERROR: lfs_traverse() error: %d", err);
    return 0;
  }
  return size;
}
#endif

uint32_t DataStore::getStorageUsedKb() const {
#if defined(ESP32)
  return SPIFFS.usedBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.usedBytes = 0;
  _fs->info(info);
  return info.usedBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int usedBlockCount = _getLfsUsedBlockCount(_getContactsChannelsFS());
  int usedBytes = config->block_size * usedBlockCount;
  return usedBytes / 1024;
#else
  return 0;
#endif
}

uint32_t DataStore::getStorageTotalKb() const {
#if defined(ESP32)
  return SPIFFS.totalBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.totalBytes = 0;
  _fs->info(info);
  return info.totalBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int totalBytes = config->block_size * config->block_count;
  return totalBytes / 1024;
#else
  return 0;
#endif
}

File DataStore::openRead(const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return _fs->open(filename, "r");
#else
  return _fs->open(filename, "r", false);
#endif
}

File DataStore::openRead(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r");
#else
  return fs->open(filename, "r", false);
#endif
}

bool DataStore::removeFile(const char* filename) {
  return _fs->remove(filename);
}

bool DataStore::removeFile(FILESYSTEM* fs, const char* filename) {
  return fs->remove(filename);
}

bool DataStore::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (_fsExtra == nullptr) {
    return _fs->format();
  } else {
    return _fs->format() && _fsExtra->format();
  }
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  bool fs_success = ((fs::SPIFFSFS *)_fs)->format();
  esp_err_t nvs_err = nvs_flash_erase(); // no need to reinit, will be done by reboot
  return fs_success && (nvs_err == ESP_OK);
#else
  #error "need to implement format()"
#endif
}

bool DataStore::loadMainIdentity(mesh::LocalIdentity &identity) {
  return identity_store.load("_main", identity);
}

bool DataStore::saveMainIdentity(const mesh::LocalIdentity &identity) {
  return identity_store.save("_main", identity);
}

void DataStore::loadPrefs(NodePrefs& prefs, double& node_lat, double& node_lon) {
  if (_fs->exists("/new_prefs")) {
    loadPrefsInt("/new_prefs", prefs, node_lat, node_lon); // new filename
  } else if (_fs->exists("/node_prefs")) {
    loadPrefsInt("/node_prefs", prefs, node_lat, node_lon);
    savePrefs(prefs, node_lat, node_lon);                // save to new filename
    _fs->remove("/node_prefs"); // remove old
  }
}

void DataStore::loadPrefsInt(const char *filename, NodePrefs& _prefs, double& node_lat, double& node_lon) {
  File file = openRead(_fs, filename);
  if (file) {
    uint8_t pad[8];

    file.read((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.read((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.read(pad, 4);                                                                     // 36
    file.read((uint8_t *)&node_lat, sizeof(node_lat));                                     // 40
    file.read((uint8_t *)&node_lon, sizeof(node_lon));                                     // 48
    file.read((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.read((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.read((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.read(pad, 1);                                                                     // 62
    file.read((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.read((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.read((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.read((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.read((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.read((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.read((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.read((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.read((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.read(pad, 2);                                                                     // 78
    file.read((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.read((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.read((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.read((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.read((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87
    file.read((uint8_t *)&_prefs.utc_offset_hours, sizeof(_prefs.utc_offset_hours));       // 88

    // Fields added later — may not exist in older prefs files
    if (file.read((uint8_t *)&_prefs.kb_flash_notify, sizeof(_prefs.kb_flash_notify)) != sizeof(_prefs.kb_flash_notify)) {
      _prefs.kb_flash_notify = 0;  // default OFF for old files
    }
    if (file.read((uint8_t *)&_prefs.ringtone_enabled, sizeof(_prefs.ringtone_enabled)) != sizeof(_prefs.ringtone_enabled)) {
      _prefs.ringtone_enabled = 0;  // default OFF for old files
    }

    // Clamp booleans to 0/1 in case of garbage
    if (_prefs.kb_flash_notify > 1) _prefs.kb_flash_notify = 0;
    if (_prefs.ringtone_enabled > 1) _prefs.ringtone_enabled = 0;

    // v1.14+ fields — may not exist in older prefs files
    if (file.read((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode)) != sizeof(_prefs.path_hash_mode)) {
      _prefs.path_hash_mode = 0;  // default: legacy 1-byte
    }
    if (file.read((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops)) != sizeof(_prefs.autoadd_max_hops)) {
      _prefs.autoadd_max_hops = 0;  // default: no limit
    }
    if (_prefs.path_hash_mode > 2) _prefs.path_hash_mode = 0;
    if (_prefs.autoadd_max_hops > 64) _prefs.autoadd_max_hops = 0;

    // v1.1+ Meck fields — may not exist in older prefs files
    if (file.read((uint8_t *)&_prefs.gps_baudrate, sizeof(_prefs.gps_baudrate)) != sizeof(_prefs.gps_baudrate)) {
      _prefs.gps_baudrate = 0;  // default: use compile-time GPS_BAUDRATE
    }
    if (file.read((uint8_t *)&_prefs.interference_threshold, sizeof(_prefs.interference_threshold)) != sizeof(_prefs.interference_threshold)) {
      _prefs.interference_threshold = 0;  // default: disabled
    }
    if (file.read((uint8_t *)&_prefs.dark_mode, sizeof(_prefs.dark_mode)) != sizeof(_prefs.dark_mode)) {
      _prefs.dark_mode = 0;  // default: light mode
    }
    if (file.read((uint8_t *)&_prefs.portrait_mode, sizeof(_prefs.portrait_mode)) != sizeof(_prefs.portrait_mode)) {
      _prefs.portrait_mode = 0;  // default: landscape
    }
    if (file.read((uint8_t *)&_prefs.auto_lock_minutes, sizeof(_prefs.auto_lock_minutes)) != sizeof(_prefs.auto_lock_minutes)) {
      _prefs.auto_lock_minutes = 0;  // default: disabled
    }

    // Clamp to valid ranges
    if (_prefs.dark_mode > 1) _prefs.dark_mode = 0;
    if (_prefs.portrait_mode > 1) _prefs.portrait_mode = 0;
    // auto_lock_minutes: only accept known options (0, 2, 5, 10, 15, 30)
    {
      uint8_t alm = _prefs.auto_lock_minutes;
      if (alm != 0 && alm != 2 && alm != 5 && alm != 10 && alm != 15 && alm != 30) {
        _prefs.auto_lock_minutes = 0;
      }
    }

    file.close();
  }
}

void DataStore::savePrefs(const NodePrefs& _prefs, double node_lat, double node_lon) {
  File file = openWrite(_fs, "/new_prefs");
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    file.write((uint8_t *)&_prefs.airtime_factor, sizeof(float));                           // 0
    file.write((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));                      // 4
    file.write(pad, 4);                                                                     // 36
    file.write((uint8_t *)&node_lat, sizeof(node_lat));                                     // 40
    file.write((uint8_t *)&node_lon, sizeof(node_lon));                                     // 48
    file.write((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));                               // 56
    file.write((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));                                   // 60
    file.write((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));                                   // 61
    file.write(pad, 1);                                                                     // 62
    file.write((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts)); // 63
    file.write((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));                                   // 64
    file.write((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));               // 68
    file.write((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base)); // 69
    file.write((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));   // 70
    file.write((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));   // 71
    file.write((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));             // 72
    file.write((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));     // 76
    file.write((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));                   // 77
    file.write(pad, 2);                                                                     // 78
    file.write((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));                         // 80
    file.write((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));               // 84
    file.write((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));                 // 85
    file.write((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));               // 86
    file.write((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));           // 87
    file.write((uint8_t *)&_prefs.utc_offset_hours, sizeof(_prefs.utc_offset_hours));     // 88
    file.write((uint8_t *)&_prefs.kb_flash_notify, sizeof(_prefs.kb_flash_notify));      // 89
    file.write((uint8_t *)&_prefs.ringtone_enabled, sizeof(_prefs.ringtone_enabled));    // 90
    file.write((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode));        // 91
    file.write((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops));   // 92
    file.write((uint8_t *)&_prefs.gps_baudrate, sizeof(_prefs.gps_baudrate));            // 93
    file.write((uint8_t *)&_prefs.interference_threshold, sizeof(_prefs.interference_threshold)); // 97
    file.write((uint8_t *)&_prefs.dark_mode, sizeof(_prefs.dark_mode));                  // 98
    file.write((uint8_t *)&_prefs.portrait_mode, sizeof(_prefs.portrait_mode));          // 99
    file.write((uint8_t *)&_prefs.auto_lock_minutes, sizeof(_prefs.auto_lock_minutes)); // 100

    file.close();
  }
}

void DataStore::loadContacts(DataStoreHost* host) {
  FILESYSTEM* fs = _getContactsChannelsFS();

  // --- Crash recovery ---
  // If /contacts3 is missing but /contacts3.tmp exists, a crash occurred
  // after removing the original but before the rename completed.
  // The .tmp file has the valid data — promote it.
  if (!fs->exists("/contacts3") && fs->exists("/contacts3.tmp")) {
    Serial.println("DataStore: recovering contacts from .tmp file");
    fs->rename("/contacts3.tmp", "/contacts3");
  }
  // If both exist, a crash occurred before the old file was removed.
  // The original /contacts3 is still valid — just clean up the orphan.
  if (fs->exists("/contacts3.tmp")) {
    fs->remove("/contacts3.tmp");
  }

  File file = openRead(fs, "/contacts3");
    if (file) {
      // --- Truncation guard ---
      // If the file is smaller than one full contact record (152 bytes),
      // it was truncated by a crash/brown-out. Discard it and try the
      // .tmp backup if available.
      size_t fsize = file.size();
      if (fsize > 0 && fsize < 152) {
        Serial.printf("DataStore: contacts3 truncated (%d bytes < 152), discarding\n", (int)fsize);
        file.close();
        fs->remove("/contacts3");
        if (fs->exists("/contacts3.tmp")) {
          File tmp = openRead(fs, "/contacts3.tmp");
          if (tmp && tmp.size() >= 152) {
            Serial.println("DataStore: recovering from .tmp after truncation");
            tmp.close();
            fs->rename("/contacts3.tmp", "/contacts3");
            file = openRead(fs, "/contacts3");
            if (!file) return;  // give up
          } else {
            if (tmp) tmp.close();
            Serial.println("DataStore: no valid contacts backup — starting fresh");
            return;
          }
        } else {
          Serial.println("DataStore: no .tmp backup — starting fresh");
          return;
        }
      } else if (fsize == 0) {
        // Empty file — nothing to load
        file.close();
        return;
      }

      bool full = false;
      while (!full) {
        ContactInfo c;
        uint8_t pub_key[32];
        uint8_t unused;

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *)&c.name, 32) == 32);
        success = success && (file.read(&c.type, 1) == 1);
        success = success && (file.read(&c.flags, 1) == 1);
        success = success && (file.read(&unused, 1) == 1);
        success = success && (file.read((uint8_t *)&c.sync_since, 4) == 4); // was 'reserved'
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read((uint8_t *)&c.last_advert_timestamp, 4) == 4);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read((uint8_t *)&c.lastmod, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lat, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lon, 4) == 4);

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        if (!host->onContactLoaded(c)) full = true;
      }
      file.close();
    }
}

void DataStore::saveContacts(DataStoreHost* host) {
  FILESYSTEM* fs = _getContactsChannelsFS();
  const char* finalPath = "/contacts3";
  const char* tmpPath   = "/contacts3.tmp";

  // --- Step 1: Write all contacts to a temporary file ---
  File file = openWrite(fs, tmpPath);
  if (!file) {
    Serial.println("DataStore: saveContacts FAILED — cannot open tmp file");
    return;
  }

  uint32_t idx = 0;
  ContactInfo c;
  uint8_t unused = 0;
  uint32_t recordsWritten = 0;
  bool writeOk = true;

  while (host->getContactForSave(idx, c)) {
    bool success = (file.write(c.id.pub_key, 32) == 32);
    success = success && (file.write((uint8_t *)&c.name, 32) == 32);
    success = success && (file.write(&c.type, 1) == 1);
    success = success && (file.write(&c.flags, 1) == 1);
    success = success && (file.write(&unused, 1) == 1);
    success = success && (file.write((uint8_t *)&c.sync_since, 4) == 4);
    success = success && (file.write((uint8_t *)&c.out_path_len, 1) == 1);
    success = success && (file.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
    success = success && (file.write(c.out_path, 64) == 64);
    success = success && (file.write((uint8_t *)&c.lastmod, 4) == 4);
    success = success && (file.write((uint8_t *)&c.gps_lat, 4) == 4);
    success = success && (file.write((uint8_t *)&c.gps_lon, 4) == 4);

    if (!success) {
      writeOk = false;
      Serial.printf("DataStore: saveContacts write error at record %d\n", idx);
      break;
    }

    recordsWritten++;
    idx++;
  }

  file.close();

  // --- Step 2: Verify the write completed ---
  // Reopen read-only to get true on-disk size (SPIFFS file.size() is unreliable before close)
  size_t expectedBytes = recordsWritten * 152;  // 152 bytes per contact record
  File verify = openRead(fs, tmpPath);
  size_t bytesWritten = verify ? verify.size() : 0;
  if (verify) verify.close();

  if (!writeOk || bytesWritten != expectedBytes) {
    Serial.printf("DataStore: saveContacts ABORTED — wrote %d bytes, expected %d (%d records)\n",
                  (int)bytesWritten, (int)expectedBytes, recordsWritten);
    fs->remove(tmpPath);  // Clean up failed tmp file
    return;  // Original /contacts3 is untouched
  }

  // --- Step 3: Replace original with verified temp file ---
  fs->remove(finalPath);
  if (fs->rename(tmpPath, finalPath)) {
    Serial.printf("DataStore: saved %d contacts (%d bytes)\n", recordsWritten, (int)bytesWritten);
  } else {
    // Rename failed — tmp file still has the good data
    Serial.println("DataStore: rename failed, tmp file preserved");
  }
}

// =========================================================================
// Chunked contact save — non-blocking across multiple loop iterations
// =========================================================================

bool DataStore::beginSaveContacts(DataStoreHost* host) {
  if (_saveInProgress) return false;  // Already saving

  FILESYSTEM* fs = _getContactsChannelsFS();
  _saveFile = openWrite(fs, "/contacts3.tmp");
  if (!_saveFile) {
    Serial.println("DataStore: chunked save FAILED — cannot open tmp file");
    return false;
  }

  _saveHost = host;
  _saveIdx = 0;
  _saveRecordsWritten = 0;
  _saveWriteOk = true;
  _saveInProgress = true;
  Serial.println("DataStore: chunked save started");
  return true;
}

bool DataStore::saveContactsChunk(int batchSize) {
  if (!_saveInProgress || !_saveWriteOk) return false;

  ContactInfo c;
  uint8_t unused = 0;
  int written = 0;

  while (written < batchSize && _saveHost->getContactForSave(_saveIdx, c)) {
    bool success = (_saveFile.write(c.id.pub_key, 32) == 32);
    success = success && (_saveFile.write((uint8_t *)&c.name, 32) == 32);
    success = success && (_saveFile.write(&c.type, 1) == 1);
    success = success && (_saveFile.write(&c.flags, 1) == 1);
    success = success && (_saveFile.write(&unused, 1) == 1);
    success = success && (_saveFile.write((uint8_t *)&c.sync_since, 4) == 4);
    success = success && (_saveFile.write((uint8_t *)&c.out_path_len, 1) == 1);
    success = success && (_saveFile.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
    success = success && (_saveFile.write(c.out_path, 64) == 64);
    success = success && (_saveFile.write((uint8_t *)&c.lastmod, 4) == 4);
    success = success && (_saveFile.write((uint8_t *)&c.gps_lat, 4) == 4);
    success = success && (_saveFile.write((uint8_t *)&c.gps_lon, 4) == 4);

    if (!success) {
      _saveWriteOk = false;
      Serial.printf("DataStore: chunked save write error at record %d\n", _saveIdx);
      return false;  // Error — finishSaveContacts will clean up
    }

    _saveRecordsWritten++;
    _saveIdx++;
    written++;
  }

  // Check if there are more contacts to write
  ContactInfo peek;
  if (_saveHost->getContactForSave(_saveIdx, peek)) {
    return true;  // More to write
  }
  return false;  // Done
}

void DataStore::finishSaveContacts() {
  if (!_saveInProgress) return;

  _saveFile.close();
  _saveInProgress = false;

  FILESYSTEM* fs = _getContactsChannelsFS();
  const char* finalPath = "/contacts3";
  const char* tmpPath   = "/contacts3.tmp";

  // Verify
  size_t expectedBytes = _saveRecordsWritten * 152;
  File verify = openRead(fs, tmpPath);
  size_t bytesWritten = verify ? verify.size() : 0;
  if (verify) verify.close();

  if (!_saveWriteOk || bytesWritten != expectedBytes) {
    Serial.printf("DataStore: chunked save ABORTED — wrote %d bytes, expected %d (%d records)\n",
                  (int)bytesWritten, (int)expectedBytes, _saveRecordsWritten);
    fs->remove(tmpPath);
    return;
  }

  fs->remove(finalPath);
  if (fs->rename(tmpPath, finalPath)) {
    Serial.printf("DataStore: saved %d contacts (%d bytes, chunked)\n",
                  _saveRecordsWritten, (int)bytesWritten);
  } else {
    Serial.println("DataStore: rename failed, tmp file preserved");
  }
}

void DataStore::loadChannels(DataStoreHost* host) {
    FILESYSTEM* fs = _getContactsChannelsFS();

    // Crash recovery (same pattern as contacts)
    if (!fs->exists("/channels2") && fs->exists("/channels2.tmp")) {
      Serial.println("DataStore: recovering channels from .tmp file");
      fs->rename("/channels2.tmp", "/channels2");
    }
    if (fs->exists("/channels2.tmp")) {
      fs->remove("/channels2.tmp");
    }

    File file = openRead(fs, "/channels2");
    if (file) {
      bool full = false;
      uint8_t channel_idx = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t unused[4];

        bool success = (file.read(unused, 4) == 4);
        success = success && (file.read((uint8_t *)ch.name, 32) == 32);
        success = success && (file.read((uint8_t *)ch.channel.secret, 32) == 32);

        if (!success) break; // EOF

        if (host->onChannelLoaded(channel_idx, ch)) {
          channel_idx++;
        } else {
          full = true;
        }
      }
      file.close();
    }
}

void DataStore::saveChannels(DataStoreHost* host) {
  FILESYSTEM* fs = _getContactsChannelsFS();
  const char* finalPath = "/channels2";
  const char* tmpPath   = "/channels2.tmp";

  File file = openWrite(fs, tmpPath);
  if (!file) {
    Serial.println("DataStore: saveChannels FAILED — cannot open tmp file");
    return;
  }

  uint8_t channel_idx = 0;
  ChannelDetails ch;
  uint8_t unused[4];
  memset(unused, 0, 4);
  bool writeOk = true;

  while (host->getChannelForSave(channel_idx, ch)) {
    bool success = (file.write(unused, 4) == 4);
    success = success && (file.write((uint8_t *)ch.name, 32) == 32);
    success = success && (file.write((uint8_t *)ch.channel.secret, 32) == 32);

    if (!success) {
      writeOk = false;
      Serial.printf("DataStore: saveChannels write error at channel %d\n", channel_idx);
      break;
    }
    channel_idx++;
  }

  file.close();

  // Reopen read-only to get true on-disk size (SPIFFS file.size() is unreliable before close)
  size_t expectedBytes = channel_idx * 68;  // 4 + 32 + 32 = 68 bytes per channel
  File verify = openRead(fs, tmpPath);
  size_t bytesWritten = verify ? verify.size() : 0;
  if (verify) verify.close();
  if (!writeOk || bytesWritten != expectedBytes) {
    Serial.printf("DataStore: saveChannels ABORTED — wrote %d bytes, expected %d\n",
                  (int)bytesWritten, (int)expectedBytes);
    fs->remove(tmpPath);
    return;
  }

  fs->remove(finalPath);
  if (fs->rename(tmpPath, finalPath)) {
    Serial.printf("DataStore: saved %d channels (%d bytes)\n", channel_idx, (int)bytesWritten);
  } else {
    Serial.println("DataStore: channels rename failed, tmp file preserved");
  }
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)

#define MAX_ADVERT_PKT_LEN   (2 + 32 + PUB_KEY_SIZE + 4 + SIGNATURE_SIZE + MAX_ADVERT_DATA_SIZE)

struct BlobRec {
  uint32_t timestamp;
  uint8_t  key[7];
  uint8_t  len;
  uint8_t  data[MAX_ADVERT_PKT_LEN];
};

void DataStore::checkAdvBlobFile() {
  if (!_getContactsChannelsFS()->exists("/adv_blobs")) {
    File file = openWrite(_getContactsChannelsFS(), "/adv_blobs");
    if (file) {
      BlobRec zeroes;
      memset(&zeroes, 0, sizeof(zeroes));
      for (int i = 0; i < MAX_BLOBRECS; i++) {     // pre-allocate to fixed size
        file.write((uint8_t *) &zeroes, sizeof(zeroes));
      }
      file.close();
    }
  }
}

void DataStore::migrateToSecondaryFS() {
  // migrate old adv_blobs, contacts3 and channels2 files to secondary FS if they don't already exist
  if (!_fsExtra->exists("/adv_blobs")) {
    if (_fs->exists("/adv_blobs")) {
    File oldAdvBlobs = openRead(_fs, "/adv_blobs");
    File newAdvBlobs = openWrite(_fsExtra, "/adv_blobs");

    if (oldAdvBlobs && newAdvBlobs) {
      BlobRec rec;
      size_t count = 0;

      // Copy 20 BlobRecs from old to new
      while (count < 20 && oldAdvBlobs.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
        newAdvBlobs.seek(count * sizeof(BlobRec));
        newAdvBlobs.write((uint8_t *)&rec, sizeof(rec));
        count++;
      }
    }
    if (oldAdvBlobs) oldAdvBlobs.close();
    if (newAdvBlobs) newAdvBlobs.close();
    _fs->remove("/adv_blobs");
    }
  }
  if (!_fsExtra->exists("/contacts3")) {
    if (_fs->exists("/contacts3")) {
      File oldFile = openRead(_fs, "/contacts3");
      File newFile = openWrite(_fsExtra, "/contacts3");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/contacts3");
    }
  }
  if (!_fsExtra->exists("/channels2")) {
    if (_fs->exists("/channels2")) {
      File oldFile = openRead(_fs, "/channels2");
      File newFile = openWrite(_fsExtra, "/channels2");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/channels2");
    }
  }
  // cleanup nodes which have been testing the extra fs, copy _main.id and new_prefs back to primary
  if (_fsExtra->exists("/_main.id")) {
      if (_fs->exists("/_main.id")) {_fs->remove("/_main.id");}
      File oldFile = openRead(_fsExtra, "/_main.id");
      File newFile = openWrite(_fs, "/_main.id");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    if (_fs->exists("/new_prefs")) {_fs->remove("/new_prefs");}
      File oldFile = openRead(_fsExtra, "/new_prefs");
      File newFile = openWrite(_fs, "/new_prefs");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/new_prefs");
  }
  // remove files from where they should not be anymore
  if (_fs->exists("/adv_blobs")) {
    _fs->remove("/adv_blobs");
  }
  if (_fs->exists("/contacts3")) {
    _fs->remove("/contacts3");
  }
  if (_fs->exists("/channels2")) {
    _fs->remove("/channels2");
  }
  if (_fsExtra->exists("/_main.id")) {
    _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    _fsExtra->remove("/new_prefs");
  }
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  File file = openRead(_getContactsChannelsFS(), "/adv_blobs");
  uint8_t len = 0;  // 0 = not found
  if (file) {
    BlobRec tmp;
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        len = tmp.len;
        memcpy(dest_buf, tmp.data, len);
        break;
      }
    }
    file.close();
  }
  return len;
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  if (len < PUB_KEY_SIZE+4+SIGNATURE_SIZE || len > MAX_ADVERT_PKT_LEN) return false;
  checkAdvBlobFile();
  File file = _getContactsChannelsFS()->open("/adv_blobs", FILE_O_WRITE);
  if (file) {
    uint32_t pos = 0, found_pos = 0;
    uint32_t min_timestamp = 0xFFFFFFFF;

    // search for matching key OR evict by oldest timestmap
    BlobRec tmp;
    file.seek(0);
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        found_pos = pos;
        break;
      }
      if (tmp.timestamp < min_timestamp) {
        min_timestamp = tmp.timestamp;
        found_pos = pos;
      }

      pos += sizeof(tmp);
    }

    memcpy(tmp.key, key, sizeof(tmp.key));  // just record 7 byte prefix of key
    memcpy(tmp.data, src_buf, len);
    tmp.len = len;
    tmp.timestamp = _clock->getCurrentTime();

    file.seek(found_pos);
    file.write((uint8_t *) &tmp, sizeof(tmp));

    file.close();
    return true;
  }
  return false; // error
}
#else
uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  char path[64];
  char fname[18];

  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);

  if (_fs->exists(path)) {
    File f = openRead(_fs, path);
    if (f) {
      int len = f.read(dest_buf, 255); // currently MAX 255 byte blob len supported!!
      f.close();
      return len;
    }
  }
  return 0; // not found
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  char path[64];
  char fname[18];

  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);

  File f = openWrite(_fs, path);
  if (f) {
    int n = f.write(src_buf, len);
    f.close();
    if (n == len) return true; // success!

    _fs->remove(path); // blob was only partially written!
  }
  return false; // error
}
#endif