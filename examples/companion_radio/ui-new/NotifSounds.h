#pragma once

// =============================================================================
// NotifSounds.h -- Per-channel notification sound configuration
//
// Stores a custom MP3 filename per channel for notification tones.
// Config persisted to /meshcore/notif_sounds.cfg on SD card.
// Sound files live in /alarms/ (shared with alarm clock sounds).
//
// Playback is request-based: UITask calls requestPlay() when a message
// arrives on a channel with a custom tone.  main.cpp loop() polls
// hasPendingPlay() and drives the Audio* object.
//
// Guard: MECK_AUDIO_VARIANT (requires speaker hardware)
// =============================================================================

#ifdef MECK_AUDIO_VARIANT

#ifndef NOTIF_SOUNDS_H
#define NOTIF_SOUNDS_H

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include "variant.h"

#ifndef MAX_GROUP_CHANNELS
#define MAX_GROUP_CHANNELS 20
#endif

#define NOTIF_SOUND_NAME_MAX   32
#define NOTIF_SOUND_SLOTS      (MAX_GROUP_CHANNELS + 1)  // +1 for DMs
#define NOTIF_SOUND_CONFIG_PATH "/meshcore/notif_sounds.cfg"
#define NOTIF_SOUND_MAGIC      0x4E534E44  // "NSND"
#define NOTIF_SOUND_VERSION    1
#ifndef ALARMS_FOLDER
#define ALARMS_FOLDER          "/alarms"
#endif

struct __attribute__((packed)) NotifSoundCfgHeader {
  uint32_t magic;
  uint8_t  version;
  uint8_t  count;      // Number of slots stored
  uint8_t  reserved[2];
  // Followed by count * NOTIF_SOUND_NAME_MAX bytes
};

class NotifSounds {
public:
  NotifSounds() : _pendingPlay(false) {
    memset(_sounds, 0, sizeof(_sounds));
    _pendingFile[0] = '\0';
  }

  void begin() {
    loadConfig();
    Serial.println("NotifSounds: Config loaded");
  }

  // --- Config accessors ---

  const char* getSoundForChannel(uint8_t channel_idx) const {
    int slot = (channel_idx == 0xFF) ? MAX_GROUP_CHANNELS : (int)channel_idx;
    if (slot < 0 || slot >= NOTIF_SOUND_SLOTS) return "";
    return _sounds[slot];
  }

  bool hasSoundForChannel(uint8_t channel_idx) const {
    const char* s = getSoundForChannel(channel_idx);
    return s && s[0] != '\0';
  }

  void setSoundForChannel(uint8_t channel_idx, const char* filename) {
    int slot = (channel_idx == 0xFF) ? MAX_GROUP_CHANNELS : (int)channel_idx;
    if (slot < 0 || slot >= NOTIF_SOUND_SLOTS) return;
    if (filename) {
      strncpy(_sounds[slot], filename, NOTIF_SOUND_NAME_MAX - 1);
      _sounds[slot][NOTIF_SOUND_NAME_MAX - 1] = '\0';
    } else {
      _sounds[slot][0] = '\0';
    }
    saveConfig();
    Serial.printf("NotifSounds: Channel %d -> '%s'\n", (int)channel_idx,
                  _sounds[slot][0] ? _sounds[slot] : "(default)");
  }

  void clearSoundForChannel(uint8_t channel_idx) {
    setSoundForChannel(channel_idx, nullptr);
  }

  // --- Sound file scanning (reuses /alarms/ folder) ---

