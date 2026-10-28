# UI v2 — Quick settings (design)

Status: proposal, nothing built yet. Target: `meck_max_ui2` first, other UI v2
builds where the hardware exists.

## Goal

One screen, reachable from almost anywhere, that switches the things people
switch often: Bluetooth, WiFi, GPS, 4G modem, notification tones, vibration,
frontlight and keyboard light. Today they are spread over the home
Connections page, Settings, triple-click (buzzer), Alt+B and both-shifts.

Quick settings is a switchboard, not a settings page: every tile does one
thing on Enter or a tap, and anything with more options hands over to the
full Settings page for it.

## Layout (240 x 320, design-system grid)

```
y   0 ┌────────────────────────────────────┐
      │ Deck  ᛒ ≋        12:41    ♪̸ ▮▮▮ 84%│  status header (unchanged)
   24 ├────────────────────────────────────┤
      │ Quick settings        (Now|Locked) │  title bar + view chip
   48 ├────────────────────────────────────┤
   52 │ ┌───────────────┐ ┌───────────────┐│
      │ │ᛒ  Bluetooth  ●│ │≋  WiFi       ●││  tile 108 x 52, 2 columns
      │ │   App connected│ │   home-net    ││  ● = switch on
      │ └───────────────┘ └───────────────┘│  inverted = selected, dashed = switching
  108 │ ┌───────────────┐ ┌───────────────┐│
      │ │⌖  GPS          │ │▤  4G          ││
      │ │   Off          │ │   Starting…   ││
      │ └───────────────┘ └───────────────┘│
  164 │ ┌───────────────┐ ┌───────────────┐│
      │ │♪  Tones       ●│ │〰 Vibration  ●││
      │ │   On           │ │   On          ││
      │ └───────────────┘ └───────────────┘│
  220 │ ┌───────────────┐ ┌───────────────┐│
      │ │☀  Light      ●│ │⌨  Keys light  ││
      │ │   60 %         │ │   Off         ││
  272 │ └───────────────┘ └───────────────┘│
      │ ☾ Sleeps when locked               │  power line (see below)
  296 ├────────────────────────────────────┤
      │ [↵] Switch [WASD] [⇧↵] More [Esc]  │  footer
  320 └────────────────────────────────────┘
```

- Tiles: 108 x 52 at x 8 / 124, rows at y 52, 108, 164, 220 (gap 4). The
  home tiles are 108 x 56; the 4 px lost per row buys the title bar and the
  power line. Same radius, icon size (16) and fonts as the home tiles, so
  the screen reads as part of the same family.
- A tile has an icon, a switch in the top-right corner, a label (12B) and
  a state line (10). On/off is the switch; the **selected** tile is
  inverted, as on the home tiles (so "on" cannot also be inversion).
  Switching (WiFi joining, modem booting, GPS rail coming up) = dashed
  outline, the "pending" style `Ui2List` rows already use. Not in this
  build = hatched icon, disabled, a toast on Enter (as home placeholders).
- Power line (y 276-295): what `meckLockSleepBlocker()` says right now,
  e.g. "Sleeps when locked" or "Stays awake when locked: GPS". Toggling a
  radio here is exactly when that matters, and the information exists.

## Tiles

| Tile | Enter / tap | State line | ⇧Enter / long press opens |
|---|---|---|---|
| Bluetooth | `meckCompanionUseBLE()` / `meckCompanionBleOff()` | Off · Waiting, PIN 123456 · App connected | Home › Connections |
| WiFi | `meckCompanionUseWiFi(true)` / `meckCompanionWifiOff()` | Off · Joining… · SSID · No network saved | Settings › WiFi |
| GPS | GPS on/off (as `toggleGPS()`) | Off · Searching · Fix, 8 sat | Settings › GPS |
| 4G | modem enable + `begin()` / `shutdown()` | Off · Starting… · Operator, bars · No SIM | Settings › Modem |
| Tones | `buzzer_quiet` flip | On · Off | Settings › Notification sounds |
| Vibration | new `notif_vibrate` flip | On · Off | Settings › Notification sounds |
| Light | frontlight on/off at the saved brightness | Off · 60 % | Settings › Display |
| Keys light | keyboard backlight on/off (as both-shifts) | Off · 50 % | Settings › Display |

Tiles keep their place in every build (spatial memory); missing hardware
gives a disabled tile, not a gap. On the T-Deck Pro without modem the 4G
tile is disabled; on builds without WiFi the WiFi tile is.

### Tones and vibration (new behaviour)

The MAX has no global mute today: `buzzer_quiet` only drives `PIN_BUZZER`
(not fitted on the MAX) and the header "silent" icon; message tones and the
DRV2605 buzz are chosen per channel in Notification sounds.

- **Tones** reuses `buzzer_quiet` as the master switch, so the header icon,
  triple-click and older prefs files keep working. Off silences message
  tones (ES8311 / modem tone) and the incoming-call ringtone.
- **Vibration** is a new pref, `notif_vibrate` (default 1), appended at the
  end of `NodePrefs` so older files short-read the default. Off skips the
  haptic buzz for messages and calls.
- Neither switch touches alarms: an alarm rings and vibrates as set in the
  alarm itself. Per-channel choices stay; the switches sit above them.
- Header: tones off shows the existing silent icon. Vibration off alone
  needs no icon.

## When locked view (what runs during sleep)

Today the rule is all or nothing: while the deck is locked it light-sleeps
(about 13 mA, measured) only if no radio is on; one GPS, WiFi or modem left
on keeps it awake (about 35 mA plus the radio), and Bluetooth advertising
is always paused. The "When locked" view makes that a choice per item.

The title-bar chip (or L) flips the same 2 x 4 grid to a second view:

