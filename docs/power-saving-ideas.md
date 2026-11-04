# Lower power use on the locked screen (ideas)

Where the T-Deck MAX stands with "Power saving when locked" on, and what
could bring the draw down further. Nothing here is implemented yet; each
item says what it would need and what it might save.

## Where it stands

From the power log of one night on battery (10.7 h, 99 % to 85 %):

| State | Current | Note |
|---|---|---|
| Locked, asleep 97 % of the time | 17.4 mA | about 75-80 h standby |
| Locked, asleep, GPS on with a fix | 31.7 mA | 47 min by a window on battery (branch gps-lock-sleep): blocker "can sleep", asleep 59.3 s of 60, one timer wake a minute, awake time as without GPS; the GPS costs about 15 mA. Whether the position was updated is not confirmed: the log has no fix or position field |
| Light sleep floor | about 13 mA | between wakes |
| Locked and awake (a blocker holds it) | about 35 mA | e.g. on USB |
| Locked with a phone connected over BLE | 23 mA | automatic light sleep with the relaxed interval (step 2.3); about 100 mA before, awake all the time |
| Locked with a phone, the GPS and 4G on | about 46 mA | automatic light sleep with both (2026-11-03, 6 min): asleep 51 s of 60 |
| Locked, phone syncs "Every 10 min" | 23.4 mA | sync windows, see 2 below |
| Before power-fixes (locked, idle) | about 82 mA | for comparison |

About 470 LoRa wakes per hour. Each one keeps the ESP32 up for at least
two loop passes.

## 1. Shorter wake after a LoRa packet

After a packet the device stays awake for at least two loop passes, and
each locked pass ends in `delay(50)`, so a wake costs 100 ms or more at
about 35 mA. At 470 wakes per hour that is about 13 s per hour awake.

- Skip the `delay(50)` while `meckRxPasses >= 0` (a packet is being
  handled) and go back to sleep as soon as `meckRxHandled()` says so.
- Check `meckRxHandled()` right after `the_mesh.loop()` instead of on the
  next pass.

Expected saving: about 1-2 mA on average. Low effort, low risk (the 2 s
upper bound stays).

## 2. Phone connected over BLE while locked (the biggest item)

Today a connected phone is a sleep blocker: the Bluetooth controller cannot
sleep on this framework, so the ESP32 stays awake at about 100 mA. Most of
that is the CPU at 240 MHz looping and the controller servicing a 15-20 ms
connection interval that the deck requests after pairing.

Steps, in order of effort:

1. **Longer connection interval while locked.** On lock, request a relaxed
   interval from the phone, as the existing `MECK_BLE_SLOW_INTERVAL` watch
   build does at connect time (100-200 ms, slave latency 2, 6 s supervision
   timeout); on unlock, request 15-20 ms again for a fast sync.
   `esp_ble_gap_update_conn_params()` can be called at any time on an open
   link. The phone decides in the end: Android usually accepts, iOS keeps
   intervals in its own allowed range (15 ms multiples, latency limits) and
   may round the request. Saves radio time on both sides; on its own it
   saves little on the deck while the CPU still runs flat out.
2. **Slower CPU while locked with a phone.** `setCpuFrequencyMhz(80)` on
   lock (back to 240 on unlock). BLE works at 80 MHz; WiFi does too, but
   slower. Expected: tens of mA.