  void scanSoundFiles() {
    _soundFiles.clear();
    if (!SD.exists(ALARMS_FOLDER)) {
      SD.mkdir(ALARMS_FOLDER);
    }
    File root = SD.open(ALARMS_FOLDER);
    if (!root || !root.isDirectory()) {
      digitalWrite(SDCARD_CS, HIGH);
      return;
    }
    File entry = root.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name = entry.name();
        if (name.length() > 0 && name.charAt(0) != '.') {
          String lower = name;
          lower.toLowerCase();
          if (lower.endsWith(".mp3")) {
            _soundFiles.push_back(name);
          }
        }
      }
      entry = root.openNextFile();
    }
    root.close();
    digitalWrite(SDCARD_CS, HIGH);
    std::sort(_soundFiles.begin(), _soundFiles.end());
    Serial.printf("NotifSounds: Found %d sound files\n", (int)_soundFiles.size());
  }

  int getSoundFileCount() const { return (int)_soundFiles.size(); }
  const String& getSoundFile(int idx) const { return _soundFiles[idx]; }
  const std::vector<String>& getSoundFiles() const { return _soundFiles; }

  // --- Pending playback request ---

  void requestPlay(const char* fullPath) {
    strncpy(_pendingFile, fullPath, sizeof(_pendingFile) - 1);
    _pendingFile[sizeof(_pendingFile) - 1] = '\0';
    _pendingPlay = true;
  }

  bool hasPendingPlay() const { return _pendingPlay; }

  const char* getPendingFile() const { return _pendingFile; }

  void clearPending() {
    _pendingPlay = false;
    _pendingFile[0] = '\0';
  }

private:
  char _sounds[NOTIF_SOUND_SLOTS][NOTIF_SOUND_NAME_MAX];
  std::vector<String> _soundFiles;

  bool _pendingPlay;
  char _pendingFile[48];

  void loadConfig() {
    memset(_sounds, 0, sizeof(_sounds));

    if (!SD.exists(NOTIF_SOUND_CONFIG_PATH)) {
      Serial.println("NotifSounds: No config file, using defaults");
      digitalWrite(SDCARD_CS, HIGH);
      return;
    }

    File f = SD.open(NOTIF_SOUND_CONFIG_PATH, "r");
    if (!f) {
      Serial.println("NotifSounds: Failed to open config");
      return;
    }

    NotifSoundCfgHeader hdr;
    if (f.read((uint8_t*)&hdr, sizeof(hdr)) != sizeof(hdr) ||
        hdr.magic != NOTIF_SOUND_MAGIC || hdr.version != NOTIF_SOUND_VERSION) {
      Serial.println("NotifSounds: Config invalid or wrong version");
      f.close();
      digitalWrite(SDCARD_CS, HIGH);
      return;
    }

    int slotsToRead = hdr.count;
    if (slotsToRead > NOTIF_SOUND_SLOTS) slotsToRead = NOTIF_SOUND_SLOTS;

    for (int i = 0; i < slotsToRead; i++) {
      if (f.read((uint8_t*)_sounds[i], NOTIF_SOUND_NAME_MAX) != NOTIF_SOUND_NAME_MAX) {
        break;
      }
      _sounds[i][NOTIF_SOUND_NAME_MAX - 1] = '\0';  // Safety null-terminate
    }

    f.close();
    digitalWrite(SDCARD_CS, HIGH);
    Serial.printf("NotifSounds: Loaded %d slots from config\n", slotsToRead);
  }

  void saveConfig() {
    if (!SD.exists("/meshcore")) {
      SD.mkdir("/meshcore");
    }

    File f = SD.open(NOTIF_SOUND_CONFIG_PATH, FILE_WRITE);
    if (!f) {
      Serial.println("NotifSounds: Failed to save config");
      return;
    }

    NotifSoundCfgHeader hdr;
    hdr.magic = NOTIF_SOUND_MAGIC;
    hdr.version = NOTIF_SOUND_VERSION;
    hdr.count = NOTIF_SOUND_SLOTS;
    hdr.reserved[0] = 0;
    hdr.reserved[1] = 0;
    f.write((uint8_t*)&hdr, sizeof(hdr));

    for (int i = 0; i < NOTIF_SOUND_SLOTS; i++) {
      f.write((uint8_t*)_sounds[i], NOTIF_SOUND_NAME_MAX);
    }

    f.close();
    digitalWrite(SDCARD_CS, HIGH);
  }
};

// Global singleton
extern NotifSounds notifSounds;

#endif // NOTIF_SOUNDS_H
#endif // MECK_AUDIO_VARIANT
