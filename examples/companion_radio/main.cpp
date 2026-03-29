#include <Arduino.h>   // needed for PlatformIO
#ifdef BLE_PIN_CODE
  #include <esp_bt.h>    // for esp_bt_controller_mem_release (web reader WiFi)
#endif
#ifdef MECK_OTA_UPDATE
  #include <esp_ota_ops.h>
#endif
#include <Mesh.h>
#include "MyMesh.h"
#include "variant.h"   // Board-specific defines (HAS_GPS, etc.)
#include "target.h"    // For sensors, board, etc.
#include "CPUPowerManager.h"

// T-Deck Pro Keyboard support
#if defined(LilyGo_TDeck_Pro)
  #include "TCA8418Keyboard.h"
  #include <SD.h>
  #include "TextReaderScreen.h"
  #include "NotesScreen.h"
  #include "ContactsScreen.h"
  #include "ChannelScreen.h"
  #include "SettingsScreen.h"
  #include "RepeaterAdminScreen.h"
  #include "DiscoveryScreen.h"
  #include "LastHeardScreen.h"
  #ifdef MECK_WEB_READER
    #include "WebReaderScreen.h"
  #endif
  extern SPIClass displaySpi;  // From GxEPDDisplay.cpp, shared SPI bus

  TCA8418Keyboard keyboard(I2C_ADDR_KEYBOARD, &Wire);
  
  // Compose mode state
  static bool composeMode = false;
  static char composeBuffer[138];  // 137 bytes max + null terminator (matches BLE wire cost)
  static int composePos = 0;       // Current wire-cost byte count
  static uint8_t composeChannelIdx = 0;
  static unsigned long lastComposeRefresh = 0;
  static bool composeNeedsRefresh = false;
  #define COMPOSE_REFRESH_INTERVAL 100  // ms before starting e-ink refresh after keypress (refresh itself takes ~644ms)

  // Phone dialer debounce — independent from compose/smsSuppressLoop to avoid
  // interfering with call view rendering and alert display
  static bool dialerNeedsRefresh = false;
  static unsigned long lastDialerRefresh = 0;

  // DM compose mode (direct message to a specific contact)
  static bool composeDM = false;
  static int composeDMContactIdx = -1;
  static char composeDMName[32];
  #ifdef MECK_WEB_READER
  static unsigned long lastWebReaderRefresh = 0;
  static bool webReaderNeedsRefresh = false;
  static bool webReaderTextEntry = false;  // True when URL/password entry active
  #endif
  // Emoji picker state
  #include "EmojiPicker.h"
  static bool emojiPickerMode = false;
  static EmojiPicker emojiPicker;
  
  // Text reader mode state
  static bool readerMode = false;

  // Notes mode state
  static bool notesMode = false;

  // Audiobook player â€” Audio object is heap-allocated on first use to avoid
  // consuming ~40KB of DMA/decode buffers at boot (starves BLE stack).
  // Audiobook player — Audio object is heap-allocated on first use to avoid
  // consuming ~40KB of DMA/decode buffers at boot (starves BLE stack).
  // Not available on 4G variant (I2S pins conflict with modem control lines).
  #ifndef HAS_4G_MODEM
    #include "AudiobookPlayerScreen.h"
    #include "Audio.h"
    Audio* audio = nullptr;
  #endif
  #ifdef MECK_AUDIO_VARIANT
    #include "VoiceMessageScreen.h"
  #endif
  static bool audiobookMode = false;
  static bool voiceMode = false;

  #ifdef HAS_4G_MODEM
    #include "ModemManager.h"
    #include "SMSStore.h"
    #include "SMSContacts.h"
    #include "SMSScreen.h"
    static bool smsMode = false;
  #endif

  // Touch input (for phone dialer numpad)
  #ifdef HAS_TOUCHSCREEN
    #include "TouchInput.h"
    TouchInput touchInput(&Wire);
  #endif

  void initKeyboard();
  void handleKeyboardInput();
  void drawComposeScreen();
  void drawEmojiPicker();
  void sendComposedMessage();

  // SD-backed persistence state
  static bool sdCardReady = false;

  // ---------------------------------------------------------------------------
  // SD Settings Backup / Restore
  // ---------------------------------------------------------------------------
  // Copies a file byte-for-byte between two filesystem objects.
  // Works across SPIFFS <-> SD because both use the Arduino File API.
  static bool copyFile(fs::FS& srcFS, const char* srcPath,
                       fs::FS& dstFS, const char* dstPath) {
    File src = srcFS.open(srcPath, "r");
    if (!src) return false;
    File dst = dstFS.open(dstPath, "w", true);
    if (!dst) { src.close(); return false; }

    uint8_t buf[128];
    while (src.available()) {
      int n = src.read(buf, sizeof(buf));
      if (n > 0) dst.write(buf, n);
    }
    src.close();
    dst.close();
    return true;
  }

  // Backup prefs, channels, and identity from SPIFFS to SD card.
  // Called after any savePrefs() to keep the SD mirror current.
  void backupSettingsToSD() {
    if (!sdCardReady) return;

    if (!SD.exists("/meshcore")) SD.mkdir("/meshcore");

    if (SPIFFS.exists("/new_prefs")) {
      copyFile(SPIFFS, "/new_prefs", SD, "/meshcore/prefs.bin");
    }
    // Channels may live on SPIFFS or ExtraFS - on ESP32 they are on SPIFFS
    if (SPIFFS.exists("/channels2")) {
      copyFile(SPIFFS, "/channels2", SD, "/meshcore/channels.bin");
    }
    // Identity
    if (SPIFFS.exists("/identity/_main.id")) {
      if (!SD.exists("/meshcore/identity")) SD.mkdir("/meshcore/identity");
      copyFile(SPIFFS, "/identity/_main.id", SD, "/meshcore/identity/_main.id");
    }
    // Contacts
    if (SPIFFS.exists("/contacts3")) {
      copyFile(SPIFFS, "/contacts3", SD, "/meshcore/contacts.bin");
    }

    digitalWrite(SDCARD_CS, HIGH);  // Release SD CS
    Serial.println("Settings backed up to SD");
  }

  // Restore prefs, channels, and identity from SD card to SPIFFS.
  // Called at boot if SPIFFS prefs file is missing (e.g. after a fresh flash).
  // Returns true if anything was restored.
  bool restoreSettingsFromSD() {
    if (!sdCardReady) return false;
    
    bool restored = false;

    // Only restore if SPIFFS is missing the prefs file (fresh flash)
    if (!SPIFFS.exists("/new_prefs") && SD.exists("/meshcore/prefs.bin")) {
      if (copyFile(SD, "/meshcore/prefs.bin", SPIFFS, "/new_prefs")) {
        Serial.println("Restored prefs from SD");
        restored = true;
      }
    }

    if (!SPIFFS.exists("/channels2") && SD.exists("/meshcore/channels.bin")) {
      if (copyFile(SD, "/meshcore/channels.bin", SPIFFS, "/channels2")) {
        Serial.println("Restored channels from SD");
        restored = true;
      }
    }

    // Identity - most critical; keeps the same device pub key across reflashes
    if (!SPIFFS.exists("/identity/_main.id") && SD.exists("/meshcore/identity/_main.id")) {
      SPIFFS.mkdir("/identity");
      if (copyFile(SD, "/meshcore/identity/_main.id", SPIFFS, "/identity/_main.id")) {
        Serial.println("Restored identity from SD");
        restored = true;
      }
    }

    if (!SPIFFS.exists("/contacts3") && SD.exists("/meshcore/contacts.bin")) {
      if (copyFile(SD, "/meshcore/contacts.bin", SPIFFS, "/contacts3")) {
        Serial.println("Restored contacts from SD");
        restored = true;
      }
    }

    if (restored) {
      Serial.println("=== Settings restored from SD card backup ===");
    }
    digitalWrite(SDCARD_CS, HIGH);
    return restored;
  }

  // -----------------------------------------------------------------------
  // On-demand export: save current contacts to SD card.
  // Writes binary backup + human-readable listing.
  // Returns number of contacts exported, or -1 on error.
  // -----------------------------------------------------------------------
  int exportContactsToSD() {
    if (!sdCardReady) {
      Serial.println("Export: SD card not ready");
      return -1;
    }

    // Ensure in-memory contacts are flushed to SPIFFS first
    the_mesh.saveContacts();

    if (!SD.exists("/meshcore")) SD.mkdir("/meshcore");

    // 1) Binary backup: SPIFFS /contacts3 → SD /meshcore/contacts.bin
    //    Non-fatal — text export reads from memory and doesn't need this.
    if (SPIFFS.exists("/contacts3")) {
      if (copyFile(SPIFFS, "/contacts3", SD, "/meshcore/contacts.bin")) {
        Serial.println("Export: binary backup OK");
      } else {
        Serial.println("Export: binary copy to SD failed (continuing with text export)");
      }
    } else {
      Serial.println("Export: /contacts3 not found on SPIFFS (skipping binary backup)");
    }

    // 2) Human-readable listing for inspection on a computer
    //    Reads from in-memory contact table — always works if SD is writable.
    int count = 0;
    File txt = SD.open("/meshcore/contacts_export.txt", "w", true);
    if (!txt) {
      Serial.println("Export: failed to open contacts_export.txt for writing");
      digitalWrite(SDCARD_CS, HIGH);
      return -1;
    }

    txt.printf("Meck Contacts Export  (%d total)\n", (int)the_mesh.getNumContacts());
    txt.printf("========================================\n");
    txt.printf("%-5s  %-30s  %s\n", "Type", "Name", "PubKey (prefix)");
    txt.printf("----------------------------------------\n");

    ContactInfo c;
    for (uint32_t i = 0; i < (uint32_t)the_mesh.getNumContacts(); i++) {
      if (the_mesh.getContactByIdx(i, c)) {
        const char* typeStr = "???";
        switch (c.type) {
          case ADV_TYPE_CHAT:     typeStr = "Chat"; break;
          case ADV_TYPE_REPEATER: typeStr = "Rptr"; break;
          case ADV_TYPE_ROOM:     typeStr = "Room"; break;
        }
        // First 8 bytes of pub key as hex identifier
        char hexBuf[20];
        mesh::Utils::toHex(hexBuf, c.id.pub_key, 8);
        txt.printf("%-5s  %-30s  %s\n", typeStr, c.name, hexBuf);
        count++;
      }
    }

    txt.printf("========================================\n");
    txt.printf("Total: %d contacts\n", count);
    txt.close();

    digitalWrite(SDCARD_CS, HIGH);
    Serial.printf("Contacts exported to SD: %d contacts\n", count);
    return count;
  }

  // -----------------------------------------------------------------------
  // On-demand import: merge contacts from SD backup into live table.
  //
  // Reads /meshcore/contacts.bin from SD and for each contact:
  //   - If already in memory (matching pub_key) → skip (keep current)
  //   - If NOT in memory → addContact (append to table)
  //
  // This is a non-destructive merge: you never lose contacts already in
  // memory, and you gain any that were only in the backup.
  //
  // After merging, saves the combined set back to SPIFFS so it persists.
  // Returns number of NEW contacts added, or -1 on error.
  // -----------------------------------------------------------------------
  int importContactsFromSD() {
    if (!sdCardReady) return -1;
    if (!SD.exists("/meshcore/contacts.bin")) return -1;

    File file = SD.open("/meshcore/contacts.bin", "r");
    if (!file) return -1;

    int added = 0;
    int skipped = 0;

    while (true) {
      ContactInfo c;
      uint8_t pub_key[32];
      uint8_t unused;

      // Parse one contact record (same binary format as DataStore::loadContacts)
      bool success = (file.read(pub_key, 32) == 32);
      success = success && (file.read((uint8_t *)&c.name, 32) == 32);
      success = success && (file.read(&c.type, 1) == 1);
      success = success && (file.read(&c.flags, 1) == 1);
      success = success && (file.read(&unused, 1) == 1);
      success = success && (file.read((uint8_t *)&c.sync_since, 4) == 4);
      success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
      success = success && (file.read((uint8_t *)&c.last_advert_timestamp, 4) == 4);
      success = success && (file.read(c.out_path, 64) == 64);
      success = success && (file.read((uint8_t *)&c.lastmod, 4) == 4);
      success = success && (file.read((uint8_t *)&c.gps_lat, 4) == 4);
      success = success && (file.read((uint8_t *)&c.gps_lon, 4) == 4);

      if (!success) break;  // EOF or read error

      c.id = mesh::Identity(pub_key);
      c.shared_secret_valid = false;

      // Check if this contact already exists in the live table
      if (the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE) != NULL) {
        skipped++;
        continue;  // Already have this contact, skip
      }

      // New contact — add to the live table
      if (the_mesh.addContact(c)) {
        added++;
      } else {
        // Table is full, stop importing
        Serial.printf("Import: table full after adding %d contacts\n", added);
        break;
      }
    }

    file.close();
    digitalWrite(SDCARD_CS, HIGH);

    // Persist the merged set to SPIFFS
    if (added > 0) {
      the_mesh.saveContacts();
    }

    Serial.printf("Contacts import: %d added, %d already present, %d total\n",
                  added, skipped, (int)the_mesh.getNumContacts());
    return added;
  }
#endif

// =============================================================================
// Touch Input — unified across T5S3 (GT911) and T-Deck Pro (CST328)
// =============================================================================

// Define MECK_TOUCH_ENABLED for any platform with touch support
#if defined(LilyGo_T5S3_EPaper_Pro) || (defined(LilyGo_TDeck_Pro) && defined(HAS_TOUCHSCREEN))
  #define MECK_TOUCH_ENABLED 1
#endif

// --- T5S3: GT911 capacitive touch driver ---
#if defined(LilyGo_T5S3_EPaper_Pro)
  #include "TouchDrvGT911.hpp"
  #include <SD.h>
  #include "TextReaderScreen.h"
  #include "NotesScreen.h"
  #include "ContactsScreen.h"
  #include "ChannelScreen.h"
  #include "SettingsScreen.h"
  #include "RepeaterAdminScreen.h"
  #include "DiscoveryScreen.h"
  #include "LastHeardScreen.h"

  static TouchDrvGT911 gt911Touch;
  static bool gt911Ready = false;
  static bool sdCardReady = false;  // T5S3 SD card state

  // ---------------------------------------------------------------------------
  // SD Settings Backup / Restore (T5S3)
  // ---------------------------------------------------------------------------
  static bool copyFile(fs::FS& srcFS, const char* srcPath,
                       fs::FS& dstFS, const char* dstPath) {
    File src = srcFS.open(srcPath, "r");
    if (!src) return false;
    File dst = dstFS.open(dstPath, "w", true);
    if (!dst) { src.close(); return false; }

    uint8_t buf[128];
    while (src.available()) {
      int n = src.read(buf, sizeof(buf));
      if (n > 0) dst.write(buf, n);
    }
    src.close();
    dst.close();
    return true;
  }

  void backupSettingsToSD() {
    if (!sdCardReady) return;

    if (!SD.exists("/meshcore")) SD.mkdir("/meshcore");

    if (SPIFFS.exists("/new_prefs")) {
      copyFile(SPIFFS, "/new_prefs", SD, "/meshcore/prefs.bin");
    }
    if (SPIFFS.exists("/channels2")) {
      copyFile(SPIFFS, "/channels2", SD, "/meshcore/channels.bin");
    }
    if (SPIFFS.exists("/identity/_main.id")) {
      if (!SD.exists("/meshcore/identity")) SD.mkdir("/meshcore/identity");
      copyFile(SPIFFS, "/identity/_main.id", SD, "/meshcore/identity/_main.id");
    }
    if (SPIFFS.exists("/contacts3")) {
      copyFile(SPIFFS, "/contacts3", SD, "/meshcore/contacts.bin");
    }

    digitalWrite(SDCARD_CS, HIGH);
    Serial.println("Settings backed up to SD");
  }

  bool restoreSettingsFromSD() {
    if (!sdCardReady) return false;

    bool restored = false;

    if (!SPIFFS.exists("/new_prefs") && SD.exists("/meshcore/prefs.bin")) {
      if (copyFile(SD, "/meshcore/prefs.bin", SPIFFS, "/new_prefs")) {
        Serial.println("Restored prefs from SD");
        restored = true;
      }
    }
    if (!SPIFFS.exists("/channels2") && SD.exists("/meshcore/channels.bin")) {
      if (copyFile(SD, "/meshcore/channels.bin", SPIFFS, "/channels2")) {
        Serial.println("Restored channels from SD");
        restored = true;
      }
    }
    if (!SPIFFS.exists("/identity/_main.id") && SD.exists("/meshcore/identity/_main.id")) {
      SPIFFS.mkdir("/identity");
      if (copyFile(SD, "/meshcore/identity/_main.id", SPIFFS, "/identity/_main.id")) {
        Serial.println("Restored identity from SD");
        restored = true;
      }
    }
    if (!SPIFFS.exists("/contacts3") && SD.exists("/meshcore/contacts.bin")) {
      if (copyFile(SD, "/meshcore/contacts.bin", SPIFFS, "/contacts3")) {
        Serial.println("Restored contacts from SD");
        restored = true;
      }
    }

    if (restored) {
      Serial.println("=== Settings restored from SD card backup ===");
    }
    digitalWrite(SDCARD_CS, HIGH);
    return restored;
  }

#ifdef MECK_CARDKB
  #include "CardKBKeyboard.h"
  static CardKBKeyboard cardkb;
  static unsigned long lastCardKBProbe = 0;
  #define CARDKB_PROBE_INTERVAL_MS 5000  // Re-probe for hot-plug every 5s
#endif

  // Read GT911 in landscape orientation (960×540)
  static bool readTouchLandscape(int16_t* outX, int16_t* outY) {
    if (!gt911Ready) return false;
    int16_t raw_x, raw_y;
    if (gt911Touch.getPoint(&raw_x, &raw_y)) {
      *outX = raw_y;
      *outY = EPD_HEIGHT - 1 - raw_x;
      return true;
    }
    return false;
  }

  // Read GT911 in portrait orientation (540×960, canvas rotation 3)
  static bool readTouchPortrait(int16_t* outX, int16_t* outY) {
    if (!gt911Ready) return false;
    int16_t raw_x, raw_y;
    if (gt911Touch.getPoint(&raw_x, &raw_y)) {
      *outX = raw_x;
      *outY = raw_y;
      return true;
    }
    return false;
  }
#endif

// --- Shared touch state machine variables ---
#ifdef MECK_TOUCH_ENABLED
  static bool touchDown = false;
  static unsigned long touchDownTime = 0;
  static int16_t touchDownX = 0;
  static int16_t touchDownY = 0;
  static int16_t touchLastX = 0;
  static int16_t touchLastY = 0;
  static unsigned long lastTouchSeenMs = 0;
  #define TOUCH_LONG_PRESS_MS  750
  #if defined(LilyGo_T5S3_EPaper_Pro)
    #define TOUCH_SWIPE_THRESHOLD 60   // T5S3: 960×540 — 60px ≈ 6% of width
  #else
    #define TOUCH_SWIPE_THRESHOLD 30   // T-Deck Pro: 240×320 — 30px ≈ 12.5% of width
  #endif
  #define TOUCH_LIFT_DEBOUNCE_MS 150
  #define TOUCH_MIN_INTERVAL_MS 300
  static bool longPressHandled = false;
  static bool swipeHandled = false;
  static bool touchCooldown = false;
  static unsigned long lastTouchEventMs = 0;

  // Unified touch reader — returns physical screen coordinates
  static bool readTouch(int16_t* outX, int16_t* outY) {
  #if defined(LilyGo_T5S3_EPaper_Pro)
    if (display.isPortraitMode()) {
      return readTouchPortrait(outX, outY);
    }
    return readTouchLandscape(outX, outY);
  #elif defined(LilyGo_TDeck_Pro)
    return touchInput.getPoint(*outX, *outY);
  #else
    return false;
  #endif
  }

  // Convert physical touch coords to virtual 128×128 coordinate space
  static void touchToVirtual(int16_t px, int16_t py, int& vx, int& vy) {
  #if defined(LilyGo_T5S3_EPaper_Pro)
    float sx = display.isPortraitMode() ? ((float)EPD_HEIGHT / 128.0f) : ((float)EPD_WIDTH / 128.0f);
    float sy = display.isPortraitMode() ? ((float)EPD_WIDTH / 128.0f) : ((float)EPD_HEIGHT / 128.0f);
  #elif defined(LilyGo_TDeck_Pro)
    float sx = (float)EINK_WIDTH / 128.0f;   // 240/128 = 1.875
    float sy = (float)EINK_HEIGHT / 128.0f;   // 320/128 = 2.5
  #endif
    vx = (int)(px / sx);
  #if defined(LilyGo_TDeck_Pro)
    // Subtract EINK_Y_OFFSET to align touch coords with virtual render coords.
    // GxEPD setCursor/fillRect add offset_y (5) internally, so raw physical→virtual
    // is off by that amount.
    vy = (int)(py / sy) - EINK_Y_OFFSET;
  #else
    vy = (int)(py / sy);
  #endif
  }
