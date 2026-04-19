#include <Arduino.h>
#include "variant.h"
#include "TDeckProMaxBoard.h"
#include <Mesh.h>  // For MESH_DEBUG_PRINTLN

// LEDC channel for e-ink backlight PWM (Arduino ESP32 core 2.x channel-based API)
#ifdef PIN_EINK_BL
  #define EINK_BL_LEDC_CHANNEL 0
#endif

// =============================================================================
// TDeckProMaxBoard::begin() — Boot sequence for T-Deck Pro MAX V0.1
//
// Critical ordering:
//   1. I2C bus init (XL9555, BQ27220, and all sensors share this bus)
//   2. XL9555 init (must be up before ANY peripheral that depends on it)
//   3. Touch reset pulse via XL9555 (needed before touch driver init)
//   4. Keyboard reset pulse via XL9555 (clean keyboard state)
//   5. LoRa power enable via XL9555 (must be on before SPI radio init)
//   6. GPS power + UART init
//   7. Parent class init (ESP32Board::begin)
//   8. LoRa SPI pin config + deep sleep wake handling
//   9. BQ27220 fuel gauge check
//  10. Low-voltage protection
//
// NOTE: We do NOT call TDeckBoard::begin() — we reimplement the boot sequence
// to handle XL9555-routed pins. BQ27220 methods are inherited unchanged.
// =============================================================================

void TDeckProMaxBoard::begin() {

  MESH_DEBUG_PRINTLN("TDeckProMaxBoard::begin() - T-Deck Pro MAX V0.1");

  // ------ Step 1: I2C bus ------
  // All I2C devices (XL9555, BQ27220, TCA8418, CST328, DRV2605, ES8311,
  // BQ25896, BHI260AP) share SDA=13, SCL=14.
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);  // 100kHz — safe for all devices on the bus
  MESH_DEBUG_PRINTLN("  I2C initialized (SDA=%d SCL=%d)", I2C_SDA, I2C_SCL);

  // ------ Step 2: XL9555 I/O Expander ------
  // This must happen before anything that needs peripheral power or resets.
  if (!xl9555_init()) {
    Serial.println("CRITICAL: XL9555 init failed — peripherals will not work!");
    // Continue anyway; some things (display, keyboard INT) might still work
    // without XL9555, but LoRa/GPS/modem will be dead.
  }

  // ------ Step 3: Touch reset pulse ------
  // The touch controller (CST328) needs a clean reset via XL9555 IO07
  // before the touch driver tries to communicate with it.
  touchReset();

  // ------ Step 4: Keyboard reset pulse ------
  keyboardReset();

  // ------ Step 5: Parent class init ------
  // ESP32Board::begin() handles common ESP32 setup.
  // We skip TDeckBoard::begin() because it uses PIN_PERF_POWERON and
  // direct GPIO for LoRa/GPS power that don't exist on MAX.
  ESP32Board::begin();

  // ------ Step 6: GPS UART init ------
  // GPS power was already enabled by XL9555 boot defaults (GPS_EN HIGH).
  // Now init the UART with the MAX-specific pins.
  #if HAS_GPS
    Serial2.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    MESH_DEBUG_PRINTLN("  GPS Serial2 initialized (RX=%d TX=%d @ %d baud)",
                       GPS_RX_PIN, GPS_TX_PIN, GPS_BAUDRATE);
  #endif

  // ------ Step 7: Configure user button ------
  pinMode(PIN_USER_BTN, INPUT);

  // ------ Step 8: Configure LoRa SPI pins ------
  // LoRa power is already enabled via XL9555 (LORA_EN HIGH in boot defaults).
  pinMode(P_LORA_MISO, INPUT_PULLUP);

  // ------ Step 9: Handle wake from deep sleep ------
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    uint64_t wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1ULL << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }

  // ------ Step 10: BQ27220 fuel gauge ------
  #if HAS_BQ27220
    uint16_t voltage = getBattMilliVolts();
    MESH_DEBUG_PRINTLN("  Battery voltage: %d mV", voltage);
    configureFuelGauge();   // Inherited from TDeckBoard — sets 1500 mAh
  #endif

  // ------ Step 11: Early low-voltage protection ------
  #if HAS_BQ27220 && defined(AUTO_SHUTDOWN_MILLIVOLTS)
  {
    uint16_t bootMv = getBattMilliVolts();
    if (bootMv > 0 && bootMv < AUTO_SHUTDOWN_MILLIVOLTS) {
      Serial.printf("CRITICAL: Boot voltage %dmV < %dmV — sleeping immediately\n",
                    bootMv, AUTO_SHUTDOWN_MILLIVOLTS);
      esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
      esp_sleep_enable_ext1_wakeup(1ULL << PIN_USER_BTN, ESP_EXT1_WAKEUP_ANY_HIGH);
      esp_deep_sleep_start();
    }
  }
  #endif

  // ------ Step 12: E-ink backlight (working on MAX!) ------
  // Configure LEDC PWM for backlight brightness control.
  // Start with backlight OFF — UI code can enable it when needed.
  #ifdef PIN_EINK_BL
    // Arduino ESP32 core 2.x uses channel-based LEDC API
    ledcSetup(EINK_BL_LEDC_CHANNEL, 1000, 8);  // Channel 0, 1kHz, 8-bit resolution
    ledcAttachPin(PIN_EINK_BL, EINK_BL_LEDC_CHANNEL);
    ledcWrite(EINK_BL_LEDC_CHANNEL, 0);        // Off by default
    MESH_DEBUG_PRINTLN("  Backlight PWM configured on IO%d", PIN_EINK_BL);
  #endif

  MESH_DEBUG_PRINTLN("TDeckProMaxBoard::begin() - complete");
}


