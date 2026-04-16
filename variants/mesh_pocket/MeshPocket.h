#pragma once

#include <Arduino.h>
#include <MeshCore.h>
#include <helpers/NRF52Board.h>

// built-ins
#define  PIN_VBAT_READ    29
#define  PIN_BAT_CTL      34
#define  MV_LSB   (3000.0F / 4096.0F) // 12-bit ADC with 3.0V input range

// HeltecMeshPocket inherits from BOTH NRF52BoardDCDC (for DC/DC converter
// efficiency) AND NRF52BoardOTA (for BLE OTA firmware update support). Both
// parent classes inherit virtually from NRF52Board so there's only one copy
// of the base-class state. The NRF52BoardOTA constructor needs the OTA
// advertising name; NRF52BoardDCDC has no explicit constructor so uses the
// default.
class HeltecMeshPocket : public NRF52BoardDCDC, public NRF52BoardOTA {
public:
  // Cast required because NRF52BoardOTA stores the name as non-const char*
  // (latent const-correctness issue in the shared header). The name is only
  // read, never modified, so the cast is safe.
  HeltecMeshPocket() : NRF52BoardOTA((char*)"MESH_POCKET_OTA") {}
  void begin();

  uint16_t getBattMilliVolts() override {
    int adcvalue = 0;
    analogReadResolution(12);
    analogReference(AR_INTERNAL_3_0);
    pinMode(PIN_BAT_CTL, OUTPUT);          // battery adc can be read only ctrl pin set to high
    pinMode(PIN_VBAT_READ, INPUT);
    digitalWrite(PIN_BAT_CTL, HIGH);

    delay(10);
    adcvalue = analogRead(PIN_VBAT_READ);
    digitalWrite(PIN_BAT_CTL, LOW);

    return (uint16_t)((float)adcvalue * MV_LSB * 4.9);
  }

  const char* getManufacturerName() const override {
    return "Heltec MeshPocket";
  }

  void powerOff() override {
    sd_power_system_off();
  }

  // Power saving — adapted from MeshCore PR #1353 (IoTThinks).
  // Puts the nRF52 into CPU light-sleep until any interrupt fires (LoRa DIO1,
  // USER button, RTC tick, SoftDevice event). When BLE is live (e.g. OTA in
  // progress or companion app connected) we fall through to a plain event wait
  // rather than the SoftDevice primitive — this is the "safe to sleep" pattern
  // from the PR review discussion that avoids BLE stack deadlocks.
  //
  // The `secs` param is ignored on NRF52 (matches upstream PR #2286 usage:
  // main.cpp passes 0 meaning "sleep whenever possible"). Any enabled IRQ
  // wakes the CPU — the RTC1 tick (~1ms) provides a hard ceiling on wake
  // latency, keeping MorseScreen timing responsive.
  void sleep(uint32_t secs) override;
};