#endif

// Board-agnostic: CPU frequency scaling and AGC reset
CPUPowerManager cpuPower;
#define AGC_RESET_INTERVAL_MS 500
static unsigned long lastAGCReset = 0;

// Believe it or not, this std C function is busted on some platforms!
static uint32_t _atoi(const char* sp) {
  uint32_t n = 0;
  while (*sp && *sp >= '0' && *sp <= '9') {
    n *= 10;
    n += (*sp++ - '0');
  }
  return n;
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #include <InternalFileSystem.h>
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
    DataStore store(InternalFS, QSPIFlash, rtc_clock);
  #else
  #if defined(EXTRAFS)
    #include <CustomLFS.h>
    CustomLFS ExtraFS(0xD4000, 0x19000, 128);
    DataStore store(InternalFS, ExtraFS, rtc_clock);
  #else
    DataStore store(InternalFS, rtc_clock);
  #endif
  #endif
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
  DataStore store(LittleFS, rtc_clock);
#elif defined(ESP32)
  #include <SPIFFS.h>
  DataStore store(SPIFFS, rtc_clock);
#endif

#ifdef ESP32
  #ifdef WIFI_SSID
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(MECK_WIFI_COMPANION)
    #include <WiFi.h>
    #include <helpers/esp32/SerialWifiInterface.h>
    SerialWifiInterface serial_interface;
    #ifndef TCP_PORT
      #define TCP_PORT 5000
    #endif
  #elif defined(BLE_PIN_CODE)
    #include <helpers/esp32/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #elif defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(RP2040_PLATFORM)
  //#ifdef WIFI_SSID
  //  #include <helpers/rp2040/SerialWifiInterface.h>
  //  SerialWifiInterface serial_interface;
  //  #ifndef TCP_PORT
  //    #define TCP_PORT 5000
  //  #endif
  // #elif defined(BLE_PIN_CODE)
  //   #include <helpers/rp2040/SerialBLEInterface.h>
  //   SerialBLEInterface serial_interface;
  #if defined(SERIAL_RX)
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
    HardwareSerial companion_serial(1);
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(NRF52_PLATFORM)
  #ifdef BLE_PIN_CODE
    #include <helpers/nrf52/SerialBLEInterface.h>
    SerialBLEInterface serial_interface;
  #else
    #include <helpers/ArduinoSerialInterface.h>
    ArduinoSerialInterface serial_interface;
  #endif
#elif defined(STM32_PLATFORM)
  #include <helpers/ArduinoSerialInterface.h>
  ArduinoSerialInterface serial_interface;
#else
  #error "need to define a serial interface"
#endif

/* GLOBAL OBJECTS */
#ifdef DISPLAY_CLASS
  #include "UITask.h"
  #if HAS_GPS
    #include "MapScreen.h"  // After BLE — PNGdec headers conflict with BLE if included earlier
  #endif
  UITask ui_task(&board, &serial_interface);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;
MyMesh the_mesh(radio_driver, fast_rng, rtc_clock, tables, store
   #ifdef DISPLAY_CLASS
      , &ui_task
   #endif
);

/* END GLOBAL OBJECTS */

// ---------------------------------------------------------------------------
// Voice-over-LoRa: incoming raw packet handler (dz0ny VE3 protocol)
// Registered with the_mesh.setVoiceHandler() when voice screen is created.
// Called from onRawDataRecv for magic 0x56 (voice data) and 0x72 (fetch req).
// ---------------------------------------------------------------------------
#ifdef MECK_AUDIO_VARIANT
// Helper: ensure voice screen exists (lazy-init for incoming voice)
static VoiceMessageScreen* ensureVoiceScreen() {
  VoiceMessageScreen* voiceScr = (VoiceMessageScreen*)ui_task.getVoiceScreen();
  if (!voiceScr) {
    Serial.println("Voice: Auto-creating voice screen for incoming message");
    if (!audio) audio = new Audio();
    voiceScr = new VoiceMessageScreen(&ui_task, audio);
    voiceScr->setSDReady(sdCardReady);
    ui_task.setVoiceScreen(voiceScr);
  }
  return voiceScr;
}

static void voiceRawCallback(uint8_t magic, const uint8_t* payload, uint8_t len) {
  VoiceMessageScreen* voiceScr = ensureVoiceScreen();

  if (magic == 0x72 && len >= 6) {
    // Fetch request: [0x72][sessionId:4B][flags:1B][requesterKey6:6B][missingCount:1B][indices...]
    uint32_t sessionId;
    memcpy(&sessionId, &payload[1], 4);
    Serial.printf("Voice: Fetch request for session 0x%08X\n", sessionId);

    if (voiceScr->getOutSessionId() == sessionId && voiceScr->hasValidOutSession()) {
      uint8_t pktBuf[184];
      int totalPkts = voiceScr->getOutSessionPacketCount();
      Serial.printf("Voice: Serving %d packets for session 0x%08X\n", totalPkts, sessionId);

      // Requester's 6-byte key prefix is at payload[6..11].
      // Look them up to get their path for sendDirect.
      if (len >= 12) {
        ContactInfo* requester = the_mesh.lookupContactByPubKey(&payload[6], 6);
        if (requester && requester->out_path_len != OUT_PATH_UNKNOWN) {
          for (int p = 0; p < totalPkts; p++) {
            int pktLen = voiceScr->buildVoicePacket(pktBuf, sizeof(pktBuf), sessionId, p);
            if (pktLen > 0) {
              mesh::Packet* raw = the_mesh.createRawData(pktBuf, pktLen);
              if (raw) {
                the_mesh.sendDirect(raw, requester->out_path, requester->out_path_len, p * 100);
              }
            }
          }
          Serial.printf("Voice: Served %d packets to %s\n", totalPkts, requester->name);
        } else {
          Serial.println("Voice: Fetch requester not found or no direct path");
        }
      }
    } else {
      Serial.printf("Voice: No cached session 0x%08X for fetch\n", sessionId);
    }
  } else if (magic == 0x56 && len > 6) {
    // Incoming voice data packet — feed to incoming session accumulator
    voiceScr->onVoicePacketReceived(payload, len);
  }
}

// Voice envelope callback — called from MyMesh::onMessageRecv when a VE3: DM arrives
static void voiceEnvelopeCallback(const char* senderName, const char* ve3Text) {
  VoiceMessageScreen* voiceScr = ensureVoiceScreen();
  voiceScr->onVE3Received(senderName, ve3Text);
  // Defer SD contact saves while voice packets are arriving —
  // SD writes block the SPI bus shared with LoRa radio
  the_mesh.setDeferSaves(true);
  Serial.println("Voice: Deferring contact saves during voice receive");
}
#endif

// Last Heard: add/remove contact for selected entry.
// Called from both touch double-tap (mapTouchTap) and keyboard Enter handler.
#ifdef DISPLAY_CLASS
static void lastHeardToggleContact() {
  LastHeardScreen* lh = (LastHeardScreen*)ui_task.getLastHeardScreen();
  if (!lh) return;
  const AdvertPath* entry = lh->getSelectedEntry();
  if (!entry) return;

  ContactInfo* existing = the_mesh.lookupContactByPubKey(entry->pubkey_prefix, 8);
  if (existing) {
    // Double-confirm for favourites (bit 0 of flags)
    static unsigned long lastRemoveAttempt = 0;
    static uint8_t lastRemovePrefix[8] = {0};
    bool isFav = (existing->flags & 0x01) != 0;

    if (isFav) {
      if (millis() - lastRemoveAttempt < 3000 &&
          memcmp(lastRemovePrefix, entry->pubkey_prefix, 8) == 0) {
        // Second press within 3s — confirmed
      } else {
        // First press on favourite — warn and wait
        lastRemoveAttempt = millis();
        memcpy(lastRemovePrefix, entry->pubkey_prefix, 8);
        ui_task.showAlert("Favourite! Press again", 2500);
        return;
      }
    }

    the_mesh.removeContact(*existing);
    the_mesh.scheduleLazyContactSave();
    char alertBuf[40];
    snprintf(alertBuf, sizeof(alertBuf), "Removed: %s", entry->name);
    ui_task.showAlert(alertBuf, 1500);
    Serial.printf("[LastHeard] Removed: %s\n", entry->name);
  } else {
    uint8_t blob[256];
    int blobLen = the_mesh.getContactBlob(entry->pubkey_prefix, 8, blob);
    if (blobLen > 0) {
      the_mesh.importContact(blob, blobLen);
      the_mesh.scheduleLazyContactSave();
      char alertBuf[40];
      snprintf(alertBuf, sizeof(alertBuf), "Added: %s", entry->name);
      ui_task.showAlert(alertBuf, 1500);
      Serial.printf("[LastHeard] Added: %s\n", entry->name);
    } else {
      // Blob store is limited to 100 entries — with many contacts, blobs
      // from non-contact nodes get evicted quickly. User needs to wait
      // for the node to re-broadcast its advert.
      ui_task.showAlert("Advert expired, try later", 2000);
      Serial.printf("[LastHeard] Blob evicted for %s (store full)\n", entry->name);
    }
  }
  ui_task.forceRefresh();
}
#endif

// Touch mapping — must be after ui_task declaration
#ifdef MECK_TOUCH_ENABLED
  // Map a single tap based on current screen context
  static char mapTouchTap(int16_t x, int16_t y) {
    // Convert physical screen coords to virtual (128×128)
    int vx, vy;
    touchToVirtual(x, y, vx, vy);

    // Dismiss boot navigation hint on any tap
    if (ui_task.isHintActive()) {
      ui_task.dismissBootHint();
      return 0;
    }

    // --- Status bar tap (top ~18 virtual units) → go home from any non-home screen ---
    // Exception: text reader reading mode uses full screen for content (no header)
    if (vy < 18 && !ui_task.isOnHomeScreen()) {
      if (ui_task.isOnTextReader()) {
        TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
        if (reader && reader->isReading()) {
          return 'd';  // reading mode: treat as next page
        }
      }
      ui_task.gotoHomeScreen();
      return 0;
    }

    // Home screen FIRST page: tile taps (virtual coordinate hit test)
    if (ui_task.isOnHomeScreen() && ui_task.isHomeShowingTiles()) {
      const int tileW = 40, tileH = 28, gapX = 1, gapY = 1;
      const int gridW = tileW * 3 + gapX * 2;
      const int gridX = (128 - gridW) / 2;  // =3
      int gridY = ui_task.getTileGridVY();

      // Check if tap is within the tile grid area
      if (vx >= gridX && vx < gridX + gridW &&
          vy >= gridY && vy < gridY + 2 * (tileH + gapY)) {
        int col = (vx - gridX) / (tileW + gapX);
        if (col > 2) col = 2;
        int row = (vy - gridY) / (tileH + gapY);
        if (row > 1) row = 1;

        if (row == 0 && col == 0) { ui_task.gotoChannelScreen(); return 0; }
        if (row == 0 && col == 1) { ui_task.gotoContactsScreen(); return 0; }
        if (row == 0 && col == 2) { ui_task.gotoSettingsScreen(); return 0; }
        if (row == 1 && col == 0) { ui_task.gotoTextReader(); return 0; }
        if (row == 1 && col == 1) { ui_task.gotoNotesScreen(); return 0; }
#ifdef MECK_WEB_READER
        if (row == 1 && col == 2) { ui_task.gotoWebReader(); return 0; }
#else
        if (row == 1 && col == 2) { ui_task.gotoDiscoveryScreen(); return 0; }
#endif
      }
      // Tap outside tiles — left half backward, right half forward
      return (vx < 64) ? (char)KEY_PREV : (char)KEY_NEXT;
    }

    // Home screen (non-tile pages): left half taps backward, right half forward
    // Exception: on Recent Adverts page, bottom area tap opens Last Heard
    if (ui_task.isOnHomeScreen()) {
      if (ui_task.isHomeOnRecentPage() && vy >= 100) {
        ui_task.gotoLastHeardScreen();
        return 0;
      }
      return (vx < 64) ? (char)KEY_PREV : (char)KEY_NEXT;
    }

    // Reader: tap in reading mode; in file list = select row
    if (ui_task.isOnTextReader()) {
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader && reader->isReading()) {
#if defined(LilyGo_T5S3_EPaper_Pro)
        // Footer zone tap → go to page via VKB
        if (vy >= 113) {
          char label[24];
          snprintf(label, sizeof(label), "Page (1-%d)", reader->getTotalPages());
          ui_task.showVirtualKeyboard(VKB_TEXT_PAGE, label, "", 5);
          return 0;
        }
#endif
        return 'd';  // Tap anywhere else = next page
      }
      // File list: tap-to-select, double-tap to open
      if (reader && reader->isInFileList()) {
        int result = reader->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;  // Moved selection
        }
        if (result == 2) return KEY_ENTER;  // Same row — open
      }
      return 0;
    }

    // Notes editing: tap → open keyboard for typing
    if (ui_task.isOnNotesScreen()) {
      NotesScreen* notes = (NotesScreen*)ui_task.getNotesScreen();
      if (notes && notes->isEditing()) {
#if defined(LilyGo_T5S3_EPaper_Pro)
        ui_task.showVirtualKeyboard(VKB_NOTES, "Edit Note", "", 137);
#endif
        return 0;  // T-Deck Pro: keyboard handles typing directly
      }
    }

#ifdef MECK_WEB_READER
    // Web reader: context-dependent taps
    if (ui_task.isOnWebReader()) {
      WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr) {
        if (wr->isReading()) {
#if defined(LilyGo_T5S3_EPaper_Pro)
          // Footer zone tap → open link VKB (if links exist)
          if (vy >= 113 && wr->getLinkCount() > 0) {
            ui_task.showVirtualKeyboard(VKB_WEB_LINK, "Link #", "", 3);
            return 0;
          }
#endif
          return 'd';  // Tap reading area → next page
        }

        if (wr->isHome()) {
#if defined(LilyGo_T5S3_EPaper_Pro)
          int sel = wr->getHomeSelected();
          if (sel == 1) {
            ui_task.showVirtualKeyboard(VKB_WEB_URL, "Enter URL",
                                         wr->getUrlText(), WEB_MAX_URL_LEN - 1);
            return 0;
          }
          if (sel == 2) {
            ui_task.showVirtualKeyboard(VKB_WEB_SEARCH, "Search DuckDuckGo", "", 127);
            return 0;
          }
#endif
          return KEY_ENTER;  // Select current item (keyboard handles text on T-Deck Pro)
        }

        if (wr->isWifiSetup()) {
#if defined(LilyGo_T5S3_EPaper_Pro)
          if (wr->isPasswordEntry()) {
            ui_task.showVirtualKeyboard(VKB_WEB_WIFI_PASS, "WiFi Password", "", 63);
            return 0;
          }
#endif
          return KEY_ENTER;  // SSID list: select, failed: retry
        }
      }
      return KEY_ENTER;
    }
#endif

    // Channel screen: tap footer area → hop path, tap elsewhere → no action
    if (ui_task.isOnChannelScreen()) {
      ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
      if (chScr && chScr->isShowingPathOverlay()) {
        return 'q';  // Dismiss overlay on any tap
      }
      // Footer zone: bottom ~15 virtual units (≈63 physical pixels)
      if (vy >= 113) {
        return 'v';  // Show path overlay
      }
      return 0;  // Tap on message area — consumed, no action
    }

    // Contacts screen: tap to select row, tap same row to activate
    if (ui_task.isOnContactsScreen()) {
      ContactsScreen* cs = (ContactsScreen*)ui_task.getContactsScreen();
      if (cs) {
        int result = cs->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;  // Moved cursor
        }
        if (result == 2) return KEY_ENTER;  // Same row — activate
      }
      return 0;
    }

    // Discovery screen: tap to select, tap same to add
    if (ui_task.isOnDiscoveryScreen()) {
      DiscoveryScreen* ds = (DiscoveryScreen*)ui_task.getDiscoveryScreen();
      if (ds) {
        int result = ds->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;
        }
        if (result == 2) return KEY_ENTER;  // Same row — add to contacts
      }
      return 0;
    }

    // Last Heard screen: tap to select, tap same to add/remove
    if (ui_task.isOnLastHeardScreen()) {
      LastHeardScreen* lh = (LastHeardScreen*)ui_task.getLastHeardScreen();
      if (lh) {
        int result = lh->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;
        }
        if (result == 2) {
          lastHeardToggleContact();
          return 0;
        }
      }
      return 0;
    }

    // Settings screen: tap to select row, tap same row to activate
    if (ui_task.isOnSettingsScreen()) {
      SettingsScreen* ss = (SettingsScreen*)ui_task.getSettingsScreen();
      if (ss && !ss->isEditing()) {
        int result = ss->selectRowAtVY(vy);
        if (result == 1) {
          ui_task.forceRefresh();
          return 0;  // Moved cursor — just refresh, don't activate
        }
        if (result == 2) return KEY_ENTER;  // Tapped same row — activate
      }
      return KEY_ENTER;  // Editing mode or header/footer tap
    }

    // SMS screen: dedicated dialer/touch handler runs separately (HAS_4G_MODEM block)
    // Return 0 so the general handler doesn't inject spurious keys
    #ifdef HAS_4G_MODEM
    if (ui_task.isOnSMSScreen()) return 0;
    #endif

    // All other screens: tap = select
    return KEY_ENTER;
  }

  // Map a swipe direction to a key
  static char mapTouchSwipe(int16_t dx, int16_t dy) {
    bool horizontal = abs(dx) > abs(dy);

    // SMS screen: dedicated touch handler covers all interaction
    #ifdef HAS_4G_MODEM
    if (ui_task.isOnSMSScreen()) return 0;
    #endif

    // Reader (reading mode): swipe left/right for page turn
    if (ui_task.isOnTextReader()) {
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader && reader->isReading()) {
        if (horizontal) {
          return (dx < 0) ? 'd' : 'a';  // swipe left=next, right=prev
        }
        // Vertical swipe in reader: also page turn (natural scroll)
        return (dy > 0) ? 'd' : 'a';  // swipe down=next, up=prev
      }
    }

#ifdef MECK_WEB_READER
    // Web reader: page turn in reading mode, list scroll elsewhere
    if (ui_task.isOnWebReader()) {
      WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr && wr->isReading()) {
        if (horizontal) {
          return (dx < 0) ? 'd' : 'a';  // swipe left=next page, right=prev
        }
        return (dy > 0) ? 'd' : 'a';  // swipe down=next, up=prev
      }
      // HOME / WIFI_SETUP / other: vertical swipe = scroll list
      if (!horizontal) {
        return (dy > 0) ? 's' : 'w';
      }
      return 0;  // Ignore horizontal swipes on non-reading modes
    }
