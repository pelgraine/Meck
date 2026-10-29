# UI v2 — Quick settings

One screen, reachable from almost anywhere, that switches the things people
switch often, and picks what keeps running while the deck is locked. Built
for `meck_max_ui2` (`MECK_QUICK_SETTINGS` in `UITask.h`: UI v2 on the MAX
with Bluetooth, WiFi and the 4G modem).

Quick settings is a switchboard, not a settings page: every tile does one
thing on Enter or a tap; M (or a long press) opens the full page for it.

Mockups: Claude Design canvas "Meck — Quick settings" (both views, 2x).

## Opening and closing

| Input | Notes |
|---|---|
| **Alt+Space** | `KB_KEY_QUICK` (0x06) from `TCA8418Keyboard`. Opens and closes it. Builds without quick settings turn it back into a space. |
| **Swipe down from the status header** | A swipe that starts in the 24 px header of a UI v2 screen. A plain header tap still goes home. |
| **Header tap on home** | Home has no other use for it. |
| **Heart pad held 600 ms** | The press still toggles the frontlight at once; a long hold switches it back on release and opens quick settings. |
| `uigo quick`, `key quick` | Serial debug CLI, for screenshots and tests. |

Closing: Q, Esc (Shift+Backspace), Backspace, Alt+Space, a header tap or a
swipe up go back to the screen it was opened over. It opens over text entry
too (compose, notes, SMS, web reader), which is drawn again when it closes,
as after the SOS screen. It does not open on the lock screen or the lock
warning, the SOS screen, during onboarding, in a running Game Boy game, or
during a call.

## Layout (240 x 320)

```
y   0  status header (unchanged)
   24  title bar: "Quick settings" | chips  Now  [lock] Locked
   52  4 rows of 2 tiles, 108 x 52, 4 px apart
  274  lock verdict: "Locked: sleeps, ~17 mA"
  296  footer: [Enter] Switch  [WASD] Move  [M] More  [L] Locked
```

A tile: 16 px icon, a switch top right, a label and a state line. The
selected tile is inverted, as on the home tiles, so the switch (not
inversion) shows on/off. Dashed outline = switching (WiFi joining, modem
starting); the screen then redraws every second instead of every 5 s.

## Keys and touch

- W/A/S/D: move in the grid (wraps).
- Enter or a tap: switch the tile.
- M or a long press: the tile's page (below). Not while text is being
  typed under it, so that is not left half done.
- L or a tap on a chip: Now / When locked.

## Now view

| Tile | Enter | State line | M opens |
|---|---|---|---|
| Bluetooth | companion Bluetooth on / off | Off · PIN 123456 · App connected | — |
| WiFi | companion WiFi on (joins the saved network) / off | Off · Joining… · SSID · App connected · No network | Settings › WiFi |
| GPS | GPS on / off, saved, no toast | Off · Searching… · Fix, 8 sat. | Map |
| 4G | modem on / off, saved as Settings › 4G Modem does | Off · Starting… · Searching… · Operator 3/5 · Error · Call | Settings › 4G Modem |
| Tones | `buzzer_quiet` | On · Off | Settings › Channels (per-channel sounds) |
| Vibration | `notif_vibrate` | On · Off | Settings › Channels |
| Light | frontlight at Backlight Brightness / off (as the heart pad) | 60 % · Off | Settings › Backlight Brightness |
| Keys light | keyboard backlight (as both Shifts) | 50 % · Off | Settings › Keyboard LED |

Tones and vibration are master switches above the per-channel choices in
Settings › Channels:

- **Tones** reuses `buzzer_quiet` (the header's silent icon, triple click on
  boards with a buzzer). Off: no message tone (ES8311 or modem tone) and no
  call ringtone.
- **Vibration** is `NodePrefs::notif_vibrate` (default on). Off: channels
  set to "Buzzer (vibrate)" do not buzz.
- Alarms and the SOS countdown are not affected.

## When locked view

What keeps running while the lock screen is up. The switch on = keeps
running; off = switched off on lock and back on at unlock. Only what the
lock switched off comes back, and the saved settings are not touched, so a
restart while locked brings everything back as it was set.

| Tile | On (keeps) | Off (pauses) | Default |
|---|---|---|---|
| Bluetooth | "Stay connected" (`ble_lock_window` 0) | sync windows every few minutes (the last interval used, 5 min at first) | on |
| WiFi | keeps running, the deck stays awake | off on lock (companion WiFi only) | on |
| GPS | keeps running while the deck sleeps: on (about +15 mA), or a fix every few minutes with Experimental › GPS when locked | off on lock, unless an SOS runs | on |
| 4G | calls and SMS arrive, the deck stays awake | off on lock, unless in a call; starting again takes seconds | on |
| Tones | as the Now switch | silent while locked | on |
| Vibration | as the Now switch | still while locked | on |
| Power saving | light sleep (`lock_sleep`) | never sleeps | on |
| On USB | stays awake on USB | sleeps on USB too (USB serial stops after a light sleep) | on |

The defaults are the behaviour from before the setting, so nothing changes
until the user picks otherwise. The Bluetooth tile is the same setting as
Settings › Phone when locked (M opens it).

The bottom line (both views) says what a lock would do now, with the
battery measurements of `docs/power-saving-ideas.md`: "Locked: sleeps,
~17 mA", "Locked: sleeps, GPS on, ~32 mA", "Locked: awake for WiFi, 4G" or
"Locked: never sleeps, ~35 mA".

Implementation: `NodePrefs::lock_keep` (`LOCK_KEEP_*` bits, appended at the
end of the prefs file), `meckLockPolicy()` in `main.cpp` (called from
`UITask::lockScreen()` / `unlockScreen()`), and `meckLockSleepBlocker()`,
which before a lock leaves out what will pause (so the lock warning does
not ask about it). A paused GPS also stops the GPS duty cycle.

## Code

- `ui-new/QuickSettingsScreen.h`: drawing and input only.
- `main.cpp` (`meckQs*`): tile state and actions, next to the radio code
  they call; entry points (keys, touch, heart pad, `uigo`).
- `UITask`: `gotoQuickSettings()` / `leaveQuickSettings()`,
  `canOpenQuickSettings()`, `tonesAllowed()` / `vibrationAllowed()`,
  `setGPS(on, feedback)`.

## List paging with A/D

In UI v2 lists, A and D now move a page back / forward (the "1 / 7" pages):
Settings and its sub-pages (also the notification sound, share and WiFi
network pickers) and the channel picker. On the first or last page they go
to the first or last row. A horizontal swipe in Settings does the same.
