#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"
#include "variant.h"   // Board-specific defines (HAS_GPS, etc.)
#include "target.h"    // For sensors, board, etc.
#include "GPSDutyCycle.h"
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

  // DM compose mode (direct message to a specific contact)
  static bool composeDM = false;
  static int composeDMContactIdx = -1;
  static char composeDMName[32];
   // AGC reset - periodically re-assert RX boosted gain to prevent sensitivity drift
  #define AGC_RESET_INTERVAL_MS 500
  static unsigned long lastAGCReset = 0;

  // Emoji picker state
  #include "EmojiPicker.h"
  static bool emojiPickerMode = false;
  static EmojiPicker emojiPicker;
  
  // Text reader mode state
  static bool readerMode = false;

  // Notes mode state
  static bool notesMode = false;

  // Audiobook player — Audio object is heap-allocated on first use to avoid
  // consuming ~40KB of DMA/decode buffers at boot (starves BLE stack).
  #include "AudiobookPlayerScreen.h"
  #include "Audio.h"
  Audio* audio = nullptr;
  static bool audiobookMode = false;

  // Power management
  #if HAS_GPS
    GPSDutyCycle gpsDuty;
  #endif
  CPUPowerManager cpuPower;
  
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
#endif

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
  // Early SD card init Ã¢â‚¬â€ needed BEFORE the_mesh.begin() so we can restore
  // settings from a previous firmware flash.  The display SPI bus is already
  // up (display.begin() ran earlier), so SD can share it now.
  // ---------------------------------------------------------------------------
  #if defined(LilyGo_TDeck_Pro) && defined(HAS_SDCARD)
  {
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);  // Deselect SD initially

    if (SD.begin(SDCARD_CS, displaySpi, 4000000)) {
      sdCardReady = true;
      MESH_DEBUG_PRINTLN("setup() - SD card initialized (early)");

      // If SPIFFS was wiped (fresh flash), restore settings from SD backup
      if (restoreSettingsFromSD()) {
        MESH_DEBUG_PRINTLN("setup() - Settings restored from SD backup");
      }
    } else {
      MESH_DEBUG_PRINTLN("setup() - SD card not available");
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
  MESH_DEBUG_PRINTLN("setup() - WiFi mode");
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
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
    Serial2.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    MESH_DEBUG_PRINTLN("setup() - Reinitialized Serial2 for GPS after sensors.begin()");
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

  // ---------------------------------------------------------------------------
  // SD card is already initialized (early init above).
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
  }
  #endif

  // ---------------------------------------------------------------------------
  // First-boot onboarding detection
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

  // GPS duty cycle â€” honour saved pref, default to enabled on first boot
  #if HAS_GPS
  {
    bool gps_wanted = the_mesh.getNodePrefs()->gps_enabled;
    gpsDuty.setStreamCounter(&gpsStream);
    gpsDuty.begin(gps_wanted);
    if (gps_wanted) {
      sensors.setSettingValue("gps", "1");
    } else {
      sensors.setSettingValue("gps", "0");
    }
    MESH_DEBUG_PRINTLN("setup() - GPS duty cycle started (enabled=%d)", gps_wanted);
  }
  #endif

  // T-Deck Pro: BLE starts disabled for standalone-first operation
  // User can toggle it on from the Bluetooth home page (Enter or long-press)
  #if defined(LilyGo_TDeck_Pro) && defined(BLE_PIN_CODE)
    serial_interface.disable();
    MESH_DEBUG_PRINTLN("setup() - BLE disabled at boot (standalone mode)");
  #endif

  Serial.printf("setup() complete — free heap: %d, largest block: %d\n",
                 ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  MESH_DEBUG_PRINTLN("=== setup() - COMPLETE ===");
}