// =============================================================================
// XL9555 I/O Expander — Lightweight I2C Driver
// =============================================================================

bool TDeckProMaxBoard::xl9555_writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(I2C_ADDR_XL9555);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

uint8_t TDeckProMaxBoard::xl9555_readReg(uint8_t reg) {
  Wire.beginTransmission(I2C_ADDR_XL9555);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((uint8_t)I2C_ADDR_XL9555, (uint8_t)1);
  return Wire.available() ? Wire.read() : 0xFF;
}

bool TDeckProMaxBoard::xl9555_init() {
  MESH_DEBUG_PRINTLN("  XL9555: Initializing I/O expander at 0x%02X", I2C_ADDR_XL9555);

  // Verify XL9555 is present on the bus
  Wire.beginTransmission(I2C_ADDR_XL9555);
  if (Wire.endTransmission() != 0) {
    Serial.println("  XL9555: NOT FOUND on I2C bus!");
    _xlReady = false;
    return false;
  }

  // Set ALL pins as outputs (config register: 0 = output)
  // Port 0 (pins 0-7): all output
  if (!xl9555_writeReg(XL9555_REG_CONFIG_0, 0x00)) return false;
  // Port 1 (pins 8-15): all output
  if (!xl9555_writeReg(XL9555_REG_CONFIG_1, 0x00)) return false;

  // Apply boot defaults
  _xlPort0 = XL9555_BOOT_PORT0;
  _xlPort1 = XL9555_BOOT_PORT1;
  if (!xl9555_writeReg(XL9555_REG_OUTPUT_0, _xlPort0)) return false;
  if (!xl9555_writeReg(XL9555_REG_OUTPUT_1, _xlPort1)) return false;

  _xlReady = true;

  MESH_DEBUG_PRINTLN("  XL9555: Ready (Port0=0x%02X Port1=0x%02X)", _xlPort0, _xlPort1);
  MESH_DEBUG_PRINTLN("  XL9555: LoRa=%s GPS=%s 1V8=%s Modem=%s Antenna=%s",
                     (_xlPort0 & (1 << XL_PIN_LORA_EN)) ? "ON" : "OFF",
                     (_xlPort0 & (1 << XL_PIN_GPS_EN)) ? "ON" : "OFF",
                     (_xlPort0 & (1 << XL_PIN_1V8_EN)) ? "ON" : "OFF",
                     (_xlPort0 & (1 << XL_PIN_6609_EN)) ? "ON" : "OFF",
                     (_xlPort0 & (1 << XL_PIN_LORA_SEL)) ? "internal" : "external");

  return true;
}

void TDeckProMaxBoard::xl9555_digitalWrite(uint8_t pin, bool value) {
  if (!_xlReady) return;

  if (pin < 8) {
    // Port 0
    if (value) _xlPort0 |= (1 << pin);
    else       _xlPort0 &= ~(1 << pin);
    xl9555_writeReg(XL9555_REG_OUTPUT_0, _xlPort0);
  } else if (pin < 16) {
    // Port 1 (subtract 8 for bit position)
    uint8_t bit = pin - 8;
    if (value) _xlPort1 |= (1 << bit);
    else       _xlPort1 &= ~(1 << bit);
    xl9555_writeReg(XL9555_REG_OUTPUT_1, _xlPort1);
  }
}

bool TDeckProMaxBoard::xl9555_digitalRead(uint8_t pin) const {
  if (pin < 8) return (_xlPort0 >> pin) & 1;
  if (pin < 16) return (_xlPort1 >> (pin - 8)) & 1;
  return false;
}

void TDeckProMaxBoard::xl9555_writePort0(uint8_t val) {
  _xlPort0 = val;
  if (_xlReady) xl9555_writeReg(XL9555_REG_OUTPUT_0, val);
}

void TDeckProMaxBoard::xl9555_writePort1(uint8_t val) {
  _xlPort1 = val;
  if (_xlReady) xl9555_writeReg(XL9555_REG_OUTPUT_1, val);
}


// =============================================================================
// High-level peripheral control
// =============================================================================

// ---- Modem (A7682E) ----