#endif

    // Home screen: swipe left = next page, swipe right = previous page
    if (ui_task.isOnHomeScreen()) {
      if (horizontal) {
        return (dx < 0) ? (char)KEY_NEXT : (char)KEY_PREV;
      }
      return (char)KEY_NEXT;  // vertical swipe = next (default)
    }

    // Settings: horizontal swipe → a/d for picker/number editing
    if (ui_task.isOnSettingsScreen() && horizontal) {
      return (dx < 0) ? 'd' : 'a';  // swipe left=next option, right=prev
    }

    // Channel screen: horizontal swipe → a/d to switch channels
    if (ui_task.isOnChannelScreen() && horizontal) {
      return (dx < 0) ? 'd' : 'a';  // swipe left=next channel, right=prev
    }

    // Contacts screen: horizontal swipe → a/d to change filter
    if (ui_task.isOnContactsScreen() && horizontal) {
      return (dx < 0) ? 'd' : 'a';  // swipe left=next filter, right=prev
    }

    // All other screens: vertical swipe scrolls
    if (!horizontal) {
      return (dy > 0) ? 's' : 'w';  // swipe down=scroll down, up=scroll up
    }

    return 0;  // ignore horizontal swipes on non-applicable screens
  }

  // Map a long press to a key
  static char mapTouchLongPress(int16_t x, int16_t y) {
    // SMS screen: dedicated touch handler covers all interaction
    #ifdef HAS_4G_MODEM
    if (ui_task.isOnSMSScreen()) return 0;
    #endif

    // Home screen: long press = activate current page action
    // (BLE toggle, send advert, hibernate, GPS toggle, etc.)
    if (ui_task.isOnHomeScreen()) {
      return (char)KEY_ENTER;
    }

    // Reader reading: long press = close book
    if (ui_task.isOnTextReader()) {
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader && reader->isReading()) {
        return 'q';
      }
      return KEY_ENTER;  // file list: open
    }

#ifdef MECK_WEB_READER
    // Web reader: long press for back navigation
    if (ui_task.isOnWebReader()) {
      WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr) {
        if (wr->isReading()) {
          return 'b';  // Back to previous page, or HOME if no history
        }
        if (wr->isHome()) {
          int sel = wr->getHomeSelected();
          int bmEnd = 3 + wr->getBookmarkCount();
          if (sel >= 3 && sel < bmEnd) {
            return '\b';  // Long press on bookmark → delete
          }
          return 'q';  // All others: exit web reader
        }
        if (wr->isWifiSetup()) {
          return 'q';  // Back from WiFi setup
        }
        if (wr->isIRCMode()) {
          return 'q';  // Back from IRC
        }
      }
      return 'q';  // Default: back out
    }
#endif

    // Channel screen: long press → compose to current channel (or DM actions on DM tab)
    if (ui_task.isOnChannelScreen()) {
      uint8_t chIdx = ui_task.getChannelScreenViewIdx();
      if (chIdx == 0xFF) {
        ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
        if (chScr->isDMInboxMode()) {
          // Inbox mode: long press = open selected conversation (same as Enter)
          return '\r';
        }
        // Conversation mode: long press = compose reply
#if defined(LilyGo_T5S3_EPaper_Pro)
        const char* dmName = chScr->getDMFilterName();
        if (dmName && dmName[0]) {
          uint32_t numC = the_mesh.getNumContacts();
          ContactInfo ci;
          for (uint32_t j = 0; j < numC; j++) {
            if (the_mesh.getContactByIdx(j, ci) && strcmp(ci.name, dmName) == 0) {
              char label[40];
              snprintf(label, sizeof(label), "DM: %s", dmName);
              ui_task.showVirtualKeyboard(VKB_DM, label, "", 137, j);
              ui_task.clearDMUnread(j);
              return 0;
            }
          }
        }
        ui_task.showAlert("Contact not found", 1000);
        return 0;
#else
        return KEY_ENTER;
#endif
      }
#if defined(LilyGo_T5S3_EPaper_Pro)
      ChannelDetails ch;
      if (the_mesh.getChannel(chIdx, ch)) {
        char label[40];
        snprintf(label, sizeof(label), "To: %s", ch.name);
        ui_task.showVirtualKeyboard(VKB_CHANNEL_MSG, label, "", 137, chIdx);
      }
      return 0;
#else
      return KEY_ENTER;  // T-Deck Pro: keyboard handles compose mode
#endif
    }

    // Contacts screen: long press → DM for chat contacts, admin for repeaters
    if (ui_task.isOnContactsScreen()) {
      ContactsScreen* cs = (ContactsScreen*)ui_task.getContactsScreen();
      if (cs) {
        int idx = cs->getSelectedContactIdx();
        uint8_t ctype = cs->getSelectedContactType();
#if defined(LilyGo_T5S3_EPaper_Pro)
        if (idx >= 0 && ctype == ADV_TYPE_CHAT) {
          if (ui_task.hasDMUnread(idx)) {
            char cname[32];
            cs->getSelectedContactName(cname, sizeof(cname));
            ui_task.clearDMUnread(idx);
            ui_task.gotoDMConversation(cname);
            return 0;
          }
          char dname[32];
          cs->getSelectedContactName(dname, sizeof(dname));
          char label[40];
          snprintf(label, sizeof(label), "DM: %s", dname);
          ui_task.showVirtualKeyboard(VKB_DM, label, "", 137, idx);
          return 0;
        } else if (idx >= 0 && ctype == ADV_TYPE_REPEATER) {
          ui_task.gotoRepeaterAdmin(idx);
          return 0;
        } else if (idx >= 0 && ctype == ADV_TYPE_ROOM) {
          // Room server: open login (after login, auto-redirects to conversation)
          ui_task.gotoRepeaterAdmin(idx);
          return 0;
        } else if (idx >= 0 && ui_task.hasDMUnread(idx)) {
          char cname[32];
          cs->getSelectedContactName(cname, sizeof(cname));
          ui_task.clearDMUnread(idx);
          ui_task.gotoDMConversation(cname);
          return 0;
        }
#else
        // T-Deck Pro: repeater admin works directly, DM via keyboard compose
        if (idx >= 0 && ctype == ADV_TYPE_REPEATER) {
          ui_task.gotoRepeaterAdmin(idx);
          return 0;
        }
        return KEY_ENTER;
#endif
      }
      return KEY_ENTER;
    }

    // Discovery screen: long press = rescan
    if (ui_task.isOnDiscoveryScreen()) {
      return 'f';
    }

    // Repeater admin: long press → open keyboard for password or CLI
    if (ui_task.isOnRepeaterAdmin()) {
      RepeaterAdminScreen* admin = (RepeaterAdminScreen*)ui_task.getRepeaterAdminScreen();
      if (admin) {
        RepeaterAdminScreen::AdminState astate = admin->getState();
        if (astate == RepeaterAdminScreen::STATE_PASSWORD_ENTRY) {
#if defined(LilyGo_T5S3_EPaper_Pro)
          ui_task.showVirtualKeyboard(VKB_ADMIN_PASSWORD, "Admin Password", "", 32);
          return 0;
#else
          return KEY_ENTER;  // T-Deck Pro: keyboard handles password entry
#endif
        }
      }
    }

    // Notes screen: long press in editor → save and exit
    if (ui_task.isOnNotesScreen()) {
      NotesScreen* notes = (NotesScreen*)ui_task.getNotesScreen();
      if (notes && notes->isEditing()) {
        notes->triggerSaveAndExit();
        return 0;
      }
    }

    // Settings screen: context-dependent long press
    if (ui_task.isOnSettingsScreen()) {
      SettingsScreen* ss = (SettingsScreen*)ui_task.getSettingsScreen();
      if (ss) {
        if (ss->isEditing()) {
          return 0;  // Consume — don't interfere with active edit mode
        }
        if (ss->isOnDeletableChannel()) {
          return 'x';  // Long press on channel row → delete
        }
      }
      return KEY_ENTER;  // Not editing: toggle/edit selected row
    }

    // Default: enter/select
    return KEY_ENTER;
  }
#endif

void halt() {
  while (1) ;
}

void setup() {
  Serial.begin(115200);
  delay(100);  // Give serial time to initialize
  MESH_DEBUG_PRINTLN("=== setup() - STARTING ===");

  board.begin();
  MESH_DEBUG_PRINTLN("setup() - board.begin() done");

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  MESH_DEBUG_PRINTLN("setup() - about to call display.begin()");
  
  // =========================================================================
  // T-Deck Pro V1.1: Initialize E-Ink reset pin BEFORE display.begin()
  // This is critical - the display won't work without proper reset sequence
  // =========================================================================
  #if defined(LilyGo_TDeck_Pro)
    // Initialize E-Ink reset pin (GPIO 16)
    pinMode(PIN_DISPLAY_RST, OUTPUT);
    digitalWrite(PIN_DISPLAY_RST, HIGH);
    MESH_DEBUG_PRINTLN("setup() - E-Ink reset pin initialized");
    
    // Initialize Touch reset pin (GPIO 38) 
    #ifdef CST328_PIN_RST
      pinMode(CST328_PIN_RST, OUTPUT);
      digitalWrite(CST328_PIN_RST, HIGH);
      delay(20);
      digitalWrite(CST328_PIN_RST, LOW);
      delay(80);
      digitalWrite(CST328_PIN_RST, HIGH);
      delay(20);
      MESH_DEBUG_PRINTLN("setup() - Touch reset pin initialized");
    #endif
  #endif
  // =========================================================================
  
  if (display.begin()) {
    MESH_DEBUG_PRINTLN("setup() - display.begin() returned true");
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
    MESH_DEBUG_PRINTLN("setup() - Loading screen drawn");
  } else {
    MESH_DEBUG_PRINTLN("setup() - display.begin() returned false!");
  }
#endif

  MESH_DEBUG_PRINTLN("setup() - about to call radio_init()");
  if (!radio_init()) { 
    MESH_DEBUG_PRINTLN("setup() - radio_init() FAILED! Halting.");
    halt(); 
  }
  MESH_DEBUG_PRINTLN("setup() - radio_init() done");

  // CPU frequency scaling â€” drop to 80 MHz for idle mesh listening
  cpuPower.begin();

  MESH_DEBUG_PRINTLN("setup() - about to call fast_rng.begin()");
  fast_rng.begin(radio_get_rng_seed());
  MESH_DEBUG_PRINTLN("setup() - fast_rng.begin() done");

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  MESH_DEBUG_PRINTLN("setup() - NRF52/STM32 filesystem init");
  InternalFS.begin();
  #if defined(QSPIFLASH)
    if (!QSPIFlash.begin()) {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: failed to initialize");
    } else {
      MESH_DEBUG_PRINTLN("CustomLFS_QSPIFlash: initialized successfully");
    }
  #else
  #if defined(EXTRAFS)
      ExtraFS.begin();
  #endif
  #endif
  MESH_DEBUG_PRINTLN("setup() - about to call store.begin()");
  store.begin();
  MESH_DEBUG_PRINTLN("setup() - store.begin() done");
  
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.begin()");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  MESH_DEBUG_PRINTLN("setup() - the_mesh.begin() done");

#ifdef BLE_PIN_CODE
  MESH_DEBUG_PRINTLN("setup() - about to call serial_interface.begin() with BLE");
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  MESH_DEBUG_PRINTLN("setup() - serial_interface.begin() done");
#else
  serial_interface.begin(Serial);
#endif
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.startInterface()");
  the_mesh.startInterface(serial_interface);
  MESH_DEBUG_PRINTLN("setup() - the_mesh.startInterface() done");

#elif defined(RP2040_PLATFORM)
  MESH_DEBUG_PRINTLN("setup() - RP2040 filesystem init");
  LittleFS.begin();
  MESH_DEBUG_PRINTLN("setup() - about to call store.begin()");
  store.begin();
  MESH_DEBUG_PRINTLN("setup() - store.begin() done");
  
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.begin()");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  MESH_DEBUG_PRINTLN("setup() - the_mesh.begin() done");

  //#ifdef WIFI_SSID
  //  WiFi.begin(WIFI_SSID, WIFI_PWD);
  //  serial_interface.begin(TCP_PORT);
  // #elif defined(BLE_PIN_CODE)
  //   char dev_name[32+16];
  //   sprintf(dev_name, "%s%s", BLE_NAME_PREFIX, the_mesh.getNodeName());
  //   serial_interface.begin(dev_name, the_mesh.getBLEPin());
  #if defined(SERIAL_RX)
    companion_serial.setPins(SERIAL_RX, SERIAL_TX);
    companion_serial.begin(115200);
    serial_interface.begin(companion_serial);
  #else
    serial_interface.begin(Serial);
  #endif
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.startInterface()");
  the_mesh.startInterface(serial_interface);
  MESH_DEBUG_PRINTLN("setup() - the_mesh.startInterface() done");

#elif defined(ESP32)
  MESH_DEBUG_PRINTLN("setup() - ESP32 filesystem init - calling SPIFFS.begin()");
  SPIFFS.begin(true);
  MESH_DEBUG_PRINTLN("setup() - SPIFFS.begin() done");

  // ---------------------------------------------------------------------------
  // Early SD card init ÃƒÂ¢Ã¢â€šÂ¬Ã¢â‚¬Â needed BEFORE the_mesh.begin() so we can restore
  // settings from a previous firmware flash.  The display SPI bus is already
  // up (display.begin() ran earlier), so SD can share it now.
  // ---------------------------------------------------------------------------
  #if defined(LilyGo_TDeck_Pro) && defined(HAS_SDCARD)
  {
    // Deselect ALL SPI devices before SD init to prevent bus contention.
    // E-ink, LoRa, and SD share the same SPI bus (SCK=36, MOSI=33, MISO=47).
    // If LoRa CS is still asserted from board/radio init, it responds on the
    // shared MISO line and corrupts SD card replies (CMD0 fails intermittently).
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);

    pinMode(PIN_EINK_CS, OUTPUT);
    digitalWrite(PIN_EINK_CS, HIGH);

    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);

    // SD cards need 74+ SPI clock cycles after power stabilization before
    // accepting CMD0.  A brief delay avoids race conditions on cold boot
    // or with slow-starting cards.
    delay(100);

    // Retry loop — some SD cards are slow to initialise, especially on
    // cold boot or marginal USB power.  Three attempts with increasing
    // settle time covers the vast majority of transient failures.
    bool mounted = false;
    for (int attempt = 0; attempt < 3 && !mounted; attempt++) {
      if (attempt > 0) {
        digitalWrite(SDCARD_CS, HIGH);   // Ensure CS released between retries
        delay(250);
        MESH_DEBUG_PRINTLN("setup() - SD card retry %d/3", attempt + 1);
      }
      mounted = SD.begin(SDCARD_CS, displaySpi, 4000000);
    }

    if (mounted) {
      sdCardReady = true;
      MESH_DEBUG_PRINTLN("setup() - SD card initialized (early)");

      // If SPIFFS was wiped (fresh flash), restore settings from SD backup
      if (restoreSettingsFromSD()) {
        MESH_DEBUG_PRINTLN("setup() - Settings restored from SD backup");
      }
    } else {
      MESH_DEBUG_PRINTLN("setup() - SD card not available after 3 attempts");
    }
  }
  #elif defined(LilyGo_T5S3_EPaper_Pro) && defined(HAS_SDCARD)
  {
    // T5S3: SD card shares LoRa SPI bus (SCK=14, MOSI=13, MISO=21)
    // LoRa SPI already initialized by target.cpp. Create a local HSPI
    // reference for SD init (same hardware peripheral, different CS).
    static SPIClass sdSpi(HSPI);
    sdSpi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, SDCARD_CS);

    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);
    pinMode(P_LORA_NSS, OUTPUT);
    digitalWrite(P_LORA_NSS, HIGH);
    delay(100);

    bool mounted = false;
    for (int attempt = 0; attempt < 3 && !mounted; attempt++) {
      if (attempt > 0) {
        digitalWrite(SDCARD_CS, HIGH);
        delay(250);
        Serial.printf("setup() - SD card retry %d/3\n", attempt + 1);
      }
      mounted = SD.begin(SDCARD_CS, sdSpi, 4000000);
    }

    if (mounted) {
      sdCardReady = true;
      Serial.println("setup() - SD card initialized");

      // If SPIFFS was wiped (fresh flash), restore settings from SD backup
      if (restoreSettingsFromSD()) {
        Serial.println("setup() - T5S3: Settings restored from SD backup");
      }
    } else {
      Serial.println("setup() - SD card not available");
    }
  }
  #endif

  MESH_DEBUG_PRINTLN("setup() - about to call store.begin()");
  store.begin();
  MESH_DEBUG_PRINTLN("setup() - store.begin() done");
  
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.begin()");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  MESH_DEBUG_PRINTLN("setup() - the_mesh.begin() done");

#ifdef WIFI_SSID
  MESH_DEBUG_PRINTLN("setup() - WiFi mode (compile-time credentials)");
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(MECK_WIFI_COMPANION)
  {
    // WiFi companion: load credentials from SD at runtime.
    // TCP server starts regardless — companion connects when WiFi comes up.
    MESH_DEBUG_PRINTLN("setup() - WiFi companion mode (runtime credentials)");
    WiFi.mode(WIFI_STA);
    if (sdCardReady) {
      File f = SD.open("/web/wifi.cfg", FILE_READ);
      if (f) {
        String ssid = f.readStringUntil('\n'); ssid.trim();
        String pass = f.readStringUntil('\n'); pass.trim();
        f.close();
        digitalWrite(SDCARD_CS, HIGH);
        if (ssid.length() > 0) {
          MESH_DEBUG_PRINTLN("setup() - WiFi: connecting to '%s'", ssid.c_str());
          WiFi.begin(ssid.c_str(), pass.c_str());
          unsigned long timeout = millis() + 3000;  // 3s max — non-critical, Settings can retry
          while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
            yield();  // Feed WDT during wait
            delay(50);
          }
          if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("WiFi companion: connected to %s, IP: %s\n",
                          ssid.c_str(), WiFi.localIP().toString().c_str());
          } else {
            Serial.println("WiFi companion: auto-connect failed (configure in Settings)");
          }
        }
      } else {
        digitalWrite(SDCARD_CS, HIGH);
        Serial.println("WiFi companion: no /web/wifi.cfg found (configure in Settings)");
      }
    }
    serial_interface.begin(TCP_PORT);
    MESH_DEBUG_PRINTLN("setup() - WiFi TCP server started on port %d", TCP_PORT);
  }
#elif defined(BLE_PIN_CODE)
  MESH_DEBUG_PRINTLN("setup() - about to call serial_interface.begin() with BLE");
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  MESH_DEBUG_PRINTLN("setup() - serial_interface.begin() done");
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  MESH_DEBUG_PRINTLN("setup() - about to call the_mesh.startInterface()");
  the_mesh.startInterface(serial_interface);
  MESH_DEBUG_PRINTLN("setup() - the_mesh.startInterface() done");

#else
  #error "need to define filesystem"
