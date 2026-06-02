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
// NOTE: We do NOT call any parent board begin() beyond ESP32Board::begin();
// the boot sequence is reimplemented here to handle XL9555-routed pins.
// The BQ27220 fuel-gauge methods are defined in this file (MAX is standalone,
// no longer inheriting TDeckBoard).
// =============================================================================

void TDeckProMaxBoard::begin() {

  MESH_DEBUG_PRINTLN("TDeckProMaxBoard::begin() - T-Deck Pro MAX V0.1");

  // ------ Step 1: I2C bus ------
  // All I2C devices (XL9555, BQ27220, TCA8418, CST328, DRV2605, ES8311,
  // BQ25896, BHI260AP) share SDA=13, SCL=14.
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);  // 100kHz — safe for all devices on the bus
  // --- TEMP: charger chip probe (BQ25896 @ 0x6B vs SY6970 @ 0x6A) ---
for (uint8_t a = 0x6A; a <= 0x6B; a++) {
  Wire.beginTransmission(a);
  uint8_t e = Wire.endTransmission();
  Serial.printf("Charger probe 0x%02X -> %s\n", a,
                e == 0 ? (a == 0x6A ? "ACK (SY6970)" : "ACK (BQ25896)") : "no response");
}
  MESH_DEBUG_PRINTLN("  I2C initialized (SDA=%d SCL=%d)", I2C_SDA, I2C_SCL);

  // ------ Step 2: XL9555 I/O Expander ------
  // This must happen before anything that needs peripheral power or resets.
  if (!xl9555_init()) {
    Serial.println("CRITICAL: XL9555 init failed — peripherals will not work!");
    // Continue anyway; some things (display, keyboard INT) might still work
    // without XL9555, but LoRa/GPS/modem will be dead.
  }

  // Configure the e-ink frontlight pin (IO41) as an output, held LOW so the
  // panel starts dark at boot. Lit only by backlightOn() (Alt+B).
#ifdef PIN_EINK_BL
  pinMode(PIN_EINK_BL, OUTPUT);
  digitalWrite(PIN_EINK_BL, LOW);
#endif

  // ------ Step 3: Touch reset pulse ------
  // The touch controller (CST328) needs a clean reset via XL9555 IO07
  // before the touch driver tries to communicate with it.
  touchReset();

  // ------ Step 4: Keyboard reset pulse ------
  keyboardReset();

  // ------ Step 5: Parent class init ------
  // ESP32Board::begin() handles common ESP32 setup. The MAX reimplements its
  // own boot sequence above for XL9555-routed power/reset, rather than using a
  // Pro-style direct-GPIO begin().
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
    configureFuelGauge();   // sets 1500 mAh (MAX design capacity)
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

  // ------ Step 12: E-ink backlight ------
  // No-op: IO41 was already configured as an output and held LOW earlier in
  // begin(). The frontlight is lit only by backlightOn() (Alt+B).

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
    analogWrite(PIN_EINK_BL, 1);
  #endif
  _backlightOn = true;
}

void TDeckProMaxBoard::backlightOff() {
  #ifdef PIN_EINK_BL
    analogWrite(PIN_EINK_BL, 0);
  #endif
  _backlightOn = false;
}

void TDeckProMaxBoard::backlightSetBrightness(uint8_t duty) {
  #ifdef PIN_EINK_BL
    analogWrite(PIN_EINK_BL, duty);
  #endif
  _backlightOn = (duty > 0);
}

bool TDeckProMaxBoard::isBacklightOn() const {
  return _backlightOn;
}


// =============================================================================
// BQ27220 Fuel Gauge
//
// Moved verbatim from TDeckBoard.cpp when the MAX board was decoupled from the
// Pro board class. The BQ27220 is identical hardware on both boards; only the
// class name differs. The three bq27220_* helpers are file-static (one copy
// per translation unit), so this file carries its own.
// =============================================================================

