// =============================================================================
// MeshCore target implementation for LilyGo T-Echo Card
//
// nRF52840 + SX1262 (HPB16B3 module) + SSD1315 OLED + L76K GPS
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
  // Enable OLED power
  #if PIN_OLED_EN >= 0
    pinMode(PIN_OLED_EN, OUTPUT);
    digitalWrite(PIN_OLED_EN, HIGH);
    delay(10);
  #endif

  // Enable GPS power
  #if defined(HAS_GPS) && PIN_GPS_EN >= 0
    pinMode(PIN_GPS_EN, OUTPUT);
    digitalWrite(PIN_GPS_EN, HIGH);
    delay(10);
  #endif

  // GPS RF/LNA enable
  #if defined(HAS_GPS) && PIN_GPS_RF_EN >= 0
    pinMode(PIN_GPS_RF_EN, OUTPUT);
    digitalWrite(PIN_GPS_RF_EN, HIGH);
  #endif

  // Initialize GPS UART
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

  // Initialize I2C
  Wire.setPins(I2C_SDA, I2C_SCL);
  Wire.begin();
  Wire.setClock(400000);

  // Initialize display
  #ifdef DISPLAY_CLASS
    display.begin(OLED_I2C_ADDR);
  #endif

  // Board-level init
  board.begin();

  // Initialize LoRa radio
  // SX1262 DIO2 as RF switch control (common for HPB16B3 modules)
  radio_driver.setDio2AsRfSwitch(true);
}
