# Power log viewer (T-Deck MAX)

The MAX firmware logs its power state once a minute (`examples/companion_radio/main.cpp`,
"Power log"): battery %, voltage, the gauge's average current, how long it slept and why it
could not, and what was switched on. Records go to `/meshcore/powerlog/YYYYMMDD.csv` on the SD
card once an hour; the newest ones are still in RAM. This tool pulls them over USB, keeps a
history across pulls, and shows it in the browser.

```
py -3 tools/powerlog/powerlog.py            # viewer at http://127.0.0.1:8765/
py -3 tools/powerlog/powerlog.py pull       # pull new records only
py -3 tools/powerlog/powerlog.py check      # validate the history, print a summary
py -3 tools/powerlog/powerlog.py export -o report.html   # one file to share, no Pull button
```

On Linux or macOS use `python3`. Pulling needs `pyserial`; the rest only the standard library.
Options: `--data DIR` (default `powerlogs/` next to the main checkout, shared by all worktrees),
`--port` (default: the Espressif USB device), `--capacity` (mAh for %/h, default 1400).

## Pulling

The deck has to be awake on USB (after light sleep on battery its USB is gone until reset) and
run a build with the debug CLI (`meck_max_ui2`). Only read-only commands are sent: `pwrlog sd`,
`pwrlog sd YYYYMMDD`, `pwrlog` and `clock`. The viewer pulls by itself only when the history is
empty; afterwards the **Pull from deck** button does it.

The history in `powerlogs/` never loses anything: each pull adds a folder under `pulls/` with
only the lines that were new, so wiping the SD card does not wipe the history. A record printed
from RAM and later from the SD card is the same line, so repeated pulls add no duplicates.
`state.json` remembers the SD file sizes (only new or grown files are read again) and the deck's
USB serial number, which picks the deck when several ESP32 boards are plugged in.

Windows: when another device got the same COM name, or the COM name stopped opening, the tool
opens the deck through its USB interface path instead.

## Wrong clocks, gaps, quiet stretches

- After a reset the deck's clock starts at a fixed time in May 2024 until the phone, GPS or
  `clock sync` sets it. Since `MECK_CLOCK_NVS_BACKUP` the MAX resumes from the time it last saved
  (every 2 minutes) instead, so it is only a few minutes behind. The record during which it changed carries the jump (`clock_jump_s`); the
  records before it are moved onto the right clock by following them back in write order. Each
  pull also notes the deck's clock against the computer's, which dates a boot whose clock was
  never set. Records still without a plausible time are listed under "unknown time".
- No data for a while (switched off, flat, reset) is drawn as a narrow hatched column.
- Long uneventful stretches (same state, steady current, nothing sent, no key press, over an
  hour) are drawn narrow and shaded; click one to expand it, or tick "Real time scale".

## What is shown

- Current in mA (positive = drawn from the battery; charging below zero, clipped).
- Battery drain in %/h: from the current (15 min average, `current / capacity`) and from the
  gauge's percentage (2 h slope; the gauge reports whole percent).
- Battery %, the switched-on lanes (locked, asleep share, USB/charging, power saving, what kept a
  locked deck awake, GPS, modem, WiFi, phone, Bluetooth, audio, backlight), and per-minute counts
  of LoRa packets, display refreshes and wakes.
- Tiles for the visible range: mean current on battery, %/h, how long a full battery would last.
- Tables: time and mean current per combination of what was on, and every run of one state.

## Firmware versions

`git_version.py` (a pre-build script of the MAX envs) gives `main.cpp` the short commit hash
(with `+` when tracked files were changed), the commit time and the build time. The power log
writes `#fw,<hash>,<commit utc>,<build utc>,<version>` once per boot in each daily file, before
that boot's first records, and `pwrlog` prints it before the RAM records. The viewer gives every
record the firmware of the last `#fw` line before it; a new boot without one is older firmware
("not named"). It shows a Firmware lane, a table of versions (commit and build date, time logged,
mean current on battery), and the state and run tables can be limited to one version.

## Log format

`utc,local_date,local_time,secs,asleep_s,wake_lora,wake_btn,wake_timer,pct,mv,avg_ma,temp_c,usb,charge,locked,blocker,saving,gps,modem,wifi,phone,ble,audio,light,rx,tx,refresh,full_refresh,clock_jump_s,utc_offset,rc_ppm,fix,ble_win,sync_win,sync_link,sync_s,link_ms,app_rx,ble_drop`

`avg_ma` is negative while discharging; `charge` 0 off / 1 pre / 2 fast / 3 done; `blocker` is
`MeckSleepBlock` (`ui-new/UITask.h`). `fix` is 1 when the GPS was powered and had a fix at the end
of the minute (the "GPS fix" lane). The first firmware with the log had no `rc_ppm` column and the
next ones no `fix` column (read as 0, so older records look like "no fix"), then none of the
Bluetooth sync window columns, then no `app_rx` / `ble_drop` (read as 0); all forms are read. `ble_win` is the "Phone when
locked" setting (minutes between sync windows, 0 = stay connected); `sync_win` the windows
opened in the minute and `sync_link` how many of them the app reconnected in; `sync_s` the time
spent in windows; `link_ms` the BLE connection interval the phone accepted (0 = no link). With "Stay connected"
windows also open once the link is lost while locked (`sync_win` counts them too). `app_rx` is
the frames the app sent over Bluetooth in the minute: a window with `sync_link` but no `app_rx`
means iOS reconnected without the app talking. `ble_drop` is why a link ended in the minute
(HCI reason, 0 = none): 8 supervision timeout, 19 closed by the phone, 22 closed by the deck
(a window ending), 34/62 link failures. Tests: `py -3 -m unittest discover -s tools/powerlog`.