3. **Let the CPU idle instead of polling.** The locked loop with a phone
   runs `delay(50)` passes; with automatic light sleep (ESP-IDF power
   management, `esp_pm_configure` with `light_sleep_enable`) the CPU would
   sleep between BLE events. This needs the Bluetooth controller's modem
   sleep (`CONFIG_BTDM_CTRL_MODEM_SLEEP` / `CONFIG_BT_CTRL_MODEM_SLEEP`)
   and a low-power clock it can keep time with. The MAX has no 32 kHz
   crystal, so it would have to use the main crystal in sleep (supported on
   the S3, costs about 1 mA more than with a 32 kHz crystal) or the
   internal 150 kHz RC clock (too inaccurate for long intervals). Both need
   a custom sdkconfig, i.e. an ESP-IDF build of the Arduino core or
   pioarduino with `custom_sdkconfig`. Expected: the deck with a phone
   connected near the sleep floor, maybe 20-25 mA. High effort.

   First part done on branch `pioarduino-s3`: the MAX envs build on
   pioarduino (Arduino core 3.3, ESP-IDF 5.5) with `custom_sdkconfig`. Core
   3.x ships the S3 libraries with NimBLE, so the sdkconfig switches back to
   Bluedroid with the 4.2 and 5.0 features of core 2.x; the T-Deck Pro envs
   stay on core 2.x. `pioarduino_fixes.py` works around problems of the
   two-pass build. On Windows, build from PowerShell (idf_tools.py refuses
   MSys) with a short `PLATFORMIO_CACHE_DIR` (the core libraries hit the
   260-character path limit while unpacking). On core 3.x the radio must
   share the display's SPIClass: a second SPIClass on the same host resets
   the bus and the first radio transfer hangs. Tried on the device: boot,
   display, radio init, UI, modem registration. Not yet: BLE pairing and
   existing bonds, WiFi with BLE, audio and voice messages (legacy I2S
   driver on IDF 5), receiving packets, the modem's `AT+CLVL` and ringtone
   upload errors (not compared with core 2.x).

   Second part on branch `ble-modem-sleep`: `custom_sdkconfig` turns on power
   management, tickless idle and the controller's modem sleep with the main
   crystal as its low-power clock (kept powered in light sleep). When the
   locked deck is kept awake only by a connected phone (blocker PHONE),
   `meckAutoSleep()` enables automatic light
   sleep at the current CPU frequency (80 MHz, no frequency scaling: the SPI
   and I2C set-ups assume the APB clock). The CPU then sleeps whenever every
   task waits, and wakes on the LoRa DIO1 (ext1), the button and task timers.
   UART data that comes in while the CPU sleeps is lost, so the modem and
   the GPS are handled as in the lock sleep (2026-11-03; with the phone, the
   GPS and 4G on: about 46 mA, asleep 51 s of 60, a 6 min sample on battery;
   the quick settings estimate says 43 mA):
   the modem task holds a PM lock (no light sleep) while the modem's UART
   is awake, RI wakes the CPU by level (its edge ISR is off meanwhile, the
   main loop passes RI on; the PM exit callback notes a low RI so an SMS
   pulse is not missed), and with the GPS on the CPU stays up once a minute
   until the next GGA, or through a whole "GPS when locked" try. A modem
   busy for a moment does not switch the automatic sleep off. Before that
   change the deck stayed awake with either of them on. Because
   the controller keeps the crystal on in every light sleep once Bluetooth
   has been enabled, the lock sleep without a link powers it down around
   `esp_light_sleep_start()`. The power log's `auto_sleep_s` column has the
   time in automatic light sleep.

   While automatic light sleep is on, the deck also asks the phone for the
   relaxed interval of step 1 (100-150 ms, slave latency 4, 6 s timeout),
   and for 15-20 ms again when it ends. Each connection event wakes the
   CPU, so the interval only pays off now that the CPU sleeps between them.

   Measured on battery with an iPhone, locked, modem and GPS off:

   | | Current | CPU awake | Interval |
   |---|---|---|---|
   | Automatic light sleep (2026-11-01, 39 min) | 31 mA | 31 % | 30 ms (iOS) |
   | With the relaxed interval (2026-11-01/02 night, 6.75 h) | 23 mA (22-27) | 13 % | 150 ms |

   The night run: 100 % to 89 % (1.6 %/h, about 60 h for the whole cell),
   three LoRa packets received while asleep, app traffic in 7 minutes. The
   link dropped once (02:02, reason 0x08, supervision timeout) and the
   phone was back within the minute; the drops soon after locking seen
   with the relaxed interval on the deck that stayed awake did not happen.
   Still to check: the lock sleep floor without a link (13 mA before) and
   the button while it sleeps.
4. **NimBLE instead of Bluedroid.** Less RAM (helps WiFi + BLE too) and
   better tested with modem sleep. A large change (see the WiFi + BLE
   analysis).

Until step 3 exists, a phone connected while locked keeps the deck awake;
the lock warning says so (blocker PHONE).

### Implemented (branch `ble-locked-relaxed`)

- **Relaxed interval while locked (2.1)**, now on its own branch
  `ble-relaxed-interval`, to come back with step 3. With power saving on,
  locking asks the phone for 100-150 ms, slave latency 4, 6 s timeout;
  unlocking asks for 15-20 ms again. Measured below, it saved nothing
  while the deck stays awake, and the link dropped soon after locking a
  few times, so it is out of this branch until it can be tested on its
  own. `pwrlink` on the debug CLI and `link_ms` still show the interval the
  phone accepted. Step 2.2 was already in place: `CPUPowerManager` runs
  the CPU at 80 MHz while locked.
