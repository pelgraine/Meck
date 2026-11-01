# Meck fork (halecivo) — working notes for Claude Code

Meck is a MeshCore companion firmware for LilyGo T-Deck devices. This fork
adds work for the **T-Deck MAX** (ESP32-S3, 8 MB PSRAM, e-ink 240x320,
SX1262, A7682E modem, ES8311 audio): the new UI (UI v2), power saving when
locked, Bluetooth + WiFi at the same time, and more.

## Language

- Code, comments, commit messages and docs: **English**.
- Talk to the user in **Czech**.
- Match the surrounding code: comment density, naming, idiom. Comments say
  why, not what.

## Branches

- `main` mirrors upstream (`upstream` = pelgraine/Meck). Only fast-forward it
  to `upstream/main` and push to `origin`; never commit to it.
- `next` is the fork's default branch: integration of finished, tested work.
  CI builds it and publishes the rolling "Latest build (next)" pre-release.
- `testing`: `next` plus features that still need testing on a device.
  Nothing goes to `next` without having been tried.
- Feature work happens on its own branch, in its own git worktree, based on
  `next` (or on `testing` when it builds on something there).
- Upstream PRs are made from branches based on `main`, without UI v2.
- Merging upstream into `next`: `git merge main` (rerere is on), resolve,
  build the MAX and Pro envs, then push.

## Building

- PlatformIO: `pio run -e <env>`; upload with `-t upload --upload-port COM3`.
- Main envs: `meck_max_ui2` (new UI, BLE + WiFi, debug CLI), `meck_max_ble_wifi`
  (classic UI), `meck_max_standalone`; T-Deck Pro: `meck_audio_ble_wifi`,
  `meck_4g_ble_wifi`. Build the ones your change touches before committing.
- `FIRMWARE_VERSION` must stay at most 19 characters (the app's 20-byte field).
- Windows shell quirks: Python is `py -3` (`python` is Python 2). In Git Bash
  set `MSYS_NO_PATHCONV=1` when passing device paths like `/tiles`, or Git
  Bash rewrites them into Windows paths.

## The device (one T-Deck MAX on COM3)

Before touching it — flashing, serial commands, screenshots — take the lock:

    py -3 tools/device_lock.py status
    py -3 tools/device_lock.py acquire --task "what you are testing"
    py -3 tools/device_lock.py release        # when done

A PreToolUse hook (`.claude/settings.json`) also takes it automatically for
device commands and blocks them while another session holds it. If it is
busy, wait or ask the user; never `release --force` someone else's lock
unless the user says that session is gone.

Rules on the device (the user's own deck, with their contacts and channels):

- **Never transmit**: no messages, adverts, DMs, calls, location or
  telemetry sends during tests. Read-only commands only, unless the user
  asks for a send.
- **No sounds, vibration or flashing light** during tests.
- Take a screenshot before pressing Enter (`tools/ui2/press_keys.py --shot`).
- Never drive a logic 1 into a switched-off chip (check the schematic:
  Xinyuan-LilyGO/T-Deck-MAX, hardware/T-Deck Pro Max V0.1 25-09-11.pdf).
- After flashing, check the output says "Hash of data verified" before
  testing.
- Don't add test messages to the user's chat history; it is persisted.
- Don't change the user's settings or contacts to test something unless
  they agree, and restore them afterwards.
- No device work when the user says the deck is not connected.

Useful tools (debug CLI exists in `meck_max_ui2`):

- Screenshot: `py -3 tools/ui2/grab_screen.py COM3 out.png --scale 2`
- Keys: `py -3 tools/ui2/press_keys.py COM3 d d enter --shot out.png`
  (`key enter/esc/bs/space` on the serial CLI)
- Serial CLI: `screendump`, `key <k>`, `uigo <screen>` (`lock`, `unlock`,
  `home`, `settings`, `chat`…), `pwr*` power commands, `pwrlog [sd]`,
  `uigeo <text>` (location parser), `clock`.
- Power log viewer: `py -3 tools/powerlog/powerlog.py` (pull, history in
  `powerlogs/` of the main checkout, not in git; see `tools/powerlog/README.md`).
- Open the port without DTR/RTS (they reset the ESP32-S3).
- USB stops working after the deck has been in light sleep (locked on
  battery); only the reset button brings it back. Ask the user.
- Boot loop after an interrupted flash: user holds the top button and taps
  reset, then `esptool --before no_reset --after no_reset write_flash ...`.

## Map tiles and SD

- `tools/maps/render_tiles.py` renders black-and-white hiking tiles for an
  area from OpenStreetMap and Terrarium elevation (on branch `map-tiles`).
- Rendered tiles live in `maps-out/` of the main checkout (not in git).
- SD upload over USB (`sdrecv`, `tools/sd_upload.py`) is on branch
  `sd-upload` only.

## Notes and memory

Longer findings (power test results, upstream PR plan, flash recovery) are
in Claude's project memory. Docs for features go to `docs/`.
