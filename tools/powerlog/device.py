"""Pulling the power log off the deck over its USB serial port.

Read-only debug CLI commands (examples/companion_radio/main.cpp, meckPowerCmd):
"pwrlog sd" lists the daily files, "pwrlog sd YYYYMMDD" prints one, "pwrlog"
prints the records still in RAM, "clock" the deck's time. "pwrlog flush" is not used: it writes the
SD card. Other log output can come in between, so only lines that look like
records are kept.
"""
import os
import re
import sys
import time

from pwrlog_format import classify

ESPRESSIF_VID = 0x303A
LISTING = re.compile(r"^(?:.*/)?(\d{8}\.csv)\s+(\d+)\s*$")
RAM_END = re.compile(r"pwrlog: \d+ records in RAM")
CLOCK = re.compile(r"\(epoch: (\d+)\)")


class PullError(Exception):
    pass


def find_port(serial_no=None):
    """(port, USB serial number) of the deck.

    With several Espressif devices on USB, the one whose serial number the
    last pull remembered; nothing is sent to an unknown device.
    """
    from serial.tools import list_ports
    all_ports = list(list_ports.comports())
    esp = [p for p in all_ports if p.vid == ESPRESSIF_VID]
    if not esp:
        raise PullError("no T-Deck found on USB (is it connected and awake?)")
    if len(esp) > 1:
        known = [p for p in esp if serial_no and p.serial_number == serial_no]
        if not known:
            raise PullError("several ESP32 devices on USB (%s); choose the deck with --port"
                            % ", ".join("%s %s" % (p.device, p.serial_number) for p in esp))
        esp = known
    deck = esp[0]
    # Windows can hand out one COM name twice; opening it then reaches the
    # other device. The deck's own interface path is unambiguous.
    others = [p for p in all_ports if p.device == deck.device and p.vid != ESPRESSIF_VID]
    if others:
        path = windows_interface_path(deck.serial_number)
        if path and can_open(path):
            return path, deck.serial_number
        raise PullError("%s is also used by another device (%s); unplug it or give the deck "
                        "another COM port in Device Manager" % (deck.device, others[0].hwid))
    return deck.device, deck.serial_number


def windows_interface_path(serial_no):
    """The serial port's device interface path for a USB serial number (Windows only).

    Opens the port even when its COM name is shared or broken: the USB device
    key named by the serial number holds the prefix of its interface's
    instance id.
    """
    if sys.platform != "win32" or not serial_no:
        return None
    import winreg
    dev = "VID_%04X&PID_1001" % ESPRESSIF_VID
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                            r"SYSTEM\CurrentControlSet\Enum\USB\%s\%s" % (dev, serial_no)) as k:
            prefix = winreg.QueryValueEx(k, "ParentIdPrefix")[0]
    except OSError:
        return None
    return "\\\\?\\USB#%s&MI_00#%s&0000#{86e0d1e0-8089-11d0-9ce4-08003e301f73}" % (dev, prefix.upper())


def can_open(port):
    import serial
    s = serial.Serial()
    s.port, s.dtr, s.rts = port, False, False
    try:
        s.open()
        s.close()
        return True
    except serial.SerialException:
        return False


def check_device_lock():
    """Refuse while a Claude Code session in another checkout holds the deck.

    Like device_lock.py, the holder is the checkout of the current directory.
    """
    tools = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sys.path.insert(0, tools)
    try:
        import device_lock
    except ImportError:
        return
    finally:
        sys.path.remove(tools)
    lock = device_lock.read_lock()
    if lock and lock.get("owner") != device_lock.worktree_of(os.getcwd()):
        raise PullError("the deck is in use by %s (%s)" % (lock.get("owner"), lock.get("task") or "no task"))


