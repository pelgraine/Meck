#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"
#include "variant.h"   // Board-specific defines (HAS_GPS, etc.)
#include "target.h"    // For sensors, board, etc.

// T-Deck Pro Keyboard support
#if defined(LilyGo_TDeck_Pro)
  #include "TCA8418Keyboard.h"
  TCA8418Keyboard keyboard(I2C_ADDR_KEYBOARD, &Wire);
  
  // Compose mode state
  static bool composeMode = false;
  static char composeBuffer[138];  // 137 chars max + null terminator
  static int composePos = 0;
  static uint8_t composeChannelIdx = 0;  // Which channel to send to
  static unsigned long lastComposeRefresh = 0;
  static bool composeNeedsRefresh = false;
  #define COMPOSE_REFRESH_INTERVAL 600  // ms between e-ink refreshes while typing (refresh takes ~650ms)
  
  void initKeyboard();
  void handleKeyboardInput();
  void drawComposeScreen();
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

  // Enable GPS by default on T-Deck Pro
  #if HAS_GPS
    // Set GPS enabled in both sensor manager and node prefs
    sensors.setSettingValue("gps", "1");
    the_mesh.getNodePrefs()->gps_enabled = 1;
    the_mesh.savePrefs();
    MESH_DEBUG_PRINTLN("setup() - GPS enabled by default");
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
    // Handle debounced compose screen refresh
    if (composeNeedsRefresh && (millis() - lastComposeRefresh) >= COMPOSE_REFRESH_INTERVAL) {
      drawComposeScreen();
      lastComposeRefresh = millis();
      composeNeedsRefresh = false;
    }
  }
  #else
  ui_task.loop();
  #endif
