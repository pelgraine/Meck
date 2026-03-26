#include <Arduino.h>
#include "variant.h"
#include "T5S3Board.h"
#include <Mesh.h>  // For MESH_DEBUG_PRINTLN

void T5S3Board::begin() {
  MESH_DEBUG_PRINTLN("T5S3Board::begin() - starting");

  // Initialize I2C with T5S3 V2 pins
  // Note: No explicit peripheral power enable needed on T5S3
  // (unlike T-Deck Pro's PIN_PERF_POWERON)
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);  // 100kHz for reliable fuel gauge communication
  MESH_DEBUG_PRINTLN("T5S3Board::begin() - I2C initialized (SDA=%d, SCL=%d)", I2C_SDA, I2C_SCL);

  // Call parent class begin (handles CPU freq, etc.)
  // Note: ESP32Board::begin() also calls Wire.begin() but with our
  // PIN_BOARD_SDA/SCL defines it will use the same pins — harmless.
  ESP32Board::begin();

  // Configure backlight (off by default — save power)
  #ifdef BOARD_BL_EN
    pinMode(BOARD_BL_EN, OUTPUT);
    digitalWrite(BOARD_BL_EN, LOW);
    MESH_DEBUG_PRINTLN("T5S3Board::begin() - backlight pin configured (GPIO%d)", BOARD_BL_EN);
  #endif

  // Configure user button
  pinMode(PIN_USER_BTN, INPUT);

  // Configure LoRa SPI MISO pullup
  pinMode(P_LORA_MISO, INPUT_PULLUP);

  // Handle wake from deep sleep
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    uint64_t wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1ULL << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }

  // Test BQ27220 communication and configure design capacity
  #if HAS_BQ27220
    uint16_t voltage = getBattMilliVolts();
    MESH_DEBUG_PRINTLN("T5S3Board::begin() - Battery voltage: %d mV", voltage);
    configureFuelGauge();
  #endif

  // Early low-voltage protection
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

  MESH_DEBUG_PRINTLN("T5S3Board::begin() - complete");
}

// ---- BQ27220 register helpers (static, file-local) ----

#if HAS_BQ27220
static uint16_t bq27220_read16(uint8_t reg) {
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2) != 2) return 0;
  uint16_t val = Wire.read();
  val |= (Wire.read() << 8);
  return val;
}

static uint8_t bq27220_read8(uint8_t reg) {
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

static bool bq27220_writeControl(uint16_t subcmd) {
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x00);
  Wire.write(subcmd & 0xFF);
  Wire.write((subcmd >> 8) & 0xFF);
  return Wire.endTransmission() == 0;
}
#endif

// ---- BQ27220 public interface ----

uint16_t T5S3Board::getBattMilliVolts() {
  #if HAS_BQ27220
    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(BQ27220_REG_VOLTAGE);
    if (Wire.endTransmission(false) != 0) return 0;
    uint8_t count = Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2);
    if (count != 2) return 0;
    uint16_t voltage = Wire.read();
    voltage |= (Wire.read() << 8);
    return voltage;
  #else
    return 0;
  #endif
}

uint8_t T5S3Board::getBatteryPercent() {
  #if HAS_BQ27220
    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(BQ27220_REG_SOC);
    if (Wire.endTransmission(false) != 0) return 0;
    uint8_t count = Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2);
    if (count != 2) return 0;
    uint16_t soc = Wire.read();
    soc |= (Wire.read() << 8);
    return (uint8_t)min(soc, (uint16_t)100);
  #else
    return 0;
  #endif
}

int16_t T5S3Board::getAvgCurrent() {
  #if HAS_BQ27220
    return (int16_t)bq27220_read16(BQ27220_REG_AVG_CURRENT);
  #else
    return 0;
  #endif
}

int16_t T5S3Board::getAvgPower() {
  #if HAS_BQ27220
    return (int16_t)bq27220_read16(BQ27220_REG_AVG_POWER);
  #else
    return 0;
  #endif
}

uint16_t T5S3Board::getTimeToEmpty() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_TIME_TO_EMPTY);
  #else
    return 0xFFFF;
  #endif
}

uint16_t T5S3Board::getRemainingCapacity() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_REMAIN_CAP);
  #else
    return 0;
  #endif
}

uint16_t T5S3Board::getFullChargeCapacity() {
  #if HAS_BQ27220
    uint16_t fcc = bq27220_read16(BQ27220_REG_FULL_CAP);
    if (fcc > BQ27220_DESIGN_CAPACITY_MAH) fcc = BQ27220_DESIGN_CAPACITY_MAH;
    return fcc;
  #else
    return 0;
  #endif
}

uint16_t T5S3Board::getDesignCapacity() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_DESIGN_CAP);
  #else
    return 0;
  #endif
}

int16_t T5S3Board::getBattTemperature() {
  #if HAS_BQ27220
    uint16_t raw = bq27220_read16(BQ27220_REG_TEMPERATURE);
    return (int16_t)(raw - 2731);  // 0.1°K to 0.1°C
  #else
    return 0;
  #endif
}

