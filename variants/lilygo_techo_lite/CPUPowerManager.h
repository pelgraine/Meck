#pragma once
// CPUPowerManager.h — nRF52 no-op stub
// nRF52840 runs at fixed 64 MHz; no frequency scaling available.
// All methods are empty so main.cpp compiles without #ifdef guards.

class CPUPowerManager {
public:
  void begin() {}
  void loop() {}
  void setBoost() {}
  void setIdle() {}
  void setLowPower() {}
  void clearLowPower() {}
  int getFrequencyMHz() { return 64; }
};
