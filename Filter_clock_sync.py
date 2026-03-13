# PlatformIO monitor filter: automatic clock sync for Meck devices
#
# When a Meck device boots with no valid RTC time, it prints "MECK_CLOCK_REQ"
# over serial.  This filter watches for that line and responds immediately
# with "clock sync <epoch>\r\n", setting the device's real-time clock to
# the host computer's current time.
#
# The sync is completely transparent — the user just sees it happen in the
# boot log.  If the RTC already has valid time, the device never sends the
# request and this filter does nothing.
#
# Install: place this file in <project>/monitor/filter_clock_sync.py
# Enable:  add "clock_sync" to monitor_filters in platformio.ini
#
# Works with: PlatformIO Core >= 6.0

import time

from platformio.device.monitor.filters.base import DeviceFilter


class ClockSync(DeviceFilter):
    NAME = "clock_sync"

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._buf = bytearray()
        self._synced = False

    def rx(self, text):
        """Called with each chunk of data received from the device."""
        if self._synced:
            return text

        # Accumulate into a line buffer to detect MECK_CLOCK_REQ
        if isinstance(text, str):
            self._buf.extend(text.encode("utf-8", errors="replace"))
        else:
            self._buf.extend(text)

        if b"MECK_CLOCK_REQ" in self._buf:
            epoch = int(time.time())
            response = "clock sync {}\r\n".format(epoch)
            try:
                # Write directly to the serial port
                self.miniterm.serial.write(response.encode("utf-8"))
            except Exception as e:
                # Fallback: shouldn't happen, but don't crash the monitor
                import sys
                print(
                    "\n[clock_sync] Failed to auto-sync: {}".format(e),
                    file=sys.stderr,
                )
            self._synced = True
            self._buf = bytearray()
        elif len(self._buf) > 2048:
            # Prevent unbounded growth — keep tail only
            self._buf = self._buf[-256:]

        return text

    def tx(self, text):
        """Called with each chunk of data sent from terminal to device."""
        return text