void loop() {
  the_mesh.loop();

  // GPS duty cycle â€” check for fix and manage power state
  #if HAS_GPS
  {
    bool gps_hw_on = gpsDuty.loop();
    if (gps_hw_on) {
      LocationProvider* lp = sensors.getLocationProvider();
      if (lp != NULL && lp->isValid()) {
        gpsDuty.notifyFix();
      }
    }
  }
  #endif

  sensors.loop();

  // CPU frequency auto-timeout back to idle
  cpuPower.loop();

  // Audiobook: service audio decode regardless of which screen is active
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
#ifdef DISPLAY_CLASS
  // Skip UITask rendering when in compose mode to prevent flickering
  #if defined(LilyGo_TDeck_Pro)
  // Also suppress during notes editing (same debounce pattern as compose)
  bool notesEditing = notesMode && ((NotesScreen*)ui_task.getNotesScreen())->isEditing();
  bool notesRenaming = notesMode && ((NotesScreen*)ui_task.getNotesScreen())->isRenaming();
  bool notesSuppressLoop = notesEditing || notesRenaming;
  if (!composeMode && !notesSuppressLoop) {
    ui_task.loop();
  } else {
    // Handle debounced screen refresh (compose, emoji picker, or notes editor)
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
      }
      lastComposeRefresh = millis();
      composeNeedsRefresh = false;
    }
  }
  // Track reader/notes/audiobook mode state for key routing
  readerMode = ui_task.isOnTextReader();
  notesMode = ui_task.isOnNotesScreen();
  audiobookMode = ui_task.isOnAudiobookPlayer();
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
        ui_task.gotoHomeScreen();
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
          ui_task.gotoHomeScreen();
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
          // Audio is playing — leave screen, audio continues via audioTick()
          Serial.println("Leaving audiobook player (audio continues in background)");
          ui_task.gotoHomeScreen();
        } else {
          // Paused or stopped — close book, show file list
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

    // All other keys â†’ settings screen via injectKey
    ui_task.injectKey(key);
    return;
  }

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
    
    case 'p':
      // Open audiobook player — lazy-init Audio + screen on first use
      Serial.println("Opening audiobook player");
      if (!ui_task.getAudiobookScreen()) {
        Serial.printf("Audiobook: lazy init — free heap: %d, largest block: %d\n",
                       ESP.getFreeHeap(), ESP.getMaxAllocHeap());
        audio = new Audio();
        AudiobookPlayerScreen* abScreen = new AudiobookPlayerScreen(&ui_task, audio);
        abScreen->setSDReady(sdCardReady);
        ui_task.setAudiobookScreen(abScreen);
        Serial.printf("Audiobook: init complete — free heap: %d\n", ESP.getFreeHeap());
      }
      ui_task.gotoAudiobookPlayer();
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
      // Open settings (from home), or navigate down on channel/contacts
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('s');  // Pass directly for channel/contacts scrolling
      } else {
        Serial.println("Opening settings");
        ui_task.gotoSettingsScreen();
      }
      break;

    case 'w':
      // Navigate up/previous (scroll on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('w');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'a':
      // Navigate left or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('a');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'd':
      // Navigate right or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('d');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Next");
        ui_task.injectKey(0xF1);  // KEY_NEXT
      }
      break;
      
    case '\r':
      // Enter = compose (only from channel or contacts screen)
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
        } else if (idx >= 0) {
          Serial.printf("Selected non-chat contact type=%d idx=%d\n", ctype, idx);
        }
      } else if (ui_task.isOnChannelScreen()) {
        composeDM = false;
        composeDMContactIdx = -1;
        composeChannelIdx = ui_task.getChannelScreenViewIdx();
        composeMode = true;
        composeBuffer[0] = '\0';
        composePos = 0;
        Serial.printf("Entering compose mode, channel %d\n", composeChannelIdx);
        drawComposeScreen();
        lastComposeRefresh = millis();
      } else {
        // Other screens: pass Enter as generic select
        ui_task.injectKey(13);
      }
      break;
      
    case 'q':
    case '\b':
      // Go back to home screen
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
      
    default:
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
// The audio library calls these global functions — must be defined at file scope.

void audio_info(const char *info) {
  Serial.printf("Audio: %s\n", info);
}

void audio_eof_mp3(const char *info) {
  Serial.printf("Audio: End of file - %s\n", info);
  // Playback finished — the player screen will detect this
  // via audio.isRunning() returning false
}

#endif // LilyGo_TDeck_Pro