// =============================================================================
// MeshCore target implementation for LilyGo T-Echo Card
//
// nRF52840 + SX1262 (HPB16B3 / S62F module) + SSD1315 OLED + L76K GPS
//
// Patches applied from Meshtastic PR #10267 (caveman99):
//   - RT9080 power rail reset cycle (handled in board.begin())
//   - SSD1315 OLED display offset for 72×40 panel
//   - SX1262 TCXO voltage via DIO3
// =============================================================================

#include "target.h"
#include "variant.h"

// --- SPI for LoRa radio (software SPI on nRF52) ---
// The HPB16B3 SX1262 module uses dedicated SPI pins, not shared with anything else.
// RadioLib Module handles the SPI internally when given pin numbers.
static Module radio_module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);

// --- Radio driver ---
RADIO_CLASS radio_driver(&radio_module);
WRAPPER_CLASS radio_wrapper(radio_driver);

// --- Board ---
TechoCardBoard board;

// --- Display (SSD1306-compatible, SSD1315 at 0x3C, 72x40) ---
#ifdef DISPLAY_CLASS
DISPLAY_CLASS display(OLED_WIDTH, OLED_HEIGHT);
#endif

// --- MeshCore stores ---
mesh::IdentityStore identity_store;
mesh::NodePrefs node_prefs;
mesh::DataStore data_store;

// --- Sensor manager ---
#if defined(ENV_INCLUDE_GPS) && ENV_INCLUDE_GPS
  #include <helpers/sensors/MicroNMEALocationProvider.h>
  static MicroNMEALocationProvider gps_provider(Serial1);
  EnvironmentSensorManager sensor_manager(gps_provider);
#else
  EnvironmentSensorManager sensor_manager;
#endif

// --- Target initialization ---
void target_setup() {
  // -------------------------------------------------------------------------
  // Board-level init FIRST — this cycles the RT9080 3V3 rail and parks
  // peripheral enable pins LOW. Must happen before any other peripheral
  // access to prevent brown-out on LoRa TX.
  // -------------------------------------------------------------------------
  board.begin();

  // -------------------------------------------------------------------------
  // Enable GPS power (was parked LOW in board.begin())
  // -------------------------------------------------------------------------
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    digitalWrite(PIN_GPS_EN, HIGH);
    delay(10);
  #endif

  // GPS RF/LNA enable
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    digitalWrite(PIN_GPS_RF_EN, HIGH);
  #endif

  // Initialise GPS UART
  #if defined(HAS_GPS)
    Serial1.setPins(PIN_GPS_RX, PIN_GPS_TX);
    Serial1.begin(GPS_BAUDRATE);
  #endif

  // Speaker off by default (save power)
  #if defined(HAS_SPEAKER)
    pinMode(PIN_SPK_EN, OUTPUT);
    digitalWrite(PIN_SPK_EN, LOW);
    #if PIN_SPK_EN2 >= 0
      pinMode(PIN_SPK_EN2, OUTPUT);
      digitalWrite(PIN_SPK_EN2, LOW);
    #endif
  #endif

  // Initialise I2C
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin();
  Wire.setClock(400000);

  // -------------------------------------------------------------------------
  // Initialise display
  //
  // The SSD1315 has a 128×64 GDDRAM but the physical panel is 72×40,
  // hardwired at columns 28–99, pages 3–7 (rows 24–63).
  //
  // After the driver's standard init, we send SETDISPLAYOFFSET = 24 to
  // shift the COM output mapping so that data written to pages 0–4
  // appears on the physical display.
  //
  // If content is still blank or shifted when hardware arrives, the
  // alternative is to modify the PAGEADDR writes in the display() method
  // to target pages 3–7 directly (as Meshtastic does with setYOffset(3)).
  // -------------------------------------------------------------------------
  #ifdef DISPLAY_CLASS
    display.begin(OLED_I2C_ADDR);

    #if defined(OLED_DISPLAY_OFFSET) && OLED_DISPLAY_OFFSET > 0
      // SSD1306/SSD1315 command 0xD3: Set Display Offset
      // Shifts COM output by OLED_DISPLAY_OFFSET rows (24 = 3 pages)
      display.sendCommand(0xD3);              // SETDISPLAYOFFSET
      display.sendCommand(OLED_DISPLAY_OFFSET);

      // SSD1306/SSD1315 command 0xA8: Set Multiplex Ratio
      // Must match actual panel height (40 - 1 = 39)
      display.sendCommand(0xA8);              // SETMULTIPLEX
      display.sendCommand(OLED_HEIGHT - 1);   // 39
    #endif
  #endif

  // -------------------------------------------------------------------------
  // Initialise LoRa radio
  // -------------------------------------------------------------------------
  // SX1262 DIO2 as RF switch control (common for HPB16B3 modules)
  radio_driver.setDio2AsRfSwitch(true);

  // SX1262 TCXO via DIO3
  // Meshtastic PR #10267 sets this to 1.8V. If the module has a TCXO
  // powered by DIO3, this must be configured before any frequency setting
  // or the oscillator won't start, causing frequency drift or init failure.
  // TODO: Verify on hardware. If the module uses a crystal (not TCXO),
  // remove this call — it would waste current on DIO3.
  #if defined(SX126X_DIO3_TCXO_VOLTAGE)
    radio_driver.setTCXO(SX126X_DIO3_TCXO_VOLTAGE);
  #endif
}
