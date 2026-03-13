#include <Arduino.h>   // needed for PlatformIO
#ifdef BLE_PIN_CODE
  #include <esp_bt.h>    // for esp_bt_controller_mem_release (web reader WiFi)
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
  static bool audiobookMode = false;

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
// T5S3 E-Paper Pro — GT911 Touch Input
// =============================================================================
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

  static TouchDrvGT911 gt911Touch;
  static bool gt911Ready = false;
  static bool sdCardReady = false;  // T5S3 SD card state

  // Touch state machine — supports tap, long press, and swipe
  static bool touchDown = false;
  static unsigned long touchDownTime = 0;
  static int16_t touchDownX = 0;
  static int16_t touchDownY = 0;
  static int16_t touchLastX = 0;
  static int16_t touchLastY = 0;
  static unsigned long lastTouchSeenMs = 0;  // Last time getPoint() returned true
  #define TOUCH_LONG_PRESS_MS  500
  #define TOUCH_SWIPE_THRESHOLD 60   // Min pixels to count as a swipe (physical)
  #define TOUCH_LIFT_DEBOUNCE_MS 150 // No-touch duration before "finger lifted"
  #define TOUCH_MIN_INTERVAL_MS 300  // Min ms between accepted events
  static bool longPressHandled = false;
  static bool swipeHandled = false;
  static bool touchCooldown = false;
  static unsigned long lastTouchEventMs = 0;

  // Read GT911 in landscape orientation (960×540)
  // GT911 reports portrait (540×960), rotate: x=raw_y, y=540-1-raw_x
  // Note: Do NOT gate on GT911_PIN_INT — it pulses briefly per event
  // and goes high between reports, causing drags to look like taps.
  // Polling getPoint() directly works for continuous touch tracking.
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

  // Read GT911 in portrait orientation (540×960, rotation 3)
  // Maps GT911 native coords to portrait logical space
  // Read GT911 in portrait orientation (540×960, canvas rotation 3)
  // Rotation 3 maps logical(lx,ly) → physical(ly, 539-lx).
  // Inverting: logical_x = raw_x, logical_y = raw_y (GT911 native = portrait).
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

  // Unified touch reader — picks landscape or portrait based on display mode
  static bool readTouch(int16_t* outX, int16_t* outY) {
    if (display.isPortraitMode()) {
      return readTouchPortrait(outX, outY);
    }
    return readTouchLandscape(outX, outY);
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

// T5S3 touch mapping — must be after ui_task declaration
#if defined(LilyGo_T5S3_EPaper_Pro)
  // Map a single tap based on current screen context
  static char mapTouchTap(int16_t x, int16_t y) {
    // Convert physical screen coords to virtual (128×128) using current scale
    // Scale factors change between landscape (7.5, 4.22) and portrait (4.22, 7.5)
    float sx = display.isPortraitMode() ? ((float)EPD_HEIGHT / 128.0f) : ((float)EPD_WIDTH / 128.0f);
    float sy = display.isPortraitMode() ? ((float)EPD_WIDTH / 128.0f) : ((float)EPD_HEIGHT / 128.0f);
    int vx = (int)(x / sx);
    int vy = (int)(y / sy);

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
    if (ui_task.isOnHomeScreen()) {
      return (vx < 64) ? (char)KEY_PREV : (char)KEY_NEXT;
    }

    // Reader (reading mode): tap = next page
    if (ui_task.isOnTextReader()) {
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader && reader->isReading()) {
        return 'd';  // next page
      }
      return KEY_ENTER;  // file list: open selected
    }

    // Notes editing: tap → open keyboard for typing
    if (ui_task.isOnNotesScreen()) {
      NotesScreen* notes = (NotesScreen*)ui_task.getNotesScreen();
      if (notes && notes->isEditing()) {
        ui_task.showVirtualKeyboard(VKB_NOTES, "Edit Note", "", 137);
        return 0;
      }
    }

#ifdef MECK_WEB_READER
    // Web reader: context-dependent taps (VKB for text fields, navigation elsewhere)
    if (ui_task.isOnWebReader()) {
      WebReaderScreen* wr = (WebReaderScreen*)ui_task.getWebReaderScreen();
      if (wr) {
        if (wr->isReading()) {
          // Footer zone tap → open link VKB (if links exist)
          if (vy >= 113 && wr->getLinkCount() > 0) {
            ui_task.showVirtualKeyboard(VKB_WEB_LINK, "Link #", "", 3);
            return 0;
          }
          return 'd';  // Tap reading area → next page
        }

        if (wr->isHome()) {
          int sel = wr->getHomeSelected();
          if (sel == 1) {
            // URL bar → open VKB for URL entry
            ui_task.showVirtualKeyboard(VKB_WEB_URL, "Enter URL",
                                         wr->getUrlText(), WEB_MAX_URL_LEN - 1);
            return 0;
          }
          if (sel == 2) {
            // Search → open VKB for DuckDuckGo search
            ui_task.showVirtualKeyboard(VKB_WEB_SEARCH, "Search DuckDuckGo", "", 127);
            return 0;
          }
          return KEY_ENTER;  // IRC, bookmarks, history: select
        }

        if (wr->isWifiSetup()) {
          if (wr->isPasswordEntry()) {
            // Open VKB for WiFi password entry
            ui_task.showVirtualKeyboard(VKB_WEB_WIFI_PASS, "WiFi Password", "", 63);
            return 0;
          }
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

    // All other screens: tap = select
    return KEY_ENTER;
  }

  // Map a swipe direction to a key
  static char mapTouchSwipe(int16_t dx, int16_t dy) {
    bool horizontal = abs(dx) > abs(dy);

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

    // Channel screen: long press → compose to current channel
    if (ui_task.isOnChannelScreen()) {
      uint8_t chIdx = ui_task.getChannelScreenViewIdx();
      ChannelDetails ch;
      if (the_mesh.getChannel(chIdx, ch)) {
        char label[40];
        snprintf(label, sizeof(label), "To: %s", ch.name);
        ui_task.showVirtualKeyboard(VKB_CHANNEL_MSG, label, "", 137, chIdx);
      }
      return 0;
    }

    // Contacts screen: long press → DM for chat contacts, admin for repeaters
    if (ui_task.isOnContactsScreen()) {
      ContactsScreen* cs = (ContactsScreen*)ui_task.getContactsScreen();
      if (cs) {
        int idx = cs->getSelectedContactIdx();
        uint8_t ctype = cs->getSelectedContactType();
        if (idx >= 0 && ctype == ADV_TYPE_CHAT) {
          char dname[32];
          cs->getSelectedContactName(dname, sizeof(dname));
          char label[40];
          snprintf(label, sizeof(label), "DM: %s", dname);
          ui_task.showVirtualKeyboard(VKB_DM, label, "", 137, idx);
          return 0;
        } else if (idx >= 0 && ctype == ADV_TYPE_REPEATER) {
          ui_task.gotoRepeaterAdmin(idx);
          return 0;
        }
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
          ui_task.showVirtualKeyboard(VKB_ADMIN_PASSWORD, "Admin Password", "", 32);
          return 0;
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

    // Default: enter/select (settings toggle, etc.)
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
          unsigned long timeout = millis() + 8000;
          while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
            delay(100);
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

  // IMPORTANT: sensors.begin() calls initBasicGPS() which steals the GPS pins for Serial1
  // We need to reinitialize Serial2 to reclaim them
  #if HAS_GPS
    Serial2.end();  // Close any existing Serial2
    {
      uint32_t gps_baud = the_mesh.getNodePrefs()->gps_baudrate;
      if (gps_baud == 0) gps_baud = GPS_BAUDRATE;
      Serial2.begin(gps_baud, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
      MESH_DEBUG_PRINTLN("setup() - Reinitialized Serial2 for GPS at %lu baud", (unsigned long)gps_baud);
    }
  #endif

#ifdef DISPLAY_CLASS
  MESH_DEBUG_PRINTLN("setup() - about to call ui_task.begin()");
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
  MESH_DEBUG_PRINTLN("setup() - ui_task.begin() done");
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

  // RTC diagnostic — verify the auto-discovered RTC is working
  #if defined(LilyGo_T5S3_EPaper_Pro)
  {
    uint32_t rtcTime = rtc_clock.getCurrentTime();
    Serial.printf("setup() - RTC time: %lu (valid=%s)\n", rtcTime, 
                  rtcTime > 1700000000 ? "YES" : "NO");
    if (rtcTime < 1700000000) {
      Serial.println("setup() - RTC has no valid time (will be set by companion app)");
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
    }
  }
  #endif

  // GPS power — honour saved pref, default to enabled on first boot
  #if HAS_GPS
  {
    bool gps_wanted = the_mesh.getNodePrefs()->gps_enabled;
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
    MESH_DEBUG_PRINTLN("setup() - GPS power %s", gps_wanted ? "on" : "off");
  }
  #endif

  // BLE starts disabled for standalone-first operation
  // User can toggle it from the Bluetooth home page (Enter or long-press)
  #if (defined(LilyGo_TDeck_Pro) || defined(LilyGo_T5S3_EPaper_Pro)) && defined(BLE_PIN_CODE)
    serial_interface.disable();
    MESH_DEBUG_PRINTLN("setup() - BLE disabled at boot (standalone mode)");
  #endif

  Serial.printf("setup() complete â€” free heap: %d, largest block: %d\n",
                 ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  MESH_DEBUG_PRINTLN("=== setup() - COMPLETE ===");
}

void loop() {
  the_mesh.loop();


  sensors.loop();

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
  #ifdef HAS_4G_MODEM
    smsMode = ui_task.isOnSMSScreen();
  #endif
  #else
  ui_task.loop();
  #endif
#endif
  rtc_clock.tick();
  // Periodic AGC reset - re-assert boosted RX gain to prevent sensitivity drift
  if ((millis() - lastAGCReset) >= AGC_RESET_INTERVAL_MS) {
    radio_reset_agc();
    lastAGCReset = millis();
  }
  // Handle T-Deck Pro keyboard input
  #if defined(LilyGo_TDeck_Pro)
    handleKeyboardInput();
  #endif

  // ---------------------------------------------------------------------------
  // T5S3 GT911 Touch Input — tap/swipe/long-press state machine
  // Gestures:
  //   Tap = finger down + up with minimal movement → select/open
  //   Swipe = finger drag > threshold → scroll/page turn
  //   Long press = finger held > 500ms without moving → edit/enter
  // After processing an event, cooldown waits for finger lift before next event.
  // Touch is disabled while lock screen is active.
  // When virtual keyboard is active, taps route to keyboard.
  // ---------------------------------------------------------------------------
  #if defined(LilyGo_T5S3_EPaper_Pro)
  if (!ui_task.isLocked() && !ui_task.isVKBActive())
  {
    int16_t tx, ty;
    bool gotPoint = readTouch(&tx, &ty);
    unsigned long now = millis();

    if (gotPoint) {
      lastTouchSeenMs = now;  // Track when we last saw a valid touch report
    }

    // Determine if finger is "present" — GT911 getPoint() only returns true
    // once per report cycle (~10ms), then returns false until the next report.
    // During a blocking e-ink refresh (~1s), many cycles are missed.
    // So "finger lifted" = no valid report for TOUCH_LIFT_DEBOUNCE_MS.
    bool fingerPresent = (now - lastTouchSeenMs) < TOUCH_LIFT_DEBOUNCE_MS;

    // Rate limit — after processing an event, wait for finger lift + cooldown
    if (touchCooldown) {
      if (!fingerPresent && (now - lastTouchEventMs) >= TOUCH_MIN_INTERVAL_MS) {
        touchCooldown = false;
        touchDown = false;
      }
    }
    else if (gotPoint && !touchDown) {
      // Finger just touched down (first valid report)
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
      // Finger still down — update position if we got a new point
      if (gotPoint) {
        touchLastX = tx;
        touchLastY = ty;
      }

      int16_t dx = touchLastX - touchDownX;
      int16_t dy = touchLastY - touchDownY;
      int16_t dist = abs(dx) > abs(dy) ? abs(dx) : abs(dy);

      // Swipe detection — fire once when threshold exceeded
      if (!swipeHandled && !longPressHandled && dist >= TOUCH_SWIPE_THRESHOLD) {
        swipeHandled = true;
        Serial.printf("[Touch] SWIPE dx=%d dy=%d\n", dx, dy);
        char c = mapTouchSwipe(dx, dy);
        if (c) {
          ui_task.injectKey(c);
          cpuPower.setBoost();
        }
        lastTouchEventMs = now;
        touchCooldown = true;
      }
      // Long press — only if finger hasn't moved much
      else if (!longPressHandled && !swipeHandled && dist < TOUCH_SWIPE_THRESHOLD &&
               (now - touchDownTime) >= TOUCH_LONG_PRESS_MS) {
        longPressHandled = true;
        Serial.printf("[Touch] LONG PRESS at (%d,%d)\n", touchDownX, touchDownY);
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
      // Finger lifted (no report for TOUCH_LIFT_DEBOUNCE_MS)
      touchDown = false;
      if (!longPressHandled && !swipeHandled) {
        Serial.printf("[Touch] TAP at (%d,%d)\n", touchDownX, touchDownY);
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

  // Virtual keyboard touch routing.
  // Guard: require finger lift AND 2s after VKB opened before accepting taps.
  // The 2s covers the ~1s blocking e-ink refresh plus margin for finger lift.
  {
    static bool vkbNeedLift = true;

    if (ui_task.isVKBActive()) {
      int16_t tx, ty;
      bool gotPt = readTouch(&tx, &ty);

      if (!gotPt) {
        vkbNeedLift = false;  // Finger lifted
      }

      bool cooldownOk = (millis() - ui_task.vkbOpenedAt()) > 2000;

      if (gotPt && !vkbNeedLift && cooldownOk) {
        float sx = display.isPortraitMode() ? ((float)EPD_HEIGHT / 128.0f) : ((float)EPD_WIDTH / 128.0f);
        float sy = display.isPortraitMode() ? ((float)EPD_WIDTH / 128.0f) : ((float)EPD_HEIGHT / 128.0f);
        int vx = (int)(tx / sx);
        int vy = (int)(ty / sy);
        if (ui_task.getVKB().handleTap(vx, vy)) {
          ui_task.forceRefresh();
        }
        vkbNeedLift = true;  // Require lift before next tap
      }
    } else {
      vkbNeedLift = true;  // Reset for next VKB open
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
  
  Serial.printf("handleKeyboardInput: key='%c' (0x%02X) composeMode=%d\n", 
                key >= 32 ? key : '?', key, composeMode);
  
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
    if (key == '\r') {
      // Enter - send the message
      Serial.println("Compose: Enter pressed, sending...");
      if (composePos > 0) {
        sendComposedMessage();
      }
      bool wasDM = composeDM;
      composeMode = false;
      emojiPickerMode = false;
      composeDM = false;
      composeDMContactIdx = -1;
      composeBuffer[0] = '\0';
      composePos = 0;
      if (wasDM) {
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
        composeMode = false;
        emojiPickerMode = false;
        composeDM = false;
        composeDMContactIdx = -1;
        composeBuffer[0] = '\0';
        composePos = 0;
        if (wasDM) {
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
      // Previous channel
      if (composeChannelIdx > 0) {
        composeChannelIdx--;
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
      // Next channel
      ChannelDetails ch;
      uint8_t nextIdx = composeChannelIdx + 1;
      if (the_mesh.getChannel(nextIdx, ch) && ch.name[0] != '\0') {
        composeChannelIdx = nextIdx;
      } else {
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

    // In password entry: Shift+Del exits, all other keys pass through normally
    if (astate == RepeaterAdminScreen::STATE_PASSWORD_ENTRY) {
      if (shiftDel) {
        Serial.println("Nav: Back to contacts from admin login");
        ui_task.gotoContactsScreen();
      } else {
        ui_task.injectKey(key);
      }
      return;
    }

    // In category menu (top level): Shift+Del exits to contacts, C opens compose
    if (astate == RepeaterAdminScreen::STATE_CATEGORY_MENU) {
      if (shiftDel) {
        Serial.println("Nav: Back to contacts from admin menu");
        ui_task.gotoContactsScreen();
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
        AudiobookPlayerScreen* abScreen = new AudiobookPlayerScreen(&ui_task, audio);
        abScreen->setSDReady(sdCardReady);
        ui_task.setAudiobookScreen(abScreen);
        Serial.printf("Audiobook: init complete - free heap: %d\n", ESP.getFreeHeap());
      }
      ui_task.gotoAudiobookPlayer();
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
                Serial.printf("  marker: %s @ %.4f,%.4f (type=%d)\n", ci.name, lat, lon, ci.type);
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
      {
        NotesScreen* notesScr2 = (NotesScreen*)ui_task.getNotesScreen();
        if (notesScr2) {
          uint32_t ts = rtc_clock.getCurrentTime();
          int8_t utcOff = the_mesh.getNodePrefs()->utc_offset_hours;
          notesScr2->setTimestamp(ts, utcOff);
        }
      }
      ui_task.gotoNotesScreen();
      break;
    
    case 's':
      // Open settings (from home), or navigate down on channel/contacts/admin/web/map/discovery
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen() || ui_task.isOnRepeaterAdmin()
          || ui_task.isOnDiscoveryScreen()
#ifdef MECK_WEB_READER
          || ui_task.isOnWebReader()
#endif
          || ui_task.isOnMapScreen()
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
          || ui_task.isOnDiscoveryScreen()
#ifdef MECK_WEB_READER
          || ui_task.isOnWebReader()
#endif
          || ui_task.isOnMapScreen()
         ) {
        ui_task.injectKey('w');  // Pass directly for scrolling
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'a':
      // Navigate left or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen() || ui_task.isOnMapScreen()) {
        ui_task.injectKey('a');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'd':
      // Navigate right or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen() || ui_task.isOnMapScreen()) {
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
          composeDM = true;
          composeDMContactIdx = idx;
          cs->getSelectedContactName(composeDMName, sizeof(composeDMName));
          composeMode = true;
          composeBuffer[0] = '\0';
          composePos = 0;
          Serial.printf("Entering DM compose to %s (idx %d)\n", composeDMName, idx);
          drawComposeScreen();
          lastComposeRefresh = millis();
        } else if (idx >= 0 && ctype == ADV_TYPE_REPEATER) {
          // Open repeater admin screen
          char rname[32];
          cs->getSelectedContactName(rname, sizeof(rname));
          Serial.printf("Opening repeater admin for %s (idx %d)\n", rname, idx);
          ui_task.gotoRepeaterAdmin(idx);
        } else if (idx >= 0) {
          // Non-chat, non-repeater contact (room, sensor, etc.) - future use
          Serial.printf("Selected contact type=%d idx=%d\n", ctype, idx);
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
      // Start discovery scan from contacts screen, or rescan on discovery screen
      if (ui_task.isOnContactsScreen()) {
        Serial.println("Contacts: Starting discovery scan...");
        the_mesh.startDiscovery();
        ui_task.gotoDiscoveryScreen();
      } else if (ui_task.isOnDiscoveryScreen()) {
        ui_task.injectKey('f');  // pass through for rescan
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