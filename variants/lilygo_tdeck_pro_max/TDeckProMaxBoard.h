#pragma once

// =============================================================================
// TDeckProMaxBoard — Board support for LilyGo T-Deck Pro MAX V0.1
//
// Extends TDeckBoard (which provides all BQ27220 fuel gauge methods) with:
//   - XL9555 I/O expander initialisation and control
//   - XL9555-routed peripheral power management
//   - Touch/keyboard reset via XL9555
//   - Modem power/PWRKEY via XL9555
//   - LoRa antenna selection via XL9555
//   - Audio output mux (ES8311 vs A7682E) via XL9555
//   - Speaker amplifier enable via XL9555
//
// The XL9555 must be initialised before LoRa, GPS, modem, or touch are used.
// All power enables, resets, and switches go through I2C — not direct GPIO.
// =============================================================================

#include "variant.h"
#include "TDeckBoard.h"   // Inherits BQ27220 fuel gauge, deep sleep, power management

class TDeckProMaxBoard : public TDeckBoard {
public:
  void begin();

  const char* getManufacturerName() const {
    return "LilyGo T-Deck Pro MAX";
  }

  // -------------------------------------------------------------------------
  // XL9555 I/O Expander — lightweight inline driver
  //
  // The XL9555 has 16 I/O pins across two 8-bit ports.
  // Pin 0-7 = Port 0, Pin 8-15 = Port 1.
  // We shadow the output state in _xlPort0/_xlPort1 to allow
  // single-bit set/clear without read-modify-write over I2C.
  // -------------------------------------------------------------------------

  // Initialise XL9555: set all used pins as outputs, apply boot defaults.
  // Returns true if I2C communication with XL9555 succeeded.
  bool xl9555_init();

  // Set a single XL9555 pin HIGH or LOW (pin 0-15).
  void xl9555_digitalWrite(uint8_t pin, bool value);

  // Read the current output state of a pin (from shadow, not I2C read).
  bool xl9555_digitalRead(uint8_t pin) const;

  // Write raw port values (for batch updates).
  void xl9555_writePort0(uint8_t val);
  void xl9555_writePort1(uint8_t val);

  // -------------------------------------------------------------------------
  // High-level peripheral control (delegates to XL9555)
  // -------------------------------------------------------------------------

  // Modem (A7682E) power control
  void modemPowerOn();       // Enable SGM6609 boost (6609_EN HIGH)
  void modemPowerOff();      // Disable SGM6609 boost (6609_EN LOW)
  void modemPwrkeyPulse();   // Toggle PWRKEY: HIGH 100ms → LOW 1200ms → HIGH

  // Audio output selection
  void selectAudioES8311();  // AUDIO_SEL LOW → ES8311 output to speaker/headphones
  void selectAudioModem();   // AUDIO_SEL HIGH → A7682E output to speaker/headphones
  void amplifierEnable();    // NS4150B amplifier ON (louder speaker)
  void amplifierDisable();   // NS4150B amplifier OFF (saves power)

  // LoRa antenna selection (SKY13453 RF switch)
  void loraAntennaInternal();  // LORA_SEL HIGH → internal PCB antenna (default)
  void loraAntennaExternal();  // LORA_SEL LOW → external IPEX antenna

  // Motor (DRV2605) power
  void motorEnable();          // MOTOR_EN HIGH
  void motorDisable();         // MOTOR_EN LOW

  // Touch controller reset via XL9555
  void touchReset();           // Pulse TOUCH_RST: LOW 20ms → HIGH, then 50ms settle

  // Keyboard reset via XL9555
  void keyboardReset();        // Pulse KEY_RST: LOW 20ms → HIGH, then 50ms settle

  // GPS power control via XL9555
  void gpsPowerOn();           // GPS_EN HIGH
  void gpsPowerOff();          // GPS_EN LOW

  // LoRa power control via XL9555
  void loraPowerOn();          // LORA_EN HIGH
  void loraPowerOff();         // LORA_EN LOW

  // -------------------------------------------------------------------------
  // E-ink front-light control
  // On MAX, IO41 has a working backlight circuit (boost converter + LEDs).
  // PWM control for brightness is possible via ledc.
  // -------------------------------------------------------------------------
  void backlightOn();
  void backlightOff();
  void backlightSetBrightness(uint8_t duty);  // 0-255, via LEDC PWM

private:
  // Shadow registers for XL9555 output ports (avoid I2C read-modify-write)
  uint8_t _xlPort0 = XL9555_BOOT_PORT0;
  uint8_t _xlPort1 = XL9555_BOOT_PORT1;
  bool    _xlReady = false;

  // Low-level I2C helpers
  bool xl9555_writeReg(uint8_t reg, uint8_t val);
  uint8_t xl9555_readReg(uint8_t reg);
};