- **Sync windows.** Settings > "Phone when locked" (shown with power
  saving on): *Stay connected* (as before) or *Every 5 / 10 / 15 / 30 / 60 min*.
  With a window period set, a connected phone no longer keeps the locked
  deck awake. The deck sleeps with Bluetooth off and opens a window on
  every Nth clock minute, on each new message the user is notified of
  (muted channels don't count), and on locking while a phone is connected.
  A window advertises for up to 30 s. Once the app has reconnected, the
  window lasts while frames flow and ends 8 s after the link goes quiet,
  2 min at most. Then Bluetooth goes off again, which drops the link.
  Messages wait in the offline queue in the meantime. The blocker shows
  as "Phone sync" (power log blocker 9). The power log has the setting
  (`ble_win`), the windows per minute and how many the app reconnected in
  (`sync_win`, `sync_link`), the time in windows (`sync_s`) and the
  accepted interval (`link_ms`).
- **Reconnect windows for "Stay connected".** A connected phone still keeps
  the locked deck awake, but once the link is gone while locked (or no
  phone was connected at lock) the deck no longer stays dark until unlock.
  It opens a window at once, then at every minute wake for 5 min, every
  5 min up to an hour, then every 15 min, and on each new message as with
  sync windows. When the phone is back the windows stop and the link keeps
  the deck awake again. Only with Bluetooth switched on. A window that finds
  no phone costs 30 s awake with advertising, so an hour away is about 16
  windows, 1-2 mA on average. The power log has `app_rx`, frames the app
  sent in the minute (a window with `sync_link` but no `app_rx` is iOS
  reconnecting without the app talking), and `ble_drop`, why a link ended.

Expected: a window costs about 40 mA while it lasts. Every 5 min with a
reconnect in ~10 s is about +1.5 mA, a window without a reconnect is
+2.7 mA. That gives about 19-20 mA on average instead of 100 mA.

To check on the device:
1. Does the app reconnect on its own within the 30 s window, on Android
   and iOS, with the app in the background? If it does not, the windows
   only cost power.
2. `pwrlog`: the average with "Every 5 min" and "Every 15 min", with a
   phone that was connected at lock time.
3. `pwrlink` while locked with "Stay connected": the interval the phone
   accepted, and how much the relaxed link saves on its own.

### Measured (2026-10-30, iPhone, on battery)

Power log of one morning: "Stay connected" 09:35-10:37 and 12:15-12:20,
"Every 10 min" 11:29-12:13 (deck clock). The sync window columns and
`link_ms` came out as zeros: the firmware filled them in after the record
had been stored (fixed since), so windows are read from blocker 9 and the
phone flag instead.

| Locked, power saving on | Mean | Awake |
|---|---|---|
| Phone connected, "Stay connected" (35 min) | 97 mA | 100% |
| Phone gone, Bluetooth paused, asleep (68 min) | 13.7 mA | 1% |
| "Every 10 min" (45 min, 5 windows) | 23.4 mA | 13% |
| (the same between windows) | 14.7 mA | 4% |

- **The relaxed interval saves nothing that shows.** 97-99 mA locked with
  the phone against about 100 mA before it, and 100-110 mA unlocked. The
  deck stays awake with the controller running either way; only step 3
  (sleep between BLE events) would change that. Whether iOS accepted the
  100-150 ms is unknown (`link_ms` was not logged).
- **"Stay connected" loses the phone and does not get it back.** The link
  held 35 min after the first lock, but later dropped 45-60 s after
  locking (10:11, 10:34, 10:37) and once after 5 min (12:20). The reason is
  not in the log. Once the link is gone nothing blocks sleep, so the deck
  pauses Bluetooth (no advertising) until it is unlocked: the app cannot
  reconnect while the deck is locked, which is what the user saw. After
  each unlock the app was back within the minute.
- **Sync windows work with iOS.** The app reconnected in the windows at
  11:30, 11:50, 12:00 and 12:10, and in the window a received message
  opened at 11:52. At 11:39 the deck was awake only about 10 s, so the app
  either connected and had nothing to sync or did not come at all (the
  sync columns would say). A window with the app is about 60-70 s at
  ~98 mA, longer than the 10-20 s expected: about 1.6 mAh, or +1 mA on
  average at 10 min, +2 mA at 5 min. Messages arrive with up to 10 min
  delay, as the user reported.
- **Wakes.** Locked and asleep the deck woke once a minute for the clock
  and on the few LoRa packets; no button wakes it did not get from the
  user. No resets during the day. After the flash at 22:52 the clock read
  May 2024 until the GPS set it at 05:55 (the NVS clock backup does not
  cover a flash), so those records sit in `20240515.csv`.

Second run (2026-10-30 11:46-12:49, without the relaxed
request, "Stay connected"): the link held the whole hour, locked, with no
drop. iOS settled on a 30 ms interval (`link_ms`), not the 15-20 ms the
deck asks for. Current 97-101 mA, the same as with the relaxed request, so
that request bought nothing and probably caused the drops.

The battery fell 99 % to 93 % in 50 min (7.2 %/h), which is what 100 mA
takes from 1400 mAh (7.1 %/h); the gauge's percentage and current agree.
At 93 % it held 1301 of 1400 mAh: about 13 h at 100 mA. The Battery
page's "Remaining" is the gauge's TimeToEmpty, remaining capacity over its
average current, and the average rises while the deck is unlocked to look
at it (display, backlight, keyboard light), so it reads lower than the
locked drain gives.

Third run (2026-10-30 afternoon, iPhone, reconnect windows,
with `app_rx` and `ble_drop`):
- **The app does talk after iOS reconnects it in the background**: 6-85
  frames in the minute after a reconnect in a window.
- **A link with no app behind it.** Once, iOS reconnected but the app
  never talked, and opening the app did not help: the deck saw a phone,
  stopped advertising, and the app could not find it until Bluetooth was
  switched off and on. Now a link with no frame from the app
  within 30 s is dropped (shows as `ble_drop` 22) and advertising resumes.
- **The link drops every 1-3 minutes** after the phone has been out of
  range once, even next to the deck, unlocked and on USB: `ble_drop` 40
  (0x28, instant passed: the deck's controller missed a timed link change
  from the phone) and 8 (supervision timeout). iOS reconnects within about
  5 s, so it only shows in the new column. Right after Bluetooth was
  switched on, 23 min with no drop (debug log: 2M PHY, 251-byte data
  length, 30 ms interval). Not explained yet.

BLE debug log on USB (`-D BLE_DEBUG_LOGGING=1`, the same evening, walking
out of range and back):
- **Frames were lost during a contact sync.** The app asks for the
  contacts changed since its last sync after every reconnect, and Meck
  marks a contact changed whenever its advert is heard, so after a while
  that is most of them (650 here). The deck queued one every 7 ms; on the
  30 ms iOS link the stack's buffers filled, `notify()` failed (rc=-1,
  1811 times in 45 min) and the frames were gone, as was a contact that
  did not fit the send queue. Now the deck sends only while the controller
  has a free buffer for the link and reports busy until the queue has room
  for a batch: 650 contacts and the end frame in 5 s, no errors.
