#pragma once

// =============================================================================
// CPUPowerManager — no-op stub for nRF52840
//
// The nRF52840 does not support runtime CPU frequency scaling.
// This stub satisfies the #include in main.cpp without any effect.
// =============================================================================

class CPUPowerManager {
public:
  void begin() {}
  void setHighPerformance() {}
  void setLowPower() {}
  void loop() {}
};