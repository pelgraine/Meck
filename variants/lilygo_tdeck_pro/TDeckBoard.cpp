#include <Arduino.h>
#include "variant.h"
#include "TDeckBoard.h"

uint32_t deviceOnline = 0x00;

void TDeckBoard::begin() {
  
  Serial.println("TDeckBoard::begin() - starting");
  
  // Enable peripheral power (keyboard, sensors, etc.) FIRST
  // This powers the BQ27220 fuel gauge and other I2C devices
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);
  delay(50);  // Allow peripherals to power up before I2C init
  Serial.println("TDeckBoard::begin() - peripheral power enabled");

  // Initialize I2C with correct pins for T-Deck Pro
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(100000);  // 100kHz for reliable fuel gauge communication
  Serial.println("TDeckBoard::begin() - I2C initialized");
  
  // Now call parent class begin (after power and I2C are ready)
  ESP32Board::begin();

  // Enable LoRa module power
  #ifdef P_LORA_EN
    pinMode(P_LORA_EN, OUTPUT);
    digitalWrite(P_LORA_EN, HIGH);
    delay(10);  // Allow module to power up
    Serial.println("TDeckBoard::begin() - LoRa power enabled");
  #endif

  // Enable GPS module power and initialize Serial2
  #if HAS_GPS
    #ifdef PIN_GPS_EN
      pinMode(PIN_GPS_EN, OUTPUT);
      digitalWrite(PIN_GPS_EN, GPS_EN_ACTIVE);  // GPS_EN_ACTIVE is 1 (HIGH)
      delay(100);  // Allow GPS to power up
      Serial.println("TDeckBoard::begin() - GPS power enabled");
    #endif
    
    // Initialize Serial2 for GPS with correct pins
    Serial2.begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
    Serial.print("TDeckBoard::begin() - GPS Serial2 initialized at ");
    Serial.print(GPS_BAUDRATE);
    Serial.println(" baud");
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
  
  // Test BQ27220 communication
  #if HAS_BQ27220
    uint16_t voltage = getBattMilliVolts();
    Serial.print("TDeckBoard::begin() - Battery voltage: ");
    Serial.print(voltage);
    Serial.println(" mV");
  #endif
  
  Serial.println("TDeckBoard::begin() - complete");
}

uint16_t TDeckBoard::getBattMilliVolts() {
  #if HAS_BQ27220
    Wire.beginTransmission(BQ27220_I2C_ADDR);
    Wire.write(BQ27220_REG_VOLTAGE);
    if (Wire.endTransmission(false) != 0) {
      Serial.println("BQ27220: I2C error reading voltage");
      return 0;
    }
    
    uint8_t count = Wire.requestFrom((uint8_t)BQ27220_I2C_ADDR, (uint8_t)2);
    if (count != 2) {
      Serial.println("BQ27220: Read error - wrong byte count");
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