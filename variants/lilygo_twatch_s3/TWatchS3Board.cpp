#include <Arduino.h>
#include "TWatchS3Board.h"
#include <SensorBMA423.hpp>
#include <esp_bt.h>   // power-debug: esp_bt_controller_get_status()

volatile bool TWatchS3Board::_tilt_flag = false;

void IRAM_ATTR TWatchS3Board::onTiltISR() { _tilt_flag = true; }

// ---- Wrapper-free BMA423 step counter (raw I2C) ----------------------------
// SensorLib's SensorBMA423 step-counter methods do not compile in this build,
// so the step counter is driven directly over I2C. Register/offset/mask values
// are from the Bosch BMA423 driver.
#define BMA423_REG_STEP_CNT_OUT    0x1E   // 4-byte little-endian step count output
#define BMA423_REG_FEATURE_CONFIG  0x5E   // 64-byte feature config stream
#define BMA423_FEATURE_LEN         64
#define BMA423_STEP_EN_BYTE        0x37   // BMA423_STEP_CNTR_OFFSET(0x36) + 1
#define BMA423_STEP_EN_BIT         0x10   // BMA423_STEP_CNTR_EN_MSK
// Step counter watermark: a 10-bit field (BMA423_STEP_CNTR_WM_MSK = 0x03FF)
// spanning feature_config[0x36] as LSB and the low two bits of [0x37] as MSB.
// It does not collide with the enable bit, which is bit 4 of [0x37] (bit 12 of
// the 16-bit word). Bosch's bma423_step_counter_set_watermark() writes exactly
// these bits; SensorLib's enableStepCounter() calls it with 1, and LilyGo's own
// firmware calls setStepCounterWatermark(1). Meck never set it, which is the
// prime suspect for the undercounting.
#define BMA423_STEP_WM_LSB_BYTE    0x36   // BMA423_STEP_CNTR_OFFSET
#define BMA423_STEP_WM_MSK         0x03FF // BMA423_STEP_CNTR_WM_MSK
#define BMA423_STEP_WM_LEVEL       1      // matches LilyGoLib and the SensorLib example

#define BMA423_REG_POWER_CONF      0x7C   // BMA4_POWER_CONF_ADDR
#define BMA423_ADV_PWR_SAVE_BIT    0x01   // BMA4_ADVANCE_POWER_SAVE_MSK

static bool bma423ReadRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(I2C_ADDR_ACCEL);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)I2C_ADDR_ACCEL, (int)len) != len) return false;
  for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

static bool bma423WriteRegs(uint8_t reg, const uint8_t* buf, uint8_t len) {
  Wire.beginTransmission(I2C_ADDR_ACCEL);
  Wire.write(reg);
  for (uint8_t i = 0; i < len; i++) Wire.write(buf[i]);
  return Wire.endTransmission() == 0;
}

// Enable the step counter by setting its enable bit in the feature config,
// preserving every other byte (tilt lives at a different offset, 0x3A, so it is
// untouched). The feature config can only be written with advanced-power-save
// disabled, so we bracket the write and restore the prior power state after.
static void bma423EnableStepCounter() {
  uint8_t pc;
  if (!bma423ReadRegs(BMA423_REG_POWER_CONF, &pc, 1)) return;   // save power state
  uint8_t off = pc & ~BMA423_ADV_PWR_SAVE_BIT;                  // disable adv power save
  bma423WriteRegs(BMA423_REG_POWER_CONF, &off, 1);
  delay(2);                                                     // wake from low-power (>=450us)

  uint8_t cfg[BMA423_FEATURE_LEN];
  if (bma423ReadRegs(BMA423_REG_FEATURE_CONFIG, cfg, BMA423_FEATURE_LEN)) {
    // Watermark first, then the enable bit, in a single read-modify-write.
    uint16_t wm = ((uint16_t)cfg[BMA423_STEP_EN_BYTE] << 8) | cfg[BMA423_STEP_WM_LSB_BYTE];
    wm = (wm & ~BMA423_STEP_WM_MSK) | (BMA423_STEP_WM_LEVEL & BMA423_STEP_WM_MSK);
    cfg[BMA423_STEP_WM_LSB_BYTE] = (uint8_t)(wm & 0xFF);
    cfg[BMA423_STEP_EN_BYTE]     = (uint8_t)((wm >> 8) & 0xFF);
    cfg[BMA423_STEP_EN_BYTE] |= BMA423_STEP_EN_BIT;
    bma423WriteRegs(BMA423_REG_FEATURE_CONFIG, cfg, BMA423_FEATURE_LEN);
    delay(1);                                                   // write settle
  }

  bma423WriteRegs(BMA423_REG_POWER_CONF, &pc, 1);              // restore power state
}