#endif
  rtc_clock.tick();

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
    // In compose mode - handle text input
    if (key == '\r') {
      // Enter - send the message
      Serial.println("Compose: Enter pressed, sending...");
      if (composePos > 0) {
        sendComposedMessage();
      }
      composeMode = false;
      composeBuffer[0] = '\0';
      composePos = 0;
      ui_task.gotoHomeScreen();
      return;
    }
    
    if (key == '\b') {
      // Backspace - check if shift was recently pressed for cancel combo
      if (keyboard.wasShiftRecentlyPressed(500)) {
        // Shift+Backspace = Cancel (works anytime)
        Serial.println("Compose: Shift+Backspace, cancelling...");
        composeMode = false;
        composeBuffer[0] = '\0';
        composePos = 0;
        ui_task.gotoHomeScreen();
        return;
      }
      // Regular backspace - delete last character
      if (composePos > 0) {
        composePos--;
        composeBuffer[composePos] = '\0';
        Serial.printf("Compose: Backspace, pos now %d\n", composePos);
        composeNeedsRefresh = true;  // Use debounced refresh
      }
      return;
    }
    
    // A/D keys switch channels (only when buffer is empty or as special function)
    if ((key == 'a' || key == 'A') && composePos == 0) {
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
      drawComposeScreen();
      return;
    }
    
    if ((key == 'd' || key == 'D') && composePos == 0) {
      // Next channel
      ChannelDetails ch;
      uint8_t nextIdx = composeChannelIdx + 1;
      if (the_mesh.getChannel(nextIdx, ch) && ch.name[0] != '\0') {
        composeChannelIdx = nextIdx;
      } else {
        composeChannelIdx = 0;  // Wrap to first channel
      }
      Serial.printf("Compose: Channel switched to %d\n", composeChannelIdx);
      drawComposeScreen();
      return;
    }
    
    // Regular character input
    if (key >= 32 && key < 127 && composePos < 137) {
      composeBuffer[composePos++] = key;
      composeBuffer[composePos] = '\0';
      Serial.printf("Compose: Added '%c', pos now %d\n", key, composePos);
      composeNeedsRefresh = true;  // Use debounced refresh
    }
    return;
  }
  
  // Normal mode - not composing
  switch (key) {
    case 'c':
    case 'C':
      // Enter compose mode
      composeMode = true;
      composeBuffer[0] = '\0';
      composePos = 0;
      // If on channel screen, sync compose channel with viewed channel
      if (ui_task.isOnChannelScreen()) {
        composeChannelIdx = ui_task.getChannelScreenViewIdx();
      }
      Serial.printf("Entering compose mode, channel %d\n", composeChannelIdx);
      drawComposeScreen();
      break;
    
    case 'm':
    case 'M':
      // Go to channel message screen
      Serial.println("Opening channel messages");
      ui_task.gotoChannelScreen();
      break;
    
    case 'w':
    case 'W':
      // Navigate up/previous (scroll on channel screen)
      if (ui_task.isOnChannelScreen()) {
        ui_task.injectKey('w');  // Pass directly for channel switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
    
    case 's':
    case 'S':
      // Navigate down/next (scroll on channel screen)
      if (ui_task.isOnChannelScreen()) {
        ui_task.injectKey('s');  // Pass directly for channel switching
      } else {
        Serial.println("Nav: Next");
        ui_task.injectKey(0xF1);  // KEY_NEXT
      }
      break;
      
    case 'a':
    case 'A':
      // Navigate left or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen()) {
        ui_task.injectKey('a');  // Pass directly for channel switching
      } else {
        Serial.println("Nav: Previous");
        ui_task.injectKey(0xF2);  // KEY_PREV
      }
      break;
      
    case 'd':
    case 'D':
      // Navigate right or switch channel (on channel screen)
      if (ui_task.isOnChannelScreen()) {
        ui_task.injectKey('d');  // Pass directly for channel switching
      } else {
        Serial.println("Nav: Next");
        ui_task.injectKey(0xF1);  // KEY_NEXT
      }
      break;
      
    case '\r':
      // Select/Enter
      Serial.println("Nav: Enter/Select");
      ui_task.injectKey(13);  // KEY_ENTER
      break;
      
    case 'q':
    case 'Q':
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
  ChannelDetails channel;
  char headerBuf[40];
  if (the_mesh.getChannel(composeChannelIdx, channel)) {
    snprintf(headerBuf, sizeof(headerBuf), "To: %s", channel.name);
  } else {
    snprintf(headerBuf, sizeof(headerBuf), "To: Channel %d", composeChannelIdx);
  }
  display.print(headerBuf);
  
  display.setColor(DisplayDriver::LIGHT);
  display.drawRect(0, 11, display.width(), 1);
  
  display.setCursor(0, 14);
  display.setColor(DisplayDriver::LIGHT);
  
  // Word wrap the compose buffer - calculate chars per line based on actual font width
  int x = 0;
  int y = 14;
  uint16_t testWidth = display.getTextWidth("MMMMMMMMMM");  // 10 wide chars
  int charsPerLine = (testWidth > 0) ? (display.width() * 10) / testWidth : 20;
  if (charsPerLine < 12) charsPerLine = 12;
  if (charsPerLine > 40) charsPerLine = 40;
  char charStr[2] = {0, 0};  // Buffer for single character as string
  
  for (int i = 0; i < composePos; i++) {
    charStr[0] = composeBuffer[i];
    display.print(charStr);
    x++;
    if (x >= charsPerLine) {
      x = 0;
      y += 11;
      display.setCursor(0, y);
    }
  }
  
  // Show cursor
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
    display.print("A/D:Ch");
    sprintf(status, "Sh+Del:X");
    display.setCursor(display.width() - display.getTextWidth(status) - 2, statusY);
    display.print(status);
  } else {
    // Has text - show send/cancel hint
    sprintf(status, "%d/137 Ent:Send", composePos);
    display.print(status);
    sprintf(status, "Sh+Del:X");
    display.setCursor(display.width() - display.getTextWidth(status) - 2, statusY);
    display.print(status);
  }
  
  display.endFrame();
  #endif
}

void sendComposedMessage() {
  if (composePos == 0) return;
  
  // Get the selected channel
  ChannelDetails channel;
  if (the_mesh.getChannel(composeChannelIdx, channel)) {
    uint32_t timestamp = rtc_clock.getCurrentTime();
    
    // Send to channel
    if (the_mesh.sendGroupMessage(timestamp, channel.channel, 
                                   the_mesh.getNodePrefs()->node_name, 
                                   composeBuffer, composePos)) {
      // Add the sent message to local channel history so we can see what we sent
      ui_task.addSentChannelMessage(composeChannelIdx, 
                                     the_mesh.getNodePrefs()->node_name, 
                                     composeBuffer);
      
      // Queue message for BLE app sync (so sent messages appear in companion app)
      the_mesh.queueSentChannelMessage(composeChannelIdx, timestamp,
                                        the_mesh.getNodePrefs()->node_name,
                                        composeBuffer);
      
      ui_task.showAlert("Sent!", 1500);
    } else {
      ui_task.showAlert("Send failed!", 1500);
    }
  } else {
    ui_task.showAlert("No channel!", 1500);
  }
}

#endif // LilyGo_TDeck_Pro