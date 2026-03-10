#pragma once

#include <helpers/ui/LGFXDisplay.h>
#include "LGFX_CrowPanel70.h"

// Custom display class for CrowPanel 7.0" that handles native landscape touch coordinates
class CrowPanel70Display : public LGFXDisplay {
public:
  CrowPanel70Display(int w, int h, LGFX_Device &disp) : LGFXDisplay(w, h, disp) {}

  // Override getTouch for native landscape display (800x480)
  // The 7" panel is natively landscape, unlike the 3.5" which is portrait rotated
  //
  // GT911 touch coords are in physical space (0-799, 0-479).
  // We map them to logical space using:
  //   display->width()/height() = physical LGFX dimensions (800, 480)
  //   width()/height() = logical DisplayDriver dimensions (128, 64)
  bool getTouch(int *x, int *y) override {
    lgfx::v1::touch_point_t point;
    int touch_count = display->getTouch(&point);

    if (touch_count > 0) {
      // Physical touch → logical coords
      *x = point.x * width() / display->width();
      *y = point.y * height() / display->height();
      return true;
    }

    *x = -1;
    *y = -1;
    return false;
  }
};