#endif

  MESH_DEBUG_PRINTLN("setup() - about to call sensors.begin()");
  sensors.begin();
  MESH_DEBUG_PRINTLN("setup() - sensors.begin() done");

  // IMPORTANT: sensors.begin() calls initBasicGPS() which steals the GPS pins for Serial1.
  // We must end Serial1 first, then reclaim the pins for Serial2 (which feeds gpsStream).
  #if HAS_GPS
    Serial1.end();   // Release GPS pins from Serial1's UART + ISR
    Serial2.end();   // Close any existing Serial2
    {
      uint32_t gps_baud = the_mesh.getNodePrefs()->gps_baudrate;
      Serial.printf("GPS: prefs gps_baudrate=%lu (0=use default)\n", (unsigned long)gps_baud);
      if (gps_baud == 0) gps_baud = GPS_BAUDRATE;
      Serial2.begin(gps_baud, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
      Serial.printf("GPS: Serial2 started at %lu baud (RX=%d TX=%d)\n",
                     (unsigned long)gps_baud, GPS_RX_PIN, GPS_TX_PIN);
    }
  #endif

#ifdef DISPLAY_CLASS
  MESH_DEBUG_PRINTLN("setup() - about to call ui_task.begin()");
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
  MESH_DEBUG_PRINTLN("setup() - ui_task.begin() done");
#endif

  // ---------------------------------------------------------------------------
  // OTA boot validation — confirm new firmware is working after an OTA update.
  // If we reach this point, display + radio + SD + mesh all initialised OK.
  // Without this call (when CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is set),
  // the bootloader will roll back to the previous partition on next reboot.
  // ---------------------------------------------------------------------------
  #ifdef MECK_OTA_UPDATE
  {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
      if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
          Serial.println("OTA: New firmware validated, rollback cancelled");
        } else {
          Serial.println("OTA: WARNING - failed to cancel rollback");
        }
      }
    }
  }
  #endif

  // Initialize T-Deck Pro keyboard
  #if defined(LilyGo_TDeck_Pro)
    initKeyboard();
  #endif

  // Initialize touch input (CST328 on T-Deck Pro)
  #if defined(LilyGo_TDeck_Pro) && defined(HAS_TOUCHSCREEN)
    if (touchInput.begin(CST328_PIN_INT)) {
      MESH_DEBUG_PRINTLN("setup() - Touch input initialized");
    } else {
      MESH_DEBUG_PRINTLN("setup() - Touch input FAILED");
    }
  #endif

  // Initialize GT911 touch (T5S3 E-Paper Pro)
  // Wire is already initialized by T5S3Board::begin(). The 4-arg begin() re-calls
  // Wire.begin() which logs "bus already initialized" — cosmetic only, not harmful.
  #if defined(LilyGo_T5S3_EPaper_Pro)
    gt911Touch.setPins(GT911_PIN_RST, GT911_PIN_INT);
    if (gt911Touch.begin(Wire, GT911_SLAVE_ADDRESS_L, GT911_PIN_SDA, GT911_PIN_SCL)) {
      gt911Ready = true;
      Serial.println("setup() - GT911 touch initialized");
    } else {
      Serial.println("setup() - GT911 touch FAILED");
    }
  #endif

  // Initialize CardKB external keyboard (if connected via QWIIC)
  #if defined(LilyGo_T5S3_EPaper_Pro) && defined(MECK_CARDKB)
    if (cardkb.begin()) {
      ui_task.setCardKBDetected(true);
      Serial.println("setup() - CardKB detected at 0x5F");
    } else {
      Serial.println("setup() - CardKB not detected (will re-probe)");
    }
  #endif

  // RTC diagnostic + boot-time serial clock sync (T5S3 has no GPS)
  #if defined(LilyGo_T5S3_EPaper_Pro)
  {
    uint32_t rtcTime = rtc_clock.getCurrentTime();
    Serial.printf("setup() - RTC time: %lu (valid=%s)\n", rtcTime, 
                  rtcTime > 1700000000 ? "YES" : "NO");
    if (rtcTime < 1700000000) {
      // No valid time.  If a USB host has the serial port open (Serial
      // evaluates true on ESP32-S3 native CDC), request an automatic
      // clock sync.  The PlatformIO monitor filter "clock_sync" watches
      // for MECK_CLOCK_REQ and responds immediately with the host time.
      // Manual sync is also accepted: type "clock sync <epoch>" in any
      // serial terminal.
      if (Serial) {
        Serial.println("MECK_CLOCK_REQ");
        Serial.println("  (Waiting 3s for clock sync from host...)");

        char syncBuf[64];
        int syncPos = 0;
        unsigned long syncDeadline = millis() + 3000;
        bool synced = false;

        while (millis() < syncDeadline && !synced) {
          while (Serial.available() && syncPos < (int)sizeof(syncBuf) - 1) {
            char c = Serial.read();
            if (c == '\r' || c == '\n') {
              if (syncPos > 0) {
                syncBuf[syncPos] = '\0';
                if (memcmp(syncBuf, "clock sync ", 11) == 0) {
                  uint32_t epoch = (uint32_t)strtoul(&syncBuf[11], nullptr, 10);
                  if (epoch > 1704067200UL && epoch < 2082758400UL) {
                    rtc_clock.setCurrentTime(epoch);
                    Serial.printf("  > Clock synced to %lu\n", (unsigned long)epoch);
                    synced = true;
                  }
                }
                syncPos = 0;
              }
              break;
            }
            syncBuf[syncPos++] = c;
          }
          if (!synced) delay(10);
        }
        if (!synced) {
          Serial.println("  > No clock sync received, continuing boot");
          Serial.println("  > Use 'clock sync <epoch>' any time to sync later");
        }
      } else {
        Serial.println("setup() - RTC not set, no serial host detected (skipping sync window)");
      }
    }
  }
  #endif
  // Now set up SD-dependent features: message history + text reader.
  // ---------------------------------------------------------------------------
  #if defined(LilyGo_TDeck_Pro) && defined(HAS_SDCARD)
  if (sdCardReady) {
    // Load persisted channel messages from SD
    ChannelScreen* chanScr = (ChannelScreen*)ui_task.getChannelScreen();
    if (chanScr) {
      chanScr->setSDReady(true);
      if (chanScr->loadFromSD()) {
        MESH_DEBUG_PRINTLN("setup() - Message history loaded from SD");
      }
    }

    // Tell the text reader that SD is ready, then pre-index books at boot
    TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
    if (reader) {
      reader->setSDReady(true);
      if (disp) {
        cpuPower.setBoost();  // Boost CPU for EPUB processing
        reader->bootIndex(*disp);
      }
    }

    // Tell notes screen that SD is ready
    NotesScreen* notesScr = (NotesScreen*)ui_task.getNotesScreen();
    if (notesScr) {
      notesScr->setSDReady(true);
    }

    // Audiobook player screen creation is deferred to first use (case 'p' in
    // handleKeyboardInput) to avoid allocating Audio I2S/DMA buffers at boot,
    // which would starve BLE of heap memory.
    MESH_DEBUG_PRINTLN("setup() - Audiobook player deferred (lazy init on first use)");

    // Do an initial settings backup to SD (captures any first-boot defaults)
    backupSettingsToSD();

    // SMS / 4G modem init (after SD is ready)
    #ifdef HAS_4G_MODEM
    {
      smsStore.begin();
      smsContacts.begin();

      // Tell SMS screen that SD is ready
      SMSScreen* smsScr = (SMSScreen*)ui_task.getSMSScreen();
      if (smsScr) {
        smsScr->setSDReady(true);
      }

      // Start modem if enabled in config (default = enabled)
      bool modemEnabled = ModemManager::loadEnabledConfig();
      if (modemEnabled) {
        modemManager.begin();
        MESH_DEBUG_PRINTLN("setup() - 4G modem manager started");
      } else {
        // Ensure modem power is off (kills red LED too)
        pinMode(MODEM_POWER_EN, OUTPUT);
        digitalWrite(MODEM_POWER_EN, LOW);
        MESH_DEBUG_PRINTLN("setup() - 4G modem disabled by config");
      }
    }
    #endif
  }
  #endif

  // T5S3 SD-dependent features
  #if defined(LilyGo_T5S3_EPaper_Pro) && defined(HAS_SDCARD)
  if (sdCardReady) {
    // Channel message history
    ChannelScreen* chanScr = (ChannelScreen*)ui_task.getChannelScreen();
    if (chanScr) {
      chanScr->setSDReady(true);
      if (chanScr->loadFromSD()) {
        Serial.println("setup() - Message history loaded from SD");
      }
    }

    // Text reader — set SD ready and pre-index books
    TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
    if (reader) {
      reader->setSDReady(true);
      if (disp) {
        cpuPower.setBoost();
        reader->bootIndex(*disp);
      }
    }

    // Notes screen
    NotesScreen* notesScr = (NotesScreen*)ui_task.getNotesScreen();
    if (notesScr) {
      notesScr->setSDReady(true);
    }
    Serial.println("setup() - SD features initialized");
  }
  #endif
  // Check if node name is still the default hex prefix (first 4 bytes of pub key)
  // If so, launch onboarding wizard to set name and radio preset
  // ---------------------------------------------------------------------------
  #if defined(LilyGo_TDeck_Pro)
  {
    char defaultName[10];
    mesh::Utils::toHex(defaultName, the_mesh.self_id.pub_key, 4);
    NodePrefs* prefs = the_mesh.getNodePrefs();
    if (strcmp(prefs->node_name, defaultName) == 0) {
      MESH_DEBUG_PRINTLN("setup() - Default node name detected, launching onboarding");
      ui_task.gotoOnboarding();
      // Show hint immediately overlaid on the onboarding screen
      if (!prefs->hint_shown) ui_task.showBootHint(true);
    }
  }
  #endif

  // GPS power — honour saved pref, default to enabled on first boot.
  // GPS is critical for timesync on standalone variants without 4G.
  #if HAS_GPS
  {
    bool gps_wanted = the_mesh.getNodePrefs()->gps_enabled;
    Serial.printf("GPS: pref gps_enabled=%d\n", (int)gps_wanted);
    if (gps_wanted) {
      #ifdef PIN_GPS_EN
        digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);
      #endif
      sensors.setSettingValue("gps", "1");
    } else {
      #ifdef PIN_GPS_EN
        digitalWrite(PIN_GPS_EN, !GPS_EN_ACTIVE);
      #endif
      sensors.setSettingValue("gps", "0");
    }
    Serial.printf("GPS: power %s, PIN_GPS_EN=%d\n", gps_wanted ? "ON" : "OFF", PIN_GPS_EN);
  }
  #endif

  // BLE starts disabled for standalone-first operation
  // User can toggle it from the Bluetooth home page (Enter or long-press)
  #if (defined(LilyGo_TDeck_Pro) || defined(LilyGo_T5S3_EPaper_Pro)) && defined(BLE_PIN_CODE)
    serial_interface.disable();
    MESH_DEBUG_PRINTLN("setup() - BLE disabled at boot (standalone mode)");
  #endif

  // Alarm clock: create at boot so config is loaded, background alarm check
  // works from first loop(), and the bell indicator is visible immediately.
  // Audio object is NOT created here — lazy-init when alarm fires or user opens player.
  #ifdef MECK_AUDIO_VARIANT
  {
    AlarmScreen* alarmScr = new AlarmScreen(&ui_task);
    alarmScr->setSDReady(sdCardReady);
    // Audio pointer set later when needed (fireAlarm or 'k'/'p' key)
    ui_task.setAlarmScreen(alarmScr);
    Serial.printf("ALARM: Boot init, %d alarms enabled\n", alarmScr->enabledCount());
  }
  #endif

  // Register voice-over-LoRa callbacks early so incoming VE3 envelopes and
  // raw voice packets are handled even before user opens the voice screen.
  // The callbacks null-check the voice screen pointer, so they're safe at boot.
  #ifdef MECK_AUDIO_VARIANT
  the_mesh.setVoiceHandler(voiceRawCallback);
  the_mesh.setVoiceEnvelopeHandler(voiceEnvelopeCallback);
  #endif

  Serial.printf("setup() complete â€” free heap: %d, largest block: %d\n",
                 ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  MESH_DEBUG_PRINTLN("=== setup() - COMPLETE ===");
}

// ---------------------------------------------------------------------------
// OTA radio control — pause LoRa during firmware updates to prevent SPI
// bus contention (SD and LoRa share the same SPI bus on both platforms).
// Also pauses the mesh loop to prevent radio state confusion while standby.
// ---------------------------------------------------------------------------
#ifdef MECK_OTA_UPDATE
extern RADIO_CLASS radio;  // Defined in target.cpp

static bool otaRadioPaused = false;

void otaPauseRadio() {
  otaRadioPaused = true;
  radio.standby();
  Serial.println("OTA: Radio standby, mesh loop paused");
}

void otaResumeRadio() {
  radio.startReceive();
  otaRadioPaused = false;
  Serial.println("OTA: Radio receive resumed, mesh loop active");
}
#endif

