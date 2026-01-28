#include <Arduino.h>
#include "variant.h"
#include "TDeckBoard.h"

uint32_t deviceOnline = 0x00;

void TDeckBoard::begin() {
  
  ESP32Board::begin();
  
  Serial.println("TDeckBoard::begin() - starting");
  
  // Enable peripheral power (keyboard, sensors, etc.)
  pinMode(PIN_PERF_POWERON, OUTPUT);
  digitalWrite(PIN_PERF_POWERON, HIGH);
  Serial.println("TDeckBoard::begin() - peripheral power enabled");

  // Enable LoRa module power
  #ifdef P_LORA_EN
    pinMode(P_LORA_EN, OUTPUT);
    digitalWrite(P_LORA_EN, HIGH);
    delay(10);  // Allow module to power up
    Serial.println("TDeckBoard::begin() - LoRa power enabled");
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
  
  Serial.println("TDeckBoard::begin() - complete");
}