#pragma once

// =============================================================================
// HynTouchBoard.h -- Meck board defines for the vendored Hynitron touch driver
// on the LilyGo T-Deck Pro MAX.
//
// Values are hardware facts from the T-Deck Pro MAX V0.1 schematic and the
// LilyGo product spec:
//   - CST328 touch controller at I2C 0x1A (treated as the cst3xx family)
//   - shared I2C bus: SDA = IO13, SCL = IO14
//   - touch INT line: TOU_INT = IO12
//   - touch reset: T_RST <- XL9555 port-0 pin 7 (active low)
//   - panel GDEQ031T10, 320x240
//
// SDA/SCL here are informational only: the Wire bus is already initialised by
// the board at these pins, and the Meck I2C transport (hyn_i2c.cpp) uses Wire.
// =============================================================================

#include <stdint.h>

// Shared I2C bus pins (schematic: SDA=IO13, SCL=IO14)
#ifndef BOARD_TOUCH_SDA
#define BOARD_TOUCH_SDA 13
#endif
#ifndef BOARD_TOUCH_SCL
#define BOARD_TOUCH_SCL 14
#endif

// Touch INT line (schematic: TOU_INT = IO12)
#ifndef BOARD_TOUCH_INT
#define BOARD_TOUCH_INT 12
#endif

// XL9555 I/O-expander virtual GPIO addressing (matches the LilyGo factory base).
// A reset_gpio >= XL9555_GPIO_BASE is recognised as a virtual pin and routed
// through the registered virtual-GPIO callbacks instead of a native GPIO.
#ifndef XL9555_GPIO_BASE
#define XL9555_GPIO_BASE       (0x100)
#endif
#ifndef XL9555_GPIO
#define XL9555_GPIO(pin)       (XL9555_GPIO_BASE + (pin))
#endif
#ifndef XL9555_GPIO_IS
#define XL9555_GPIO_IS(id)     ((int)(id) >= XL9555_GPIO_BASE && (int)(id) < (XL9555_GPIO_BASE + 16))
#endif
#ifndef XL9555_GPIO_TO_PIN
#define XL9555_GPIO_TO_PIN(id) ((uint8_t)((id) - XL9555_GPIO_BASE))
#endif

// Touch reset = XL9555 port-0 pin 7 (schematic: T_RST <- XL9555 P07)
#ifndef BOARD_XL9555_07_TOUCH_RST
#define BOARD_XL9555_07_TOUCH_RST (7)
#endif
#ifndef BOARD_TOUCH_RST
#define BOARD_TOUCH_RST XL9555_GPIO(BOARD_XL9555_07_TOUCH_RST)
#endif

// Panel resolution. Driver convention follows the factory config: HOR=240,
// VER=320 (LilyGo spec lists the GDEQ031T10 as 320x240).
#ifndef LCD_HOR_SIZE
#define LCD_HOR_SIZE 240
#endif
#ifndef LCD_VER_SIZE
#define LCD_VER_SIZE 320
#endif