void loop() {
  #ifdef MECK_OTA_UPDATE
  if (!otaRadioPaused) {
  #endif
  the_mesh.loop();
  #ifdef MECK_OTA_UPDATE
  } else {
    // OTA/File Manager active — poll the web server from the main loop for fast response.
    // The render cycle on T5S3 (960×540 FastEPD) can block for 500ms+ during
    // e-ink refresh, causing the browser to timeout before handleClient() runs.
    // Polling here gives us ~1-5ms response time instead.
    if (ui_task.isOnSettingsScreen()) {
      SettingsScreen* ss = (SettingsScreen*)ui_task.getSettingsScreen();
      if (ss) {
        ss->pollOTAServer();
        // Detect upload completion and trigger verify → flash → reboot.
        // Must happen here (not in render) because T5S3 e-ink refresh blocks
        // for 500ms+ and the render-based check never fires reliably.
        ss->checkOTAComplete(display);
      }
    }
  }
  #endif


  sensors.loop();

  // GPS diagnostic — disabled to reduce serial noise (uncomment for debugging)
  // #if HAS_GPS
  // {
  //   static unsigned long lastGpsDiag = 0;
  //   if (millis() - lastGpsDiag >= 30000) {
  //     lastGpsDiag = millis();
  //     uint32_t sentences = gpsStream.getSentenceCount();
  //     uint16_t perSec = gpsStream.getSentencesPerSec();
  //     Serial.printf("GPS diag: %lu sentences total, %u/sec, Serial2.available=%d, lat=%.6f lon=%.6f\n",
  //                   (unsigned long)sentences, perSec, Serial2.available(),
  //                   sensors.node_lat, sensors.node_lon);
  //   }
  // }
  // #endif

  // Map screen: periodically update own GPS position and contact markers
  #if HAS_GPS
  if (ui_task.isOnMapScreen()) {
    static unsigned long lastMapUpdate = 0;
    if (millis() - lastMapUpdate > 30000) {  // Every 30 seconds
      lastMapUpdate = millis();
      MapScreen* ms = (MapScreen*)ui_task.getMapScreen();
      if (ms) {
        // Update own GPS position when GPS is enabled
        ms->updateGPSPosition(sensors.node_lat, sensors.node_lon);

        // Always refresh contact markers (new contacts arrive via radio)
        ms->clearMarkers();
        ContactsIterator it = the_mesh.startContactsIterator();
        ContactInfo ci;
        while (it.hasNext(&the_mesh, ci)) {
          if (ci.gps_lat != 0 || ci.gps_lon != 0) {
            double lat = ((double)ci.gps_lat) / 1000000.0;
            double lon = ((double)ci.gps_lon) / 1000000.0;
            ms->addMarker(lat, lon, ci.name, ci.type);
          }
        }
      }
    }
  }
  #endif

  // CPU frequency auto-timeout back to idle
  cpuPower.loop();

  // Low-power mode — drop CPU to 40 MHz and throttle loop when lock screen
  // is active.  The mesh radio has its own FIFO so packets are buffered;
  // 50 ms yield means the loop still runs 20×/sec which is more than enough
  // to drain the radio FIFO before overflow.
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
  {
    static bool wasLocked = false;
    bool nowLocked = ui_task.isLocked();
    if (nowLocked && !wasLocked) {
      cpuPower.setLowPower();
      Serial.printf("[Power] Low-power mode: CPU %d MHz, loop throttled\n",
                    cpuPower.getFrequencyMHz());
    } else if (!nowLocked && wasLocked) {
      cpuPower.clearLowPower();
      Serial.printf("[Power] Normal mode: CPU %d MHz\n",
                    cpuPower.getFrequencyMHz());
    }
    wasLocked = nowLocked;
  }
#endif

  // Audiobook: service audio decode regardless of which screen is active
  #if defined(LilyGo_TDeck_Pro) && !defined(HAS_4G_MODEM)
  {
    AudiobookPlayerScreen* abPlayer =
      (AudiobookPlayerScreen*)ui_task.getAudiobookScreen();
    if (abPlayer) {
      abPlayer->audioTick();
      // Keep CPU at high freq during active audio decode
      if (abPlayer->isAudioActive()) {
        cpuPower.setBoost();
      }
    }
  }
  #endif

  // Alarm clock: background alarm check + audio tick
  #if defined(LilyGo_TDeck_Pro) && defined(MECK_AUDIO_VARIANT)
  {
    AlarmScreen* alarmScr = (AlarmScreen*)ui_task.getAlarmScreen();
    if (alarmScr) {
      // Service alarm audio decode (like audiobook audioTick)
      alarmScr->alarmAudioTick();
      if (alarmScr->isAlarmAudioActive()) {
        cpuPower.setBoost();
      }

      // Periodic alarm check (~every 10 seconds)
      static unsigned long lastAlarmCheck = 0;
      if (millis() - lastAlarmCheck > ALARM_CHECK_INTERVAL_MS) {
        lastAlarmCheck = millis();
        uint32_t rtcNow = the_mesh.getRTCClock()->getCurrentTime();
        int fireSlot = alarmScr->checkAlarms(rtcNow, the_mesh.getNodePrefs()->utc_offset_hours);
        if (fireSlot >= 0 && !alarmScr->isRinging()) {
          // If audiobook is playing, the alarm will take over the shared Audio*
          // object. The audiobook auto-saves bookmarks every 30s, so at most
          // 30s of position is lost. User can resume from audiobook player after.
          AudiobookPlayerScreen* abPlayer =
            (AudiobookPlayerScreen*)ui_task.getAudiobookScreen();
          if (abPlayer && abPlayer->isAudioActive()) {
            Serial.println("ALARM: Audiobook active — alarm taking over Audio");
          }

          // Ensure Audio object is shared
          if (!audio) audio = new Audio();
          alarmScr->setAudio(audio);

          // Fire the alarm
          alarmScr->fireAlarm(fireSlot);
          alarmScr->setLastFiredEpoch(fireSlot, rtcNow);

          // Let audio buffer fill before e-ink refresh blocks SPI
          for (int i = 0; i < 50; i++) {
            alarmScr->alarmAudioTick();
            delay(2);
          }

          // Switch UI to alarm screen (ringing mode)
          ui_task.gotoAlarmScreen();

          // Wake display if asleep
          ui_task.keepAlive();
          ui_task.forceRefresh();

          Serial.printf("ALARM: Fired slot %d, switched to ringing screen\n", fireSlot);
        }
      }
    }
  }
  #endif

  // Voice message: service mic DMA capture + playback audio decode
  #if defined(LilyGo_TDeck_Pro) && defined(MECK_AUDIO_VARIANT)
  {
    VoiceMessageScreen* voiceScr = (VoiceMessageScreen*)ui_task.getVoiceScreen();
    if (voiceScr) {
      voiceScr->voiceTick();

      // Sync shared audio pointer — playFile() may have recreated Audio*
      Audio* voiceAudio = voiceScr->getAudio();
      if (voiceAudio != audio) {
        Serial.println("Voice: Syncing shared Audio* after recreation");
        audio = voiceAudio;
        AlarmScreen* alarmScr = (AlarmScreen*)ui_task.getAlarmScreen();
        if (alarmScr) alarmScr->setAudio(audio);
      }

      // Service audio decode for voice playback (shared Audio* object)
      if (voiceScr->isAudioActive()) {
        if (audio) audio->loop();
        cpuPower.setBoost();
      }

      // Detect end-of-playback and refresh UI
      voiceScr->checkPlaybackFinished();
      if (voiceScr->consumePlaybackFinished()) {
        ui_task.forceRefresh();
      }

      // --- Contact picker: load contacts when mode transitions to CONTACT_PICK ---
      static bool pickContactsLoaded = false;
      if (voiceScr->getMode() == VoiceMessageScreen::CONTACT_PICK) {
        if (!pickContactsLoaded) {
          // Build list of chat contacts with direct paths
          VoiceMessageScreen::PickContact pickBuf[40];
          int pickCount = 0;
          ContactInfo ci;
          for (int idx = 0; idx < the_mesh.getNumContacts() && pickCount < 40; idx++) {
            if (!the_mesh.getContactByIdx(idx, ci)) continue;
            if (ci.type != ADV_TYPE_CHAT) continue;  // Only chat nodes
            if (ci.name[0] == '\0') continue;
            pickBuf[pickCount].meshIdx = idx;
            strncpy(pickBuf[pickCount].name, ci.name, 31);
            pickBuf[pickCount].name[31] = '\0';
            pickBuf[pickCount].type = ci.type;
            pickBuf[pickCount].hasDirect = (ci.out_path_len != OUT_PATH_UNKNOWN);
            pickCount++;
          }
          // Sort: direct-path contacts first, then alphabetical within each group
          std::sort(pickBuf, pickBuf + pickCount,
            [](const VoiceMessageScreen::PickContact& a, const VoiceMessageScreen::PickContact& b) {
              if (a.hasDirect != b.hasDirect) return a.hasDirect > b.hasDirect;
              return strcasecmp(a.name, b.name) < 0;
            });
          voiceScr->loadPickContacts(pickBuf, pickCount);
          pickContactsLoaded = true;
          ui_task.forceRefresh();
        }
      } else {
        pickContactsLoaded = false;
      }

      // --- Detect confirmed send from contact picker ---
      // Queue all packets at once using sendDirect's built-in delay parameter.
      // This lets the Mesh Dispatcher handle timing internally, spacing
      // transmissions so the SPI bus (shared with SD card) isn't contended.
      int sendIdx = voiceScr->consumePendingSend();
      if (sendIdx >= 0 && voiceScr->hasCodec2Data()) {
        cpuPower.setBoost();
        uint32_t sessionId = (uint32_t)(millis() & 0xFFFFFFFF);

        char envelope[64];
        voiceScr->formatEnvelope(envelope, sizeof(envelope), sessionId);

        ui_task.showAlert("Sending voice...", 10000);
        bool dmOk = the_mesh.uiSendDirectMessage(sendIdx, envelope);
        Serial.printf("Voice: VE3 DM '%s' to idx %d: %s\n",
                      envelope, sendIdx, dmOk ? "OK" : "FAIL");

        if (dmOk) {
          // Look up recipient for direct sendDirect calls
          ContactInfo ci;
          the_mesh.getContactByIdx(sendIdx, ci);
          ContactInfo* recipient = the_mesh.lookupContactByPubKey(ci.id.pub_key, PUB_KEY_SIZE);

          int totalPkts = voiceScr->getOutSessionPacketCount();
          int sentPkts = 0;

          if (recipient && recipient->out_path_len != OUT_PATH_UNKNOWN) {
            for (int p = 0; p < totalPkts; p++) {
              uint8_t pktBuf[184];
              int pktLen = voiceScr->buildVoicePacket(pktBuf, sizeof(pktBuf), sessionId, p);
              if (pktLen > 0) {
                mesh::Packet* raw = the_mesh.createRawData(pktBuf, pktLen);
                if (raw) {
                  // Stagger packets: first at 3s (after VE3 + ACK + contact save),
                  // each subsequent 3s apart. The Dispatcher queues them all now
                  // and transmits at the specified delay offsets.
                  uint32_t delayMs = 3000 + (uint32_t)p * 3000;
                  the_mesh.sendDirect(raw, recipient->out_path, recipient->out_path_len, delayMs);
                  sentPkts++;
                  Serial.printf("Voice: Queued packet %d/%d (delay %dms)\n",
                                p + 1, totalPkts, delayMs);
                }
              }
            }
          }
          Serial.printf("Voice: Queued %d/%d voice packets to %s\n",
                        sentPkts, totalPkts, recipient ? recipient->name : "?");
          voiceScr->onSendComplete(sentPkts == totalPkts);
          ui_task.showAlert(sentPkts == totalPkts ? "Voice sent!" : "Send partial", 2000);
        } else {
          voiceScr->onSendComplete(false);
          ui_task.showAlert("Send failed!", 1500);
        }
        ui_task.forceRefresh();
      }

      // --- Auto-play incoming voice session when all packets received ---
      if (voiceScr->isIncomingReady()) {
        the_mesh.setDeferSaves(false);  // Resume contact saves
        Serial.println("Voice: Incoming session complete — auto-playing");
        cpuPower.setBoost();
        if (voiceScr->playIncoming()) {
          ui_task.showAlert("Voice msg received!", 2000);
          ui_task.gotoVoiceScreen();
        } else {
          ui_task.showAlert("Voice decode failed", 1500);
        }
        ui_task.forceRefresh();
      }

      // Safety timeout: if saves are deferred for more than 15s, resume them
      // (in case voice packets never arrive or session is abandoned)
      static unsigned long deferStarted = 0;
      if (the_mesh.isDeferSaves()) {
        if (deferStarted == 0) deferStarted = millis();
        if (millis() - deferStarted > 15000) {
          the_mesh.setDeferSaves(false);
          deferStarted = 0;
          Serial.println("Voice: Save defer timeout — resuming saves");
        }
      } else {
        deferStarted = 0;
      }

      // During recording: keep CPU fast for DMA reads
      if (voiceScr->isRecording()) {
        cpuPower.setBoost();
      }
    }
  }
  #endif

  // SMS: poll for incoming messages from modem
  #ifdef HAS_4G_MODEM
  {
    SMSIncoming incoming;
    while (modemManager.recvSMS(incoming)) {
      // Save to store and notify UI
      SMSScreen* smsScr = (SMSScreen*)ui_task.getSMSScreen();
      if (smsScr) {
        smsScr->onIncomingSMS(incoming.phone, incoming.body, incoming.timestamp);
      }

      // Alert + buzzer
      char alertBuf[48];
      snprintf(alertBuf, sizeof(alertBuf), "SMS: %s", incoming.phone);
      ui_task.showAlert(alertBuf, 2000);
      ui_task.notify(UIEventType::contactMessage);

      Serial.printf("[SMS] Received from %s: %.40s...\n", incoming.phone, incoming.body);
    }

    // Poll for voice call events from modem
    CallEvent callEvt;
    while (modemManager.pollCallEvent(callEvt)) {
      SMSScreen* smsScr2 = (SMSScreen*)ui_task.getSMSScreen();
      if (smsScr2) {
        smsScr2->onCallEvent(callEvt);
      }

      if (callEvt.type == CallEventType::INCOMING) {
        // Incoming call — auto-switch to SMS screen if not already there
        char alertBuf[48];
        char dispName[SMS_CONTACT_NAME_LEN];
        smsContacts.displayName(callEvt.phone, dispName, sizeof(dispName));
        snprintf(alertBuf, sizeof(alertBuf), "Call: %s", dispName);
        ui_task.showAlert(alertBuf, 3000);
        ui_task.notify(UIEventType::contactMessage);

        if (!smsMode) {
          ui_task.gotoSMSScreen();
        }
        ui_task.forceRefresh();
        Serial.printf("[Call] Incoming from %s\n", callEvt.phone);
      } else if (callEvt.type == CallEventType::CONNECTED) {
        Serial.printf("[Call] Connected to %s\n", callEvt.phone);
        ui_task.forceRefresh();
      } else if (callEvt.type == CallEventType::ENDED) {
        Serial.printf("[Call] Ended (%lus) with %s\n",
                      (unsigned long)callEvt.duration, callEvt.phone);
        // Show alert with duration (supplements the immediate alert from Q hangup;
        // this catches remote hangups and network drops)
        {
          char alertBuf[48];
          if (callEvt.duration > 0) {
            snprintf(alertBuf, sizeof(alertBuf), "Call Ended  %lu:%02lu",
                     (unsigned long)(callEvt.duration / 60),
                     (unsigned long)(callEvt.duration % 60));
          } else {
            snprintf(alertBuf, sizeof(alertBuf), "Call Ended");
          }
          ui_task.showAlert(alertBuf, 2000);
        }
        ui_task.forceRefresh();
      } else if (callEvt.type == CallEventType::MISSED) {
        char alertBuf[48];
        char dispName[SMS_CONTACT_NAME_LEN];
        smsContacts.displayName(callEvt.phone, dispName, sizeof(dispName));
        snprintf(alertBuf, sizeof(alertBuf), "Missed: %s", dispName);
        ui_task.showAlert(alertBuf, 3000);
        Serial.printf("[Call] Missed from %s\n", callEvt.phone);
        ui_task.forceRefresh();
      } else if (callEvt.type == CallEventType::BUSY) {
        ui_task.showAlert("Line busy", 2000);
        Serial.printf("[Call] Busy: %s\n", callEvt.phone);
        ui_task.forceRefresh();
      } else if (callEvt.type == CallEventType::NO_ANSWER) {
        ui_task.showAlert("No answer", 2000);
        Serial.printf("[Call] No answer: %s\n", callEvt.phone);
        ui_task.forceRefresh();
      } else if (callEvt.type == CallEventType::DIAL_FAILED) {
        ui_task.showAlert("Call failed", 2000);
        Serial.printf("[Call] Dial failed: %s\n", callEvt.phone);
        ui_task.forceRefresh();
      }
    }
  }
  #endif
#ifdef DISPLAY_CLASS
  // Skip UITask rendering when in compose mode to prevent flickering
  #if defined(LilyGo_TDeck_Pro)
  // Also suppress during notes editing (same debounce pattern as compose)
  bool notesEditing = notesMode && ((NotesScreen*)ui_task.getNotesScreen())->isEditing();
  bool notesRenaming = notesMode && ((NotesScreen*)ui_task.getNotesScreen())->isRenaming();
  bool notesSuppressLoop = notesEditing || notesRenaming;
  #ifdef HAS_4G_MODEM
    bool smsSuppressLoop = smsMode && ((SMSScreen*)ui_task.getSMSScreen())->isComposing();
  #else
    bool smsSuppressLoop = false;
  #endif
  #ifdef MECK_WEB_READER
  // Safety: clear web reader text entry flag if we're no longer on the web reader
  if (webReaderTextEntry && !ui_task.isOnWebReader()) {
    webReaderTextEntry = false;
    webReaderNeedsRefresh = false;
  }
  #endif
  if (!composeMode && !notesSuppressLoop && !smsSuppressLoop && !dialerNeedsRefresh
  #ifdef MECK_WEB_READER
      && !webReaderTextEntry
  #endif
     ) {
    ui_task.loop();
  } else {
    // Handle debounced screen refresh (compose, emoji picker, notes, or web reader text entry)
    if (composeNeedsRefresh && (millis() - lastComposeRefresh) >= COMPOSE_REFRESH_INTERVAL) {
      if (composeMode) {
        if (emojiPickerMode) {
          drawEmojiPicker();
        } else {
          drawComposeScreen();
        }
      } else if (notesSuppressLoop) {
        // Notes editor/rename renders through UITask - force a refresh cycle
        ui_task.forceRefresh();
        ui_task.loop();
      } else if (smsSuppressLoop) {
        // SMS compose: render directly to display, same as mesh compose
        #if defined(DISPLAY_CLASS) && defined(HAS_4G_MODEM)
        display.startFrame();
        ((SMSScreen*)ui_task.getSMSScreen())->render(display);
        display.endFrame();
        #endif
      }
      lastComposeRefresh = millis();
      composeNeedsRefresh = false;
    }
    // Phone dialer debounced render (separate from compose debounce)
    #ifdef HAS_4G_MODEM
    if (dialerNeedsRefresh && (millis() - lastDialerRefresh) >= COMPOSE_REFRESH_INTERVAL) {
      if (smsMode) {
        SMSScreen* dialScr = (SMSScreen*)ui_task.getSMSScreen();
        if (dialScr && dialScr->getSubView() == SMSScreen::PHONE_DIALER) {
          display.startFrame();
          dialScr->render(display);
          display.endFrame();
        }
      }
      dialerNeedsRefresh = false;
      lastDialerRefresh = millis();
    }
    #endif
    #ifdef MECK_WEB_READER
    if (webReaderNeedsRefresh && (millis() - lastWebReaderRefresh) >= COMPOSE_REFRESH_INTERVAL) {
      WebReaderScreen* wr2 = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr2) {
        display.startFrame();
        wr2->render(display);
        display.endFrame();
      }
      lastWebReaderRefresh = millis();
      webReaderNeedsRefresh = false;
    }
    // Password reveal expiry: re-render to mask character after 800ms
    if (webReaderTextEntry && !webReaderNeedsRefresh) {
      WebReaderScreen* wr3 = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr3 && wr3->needsRevealRefresh() && (millis() - lastWebReaderRefresh) >= 850) {
        display.startFrame();
        wr3->render(display);
        display.endFrame();
        lastWebReaderRefresh = millis();
      }
    }
    #endif
  }
  // Track reader/notes/audiobook mode state for key routing
  readerMode = ui_task.isOnTextReader();
  notesMode = ui_task.isOnNotesScreen();
  audiobookMode = ui_task.isOnAudiobookPlayer();
  #ifdef MECK_AUDIO_VARIANT
    voiceMode = ui_task.isOnVoiceScreen();
  #endif
  #ifdef HAS_4G_MODEM
    smsMode = ui_task.isOnSMSScreen();
  #endif
  #else
  ui_task.loop();
  #endif
#endif
  rtc_clock.tick();
  // Periodic AGC reset - re-assert boosted RX gain to prevent sensitivity drift
  #ifdef MECK_OTA_UPDATE
  if (!otaRadioPaused)
  #endif
  if ((millis() - lastAGCReset) >= AGC_RESET_INTERVAL_MS) {
    radio_reset_agc();
    lastAGCReset = millis();
  }
  // Handle T-Deck Pro keyboard input
  #if defined(LilyGo_TDeck_Pro)
    handleKeyboardInput();
  #endif

  // ---------------------------------------------------------------------------
  // Touch Input — tap/swipe/long-press state machine (T5S3 + T-Deck Pro)
  // Gestures:
  //   Tap = finger down + up with minimal movement → select/open
  //   Swipe = finger drag > threshold → scroll/page turn
  //   Long press = finger held > 750ms without moving → edit/enter
  // After processing an event, cooldown waits for finger lift before next event.
  // Touch is disabled while lock screen is active.
  // When virtual keyboard is active (T5S3), taps route to keyboard.
  // ---------------------------------------------------------------------------
  #ifdef MECK_TOUCH_ENABLED
  {
    // Guard: skip touch when locked or VKB active
    bool touchBlocked = ui_task.isLocked();
#if defined(LilyGo_T5S3_EPaper_Pro)
    touchBlocked = touchBlocked || ui_task.isVKBActive();
#endif
#ifdef HAS_4G_MODEM
    // SMS dialer has its own dedicated touch handler — don't consume touch data here
    if (smsMode) {
      SMSScreen* smsScr = (SMSScreen*)ui_task.getSMSScreen();
      if (smsScr && smsScr->getSubView() == SMSScreen::PHONE_DIALER) {
        touchBlocked = true;
      }
    }
#endif

    if (!touchBlocked)
    {
      int16_t tx, ty;
      bool gotPoint = readTouch(&tx, &ty);
      unsigned long now = millis();

      if (gotPoint) {
        lastTouchSeenMs = now;
      }

      bool fingerPresent = (now - lastTouchSeenMs) < TOUCH_LIFT_DEBOUNCE_MS;

      if (touchCooldown) {
        if (!fingerPresent && (now - lastTouchEventMs) >= TOUCH_MIN_INTERVAL_MS) {
          touchCooldown = false;
          touchDown = false;
        }
      }
      else if (gotPoint && !touchDown) {
        touchDown = true;
        touchDownTime = now;
        touchDownX = tx;
        touchDownY = ty;
        touchLastX = tx;
        touchLastY = ty;
        longPressHandled = false;
        swipeHandled = false;
      }
      else if (touchDown && fingerPresent) {
        if (gotPoint) {
          touchLastX = tx;
          touchLastY = ty;
        }

        int16_t dx = touchLastX - touchDownX;
        int16_t dy = touchLastY - touchDownY;
        int16_t dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

        if (!swipeHandled && !longPressHandled && dist >= TOUCH_SWIPE_THRESHOLD) {
          swipeHandled = true;
          char c = mapTouchSwipe(dx, dy);
          if (c) {
            ui_task.injectKey(c);
            cpuPower.setBoost();
          }
          lastTouchEventMs = now;
          touchCooldown = true;
        }
        else if (!longPressHandled && !swipeHandled && dist < TOUCH_SWIPE_THRESHOLD &&
                 (now - touchDownTime) >= TOUCH_LONG_PRESS_MS) {
          longPressHandled = true;
          char c = mapTouchLongPress(touchDownX, touchDownY);
          if (c) {
            ui_task.injectKey(c);
            cpuPower.setBoost();
          }
          lastTouchEventMs = now;
          touchCooldown = true;
        }
      }
      else if (touchDown && !fingerPresent) {
        touchDown = false;
        if (!longPressHandled && !swipeHandled) {
          char c = mapTouchTap(touchDownX, touchDownY);
          if (c) {
            ui_task.injectKey(c);
          }
          cpuPower.setBoost();
          lastTouchEventMs = now;
          touchCooldown = true;
        }
      }
    }
  }

  // Virtual keyboard touch routing (T5S3 only — T-Deck Pro uses physical keyboard)
#if defined(LilyGo_T5S3_EPaper_Pro)
  {
    static bool vkbNeedLift = true;

    if (ui_task.isVKBActive()) {
      int16_t tx, ty;
      bool gotPt = readTouch(&tx, &ty);

      if (!gotPt) {
        vkbNeedLift = false;
      }

      bool cooldownOk = (millis() - ui_task.vkbOpenedAt()) > 2000;

      if (gotPt && !vkbNeedLift && cooldownOk) {
        int vx, vy;
        touchToVirtual(tx, ty, vx, vy);
        if (ui_task.getVKB().handleTap(vx, vy)) {
          ui_task.forceRefresh();
        }
        vkbNeedLift = true;
      }
    } else {
      vkbNeedLift = true;
    }
  }
#endif
  #endif // MECK_TOUCH_ENABLED

  // ---------------------------------------------------------------------------
  // CardKB external keyboard polling (T5S3 only, via QWIIC)
  // When VKB is active: typed characters feed into the VKB text buffer.
  // When VKB is not active: navigation keys route through injectKey().
  // ESC key maps to 'q' (back) when no VKB is active.
  // ---------------------------------------------------------------------------