uint16_t TDeckProMaxBoard::getBattMilliVolts() {
  #if HAS_BQ27220
    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(BQ27220_REG_VOLTAGE);
    if (Wire.endTransmission(false) != 0) {
      MESH_DEBUG_PRINTLN("BQ27220: I2C error reading voltage");
      return 0;
    }

    uint8_t count = Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2);
    if (count != 2) {
      MESH_DEBUG_PRINTLN("BQ27220: Read error - wrong byte count");
      return 0;
    }

    uint16_t voltage = Wire.read();
    voltage |= (Wire.read() << 8);
    return voltage;
  #else
    return 0;
  #endif
}

uint8_t TDeckProMaxBoard::getBatteryPercent() {
  #if HAS_BQ27220
    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(BQ27220_REG_SOC);
    if (Wire.endTransmission(false) != 0) {
      return 0;
    }

    uint8_t count = Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2);
    if (count != 2) {
      return 0;
    }

    uint16_t soc = Wire.read();
    soc |= (Wire.read() << 8);
    return (uint8_t)min(soc, (uint16_t)100);
  #else
    return 0;
  #endif
}

// ---- BQ27220 extended register helpers ----

#if HAS_BQ27220
// Read a 16-bit register from BQ27220. Returns 0 on I2C error.
static uint16_t bq27220_read16(uint8_t reg) {
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2) != 2) return 0;
  uint16_t val = Wire.read();
  val |= (Wire.read() << 8);
  return val;
}

// Read a single byte from BQ27220 register.
static uint8_t bq27220_read8(uint8_t reg) {
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

// Write a 16-bit subcommand to BQ27220 Control register (0x00).
// Subcommands control unsealing, config mode, sealing, etc.
static bool bq27220_writeControl(uint16_t subcmd) {
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x00);                    // Control register
  Wire.write(subcmd & 0xFF);           // LSB first
  Wire.write((subcmd >> 8) & 0xFF);    // MSB
  return Wire.endTransmission() == 0;
}
#endif

// ---- BQ27220 Design Capacity configuration ----
// The BQ27220 ships with a 3000 mAh default. The T-Deck Pro uses a 2000 mAh
// cell. This function checks on boot and writes the correct value via the
// MAC Data Memory interface if needed. The value persists in battery-backed
// RAM, so this typically only writes once (or after a full battery disconnect).
//
// Procedure follows TI TRM SLUUBD4A Section 6.1:
//   1. Unseal  ->  2. Full Access  ->  3. Enter CFG_UPDATE
//   4. Write Design Capacity via MAC  ->  5. Exit CFG_UPDATE  ->  6. Seal