| Tile | Switch on | Switch off |
|---|---|---|
| Bluetooth | stays advertising/linked, deck stays awake | pauses on lock (today's behaviour) |
| WiFi | keeps running, deck stays awake | off on lock, back on unlock |
| GPS | keeps tracking, deck stays awake | off on lock, back on unlock |
| 4G | calls and SMS arrive, deck stays awake | off on lock, back on unlock |
| Tones | message tones while locked | silent while locked |
| Vibration | vibrates while locked | still while locked |
| Power saving | light sleep allowed (`lock_sleep`) | never sleeps |
| On USB | stays awake on USB (today) | sleeps on USB too |

- "Pause" means: on lock the radio is switched off and remembered, on
  unlock it comes back. A radio that is off right now shows "Off now"; its
  policy still applies the next time it is on.
- `meckLockSleepBlocker()` then only names radios that are on **and** kept;
  paused ones are off by the time the sleep check runs.
- The bottom line is the verdict, worked out live: "Sleeps · ~13 mA",
  "Awake: GPS, 4G · 35+ mA" or "Never sleeps".
- Storage: one new byte `lock_keep` (bit per item: BLE, WiFi, GPS, 4G,
  tones, vibration, USB) appended to `NodePrefs`; `lock_sleep` stays the
  master switch.
- Modem: switching it back on takes seconds (`modemManager.begin()`), so
  the unlock frame shows the 4G tile as pending; calls during the lock
  are missed, which the state line says ("Pauses").
- The existing lock warning ("GPS is on, Enter turns it off") gains a
  third choice, "Pause while locked", which sets the GPS bit and locks.
- Defaults (open question): keep today's behaviour (everything kept, BLE
  paused) or switch to pausing GPS and WiFi by default.

## Opening and closing

Reachable from every UI v2 screen that is a normal `UIScreen`:

| Input | Opens | Notes |
|---|---|---|
| **Alt+Space** | yes | New `KB_KEY_QUICK` (0x06) from `TCA8418Keyboard`, next to the Alt+B case. Alt+Space types a plain space today, so nothing is lost. Handled in `main.cpp` with `KB_KEY_BACKLIGHT`, before screen dispatch. |
| **Swipe down from the status header** | yes | Swipe whose `touchDownY < HDR_H` and `dy > TOUCH_SWIPE_THRESHOLD`. A plain header tap still goes home. |
| **Header tap on home** | yes | Home has no "go home" use for it today. |
| **Long press on the heart pad** (MAX) | yes | ≥ 600 ms; short press keeps toggling the frontlight. `meckHynKeyEvent` already gets press and release. |
| `uigo quick` (serial) | yes | For screenshots and tests. |

Closing: Esc, Q, Backspace, Alt+Space again, a header tap or a swipe up →
back to the screen it was opened from, with its state untouched (the saved
pointer pattern of `_screenBeforeLock`). Opening it again starts on the
last selected tile.

Not reachable (Alt+Space is ignored, the gesture falls through):

- lock screen and the lock warning, onboarding;
- compose mode and the emoji picker (they draw outside `UIScreen`; can be
  added later by redrawing compose on return);
- a running Game Boy game, an active or ringing call, open confirm dialogs.

## Keys and touch on the screen

- W/A/S/D: move in the 2 x 4 grid (wraps by row).
- Enter: switch the selected tile.
- Shift+Enter: open its Settings page ("More").
- L: switch between the Now and When locked views.
- Esc / Q / Backspace: close.
- Tap a tile: select and switch it. Long press: open its Settings page.

## Refresh and e-ink

- Partial refresh on every switch; the tile changes state at once
  (optimistic), then follows the real state on the next frame.
- Refresh every 1 s while any tile is pending, else every 5 s (as the home
  Connections page).
- No toasts from the switches: the tile shows the result. Existing helpers
  that toast or beep (`toggleGPS()`, `toggleBuzzer()`) get a quiet variant,
  e.g. `setGPS(bool on, bool feedback)`.
- Errors (WiFi join failed, no SIM) show on the state line and as one toast.

## Implementation sketch

- `ui-new/QuickSettingsScreen.h` (UI v2 only): render, `handleInput`,
  `tileAt(px, py)`, `open(UIScreen* returnTo)`.
- `UITask`: `quick_settings` screen, `gotoQuickSettings()` /
  `closeQuickSettings()`, `isOnQuickSettings()`, a `canOpenQuickSettings()`
  check for the list above.
- Shared helpers so Settings and Quick settings cannot drift: modem enable
  (now inline in `SettingsScreen.h` around the modem toggle), keyboard
  light toggle (now in the both-shifts handler), frontlight toggle (heart
  pad and Alt+B).
- `NodePrefs::notif_vibrate`, plus gating at the haptic / tone call sites in
  `UITask` (message notify) and the call ringtone.
- `TCA8418Keyboard`: Alt+Space → `KB_KEY_QUICK`.
- `main.cpp`: key, swipe, header-tap-on-home and heart long-press entry
  points; tile taps when `isOnQuickSettings()`.

Suggested order: screen + Alt+Space + `uigo quick` (BLE, WiFi, GPS, Light),
then 4G and Keys light, then Tones/Vibration gating, then touch entries.

## Open questions

1. Eighth tile: Keys light, or something else (antenna internal/external,
   dark mode, "Lock now")?
2. Should the lock screen allow quick settings (as phones do), or stay
   locked down?
3. Tones off: silence the call ringtone too, or only message tones?
4. Brightness: on/off only here, or Enter cycles 25/50/100 % and off?
5. When locked defaults: today's behaviour, or GPS and WiFi paused?

Mockups: Claude Design canvas "Meck — Quick settings" (both views, 2x).