void TWatchS3Board::begin() {
  ESP32Board::begin();
  power_init();

  // BMA423 accelerometer (always-on I2C, 0x19): enable the tilt / wrist-raise
  // feature and its interrupt (routed to PIN1 -> GPIO14) for raise-to-wake.
  _accel = new SensorBMA423();
  if (_accel->begin(Wire, I2C_ADDR_ACCEL, PIN_BOARD_SDA, PIN_BOARD_SCL)) {
    _accel->setRemapAxes(SensorRemap::BOTTOM_LAYER_TOP_RIGHT_CORNER);
    // 100 Hz ODR: SensorLib's BMA423_StepDetector example runs the pedometer at
    // 100 Hz, not the 50 Hz used here previously. Everything else already matched
    // (NORMAL, FS_2G, OSR2_AVG2, CIC_AVG_MODE). Revert this one literal to 50.0f
    // to A/B it against the watermark change above.
    _accel->configAccelerometer(OperationMode::NORMAL, AccelFullScaleRange::FS_2G,
                                100.0f, AccelBandwidth::OSR2_AVG2, AccelPerfMode::CIC_AVG_MODE);
    // INT1 pin electrical config: level trigger, active high, push-pull,
    // output enabled. INT1_IO_CTRL resets to output-disabled, so without
    // this the pin never drives and INPUT_PULLDOWN reads low forever.
    _accel->setInterruptPinConfig(InterruptPinMap::PIN1, false, false, true, false);
    pinMode(PIN_ACCEL_IRQ, INPUT_PULLDOWN);
    // Attach the edge ISR BEFORE enabling the tilt source, so the first
    // assertion cannot occur before the handler is armed (a missed first edge
    // on a self-clearing line otherwise locks tilt-wake out permanently).
    attachInterrupt(digitalPinToInterrupt(PIN_ACCEL_IRQ), onTiltISR, RISING);
    _accel->enableTiltDetector(true, true);
    // Enable the hardware step counter via raw I2C (SensorLib's wrapper method
    // does not compile in this build). It then counts in the BMA423 feature
    // engine with no CPU cost, even while the display is off.
    bma423EnableStepCounter();
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

bool TWatchS3Board::power_init() {
  _axp = new XPowersAXP2101(Wire, PIN_BOARD_SDA, PIN_BOARD_SCL, I2C_ADDR_PMU);
  PMU = _axp;   // same object; see the note in TWatchS3Board.h
  if (!PMU->init()) {
    MESH_DEBUG_PRINTLN("Warning: Failed to find AXP2101 power management");
    delete _axp;
    _axp = NULL;
    PMU = NULL;
    return false;
  }

  PMU->setChargingLedMode(XPOWERS_CHG_LED_CTRL_CHG);

  // Power rails per the T-Watch S3 PowerManage table, cross-checked against the
  // schematic (rev 25-03-24):
  //   ALDO1 = unused,           ALDO2 = display backlight,
  //   ALDO3 = display + touch,  ALDO4 = LoRa (schematic net LDO4 -> HPD16B3 VCC),
  //   BLDO1 = unused (no GNSS), BLDO2 = DRV2605 haptic,
  //   DLDO1 = MAX98357A speaker amp VDD (schematic sheet 6, net SPK_VDD),
  //   VBACKUP = MS412FE rechargeable coin cell backing the PCF8563 RTC domain.
  //
  // LilyGo's hardware doc lists DLDO1 as unused. The schematic disagrees: it is
  // the speaker rail. Meck compiles no audio, so it stays off, which fully
  // unpowers the amp rather than merely idling it.
  PMU->setPowerChannelVoltage(XPOWERS_ALDO4, 3300);  // LoRa radio
  PMU->enablePowerOutput(XPOWERS_ALDO4);
  PMU->setPowerChannelVoltage(XPOWERS_ALDO3, 3300);  // display + touch
  PMU->enablePowerOutput(XPOWERS_ALDO3);
  PMU->setPowerChannelVoltage(XPOWERS_ALDO2, 3300);  // display backlight
  PMU->enablePowerOutput(XPOWERS_ALDO2);
  PMU->setPowerChannelVoltage(XPOWERS_BLDO2, 3300);  // DRV2605 haptic
  PMU->enablePowerOutput(XPOWERS_BLDO2);

  PMU->disablePowerOutput(XPOWERS_DCDC2);
  PMU->disablePowerOutput(XPOWERS_DCDC3);
  PMU->disablePowerOutput(XPOWERS_DCDC4);
  PMU->disablePowerOutput(XPOWERS_DCDC5);
  PMU->disablePowerOutput(XPOWERS_ALDO1);    // unused
  PMU->disablePowerOutput(XPOWERS_BLDO1);    // GNSS rail on the Plus; unpopulated here
  PMU->disablePowerOutput(XPOWERS_DLDO1);    // MAX98357A speaker amp -- audio not compiled in
  PMU->disablePowerOutput(XPOWERS_DLDO2);

  // RTC backup cell. The PCF8563 has a single VDD pin (no separate battery
  // input), and the schematic diode-ORs it against the MS412FE on J12, which is
  // charged from the AXP2101 BACKUP pin. Leaving this off drains the cell with
  // nothing to replenish it. 3300 mV matches LilyGo's own firmware.
  // setPowerChannelVoltage/enablePowerOutput on XPOWERS_VBACKUP map onto
  // setButtonBatteryChargeVoltage()/enableButtonBatteryCharge().
  PMU->setPowerChannelVoltage(XPOWERS_VBACKUP, 3300);
  PMU->enablePowerOutput(XPOWERS_VBACKUP);

  // PWR key. The side switch (schematic SW7) is wired to PWRON, not a GPIO.
  //   press < 1s          -> PKEY_SHORT_IRQ, consumed by PMUButton as a click
  //   1s <= press < 6s    -> nothing (PKEY_LONG_IRQ is left masked)
  //   press >= 6s         -> hardware power-off, firmware never sees it
  //   hold 2s from off    -> power-on
  // Matches the 2S ON / 6S OFF behaviour printed on LilyGo's own pin diagram.
  PMU->setPowerKeyPressOnTime(XPOWERS_POWERON_2S);
  PMU->setPowerKeyPressOffTime(XPOWERS_POWEROFF_6S);
  _axp->setIrqLevelTime(XPOWERS_AXP2101_IRQ_TIME_1S);   // not on XPowersLibInterface

  PMU->disableIRQ(XPOWERS_AXP2101_ALL_IRQ);
  PMU->clearIrqStatus();
  // SHORT gives the click; NEGATIVE/POSITIVE are the press/release edges that
  // back PMUButton::isPressed().
  PMU->enableIRQ(XPOWERS_AXP2101_PKEY_SHORT_IRQ |
                 XPOWERS_AXP2101_PKEY_NEGATIVE_IRQ |
                 XPOWERS_AXP2101_PKEY_POSITIVE_IRQ);

  PMU->setChargerConstantCurr(XPOWERS_AXP2101_CHG_CUR_125MA);
  PMU->setChargeTargetVoltage(XPOWERS_AXP2101_CHG_VOL_4V2);

  PMU->disableTSPinMeasure();
  PMU->enableSystemVoltageMeasure();
  PMU->enableVbusVoltageMeasure();
  PMU->enableBattVoltageMeasure();

  Serial.printf("[PWR] rails: ALDO2(bl)=%d ALDO3(disp/touch)=%d ALDO4(LoRa)=%d BLDO2(haptic)=%d DLDO1(spk)=%d VBACKUP(rtc)=%d\n",
                PMU->isPowerChannelEnable(XPOWERS_ALDO2),
                PMU->isPowerChannelEnable(XPOWERS_ALDO3),
                PMU->isPowerChannelEnable(XPOWERS_ALDO4),
                PMU->isPowerChannelEnable(XPOWERS_BLDO2),
                PMU->isPowerChannelEnable(XPOWERS_DLDO1),
                PMU->isPowerChannelEnable(XPOWERS_VBACKUP));
  return true;
}

void TWatchS3Board::printPowerDebug() {
  if (!PMU) return;
  Serial.printf("[PWR] batt=%dmV %d%% vbus=%dmV charging=%d cpu=%dMHz bt=%d\n",
                PMU->getBattVoltage(), PMU->getBatteryPercent(),
                PMU->getVbusVoltage(), PMU->isCharging(),
                getCpuFrequencyMhz(), (int)esp_bt_controller_get_status());
}

bool TWatchS3Board::tiltFired() {
  if (_tilt_flag) {                   // set by the GPIO14 rising-edge ISR
    _tilt_flag = false;
    _accel->update();                 // reading the status clears the sensor INT
    return true;
  }
  return false;
}

uint32_t TWatchS3Board::getStepCount() {
  uint8_t d[4];
  if (!bma423ReadRegs(BMA423_REG_STEP_CNT_OUT, d, 4)) return 0;
  return (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
         ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
}
