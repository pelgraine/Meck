#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>

#ifndef CROWPANEL_V13
  // V1.0 uses TCA9534 I/O expander at 0x18
  #include <TCA9534.h>
#endif

#include <lgfx/v1/platforms/esp32s3/Panel_RGB.hpp>
#include <lgfx/v1/platforms/esp32s3/Bus_RGB.hpp>

// CrowPanel 7.0" uses RGB parallel interface (16-bit) with:
//   V1.0: TCA9534 I/O expander at 0x18 for display power/reset/backlight
//   V1.2/V1.3: STC8H1K28 MCU at 0x30 for backlight, buzzer, speaker control
//
// Display: 800x480 native landscape, ST7277 driver IC
// Touch: GT911 at 0x5D on I2C (SDA=15, SCL=16)
//
// STC8H1K28 (v1.3) command byte reference:
//   0       = backlight max brightness
//   1-244   = backlight dimmer (244 = dimmest)
//   245     = backlight off
//   246     = buzzer on
//   247     = buzzer off
//   248     = speaker amp on
//   249     = speaker amp off

#define STC8H_I2C_ADDR  0x30

class LGFX_CrowPanel70 : public lgfx::LGFX_Device {
  lgfx::Bus_RGB _bus_instance;
  lgfx::Panel_RGB _panel_instance;
  lgfx::Touch_GT911 _touch_instance;

#ifndef CROWPANEL_V13
  TCA9534 _ioex;
#endif

public:
  static constexpr uint16_t SCREEN_WIDTH = 800;
  static constexpr uint16_t SCREEN_HEIGHT = 480;

  // --- STC8H1K28 helper (v1.3) ---
  static void sendSTC8Command(uint8_t cmd) {
    Wire.beginTransmission(STC8H_I2C_ADDR);
    Wire.write(cmd);
    Wire.endTransmission();
  }

  // Set backlight brightness: 0 = max, 244 = min, 245 = off
  static void setBacklight(uint8_t brightness) {
#ifdef CROWPANEL_V13
    sendSTC8Command(brightness);
#endif
  }

  static void buzzerOn()   { sendSTC8Command(246); }
  static void buzzerOff()  { sendSTC8Command(247); }
  static void speakerOn()  { sendSTC8Command(248); }
  static void speakerOff() { sendSTC8Command(249); }

  bool init_impl(bool use_reset, bool use_clear) override {

#ifdef CROWPANEL_V13
    // ---- V1.3: All I2C + GPIO init done externally in target.cpp ----
    // Wire.begin(), STC8H backlight, and GPIO1 touch reset pulse are
    // called BEFORE lcd.init() to avoid Wire double-init conflicts and
    // ensure the display is powered before Panel_RGB allocates framebuffer.

#else
    // ---- V1.0: TCA9534 init ----
    _ioex.attach(Wire);
    _ioex.setDeviceAddress(0x18);

    // Configure TCA9534 pins as outputs
    _ioex.config(1, TCA9534::Config::OUT);  // Display power
    _ioex.config(2, TCA9534::Config::OUT);  // Display reset
    _ioex.config(3, TCA9534::Config::OUT);  // Not used
    _ioex.config(4, TCA9534::Config::OUT);  // Backlight

    // Power on display
    _ioex.output(1, TCA9534::Level::H);

    // Reset sequence
    pinMode(1, OUTPUT);
    digitalWrite(1, LOW);
    _ioex.output(2, TCA9534::Level::L);
    delay(20);
    _ioex.output(2, TCA9534::Level::H);
    delay(100);
    pinMode(1, INPUT);

    // Turn on backlight
    _ioex.output(4, TCA9534::Level::H);
#endif

    return LGFX_Device::init_impl(use_reset, use_clear);
  }

  LGFX_CrowPanel70(void) {
    // Panel configuration
    {
      auto cfg = _panel_instance.config();
      cfg.memory_width = SCREEN_WIDTH;
      cfg.memory_height = SCREEN_HEIGHT;
      cfg.panel_width = SCREEN_WIDTH;
      cfg.panel_height = SCREEN_HEIGHT;
      cfg.offset_x = 0;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;  // Panel_RGB: rotation not supported via offset_rotation
      // Display renders in portrait (480x800 physical). Landscape rotation will
      // be handled by swapping the CrowPanel70Display coordinate mapping.
      _panel_instance.config(cfg);
    }

    // Panel detail configuration
    {
      auto cfg = _panel_instance.config_detail();
      cfg.use_psram = 1;  // Use PSRAM for frame buffer
      _panel_instance.config_detail(cfg);
    }

    // RGB bus configuration
    // Pin mapping is identical across all CrowPanel 7" hardware versions
    {
      auto cfg = _bus_instance.config();
      cfg.panel = &_panel_instance;

      // Blue (B3-B7 on panel, 5 bits)
      cfg.pin_d0 = 21;   // B3
      cfg.pin_d1 = 47;   // B4
      cfg.pin_d2 = 48;   // B5
      cfg.pin_d3 = 45;   // B6
      cfg.pin_d4 = 38;   // B7

      // Green (G2-G7 on panel, 6 bits)
      cfg.pin_d5 = 9;    // G2
      cfg.pin_d6 = 10;   // G3
      cfg.pin_d7 = 11;   // G4
      cfg.pin_d8 = 12;   // G5
      cfg.pin_d9 = 13;   // G6
      cfg.pin_d10 = 14;  // G7

      // Red (R3-R7 on panel, 5 bits)
      cfg.pin_d11 = 7;   // R3
      cfg.pin_d12 = 17;  // R4
      cfg.pin_d13 = 18;  // R5
      cfg.pin_d14 = 3;   // R6
      cfg.pin_d15 = 46;  // R7

      // Control pins
      cfg.pin_henable = 42;  // DE (Data Enable)
      cfg.pin_vsync = 41;    // VSYNC
      cfg.pin_hsync = 40;    // HSYNC
      cfg.pin_pclk = 39;     // Pixel clock (DCLK)

      // Timing configuration (14MHz pixel clock for 7" display)
      cfg.freq_write = 14000000;

      // Horizontal timing
      cfg.hsync_polarity = 0;
      cfg.hsync_front_porch = 8;
      cfg.hsync_pulse_width = 4;
      cfg.hsync_back_porch = 8;

      // Vertical timing
      cfg.vsync_polarity = 0;
      cfg.vsync_front_porch = 8;
      cfg.vsync_pulse_width = 4;
      cfg.vsync_back_porch = 8;

      // Clock configuration
      cfg.pclk_idle_high = 1;
      cfg.pclk_active_neg = 0;

      _bus_instance.config(cfg);
    }
    _panel_instance.setBus(&_bus_instance);

    // Touch configuration (GT911 at 0x5D)
    {
      auto cfg = _touch_instance.config();
      cfg.x_min = 0;
      cfg.x_max = SCREEN_WIDTH - 1;
      cfg.y_min = 0;
      cfg.y_max = SCREEN_HEIGHT - 1;
      cfg.pin_int = -1;   // IO1 is TP_INT but we poll, not interrupt-driven
      cfg.pin_rst = -1;   // Reset via STC8H1K28 (v1.3) or TCA9534 (v1.0)
      cfg.bus_shared = true;
      cfg.offset_rotation = 0;  // Match panel config
      cfg.i2c_port = 0;
      cfg.i2c_addr = 0x5D;
      cfg.pin_sda = 15;
      cfg.pin_scl = 16;
      cfg.freq = 400000;
      _touch_instance.config(cfg);
      _panel_instance.setTouch(&_touch_instance);
    }

    setPanel(&_panel_instance);
  }
};