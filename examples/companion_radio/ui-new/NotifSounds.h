#pragma once

// =============================================================================
// NotifSounds.h -- Per-channel notification sound configuration
//
// Stores a custom sound filename per channel for notification tones.
// Config persisted to /meshcore/notif_sounds.cfg on SD card.
//
// Audio variant: Sound files are MP3s in /alarms/ (shared with alarm clock).
//   Playback is request-based: UITask calls requestPlay() when a message
//   arrives on a channel with a custom tone.  main.cpp loop() polls
//   hasPendingPlay() and drives the Audio* object.
//
// 4G variant: Sound files are 8kHz mono WAVs embedded in firmware
//   (ModemBundledSounds.h) and transferred to modem C:/ filesystem on boot.
//   UITask calls modemManager.requestNotifTone() directly.
//
// Guard: MECK_AUDIO_VARIANT || HAS_4G_MODEM
// =============================================================================

#if defined(MECK_AUDIO_VARIANT) || defined(HAS_4G_MODEM)

#ifndef NOTIF_SOUNDS_H
#define NOTIF_SOUNDS_H

#include <Arduino.h>
#include <SD.h>
#include <vector>
#include <algorithm>
#include "variant.h"

#ifdef HAS_4G_MODEM
#include "ModemBundledSounds.h"
#endif

#ifndef MAX_GROUP_CHANNELS
#define MAX_GROUP_CHANNELS 20
#endif

#define NOTIF_SOUND_NAME_MAX   32
// Reserved one-byte sentinel stored in a channel's name slot to mean
// "Buzzer (vibrate)" instead of a sound filename. 0x01 can never be the first
// byte of a real filename, so it is unambiguous and needs no config change.
#define NOTIF_VIBRATE_MARKER   "\x01"
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
  NotifSounds() {
    memset(_sounds, 0, sizeof(_sounds));
#ifdef MECK_AUDIO_VARIANT
    _pendingPlay = false;
    _pendingFile[0] = '\0';
#endif
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

  // --- Vibrate (haptic) selection ---
  // "Buzzer (vibrate)" is stored as a reserved marker in the channel's name
  // slot, so it persists through the existing config save/load unchanged.
  bool isVibrateForChannel(uint8_t channel_idx) const {
    const char* s = getSoundForChannel(channel_idx);
    return s && (uint8_t)s[0] == (uint8_t)NOTIF_VIBRATE_MARKER[0];
  }

  void setVibrateForChannel(uint8_t channel_idx) {
    setSoundForChannel(channel_idx, NOTIF_VIBRATE_MARKER);
  }

  // --- Sound file scanning ---

  void scanSoundFiles() {
    _soundFiles.clear();

#ifdef HAS_4G_MODEM
    // 4G variant: available tones are embedded in firmware
    for (int i = 0; i < MODEM_BUNDLED_TONE_COUNT; i++) {
      _soundFiles.push_back(String(modemBundledTones[i].filename));
    }
    Serial.printf("NotifSounds: %d modem tones available\n", (int)_soundFiles.size());

#else  // MECK_AUDIO_VARIANT
    // Audio variant: scan SD card /alarms/ folder for MP3 files
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
#endif
  }

  int getSoundFileCount() const { return (int)_soundFiles.size(); }
  const String& getSoundFile(int idx) const { return _soundFiles[idx]; }
  const std::vector<String>& getSoundFiles() const { return _soundFiles; }

#ifdef HAS_4G_MODEM
  // Get the display label for a tone file (4G variant only).
  // Returns the human-readable label from ModemBundledSounds.
  const char* getToneLabel(int idx) const {
    if (idx < 0 || idx >= MODEM_BUNDLED_TONE_COUNT) return "";
    return modemBundledTones[idx].label;
  }
#endif

  // --- Pending playback request (audio variant only) ---
  // The 4G variant calls modemManager.requestNotifTone() directly
  // from UITask; no pending mechanism needed.

#ifdef MECK_AUDIO_VARIANT
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
#endif

private:
  char _sounds[NOTIF_SOUND_SLOTS][NOTIF_SOUND_NAME_MAX];
  std::vector<String> _soundFiles;

#ifdef MECK_AUDIO_VARIANT
  bool _pendingPlay;
  char _pendingFile[48];
#endif

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
#endif // MECK_AUDIO_VARIANT || HAS_4G_MODEM