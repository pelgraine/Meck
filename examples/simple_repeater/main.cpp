#include <Arduino.h>   // needed for PlatformIO
#include <Mesh.h>
#include <time.h> 
#include "MyMesh.h"

#ifdef HAS_4G_MODEM
#include <SD.h>
#include "CellularMQTT.h"
#endif

#ifdef MECK_WIFI_REMOTE
#if defined(HAS_SDCARD) || defined(SDCARD_CS)
#include <SD.h>
#endif
#include "WiFiMQTT.h"
#endif

#ifdef DISPLAY_CLASS
  #include "UITask.h"
  static UITask ui_task(display);
#endif

StdRNG fast_rng;
SimpleMeshTables tables;

MyMesh the_mesh(board, radio_driver, *new ArduinoMillis(), fast_rng, rtc_clock, tables);

void halt() {
  while (1) ;
}

static char command[160];

// For power saving
unsigned long lastActive = 0; // mark last active time
unsigned long nextSleepinSecs = 120; // next sleep in seconds. The first sleep (if enabled) is after 2 minutes from boot

#if (defined(HAS_4G_MODEM) || defined(MECK_WIFI_REMOTE)) && (defined(HAS_SDCARD) || defined(SDCARD_CS))
static bool sdCardReady = false;
#endif

void setup() {
  Serial.begin(115200);
  delay(1000);

  board.begin();

  // For power saving
  lastActive = millis(); // mark last active time since boot

#ifdef DISPLAY_CLASS
  if (display.begin()) {
    display.startFrame();
    display.setCursor(0, 0);
    display.print("Please wait...");
    display.endFrame();
  }
#endif

  if (!radio_init()) {
    halt();
  }

  fast_rng.begin(radio_get_rng_seed());

  FILESYSTEM* fs;
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  InternalFS.begin();
  fs = &InternalFS;
  IdentityStore store(InternalFS, "");
#elif defined(ESP32)
  SPIFFS.begin(true);
  fs = &SPIFFS;
  IdentityStore store(SPIFFS, "/identity");
#elif defined(RP2040_PLATFORM)
  LittleFS.begin();
  fs = &LittleFS;
  IdentityStore store(LittleFS, "/identity");
  store.begin();
#else
  #error "need to define filesystem"
#endif
  if (!store.load("_main", the_mesh.self_id)) {
    MESH_DEBUG_PRINTLN("Generating new keypair");
    the_mesh.self_id = radio_new_identity();   // create new random identity
    int count = 0;
    while (count < 10 && (the_mesh.self_id.pub_key[0] == 0x00 || the_mesh.self_id.pub_key[0] == 0xFF)) {  // reserved id hashes
      the_mesh.self_id = radio_new_identity(); count++;
    }
    store.save("_main", the_mesh.self_id);
  }

  Serial.print("Repeater ID: ");
  mesh::Utils::printHex(Serial, the_mesh.self_id.pub_key, PUB_KEY_SIZE); Serial.println();

  command[0] = 0;

  sensors.begin();

  the_mesh.begin(fs);

  // ---------------------------------------------------------------------------
  // SD card init — needed for MQTT config on devices with SD slots.
  // T-Deck Pro: SD shares display SPI bus (HSPI via displaySpi)
  // T5S3: SD shares LoRa SPI bus (SCK=14, MOSI=13, MISO=21)
  // Heltec V4 and others without SD: config lives in SPIFFS (already init'd)
  // ---------------------------------------------------------------------------
#if (defined(HAS_4G_MODEM) || defined(MECK_WIFI_REMOTE)) && (defined(HAS_SDCARD) || defined(SDCARD_CS))
  {
    // Deselect all SPI devices before SD init to prevent bus contention
    #ifdef SDCARD_CS
      pinMode(SDCARD_CS, OUTPUT);
      digitalWrite(SDCARD_CS, HIGH);
    #endif
    #ifdef PIN_DISPLAY_CS
      pinMode(PIN_DISPLAY_CS, OUTPUT);
      digitalWrite(PIN_DISPLAY_CS, HIGH);
    #endif
    #ifdef P_LORA_NSS
      pinMode(P_LORA_NSS, OUTPUT);
      digitalWrite(P_LORA_NSS, HIGH);
    #endif
    delay(100);

    for (int i = 0; i < 3; i++) {
      #if defined(LilyGo_T5S3_EPaper_Pro)
      // T5S3: SD shares LoRa SPI bus — create local HSPI reference
      static SPIClass sdSpi(HSPI);
      sdSpi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, SDCARD_CS);
      if (SD.begin(SDCARD_CS, sdSpi, 4000000)) { sdCardReady = true; break; }
      #elif defined(SDCARD_CS)
      extern SPIClass displaySpi;
      if (SD.begin(SDCARD_CS, displaySpi)) { sdCardReady = true; break; }
      #else
      if (SD.begin(SPI_CS)) { sdCardReady = true; break; }
      #endif
      delay(200);
    }
    Serial.printf("SD card: %s\n", sdCardReady ? "ready" : "FAILED");
  }