- **A sync cut by a drop carried on into the next link.** The contact
  iterator kept running; after an iOS background reconnect (no
  `CMD_APP_START`) the deck sent the rest of the list unasked and answered
  the app's own `CMD_GET_CONTACTS` with "busy", and the app hung until it
  was restarted. The iterator now stops when the link drops.
- Near the edge of range iOS opens the link with a 720 ms supervision
  timeout (the deck then asks for 4 s), so drops there are expected.
  Whether drops at close range remain with these fixes is not known yet.

Next:
1. If 0x28 drops at close range come back, catch one with the debug log
   and try without the deck's 15-20 ms / 2M PHY / data length requests at
   connect.
2. Messages: `CMD_SYNC_NEXT_MESSAGE` takes a message off the offline queue
   when it is sent; if the link drops before it arrives, the app never gets
   it (it stays in the deck's chat history). Remove it only when the app
   asks for the next one.
3. On battery: whether the app syncs in reconnect windows (`sync_link` /
   `app_rx`). An app closed from the app switcher is not woken by iOS.
4. Shorter windows: find what keeps the link busy for a minute after the
   app reconnects (`sync_s` per window).

## 3. LoRa RX duty cycle

The SX1262 can sleep between short listen windows and wake on a preamble
(`startReceiveDutyCycleAuto`, already behind `MECK_RX_DUTY_CYCLE` for the
watch build). The radio itself uses about 5-6 mA in continuous RX, so this
could save a few mA. The catch: packets with a short preamble can be missed,
the noise-floor sampling is skipped, and it does not reduce the ESP32 wakes.
Worth trying only while locked, and measured against missed packets.

## 4. The 13 mA sleep floor

LilyGo quotes about 0.34 mA for the MAX in deep sleep, while our light
sleep floor is about 13 mA, of which the SX1262 in RX is about 5-6 mA.
The rest (about 7 mA) is worth finding:

- the display controller and e-ink driver (should be idle after the
  redraw)
- the touch controller, keyboard backlight and keyboard MCU
- the GPS, 4G modem and audio codec power rails (checked off via the
  XL9555, but worth measuring one at a time)
- the speaker amplifier (XL9555 IO06) and the antenna switch (IO04)
- PSRAM in light sleep (kept powered; unavoidable while it holds data)
- pull-ups on lines into unpowered chips

Method: put the deck to sleep with the power log on, switch one rail or
peripheral at a time from the debug console, compare the logged average.

## 5. Fewer minute wakes

The lock screen redraws on each minute boundary. A redraw is short, but a
partial e-ink refresh plus the wake costs some energy every minute.
Optional setting: redraw the clock every 5 minutes, or show only the date
and battery. Small saving (well under 1 mA).

## 6. GPS when locked: a fix every few minutes (experimental)

With the GPS on, the locked device sleeps and the receiver keeps tracking
on its own (31.7 mA measured, the GPS about 15 mA of it). Settings >
Experimental Features > "GPS when locked" can instead switch the GPS off
and power it only for a fix every 2-60 minutes; the default ("Stays on")
keeps the behaviour above.

The MIA-M10Q's V_BCKP is fed from the MS621FE cell (VRTC), so with GPS_EN
off the receiver keeps its orbit data, but it has no RTC crystal and loses
the time: it has to get the time from the satellites first (about 7 s
with a good sky, by the integration manual) before the orbits help.
Expected per fix: about 10 s awake at roughly 50 mA (ESP32 35 mA plus the
GPS acquiring 15 mA). Every 10 min that is under 1 mA on top of the
17.4 mA sleep, so about 18-19 mA; every 2 min about 21-22 mA. Without a
sky each try runs into the 60 s timeout (about 50 mA for a minute), so
the interval doubles, then quadruples after misses.

To check on a battery run: the power log's awake seconds in the minutes
with a try (they show the time to the fix), the fix lane, and the average
current. Next steps if the starts are slow: coarse time aiding from the
deck's clock (UBX-MGA-INI-TIME_UTC, only once the clock is synced) and
real UBX configuration instead of the $PCAS/$PMTK lines, which the u-blox
module ignores.

### Measured (2026-10-30, "Every 10 min", by a window, on battery)

56 minutes locked (19:51-20:46, firmware 191bd9f0, "Phone when locked"
Stay connected with no phone). The rest of the run was lost: records still
in RAM do not survive the reset that USB needs after light sleep.

| | Minutes | Current | Awake per minute |
|---|---|---|---|
| All | 55 | 33.5 mA | 6.3 s |
| Without a try | 49 | 30.9 mA | 4.6 s |
| With a try | 6 | 54.7 mA | 19.8 s |

- The cycle works: a try every 10 minutes (20:01, 20:10, 20:20, 20:30,
  20:40), and every one got a fix. The fix column is 1 between tries.
  It is 0 only in the minute records taken while a try was still
  looking.
- Time to fix: a try kept the deck awake 15-23 s longer than a minute
  without one (about 18 s on average). Without the three GGAs the fix has
  to hold for, that is about 12-20 s to the fix, slower than the 10 s
  expected.
- A try costs about 0.5 mAh, so "Every 10 min" adds about 3 mA on average.
  "Stays on" costs about 14 mA (31.7 vs 17.4 mA).
- The total does not show the saving: 33.5 mA is no better than 31.7 mA
  with "Stays on". This run is not comparable with those two numbers,
  though. It received 35 LoRa packets a minute, against 0-6 in the
  earlier runs, so the deck was awake 4.6 s a minute between tries
  instead of about 1 s. It also ran at 10 C. The log's current is the
  gauge's average read at the minute wake, which busy minutes push up.
  Between tries the GPS goes off the same way as with the GPS setting
  off (EN low, UART pins released), so the 30.9 mA is not the GPS left
  on.
- To get a number that compares: run "Stays on" and "Every 10 min" one
  after the other, each for an hour in the same place with similar
  traffic.
- Minor: a LoRa wake can start a try up to 10 s early (the slack meant for
  the minute wake), so tries creep about 10 s earlier each time. The
  20:10 try started at the end of the 20:09 minute.

## 4G modem while locked

With the modem switched on, the locked deck does not sleep at all: any
modem state but OFF is the sleep blocker MODEM. The modem itself never
slept either: DTR was held low and UART sleep (`AT+CSCLK`) was off, and
the modem task listed SMS every 10 s and read the signal every 30 s.
The A7682E hardware design guide gives 12 mA for LTE idle and 2 mA for
LTE sleep (VBAT 3.8 V); in sleep it still takes calls and SMS.

What the schematic says (T-Deck Pro Max V0.1 25-09-11, page 6): RI goes
to IO7 and DTR to IO8 through U17, an RS0104 auto-direction level shifter
(A side VDD_EXT 1.8 V from the modem, B side 3.3 V). Its OE is VDD_EXT,
so with the modem off the shifter is high-impedance. IO7 is not used for
anything else on the MAX. The modem's USB only goes to test points
(TP3-TP5), so USB does not keep it awake. RI is high normally, pulses low
for 120 ms on an SMS and 60 ms on other URCs, and stays low while a call
rings in (until ATA or the caller hangs up).

### Implemented (branch `modem-sleep`)

1. **SMS on +CMTI, no signal polling while locked.** SMS are read when
   the +CMTI URC comes (also when it shows up inside an AT response), on
   an RI pulse, and on unlock. A safety poll every 10 min catches lost
   URCs. After a message has been read the list is read again until it
   is empty (the 512-byte response buffer holds only a few). AT+CSQ runs
   every 30 s only while unlocked, and once right after unlocking. Both
   4G builds get this, the Pro too.
2. **UART sleep (MAX only, `MODEM_UART_SLEEP`).** After init the modem
   gets `AT+CSCLK=1`. The task raises DTR once the modem has been idle
   for 2 s: READY, no call, no tone, nothing queued, no SMS pending, RI
   high. `sendAT()` pulls DTR low and waits 50 ms before each command,
   and sends a command once more if the first got no byte back. An RI
   falling edge (interrupt on IO7) wakes the task at once, pulls DTR low
   and checks for SMS; while RI stays low (a call ringing in) the modem
   stays awake and the SMS check waits for the call. Shutdown drops DTR
   low after the modem power is off and releases RI. `-D
   MODEM_NO_UART_SLEEP` builds without it.

3. **The ESP32 sleeps with the modem on (MAX, branch
   `modem-light-sleep`).** The modem is the sleep blocker MODEM only
   while it has something to do: not READY, a call, a tone, an SMS or
   call queued or not yet handed to the main loop, an SMS to read, RI
   low, a URC half read, or less than 2 s since the last byte or AT
   command. Idle, DTR is high (UART sleep) and the deck light-sleeps as
   without the modem. DTR is held high through the sleep
   (`gpio_hold_en`), RI is a second GPIO wake source (low level) next to
   the button; its edge interrupt is off during the sleep and set back
   after it. A GPIO wake with the button up is taken as RI: the task is
   woken and reads SMS (the URC itself may have been lost while the ESP32
   woke up), and a call ringing in keeps RI low, so the modem stays awake
   and the RING repeats. No wake window beyond 300 ms: the modem blocker
   keeps the deck up while the task works. The modem task's 500 ms wait
   needs no change: with the explicit sleep it simply does not run while
   the ESP32 sleeps.

The power log flag shows the modem on; `pwrmodem` on the debug CLI shows
the state, the time asleep since READY, RI falls, DTR wakes, SMS reads,
whether the ESP32 may sleep now, its light sleeps with the modem on and
the RI wakes among them.

Known gap: the main loop checks the modem (core 1) a moment before it
sleeps, and the modem task (core 0) could start an AT command in
between, e.g. the 10 min safety poll. That command then times out and
fails once; the next poll repeats it.

To check on the device (needs a SIM, and a test SMS and call from the
user):
1. `pwrmodem` after boot: "uart sleep on" (AT+CSCLK=1 accepted), and
   mostly "asleep" when idle.
2. An SMS to the deck, locked and unlocked: does RI fall (`ri falls` up),
   does it arrive at once? If RI does not pulse for SMS with the default
   `AT+CFGRI`, try `AT+CFGRI=1`.
3. A call to the deck while the modem is asleep: does it ring, does
   answering work, does audio work; the same with the deck locked.
4. An outgoing SMS and call right after the modem fell asleep (the first
   command after a wake).
5. `pwrlog`: locked with the modem on, an hour before and after, ideally
   in the same place (the signal changes the current a lot).
6. ESP32 light sleep with the modem on (step 3): locked on battery,
   blocker "can sleep" with the modem on, `pwrmodem` shows light sleeps
   going up. Then an SMS and a call to the locked, sleeping deck: does it
   wake (`ri wakes`), does the SMS arrive, does the call ring. If it never
   sleeps: `pwrmodem` "esp32 may sleep 0" and the reason (state, uart
   asleep, ri pin). If the modem wakes by itself in each sleep (`dtr
   wakes` with no command), DTR is not held high.

### Measured (2026-10-31, T-Deck MAX, T-Mobile CZ LTE, CSQ 17-19)

Built: `meck_max_ui2`, `meck_max_ble_wifi` and `meck_4g_ble_wifi`, no
errors. "UART sleep enabled" was a MESH_DEBUG line (off in these builds),
now a plain serial line.

First try on a cell run flat (3.60-3.66 V, on the PC's USB port): the
A7682E restarted by itself every 20-25 s (`*ATREADY`, `+CREG: 0,0`, CSQ
99), on the old firmware as well. At 3.75 V it registered within 7 s of
`*ATREADY`. The firmware does not notice a modem restart: CSCLK, ATE0 and
CNMI are gone afterwards while `_sleepEnabled` stays true.

On a working network (3.75 V):
- Boot: `AT+CSCLK=1` OK, "UART sleep enabled" 30 s after `*ATREADY`; the
  seven tones go over with `AT+CFTRANRX` with no timeout (each `AT+FSDEL`
  before it answers ERROR; the per-tone result is a MESH_DEBUG line). One
  `AT+CMGL` after READY, then none until the 10 min safety poll.
- `pwrmodem` every minute for 10.7 min: "uart sleep on", "asleep" every
  time, asleep 98 % (630 of 640 s), 3 DTR wakes (2 CSQ while still
  unlocked, 1 safety poll), ri falls 0, ri pin 1, dtr pin 1. Over the
  15 min locked battery run: asleep 99 %, 2 DTR wakes.
- Unlocked: `AT+CSQ` every 30-31 s, each answered at once. "TX (after
  wake)" never appeared, so 50 ms after DTR low is enough.
- Locked: no CSQ. Unlock: one `AT+CMGL` and one `AT+CSQ` at once. SMS
  screen and settings show T-Mobile CZ 4/5 as before.
- Battery (locked, 4G on, `pwrhiz 1` for 15 min, same place, GPS off, no
  BLE): **112.6 mA** with UART sleep, **144.7 mA** with
  `-D MODEM_NO_UART_SLEEP` (gauge avgI every 30 s; 27 vs 35 mAh by the
  gauge's remaining capacity). UART sleep saves about 32 mA, more than
  the 10 mA the hardware design guide suggests. The deck stays awake in
  both (the modem is the sleep blocker); the rest is the ESP32 awake.
- `pwrhiz` used to end after about 40 s: setting EN_HIZ puts the SY6970
  in host mode and its I2C watchdog then restores the defaults. The
  watchdog is now off during HIZ and back to 40 s after it; the run ended
  by itself after 15 min and charging resumed.
- 4G off in settings: `AT+CPOF` OK, GPIO8 output LOW, `pwrmodem` OFF,
  XL9555 modem=0; the charging current rose by 48 mA, so the modem draws
  nothing once off. 4G on again: READY after 14 s, "UART sleep enabled"
  after 36 s.

Still to test with someone at the deck: an SMS in (locked and unlocked:
does RI fall, does it arrive at once), a call in while the modem sleeps
(ring, answer, audio), an SMS and a call out right after the modem fell
asleep, and an hour locked in `pwrlog`.

Seen on the way, not from this branch: modem init sends `AT+CMGD=1,4`,
which deletes SMS that arrived while the deck was off before they are
read; `_state` is READY before the tones and CSCLK. And once, locked on
USB with 4G off, the deck light-slept for 30 s and USB was gone until
reset: the charger had just redone its input detection (charging current
145 -> 450 mA), and one PG_STAT read of 0 was enough to let it sleep.

### Measured: ESP32 light sleep with the modem on (2026-10-31, same deck)

Rebased onto `modem-sleep`, no conflicts; `meck_max_ui2`,
`meck_max_ble_wifi` and `meck_4g_ble_wifi` build. Boot as above: CSCLK
OK, "UART sleep enabled", T-Mobile CZ CSQ 18.
- Locked on USB, 4G on: `pwrmodem` "esp32 may sleep 1"; the lock screen
  names only "On USB power" as the reason to stay awake.
- Battery (locked, 4G on, `pwrhiz 1`, 23:49-00:05, GPS off, no BLE): the
  deck light-slept 59.3 s of every 60 (99 %), one timer wake a minute,
  no LoRa wakes, modem flag on throughout, blocker MODEM in one minute
  only (23:58, most likely the 10 min safety poll). Gauge avgI 21-24 mA,
  **about 22 mA**, against 112.6 mA with UART sleep alone (ESP32 awake)
  and 144.7 mA with neither. 3729 -> 3721 mV in 15 min.
- The counters in `pwrmodem` (light sleeps, RI wakes) could not be read:
  USB serial died with the first light sleep, as it always does, so
  these numbers come from the power log after an unlock and a reset.
- When HIZ ended the deck stayed awake on USB and charged, as it should.
  After the night the modem was still registered (CREG 0,1, CSQ 17 right
  after the reset).
- The boot after that reset printed "XL9555: NOT FOUND on I2C bus!" once
  (modem and display kept working, the expander holds its outputs); the
  next boot, after flashing, was clean. Maybe the I2C bus was stuck at
  the reset; worth watching.

About 22 mA is 6-9 mA above the expected 13 mA floor plus the modem's
2-3 mA.

SMS and call into the sleeping deck (2026-11-01, locked, `pwrhiz 1`,
asleep 59.3 s a minute before each): both came through at once.
- SMS: the RI pulse woke the deck, it was awake 3.4 s of that minute (no
  LoRa or button wake in it) and the SMS was there right away; then back
  to 59.3 s asleep a minute.
- Call: it rang at once, answering worked and both sides heard each
  other. The deck stayed awake from the first ring (blocker MODEM),
  about 270 mA while ringing and 200 mA in the call.
- RI wakes have no column of their own in the power log (they are not
  LoRa, button or timer wakes), only the shorter sleep in that minute.

### Low battery (MAX)

On 2026-10-31 the deck ran flat with 4G on and the modem kept running
after the ESP32 died: the SGM6609 boost takes the cell directly and the
XL9555 keeps 6609_EN high. Below about 3.65 V the A7682E also restarts by
itself under load, and the 2.8 V shutdown was never reached.
- **4G off first.** Below `MODEM_MIN_MILLIVOLTS` (3650, three readings
  8 s apart, one wake apart while asleep) the modem is shut down, and
  `begin()` refuses to start it until the cell is above
  `MODEM_RESUME_MILLIVOLTS` (3800). Then it starts again by itself if 4G
  is on in settings; the saved setting is never changed. The deck runs
  on for LoRa. A low cell at boot keeps the modem off from the start.
- **What the user sees.** An alert "Battery low: 4G off. Charge the
  deck."; on the lock screen "Battery low, 4G off: charge now" (on USB
  "4G off until charged") instead of the time estimate; in settings 4G
  shows OFF with "Battery low: charge first", and turning it on is
  refused with an alert.
- **Shutdown at 3400 mV** instead of 2800, and PWRKEY is released before
  `AT+CPOF` (the power-on pulse left it held down, which with the
  auto power-on wiring can switch the A7682E back on).
- Debug CLI `pwrmodemmv <mV>` makes the check see that voltage (0 = the
  gauge), to test without draining the cell.

Tried on the deck (2026-11-01, on USB, `pwrmodemmv`): at 3600 mV the
modem went off 21 s later (`AT+CPOF` OK, state OFF, XL9555 modem=0,
DTR low), the alert showed over the lock screen, the lock screen read
"4G off until charged", the settings row and the refusal as above. At
3800 mV it came back by itself: READY 16 s later, "UART sleep enabled"
after 38 s, settings 4G ON. Not tried: the lock screen text on battery
(a locked deck on battery sleeps and USB goes), a low cell at boot, the
3400 mV shutdown.

Also fixed on the way: with the charger watchdog off during `pwrhiz 1`,
a reset in the 15 min window left the SY6970 in HIZ for good (the deck
did not charge for 1.5 h). The first loop after boot now switches a
leftover HIZ off.

### Next steps

Expected with step 3: the locked deck with 4G near the sleep without it
(about 13 mA) plus the modem's 2-3 mA and its wakes.

4. **Network settings.** LTE only (`AT+CNMP=38`) stops searching 2G/3G
   with a weak signal, but calls then need VoLTE on the A7682E with the
   user's operator. eDRX (`AT+CEDRX`) with a short cycle would save more
   but delays ringing and depends on the operator. PSM (`AT+CPSMS`) makes
   the deck unreachable for calls, so not by default. The companion
   never activates a PDP context (only the repeater's cellular MQTT
   does), so no data session is kept up.
5. **"4G when locked" setting.** Always reachable / SMS only (eDRX) /
   off (`AT+CFUN=4`) with a check every N minutes, like the Bluetooth
   sync windows.

## WiFi while locked

WiFi on is a sleep blocker (about 100 mA or more). Turning it off on lock
and back on at unlock would break an app connected over WiFi. WiFi
power save (`WiFi.setSleep(true)`, modem sleep with DTIM) is the default in
Arduino and already in use, but without automatic light sleep (see 2.3) the
CPU stays awake. So for now: WiFi off when not needed.

### Implemented (branch `claude/wifi-power-consumption-4da36c`, on `ble-modem-sleep`)

The automatic light sleep of 2.3 now also runs while locked with WiFi on,
when WiFi is the only thing that keeps the deck awake, or WiFi and a
connected phone. Not with USB power set to keep it awake, the modem or the
GPS on, audio, or WiFi setup's access point. The station stays joined:
WiFi modem sleep wakes the radio for beacons, and the CPU sleeps between
them. While the deck sleeps, WiFi power save goes from Arduino's
`WIFI_PS_MIN_MODEM` (every DTIM beacon) to `WIFI_PS_MAX_MODEM` (every
`listen_interval` beacons, 3 by default, about 300 ms), and back on
unlock. App frames over WiFi wait at the access point until the next
beacon the deck listens to.

The lock warning treats WiFi as it treats a phone the deck sleeps with.
The lock screen, whenever the deck sleeps, lists what stays on rather than
the one that chose the automatic sleep: "Power saving · app, Wi-Fi, 4G,
GPS" (`meckSleepKeepsText()`).

### Measured (2026-11-03, on battery)

Locked 110 min with WiFi joined, Bluetooth, GPS and 4G off: **25.0 mA**
mean, asleep about 54 s of every minute (blocker 6, WiFi). Once every 15
minutes one minute sleeps only about 36 s (27-28 mA), cause unknown (DHCP or
ARP?), worth about 0.2 mA on average. No power log has WiFi on and locked on
battery without it, so the "about 100 mA" above is still an estimate.

Not verified: LoRa reception in automatic light sleep. The log counted no
packets during the test, but none in the three hours before it either, with
the deck awake, so it says nothing. The same holds for the phone case.

## Suggested order

1. Shorter wake after a packet (1): small, safe.
2. Relaxed BLE interval and 80 MHz CPU while locked with a phone (2.1, 2.2).
3. Find the rest of the 13 mA floor (4).
4. Automatic light sleep with BLE modem sleep (2.3): the big one, needs the
   custom sdkconfig.
5. 4G: UART sleep, then light sleep with RI as a wake source (both
   implemented; locked with 4G on 145 -> 113 -> 22 mA on the device, an
   SMS and a call wake the sleeping deck; see "4G modem while locked").
