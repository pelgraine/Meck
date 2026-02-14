#include <Arduino.h>
#include "variant.h"
#include "TDeckBoard.h"
#include <Mesh.h>  // For MESH_DEBUG_PRINTLN

uint32_t deviceOnline = 0x00;

void TDeckBoard::begin() {
  
  MESH_DEBUG_PRINTLN("TDeckBoard::begin() - starting");
  
  // Enable peripheral power (keyboard, sensors, etc.) FIRST
  // This powers the BQ27220 fuel gauge and other I2C devices
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);
  delay(50);  // Allow peripherals to power up before I2C init
  MESH_DEBUG_PRINTLN("TDeckBoard::begin() - peripheral power enabled");

  // Initialize I2C with correct pins for T-Deck Pro
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);  // 100kHz for reliable fuel gauge communication
  MESH_DEBUG_PRINTLN("TDeckBoard::begin() - I2C initialized");
  
  // Now call parent class begin (after power and I2C are ready)
  ESP32Board::begin();

  // Enable LoRa module power
  #ifdef P_LORA_EN
    pinMode(P_LORA_EN, OUTPUT);
    digitalWrite(P_LORA_EN, HIGH);
    delay(10);  // Allow module to power up
    MESH_DEBUG_PRINTLN("TDeckBoard::begin() - LoRa power enabled");
  #endif

  // Enable GPS module power and initialize Serial2
  #if HAS_GPS
    #ifdef PIN_GPS_EN
      pinMode(PIN_GPS_EN, OUTPUT);
      digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);  // GPS_EN_ACTIVE is 1 (HIGH)
      delay(100);  // Allow GPS to power up
      MESH_DEBUG_PRINTLN("TDeckBoard::begin() - GPS power enabled");
    #endif
    
    // Initialize Serial2 for GPS with correct pins
    Serial2.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    MESH_DEBUG_PRINTLN("TDeckBoard::begin() - GPS Serial2 initialized at %d baud", GPS_BAUDRATE);
  #endif

  // Disable 4G modem power (only present on 4G version, not audio version)
  // This turns off the red status LED on the modem module
  #ifdef MODEM_POWER_EN
    pinMode(MODEM_POWER_EN, OUTPUT);
    digitalWrite(MODEM_POWER_EN, LOW);  // Cut power to modem
    MESH_DEBUG_PRINTLN("TDeckBoard::begin() - 4G modem power disabled");
  #endif

  // Configure user button
  pinMode(PIN_USER_BTN, INPUT);

  // Configure LoRa SPI pins
  pinMode(P_LORA_MISO, INPUT_PULLUP);

  // Handle wake from deep sleep
  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    uint64_t wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1ULL << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;  // Received a LoRa packet while in deep sleep
    }

    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
  
  // Test BQ27220 communication and configure design capacity
  #if HAS_BQ27220
    uint16_t voltage = getBattMilliVolts();
    MESH_DEBUG_PRINTLN("TDeckBoard::begin() - Battery voltage: %d mV", voltage);
    configureFuelGauge();
  #endif
  
  MESH_DEBUG_PRINTLN("TDeckBoard::begin() - complete");
}

uint16_t TDeckBoard::getBattMilliVolts() {
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

uint8_t TDeckBoard::getBatteryPercent() {
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
// The BQ27220 ships with a 3000 mAh default. The T-Deck Pro uses a 1400 mAh
// cell. This function checks on boot and writes the correct value via the
// MAC Data Memory interface if needed. The value persists in battery-backed
// RAM, so this typically only writes once (or after a full battery disconnect).
//
// Procedure follows TI TRM SLUUBD4A Section 6.1:
//   1. Unseal  →  2. Full Access  →  3. Enter CFG_UPDATE
//   4. Write Design Capacity via MAC  →  5. Exit CFG_UPDATE  →  6. Seal

bool TDeckBoard::configureFuelGauge(uint16_t designCapacity_mAh) {
#if HAS_BQ27220
  // Read current design capacity from standard command register
  uint16_t currentDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  Serial.printf("BQ27220: Design Capacity = %d mAh (target %d)\n", currentDC, designCapacity_mAh);

  if (currentDC == designCapacity_mAh) {
    Serial.println("BQ27220: Design Capacity already correct, skipping");
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

  // Step 5: Exit CFG_UPDATE (with reinit to apply changes immediately)
  bq27220_writeControl(0x0091);  // EXIT_CFG_UPDATE_REINIT
  Serial.println("BQ27220: Sent EXIT_CFG_UPDATE_REINIT, waiting...");
  delay(200);  // Allow gauge to reinitialize

  // Verify
  uint16_t verifyDC = bq27220_read16(BQ27220_REG_DESIGN_CAP);
  Serial.printf("BQ27220: Design Capacity now reads %d mAh (expected %d)\n",
                     verifyDC, designCapacity_mAh);

  if (verifyDC == designCapacity_mAh) {
    Serial.println("BQ27220: Configuration SUCCESS");
  } else {
    Serial.println("BQ27220: Configuration FAILED");
  }

  // Step 7: Seal the device
  bq27220_writeControl(0x0030);
  delay(5);

  return verifyDC == designCapacity_mAh;
#else
  return false;
#endif
}

int16_t TDeckBoard::getAvgCurrent() {
  #if HAS_BQ27220
    return (int16_t)bq27220_read16(BQ27220_REG_AVG_CURRENT);
  #else
    return 0;
  #endif
}

int16_t TDeckBoard::getAvgPower() {
  #if HAS_BQ27220
    return (int16_t)bq27220_read16(BQ27220_REG_AVG_POWER);
  #else
    return 0;
  #endif
}

uint16_t TDeckBoard::getTimeToEmpty() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_TIME_TO_EMPTY);
  #else
    return 0xFFFF;
  #endif
}

uint16_t TDeckBoard::getRemainingCapacity() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_REMAIN_CAP);
  #else
    return 0;
  #endif
}

uint16_t TDeckBoard::getFullChargeCapacity() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_FULL_CAP);
  #else
    return 0;
  #endif
}

uint16_t TDeckBoard::getDesignCapacity() {
  #if HAS_BQ27220
    return bq27220_read16(BQ27220_REG_DESIGN_CAP);
  #else
    return 0;
  #endif
}