#if defined(LilyGo_T5S3_EPaper_Pro) && defined(MECK_CARDKB)
  {
    // Hot-plug detection: re-probe periodically
    if (millis() - lastCardKBProbe >= CARDKB_PROBE_INTERVAL_MS) {
      lastCardKBProbe = millis();
      bool wasDetected = cardkb.isDetected();
      bool nowDetected = cardkb.probe();
      if (nowDetected != wasDetected) {
        ui_task.setCardKBDetected(nowDetected);
        Serial.printf("[CardKB] %s\n", nowDetected ? "Connected" : "Disconnected");
      }
    }

    // Poll for keypress
    char ckb = cardkb.readKey();
    if (ckb != 0) {
      // Block input while locked (same as touch)
      if (!ui_task.isLocked()) {
        cpuPower.setBoost();
        ui_task.keepAlive();

        if (ui_task.isVKBActive()) {
          // VKB is open — feed character into VKB text buffer
          ui_task.feedCardKBChar(ckb);
        } else if (ui_task.isOnHomeScreen()) {
          // Home screen: ESC does nothing special, letter shortcuts open tiles
          if (ckb == 0x1B) {
            // ESC on home — no-op (already home)
          } else {
            switch (ckb) {
              case 'm': ui_task.gotoChannelScreen(); break;
              case 'c': ui_task.gotoContactsScreen(); break;
              case 'e': ui_task.gotoTextReader(); break;
              case 'n': ui_task.gotoNotesScreen(); break;
              case 's': ui_task.gotoSettingsScreen(); break;
              case 'f': ui_task.gotoDiscoveryScreen(); break;
              case 'h': ui_task.gotoLastHeardScreen(); break;
#ifdef MECK_WEB_READER
              case 'b': ui_task.gotoWebReader(); break;
#endif
#if HAS_GPS
              case 'g': ui_task.gotoMapScreen(); break;
#endif
              default:  ui_task.injectKey(ckb); break;
            }
          }
        } else {
          // Non-home screens: context-specific routing
          bool handled = false;

          // Notes editing/renaming: route ALL keys directly (no VKB).
          // This gives: Enter=newline, arrows=cursor, printable=insert, ESC=save&exit
          if (ui_task.isOnNotesScreen()) {
            NotesScreen* notesScr = (NotesScreen*)ui_task.getNotesScreen();
            if (notesScr && (notesScr->isEditing() || notesScr->isRenaming())) {
              handled = true;
              if (ckb == 0x1B) {
                // ESC: save & exit editing, or cancel rename
                if (notesScr->isEditing()) {
                  notesScr->triggerSaveAndExit();
                } else {
                  ui_task.injectKey('q');
                }
              } else if (notesScr->isEditing()) {
                // Editing mode: arrows move cursor, everything else types directly
                switch (ckb) {
                  case (char)0xF2: notesScr->moveCursorUp();    break;
                  case (char)0xF1: notesScr->moveCursorDown();  break;
                  case (char)0xF3: notesScr->moveCursorLeft();  break;
                  case (char)0xF4: notesScr->moveCursorRight(); break;
                  default:         ui_task.injectKey(ckb);      break;
                }
              } else {
                // Renaming mode: all keys go directly to rename handler
                ui_task.injectKey(ckb);
              }
              ui_task.forceRefresh();
            }
          }

          if (!handled) {
            // ESC → back (same as 'q' on T-Deck Pro) for all non-notes screens
            if (ckb == 0x1B) {
              ui_task.injectKey('q');
            } else if (ckb == '\r') {
              // Enter key — screen-specific compose or select
              if (ui_task.isOnChannelScreen()) {
                uint8_t chIdx = ui_task.getChannelScreenViewIdx();
                if (chIdx == 0xFF) {
                  ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
                  if (chScr->isDMInboxMode()) {
                    // Inbox mode: inject Enter to open conversation
                    ui_task.injectKey('\r');
                  } else {
                    // Conversation mode: open VKB DM compose
                    const char* dmName = chScr->getDMFilterName();
                    if (dmName && dmName[0]) {
                      uint32_t numC = the_mesh.getNumContacts();
                      ContactInfo ci;
                      for (uint32_t j = 0; j < numC; j++) {
                        if (the_mesh.getContactByIdx(j, ci) && strcmp(ci.name, dmName) == 0) {
                          char label[40];
                          snprintf(label, sizeof(label), "DM: %s", dmName);
                          ui_task.showVirtualKeyboard(VKB_DM, label, "", 137, j);
                          ui_task.clearDMUnread(j);
                          break;
                        }
                      }
                    }
                  }
                } else {
                  // Open VKB for channel message compose
                  ChannelDetails ch;
                  if (the_mesh.getChannel(chIdx, ch)) {
                    char label[40];
                    snprintf(label, sizeof(label), "To: %s", ch.name);
                    ui_task.showVirtualKeyboard(VKB_CHANNEL_MSG, label, "", 137, chIdx);
                  }
                }
              } else if (ui_task.isOnContactsScreen()) {
                // DM compose for chat contacts, admin for repeaters
                ContactsScreen* cs = (ContactsScreen*)ui_task.getContactsScreen();
                if (cs) {
                  int idx = cs->getSelectedContactIdx();
                  uint8_t ctype = cs->getSelectedContactType();
                  if (idx >= 0 && ctype == ADV_TYPE_CHAT) {
                    if (ui_task.hasDMUnread(idx)) {
                      char cname[32];
                      cs->getSelectedContactName(cname, sizeof(cname));
                      ui_task.clearDMUnread(idx);
                      ui_task.gotoDMConversation(cname);
                    } else {
                      char dname[32];
                      cs->getSelectedContactName(dname, sizeof(dname));
                      char label[40];
                      snprintf(label, sizeof(label), "DM: %s", dname);
                      ui_task.showVirtualKeyboard(VKB_DM, label, "", 137, idx);
                    }
                  } else if (idx >= 0 && ctype == ADV_TYPE_REPEATER) {
                    ui_task.gotoRepeaterAdmin(idx);
                  } else if (idx >= 0 && ctype == ADV_TYPE_ROOM) {
                    // Room server: open login (auto-redirects to conversation)
                    ui_task.gotoRepeaterAdmin(idx);
                  } else if (idx >= 0 && ui_task.hasDMUnread(idx)) {
                    char cname[32];
                    cs->getSelectedContactName(cname, sizeof(cname));
                    ui_task.clearDMUnread(idx);
                    ui_task.gotoDMConversation(cname);
                  }
                }
              } else if (ui_task.isOnRepeaterAdmin()) {
                // Open VKB for password or CLI entry
                RepeaterAdminScreen* admin = (RepeaterAdminScreen*)ui_task.getRepeaterAdminScreen();
                if (admin) {
                  RepeaterAdminScreen::AdminState astate = admin->getState();
                  if (astate == RepeaterAdminScreen::STATE_PASSWORD_ENTRY) {
                    ui_task.showVirtualKeyboard(VKB_ADMIN_PASSWORD, "Admin Password", "", 32);
                  } else {
                    ui_task.showVirtualKeyboard(VKB_ADMIN_CLI, "Admin Command", "", 137);
                  }
                }
              } else {
                // All other screens: pass Enter through for native handling
                // (settings toggle, discovery add-contact, last heard, text reader, notes file list, etc.)
                ui_task.injectKey('\r');
              }
            } else {
              // Non-Enter keys: remap arrows to WASD, pass others through
              switch (ckb) {
                case (char)0xF2: ui_task.injectKey('w'); break;  // Up → scroll up
                case (char)0xF1: ui_task.injectKey('s'); break;  // Down → scroll down
                case (char)0xF3: ui_task.injectKey('a'); break;  // Left → prev channel/category
                case (char)0xF4: ui_task.injectKey('d'); break;  // Right → next channel/category
                default:         ui_task.injectKey(ckb); break;
              }
            }
          }
        }
      }
    }
  }
#endif

  // Poll touch input for phone dialer numpad
  // Hybrid debounce: finger-up detection + 150ms minimum between accepted taps.
  // The CST328 INT pin is pulse-based (not level), so getPoint() can return
  // false intermittently during a hold. Time guard prevents that from
  // causing repeat fires.
  #if defined(HAS_TOUCHSCREEN) && defined(HAS_4G_MODEM)
  {
    static bool touchFingerDown = false;
    static unsigned long lastTouchAccepted = 0;

    if (smsMode) {
      SMSScreen* smsScr = (SMSScreen*)ui_task.getSMSScreen();
      if (smsScr && smsScr->getSubView() == SMSScreen::PHONE_DIALER) {
        int16_t tx, ty;
        if (touchInput.getPoint(tx, ty)) {
          unsigned long now = millis();
          if (!touchFingerDown && (now - lastTouchAccepted >= 150)) {
            touchFingerDown = true;
            lastTouchAccepted = now;
            if (smsScr->handleTouch(tx, ty)) {
              dialerNeedsRefresh = true;
              lastDialerRefresh = millis();
            }
          }
        } else {
          // Only allow finger-up after 100ms from last acceptance
          // (prevents INT pulse misses from resetting state mid-hold)
          if (touchFingerDown && (millis() - lastTouchAccepted >= 100)) {
            touchFingerDown = false;
          }
        }
      } else {
        touchFingerDown = false;
      }
    }
  }
  #endif

  // Low-power loop throttle — yield CPU when lock screen is active.
  // The RTOS idle task executes WFI (wait-for-interrupt) during delay(),
  // dramatically reducing CPU power draw.  50 ms gives 20 loop cycles/sec
  // which is ample for LoRa packet reception (radio has hardware FIFO).
#if defined(LilyGo_T5S3_EPaper_Pro) || defined(LilyGo_TDeck_Pro)
  if (ui_task.isLocked()) {
    delay(50);
  }
#endif
}

// ============================================================================
// T-DECK PRO KEYBOARD FUNCTIONS
// ============================================================================

#if defined(LilyGo_TDeck_Pro)

void initKeyboard() {
  // Keyboard uses the same I2C bus as other peripherals (already initialized)
  if (keyboard.begin()) {
    MESH_DEBUG_PRINTLN("setup() - Keyboard initialized");
    composeBuffer[0] = '\0';
    composePos = 0;
    composeMode = false;
    composeNeedsRefresh = false;
    lastComposeRefresh = 0;
  } else {
    MESH_DEBUG_PRINTLN("setup() - Keyboard initialization failed!");
  }
}

void handleKeyboardInput() {
  if (!keyboard.isReady()) return;
  
  char key = keyboard.readKey();
  if (key == 0) return;

  // Block all keyboard input while lock screen is active.
  // Still read the key above to clear the TCA8418 buffer.
  if (ui_task.isLocked()) return;

  // Dismiss boot navigation hint on any keypress
  if (ui_task.isHintActive()) {
    ui_task.dismissBootHint();
    return;  // Consume the keypress (don't act on it)
  }
  
  Serial.printf("handleKeyboardInput: key='%c' (0x%02X) composeMode=%d\n", 
                key >= 32 ? key : '?', key, composeMode);
  
  // Alarm ringing: ANY key dismisses (highest priority after lock screen)
  #ifdef MECK_AUDIO_VARIANT
  {
    AlarmScreen* alarmScr = (AlarmScreen*)ui_task.getAlarmScreen();
    if (alarmScr && alarmScr->isRinging()) {
      if (key == 'z') {
        alarmScr->handleInput('z');  // Snooze
      } else {
        alarmScr->dismiss();         // Any other key = dismiss
      }
      ui_task.gotoHomeScreen();
      ui_task.forceRefresh();
      return;  // Consume the key
    }
  }
  #endif

  if (composeMode) {
    // Emoji picker sub-mode
    if (emojiPickerMode) {
      uint8_t result = emojiPicker.handleInput(key);
      if (result == 0xFF) {
        // Cancelled - immediate draw to return to compose
        emojiPickerMode = false;
        drawComposeScreen();
        lastComposeRefresh = millis();
        composeNeedsRefresh = false;
      } else if (result >= EMOJI_ESCAPE_START && result <= EMOJI_ESCAPE_END) {
        // Emoji selected - insert escape byte + padding to match UTF-8 wire cost
        int cost = emojiUtf8Cost(result);
        if (composePos + cost <= 137) {
          composeBuffer[composePos++] = (char)result;
          for (int p = 1; p < cost; p++) {
            composeBuffer[composePos++] = (char)EMOJI_PAD_BYTE;
          }
          composeBuffer[composePos] = '\0';
          Serial.printf("Compose: Inserted emoji 0x%02X cost=%d, pos=%d\n", result, cost, composePos);
        }
        emojiPickerMode = false;
        drawComposeScreen();
        lastComposeRefresh = millis();
        composeNeedsRefresh = false;
      } else {
        // Navigation - debounce (don't draw immediately, let loop handle it)
        composeNeedsRefresh = true;
      }
      return;
    }
    
    // In compose mode - handle text input
    
    // Ignore mic key press/release while composing text
    if (key == KB_KEY_MIC || key == KB_KEY_MIC_RELEASE) return;

    if (key == '\r') {
      // Enter - send the message
      Serial.println("Compose: Enter pressed, sending...");
      bool composeWasSent = false;
      if (composePos > 0) {
        sendComposedMessage();
        composeWasSent = true;  // sendComposedMessage shows its own alert
      }
      bool wasDM = composeDM;
      int savedDMIdx = composeDMContactIdx;
      char savedDMName[32];
      if (wasDM) strncpy(savedDMName, composeDMName, sizeof(savedDMName));
      composeMode = false;
      emojiPickerMode = false;
      composeDM = false;
      composeDMContactIdx = -1;
      composeBuffer[0] = '\0';
      composePos = 0;
      if (wasDM && savedDMIdx >= 0) {
        // Return to DM conversation to see sent message
        ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
        uint8_t savedPerms = (chScr && chScr->isDMConversation()) ? chScr->getDMContactPerms() : 0;
        ui_task.gotoDMConversation(savedDMName, savedDMIdx, savedPerms);
        // Re-show alert after navigation (setCurrScreen clears prior alerts)
        if (composeWasSent) ui_task.showAlert("DM sent!", 1500);
      } else if (wasDM) {
        ui_task.gotoContactsScreen();
      } else {
        ui_task.gotoChannelScreen();
      }
      return;
    }
    
    if (key == '\b') {
      // Backspace - check if shift was recently pressed for cancel combo
      if (keyboard.wasShiftRecentlyPressed(500)) {
        // Shift+Backspace = Cancel (works anytime)
        Serial.println("Compose: Shift+Backspace, cancelling...");
        bool wasDM = composeDM;
        int savedDMIdx = composeDMContactIdx;
        char savedDMName[32];
        if (wasDM) strncpy(savedDMName, composeDMName, sizeof(savedDMName));
        composeMode = false;
        emojiPickerMode = false;
        composeDM = false;
        composeDMContactIdx = -1;
        composeBuffer[0] = '\0';
        composePos = 0;
        if (wasDM && savedDMIdx >= 0) {
          ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
          uint8_t savedPerms = (chScr && chScr->isDMConversation()) ? chScr->getDMContactPerms() : 0;
          ui_task.gotoDMConversation(savedDMName, savedDMIdx, savedPerms);
        } else if (wasDM) {
          ui_task.gotoContactsScreen();
        } else {
          ui_task.gotoChannelScreen();
        }
        return;
      }
      // Regular backspace - delete last character (or entire emoji including pads)
      if (composePos > 0) {
        // Delete trailing pad bytes first, then the escape byte
        while (composePos > 0 && (uint8_t)composeBuffer[composePos - 1] == EMOJI_PAD_BYTE) {
          composePos--;
        }
        // Now delete the actual character (escape byte or regular char)
        if (composePos > 0) {
          composePos--;
        }
        composeBuffer[composePos] = '\0';
        Serial.printf("Compose: Backspace, pos=%d\n", composePos);
        composeNeedsRefresh = true;  // Use debounced refresh
      }
      return;
    }
    
    // A/D keys switch channels (only when buffer is empty, not in DM mode)
    if ((key == 'a') && composePos == 0 && !composeDM) {
      // Previous channel — skip gaps
      if (composeChannelIdx > 0) {
        bool found = false;
        for (uint8_t prev = composeChannelIdx - 1; ; prev--) {
          ChannelDetails ch;
          if (the_mesh.getChannel(prev, ch) && ch.name[0] != '\0') {
            composeChannelIdx = prev;
            found = true;
            break;
          }
          if (prev == 0) break;
        }
        if (!found) {
          // Wrap to last valid channel
          for (uint8_t i = MAX_GROUP_CHANNELS - 1; i > 0; i--) {
            ChannelDetails ch;
            if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
              composeChannelIdx = i;
              break;
            }
          }
        }
      } else {
        // Wrap to last valid channel
        for (uint8_t i = MAX_GROUP_CHANNELS - 1; i > 0; i--) {
          ChannelDetails ch;
          if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
            composeChannelIdx = i;
            break;
          }
        }
      }
      Serial.printf("Compose: Channel switched to %d\n", composeChannelIdx);
      composeNeedsRefresh = true;  // Debounced refresh
      return;
    }
    
    if ((key == 'd') && composePos == 0 && !composeDM) {
      // Next channel — skip gaps
      bool found = false;
      for (uint8_t next = composeChannelIdx + 1; next < MAX_GROUP_CHANNELS; next++) {
        ChannelDetails ch;
        if (the_mesh.getChannel(next, ch) && ch.name[0] != '\0') {
          composeChannelIdx = next;
          found = true;
          break;
        }
      }
      if (!found) {
        composeChannelIdx = 0;  // Wrap to first channel
      }
      Serial.printf("Compose: Channel switched to %d\n", composeChannelIdx);
      composeNeedsRefresh = true;  // Debounced refresh
      return;
    }
    
    // '$' key (without Sym) opens emoji picker
    if (key == KB_KEY_EMOJI) {
      emojiPicker.reset();
      emojiPickerMode = true;
      drawEmojiPicker();
      lastComposeRefresh = millis();
      composeNeedsRefresh = false;
      return;
    }
    
    // Regular character input
    if (key >= 32 && key < 127 && composePos < 137) {
      composeBuffer[composePos++] = key;
      composeBuffer[composePos] = '\0';
      Serial.printf("Compose: Added '%c', pos=%d\n", key, composePos);
      composeNeedsRefresh = true;  // Use debounced refresh
    }
    return;
  }
  
  // *** AUDIOBOOK MODE ***
  #ifndef HAS_4G_MODEM
  if (audiobookMode) {
    AudiobookPlayerScreen* abPlayer =
      (AudiobookPlayerScreen*)ui_task.getAudiobookScreen();

    // Q key: behavior depends on playback state
    //   - Playing: navigate home, audio continues in background
    //   - Paused/stopped: close book, return to file list
    //   - File list: exit player entirely
    if (key == 'q') {
      if (abPlayer->isBookOpen()) {
        if (abPlayer->isAudioActive()) {
          // Audio is playing â€” leave screen, audio continues via audioTick()
          Serial.println("Leaving audiobook player (audio continues in background)");
          ui_task.gotoHomeScreen();
        } else {
          // Paused or stopped â€” close book, show file list
          abPlayer->closeCurrentBook();
          Serial.println("Closed audiobook (was paused/stopped)");
          // Stay on audiobook screen showing file list
        }
      } else {
        abPlayer->exitPlayer();
        Serial.println("Exiting audiobook player");
        ui_task.gotoHomeScreen();
      }
      return;
    }

    // All other keys pass through to the player screen
    ui_task.injectKey(key);
    return;
  }
  #endif // !HAS_4G_MODEM

  // *** VOICE MESSAGE MODE ***
  #ifdef MECK_AUDIO_VARIANT
  if (voiceMode) {
    VoiceMessageScreen* voiceScr = (VoiceMessageScreen*)ui_task.getVoiceScreen();
    if (!voiceScr) { voiceMode = false; }
    else {
      // Mic key press starts recording (PTT)
      if (key == KB_KEY_MIC) {
        voiceScr->onMicPress();
        ui_task.forceRefresh();
        return;
      }
      // Mic key release stops recording
      if (key == KB_KEY_MIC_RELEASE) {
        voiceScr->onMicRelease();
        ui_task.forceRefresh();
        return;
      }
      // Q from message list exits voice screen
      if (key == 'q' && voiceScr->getMode() == VoiceMessageScreen::MESSAGE_LIST) {
        Serial.println("Exiting voice message screen");
        ui_task.gotoHomeScreen();
        return;
      }
      // All other keys pass through to voice screen
      voiceScr->handleInput(key);
      ui_task.forceRefresh();
      return;
    }
  }
  #endif // MECK_AUDIO_VARIANT

  // *** TEXT READER MODE ***
  if (readerMode) {
    TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
    
    // Q key: if reading, reader handles it (close book -> file list)
    //         if on file list, exit reader entirely
    if (key == 'q') {
      if (reader->isReading()) {
        // Let the reader handle Q (close book, go to file list)
        ui_task.injectKey('q');
      } else {
        // On file list - exit reader, go home
        reader->exitReader();
        Serial.println("Exiting text reader");
        ui_task.gotoHomeScreen();
      }
      return;
    }
    
    // All other keys pass through to the reader screen
    ui_task.injectKey(key);
    return;
  }

  // *** NOTES MODE ***
  if (notesMode) {
    NotesScreen* notes = (NotesScreen*)ui_task.getNotesScreen();

    // ---- EDITING MODE ----
    if (notes->isEditing()) {
      // Shift+Backspace = save and exit
      if (key == '\b') {
        if (keyboard.wasShiftConsumed()) {
          Serial.println("Notes: Shift+Backspace, saving...");
          notes->saveAndExit();
          ui_task.forceRefresh();
          return;
        }
        // Regular backspace - delete before cursor
        ui_task.injectKey(key);
        composeNeedsRefresh = true;
        return;
      }

      // Cursor navigation via Shift+WASD (produces uppercase)
      if (key == 'W') { notes->moveCursorUp();    composeNeedsRefresh = true; return; }
      if (key == 'A') { notes->moveCursorLeft();   composeNeedsRefresh = true; return; }
      if (key == 'S') { notes->moveCursorDown();   composeNeedsRefresh = true; return; }
      if (key == 'D') { notes->moveCursorRight();  composeNeedsRefresh = true; return; }

      // Q when buffer is empty or unchanged = exit (nothing to lose)
      if (key == 'q' && (notes->isEmpty() || !notes->isDirty())) {
        Serial.println("Notes: Q exit (nothing to save)");
        notes->discardAndExit();
        ui_task.forceRefresh();
        return;
      }

      // Enter = newline (pass through with debounce)
      if (key == '\r') {
        ui_task.injectKey(key);
        composeNeedsRefresh = true;
        return;
      }

      // All other printable chars (lowercase only - uppercase consumed by cursor nav)
      if (key >= 32 && key < 127) {
        ui_task.injectKey(key);
        composeNeedsRefresh = true;
        return;
      }
      return;
    }

    // ---- RENAMING MODE ----
    if (notes->isRenaming()) {
      // All input goes to rename handler (debounced like editing)
      ui_task.injectKey(key);
      composeNeedsRefresh = true;
      if (!notes->isRenaming()) {
        // Exited rename mode (confirmed or cancelled)
        ui_task.forceRefresh();
      }
      return;
    }

    // ---- DELETE CONFIRMATION MODE ----
    if (notes->isConfirmingDelete()) {
      ui_task.injectKey(key);
      if (!notes->isConfirmingDelete()) {
        // Exited confirm mode
        ui_task.forceRefresh();
      }
      return;
    }

    // ---- FILE LIST MODE ----
    if (notes->isInFileList()) {
      if (key == 'q') {
        notes->exitNotes();
        Serial.println("Exiting notes");
        ui_task.gotoHomeScreen();
        return;
      }

      // Shift+Backspace on a file = delete with confirmation
      if (key == '\b' && keyboard.wasShiftConsumed()) {
        if (notes->startDeleteFromList()) {
          ui_task.forceRefresh();
        }
        return;
      }

      // R on a file = rename
      if (key == 'r') {
        if (notes->startRename()) {
          composeNeedsRefresh = true;
          lastComposeRefresh = millis() - COMPOSE_REFRESH_INTERVAL;  // Trigger on next loop iteration
        }
        return;
      }

      // Normal keys pass through
      ui_task.injectKey(key);
      // Check if we just entered editing mode (new note via Enter)
      if (notes->isEditing()) {
        composeNeedsRefresh = true;
        lastComposeRefresh = millis();  // Draw after debounce interval, not immediately
      }
      return;
    }

    // ---- READING MODE ----
    if (notes->isReading()) {
      if (key == 'q') {
        ui_task.injectKey('q');
        return;
      }

      // Shift+Backspace = delete note
      if (key == '\b' && keyboard.wasShiftConsumed()) {
        Serial.println("Notes: Deleting current note");
        notes->deleteCurrentNote();
        ui_task.forceRefresh();
        return;
      }

      // All other keys (Enter for edit, W/S for page nav)
      ui_task.injectKey(key);
      if (notes->isEditing()) {
        composeNeedsRefresh = true;
        lastComposeRefresh = millis();  // Draw after debounce interval, not immediately
      }
      return;
    }

    return;
  }

  // *** SETTINGS MODE ***
  if (ui_task.isOnSettingsScreen()) {
    SettingsScreen* settings = (SettingsScreen*)ui_task.getSettingsScreen();

    // Q key: exit settings (when not editing)
    if (!settings->isEditing() && (key == 'q')) {
      if (settings->hasRadioChanges()) {
        // Let settings show "apply changes?" confirm dialog
        ui_task.injectKey(key);
      } else {
        Serial.println("Exiting settings");
        ui_task.gotoHomeScreen();
      }
      return;
    }

    // All other keys Ã¢â€ â€™ settings screen via injectKey
    ui_task.injectKey(key);
    return;
  }

  // *** REPEATER ADMIN MODE ***
  if (ui_task.isOnRepeaterAdmin()) {
    RepeaterAdminScreen* admin = (RepeaterAdminScreen*)ui_task.getRepeaterAdminScreen();
    RepeaterAdminScreen::AdminState astate = admin->getState();
    bool shiftDel = (key == '\b' && keyboard.wasShiftConsumed());

    // Helper: exit admin — room servers go to DM conversation if logged in, otherwise contacts
    auto exitAdmin = [&]() {
      int cidx = admin->getContactIdx();
      uint8_t perms = admin->getPermissions() & 0x03;
      ContactInfo ci;
      if (cidx >= 0 && perms > 0 && the_mesh.getContactByIdx(cidx, ci) && ci.type == ADV_TYPE_ROOM) {
        ui_task.gotoDMConversation(ci.name, cidx, perms);
        Serial.printf("Nav: Admin -> conversation for %s\n", ci.name);
      } else {
        ui_task.gotoContactsScreen();
        Serial.println("Nav: Admin -> contacts");
      }
    };

    // In password entry: Shift+Del exits, all other keys pass through normally
    if (astate == RepeaterAdminScreen::STATE_PASSWORD_ENTRY) {
      if (shiftDel) {
        exitAdmin();
      } else {
        ui_task.injectKey(key);
      }
      return;
    }

    // In category menu (top level): Shift+Del exits, C opens compose
    if (astate == RepeaterAdminScreen::STATE_CATEGORY_MENU) {
      if (shiftDel) {
        exitAdmin();
        return;
      }
      // C key: allow entering compose mode from admin menu
      if (key == 'c' || key == 'C') {
        composeDM = false;
        composeDMContactIdx = -1;
        composeMode = true;
        composeBuffer[0] = '\0';
        composePos = 0;
        drawComposeScreen();
        lastComposeRefresh = millis();
        return;
      }
      // All other keys pass to admin screen
      ui_task.injectKey(key);
      return;
    }

    // All other states (command menu, param entry, confirm, waiting,
    // response, error): convert Shift+Del to exit signal and let the
    // screen handle back-navigation internally
    if (shiftDel) {
      ui_task.injectKey(KEY_ADMIN_EXIT);
    } else {
      ui_task.injectKey(key);
    }
    return;
  }

  // SMS mode key routing (when on SMS screen)
  #ifdef HAS_4G_MODEM
  if (smsMode) {
    SMSScreen* smsScr = (SMSScreen*)ui_task.getSMSScreen();
    if (smsScr) {
      // Keep display alive — SMS routes many keys via handleInput() directly,
      // bypassing injectKey() which normally extends the auto-off timer.
      ui_task.keepAlive();
      if (smsScr->isInCallView()) {
        smsScr->handleInput(key);
        if (!smsScr->isInCallView()) {
          // Hangup just happened — show "Call Ended" alert immediately
          ui_task.showAlert("Call Ended", 2000);
        }
        // Force immediate render (call screen updates or return-to-dialer)
        ui_task.forceRefresh();
        ui_task.loop();
        return;
      }

      // Q from app menu → go home; Q from inner views is handled by SMSScreen
      if ((key == 'q' || key == '\b') && smsScr->getSubView() == SMSScreen::APP_MENU) {
        Serial.println("Nav: SMS -> Home");
        ui_task.gotoHomeScreen();
        return;
      }

      // Phone dialer: debounced refresh for digit entry, immediate render for
      // view transitions (Enter=call, Q=back). This avoids the 686ms e-ink
      // block per keypress while ensuring call/back screens render instantly.
      if (smsScr->getSubView() == SMSScreen::PHONE_DIALER) {
        smsScr->handleInput(key);
        if (smsScr->getSubView() == SMSScreen::PHONE_DIALER) {
          // Still on dialer (digit/backspace) — debounced refresh
          dialerNeedsRefresh = true;
          lastDialerRefresh = millis();
        } else {
          // View changed (startCall or Q back) — render immediately
          dialerNeedsRefresh = false;
          ui_task.forceRefresh();
          ui_task.loop();
        }
        return;
      }

      if (smsScr->isComposing()) {
        // Composing/text input: route directly to screen, bypass injectKey()
        // to avoid UITask scheduling its own competing refresh
        smsScr->handleInput(key);
        if (smsScr->isComposing()) {
          // Still composing — debounced refresh
          composeNeedsRefresh = true;
          lastComposeRefresh = millis();
        } else {
          // View changed (sent/cancelled) — immediate UITask refresh
          composeNeedsRefresh = false;
          ui_task.forceRefresh();
        }
      } else {
        // Non-compose views (inbox, conversation, contacts): use normal inject
        ui_task.injectKey(key);
      }
      return;
    }
  }
  #endif

  // *** WEB READER TEXT INPUT MODE ***
  // Match compose mode pattern: key handler sets a flag and returns instantly.
  // Main loop renders with 100ms debounce (same as COMPOSE_REFRESH_INTERVAL).
  // This way the key handler never blocks for 648ms during a render.
