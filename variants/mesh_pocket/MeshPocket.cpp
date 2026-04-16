#include <Arduino.h>
#include <Wire.h>
#include <nrf_soc.h>

#include "MeshPocket.h"

void HeltecMeshPocket::begin() {
  // Call NRF52BoardDCDC::begin() rather than NRF52Board::begin() so the
  // internal DC/DC regulator is actually enabled — this is the whole reason
  // HeltecMeshPocket extends NRF52BoardDCDC and directly improves battery
  // life by ~30% during BLE transmit bursts vs the default LDO.
  NRF52BoardDCDC::begin();
  Serial.begin(115200);
  pinMode(PIN_VBAT_READ, INPUT);

  pinMode(PIN_USER_BTN, INPUT);
}

// =============================================================================
// Power saving — CPU light-sleep until next interrupt.
// Adapted from MeshCore PR #1353 (IoTThinks) with the "safe to sleep" check
// pattern suggested by fschrempf in the review. Called from main.cpp loop()
// only when hasPendingWork() returns false.
//
// Wakeup sources (no GPIO sense configuration needed — RadioLib already
// attaches a DIO1 IRQ, MomentaryButton is polled from loop so any GPIO
// change via attachInterrupt or the RTC1 tick wakes us):
//   - LoRa DIO1       (incoming packet / TX done)
//   - RTC1 tick       (~1ms, drives millis() and scheduler)
//   - USER button     (via RTC tick on next poll, or attached IRQ if added)
//   - SoftDevice      (BLE stack events)
//
// When BLE SoftDevice is active we MUST use sd_app_evt_wait() rather than
// raw __WFE() — calling WFE directly with SoftDevice enabled can wedge the
// BLE stack. When it's not active (USB-only builds, or BLE disabled via
// settings) we use WFE directly.
// =============================================================================
void HeltecMeshPocket::sleep(uint32_t secs) {
  (void)secs;  // NRF52 ignores — any interrupt wakes us

  uint8_t sd_enabled = 0;
  sd_softdevice_is_enabled(&sd_enabled);

  if (sd_enabled) {
    // BLE is active (includes OTA) — use SoftDevice primitive.
    // This is the only safe way to sleep while the SoftDevice is running.
    sd_app_evt_wait();
  } else {
    // No SoftDevice — raw ARM WFE. Double-WFE pattern clears any stale
    // event flag on the first call and actually sleeps on the second.
    __SEV();
    __WFE();  // clear event flag
    __WFE();  // sleep until next event
  }
}