void TDeckProMaxBoard::modemPowerOn() {
  MESH_DEBUG_PRINTLN("  XL9555: Modem power ON (6609_EN HIGH)");
  xl9555_digitalWrite(XL_PIN_6609_EN, HIGH);
  delay(100);  // Allow SGM6609 boost to stabilise
}

void TDeckProMaxBoard::modemPowerOff() {
  MESH_DEBUG_PRINTLN("  XL9555: Modem power OFF (6609_EN LOW)");
  xl9555_digitalWrite(XL_PIN_6609_EN, LOW);
}

void TDeckProMaxBoard::modemPwrkeyPulse() {
  // A7682E power-on sequence: pulse PWRKEY LOW for >= 500ms
  // (Some datasheets say pull HIGH then LOW; LilyGo factory sets HIGH then toggles.)
  MESH_DEBUG_PRINTLN("  XL9555: Modem PWRKEY pulse");
  xl9555_digitalWrite(XL_PIN_PWRKEY_EN, HIGH);
  delay(100);
  xl9555_digitalWrite(XL_PIN_PWRKEY_EN, LOW);
  delay(1200);
  xl9555_digitalWrite(XL_PIN_PWRKEY_EN, HIGH);
}

// ---- Audio output selection ----

void TDeckProMaxBoard::selectAudioES8311() {
  MESH_DEBUG_PRINTLN("  XL9555: Audio select → ES8311");
  xl9555_digitalWrite(XL_PIN_AUDIO_SEL, LOW);
}

void TDeckProMaxBoard::selectAudioModem() {
  MESH_DEBUG_PRINTLN("  XL9555: Audio select → A7682E");
  xl9555_digitalWrite(XL_PIN_AUDIO_SEL, HIGH);
}

void TDeckProMaxBoard::amplifierEnable() {
  xl9555_digitalWrite(XL_PIN_AMPLIFIER, HIGH);
}

void TDeckProMaxBoard::amplifierDisable() {
  xl9555_digitalWrite(XL_PIN_AMPLIFIER, LOW);
}

// ---- LoRa antenna selection ----

void TDeckProMaxBoard::loraAntennaInternal() {
  MESH_DEBUG_PRINTLN("  XL9555: LoRa antenna → internal");
  xl9555_digitalWrite(XL_PIN_LORA_SEL, HIGH);
}

void TDeckProMaxBoard::loraAntennaExternal() {
  MESH_DEBUG_PRINTLN("  XL9555: LoRa antenna → external");
  xl9555_digitalWrite(XL_PIN_LORA_SEL, LOW);
}

// ---- Motor (DRV2605) ----

void TDeckProMaxBoard::motorEnable() {
  xl9555_digitalWrite(XL_PIN_MOTOR_EN, HIGH);
}

void TDeckProMaxBoard::motorDisable() {
  xl9555_digitalWrite(XL_PIN_MOTOR_EN, LOW);
}

// ---- Touch reset ----

void TDeckProMaxBoard::touchReset() {
  if (!_xlReady) return;
  MESH_DEBUG_PRINTLN("  XL9555: Touch reset pulse");
  xl9555_digitalWrite(XL_PIN_TOUCH_RST, LOW);
  delay(20);
  xl9555_digitalWrite(XL_PIN_TOUCH_RST, HIGH);
  delay(50);  // Allow touch controller to come out of reset
}

// ---- Keyboard reset ----

void TDeckProMaxBoard::keyboardReset() {
  if (!_xlReady) return;
  MESH_DEBUG_PRINTLN("  XL9555: Keyboard reset pulse");
  xl9555_digitalWrite(XL_PIN_KEY_RST, LOW);
  delay(20);
  xl9555_digitalWrite(XL_PIN_KEY_RST, HIGH);
  delay(50);
}

// ---- GPS power ----

void TDeckProMaxBoard::gpsPowerOn() {
  xl9555_digitalWrite(XL_PIN_GPS_EN, HIGH);
  delay(100);
}

void TDeckProMaxBoard::gpsPowerOff() {
  xl9555_digitalWrite(XL_PIN_GPS_EN, LOW);
}

// ---- LoRa power ----

void TDeckProMaxBoard::loraPowerOn() {
  xl9555_digitalWrite(XL_PIN_LORA_EN, HIGH);
  delay(10);
}

void TDeckProMaxBoard::loraPowerOff() {
  xl9555_digitalWrite(XL_PIN_LORA_EN, LOW);
}

// ---- E-ink backlight (working on MAX!) ----

void TDeckProMaxBoard::backlightOn() {
  #ifdef PIN_EINK_BL
    ledcWrite(EINK_BL_LEDC_CHANNEL, 255);
  #endif
}

void TDeckProMaxBoard::backlightOff() {
  #ifdef PIN_EINK_BL
    ledcWrite(EINK_BL_LEDC_CHANNEL, 0);
  #endif
}

void TDeckProMaxBoard::backlightSetBrightness(uint8_t duty) {
  #ifdef PIN_EINK_BL
    ledcWrite(EINK_BL_LEDC_CHANNEL, duty);
  #endif
}