#ifdef MECK_WEB_READER
  if (ui_task.isOnWebReader()) {
    WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
    bool urlEdit = wr ? wr->isUrlEditing() : false;
    bool passEdit = wr ? wr->isPasswordEntry() : false;
    bool formEdit = wr ? wr->isFormFilling() : false;
    bool searchEdit = wr ? wr->isSearchEditing() : false;
    if (wr && (urlEdit || passEdit || formEdit || searchEdit)) {
      webReaderTextEntry = true;  // Suppress ui_task.loop() in main loop
      wr->handleInput(key);       // Updates buffer instantly, no render
      
      // Check if text entry ended (submitted, cancelled, etc.)
      if (!wr->isUrlEditing() && !wr->isPasswordEntry() && !wr->isFormFilling() && !wr->isSearchEditing()) {
        // Text entry ended
        webReaderTextEntry = false;
        webReaderNeedsRefresh = false;
        // fetchPage()/submitForm() handle their own rendering, or mode changed —
        // let ui_task.loop() resume on next iteration
      } else {
        // Still typing — request debounced refresh
        webReaderNeedsRefresh = true;
        lastWebReaderRefresh = millis();
      }
      return;
    } else {
      // Not in text entry — clear flag so ui_task.loop() resumes
      webReaderTextEntry = false;

      // Q from HOME mode exits the web reader entirely (like text reader)
      if ((key == 'q' || key == 'Q') && wr && wr->isHome() && !wr->isUrlEditing() && !wr->isSearchEditing()) {
        Serial.println("Exiting web reader");
        ui_task.gotoHomeScreen();
        return;
      }

      // Route keys through normal UITask for navigation/scrolling
      ui_task.injectKey(key);
      return;
    }
  }