class Deck:
    def __init__(self, port):
        import serial
        # DTR/RTS reset the ESP32-S3 on its native USB port
        self.s = serial.Serial()
        self.s.port, self.s.baudrate, self.s.timeout = port, 115200, 0.3
        self.s.dtr = False
        self.s.rts = False
        try:
            self.s.open()
        except serial.SerialException as e:
            raise PullError("cannot open %s: %s" % (port, e))
        time.sleep(0.2)

    def close(self):
        self.s.close()

    def command(self, cmd, done=None, quiet=1.5, timeout=20.0):
        """Send a CLI command, return its output lines.

        Ends when done(line) is true, after `quiet` seconds without output, or
        at the timeout (then PullError when `done` was given).
        """
        self.s.reset_input_buffer()
        self.s.write(("\r\n%s\r\n" % cmd).encode("ascii"))
        lines = []
        start = last = time.time()
        while True:
            raw = self.s.readline()
            now = time.time()
            if raw:
                last = now
                line = raw.decode("ascii", "replace").strip()
                lines.append(line)
                if done and done(line):
                    return lines
            elif done is None and now - last > quiet:
                return lines
            if now - start > timeout:
                if done:
                    raise PullError("no complete answer to '%s' within %.0f s" % (cmd, timeout))
                return lines


def pull(store, port=None, log=print):
    """Copy new power records from the deck into the store. Returns a summary."""
    check_device_lock()
    state = store.state()
    serial_no = state.get("deck_serial")
    if not port:
        port, serial_no = find_port(serial_no)
    log("connecting to %s" % port)
    try:
        deck = Deck(port)
    except PullError:
        path = windows_interface_path(serial_no)
        if not path:
            raise
        log("%s does not open, trying its USB interface" % port)
        deck = Deck(path)
    try:
        listing = deck.command("pwrlog sd", quiet=1.5, timeout=10)
        if any("command not found" in l or "Unknown command" in l for l in listing):
            raise PullError("the device on %s is not a T-Deck with the debug CLI" % port)
        if any("no SD card" in l for l in listing):
            log("the deck has no SD card")
        files = {}
        for l in listing:
            m = LISTING.match(l)
            if m:
                files[m.group(1)] = int(m.group(2))
        if not files and not any(l.startswith("pwrlog:") for l in listing):
            raise PullError("the deck does not answer (needs a build with the debug CLI, and to be awake)")
        known = state.setdefault("files", {})
        dumps, complete = {}, {}
        todo = [n for n in sorted(files) if known.get(n) != files[n]]
        log("%d log files on the SD card, %d new or grown" % (len(files), len(todo)))
        for i, name in enumerate(todo, 1):
            size = files[name]
            log("reading %s (%d/%d, %d kB)" % (name, i, len(todo), size // 1024 + 1))
            out = deck.command("pwrlog sd " + name[:-4],
                               done=lambda l: l == "pwrlog: end" or "not found" in l,
                               timeout=15 + size / 2000)
            got = sum(len(l) + 2 for l in out if classify(l) in ("header", "fw", "record", "bad"))
            dumps[name] = out
            if got >= size:
                complete[name] = size
            else:
                log("%s: only %d of %d bytes came through, will retry next time" % (name, got, size))
        log("reading records still in RAM")
        dumps["ram.csv"] = deck.command("pwrlog", done=lambda l: bool(RAM_END.search(l)), timeout=20)
        # the deck's clock against ours dates records written before it was set
        t0 = time.time()
        out = deck.command("clock", done=lambda l: bool(CLOCK.search(l)), timeout=5)
        meta = {"host_utc": round((t0 + time.time()) / 2, 1),
                "deck_utc": int(CLOCK.search(out[-1]).group(1))}
        log("deck clock is %+d s off" % round(meta["host_utc"] - meta["deck_utc"]))
    finally:
        deck.close()
    new = store.save_pull(dumps, meta)
    known.update(complete)
    if serial_no:
        state["deck_serial"] = serial_no
    store.save_state(state)
    log("%d new records" % new)
    return {"new": new, "files": len(files), "read": len(todo), "port": port}
