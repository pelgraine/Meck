## Meshcore + Fork = Meck

A fork created specifically to focus on enabling BLE & WiFi companion firmware for the LilyGo T-Deck Pro & LilyGo T5 E-Paper S3 Pro. Created wholly with Claude AI using Meshcore v1.11 code. 100% vibecoded.

[Check out the Meck discussion channel on the MeshCore Discord](https://discord.com/channels/1343693475589263471/1460136499390447670)

<img src="https://github.com/user-attachments/assets/b30ce6bd-79af-44d3-93c4-f5e7e21e5621" alt="IMG_1453" width="300" height="650">

### Contents
- [Supported Devices](#supported-devices)
- [SD Card Requirements](#sd-card-requirements)
- [Flashing Firmware](#flashing-firmware)
  - [First-Time Flash (Merged Firmware)](#first-time-flash-merged-firmware)
  - [Upgrading Firmware](#upgrading-firmware)
  - [Launcher](#launcher)
  - [OTA Firmware Update](#ota-firmware-update-v13)
- [Path Hash Mode (v0.9.9+)](#path-hash-mode-v099)
- [T-Deck Pro](#t-deck-pro)
  - [Build Variants](#t-deck-pro-build-variants)
  - [Keyboard Controls](#t-deck-pro-keyboard-controls)
  - [Navigation (Home Screen)](#navigation-home-screen)
  - [Bluetooth (BLE)](#bluetooth-ble)
  - [WiFi Companion](#wifi-companion)
  - [Clock & Timezone](#clock--timezone)
  - [Channel Message Screen](#channel-message-screen)
  - [Contacts Screen](#contacts-screen)
  - [Sending a Direct Message](#sending-a-direct-message)
  - [Roomservers](#roomservers)
  - [Repeater Admin Screen](#repeater-admin-screen)
  - [Settings Screen](#settings-screen)
  - [Compose Mode](#compose-mode)
  - [Symbol Entry (Sym Key)](#symbol-entry-sym-key)
  - [Emoji Picker](#emoji-picker)
  - [SMS & Phone App (4G only)](#sms--phone-app-4g-only)
  - [Web Browser & IRC](#web-browser--irc)
  - [Alarm Clock (Audio only)](#alarm-clock-audio-only)
  - [Lock Screen (T-Deck Pro)](#lock-screen-t-deck-pro)
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

## SD Card Requirements

**An SD card is essential for Meck to function properly.** Many features — including the e-book reader, notes, bookmarks, web reader cache, audiobook playback, firmware updates, contact import/export, and WiFi credential storage — rely on files stored on the SD card. Without an SD card inserted, the device will boot and handle mesh messaging, but most extended features will be unavailable or will fail silently.

**Recommended:** A **32 GB or larger** microSD card formatted as **FAT32**. MeshCore users have found that **SanDisk** microSD cards are the most reliable across both the T-Deck Pro and T5S3.

---

## Flashing Firmware

Download the latest firmware from the [Releases](https://github.com/pelgraine/Meck/releases) page. Each release includes two types of `.bin` files per build variant:

| File Type | When to Use |
|-----------|-------------|
| `*-merged.bin` | **First-time flash** — includes bootloader, partition table, and firmware in a single file. Flash at address `0x0`. |
| `*.bin` (non-merged) | **Upgrading existing firmware** — firmware image only. Also used when loading firmware from an SD card via the Launcher. |

### First-Time Flash (Merged Firmware)

If the device has never had Meck firmware (or you want a clean start), use the **merged** `.bin` file. This contains the bootloader, partition table, and application firmware combined into a single image.

**Using esptool.py:**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x0 meck_t5s3_standalone-merged.bin
```

On macOS the port is typically `/dev/cu.usbmodem*`. On Windows it will be a COM port like `COM3`.

**Using the MeshCore Flasher (web-based, T-Deck Pro only):**

1. Go to https://flasher.meshcore.co.uk
2. Select **Custom Firmware**
3. Select the **merged** `.bin` file you downloaded
4. Click **Flash**, select your device in the popup, and click **Connect**

> **Note:** The MeshCore Flasher detects merged firmware by the `-merged.bin` suffix in the filename and automatically flashes at address `0x0`. If the filename doesn't end with `-merged.bin`, the flasher writes at `0x10000` instead, which will fail on a clean device.

### Upgrading Firmware

If the device is already running Meck (or any MeshCore-based firmware with a valid bootloader), use the **non-merged** `.bin` file. This is smaller and faster to flash since it only contains the application firmware.

**Using esptool.py:**

```
esptool.py --chip esp32s3 --port /dev/ttyACM0 --baud 921600 \
  write_flash 0x10000 meck_t5s3_standalone.bin
```

> **Tip:** If you're unsure whether the device already has a bootloader, it's always safe to use the merged file and flash at `0x0` — it will overwrite everything cleanly.

### Launcher

If you're loading firmware from an SD card via the LilyGo Launcher firmware, use the **non-merged** `.bin` file. The Launcher provides its own bootloader and only needs the application image.

### OTA Firmware Update (v1.3+)

Once Meck is installed, you can update firmware directly from your phone — no computer or serial cable required. The device creates a temporary WiFi access point and you upload the new `.bin` via your phone's browser.

1. Download the new **non-merged** `.bin` to your phone (from GitHub Releases, Discord, etc.)
2. On the device: **Settings → OTA Tools → Firmware Update → Enter** (T-Deck Pro) or **tap** (T5S3)
3. The device starts a WiFi network called `Meck-Update-XXXX` and displays connection details
4. On your phone: connect to the `Meck-Update` WiFi network, open a browser, go to `192.168.4.1`
5. Tap **Choose File**, select the `.bin`, tap **Upload**
6. The device receives the file, saves to SD, verifies, flashes, and reboots

The partition layout supports dual OTA slots — the old firmware remains on the inactive partition as an automatic rollback target. If the new firmware fails to boot, the ESP32 bootloader reverts to the previous working version automatically.

> **Note:** Use the **non-merged** `.bin` for OTA updates. The merged binary is only needed for first-time USB flashing.

**OTA Tools (v1.5+):** The firmware update has moved into **Settings → OTA Tools**, a submenu that also contains the new **SD File Manager**. The file manager creates the same WiFi access point and serves a browser-based interface where you can browse, upload, download, and delete files on the SD card from your phone — useful for managing audiobooks, alarm sounds, e-books, and notes without ejecting the SD card. Both OTA tools work on all variants including standalone builds.

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
| Audio + WiFi | `meck_audio_wifi` | — | Yes (TCP:5000) | — | PCM5102A | Yes | 1,500 |
| Audio + Standalone | `meck_audio_standalone` | — | — | — | PCM5102A | No | 1,500 |
| 4G + BLE | `meck_4g_ble` | Yes | Yes | A7682E | — | Yes | 500 |
| 4G + WiFi | `meck_4g_wifi` | — | Yes (TCP:5000) | A7682E | — | Yes | 1,500 |
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
| K | Open alarm clock (audio variant only) |
| F | Open node discovery (search for nearby repeaters/nodes) |
| H | Open last heard list (passive advert history) |
| G | Open map screen (shows contacts with GPS positions) |
| Q | Back to home screen |
| Double-click Boot | Lock / unlock screen |

### Bluetooth (BLE)

BLE is **disabled by default** at boot to support standalone-first operation. The device is fully functional without a phone — you can send and receive messages, browse contacts, read e-books, and set your timezone directly from the keyboard.

To connect to the MeshCore companion app, navigate to the **Bluetooth** home page (use D to page through) and press **Enter** to toggle BLE on. The BLE PIN will be displayed on screen. Toggle it off again the same way when you're done.

### WiFi Companion

The WiFi companion variants (`meck_audio_wifi`, `meck_4g_wifi`) connect to the MeshCore web app, meshcore.js, or Python CLI over your local network via TCP on port 5000. WiFi credentials are stored on the SD card at `/web/wifi.cfg`.

**Connecting:**

1. Navigate to the **WiFi** home page (use D to page through)
2. Press **Enter** to toggle WiFi on
3. The device scans for networks — select yours and enter the password
4. Once connected, the IP address is displayed on the WiFi home page

Connect the MeshCore web app or meshcore.js to `<device IP>:5000`.

WiFi is also used by the web reader and IRC client on WiFi variants. The web reader shares the same connection — no extra setup needed.

> **Tip:** WiFi variants support up to 1,500 contacts (vs 500 for BLE variants) because they are not constrained by the BLE protocol ceiling.

### Clock & Timezone

The T-Deck Pro does not include a dedicated RTC chip, so after each reboot the device clock starts unset. The clock will appear in the nav bar (between node name and battery) once the time has been synced by one of these methods:

1. **GPS fix** (standalone) — Once the GPS acquires a satellite fix, the time is automatically synced from the NMEA data. No phone or BLE connection required. Typical time to first fix is 30–90 seconds outdoors with clear sky.
2. **BLE/WiFi companion app** — If connected to the MeshCore companion app (via BLE or WiFi), the app will push the current time to the device.

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
| A / D | Switch between channels (press D past the last channel to reach the DM inbox, A to return) |
| Enter | Compose new message |
| R | Reply to a message — enter reply select mode, scroll to a message with W/S, then press Enter to compose a reply with an @mention |
| V | View relay path of the last received message (scrollable, up to 20 hops) |
| Q | Back to home screen |

### Contacts Screen

Press **C** from the home screen to open the contacts list. All known mesh contacts are shown sorted by most recently heard, with their type prefix, estimated hop count, and time since last advert.

**Contact type prefixes**

| Prefix | Type |
|--------|------|
| C | Chat node |
| R | Repeater |
| RS | Room server |
| ? | Unknown / sensor |

**Hop count display**

| Display | Meaning |
|---------|---------|
| `D` | Direct path known (path exchange completed) |
| `D*` | Direct path, manually locked |
| `N` | N-hop path known (e.g. `2` = 2 hops) |
| `N*` | N-hop path, manually locked |
| `~D` | Heard direct via flood advert (no path exchange yet) |
| `~N` | Estimated N hops via flood advert |
| `?` | No path information available |

Flood-based hop estimates (`~D`, `~N`) are shown for the 12 most recently heard contacts and reset to `?` on reboot until each contact re-advertises. Confirmed path values (`D`, `N`) persist until overwritten by a new path exchange.

**Normal mode controls**

| Key | Action |
|-----|--------|
| W / S | Scroll up / down through contacts |
| A / D | Cycle filter: All → Chat → Rptr → Room → Sens → Fav |
| Enter | Enter select mode (highlights current contact, enables batch operations) |
| P | Open path editor for the highlighted contact |
| Q | Back to home screen |

> **Note:** The **Fav** filter shows only contacts you have marked as favourites. If it appears empty, no contacts have been favourited yet — use select mode (Enter) and then **F** to mark contacts.

**Select mode** — press Enter from the contacts list to enter select mode. The highlighted contact is pre-selected. Use W/S to scroll and Enter to toggle selection on any row.

| Key | Action |
|-----|--------|
| W / S | Scroll up / down |
| Enter | Toggle selection on current contact |
| A | Select all contacts in current filter |
| D | Deselect all |
| F | Toggle favourite on all selected contacts |
| X | Export selected contacts to SD card |
| Backspace | Delete selected contacts |
| Q | Exit select mode |

**Adding contacts**

Contacts can be added three ways:

1. **Automatic** — if Settings → Contacts → Add Mode is set to *Auto All*, any node whose advert is heard is added automatically. *Custom* mode adds only nodes matching the enabled type toggles (Companion, Repeater, Room Server, Sensor) — each toggle controls whether receiving an advert of that type triggers an auto-add. *Manual Only* disables all auto-add.

2. **From the Last Heard screen** — press **H** from the home screen to open the last-heard advert list. Scroll to the node you want and press **Enter** (or tap the row) to add it to contacts. Press **Enter** again on an existing contact to remove it (favourites require a second press within 3 seconds to confirm). Entries show `[+]` if already in contacts, `[★]` if a favourite.

   > **Note:** The Last Heard list holds up to 1,000 entries in PSRAM, and advert data is stored persistently on the SD card — so contacts can be added long after the original advertisement was received, even across reboots. This makes Last Heard especially useful when auto-add is set to *Manual Only*, as it provides a passive catalogue of every node heard on the network.

3. **From the Discovery screen** — press **F** from the home screen to run an active discovery scan. Nodes that respond appear in a list; press **Enter** on any entry to add it to contacts.

**Deleting contacts**

Enter select mode (Enter), select the contacts to remove (Enter to toggle, A to select all), then press **Backspace** to delete. You will be returned to the contacts list once the deletion is complete.

**Contact limits:** Standalone and WiFi variants support up to 1,500 contacts (stored in PSRAM). BLE variants are limited to 500 contacts due to BLE protocol constraints.

### Sending a Direct Message

Select a **Chat** contact in the contacts list and press **Enter** to start composing a direct message. The compose screen will show `DM: ContactName` in the header. Type your message and press **Enter** to send. The DM is sent encrypted directly to that contact (or flooded if no direct path is known). After sending or cancelling, you're returned to the contacts list.

Contacts with unread direct messages show a `*` marker next to their name in the contacts list.

**Reading received DMs:** On the Channel Messages screen, press **D** past the last group channel to reach the **DM inbox**. This shows all received direct messages with sender name and timestamp. Entering the DM inbox marks all DM messages as read and clears the unread indicator. Press **A** to return to group channels.

### Roomservers

Room servers are MeshCore nodes that host persistent chat rooms. Messages sent to a room server are stored and relayed to anyone who logs in. In Meck, room server messages arrive as contact messages and appear in the DM inbox alongside regular direct messages.

To interact with a room server, navigate to the Contacts screen, filter to **Room** contacts, select the room, and press **Enter** to open the Repeater Admin screen. Log in with the room's admin password to access room administration. On successful login, all unread messages from that room are automatically marked as read.

Room server messages are also synced to the companion app when connected via BLE or WiFi — the companion app will pull and display them alongside other messages.

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
| Enter | Edit selected setting, or enter a sub-screen |
| Q | Back one level (sub-screen → top level → home screen) |

**Available settings:**

| Setting | Edit Method |
|---------|-------------|
| Device Name | Text entry — type a name, Enter to confirm |
| Radio Preset | A / D to cycle presets (MeshCore Default, Long Range, Fast/Short, EU Default), Enter to apply |
| Frequency | Text entry — type exact value (e.g. 916.575), Enter to confirm |
| Bandwidth | W / S to cycle standard values (31.25 / 62.5 / 125 / 250 / 500 kHz), Enter to confirm |
| Spreading Factor | W / S to adjust (5–12), Enter to confirm |
| Coding Rate | W / S to adjust (5–8), Enter to confirm |
| TX Power | W / S to adjust (1–20 dBm), Enter to confirm |
| UTC Offset | W / S to adjust (-12 to +14), Enter to confirm |
| Msg Rcvd LED Pulse | Toggle keyboard backlight flash on new message (Enter to toggle) |
| GPS Baud Rate | A / D to cycle (Default 38400 / 4800 / 9600 / 19200 / 38400 / 57600 / 115200), Enter to confirm. **Requires reboot to take effect.** |
| Path Hash Mode | W / S to cycle (1-byte / 2-byte / 3-byte), Enter to confirm |
| Dark Mode | Toggle inverted display — white text on black background (Enter to toggle) |
| Larger Font | Toggle larger text size on channel messages, contacts, DM inbox, and repeater admin screens (Enter to toggle) |
| Auto Lock | A / D to cycle timeout (None / 2 / 5 / 10 / 15 / 30 min), Enter to confirm |
| Contacts >> | Opens the Contacts sub-screen (see below) |
| Channels >> | Opens the Channels sub-screen (see below) |
| Device Info | Public key and firmware version (read-only) |

**Contacts sub-screen** — press Enter on the `Contacts >>` row to open. Contains the contact auto-add mode picker and, when set to Custom, per-type toggles:

| Toggle | Meaning when ON |
|--------|----------------|
| Companion | Auto-add a chat node when its advert is heard |
| Repeater | Auto-add repeaters heard via advert |
| Room Server | Auto-add room servers heard via advert |
| Sensor | Auto-add sensor nodes heard via advert |
| Overwrite Oldest | When the contact list is full, overwrite the oldest non-favourite entry instead of discarding the new contact |

Press Q to return to the top-level settings list.

**Channels sub-screen** — press Enter on the `Channels >>` row to open. Lists all current channels, with an option to add hashtag channels or delete non-primary channels (X). Press Q to return to the top-level settings list.

The top-level settings screen also displays your node ID and firmware version. On the 4G variant, IMEI, carrier name, and APN details are shown here as well.

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

Press **B** from the home screen to open the web reader. This is available on the BLE, WiFi, and 4G variants (not the standalone audio variant, which excludes WiFi to preserve lowest-battery-usage design).

The web reader home screen provides access to the **IRC client**, the **URL bar**, and your **bookmarks** and **history**. Select IRC Chat and press Enter to configure and connect to an IRC server. Select the URL bar to enter a web address, or scroll down to open a bookmark or history entry.

The browser is a text-centric reader best suited to text-heavy websites. It also includes basic web search via DuckDuckGo Lite, and can download EPUB files — follow a link to an `.epub` and it will be saved to the books folder on your SD card for reading later in the e-book reader.

For full documentation including key mappings, WiFi setup, bookmarks, IRC configuration, and SD card structure, see the [Web App Guide](Web_App_Guide.md).

### Alarm Clock (Audio only)

Press **K** from the home screen to open the alarm clock. This is available on the audio variant of the T-Deck Pro (PCM5102A DAC). Set up to five daily alarms that play custom MP3 files through the headphone jack.

**Setup:**

1. Place MP3 files (44100 Hz sample rate) in `/alarms/` on the SD card
2. Press **K** to open the alarm clock
3. Select an alarm slot (1–5) with **W / S** and press **Enter** to edit
4. Set the hour and minute, then choose an MP3 file from the list
5. Press **Enter** to save the alarm

| Key | Action |
|-----|--------|
| W / S | Navigate alarm slots / adjust time |
| A / D | Switch between hour and minute fields |
| Enter | Edit slot / save alarm / select MP3 |
| X | Delete selected alarm |
| Q | Back to home screen |

**When an alarm fires:**

The selected MP3 plays through the headphone jack, even if you're on another screen or playing an audiobook.

| Key | Action |
|-----|--------|
| Z | Snooze for 5 minutes |
| Any other key | Dismiss alarm |

Alarm configuration is stored in `/alarms/.alarmcfg` on the SD card. Alarms persist across reboots — if the RTC has valid time (via GPS or companion app sync), alarms fire at the correct time after a restart.

> **Note:** MP3 files should be encoded at **44100 Hz** sample rate. Lower sample rates may cause distortion due to ESP32-S3 I2S hardware limitations (same requirement as the audiobook player).

**SD Card Folder Structure:**

```
SD Card
├── alarms/
│   ├── .alarmcfg             (auto-created, stores alarm slot config)
│   ├── morning-chime.mp3
│   ├── rooster.mp3
│   └── gentle-bells.mp3
├── audiobooks/               (existing — audiobook player)
│   └── ...
├── books/                    (existing — text reader)
│   └── ...
└── ...
```

### Lock Screen (T-Deck Pro)

Double-click the Boot button to lock the screen. The lock screen shows the current time, battery percentage, and unread message count. The CPU drops to 40 MHz while locked to reduce power consumption.

Double-click the Boot button again to unlock and return to whatever screen you were on.

An auto-lock timer can be configured in **Settings → Auto Lock** (None / 2 / 5 / 10 / 15 / 30 minutes of idle time).

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

An auto-lock timer can be configured in **Settings → Auto Lock** (None / 2 / 5 / 10 / 15 / 30 minutes of idle time). The CPU drops to 40 MHz while locked to reduce power consumption.

### Virtual Keyboard

Since the T5S3 has no physical keyboard, a full-screen QWERTY virtual keyboard appears automatically when text input is needed (composing messages, entering WiFi passwords, editing settings, etc.).

The virtual keyboard supports:
- QWERTY letter layout with a symbol/number layer (tap the **123** key to switch)
- Shift toggle for uppercase
- Backspace and Enter keys
- Phantom keystroke prevention (a brief cooldown after the keyboard opens prevents accidental taps)

Tap keys to type. Tap **Enter** to submit, or press the **Boot button** to cancel and close the keyboard.

### External Keyboard (CardKB)

The T5S3 supports the M5Stack CardKB (or compatible I2C keyboard) connected via the QWIIC port. When detected at boot, the CardKB can be used for all text input — composing messages, entering URLs, editing notes, and navigating menus — without the on-screen virtual keyboard.

The CardKB is auto-detected on the I2C bus at address `0x5F`. No configuration is needed — just plug it in.

### Display Settings

The T5S3 Settings screen includes one additional display option not available on the T-Deck Pro:

| Setting | Description |
|---------|-------------|
| **Dark Mode** | Inverts the display — white text on black background. Tap to toggle on/off. Available on both T-Deck Pro and T5S3. |
| **Larger Font** | Increases text size on channel messages, contacts, DM inbox, and repeater admin screens. Tap to toggle on/off. Available on both T-Deck Pro and T5S3. |
| **Portrait Mode** | Rotates the display 90° from landscape (960×540) to portrait (540×960). Touch coordinates are automatically remapped. Text reader layout recalculates on orientation change. T5S3 only. |

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
| Swipe left / right | Switch between channels (swipe left past the last channel to reach the DM inbox) |
| Tap footer area | View relay path of last received message |
| Tap path overlay | Dismiss overlay |
| Long press (touch) | Open virtual keyboard to compose message to current channel |

#### Contacts

The contacts list shows all known nodes sorted by most recently heard, with type prefix, estimated hop count, and time since last advert. See the [T-Deck Pro Contacts Screen](#contacts-screen) section for an explanation of the type prefix and hop count display — the same conventions apply on the T5S3.

**Normal mode**

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll through contacts |
| Swipe left / right | Cycle contact filter (All → Chat → Rptr → Room → Sens → Fav) |
| Tap | Enter select mode (tapped contact is pre-selected) |
| Long press on Chat contact | View unread DMs (if any), then compose DM |
| Long press on Repeater/RS contact | Open repeater admin login |

> **Note:** The **Fav** filter shows only contacts you have marked as favourites. If it appears empty, no contacts have been favourited yet — use select mode (tap a contact) and then long-press to mark favourites.

**Select mode** — tap any contact row to enter select mode. The tapped contact is pre-selected (shown with `*`). Swipe or tap to navigate and toggle selections.

| Gesture | Action |
|---------|--------|
| Tap | Toggle selection on tapped row |
| Swipe left | Select all contacts in current filter |
| Swipe right | Deselect all |
| Long press | Exit select mode (confirm favourites / deletions first) |

Batch operations (favourite toggle, delete) are triggered from the overlay that appears after exiting select mode with contacts selected.

**Adding contacts**

1. **Automatic** — if Settings → Contacts → Add Mode is set to *Auto All*, nodes are added as their adverts are heard. *Custom* mode adds only nodes matching the enabled type toggles (Companion, Repeater, Room Server, Sensor). *Manual Only* disables auto-add.

2. **From the Last Heard screen** — tap the **Discover** tile (or access via the home page on non-WiFi builds) and navigate to Last Heard. Tap any entry to add it to contacts, or tap an existing contact to remove it (favourites require a second tap within 3 seconds to confirm). Entries show `[+]` if already in contacts, `[★]` if a favourite.

   > **Note:** The Last Heard list holds up to 1,000 entries in PSRAM, and advert data is stored persistently on the SD card — so contacts can be added long after the original advertisement was received, even across reboots. This makes Last Heard especially useful when auto-add is set to *Manual Only*, as it provides a passive catalogue of every node heard on the network.

3. **From the Discovery screen** — tap the Discover tile and run an active scan. Tap any result to add it to contacts.

**Deleting contacts**

Tap a contact to enter select mode, select the contacts to remove, exit select mode, and choose delete from the confirmation overlay.

#### Text Reader (File List)

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll file list |
| Tap | Open selected book |

#### Text Reader (Reading)

| Gesture | Action |
|---------|--------|
| Tap anywhere | Next page |
| Tap footer bar | Go to page number (via virtual keyboard) |
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
| Tap | Add selected node to contacts |
| Long press | Rescan for nodes |

#### Last Heard

| Gesture | Action |
|---------|--------|
| Swipe up / down | Scroll advert list |
| Tap | Add to or delete from contacts |

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

The companion firmware can be connected to via BLE (T-Deck Pro and T5S3 BLE variants) or WiFi (T-Deck Pro WiFi variants and T5S3 WiFi variant, TCP port 5000).

> **Note:** On both the T-Deck Pro and T5S3, BLE and WiFi are disabled by default at boot. On the T-Deck Pro, navigate to the Bluetooth or WiFi home page and press Enter to enable. On the T5S3, navigate to the Bluetooth home page and long-press the screen to toggle BLE on.

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
- [X] Lock screen with auto-lock timer and low-power standby
- [X] Last heard passive advert list
- [X] Touch-to-select on contacts, discovery, settings, text reader, notes screens
- [X] Map screen with GPS tile rendering
- [X] WiFi companion environment
- [X] OTA firmware update via phone
- [X] DM inbox with per-contact unread indicators
- [X] Roomserver message handling and mark-read on login
- [X] Alarm clock with custom MP3 sounds (audio variant)
- [X] Customised user option for larger-font mode
- [ ] Fix M4B rendering to enable chaptered audiobook playback
- [ ] Better JPEG and PNG decoding
- [ ] Improve EPUB rendering and EPUB format handling
- [ ] Figure out a way to silence the ringtone
- [ ] Figure out a way to customise the ringtone

**T5S3 E-Paper Pro:**
- [X] Core port: display, touch input, LoRa, battery, RTC
- [X] Touch-navigable home screen with tappable tile grid
- [X] Full virtual keyboard for text entry
- [X] Lock screen with clock, battery, unread count, and auto-lock timer
- [X] Backlight control (double/triple-click Boot button)
- [X] Dark mode and portrait mode display settings
- [X] Channel messages with swipe navigation and touch compose
- [X] Contacts with filter cycling and long-press DM/admin
- [X] Text reader with swipe page turns
- [X] Web reader with virtual keyboard URL/search entry (WiFi variant)
- [X] Settings screen with touch editing
- [X] Serial clock sync for hardware RTC
- [X] CardKB external keyboard support (via QWIIC)
- [X] Last heard passive advert list
- [X] Tap-to-select on contacts, discovery, settings, text reader, notes screens
- [X] OTA firmware update via phone (WiFi variant)
- [X] DM inbox with per-contact unread indicators
- [X] Roomserver message handling and mark-read on login
- [X] Customised user option for larger-font mode
- [ ] Improve EPUB rendering and EPUB format handling

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