bool TDeckProMaxBoard::configureFuelGauge(uint16_t designCapacity_mAh) {
#if HAS_BQ27220
  // Read current design capacity from standard command register
  uint16_t currentDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  Serial.printf("BQ27220: Design Capacity = %d mAh (target %d)\n", currentDC, designCapacity_mAh);

  if (currentDC == designCapacity_mAh) {
    // Design Capacity correct, but check if Full Charge Capacity is sane.
    uint16_t fcc = bq27220_read16(BQ27220_REG_FULL_CAP);
    Serial.printf("BQ27220: Design Capacity already correct, FCC=%d mAh\n", fcc);
    // Check if FCC is outside an acceptable band around design capacity.
    // Catches both: FCC too high (stale factory 3000mAh) and FCC too low
    // (gauge learned on a smaller battery, e.g. 1400mAh on a 2500mAh pack).
    uint16_t fccLo = (designCapacity_mAh > 100) ? designCapacity_mAh - 100 : 0;
    uint16_t fccHi = designCapacity_mAh + 100;
    if (fcc < fccLo || fcc > fccHi) {
      // FCC is >=150% of design — stale from factory defaults (typically 3000 mAh).
      uint16_t designEnergy = (uint16_t)((uint32_t)designCapacity_mAh * 37 / 10);
      Serial.printf("BQ27220: FCC %d outside target band [%d..%d], checking Design Energy (target %d mWh)\n",
                    fcc, fccLo, fccHi, designEnergy);

      // Unseal to read data memory and issue RESET
      bq27220_writeControl(0x0414); delay(2);
      bq27220_writeControl(0x3672); delay(2);
      // Full Access
      bq27220_writeControl(0xFFFF); delay(2);
      bq27220_writeControl(0xFFFF); delay(2);

      // Read current Design Energy from data memory to check if it needs writing
      // Enter CFG_UPDATE to access data memory
      bq27220_writeControl(0x0090);
      bool ready = false;
      for (int i = 0; i < 50; i++) {
        delay(20);
        uint16_t opSt = bq27220_read16(BQ27220_REG_OP_STATUS);
        if (opSt & 0x0400) { ready = true; break; }
      }
      if (ready) {
        // Read Design Energy at data memory address 0x92A1
        Wire.beginTransmission(BQ27220_I2C_ADDR);
        Wire.write(0x3E); Wire.write(0xA1); Wire.write(0x92);
        Wire.endTransmission();
        delay(10);
        uint8_t oldMSB = bq27220_read8(0x40);
        uint8_t oldLSB = bq27220_read8(0x41);
        uint16_t currentDE = (oldMSB << 8) | oldLSB;

        if (currentDE != designEnergy) {
          // Design Energy actually needs updating — write it
          uint8_t oldChk = bq27220_read8(0x60);
          uint8_t dLen   = bq27220_read8(0x61);
          uint8_t newMSB = (designEnergy >> 8) & 0xFF;
          uint8_t newLSB = designEnergy & 0xFF;
          uint8_t temp = (255 - oldChk - oldMSB - oldLSB);
          uint8_t newChk = 255 - ((temp + newMSB + newLSB) & 0xFF);

          Serial.printf("BQ27220: DE old=%d new=%d mWh, writing\n", currentDE, designEnergy);

          Wire.beginTransmission(BQ27220_I2C_ADDR);
          Wire.write(0x3E); Wire.write(0xA1); Wire.write(0x92);
          Wire.write(newMSB); Wire.write(newLSB);
          Wire.endTransmission();
          delay(5);
          Wire.beginTransmission(BQ27220_I2C_ADDR);
          Wire.write(0x60); Wire.write(newChk); Wire.write(dLen);
          Wire.endTransmission();
          delay(10);

          // Exit with reinit since we actually changed data
          bq27220_writeControl(0x0091);  // EXIT_CFG_UPDATE_REINIT
          delay(200);
          Serial.println("BQ27220: Design Energy written, exited CFG_UPDATE");
        } else {
          // DC=2000, DE=7400, Update Status=0x00, but FCC is stuck at 3000.
          // Diagnostic scan found the culprits:
          //   0x9106 = Qmax Cell 0 (IT Cfg class) — the raw capacity the
          //            gauge uses for FCC calculation. Factory default 3000.
          //   0x929D = Stored FCC reference (Gas Gauging class, 2 bytes
          //            before Design Capacity). Also stuck at 3000.
          //
          // Fix: overwrite both with designCapacity_mAh (2000).
          Serial.printf("BQ27220: DE correct (%d mWh) — fixing Qmax + stored FCC\n", currentDE);

          // --- Helper lambda for MAC data memory 2-byte write ---
          // Reads old value + checksum, computes differential checksum, writes new value.
          auto writeDM16 = [](uint16_t addr, uint16_t newVal) -> bool {
            // Select address
            Wire.beginTransmission(BQ27220_I2C_ADDR);
            Wire.write(0x3E);
            Wire.write(addr & 0xFF);
            Wire.write((addr >> 8) & 0xFF);
            Wire.endTransmission();
            delay(10);

            uint8_t oldMSB = bq27220_read8(0x40);
            uint8_t oldLSB = bq27220_read8(0x41);
            uint8_t oldChk = bq27220_read8(0x60);
            uint8_t dLen   = bq27220_read8(0x61);
            uint16_t oldVal = (oldMSB << 8) | oldLSB;

            if (oldVal == newVal) {
              Serial.printf("BQ27220:   [0x%04X] already %d, skip\n", addr, newVal);
              return true;  // already correct
            }

            uint8_t newMSB = (newVal >> 8) & 0xFF;
            uint8_t newLSB = newVal & 0xFF;
            uint8_t temp = (255 - oldChk - oldMSB - oldLSB);
            uint8_t newChk = 255 - ((temp + newMSB + newLSB) & 0xFF);

            Serial.printf("BQ27220:   [0x%04X] %d -> %d\n", addr, oldVal, newVal);

            // Write new value
            Wire.beginTransmission(BQ27220_I2C_ADDR);
            Wire.write(0x3E);
            Wire.write(addr & 0xFF);
            Wire.write((addr >> 8) & 0xFF);
            Wire.write(newMSB);
            Wire.write(newLSB);
            Wire.endTransmission();
            delay(5);

            // Write checksum
            Wire.beginTransmission(BQ27220_I2C_ADDR);
            Wire.write(0x60);
            Wire.write(newChk);
            Wire.write(dLen);
            Wire.endTransmission();
            delay(10);
            return true;
          };

          // Overwrite Qmax Cell 0 (IT Cfg) — this is what FCC is derived from
          writeDM16(0x9106, designCapacity_mAh);

          // Overwrite stored FCC reference (Gas Gauging, 2 bytes before DC)
          writeDM16(0x929D, designCapacity_mAh);

          // Exit with reinit to apply the new values
          bq27220_writeControl(0x0091);  // EXIT_CFG_UPDATE_REINIT
          delay(200);
          Serial.println("BQ27220: Qmax + stored FCC updated, exited CFG_UPDATE");
        }
      } else {
        Serial.println("BQ27220: Failed to enter CFG_UPDATE for DE check");
      }

      // Seal first, then issue RESET.
      // RESET forces the gauge to fully reinitialize its Impedance Track
      // algorithm and recalculate FCC from the current DC/DE values.
      // This is the actual fix when DC and DE are correct but FCC is stuck.
      bq27220_writeControl(0x0030);  // SEAL
      delay(5);
      Serial.println("BQ27220: Issuing RESET to force FCC recalculation...");
      bq27220_writeControl(0x0041);  // RESET
      delay(2000);  // Full reset needs generous settle time

      fcc = bq27220_read16(BQ27220_REG_FULL_CAP);
      Serial.printf("BQ27220: FCC after RESET: %d mAh (target <= %d)\n", fcc, designCapacity_mAh);

      if (fcc > designCapacity_mAh) {
        // RESET didn't fix FCC — the gauge IT algorithm is stubbornly
        // retaining its learned value. This typically resolves after one
        // full charge/discharge cycle. Software clamp in
        // getFullChargeCapacity() ensures correct display regardless.
        Serial.printf("BQ27220: FCC still stale at %d — software clamp active\n", fcc);
      }
    }
    return true;
  }

  Serial.printf("BQ27220: Updating Design Capacity from %d to %d mAh\n", currentDC, designCapacity_mAh);

  // Step 1: Unseal (default unseal keys)
  bq27220_writeControl(0x0414);
  delay(2);
  bq27220_writeControl(0x3672);
  delay(2);

  // Step 2: Enter Full Access mode
  bq27220_writeControl(0xFFFF);
  delay(2);
  bq27220_writeControl(0xFFFF);
  delay(2);

  // Step 3: Enter CFG_UPDATE mode
  bq27220_writeControl(0x0090);

  // Wait for CFGUPMODE bit (bit 10) in OperationStatus register
  bool cfgReady = false;
  for (int i = 0; i < 50; i++) {
    delay(20);
    uint16_t opStatus = bq27220_read16(BQ27220_REG_OP_STATUS);
    Serial.printf("BQ27220: OperationStatus = 0x%04X (attempt %d)\n", opStatus, i);
    if (opStatus & 0x0400) {  // CFGUPMODE is bit 10
      cfgReady = true;
      break;
    }
  }
  if (!cfgReady) {
    Serial.println("BQ27220: ERROR - Timeout waiting for CFGUPDATE mode");
    bq27220_writeControl(0x0092);  // Try to exit cleanly
    bq27220_writeControl(0x0030);  // Re-seal
    return false;
  }
  Serial.println("BQ27220: Entered CFGUPDATE mode");

  // Step 4: Write Design Capacity via MAC Data Memory interface
  // Design Capacity mAh lives at data memory address 0x929F

  // 4a. Select the data memory block by writing address to 0x3E-0x3F
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x3E);   // MACDataControl register
  Wire.write(0x9F);   // Address low byte
  Wire.write(0x92);   // Address high byte
  Wire.endTransmission();
  delay(10);

  // 4b. Read old data (MSB, LSB) and checksum for differential update
  uint8_t oldMSB     = bq27220_read8(0x40);
  uint8_t oldLSB     = bq27220_read8(0x41);
  uint8_t oldChksum  = bq27220_read8(0x60);
  uint8_t dataLen    = bq27220_read8(0x61);

  Serial.printf("BQ27220: Old DC bytes=0x%02X 0x%02X chk=0x%02X len=%d\n",
                     oldMSB, oldLSB, oldChksum, dataLen);

  // 4c. Compute new values (BQ27220 stores big-endian in data memory)
  uint8_t newMSB = (designCapacity_mAh >> 8) & 0xFF;
  uint8_t newLSB = designCapacity_mAh & 0xFF;

  // Differential checksum: remove old bytes, add new bytes
  uint8_t temp = (255 - oldChksum - oldMSB - oldLSB);
  uint8_t newChksum = 255 - ((temp + newMSB + newLSB) & 0xFF);

  Serial.printf("BQ27220: New DC bytes=0x%02X 0x%02X chk=0x%02X\n",
                     newMSB, newLSB, newChksum);

  // 4d. Write address + new data as a single block transaction
  //     BQ27220 MAC requires: [0x3E] [addr_lo] [addr_hi] [data...]
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x3E);    // Start at MACDataControl
  Wire.write(0x9F);    // Address low byte
  Wire.write(0x92);    // Address high byte
  Wire.write(newMSB);  // Data byte 0 (at 0x40)
  Wire.write(newLSB);  // Data byte 1 (at 0x41)
  uint8_t writeResult = Wire.endTransmission();
  Serial.printf("BQ27220: Write block result = %d\n", writeResult);

  // 4e. Write updated checksum and length
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x60);
  Wire.write(newChksum);
  Wire.write(dataLen);
  writeResult = Wire.endTransmission();
  Serial.printf("BQ27220: Write checksum result = %d\n", writeResult);
  delay(10);

  // 4f. Verify the write took effect before exiting config mode
  //     Re-read the block to confirm
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x3E);
  Wire.write(0x9F);
  Wire.write(0x92);
  Wire.endTransmission();
  delay(10);
  uint8_t verMSB = bq27220_read8(0x40);
  uint8_t verLSB = bq27220_read8(0x41);
  Serial.printf("BQ27220: Verify in CFGUPDATE: DC bytes=0x%02X 0x%02X (%d mAh)\n",
                     verMSB, verLSB, (verMSB << 8) | verLSB);

  // Step 4g: Also update Design Energy (address 0x92A1) while in CFG_UPDATE.
  // Design Energy = capacity x 3.7V (nominal LiPo voltage).
  // The gauge uses both DC and DE to compute Full Charge Capacity.
  {
    uint16_t designEnergy = (uint16_t)((uint32_t)designCapacity_mAh * 37 / 10);
    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(0x3E); Wire.write(0xA1); Wire.write(0x92);
    Wire.endTransmission();
    delay(10);
    uint8_t deOldMSB = bq27220_read8(0x40);
    uint8_t deOldLSB = bq27220_read8(0x41);
    uint8_t deOldChk = bq27220_read8(0x60);
    uint8_t deLen    = bq27220_read8(0x61);

    uint8_t deNewMSB = (designEnergy >> 8) & 0xFF;
    uint8_t deNewLSB = designEnergy & 0xFF;
    uint8_t deTemp = (255 - deOldChk - deOldMSB - deOldLSB);
    uint8_t deNewChk = 255 - ((deTemp + deNewMSB + deNewLSB) & 0xFF);

    Serial.printf("BQ27220: Design Energy: old=%d new=%d mWh\n",
                  (deOldMSB << 8) | deOldLSB, designEnergy);

    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(0x3E); Wire.write(0xA1); Wire.write(0x92);
    Wire.write(deNewMSB); Wire.write(deNewLSB);
    Wire.endTransmission();
    delay(5);
    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(0x60); Wire.write(deNewChk); Wire.write(deLen);
    Wire.endTransmission();
    delay(10);
  }

  // Step 5: Exit CFG_UPDATE (with reinit to apply changes immediately)
  bq27220_writeControl(0x0091);  // EXIT_CFG_UPDATE_REINIT
  Serial.println("BQ27220: Sent EXIT_CFG_UPDATE_REINIT, waiting...");
  delay(200);  // Allow gauge to reinitialize

  // Verify
  uint16_t verifyDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  Serial.printf("BQ27220: Design Capacity now reads %d mAh (expected %d)\n",
                     verifyDC, designCapacity_mAh);

  uint16_t newFCC = bq27220_read16(BQ27220_REG_FULL_CAP);
  Serial.printf("BQ27220: Full Charge Capacity: %d mAh\n", newFCC);

  if (verifyDC == designCapacity_mAh) {
    Serial.println("BQ27220: Configuration SUCCESS");
  } else {
    Serial.println("BQ27220: Configuration FAILED");
  }

  // Step 6: Seal the device
  bq27220_writeControl(0x0030);
  delay(5);

  // Step 7: Force full gauge RESET to reinitialize FCC from new DC/DE.
  // Without this, the Impedance Track algorithm retains the old FCC
  // (often 3000 mAh from factory) until a full charge/discharge cycle.
  bq27220_writeControl(0x0041);  // RESET
  delay(1000);  // Gauge needs time to fully reinitialize

  // Re-verify after hard reset
  verifyDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  newFCC = bq27220_read16(BQ27220_REG_FULL_CAP);
  Serial.printf("BQ27220: Post-RESET DC=%d FCC=%d mAh\n", verifyDC, newFCC);

  return verifyDC == designCapacity_mAh;