#endif

  // Start MQTT backhaul
#ifdef HAS_4G_MODEM
  if (sdCardReady) {
    cellularMQTT.begin();
    Serial.println("Cellular MQTT starting...");
  } else {
    Serial.println("Cellular MQTT skipped — no SD card for config");
  }
#endif

#ifdef MECK_WIFI_REMOTE
  #if defined(HAS_SDCARD) || defined(SDCARD_CS)
  if (sdCardReady) {
    wifiMQTT.begin();
    Serial.println("WiFi MQTT starting...");
  } else {
    Serial.println("WiFi MQTT skipped — no SD card for config");
  }
  #else
  // No SD card slot — config lives in SPIFFS (already initialized above)
  wifiMQTT.begin();
  Serial.println("WiFi MQTT starting (SPIFFS config)...");
  #endif
#endif

#ifdef DISPLAY_CLASS
  ui_task.begin(the_mesh.getNodePrefs(), FIRMWARE_BUILD_DATE, FIRMWARE_VERSION);
#endif

  // send out initial Advertisement to the mesh
  the_mesh.sendSelfAdvertisement(16000);
}

void loop() {
  int len = strlen(command);
  while (Serial.available() && len < sizeof(command)-1) {
    char c = Serial.read();
    if (c != '\n') {
      command[len++] = c;
      command[len] = 0;
      Serial.print(c);
    }
    if (c == '\r') break;
  }
  if (len == sizeof(command)-1) {  // command buffer full
    command[sizeof(command)-1] = '\r';
  }

  if (len > 0 && command[len - 1] == '\r') {  // received complete line
    Serial.print('\n');
    command[len - 1] = 0;  // replace newline with C string null terminator
    char reply[160];
    the_mesh.handleCommand(0, command, reply);  // NOTE: there is no sender_timestamp via serial!
    if (reply[0]) {
      Serial.print("  -> "); Serial.println(reply);
    }

    command[0] = 0;  // reset command buffer
  }

  // ---------------------------------------------------------------------------
  // MQTT → CLI bridge: process incoming commands from MQTT (cellular)
  // ---------------------------------------------------------------------------
#ifdef HAS_4G_MODEM
  {
    MQTTCommand mqttCmd;
    while (cellularMQTT.recvCommand(mqttCmd)) {
      Serial.printf("[MQTT] CLI: %s\n", mqttCmd.cmd);
      char reply[512];
      reply[0] = '\0';
      the_mesh.handleCommand((uint32_t)time(nullptr), mqttCmd.cmd, reply);

      if (reply[0] == '\0') strcpy(reply, "OK");

      cellularMQTT.sendResponse(cellularMQTT.getRspTopic(), reply);
      Serial.printf("[MQTT] Reply: %.80s\n", reply);
    }
  }

  // Periodic telemetry snapshot for cellular MQTT
  {
    static unsigned long lastTelemUpdate = 0;
    if (millis() - lastTelemUpdate > 10000) {
      NodePrefs* p = the_mesh.getNodePrefs();
      TelemetryData td;
      memset(&td, 0, sizeof(td));
      td.uptime_secs = millis() / 1000;
      td.battery_mv = board.getBattMilliVolts();
#ifdef HAS_BQ27220
      td.battery_pct = board.getBatteryPercent();
      td.temperature = board.getBattTemperature();
#else
      td.battery_pct = 0;
      td.temperature = 0;
#endif
      td.csq = cellularMQTT.getCSQ();
      td.freq = p->freq;
      td.bw = p->bw;
      td.sf = p->sf;
      td.cr = p->cr;
      td.tx_power = p->tx_power_dbm;
      strncpy(td.node_name, p->node_name, sizeof(td.node_name) - 1);
      strncpy(td.apn, cellularMQTT.getAPN(), sizeof(td.apn) - 1);
      strncpy(td.oper, cellularMQTT.getOperator(), sizeof(td.oper) - 1);
      td.mqtt_connected = cellularMQTT.isConnected();
      td.neighbor_count = 0;  // TODO: expose from MyMesh
      td.loop_detect = p->loop_detect;
      td.path_hash_mode = p->path_hash_mode;
      td.flood_max = p->flood_max;

      cellularMQTT.updateTelemetry(td);
      lastTelemUpdate = millis();
    }
  }
#endif

  // ---------------------------------------------------------------------------
  // MQTT → CLI bridge: process incoming commands from MQTT (WiFi)
  // ---------------------------------------------------------------------------
#ifdef MECK_WIFI_REMOTE
  wifiMQTT.loop();

  {
    MQTTCommand mqttCmd;
    while (wifiMQTT.recvCommand(mqttCmd)) {
      Serial.printf("[MQTT] CLI: %s\n", mqttCmd.cmd);
      char reply[512];
      reply[0] = '\0';
      the_mesh.handleCommand((uint32_t)time(nullptr), mqttCmd.cmd, reply);

      if (reply[0] == '\0') strcpy(reply, "OK");

      wifiMQTT.sendResponse(wifiMQTT.getRspTopic(), reply);
      Serial.printf("[MQTT] Reply: %.80s\n", reply);
    }
  }

  // Periodic telemetry snapshot for WiFi MQTT
  {
    static unsigned long lastTelemUpdate = 0;
    if (millis() - lastTelemUpdate > 10000) {
      NodePrefs* p = the_mesh.getNodePrefs();
      TelemetryData td;
      memset(&td, 0, sizeof(td));
      td.uptime_secs = millis() / 1000;
      td.battery_mv = board.getBattMilliVolts();
#ifdef HAS_BQ27220
      td.battery_pct = board.getBatteryPercent();
      td.temperature = board.getBattTemperature();
#else
      td.battery_pct = 0;
      td.temperature = 0;
#endif
      td.rssi = wifiMQTT.getRSSI();
      td.freq = p->freq;
      td.bw = p->bw;
      td.sf = p->sf;
      td.cr = p->cr;
      td.tx_power = p->tx_power_dbm;
      strncpy(td.node_name, p->node_name, sizeof(td.node_name) - 1);
      td.mqtt_connected = wifiMQTT.isConnected();
      td.neighbor_count = 0;
      td.loop_detect = p->loop_detect;
      td.path_hash_mode = p->path_hash_mode;
      td.flood_max = p->flood_max;

      wifiMQTT.updateTelemetry(td);
      lastTelemUpdate = millis();
    }
  }
#endif

  the_mesh.loop();
  sensors.loop();
#ifdef DISPLAY_CLASS
  ui_task.loop();
#endif
  rtc_clock.tick();

#if !defined(HAS_4G_MODEM) && !defined(MECK_WIFI_REMOTE)
  if (the_mesh.getNodePrefs()->powersaving_enabled &&
      the_mesh.millisHasNowPassed(lastActive + nextSleepinSecs * 1000)) {
    if (!the_mesh.hasPendingWork()) {
      board.sleep(1800);
      lastActive = millis();
      nextSleepinSecs = 5;
    } else {
      nextSleepinSecs += 5;
    }
  }
#endif
}