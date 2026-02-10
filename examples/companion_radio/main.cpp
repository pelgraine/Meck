#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"
#include "variant.h"   // Board-specific defines (HAS_GPS, etc.)
#include "target.h"    // For sensors, board, etc.

// T-Deck Pro Keyboard support
#if defined(LilyGo_TDeck_Pro)
  #include "TCA8418Keyboard.h"
  #include <SD.h>
  #include "TextReaderScreen.h"
  #include "ContactsScreen.h"
  extern SPIClass displaySpi;  // From GxEPDDisplay.cpp, shared SPI bus

  TCA8418Keyboard keyboard(I2C_ADDR_KEYBOARD, &Wire);
  
  // Compose mode state
  static bool composeMode = false;
  static char composeBuffer[138];  // 137 bytes max + null terminator (matches BLE wire cost)
  static int composePos = 0;       // Current wire-cost byte count
  static uint8_t composeChannelIdx = 0;
  static unsigned long lastComposeRefresh = 0;
  static bool composeNeedsRefresh = false;
  #define COMPOSE_REFRESH_INTERVAL 600  // ms between e-ink refreshes while typing (refresh takes ~650ms)

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
  
  void initKeyboard();
  void handleKeyboardInput();
  void drawComposeScreen();
  void drawEmojiPicker();
  void sendComposedMessage();
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

  // Initialize SD card for text reader
  #if defined(LilyGo_TDeck_Pro) && defined(HAS_SDCARD)
  {
    pinMode(SDCARD_CS, OUTPUT);
    digitalWrite(SDCARD_CS, HIGH);  // Deselect SD initially
    
    if (SD.begin(SDCARD_CS, displaySpi, 4000000)) {
      MESH_DEBUG_PRINTLN("setup() - SD card initialized");
      // Tell the text reader that SD is ready, then pre-index books at boot
      TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
      if (reader) {
        reader->setSDReady(true);
        if (disp) {
          reader->bootIndex(*disp);
        }
      }
  }
  }
  #endif

  // Enable GPS by default on T-Deck Pro
  #if HAS_GPS
    // Set GPS enabled in both sensor manager and node prefs
    sensors.setSettingValue("gps", "1");
    the_mesh.getNodePrefs()->gps_enabled = 1;
    the_mesh.savePrefs();
    MESH_DEBUG_PRINTLN("setup() - GPS enabled by default");
  #endif

  // T-Deck Pro: BLE starts disabled for standalone-first operation
  // User can toggle it on from the Bluetooth home page (Enter or long-press)
  #if defined(LilyGo_TDeck_Pro) && defined(BLE_PIN_CODE)
    serial_interface.disable();
    MESH_DEBUG_PRINTLN("setup() - BLE disabled at boot (standalone mode)");
  #endif

  MESH_DEBUG_PRINTLN("=== setup() - COMPLETE ===");
}

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  // Skip UITask rendering when in compose mode to prevent flickering
  #if defined(LilyGo_TDeck_Pro)
  if (!composeMode) {
    ui_task.loop();
  } else {
    // Handle debounced compose/emoji picker screen refresh
    if (composeNeedsRefresh && (millis() - lastComposeRefresh) >= COMPOSE_REFRESH_INTERVAL) {
      if (emojiPickerMode) {
        drawEmojiPicker();
      } else {
        drawComposeScreen();
      }
      lastComposeRefresh = millis();
      composeNeedsRefresh = false;
    }
  }
  // Track reader mode state for key routing
  readerMode = ui_task.isOnTextReader();
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
    if ((key == 'a' || key == 'A') && composePos == 0 && !composeDM) {
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
    
    if ((key == 'd' || key == 'D') && composePos == 0 && !composeDM) {
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
  
  // *** TEXT READER MODE ***
  if (readerMode) {
    TextReaderScreen* reader = (TextReaderScreen*)ui_task.getTextReaderScreen();
    
    // Q key: if reading, reader handles it (close book -> file list)
    //         if on file list, exit reader entirely
    if (key == 'q' || key == 'Q') {
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
    
    // C key: allow entering compose mode from reader
    if (key == 'c' || key == 'C') {
      composeDM = false;
      composeDMContactIdx = -1;
      composeMode = true;
      composeBuffer[0] = '\0';
      composePos = 0;
      Serial.printf("Entering compose mode from reader, channel %d\n", composeChannelIdx);
      drawComposeScreen();
      lastComposeRefresh = millis();
      return;
    }
    
    // All other keys pass through to the reader screen
    ui_task.injectKey(key);
    return;
  }

  // Normal mode - not composing
  switch (key) {
    case 'c':
    case 'C':
      // Enter compose mode - DM if on contacts screen, channel otherwise
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
        }
      } else {
        composeDM = false;
        composeDMContactIdx = -1;
        composeMode = true;
        composeBuffer[0] = '\0';
        composePos = 0;
        // If on channel screen, sync compose channel with viewed channel
        if (ui_task.isOnChannelScreen()) {
          composeChannelIdx = ui_task.getChannelScreenViewIdx();
        }
        Serial.printf("Entering compose mode, channel %d\n", composeChannelIdx);
        drawComposeScreen();
        lastComposeRefresh = millis();
      }
      break;
    
    case 'm':
    case 'M':
      // Go to channel message screen
      Serial.println("Opening channel messages");
      ui_task.gotoChannelScreen();
      break;
    
    case 'r':
    case 'R':
      // Open text reader
      Serial.println("Opening text reader");
      ui_task.gotoTextReader();
      break;
    
    case 'n':
    case 'N':
      // Open contacts list
      Serial.println("Opening contacts");
      ui_task.gotoContactsScreen();
      break;
    
    case 'w':
    case 'W':
      // Navigate up/previous (scroll on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('w');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
    
    case 's':
    case 'S':
      // Navigate down/next (scroll on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('s');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Next");
        ui_task.injectKey(0xF1);  // KEY_NEXT
      }
      break;
      
    case 'a':
    case 'A':
      // Navigate left or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('a');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'd':
    case 'D':
      // Navigate right or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen() || ui_task.isOnContactsScreen()) {
        ui_task.injectKey('d');  // Pass directly for channel/contacts switching
      } else {
        Serial.println("Nav: Next");
        ui_task.injectKey(0xF1);  // KEY_NEXT
      }
      break;
      
    case '\r':
      // Select/Enter - if on contacts screen, enter DM compose for chat contacts
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
          // Non-chat contact selected (repeater, room, etc.) - future use
          Serial.printf("Selected non-chat contact type=%d idx=%d\n", ctype, idx);
        }
      } else {
        Serial.println("Nav: Enter/Select");
        ui_task.injectKey(13);  // KEY_ENTER
      }
      break;
      
    case 'q':
    case 'Q':
    case '\b':
      // If editing UTC offset on GPS page, pass through to cancel
      if (ui_task.isEditingHomeScreen()) {
        ui_task.injectKey('q');
      } else {
        // Go back to home screen
        Serial.println("Nav: Back to home");
        ui_task.gotoHomeScreen();
      }
      break;
      
    case 'u':
    case 'U':
      // UTC offset editing (on GPS home page)
      Serial.println("Nav: UTC offset");
      ui_task.injectKey('u');
      break;
      
    case ' ':
      // Space - also acts as next/select
      Serial.println("Nav: Space (Next)");
      ui_task.injectKey(0xF1);  // KEY_NEXT
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

#endif // LilyGo_TDeck_Pro