// ---- BQ27220 Design Capacity configuration ----
// The BQ27220 ships with a 3000 mAh default. T5S3 uses a 1500 mAh cell.
// This function checks on boot and writes the correct value via the
// MAC Data Memory interface if needed. The value persists in battery-backed
// RAM, so this typically only writes once (or after a full battery disconnect).
//
// When DC and DE are already correct but FCC is stuck (common after initial
// flash), the root cause is Qmax Cell 0 (0x9106) and stored FCC (0x929D)
// retaining factory 3000 mAh defaults. This function detects and fixes all
// three layers: DC/DE, Qmax, and stored FCC.

bool T5S3Board::configureFuelGauge(uint16_t designCapacity_mAh) {
#if HAS_BQ27220
  uint16_t currentDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  Serial.printf("BQ27220: Design Capacity = %d mAh (target %d)\n", currentDC, designCapacity_mAh);

  if (currentDC == designCapacity_mAh) {
    // Design Capacity correct, but check if Full Charge Capacity is sane.
    uint16_t fcc = bq27220_read16(BQ27220_REG_FULL_CAP);
    Serial.printf("BQ27220: Design Capacity already correct, FCC=%d mAh\n", fcc);
    if (fcc >= designCapacity_mAh * 3 / 2) {
      // FCC is >=150% of design — stale from factory defaults (typically 3000 mAh).
      uint16_t designEnergy = (uint16_t)((uint32_t)designCapacity_mAh * 37 / 10);
      Serial.printf("BQ27220: FCC %d >> DC %d, checking Design Energy (target %d mWh)\n",
                    fcc, designCapacity_mAh, designEnergy);

      // Unseal to read data memory and issue RESET
      bq27220_writeControl(0x0414); delay(2);
      bq27220_writeControl(0x3672); delay(2);
      // Full Access
      bq27220_writeControl(0xFFFF); delay(2);
      bq27220_writeControl(0xFFFF); delay(2);

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
          // DC and DE are both correct, but FCC is stuck.
          // Root cause: Qmax Cell 0 (0x9106) and stored FCC (0x929D) retain
          // factory 3000 mAh defaults. Overwrite both with designCapacity_mAh.
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
      bq27220_writeControl(0x0030);  // SEAL
      delay(5);
      Serial.println("BQ27220: Issuing RESET to force FCC recalculation...");
      bq27220_writeControl(0x0041);  // RESET
      delay(2000);  // Full reset needs generous settle time

      fcc = bq27220_read16(BQ27220_REG_FULL_CAP);
      Serial.printf("BQ27220: FCC after RESET: %d mAh (target <= %d)\n", fcc, designCapacity_mAh);

      if (fcc > designCapacity_mAh * 3 / 2) {
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
  bq27220_writeControl(0x0414); delay(2);
  bq27220_writeControl(0x3672); delay(2);

  // Step 2: Full Access
  bq27220_writeControl(0xFFFF); delay(2);
  bq27220_writeControl(0xFFFF); delay(2);

  // Step 3: Enter CFG_UPDATE
  bq27220_writeControl(0x0090);
  bool cfgReady = false;
  for (int i = 0; i < 50; i++) {
    delay(20);
    uint16_t opStatus = bq27220_read16(BQ27220_REG_OP_STATUS);
    if (opStatus & 0x0400) { cfgReady = true; break; }
  }
  if (!cfgReady) {
    Serial.println("BQ27220: Timeout waiting for CFGUPDATE");
    bq27220_writeControl(0x0092);
    bq27220_writeControl(0x0030);
    return false;
  }

  // Step 4: Write Design Capacity at 0x929F
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x3E); Wire.write(0x9F); Wire.write(0x92);
  Wire.endTransmission();
  delay(10);

  uint8_t oldMSB = bq27220_read8(0x40);
  uint8_t oldLSB = bq27220_read8(0x41);
  uint8_t oldChk = bq27220_read8(0x60);
  uint8_t dataLen = bq27220_read8(0x61);

  uint8_t newMSB = (designCapacity_mAh >> 8) & 0xFF;
  uint8_t newLSB = designCapacity_mAh & 0xFF;
  uint8_t temp = (255 - oldChk - oldMSB - oldLSB);
  uint8_t newChk = 255 - ((temp + newMSB + newLSB) & 0xFF);

  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x3E); Wire.write(0x9F); Wire.write(0x92);
  Wire.write(newMSB); Wire.write(newLSB);
  Wire.endTransmission();
  delay(5);
  Wire.beginTransmission(BQ27220_I2C_ADDR);
  Wire.write(0x60); Wire.write(newChk); Wire.write(dataLen);
  Wire.endTransmission();
  delay(10);

  // Step 4a: Write Design Energy at 0x92A1
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

  // Step 5: Exit CFG_UPDATE with reinit
  bq27220_writeControl(0x0091);
  Serial.println("BQ27220: Sent EXIT_CFG_UPDATE_REINIT, waiting...");
  delay(200);

  // Step 6: Seal
  bq27220_writeControl(0x0030);
  delay(5);

  // Step 7: Force RESET to reinitialize FCC from new DC/DE
  bq27220_writeControl(0x0041);  // RESET
  delay(1000);

  uint16_t verifyDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  uint16_t newFCC = bq27220_read16(BQ27220_REG_FULL_CAP);
  Serial.printf("BQ27220: Post-config DC=%d FCC=%d mAh\n", verifyDC, newFCC);

  return verifyDC == designCapacity_mAh;
#else
  return false;
#endif
}