#else
  return false;
#endif
}

int16_t TDeckProMaxBoard::getAvgCurrent() {
  #if HAS_BQ27220
    return (int16_t)bq27220_read16(BQ27220_REG_AVG_CURRENT);
  #else
    return 0;
  #endif
}

int16_t TDeckProMaxBoard::getAvgPower() {
  #if HAS_BQ27220
    return (int16_t)bq27220_read16(BQ27220_REG_AVG_POWER);
  #else
    return 0;
  #endif
}

uint16_t TDeckProMaxBoard::getTimeToEmpty() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_TIME_TO_EMPTY);
  #else
    return 0xFFFF;
  #endif
}

uint16_t TDeckProMaxBoard::getRemainingCapacity() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_REMAIN_CAP);
  #else
    return 0;
  #endif
}

uint16_t TDeckProMaxBoard::getFullChargeCapacity() {
  #if HAS_BQ27220
    uint16_t fcc = bq27220_read16(BQ27220_REG_FULL_CAP);
    // Clamp to design capacity — the gauge may report a stale factory FCC
    // (e.g. 3000 mAh) until it completes a full learning cycle. Never let
    // the reported FCC exceed what the actual cell can hold.
    if (fcc > BQ27220_DESIGN_CAPACITY_MAH) fcc = BQ27220_DESIGN_CAPACITY_MAH;
    return fcc;
  #else
    return 0;
  #endif
}

uint16_t TDeckProMaxBoard::getDesignCapacity() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_DESIGN_CAP);
  #else
    return 0;
  #endif
}

int16_t TDeckProMaxBoard::getBattTemperature() {
  #if HAS_BQ27220
    uint16_t raw = bq27220_read16(BQ27220_REG_TEMPERATURE);
    // BQ27220 returns 0.1 K, convert to 0.1 C (273.1K = 0 C)
    return (int16_t)(raw - 2731);
  #else
    return 0;
  #endif
}