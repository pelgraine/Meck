## Meshcore + Fork = Meck

A fork created specifically to focus on enabling BLE & WiFi companion firmware for the LilyGo T-Deck Pro & LilyGo T5 E-Paper S3 Pro. Created with the assistance of Claude AI using Meshcore v1.11 code.

<img src="https://github.com/user-attachments/assets/b30ce6bd-79af-44d3-93c4-f5e7e21e5621" alt="IMG_1453" width="300" height="650">

### Contents
- [Supported Devices](#supported-devices)
- [Flashing Firmware](#flashing-firmware)
  - [First-Time Flash (Merged Firmware)](#first-time-flash-merged-firmware)
  - [Upgrading Firmware](#upgrading-firmware)
  - [SD Card Launcher](#sd-card-launcher)
- [Path Hash Mode (v0.9.9+)](#path-hash-mode-v099)
- [T-Deck Pro](#t-deck-pro)
  - [Build Variants](#t-deck-pro-build-variants)
  - [Keyboard Controls](#t-deck-pro-keyboard-controls)
  - [Navigation (Home Screen)](#navigation-home-screen)
  - [Bluetooth (BLE)](#bluetooth-ble)
  - [Clock & Timezone](#clock--timezone)
  - [Channel Message Screen](#channel-message-screen)
  - [Contacts Screen](#contacts-screen)
  - [Sending a Direct Message](#sending-a-direct-message)
  - [Repeater Admin Screen](#repeater-admin-screen)
  - [Settings Screen](#settings-screen)
  - [Compose Mode](#compose-mode)
  - [Symbol Entry (Sym Key)](#symbol-entry-sym-key)
  - [Emoji Picker](#emoji-picker)
  - [SMS & Phone App (4G only)](#sms--phone-app-4g-only)
  - [Web Browser & IRC](#web-browser--irc)
- [T5S3 E-Paper Pro](#t5s3-e-paper-pro)
  - [Build Variants](#t5s3-build-variants)
  - [Touch Navigation](#touch-navigation)
  - [Home Screen](#t5s3-home-screen)
  - [Boot Button Controls](#boot-button-controls)
  - [Backlight](#backlight)
  - [Lock Screen](#lock-screen)
  - [Virtual Keyboard](#virtual-keyboard)
  - [Display Settings](#display-settings)
  - [Clock & RTC](#clock--rtc)
  - [Touch Gestures by Screen](#touch-gestures-by-screen)
- [Serial Settings (USB)](Serial_Settings_Guide.md)
- [Text & EPUB Reader](TXT___EPUB_Reader_Guide.md)
- [Web Browser & IRC Guide](Web_App_Guide.md)
- [SMS & Phone App Guide](SMS___Phone_App_Guide.md)
- [About MeshCore](#about-meshcore)
- [What is MeshCore?](#what-is-meshcore)
- [Key Features](#key-features)
- [What Can You Use MeshCore For?](#what-can-you-use-meshcore-for)
- [How to Get Started](#how-to-get-started)
- [MeshCore Clients](#meshcore-clients)
- [Hardware Compatibility](#-hardware-compatibility)
- [Contributing](#contributing)
- [Road-Map / To-Do](#road-map--to-do)
- [Get Support](#-get-support)
- [License](#-license)
  - [Third-Party Libraries](#third-party-libraries)

---

## Supported Devices

Meck currently targets two LilyGo devices:

| Device | Display | Input | LoRa | Battery | GPS | RTC |
|--------|---------|-------|------|---------|-----|-----|
| **T-Deck Pro** | 240×320 e-ink (GxEPD2) | TCA8418 keyboard + optional touch | SX1262 | BQ27220 fuel gauge, 1400 mAh | Yes | No (uses GPS time) |
| **T5S3 E-Paper Pro** (V2, H752-B) | 960×540 e-ink (FastEPD, parallel) | GT911 capacitive touch (no keyboard) | SX1262 | BQ27220 fuel gauge, 1500 mAh | No (non-GPS variant) | Yes (PCF8563 hardware RTC) |

Both devices use the ESP32-S3 with 16 MB flash and 8 MB PSRAM.

---

## Flashing Firmware

Download the latest firmware from the [Releases](https://github.com/pelgraine/Meck/releases) page. Each release includes two types of `.bin` files per build variant:

| File Type | When to Use |
|-----------|-------------|
| `*_merged.bin` | **First-time flash** — includes bootloader, partition table, and firmware in a single file. Flash at address `0x0`. |
| `*.bin` (non-merged) | **Upgrading existing firmware** — firmware image only. Also used when loading firmware from an SD card via the Launcher. |

### First-Time Flash (Merged Firmware)

If the device has never had Meck firmware (or you want a clean start), use the **merged** `.bin` file. This contains the bootloader, partition table, and application firmware combined into a single image.

**Using esptool.py:**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 meck_t5s3_standalone_merged.bin
```

On macOS the port is typically `/dev/cu.usbmodem*`. On Windows it will be a COM port like `COM3`.

**Using the MeshCore Flasher (web-based, T-Deck Pro only):**

1. Go to https://flasher.meshcore.co.uk
2. Select **Custom Firmware**
3. Select the **merged** `.bin` file you downloaded
4. Click **Flash**, select your device in the popup, and click **Connect**

> **Note:** The MeshCore Flasher flashes at address `0x0` by default, so the merged file is the correct choice here for first-time flashes.

### Upgrading Firmware

If the device is already running Meck (or any MeshCore-based firmware with a valid bootloader), use the **non-merged** `.bin` file. This is smaller and faster to flash since it only contains the application firmware.

**Using esptool.py:**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 meck_t5s3_standalone.bin
```

> **Tip:** If you're unsure whether the device already has a bootloader, it's always safe to use the merged file and flash at `0x0` — it will overwrite everything cleanly.

### SD Card Launcher

If you're loading firmware from an SD card via the LilyGo Launcher firmware, use the **non-merged** `.bin` file. The Launcher provides its own bootloader and only needs the application image.

---

## Path Hash Mode (v0.9.9+)

Meck supports multibyte path hash, bringing it in line with MeshCore firmware v1.14. The path hash controls how many bytes each repeater uses to identify itself in forwarded flood packets. Larger hashes reduce the chance of identity collisions at the cost of fewer maximum hops per packet.

You can configure the path hash size in the device settings (press **S** from the home screen on T-Deck Pro, or open Settings via the tile on T5S3) or set it via USB serial:

```
set path.hash.mode 1
```

| Mode | Bytes per hop | Max hops | Notes |
|------|--------------|----------|-------|
| 0 | 1 | 64 | Legacy — prone to hash collisions in larger networks |
| 1 | 2 | 32 | Recommended — effectively eliminates collisions |
| 2 | 3 | 21 | Maximum precision, rarely needed |

Nodes with different path hash modes can coexist on the same network. The mode only affects packets your node originates — the hash size is encoded in each packet's header, so receiving nodes adapt automatically.

For a detailed explanation of what multibyte path hash means and why it matters, see the [Path Diagnostics & Improvements write-up](https://buymeacoffee.com/ripplebiz/path-diagnostics-improvements).

---

## T-Deck Pro

### T-Deck Pro Build Variants

| Variant | Environment | BLE | WiFi | 4G Modem | Audio DAC | Web Reader | Max Contacts |
|---------|------------|-----|------|----------|-----------|------------|-------------|
| Audio + BLE | `meck_audio_ble` | Yes | Yes (via BLE stack) | — | PCM5102A | Yes | 500 |
| Audio + Standalone | `meck_audio_standalone` | — | — | — | PCM5102A | No | 1,500 |
| 4G + BLE | `meck_4g_ble` | Yes | Yes | A7682E | — | Yes | 500 |
| 4G + Standalone | `meck_4g_standalone` | — | Yes | A7682E | — | Yes | 1,500 |

The audio DAC and 4G modem occupy the same hardware slot and are mutually exclusive.

### T-Deck Pro Keyboard Controls

The T-Deck Pro firmware includes full keyboard support for standalone messaging without a phone.

### Navigation (Home Screen)

| Key | Action |
|-----|--------|
| W / A | Previous page |
| D | Next page |
| Enter | Select / Confirm |
| M | Open channel messages |
| C | Open contacts list |
| E | Open e-book reader |
| N | Open notes |
| S | Open settings |
| B | Open web browser (BLE and 4G variants only) |
| T | Open SMS & Phone app (4G variant only) |
| P | Open audiobook player (audio variant only) |
| F | Open node discovery (search for nearby repeaters/nodes) |
| G | Open map screen (shows contacts with GPS positions) |
| Q | Back to home screen |

### Bluetooth (BLE)

BLE is **disabled by default** at boot to support standalone-first operation. The device is fully functional without a phone — you can send and receive messages, browse contacts, read e-books, and set your timezone directly from the keyboard.

To connect to the MeshCore companion app, navigate to the **Bluetooth** home page (use D to page through) and press **Enter** to toggle BLE on. The BLE PIN will be displayed on screen. Toggle it off again the same way when you're done.

### Clock & Timezone

The T-Deck Pro does not include a dedicated RTC chip, so after each reboot the device clock starts unset. The clock will appear in the nav bar (between node name and battery) once the time has been synced by one of two methods:

1. **GPS fix** (standalone) — Once the GPS acquires a satellite fix, the time is automatically synced from the NMEA data. No phone or BLE connection required. Typical time to first fix is 30–90 seconds outdoors with clear sky.
2. **BLE companion app** — If BLE is enabled and connected to the MeshCore companion app, the app will push the current time to the device.

**Setting your timezone:**

The UTC offset can be set from the **Settings** screen (press **S** from the home screen), or from the **GPS** home page by pressing **U** to open the UTC offset editor.

| Key | Action |
|-----|--------|
| W | Increase offset (+1 hour) |
| S | Decrease offset (-1 hour) |
| Enter | Save and exit |
| Q | Cancel and exit |

The UTC offset is persisted to flash and survives reboots — you only need to set it once. The valid range is UTC-12 to UTC+14. For example, AEST is UTC+10 and AEDT is UTC+11.

The GPS page also shows the current time, satellite count, position, altitude, and your configured UTC offset for reference.

### Channel Message Screen

| Key | Action |
|-----|--------|
| W / S | Scroll messages up/down |
| A / D | Switch between channels |
| Enter | Compose new message |
| R | Reply to a message — enter reply select mode, scroll to a message with W/S, then press Enter to compose a reply with an @mention |
| V | View relay path of the last received message (scrollable, up to 20 hops) |
| Q | Back to home screen |

### Contacts Screen

Press **C** from the home screen to open the contacts list. All known mesh contacts are shown sorted by most recently seen, with their type (Chat, Repeater, Room, Sensor), hop count, and time since last advert.

| Key | Action |
|-----|--------|
| W / S | Scroll up / down through contacts |
| A / D | Cycle filter: All → Chat → Repeater → Room → Sensor → Favourites |
| Enter | Open DM compose (Chat contact) or repeater admin (Repeater contact) |
| X | Export contacts to SD card (wait 5–10 seconds for confirmation popup) |
| R | Import contacts from SD card (wait 5–10 seconds for confirmation popup) |
| Q | Back to home screen |

**Contact limits:** Standalone variants support up to 1,500 contacts (stored in PSRAM). BLE variants (both Audio-BLE and 4G-BLE) are limited to 500 contacts due to BLE protocol constraints.

### Sending a Direct Message

Select a **Chat** contact in the contacts list and press **Enter** to start composing a direct message. The compose screen will show `DM: ContactName` in the header. Type your message and press **Enter** to send. The DM is sent encrypted directly to that contact (or flooded if no direct path is known). After sending or cancelling, you're returned to the contacts list.

### Repeater Admin Screen

Select a **Repeater** contact in the contacts list and press **Enter** to open the repeater admin screen. You'll be prompted for the repeater's admin password. Characters briefly appear as you type them before being masked, making it easier to enter symbols and numbers on the T-Deck Pro keyboard.

After a successful login, you'll see a menu with the following remote administration commands:

| Menu Item | Description |
|-----------|-------------|
| Clock Sync | Push your device's clock time to the repeater |
| Send Advert | Trigger the repeater to broadcast an advertisement |
| Neighbors | View other repeaters heard via zero-hop adverts |
| Get Clock | Read the repeater's current clock value |
| Version | Query the repeater's firmware version |
| Get Status | Retrieve repeater status information |

| Key | Action |
|-----|--------|
| W / S | Navigate menu items |
| Enter | Execute selected command |
| Q | Back to contacts (from menu) or cancel login |

Command responses are displayed in a scrollable view. Use **W / S** to scroll long responses and **Q** to return to the menu.

### Settings Screen

Press **S** from the home screen to open settings. On first boot (when the device name is still the default hex ID), the settings screen launches automatically as an onboarding wizard to set your device name and radio preset.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down through settings |
| Enter | Edit selected setting |
| Q | Back to home screen |

**Available settings:**

| Setting | Edit Method |
|---------|-------------|
| Device Name | Text entry — type a name, Enter to confirm |
| Radio Preset | A / D to cycle presets (MeshCore Default, Long Range, Fast/Short, EU Default), Enter to apply |
| Frequency | W / S to adjust, Enter to confirm |
| Bandwidth | W / S to cycle standard values (31.25 / 62.5 / 125 / 250 / 500 kHz), Enter to confirm |
| Spreading Factor | W / S to adjust (5–12), Enter to confirm |
| Coding Rate | W / S to adjust (5–8), Enter to confirm |
| TX Power | W / S to adjust (1–20 dBm), Enter to confirm |
| UTC Offset | W / S to adjust (-12 to +14), Enter to confirm |
| Path Hash Mode | A / D to cycle (0 = 1-byte, 1 = 2-byte, 2 = 3-byte), Enter to confirm |
| Channels | View existing channels, add hashtag channels, or delete non-primary channels (X) |
| Device Info | Public key and firmware version (read-only) |

The bottom of the settings screen also displays your node ID and firmware version. On the 4G variant, IMEI, carrier name, and APN details are shown here as well.

When adding a hashtag channel, type the channel name and press Enter. The channel secret is automatically derived from the name via SHA-256, matching the standard MeshCore hashtag convention.

If you've changed radio parameters, pressing Q will prompt you to apply changes before exiting.

> **Tip:** All device settings (plus mesh tuning parameters not available on-screen) can also be configured via USB serial. See the [Serial Settings Guide](Serial_Settings_Guide.md) for complete documentation.

### Compose Mode

| Key | Action |
|-----|--------|
| A / D | Switch destination channel (when message is empty, channel compose only) |
| Enter | Send message |
| Backspace | Delete last character |
| Shift + Backspace | Cancel and exit compose mode |

### Symbol Entry (Sym Key)

Press the **Sym** key then the letter key to enter numbers and symbols:

| Key | Sym+ | | Key | Sym+ | | Key | Sym+ |
|-----|------|-|-----|------|-|-----|------|
| Q | # | | A | * | | Z | 7 |
| W | 1 | | S | 4 | | X | 8 |
| E | 2 | | D | 5 | | C | 9 |
| R | 3 | | F | 6 | | V | ? |
| T | ( | | G | / | | B | ! |
| Y | ) | | H | : | | N | , |
| U | _ | | J | ; | | M | . |
| I | - | | K | ' | | Mic | 0 |
| O | + | | L | " | | $ | Emoji picker (Sym+$ for literal $) |
| P | @ | | | | | | |

### Other Keys

| Key | Action |
|-----|--------|
| Shift | Uppercase next letter |
| Alt | Same as Sym (for numbers/symbols) |
| Space | Space character / Next in navigation |

### Emoji Picker

While in compose mode, press the **$** key to open the emoji picker. A scrollable grid of 47 emoji is displayed in a 5-column layout.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down |
| A / D | Navigate left / right |
| Enter | Insert selected emoji |
| $ / Q / Backspace | Cancel and return to compose |

### SMS & Phone App (4G only)

Press **T** from the home screen to open the SMS & Phone app. The app opens to a menu screen where you can choose between the **Phone** dialer (for calling any number) or the **SMS Inbox** (for messaging and calling saved contacts).

For full documentation including key mappings, dialpad usage, contacts management, and troubleshooting, see the [SMS & Phone App Guide](SMS___Phone_App_Guide.md).

### Web Browser & IRC

Press **B** from the home screen to open the web reader. This is available on the BLE and 4G variants (not the standalone audio variant, which excludes WiFi to preserve lowest-battery-usage design).

The web reader home screen provides access to the **IRC client**, the **URL bar**, and your **bookmarks** and **history**. Select IRC Chat and press Enter to configure and connect to an IRC server. Select the URL bar to enter a web address, or scroll down to open a bookmark or history entry.

The browser is a text-centric reader best suited to text-heavy websites. It also includes basic web search via DuckDuckGo Lite, and can download EPUB files — follow a link to an `.epub` and it will be saved to the books folder on your SD card for reading later in the e-book reader.

For full documentation including key mappings, WiFi setup, bookmarks, IRC configuration, and SD card structure, see the [Web App Guide](Web_App_Guide.md).

---

## T5S3 E-Paper Pro

The LilyGo T5S3 E-Paper Pro (V2, H752-B) is a 4.7-inch e-ink device with capacitive touch and no physical keyboard. All navigation is done via touch gestures and the Boot button (GPIO0). The larger 960×540 display provides significantly more screen real estate than the T-Deck Pro's 240×320 panel.

### T5S3 Build Variants

| Variant | Environment | BLE | WiFi | Web Reader | Max Contacts |
|---------|------------|-----|------|------------|-------------|
| Standalone | `meck_t5s3_standalone` | — | — | No | 1,500 |
| BLE Companion | `meck_t5s3_ble` | Yes | — | No | 500 |
| WiFi Companion | `meck_t5s3_wifi` | — | Yes (TCP:5000) | Yes | 1,500 |

The WiFi variant connects to the MeshCore web app or meshcore.js over your local network. The web reader shares the same WiFi connection — no extra setup needed.

### Touch Navigation

The T5S3 uses a combination of touch gestures and the Boot button for all interaction. There is no physical keyboard — text entry uses an on-screen virtual keyboard that appears when needed.

**Core gesture types:**

| Gesture | Description |
|---------|-------------|
| **Tap** | Touch and release quickly. Context-dependent: opens tiles on home screen, selects items in lists, advances pages in readers. |
| **Swipe** | Touch, drag at least 60 pixels, and release. Direction determines action (scroll, page turn, switch channel/filter). |
| **Long press (touch)** | Touch and hold for 500ms+. Context-dependent: compose messages, open DMs, delete bookmarks. |

### T5S3 Home Screen

The home screen displays a 3×2 grid of tappable tiles:

| | Column 1 | Column 2 | Column 3 |
|---|----------|----------|----------|
| **Row 1** | Messages | Contacts | Settings |
| **Row 2** | Reader | Notes | Browser (WiFi) / Discover (other) |

Tap a tile to open that screen. Tap outside the tile grid (or swipe left/right) to cycle between home pages. The additional home pages show BLE status, battery info, GPS status, and a hibernate option — same as the T-Deck Pro but navigated by swiping or tapping the left/right halves of the screen instead of pressing keys.

### Boot Button Controls

The Boot button (GPIO0, bottom of device) provides essential navigation and utility functions:

| Action | Effect |
|--------|--------|
| **Single click** | On home screen: cycle to next page. On other screens: go back (same as pressing Q on T-Deck Pro). In text reader reading mode: close book and return to file list. |
| **Double-click** | Toggle backlight at full brightness (comfortable for indoor reading). |
| **Triple-click** | Toggle backlight at low brightness (dim nighttime reading). |
| **Long press** | Lock or unlock the screen. While locked, touch is disabled and a lock screen shows the time, battery percentage, and unread message count. |
| **Long press during first 8 seconds after boot** | Enter CLI rescue mode (serial settings interface). |

### Backlight

The T5S3 has a warm-tone front-light controlled by PWM on GPIO11. Brightness ranges from 0 (off) to 255 (maximum).

- **Double-click Boot button** — toggle backlight on at 153/255 brightness (comfortable reading level)
- **Triple-click Boot button** — toggle backlight on at low brightness (4/255, nighttime reading)
- The backlight turns off automatically when the screen locks

### Lock Screen

Long press the Boot button to lock the device. The lock screen shows:
- Current time in large text (HH:MM)
- Battery percentage
- Unread message count (if any)
- "Hold button to unlock" hint

Touch input is completely disabled while locked. Long press the Boot button again to unlock and return to whatever screen you were on.

### Virtual Keyboard

Since the T5S3 has no physical keyboard, a full-screen QWERTY virtual keyboard appears automatically when text input is needed (composing messages, entering WiFi passwords, editing settings, etc.).

The virtual keyboard supports:
- QWERTY letter layout with a symbol/number layer (tap the **123** key to switch)
- Shift toggle for uppercase
- Backspace and Enter keys
- Phantom keystroke prevention (a brief cooldown after the keyboard opens prevents accidental taps)

Tap keys to type. Tap **Enter** to submit, or press the **Boot button** to cancel and close the keyboard.

### Display Settings

The T5S3 Settings screen includes two additional options not available on the T-Deck Pro:

| Setting | Description |
|---------|-------------|
| **Dark Mode** | Inverts the display — white text on black background. Tap to toggle on/off. |
| **Portrait Mode** | Rotates the display 90° from landscape (960×540) to portrait (540×960). Touch coordinates are automatically remapped. Text reader layout recalculates on orientation change. |

These settings are persisted and survive reboots.

### Clock & RTC

Unlike the T-Deck Pro (which relies on GPS for time), the T5S3 has a hardware RTC (PCF8563/BM8563) that maintains time across reboots as long as the battery has charge. On first use (or after a full battery drain), the clock needs to be set via USB serial:

```
clock sync 1773554535
```

Where the number is a Unix epoch timestamp. Quick one-liner from a macOS/Linux terminal:

```
echo "clock sync $(date +%s)" > /dev/ttyACM0
```

Once set, the RTC retains the time across reboots. See the [Serial Settings Guide](Serial_Settings_Guide.md) for full clock sync documentation including the PlatformIO auto-sync feature.

The UTC offset is configured in the Settings screen (same as T-Deck Pro) and is persisted to flash.

### Touch Gestures by Screen

#### Home Screen

| Gesture | Action |
|---------|--------|
| Tap tile | Open that screen (Messages, Contacts, Settings, Reader, Notes, Browser/Discover) |
| Tap outside tiles (left half) | Previous home page |
| Tap outside tiles (right half) | Next home page |
| Swipe left / right | Next / previous home page |
| Long press (touch) | Activate current page action (toggle BLE, hibernate, etc.) |

#### Channel Messages

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll messages |
| Swipe left / right | Switch between channels |
| Tap footer area | View relay path of last received message |
| Tap path overlay | Dismiss overlay |
| Long press (touch) | Open virtual keyboard to compose message to current channel |

#### Contacts

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll through contacts |
| Swipe left / right | Cycle contact filter (All → Chat → Repeater → Room → Sensor → Favourites) |
| Tap | Select contact |
| Long press on Chat contact | Open virtual keyboard to compose DM |
| Long press on Repeater contact | Open repeater admin login |

#### Text Reader (File List)

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll file list |
| Tap | Open selected book |

#### Text Reader (Reading)

| Gesture | Action |
|---------|--------|
| Tap anywhere | Next page |
| Swipe left | Next page |
| Swipe right | Previous page |
| Swipe up / down | Next / previous page |
| Long press (touch) | Close book, return to file list |
| Tap status bar | Go to home screen |

#### Web Reader (WiFi variant)

| Gesture | Action |
|---------|--------|
| Tap URL bar | Open virtual keyboard for URL entry |
| Tap Search | Open virtual keyboard for DuckDuckGo search |
| Tap reading area | Next page |
| Tap footer (if links exist) | Open virtual keyboard to enter link number |
| Swipe left / right (reading) | Next / previous page |
| Swipe up / down (home/lists) | Scroll list |
| Long press (reading) | Navigate back |
| Long press on bookmark | Delete bookmark |
| Long press on home | Exit web reader |

#### Settings

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll through settings |
| Swipe left / right | Adjust value (same as A/D keys on T-Deck Pro) |
| Tap | Toggle or edit selected setting |

#### Notes

| Gesture | Action |
|---------|--------|
| Tap (while editing) | Open virtual keyboard for text entry |
| Long press (while editing) | Save note and exit editor |

#### Discovery

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll node list |
| Long press | Rescan for nodes |

#### Repeater Admin

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll menu / response |
| Long press (password entry) | Open virtual keyboard for admin password |

#### All Screens

| Gesture | Action |
|---------|--------|
| Tap status bar (top of screen) | Return to home screen (except in text reader reading mode, where it advances the page) |

---

## About MeshCore

MeshCore is a lightweight, portable C++ library that enables multi-hop packet routing for embedded projects using LoRa and other packet radios. It is designed for developers who want to create resilient, decentralized communication networks that work without the internet.

## What is MeshCore?

MeshCore now supports a range of LoRa devices, allowing for easy flashing without the need to compile firmware manually. Users can flash a pre-built binary using tools like esptool.py and interact with the network through a serial console.
MeshCore provides the ability to create wireless mesh networks, similar to Meshtastic and Reticulum but with a focus on lightweight multi-hop packet routing for embedded projects. Unlike Meshtastic, which is tailored for casual LoRa communication, or Reticulum, which offers advanced networking, MeshCore balances simplicity with scalability, making it ideal for custom embedded solutions, where devices (nodes) can communicate over long distances by relaying messages through intermediate nodes. This is especially useful in off-grid, emergency, or tactical situations where traditional communication infrastructure is unavailable.

## Key Features

* Multi-Hop Packet Routing
  * Devices can forward messages across multiple nodes, extending range beyond a single radio's reach.
  * Supports up to a configurable number of hops to balance network efficiency and prevent excessive traffic.
  * Nodes use fixed roles where "Companion" nodes are not repeating messages at all to prevent adverse routing paths from being used.
* Supports LoRa Radios — Works with Heltec, RAK Wireless, and other LoRa-based hardware.
* Decentralized & Resilient — No central server or internet required; the network is self-healing.
* Low Power Consumption — Ideal for battery-powered or solar-powered devices.
* Simple to Deploy — Pre-built example applications make it easy to get started.

## What Can You Use MeshCore For?

* Off-Grid Communication: Stay connected even in remote areas.
* Emergency Response & Disaster Recovery: Set up instant networks where infrastructure is down.
* Outdoor Activities: Hiking, camping, and adventure racing communication.
* Tactical & Security Applications: Military, law enforcement, and private security use cases.
* IoT & Sensor Networks: Collect data from remote sensors and relay it back to a central location.

## How to Get Started

- Watch the [MeshCore Intro Video](https://www.youtube.com/watch?v=t1qne8uJBAc) by Andy Kirby.
- Read through our [Frequently Asked Questions](./docs/faq.md) section.
- Download firmware from the [Releases](https://github.com/pelgraine/Meck/releases) page and flash it using the instructions above.
- Connect with a supported client.

For developers:

- Install [PlatformIO](https://docs.platformio.org) in [Visual Studio Code](https://code.visualstudio.com).
- Clone and open the Meck repository in Visual Studio Code.
- Build for your target device using the environment names listed in the build variant tables above.

## MeshCore Clients

**Companion Firmware**

The companion firmware can be connected to via BLE (T-Deck Pro and T5S3 BLE variants) or WiFi (T5S3 WiFi variant, TCP port 5000).

> **Note:** On both the T-Deck Pro and T5S3, BLE is disabled by default at boot. On the T-Deck Pro, navigate to the Bluetooth home page and press Enter to enable BLE. On the T5S3, navigate to the Bluetooth home page and long-press the screen to toggle BLE on.

- Web: https://app.meshcore.nz
- Android: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- iOS: https://apps.apple.com/us/app/meshcore/id6742354151?platform=iphone
- NodeJS: https://github.com/liamcottle/meshcore.js
- Python: https://github.com/fdlamotte/meshcore-cli

## 🛠 Hardware Compatibility

MeshCore is designed for devices listed in the [MeshCore Flasher](https://flasher.meshcore.co.uk). Meck specifically targets the LilyGo T-Deck Pro and LilyGo T5S3 E-Paper Pro.

## Contributing

Please submit PR's using 'dev' as the base branch!
For minor changes just submit your PR and I'll try to review it, but for anything more 'impactful' please open an Issue first and start a discussion. Is better to sound out what it is you want to achieve first, and try to come to a consensus on what the best approach is, especially when it impacts the structure or architecture of this codebase.

Here are some general principals you should try to adhere to:
* Keep it simple. Please, don't think like a high-level lang programmer. Think embedded, and keep code concise, without any unnecessary layers.
* No dynamic memory allocation, except during setup/begin functions.
* Use the same brace and indenting style that's in the core source modules. (A .clang-format is prob going to be added soon, but please do NOT retroactively re-format existing code. This just creates unnecessary diffs that make finding problems harder)

## Road-Map / To-Do

There are a number of fairly major features in the pipeline, with no particular time-frames attached yet. In partly chronological order:

**T-Deck Pro:**
- [X] Companion radio: BLE
- [X] Text entry for Public channel messages Companion BLE firmware
- [X] View and compose all channel messages Companion BLE firmware
- [X] Standalone DM functionality for Companion BLE firmware
- [X] Contacts list with filtering for Companion BLE firmware
- [X] Standalone repeater admin access for Companion BLE firmware
- [X] GPS time sync with on-device timezone setting
- [X] Settings screen with radio presets, channel management, and first-boot onboarding
- [X] Expand SMS app to enable phone calls
- [X] Basic web reader app with IRC client
- [ ] Fix M4B rendering to enable chaptered audiobook playback
- [ ] Better JPEG and PNG decoding
- [ ] Improve EPUB rendering and EPUB format handling
- [ ] Map support with GPS
- [ ] WiFi companion environment

**T5S3 E-Paper Pro:**
- [X] Core port: display, touch input, LoRa, battery, RTC
- [X] Touch-navigable home screen with tappable tile grid
- [X] Full virtual keyboard for text entry
- [X] Lock screen with clock, battery, and unread count
- [X] Backlight control (double/triple-click Boot button)
- [X] Dark mode and portrait mode display settings
- [X] Channel messages with swipe navigation and touch compose
- [X] Contacts with filter cycling and long-press DM/admin
- [X] Text reader with swipe page turns
- [X] Web reader with virtual keyboard URL/search entry (WiFi variant)
- [X] Settings screen with touch editing
- [X] Serial clock sync for hardware RTC
- [ ] Emoji sprites on home tiles
- [ ] Portrait mode toggle via quadruple-click Boot button
- [ ] Hibernate should auto-off backlight

## 📞 Get Support

- Join [MeshCore Discord](https://discord.gg/BMwCtwHj5V) to chat with the developers and get help from the community.

## 📜 License

The upstream [MeshCore](https://github.com/meshcore-dev/MeshCore) library is released under the **MIT License** (Copyright © 2025 Scott Powell / rippleradios.com). Meck-specific code (UI screens, display helpers, device integration) is also provided under the MIT License.

However, this firmware links against libraries with different license terms. Because two dependencies use the **GPL-3.0** copyleft license, the combined firmware binary is effectively subject to GPL-3.0 obligations when distributed. Please review the individual licenses below if you intend to redistribute or modify this firmware.

### Third-Party Libraries

| Library | License | Author / Source |
|---------|---------|-----------------|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | Scott Powell / rippleradios.com |
| [GxEPD2](https://github.com/ZinggJM/GxEPD2) | GPL-3.0 | Jean-Marc Zingg |
| [FastEPD](https://github.com/bitbank2/FastEPD) | Apache-2.0 | Larry Bank (bitbank2) |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | GPL-3.0 | schreibfaul1 (Wolle) |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | BSD | Adafruit |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | Jan Gromeš |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | MIT | Lewis He |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) | Apache-2.0 | Larry Bank (bitbank2) |
| [PNGdec](https://github.com/bitbank2/PNGdec) | Apache-2.0 | Larry Bank (bitbank2) |
| [CRC32](https://github.com/bakercp/CRC32) | MIT | Christopher Baker |
| [base64](https://github.com/Densaugeo/base64_arduino) | MIT | densaugeo |
| [Arduino Crypto](https://github.com/rweather/arduinolibs) | MIT | Rhys Weatherley |

Full license texts for each dependency are available in their respective repositories linked above.