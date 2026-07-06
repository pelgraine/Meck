#include <Arduino.h>
#include "TWatchS3PlusBoard.h"
#include <SensorBMA423.hpp>

void TWatchS3PlusBoard::begin() {
  ESP32Board::begin();
  power_init();

  // BMA423 accelerometer (always-on I2C, 0x19): enable the tilt / wrist-raise
  // feature and its interrupt (routed to PIN1 -> GPIO14) for raise-to-wake.
  _accel = new SensorBMA423();
  if (_accel->begin(Wire, I2C_ADDR_ACCEL, PIN_BOARD_SDA, PIN_BOARD_SCL)) {
    _accel->setRemapAxes(SensorRemap::BOTTOM_LAYER_TOP_RIGHT_CORNER);
    _accel->configAccelerometer(OperationMode::NORMAL, AccelFullScaleRange::FS_2G,
                                50.0f, AccelBandwidth::OSR2_AVG2, AccelPerfMode::CIC_AVG_MODE);
    _accel->enableTiltDetector(true, true);
    pinMode(PIN_ACCEL_IRQ, INPUT_PULLDOWN);
  }

  esp_reset_reason_t reason = esp_reset_reason();
  if (reason == ESP_RST_DEEPSLEEP) {
    long wakeup_source = esp_sleep_get_ext1_wakeup_status();
    if (wakeup_source & (1 << P_LORA_DIO_1)) {
      startup_reason = BD_STARTUP_RX_PACKET;
    }
    rtc_gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
  }
}

bool TWatchS3PlusBoard::power_init() {
  PMU = new XPowersAXP2101(Wire, PIN_BOARD_SDA, PIN_BOARD_SCL, I2C_ADDR_PMU);
  if (!PMU->init()) {
    MESH_DEBUG_PRINTLN("Warning: Failed to find AXP2101 power management");
    delete PMU;
    PMU = NULL;
    return false;
  }

  PMU->setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

  // Power rails per the T-Watch S3 Plus PowerManage table:
  //   ALDO2 = display backlight, ALDO3 = display + touch,
  //   ALDO4 = LoRa, BLDO1 = GNSS, BLDO2 = DRV2605, ALDO1 = unused.
  PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);  // LoRa radio
  PMU->enablePowerOutput(XPOWERS_ALDO4);
  PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);  // display + touch
  PMU->enablePowerOutput(XPOWERS_ALDO3);
  PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);  // display backlight
  PMU->enablePowerOutput(XPOWERS_ALDO2);
  PMU->setPowerChannelVoltage(XPOWERS_BLDO2, 3300);  // DRV2605 haptic
  PMU->enablePowerOutput(XPOWERS_BLDO2);
  // GNSS (MIA-M10Q) on BLDO1 -- set the rail voltage but leave it OFF at boot.
  // It is powered on demand via gpsPowerOn() when gps_enabled is set.
  PMU->setPowerChannelVoltage(XPOWERS_BLDO1, 3300);
  PMU->disablePowerOutput(XPOWERS_BLDO1);

  PMU->disablePowerOutput(XPOWERS_DCDC2);
  PMU->disablePowerOutput(XPOWERS_DCDC3);
  PMU->disablePowerOutput(XPOWERS_DCDC4);
  PMU->disablePowerOutput(XPOWERS_DCDC5);
  PMU->disablePowerOutput(XPOWERS_ALDO1);    // unused on the Plus
  PMU->disablePowerOutput(XPOWERS_DLDO1);
  PMU->disablePowerOutput(XPOWERS_DLDO2);
  PMU->disablePowerOutput(XPOWERS_VBACKUP);

  PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  PMU->clearIrqStatus();

  PMU->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_125MA);
  PMU->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

  PMU->disableTSPinMeasure();
  PMU->enableSystemVoltageMeasure();
  PMU->enableVbusVoltageMeasure();
  PMU->enableBattVoltageMeasure();

  PMU->setPowerKeyPressOffTime(XPOWERS_POWEROFF_4S);
  return true;
}

void TWatchS3PlusBoard::gpsPowerOn() {
  if (PMU) {
    PMU->enablePowerOutput(XPOWERS_BLDO1);
    delay(100);  // allow the module to boot before we expect NMEA
  }
}

void TWatchS3PlusBoard::gpsPowerOff() {
  if (PMU) PMU->disablePowerOutput(XPOWERS_BLDO1);
}

bool TWatchS3PlusBoard::tiltFired() {
  if (digitalRead(PIN_ACCEL_IRQ)) {   // only the tilt IRQ is enabled
    _accel->update();                 // reading the status clears the latched INT
    return true;
  }
  return false;
}