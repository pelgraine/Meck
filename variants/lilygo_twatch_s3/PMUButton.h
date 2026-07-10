#pragma once

#include <Arduino.h>
#include <helpers/ui/MomentaryButton.h>   // BUTTON_EVENT_* constants
#include "TWatchS3Board.h"

// PMUButton -- a MomentaryButton work-alike for the LilyGo T-Watch S3, whose
// only control is the side PWR key. That key is not a GPIO: schematic sheet 1
// wires SW7 to net PWR_KEY, which lands on AXP2101 pin 30 (PWRON). Presses are
// therefore reported as PMU interrupts and read back over I2C.
//
// It exposes the three methods UITask uses on `user_btn` (begin, isPressed,
// check), so it drops into target.h in place of MomentaryButton.
//
// Event mapping, set up in TWatchS3Board::power_init():
//   press < 1s        PKEY_SHORT_IRQ      -> BUTTON_EVENT_CLICK
//   1s <= press < 6s  PKEY_LONG_IRQ       masked; no event
//   press >= 6s       hardware power-off  never reaches firmware
//
// BUTTON_EVENT_LONG_PRESS, _DOUBLE_CLICK and _TRIPLE_CLICK are never returned:
// the long hold belongs to the PMU's power-off, and the AXP2101 reports no
// multi-click.
//
// The IRQ line (PIN_PMU_IRQ, GPIO21) is not used. check() polls the PMU's
// latched status registers directly, which removes any dependence on that pin
// being wired, pulled up, or serviced by an ISR. Three I2C register reads every
// POLL_INTERVAL_MS is negligible on a bus already shared with the RTC and
// accelerometer. Note that clearIrqStatus() is write-1-to-clear across all
// three status registers, so an edge landing inside the ~200us between the read
// and the clear is dropped; press and release are ~100ms apart, so this cannot
// swallow a real event.
class PMUButton {
  TWatchS3Board& _board;
  unsigned long _last_poll = 0;
  bool _down = false;

  static const unsigned long POLL_INTERVAL_MS = 30;

public:
  PMUButton(TWatchS3Board& board) : _board(board) {}

  // The PWRON key needs no pin setup; the PMU is configured in power_init(),
  // which board.begin() runs before UITask::begin() reaches here.
  void begin() {}

  // True while the key is held down. Updated by check(), so it is at most
  // POLL_INTERVAL_MS stale. UITask uses this to abort a pending shutdown.
  bool isPressed() const { return _down; }

  int check() {
    if (millis() - _last_poll < POLL_INTERVAL_MS) return BUTTON_EVENT_NONE;
    _last_poll = millis();

    XPowersAXP2101* pmu = _board.getPMU();
    if (pmu == NULL) return BUTTON_EVENT_NONE;

    pmu->getIrqStatus();   // latch INTSTS1..3 into the driver's buffer

    // Apply the edges before the click, so that a poll which catches press,
    // release and short-press together still leaves _down false.
    if (pmu->isPekeyNegativeIrq()) _down = true;   // PWRON is active low: press
    if (pmu->isPekeyPositiveIrq()) _down = false;  // release

    int ev = pmu->isPekeyShortPressIrq() ? BUTTON_EVENT_CLICK : BUTTON_EVENT_NONE;

    pmu->clearIrqStatus();
    return ev;
  }
};