#endif

  // Normal mode - not composing

  // Mic key release outside voice screen — ignore (PTT only matters on voice screen)
  if (key == KB_KEY_MIC_RELEASE) return;

  // Mic key press from any non-modal screen — open voice message screen
  #ifdef MECK_AUDIO_VARIANT
  if (key == KB_KEY_MIC) {
    Serial.println("Opening voice message screen (mic key)");
    if (!ui_task.getVoiceScreen()) {
      Serial.printf("Voice: lazy init - free heap: %d, largest block: %d\n",
                     ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      if (!audio) audio = new Audio();
      VoiceMessageScreen* voiceScr = new VoiceMessageScreen(&ui_task, audio);
      voiceScr->setSDReady(sdCardReady);
      ui_task.setVoiceScreen(voiceScr);
      Serial.printf("Voice: init complete - free heap: %d\n", ESP.getFreeHeap());
    } else {
      // Ensure Audio* is shared (may have been created by audiobook/alarm)
      VoiceMessageScreen* voiceScr = (VoiceMessageScreen*)ui_task.getVoiceScreen();
      if (!audio) audio = new Audio();
      voiceScr->setAudio(audio);
    }
    ui_task.gotoVoiceScreen();
    // Don't start recording here — user tapped mic to navigate.
    // Recording starts on mic press when already on voice screen.
    return;
  }
  #endif

  switch (key) {
    case 'c':
      // Open contacts list
      Serial.println("Opening contacts");
      ui_task.gotoContactsScreen();
      break;
    
    case 'm':
      // Go to channel message screen
      Serial.println("Opening channel messages");
      ui_task.gotoChannelScreen();
      break;
    
    case 'e':
      // Open text reader (ebooks)
      Serial.println("Opening text reader");
      ui_task.gotoTextReader();
      break;
    
    #ifndef HAS_4G_MODEM
    case 'p':
      // Open audiobook player - lazy-init Audio + screen on first use
      Serial.println("Opening audiobook player");
      if (!ui_task.getAudiobookScreen()) {
        Serial.printf("Audiobook: lazy init - free heap: %d, largest block: %d\n",
                       ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        audio = new Audio();
        AudiobookPlayerScreen* abScreen = new AudiobookPlayerScreen(&ui_task, audio, the_mesh.getNodePrefs());
        abScreen->setSDReady(sdCardReady);
        ui_task.setAudiobookScreen(abScreen);
        Serial.printf("Audiobook: init complete - free heap: %d\n", ESP.getFreeHeap());
      }
      ui_task.gotoAudiobookPlayer();
      break;
    #endif

    #ifdef MECK_AUDIO_VARIANT
    case 'k':
      // Open alarm clock (screen created at boot; just ensure Audio* is available)
      Serial.println("Opening alarm clock");
      if (!audio) {
        Serial.printf("Alarm: lazy init Audio - free heap: %d, largest block: %d\n",
                       ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        audio = new Audio();
      }
      {
        AlarmScreen* alarmScr = (AlarmScreen*)ui_task.getAlarmScreen();
        if (alarmScr) alarmScr->setAudio(audio);
      }
      ui_task.gotoAlarmScreen();
      break;
    #endif

    #ifdef HAS_4G_MODEM
    case 't':
      // Open SMS (4G variant only)
      Serial.println("Opening SMS");
      ui_task.gotoSMSScreen();
      break;
    #endif

    #ifdef MECK_WEB_READER
    case 'b':
      // Open web reader (browser)
      Serial.println("Opening web reader");
      {
        static bool webReaderWifiReady = false;
      #ifdef MECK_WIFI_COMPANION
        // WiFi companion: WiFi is already up from boot, no BLE to tear down
        webReaderWifiReady = true;
      #endif
        if (!webReaderWifiReady) {
      #ifdef BLE_PIN_CODE
          // WiFi needs ~40KB contiguous heap. The BLE controller holds ~30KB,
          // leaving only ~30KB largest block. We MUST release BLE memory first.
          //
          // This disables BLE for the duration of the session.
          // BLE comes back on reboot.
          Serial.printf("WebReader: heap BEFORE BT release: free=%d, largest=%d\n",
                         ESP.getFreeHeap(), ESP.getMaxAllocHeap());

          // 1) Stop BLE controller (disable + deinit)
          btStop();
          delay(50);

          // 2) Release the BT controller's reserved memory region back to heap
          esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);
          delay(50);

          Serial.printf("WebReader: heap AFTER BT release: free=%d, largest=%d\n",
                         ESP.getFreeHeap(), ESP.getMaxAllocHeap());
      #endif

          // Init WiFi while we have maximum contiguous heap
          if (WiFi.mode(WIFI_STA)) {
            Serial.println("WebReader: WiFi STA init OK");
            webReaderWifiReady = true;
          } else {
            Serial.println("WebReader: WiFi STA init FAILED");
            WiFi.mode(WIFI_OFF);
          }

          Serial.printf("WebReader: heap after WiFi init: free=%d, largest=%d\n",
                         ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        }
      }
      ui_task.gotoWebReader();
      break;
    #endif
    
    case 'g':
      // Open map screen, or re-center on GPS if already on map
      if (ui_task.isOnMapScreen()) {
        ui_task.injectKey('g');  // Re-center on GPS
      } else {
        Serial.println("Opening map");
        cpuPower.setBoost();  // Map render is CPU-intensive (PNG decode + SD reads)
        {
          MapScreen* ms = (MapScreen*)ui_task.getMapScreen();
          if (ms) {
            ms->setSDReady(sdCardReady);
            ms->setGPSPosition(sensors.node_lat,
                               sensors.node_lon);
            // Populate contact markers via iterator
            ms->clearMarkers();
            ContactsIterator it = the_mesh.startContactsIterator();
            ContactInfo ci;
            int markerCount = 0;
            while (it.hasNext(&the_mesh, ci)) {
              if (ci.gps_lat != 0 || ci.gps_lon != 0) {
                double lat = ((double)ci.gps_lat) / 1000000.0;
                double lon = ((double)ci.gps_lon) / 1000000.0;
                ms->addMarker(lat, lon, ci.name, ci.type);
                markerCount++;
              }
            }
            Serial.printf("MapScreen: %d contacts with GPS position\n", markerCount);
          }
        }
        ui_task.gotoMapScreen();
      }
      break;
    
    case 'n':
      // Open notes
      Serial.println("Opening notes");
      ui_task.gotoNotesScreen();
      break;
    
    case 's':
      // Open settings (from home), or navigate down on channel/contacts/admin/web/map/discovery/lastheard
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen() || ui_task.isOnRepeaterAdmin()
          || ui_task.isOnDiscoveryScreen() || ui_task.isOnLastHeardScreen()
#ifdef MECK_WEB_READER
          || ui_task.isOnWebReader()
#endif
          || ui_task.isOnMapScreen()
#ifdef MECK_AUDIO_VARIANT
          || ui_task.isOnAlarmScreen()
#endif
         ) {
        ui_task.injectKey('s');  // Pass directly for scrolling
      } else {
        Serial.println("Opening settings");
        ui_task.gotoSettingsScreen();
      }
      break;

    case 'w':
      // Navigate up/previous (scroll on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen() || ui_task.isOnRepeaterAdmin()
          || ui_task.isOnDiscoveryScreen() || ui_task.isOnLastHeardScreen()
#ifdef MECK_WEB_READER
          || ui_task.isOnWebReader()
#endif
          || ui_task.isOnMapScreen()
#ifdef MECK_AUDIO_VARIANT
          || ui_task.isOnAlarmScreen()
#endif
         ) {
        ui_task.injectKey('w');  // Pass directly for scrolling
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'a':
      // Navigate left or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen() || ui_task.isOnMapScreen()
#ifdef MECK_AUDIO_VARIANT
          || ui_task.isOnAlarmScreen()
#endif
         ) {
        ui_task.injectKey('a');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'd':
      // Navigate right or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen() || ui_task.isOnMapScreen()
#ifdef MECK_AUDIO_VARIANT
          || ui_task.isOnAlarmScreen()
#endif
         ) {
        ui_task.injectKey('d');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Next");
        ui_task.injectKey(0xF1);  // KEY_NEXT
      }
      break;
      
    case '\r':
      // Select/Enter - if on contacts screen, enter DM compose for chat contacts
      //                or repeater admin for repeater contacts
      if (ui_task.isOnContactsScreen()) {
        ContactsScreen* cs = (ContactsScreen*)ui_task.getContactsScreen();
        int idx = cs->getSelectedContactIdx();
        uint8_t ctype = cs->getSelectedContactType();
        if (idx >= 0 && ctype == ADV_TYPE_CHAT) {
          // If unread DMs exist, go to conversation view to read first
          if (ui_task.hasDMUnread(idx)) {
            char cname[32];
            cs->getSelectedContactName(cname, sizeof(cname));
            ui_task.clearDMUnread(idx);
            ui_task.gotoDMConversation(cname);
            Serial.printf("Unread DMs from %s — opening conversation\n", cname);
          } else {
            composeDM = true;
            composeDMContactIdx = idx;
            cs->getSelectedContactName(composeDMName, sizeof(composeDMName));
            composeMode = true;
            composeBuffer[0] = '\0';
            composePos = 0;
            Serial.printf("Entering DM compose to %s (idx %d)\n", composeDMName, idx);
            drawComposeScreen();
            lastComposeRefresh = millis();
          }
        } else if (idx >= 0 && ctype == ADV_TYPE_REPEATER) {
          // Open repeater admin screen
          char rname[32];
          cs->getSelectedContactName(rname, sizeof(rname));
          Serial.printf("Opening repeater admin for %s (idx %d)\n", rname, idx);
          ui_task.gotoRepeaterAdmin(idx);
        } else if (idx >= 0 && ctype == ADV_TYPE_ROOM) {
          // Room server: open login screen (after login, auto-redirects to conversation)
          char rname[32];
          cs->getSelectedContactName(rname, sizeof(rname));
          Serial.printf("Room %s — opening login\n", rname);
          ui_task.gotoRepeaterAdmin(idx);
        } else if (idx >= 0) {
          // Other contacts with unreads
          if (ui_task.hasDMUnread(idx)) {
            char cname[32];
            cs->getSelectedContactName(cname, sizeof(cname));
            ui_task.clearDMUnread(idx);
            ui_task.gotoDMConversation(cname);
          } else {
            Serial.printf("Selected contact type=%d idx=%d\n", ctype, idx);
          }
        }
      } else if (ui_task.isOnChannelScreen()) {
        // If path overlay is showing, Enter copies path text to compose buffer
        ChannelScreen* chScr2 = (ChannelScreen*)ui_task.getChannelScreen();
        if (chScr2 && chScr2->isShowingPathOverlay()) {
          char pathText[138];
          int pathLen = chScr2->formatPathAsText(pathText, sizeof(pathText));
          if (pathLen > 0) {
            int copyLen = pathLen < 137 ? pathLen : 137;
            memcpy(composeBuffer, pathText, copyLen);
            composeBuffer[copyLen] = '\0';
            composePos = copyLen;
          } else {
            composeBuffer[0] = '\0';
            composePos = 0;
          }
          composeDM = false;
          composeDMContactIdx = -1;
          composeChannelIdx = ui_task.getChannelScreenViewIdx();
          composeMode = true;
          chScr2->dismissPathOverlay();
          Serial.printf("Compose with path, channel %d, prefill %d chars\n", composeChannelIdx, composePos);
          drawComposeScreen();
          lastComposeRefresh = millis();
          break;
        }

        // DM inbox mode: pass Enter to ChannelScreen to open the selected conversation
        if (chScr2 && chScr2->isDMInboxMode()) {
          ui_task.injectKey('\r');
          break;
        }

        // DM conversation mode: Enter opens DM compose to the contact being viewed
        // (DM inbox mode Enter is handled by ChannelScreen::handleInput internally)
        if (chScr2 && chScr2->isDMConversation()) {
          const char* dmName = chScr2->getDMFilterName();
          if (dmName && dmName[0]) {
            uint32_t numC = the_mesh.getNumContacts();
            ContactInfo ci;
            for (uint32_t j = 0; j < numC; j++) {
              if (the_mesh.getContactByIdx(j, ci) && strcmp(ci.name, dmName) == 0) {
                composeDM = true;
                composeDMContactIdx = (int)j;
                strncpy(composeDMName, dmName, sizeof(composeDMName) - 1);
                composeDMName[sizeof(composeDMName) - 1] = '\0';
                composeMode = true;
                composeBuffer[0] = '\0';
                composePos = 0;
                ui_task.clearDMUnread(j);
                Serial.printf("DM conversation compose to %s (idx %d)\n", dmName, j);
                drawComposeScreen();
                lastComposeRefresh = millis();
                break;
              }
            }
          } else {
            ui_task.showAlert("No contact selected", 1000);
          }
          break;
        }

        composeDM = false;
        composeDMContactIdx = -1;
        composeChannelIdx = ui_task.getChannelScreenViewIdx();
        composeMode = true;

        // If reply select mode is active, pre-fill @SenderName
        char replySender[32];
        if (chScr2 && chScr2->isReplySelectMode()
            && chScr2->getReplySelectSender(replySender, sizeof(replySender))) {
          int prefixLen = snprintf(composeBuffer, sizeof(composeBuffer),
                                   "@%s ", replySender);
          composePos = prefixLen;
          chScr2->exitReplySelect();
          Serial.printf("Reply compose to @%s, channel %d\n",
                        replySender, composeChannelIdx);
        } else {
          composeBuffer[0] = '\0';
          composePos = 0;
          if (chScr2) chScr2->exitReplySelect();  // Clean up if somehow active
          Serial.printf("Entering compose mode, channel %d\n", composeChannelIdx);
        }
        drawComposeScreen();
        lastComposeRefresh = millis();
      } else if (ui_task.isOnDiscoveryScreen()) {
        // Discovery screen: Enter adds selected node to contacts
        DiscoveryScreen* ds = (DiscoveryScreen*)ui_task.getDiscoveryScreen();
        int didx = ds->getSelectedIdx();
        if (didx >= 0 && didx < the_mesh.getDiscoveredCount()) {
          const DiscoveredNode& node = the_mesh.getDiscovered(didx);
          if (node.already_in_contacts) {
            ui_task.showAlert("Already in contacts", 800);
          } else if (the_mesh.addDiscoveredToContacts(didx)) {
            char alertBuf[48];
            snprintf(alertBuf, sizeof(alertBuf), "Added: %s", node.contact.name);
            ui_task.showAlert(alertBuf, 1500);
            ui_task.notify(UIEventType::ack);
          } else {
            ui_task.showAlert("Add failed", 1000);
          }
        }
      } else if (ui_task.isOnLastHeardScreen()) {
        lastHeardToggleContact();
      } else {
        // Other screens: pass Enter as generic select
        ui_task.injectKey(13);
      }
      break;
      
    case 'z':
      // Zoom in on map screen
      if (ui_task.isOnMapScreen()) {
        ui_task.injectKey('z');
      }
      break;

    case 'x':
      // Zoom out on map screen, or export contacts on contacts screen
      if (ui_task.isOnMapScreen()) {
        ui_task.injectKey('x');
      } else if (ui_task.isOnContactsScreen()) {
        Serial.println("Contacts: Exporting to SD...");
        int exported = exportContactsToSD();
        if (exported >= 0) {
          char alertBuf[48];
          snprintf(alertBuf, sizeof(alertBuf), "Exported %d to SD", exported);
          ui_task.showAlert(alertBuf, 2000);
        } else {
          ui_task.showAlert("Export failed (check serial)", 2000);
        }
      }
      break;

    case 'r':
      // Reply select mode (channel screen) or import contacts (contacts screen)
      if (ui_task.isOnChannelScreen()) {
        ui_task.injectKey('r');
      } else if (ui_task.isOnContactsScreen()) {
        Serial.println("Contacts: Importing from SD...");
        int added = importContactsFromSD();
        if (added > 0) {
          // Invalidate the contacts screen cache so it rebuilds
          ContactsScreen* cs2 = (ContactsScreen*)ui_task.getContactsScreen();
          if (cs2) cs2->invalidateCache();
          char alertBuf[48];
          snprintf(alertBuf, sizeof(alertBuf), "+%d imported (%d total)",
                   added, (int)the_mesh.getNumContacts());
          ui_task.showAlert(alertBuf, 2500);
        } else if (added == 0) {
          ui_task.showAlert("No new contacts to add", 2000);
        } else {
          ui_task.showAlert("Import failed (no backup?)", 2000);
        }
      }
      break;

    case 'f':
      // Start discovery scan from home/contacts screen, or rescan on discovery screen
      if (ui_task.isOnContactsScreen() || ui_task.isOnHomeScreen()) {
        Serial.println("Starting discovery scan...");
        the_mesh.startDiscovery();
        ui_task.gotoDiscoveryScreen();
      } else if (ui_task.isOnDiscoveryScreen()) {
        ui_task.injectKey('f');  // pass through for rescan
      }
      break;

    case 'h':
      // Open Last Heard screen (passive advert list)
      if (!ui_task.isOnLastHeardScreen()) {
        Serial.println("Opening last heard");
        ui_task.gotoLastHeardScreen();
      }
      break;

    case 'l':
      // L = Login/Admin — from DM conversation, open repeater admin with auto-login
      if (ui_task.isOnChannelScreen()) {
        ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
        if (chScr && chScr->isDMConversation() && chScr->getDMContactPerms() > 0) {
          int cidx = chScr->getDMContactIdx();
          if (cidx >= 0) {
            ui_task.gotoRepeaterAdminDirect(cidx);
            Serial.printf("DM conversation: auto-login admin for idx %d\n", cidx);
          }
        }
      }
      break;

    case 'q':
    case '\b':
      // If channel screen reply select or path overlay is showing, dismiss it
      if (ui_task.isOnChannelScreen()) {
        ChannelScreen* chScr = (ChannelScreen*)ui_task.getChannelScreen();
        if (chScr && chScr->isReplySelectMode()) {
          ui_task.injectKey('q');
          break;
        }
        if (chScr && chScr->isShowingPathOverlay()) {
          ui_task.injectKey('q');
          break;
        }
      }
#ifdef MECK_WEB_READER
      // If web reader is in reading/link/wifi mode, inject q for internal navigation
      // (reading→home, wifi→home). Only exit to firmware home if already on web home.
      if (ui_task.isOnWebReader()) {
        WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
        if (wr && !wr->isHome()) {
          ui_task.injectKey('q');
          break;
        }
      }
#endif
      // Discovery screen: Q goes back to contacts (not home)
      if (ui_task.isOnDiscoveryScreen()) {
        the_mesh.stopDiscovery();
        Serial.println("Nav: Discovery -> Contacts");
        ui_task.gotoContactsScreen();
        break;
      }
      // Alarm screen: Q/backspace routing depends on sub-mode
#ifdef MECK_AUDIO_VARIANT
      if (ui_task.isOnAlarmScreen()) {
        AlarmScreen* alarmScr = (AlarmScreen*)ui_task.getAlarmScreen();
        if (alarmScr && alarmScr->isRinging()) {
          alarmScr->dismiss();
          ui_task.gotoHomeScreen();
        } else if (alarmScr && alarmScr->getMode() != AlarmScreen::ALARM_LIST) {
          // In edit/picker/digit mode — pass to screen (Q = back to list, backspace = delete)
          ui_task.injectKey(key);
        } else {
          // On alarm list — go home
          Serial.println("Nav: Alarm -> Home");
          ui_task.gotoHomeScreen();
        }
        break;
      }
#endif
      // Last Heard: Q goes back to home
      if (ui_task.isOnLastHeardScreen()) {
        Serial.println("Nav: Last Heard -> Home");
        ui_task.gotoHomeScreen();
        break;
      }
      // Go back to home screen (admin mode handled above)
      Serial.println("Nav: Back to home");
      ui_task.gotoHomeScreen();
      break;
      
    case ' ':
      // Space - also acts as next/select
      Serial.println("Nav: Space (Next)");
      ui_task.injectKey(0xF1);  // KEY_NEXT
      break;

    case 'u':
      // UTC offset edit (home screen GPS page handles this)
      ui_task.injectKey('u');
      break;

    case 'v':
      // View path overlay (channel screen only)
      if (ui_task.isOnChannelScreen()) {
        ui_task.injectKey('v');
      }
      break;
      
    default:
#ifdef MECK_WEB_READER
      // Pass unhandled keys to web reader (l=link, g=go, k=bookmark, 0-9=link#)
      if (ui_task.isOnWebReader()) {
        ui_task.injectKey(key);
        break;
      }
#endif
      // Pass unhandled keys to map screen (+, -, i, o for zoom)
      if (ui_task.isOnMapScreen()) {
        ui_task.injectKey(key);
        break;
      }
#ifdef MECK_AUDIO_VARIANT
      // Pass unhandled keys to alarm screen (digits for time entry, o for toggle)
      if (ui_task.isOnAlarmScreen()) {
        ui_task.injectKey(key);
        break;
      }
#endif
      Serial.printf("Unhandled key in normal mode: '%c' (0x%02X)\n", key, key);
      break;
  }
}

void drawComposeScreen() {
  #ifdef DISPLAY_CLASS
  display.startFrame();
  display.setTextSize(1);
  display.setColor(DisplayDriver::GREEN);
  display.setCursor(0, 0);
  
  // Get the channel name for display
  char headerBuf[40];
  if (composeDM) {
    snprintf(headerBuf, sizeof(headerBuf), "DM: %s", composeDMName);
  } else {
    ChannelDetails channel;
    if (the_mesh.getChannel(composeChannelIdx, channel)) {
      snprintf(headerBuf, sizeof(headerBuf), "To: %s", channel.name);
    } else {
      snprintf(headerBuf, sizeof(headerBuf), "To: Channel %d", composeChannelIdx);
    }
  }
  display.print(headerBuf);
  
  display.setColor(DisplayDriver::LIGHT);
  display.drawRect(0, 11, display.width(), 1);
  
  display.setCursor(0, 14);
  display.setColor(DisplayDriver::LIGHT);
  
  // Word wrap the compose buffer with word-boundary awareness
  // Uses advance width (cursor movement) not bounding box width for px tracking.
  // Advance = getTextWidth("cc") - getTextWidth("c") to get true cursor step.
  int y = 14;
  char charStr[2] = {0, 0};
  char dblStr[3] = {0, 0, 0};
  
  int px = 0;
  int lineW = display.width();
  bool atWordBoundary = true;
  
  for (int i = 0; i < composePos; i++) {
    uint8_t b = (uint8_t)composeBuffer[i];
    
    if (b == EMOJI_PAD_BYTE) continue;
    
    // Word wrap: when starting a new text word, check if it fits on this line
    if (atWordBoundary && b != ' ' && !isEmojiEscape(b) && px > 0) {
      int wordW = 0;
      for (int j = i; j < composePos; j++) {
        uint8_t wb = (uint8_t)composeBuffer[j];
        if (wb == EMOJI_PAD_BYTE) continue;
        if (wb == ' ' || isEmojiEscape(wb)) break;
        dblStr[0] = dblStr[1] = (char)wb;
        charStr[0] = (char)wb;
        wordW += display.getTextWidth(dblStr) - display.getTextWidth(charStr);
      }
      if (px + wordW > lineW) {
        px = 0;
        y += 12;
      }
    }
    
    if (isEmojiEscape(b)) {
      if (px + EMOJI_LG_W > lineW) {
        px = 0;
        y += 12;
      }
      const uint8_t* sprite = getEmojiSpriteLg(b);
      if (sprite) {
        display.drawXbm(px, y, sprite, EMOJI_LG_W, EMOJI_LG_H);
      }
      px += EMOJI_LG_W + 1;
      display.setCursor(px, y);
      atWordBoundary = true;
    } else if (b == ' ') {
      charStr[0] = ' ';
      dblStr[0] = dblStr[1] = ' ';
      int adv = display.getTextWidth(dblStr) - display.getTextWidth(charStr);
      if (px + adv > lineW) {
        px = 0;
        y += 12;
      } else {
        display.setCursor(px, y);
        display.print(charStr);
        px += adv;
      }
      atWordBoundary = true;
    } else {
      charStr[0] = (char)b;
      dblStr[0] = dblStr[1] = (char)b;
      int adv = display.getTextWidth(dblStr) - display.getTextWidth(charStr);
      if (px + adv > lineW) {
        px = 0;
        y += 12;
      }
      display.setCursor(px, y);
      display.print(charStr);
      px += adv;
      atWordBoundary = false;
    }
  }
  
  // Show cursor
  display.setCursor(px, y);
  display.print("_");
  
  // Status bar
  int statusY = display.height() - 12;
  display.setColor(DisplayDriver::LIGHT);
  display.drawRect(0, statusY - 2, display.width(), 1);
  display.setCursor(0, statusY);
  display.setColor(DisplayDriver::YELLOW);
  
  char status[40];
  if (composePos == 0) {
    // Empty buffer - show channel switching hint
    display.print("A/D:Ch $:Emoji");
    sprintf(status, "Sh+Del:X");
    display.setCursor(display.width() - display.getTextWidth(status) - 2, statusY);
    display.print(status);
  } else {
    // Has text - show send/cancel hint
    sprintf(status, "%d/137 $:Emj", composePos);
    display.print(status);
    sprintf(status, "Sh+Del:X");
    display.setCursor(display.width() - display.getTextWidth(status) - 2, statusY);
    display.print(status);
  }
  
  display.endFrame();
  #endif
}

void drawEmojiPicker() {
  #ifdef DISPLAY_CLASS
  display.startFrame();
  emojiPicker.draw(display);
  display.endFrame();
  #endif
}

void sendComposedMessage() {
  if (composePos == 0) return;

  cpuPower.setBoost();  // Boost CPU for crypto + radio TX
  
  // Convert escape bytes back to UTF-8 for mesh transmission and BLE app
  char utf8Buf[512];
  emojiUnescape(composeBuffer, utf8Buf, sizeof(utf8Buf));

  if (composeDM) {
    // Direct message to a specific contact
    if (composeDMContactIdx >= 0) {
      if (the_mesh.uiSendDirectMessage((uint32_t)composeDMContactIdx, utf8Buf)) {
        // Add to channel screen so sent DM appears in conversation view
        ui_task.addSentDM(composeDMName, the_mesh.getNodePrefs()->node_name, utf8Buf);
        ui_task.showAlert("DM sent!", 1500);
      } else {
        ui_task.showAlert("DM failed!", 1500);
      }
    } else {
      ui_task.showAlert("No contact!", 1500);
    }
    return;
  }

  // Channel (group) message
  ChannelDetails channel;
  if (the_mesh.getChannel(composeChannelIdx, channel)) {
    uint32_t timestamp = rtc_clock.getCurrentTime();
    int utf8Len = strlen(utf8Buf);
    
    if (the_mesh.sendGroupMessage(timestamp, channel.channel, 
                                   the_mesh.getNodePrefs()->node_name, 
                                   utf8Buf, utf8Len)) {
      ui_task.addSentChannelMessage(composeChannelIdx, 
                                     the_mesh.getNodePrefs()->node_name, 
                                     utf8Buf);
      
      the_mesh.queueSentChannelMessage(composeChannelIdx, timestamp,
                                        the_mesh.getNodePrefs()->node_name,
                                        utf8Buf);
      
      ui_task.showAlert("Sent!", 1500);
    } else {
      ui_task.showAlert("Send failed!", 1500);
    }
  } else {
    ui_task.showAlert("No channel!", 1500);
  }
}

// ============================================================================
// ESP32-audioI2S CALLBACKS
// ============================================================================
// The audio library calls these global functions - must be defined at file scope.
// Not available on 4G variant (no audio hardware).

#ifndef HAS_4G_MODEM
void audio_info(const char *info) {
  Serial.printf("Audio: %s\n", info);
}

void audio_eof_mp3(const char *info) {
  Serial.printf("Audio: End of file - %s\n", info);
  // Signal the player screen for auto-advance to next track
  AudiobookPlayerScreen* abPlayer =
    (AudiobookPlayerScreen*)ui_task.getAudiobookScreen();
  if (abPlayer) {
    abPlayer->onEOF();
  }
}
#endif // !HAS_4G_MODEM

#endif // LilyGo_TDeck_Pro