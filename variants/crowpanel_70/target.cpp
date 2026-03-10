#include <Arduino.h>
#include "target.h"

CrowPanel70Board board;

// SPI bus for LoRa — use HSPI (SPI3_HOST). Pass -1 for SS so RadioLib
// can manually toggle NSS via digitalWrite (hardware CS conflicts with this).
static SPIClass spi(HSPI);
RADIO_CLASS radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, spi);

WRAPPER_CLASS radio_driver(radio, board);

ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

EnvironmentSensorManager sensors;

#ifdef DISPLAY_CLASS
  LGFX_CrowPanel70 lcd;
  CrowPanel70Display display(800, 480, lcd);
  #ifndef HAS_TOUCH_SCREEN
    TouchButton user_btn(&display);
  #endif
#endif

bool radio_init() {
  delay(1000);

#ifdef CROWPANEL_V13
  Serial.println("\n\n=== CrowPanel 7.0 V1.3 MeshCore ===");
#else
  Serial.println("\n\n=== CrowPanel 7.0 MeshCore ===");
#endif
  Serial.println("Initializing...");
  Serial.printf("  LoRa pins: NSS=%d DIO1=%d RST=%d BUSY=%d\n",
                P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY);
  Serial.printf("  SPI pins: MOSI=%d MISO=%d SCK=%d\n",
                P_LORA_MOSI, P_LORA_MISO, P_LORA_SCLK);

#ifdef DISPLAY_CLASS
#ifdef CROWPANEL_V13
  Serial.println("Step 1: STC8H1K28 backlight ON...");
  Wire.beginTransmission(0x30);
  Wire.write(0);
  Wire.endTransmission();
#endif

  Serial.println("Step 2: Touch reset pulse...");
  pinMode(1, OUTPUT);
  digitalWrite(1, LOW);
  delay(120);
  pinMode(1, INPUT);

  // NO ST7277 SPI init — MADCTL breaks RGB mode on this panel.
  // Rotation handled by LovyanGFX offset_rotation in constructor.

  Serial.println("Step 3: lcd.init()...");
  lcd.init();
  Serial.printf("  LGFX: %dx%d\n", lcd.width(), lcd.height());

  Serial.println("Step 4: display.begin()...");
  display.begin();
  #ifndef HAS_TOUCH_SCREEN
    user_btn.begin();
  #endif
  Serial.println("Display ready");

  // Orientation test
  lcd.fillScreen(0x0000);
  int w = lcd.width();
  int h = lcd.height();
  lcd.fillRect(0, 0, 60, 40, 0xF800);
  lcd.fillRect(w-60, 0, 60, 40, 0x07E0);
  lcd.fillRect(0, h-40, 60, 40, 0x001F);
  lcd.fillRect(w-60, h-40, 60, 40, 0xFFE0);
  lcd.setTextColor(0xFFFF);
  lcd.setTextSize(2);
  lcd.setCursor(80, 10);
  lcd.printf("LGFX: %dx%d", w, h);
  lcd.setCursor(80, 40);
  lcd.print("CrowPanel 7.0 V1.3 + Meck");
  lcd.setCursor(80, 70);
  lcd.print("RED=TL GRN=TR BLU=BL YEL=BR");
  lcd.setCursor(80, 100);
  lcd.print("Testing LoRa...");
#endif

  Serial.println("Initializing RTC...");
  fallback_clock.begin();
  rtc_clock.begin(Wire);

  // --- LoRa SPI diagnostic: bitbang to bypass peripheral ---
  Serial.println("Initializing LoRa radio...");
  Serial.println("  Hardware SPI returned all zeros — trying bitbang to test physical wiring\n");

  // Reset radio
  pinMode(P_LORA_RESET, OUTPUT);
  digitalWrite(P_LORA_RESET, LOW);
  delay(20);
  digitalWrite(P_LORA_RESET, HIGH);
  delay(100);
  pinMode(P_LORA_BUSY, INPUT);
  unsigned long bs = millis();
  while (digitalRead(P_LORA_BUSY) && (millis() - bs < 1000)) delay(1);
  Serial.printf("  BUSY after reset: %s (%ldms)\n",
                digitalRead(P_LORA_BUSY) ? "HIGH" : "LOW", millis() - bs);

  // Configure pins as GPIO (not SPI peripheral)
  pinMode(P_LORA_SCLK, OUTPUT);   // GPIO5 = SCK
  pinMode(P_LORA_MOSI, OUTPUT);   // GPIO6 = MOSI
  pinMode(P_LORA_MISO, INPUT);    // GPIO4 = MISO
  pinMode(P_LORA_NSS, OUTPUT);    // GPIO20 = NSS
  digitalWrite(P_LORA_SCLK, LOW);
  digitalWrite(P_LORA_NSS, HIGH);

  // Bitbang SPI Mode 0: CPOL=0, CPHA=0
  // Clock idle LOW, data sampled on rising edge
  auto bbTransfer = [](uint8_t tx) -> uint8_t {
    uint8_t rx = 0;
    for (int i = 7; i >= 0; i--) {
      // Set MOSI
      digitalWrite(P_LORA_MOSI, (tx >> i) & 1);
      delayMicroseconds(2);
      // Rising edge — slave clocks in MOSI, we read MISO
      digitalWrite(P_LORA_SCLK, HIGH);
      delayMicroseconds(2);
      if (digitalRead(P_LORA_MISO)) rx |= (1 << i);
      // Falling edge
      digitalWrite(P_LORA_SCLK, LOW);
      delayMicroseconds(2);
    }
    return rx;
  };

  // Read register 0x0320 via bitbang
  Serial.println("  Bitbang SPI read of reg 0x0320:");
  digitalWrite(P_LORA_NSS, LOW);
  delayMicroseconds(50);
  uint8_t b0 = bbTransfer(0x1D);  // ReadRegister
  uint8_t b1 = bbTransfer(0x03);  // Addr high
  uint8_t b2 = bbTransfer(0x20);  // Addr low
  uint8_t b3 = bbTransfer(0x00);  // NOP (status)
  uint8_t b4 = bbTransfer(0x00);  // Register value
  digitalWrite(P_LORA_NSS, HIGH);

  Serial.printf("    SCK=%d MOSI=%d MISO=%d NSS=%d\n",
                P_LORA_SCLK, P_LORA_MOSI, P_LORA_MISO, P_LORA_NSS);
  Serial.printf("    bytes: %02X %02X %02X %02X %02X\n", b0, b1, b2, b3, b4);
  Serial.printf("    val=0x%02X %s\n", b4,
                b4 == 0x58 ? "<<< SX1262 FOUND via bitbang!" :
                (b4 == 0xFF ? "(no response)" :
                (b4 == 0x00 ? "(zeros — physical wiring issue)" : "")));

  // Also try with MISO and MOSI swapped
  Serial.println("\n  Bitbang with MISO=6, MOSI=4 (swapped):");
  pinMode(6, INPUT);   // Now MISO
  pinMode(4, OUTPUT);  // Now MOSI
  digitalWrite(4, LOW);

  // Reset again
  digitalWrite(P_LORA_RESET, LOW);
  delay(20);
  digitalWrite(P_LORA_RESET, HIGH);
  delay(100);
  bs = millis();
  while (digitalRead(P_LORA_BUSY) && (millis() - bs < 500)) delay(1);

  auto bbTransfer2 = [](uint8_t tx) -> uint8_t {
    uint8_t rx = 0;
    for (int i = 7; i >= 0; i--) {
      digitalWrite(4, (tx >> i) & 1);  // MOSI on GPIO4
      delayMicroseconds(2);
      digitalWrite(5, HIGH);           // SCK
      delayMicroseconds(2);
      if (digitalRead(6)) rx |= (1 << i);  // MISO on GPIO6
      digitalWrite(5, LOW);
      delayMicroseconds(2);
    }
    return rx;
  };

  digitalWrite(P_LORA_NSS, LOW);
  delayMicroseconds(50);
  b0 = bbTransfer2(0x1D);
  b1 = bbTransfer2(0x03);
  b2 = bbTransfer2(0x20);
  b3 = bbTransfer2(0x00);
  b4 = bbTransfer2(0x00);
  digitalWrite(P_LORA_NSS, HIGH);

  Serial.printf("    SCK=5 MOSI=4 MISO=6 NSS=%d\n", P_LORA_NSS);
  Serial.printf("    bytes: %02X %02X %02X %02X %02X\n", b0, b1, b2, b3, b4);
  Serial.printf("    val=0x%02X %s\n", b4,
                b4 == 0x58 ? "<<< SX1262 FOUND (MISO/MOSI swapped)!" :
                (b4 == 0xFF ? "(no response)" :
                (b4 == 0x00 ? "(zeros)" : "")));

  // Restore pins for hardware SPI attempt
  spi.begin(P_LORA_SCLK, P_LORA_MISO, P_LORA_MOSI, -1);
  bool result = radio.std_init(&spi);
  if (result) {
    Serial.println("LoRa: OK!");
#ifdef DISPLAY_CLASS
    lcd.setTextColor(0x07E0);
    lcd.setCursor(80, 130);
    lcd.print("LoRa: OK!");
#endif
  } else {
    Serial.println("LoRa: FAILED");
#ifdef DISPLAY_CLASS
    lcd.setTextColor(0xF800);
    lcd.setCursor(80, 130);
    lcd.print("LoRa: FAILED");
#endif
  }
  return true;
}

uint32_t radio_get_rng_seed() { return radio.random(0x7FFFFFFF); }
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
  radio.setFrequency(freq); radio.setSpreadingFactor(sf);
  radio.setBandwidth(bw); radio.setCodingRate(cr);
}
void radio_set_tx_power(uint8_t dbm) { radio.setOutputPower(dbm); }
mesh::LocalIdentity radio_new_identity() {
  RadioNoiseListener rng(radio); return mesh::LocalIdentity(&rng);
}