#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include "MyMesh.h"

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
  Serial.println("=== setup() - STARTING ===");

  board.begin();
  Serial.println("setup() - board.begin() done");

#ifdef DISPLAY_CLASS
  DisplayDriver* disp = NULL;
  Serial.println("setup() - about to call display.begin()");
  
  // =========================================================================
  // T-Deck Pro V1.1: Initialize E-Ink reset pin BEFORE display.begin()
  // This is critical - the display won't work without proper reset sequence
  // =========================================================================
  #if defined(LilyGo_TDeck_Pro)
    // Initialize E-Ink reset pin (GPIO 16)
    pinMode(PIN_DISPLAY_RST, OUTPUT);
    digitalWrite(PIN_DISPLAY_RST, HIGH);
    Serial.println("setup() - E-Ink reset pin initialized");
    
    // Initialize Touch reset pin (GPIO 38) 
    #ifdef CST328_PIN_RST
      pinMode(CST328_PIN_RST, OUTPUT);
      digitalWrite(CST328_PIN_RST, HIGH);
      delay(20);
      digitalWrite(CST328_PIN_RST, LOW);
      delay(80);
      digitalWrite(CST328_PIN_RST, HIGH);
      delay(20);
      Serial.println("setup() - Touch reset pin initialized");
    #endif
  #endif
  // =========================================================================
  
  if (display.begin()) {
    Serial.println("setup() - display.begin() returned true");
    disp = &display;
    disp->startFrame();
  #ifdef ST7789
    disp->setTextSize(2);
  #endif
    disp->drawTextCentered(disp->width() / 2, 28, "Loading...");
    disp->endFrame();
    Serial.println("setup() - Loading screen drawn");
  } else {
    Serial.println("setup() - display.begin() returned false!");
  }
#endif

  Serial.println("setup() - about to call radio_init()");
  if (!radio_init()) { 
    Serial.println("setup() - radio_init() FAILED! Halting.");
    halt(); 
  }
  Serial.println("setup() - radio_init() done");

  Serial.println("setup() - about to call fast_rng.begin()");
  fast_rng.begin(radio_get_rng_seed());
  Serial.println("setup() - fast_rng.begin() done");

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  Serial.println("setup() - NRF52/STM32 filesystem init");
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
  Serial.println("setup() - about to call store.begin()");
  store.begin();
  Serial.println("setup() - store.begin() done");
  
  Serial.println("setup() - about to call the_mesh.begin()");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  Serial.println("setup() - the_mesh.begin() done");

#ifdef BLE_PIN_CODE
  Serial.println("setup() - about to call serial_interface.begin() with BLE");
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  Serial.println("setup() - serial_interface.begin() done");
#else
  serial_interface.begin(Serial);
#endif
  Serial.println("setup() - about to call the_mesh.startInterface()");
  the_mesh.startInterface(serial_interface);
  Serial.println("setup() - the_mesh.startInterface() done");

#elif defined(RP2040_PLATFORM)
  Serial.println("setup() - RP2040 filesystem init");
  LittleFS.begin();
  Serial.println("setup() - about to call store.begin()");
  store.begin();
  Serial.println("setup() - store.begin() done");
  
  Serial.println("setup() - about to call the_mesh.begin()");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  Serial.println("setup() - the_mesh.begin() done");

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
  Serial.println("setup() - about to call the_mesh.startInterface()");
  the_mesh.startInterface(serial_interface);
  Serial.println("setup() - the_mesh.startInterface() done");

#elif defined(ESP32)
  Serial.println("setup() - ESP32 filesystem init - calling SPIFFS.begin()");
  SPIFFS.begin(true);
  Serial.println("setup() - SPIFFS.begin() done");
  
  Serial.println("setup() - about to call store.begin()");
  store.begin();
  Serial.println("setup() - store.begin() done");
  
  Serial.println("setup() - about to call the_mesh.begin()");
  the_mesh.begin(
    #ifdef DISPLAY_CLASS
        disp != NULL
    #else
        false
    #endif
  );
  Serial.println("setup() - the_mesh.begin() done");

#ifdef WIFI_SSID
  Serial.println("setup() - WiFi mode");
  WiFi.begin(WIFI_SSID, WIFI_PWD);
  serial_interface.begin(TCP_PORT);
#elif defined(BLE_PIN_CODE)
  Serial.println("setup() - about to call serial_interface.begin() with BLE");
  serial_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name, the_mesh.getBLEPin());
  Serial.println("setup() - serial_interface.begin() done");
#elif defined(SERIAL_RX)
  companion_serial.setPins(SERIAL_RX, SERIAL_TX);
  companion_serial.begin(115200);
  serial_interface.begin(companion_serial);
#else
  serial_interface.begin(Serial);
#endif
  Serial.println("setup() - about to call the_mesh.startInterface()");
  the_mesh.startInterface(serial_interface);
  Serial.println("setup() - the_mesh.startInterface() done");

#else
  #error "need to define filesystem"
#endif

  Serial.println("setup() - about to call sensors.begin()");
  sensors.begin();
  Serial.println("setup() - sensors.begin() done");

#ifdef DISPLAY_CLASS
  Serial.println("setup() - about to call ui_task.begin()");
  ui_task.begin(disp, &sensors, the_mesh.getNodePrefs());
  Serial.println("setup() - ui_task.begin() done");
#endif

  Serial.println("=== setup() - COMPLETE ===");
}

void loop() {
  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();
}
