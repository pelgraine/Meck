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
// Identical procedure to TDeckBoard — sets 1500 mAh for T5S3's larger cell.
// The BQ27220 ships with 3000 mAh default. This writes once on first boot
// and persists in battery-backed RAM.

bool T5S3Board::configureFuelGauge(uint16_t designCapacity_mAh) {
#if HAS_BQ27220
  uint16_t currentDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  Serial.printf("BQ27220: Design Capacity = %d mAh (target %d)\n", currentDC, designCapacity_mAh);

  if (currentDC == designCapacity_mAh) {
    uint16_t fcc = bq27220_read16(BQ27220_REG_FULL_CAP);
    Serial.printf("BQ27220: Design Capacity correct, FCC=%d mAh\n", fcc);
    if (fcc < designCapacity_mAh * 3 / 2) {
      return true;  // FCC is sane, nothing to do
    }
    // FCC is stale from factory — fall through to reconfigure
    Serial.printf("BQ27220: FCC %d >> DC %d, reconfiguring\n", fcc, designCapacity_mAh);
  }

  // Unseal
  bq27220_writeControl(0x0414); delay(2);
  bq27220_writeControl(0x3672); delay(2);
  // Full Access
  bq27220_writeControl(0xFFFF); delay(2);
  bq27220_writeControl(0xFFFF); delay(2);

  // Enter CFG_UPDATE
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

  // Write Design Capacity at 0x929F
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

  // Write Design Energy at 0x92A1
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

  // Exit CFG_UPDATE with reinit
  bq27220_writeControl(0x0091);
  delay(200);

  // Seal
  bq27220_writeControl(0x0030);
  delay(5);

  // Force RESET to reinitialize FCC
  bq27220_writeControl(0x0041);
  delay(1000);

  uint16_t verifyDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  uint16_t newFCC = bq27220_read16(BQ27220_REG_FULL_CAP);
  Serial.printf("BQ27220: Post-config DC=%d FCC=%d mAh\n", verifyDC, newFCC);

  return verifyDC == designCapacity_mAh;
#else
  return false;
#endif
}
