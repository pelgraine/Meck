## Meshcore + Fork = Meck

A fork created specifically to focus on enabling BLE & WiFi companion firmware for the LilyGo T-Deck Pro, LilyGo T-Deck Max & LilyGo T5 E-Paper S3 Pro. Created wholly with Claude AI using Meshcore v1.11 code. 100% vibecoded.

[Check out the Meck discussion channel on the MeshCore Discord](https://discord.com/channels/1495203904898728149/1496789639556501614)

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
- [Region Scope (v1.7+)](#region-scope-v17)
- [T-Deck Pro](#t-deck-pro)
  - [Build Variants](#t-deck-pro-build-variants)
  - [Keyboard Controls](#t-deck-pro-keyboard-controls)
  - [Navigation (Home Screen)](#navigation-home-screen)
  - [Bluetooth (BLE)](#bluetooth-ble)
  - [WiFi Companion](#wifi-companion)
  - [Clock & Timezone](#clock--timezone)
  - [Channel Message Screen](#channel-message-screen)
  - [Channel Picker](#channel-picker)
  - [Contacts Screen](#contacts-screen)
  - [Sending a Direct Message](#sending-a-direct-message)
  - [Roomservers](#roomservers)
  - [Repeater Admin Screen](#repeater-admin-screen)
  - [Trace Route Screen (v1.9+)](#trace-route-screen-v19)
  - [Rx Log](#rx-log)
  - [Delete Message History (v1.10+)](#delete-message-history-v110)
  - [Per-Channel Notification Preferences (v1.10+)](#per-channel-notification-preferences-v110)
  - [Custom Notification Tones (v1.10+)](#custom-notification-tones-v110)
  - [Games (v1.10+)](#games-v110)
  - [Private Channels (v1.11+)](#private-channels-v111)
  - [Channel Sharing via DM (v1.11+)](#channel-sharing-via-dm-v111)
  - [Config Export/Import (v1.11+)](#config-exportimport-v111)
  - [Settings Screen](#settings-screen)
  - [Font Styles](#font-styles)
  - [Compose Mode](#compose-mode)
  - [Symbol Entry (Sym Key)](#symbol-entry-sym-key)
  - [Emoji Picker](#emoji-picker)
  - [SMS & Phone App (4G only)](#sms--phone-app-4g-only)
  - [Web Browser & IRC](#web-browser--irc)
  - [Alarm Clock (Audio only)](#alarm-clock-audio-only)
  - [Voice Notes Over LoRa (Audio only)](#voice-notes-over-lora-audio-only)
  - [Lock Screen (T-Deck Pro)](#lock-screen-t-deck-pro)
  - [Shutdown (T-Deck Pro)](#shutdown-t-deck-pro)
- [T-Deck Max](#t-deck-pro-max)
  - [Build Variants](#t-deck-pro-max-build-variants)
  - [4G and Audio at the Same Time](#4g-and-audio-at-the-same-time)
  - [Antenna (Internal / External)](#antenna-internal--external)
  - [Buzzer (Vibrate) Notifications](#buzzer-vibrate-notifications)
  - [Capacitive Touch & Buttons](#capacitive-touch--buttons)
  - [Frontlight & Backlight Brightness](#frontlight--backlight-brightness)
  - [Keyboard Backlight](#keyboard-backlight)
  - [Multi-Constellation GPS](#multi-constellation-gps)
- [Remote Repeater (T-Deck Pro 4G)](#remote-repeater-t-deck-pro-4g)
- [WiFi Repeater](#wifi-repeater)
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
- [Audiobook Player Guide](Audiobook_Player_Guide.md)
- [Meck-Mycelium Web App](#meck-mycelium-web-app)
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

Meck currently targets three LilyGo devices and also supports the Heltec V3 and V4 as remote repeaters:

| Device | Display | Input | LoRa | Battery | GPS | RTC |
|--------|---------|-------|------|---------|-----|-----|
| **T-Deck Pro** | 240×320 e-ink (GxEPD2) | TCA8418 keyboard + optional touch | SX1262 | BQ27220 fuel gauge, 1400 mAh | Yes | No (uses GPS time) |
| **T-Deck Max** | 240×320 e-ink (GxEPD2) + frontlight | TCA8418 keyboard + CST328 capacitive touch + 3 capacitive buttons | SX1262 | BQ27220 fuel gauge, 1500 mAh | Yes (multi-constellation) | No (uses GPS time) |
| **T5S3 E-Paper Pro** (V2, H752-B) | 960×540 e-ink (FastEPD, parallel) | GT911 capacitive touch (no keyboard) | SX1262 | BQ27220 fuel gauge, 1500 mAh | No (non-GPS variant) | Yes (PCF8563 hardware RTC) |
| **Heltec V3** (remote repeater only) | 0.96" OLED (SSD1306) | — | SX1262 | — | No | No |
| **Heltec V4** (remote repeater only) | 0.96" OLED (SSD1306) | — | SX1262 | — | No | No |

The T-Deck Pro, T-Deck Max, and T5S3 use the ESP32-S3 with 16 MB flash and 8 MB PSRAM. The Heltec V3 and V4 use the ESP32-S3 with 8 MB flash and 8 MB PSRAM.

---

## SD Card Requirements

**An SD card is essential for Meck to function properly.** Many features — including the e-book reader, notes, bookmarks, web reader cache, audiobook playback, firmware updates, contact import/export, and WiFi credential storage — rely on files stored on the SD card. Without an SD card inserted, the device will boot and handle mesh messaging, but most extended features will be unavailable or will fail silently.

**Recommended:** A **32 GB or larger** microSD card formatted as **FAT32**. Meck's extensive feature set — audiobooks, e-books, voice recordings, contact exports, alarm sounds, web reader cache, notes, and firmware images — can accumulate significant storage over time, so a larger card is worthwhile. MeshCore users have found that **SanDisk** microSD cards are the most reliable across both the T-Deck Pro and T5S3.

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

1. Go to https://flasher.meshcore.io
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

> **First boot:** On a fresh flash, the device will format its internal storage partition. The display shows "Formatting storage... First boot - please wait" — this takes 1-2 minutes and only happens once. If the device was previously running different firmware (e.g. stock LilyGo or Meshtastic), the partition is automatically erased and reformatted to ensure a clean start.

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

## Region Scope (v1.7+)

Regions limit how far your flood messages propagate through the mesh. When you set a region, outgoing messages are tagged with a transport code that repeaters use to decide whether to forward them. Messages sent without a region reach all repeaters via the default wildcard, same as always.

Meck does not pre-set any region on a fresh flash. Region names are determined by your local mesh community — check with your local group for the names in use. Common patterns follow ISO 3166 country/subdivision codes (e.g. `au` for Australia, `gb-eng` for England, `us-ca` for California), but communities may also use custom names for their area. Region names must be lowercase alphanumeric characters and hyphens only, max 29 characters.

**Device-wide default region:** Set in **Settings → Default Region**. This applies to all channels and DMs unless a channel has its own override.

**Per-channel region:** In Settings → Channels, select a channel and press Enter to edit its region scope. This overrides the device default for that specific channel only.

**Settings nudge:** When exiting settings, if no region is configured anywhere (no device default and no per-channel scopes), a prompt reminds you to consider setting one. You can dismiss it to stay unscoped.

Region scope can also be configured via serial commands — see the [Serial Settings Guide](Serial_Settings_Guide.md) for `set region`, `get region`, `set channel.scope`, and `get channel.scope` commands.

---

## T-Deck Pro

### T-Deck Pro Build Variants

| Variant | Environment | BLE | WiFi | 4G Modem | Audio DAC | Web Reader | Max Contacts |
|---------|------------|-----|------|----------|-----------|------------|-------------|
| Audio + BLE | `meck_audio_ble` | Yes | Yes (web reader only) | — | PCM5102A | Yes | 2,000 |
| Audio + WiFi | `meck_audio_wifi` | — | Yes (TCP:5000) | — | PCM5102A | Yes | 2,000 |
| Audio + Standalone | `meck_audio_standalone` | — | — | — | PCM5102A | No | 2,000 |
| 4G + BLE | `meck_4g_ble` | Yes | Yes | A7682E | — | Yes | 2,000 |
| 4G + WiFi | `meck_4g_wifi` | — | Yes (TCP:5000) | A7682E | — | Yes | 2,000 |
| 4G + Standalone | `meck_4g_standalone` | — | Yes | A7682E | — | Yes | 2,000 |
| Remote Repeater (4G) | `meck_remote_repeater` | — | — | A7682E (MQTT) | — | No | — |
| WiFi Repeater | `meck_wifi_repeater` | — | Yes (MQTT) | — | — | No | — |

The audio DAC and 4G modem occupy the same hardware slot and are mutually exclusive. (The T-Deck Max lifts this restriction — it runs both at once. See [T-Deck Max](#t-deck-pro-max).) The remote repeater and WiFi repeater variants operate as dedicated MeshCore repeaters — they forward mesh traffic and respond to guest logins as normal, but **admin management is handled remotely via MQTT** through the [Meck-Mycelium dashboard](https://pelgraine.github.io/Meck-Mycelium), not via the standard mesh admin password login. See [Remote Repeater](#remote-repeater-t-deck-pro-4g) and [WiFi Repeater](#wifi-repeater) below.

### T-Deck Pro Keyboard Controls

The T-Deck Pro firmware includes full keyboard support for standalone messaging without a phone.

### Navigation (Home Screen)

| Key | Action |
|-----|--------|
| W / A | Previous page |
| D | Next page |
| Enter | Select / Confirm |
| M | Open channel picker |
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
| R | Open trace route screen (v1.9+) -- see [Trace Route Screen](#trace-route-screen-v19) |
| J | Open games menu (v1.10+) -- see [Games](#games-v110) |
| G | Open map screen (shows contacts with GPS positions) |
| Mic | Open voice messages (audio variant only) |
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
| Enter | Compose new message |
| R | Reply to a message — enter reply select mode, scroll to a message with W/S, then press Enter to compose a reply with an @mention |
| V | View relay path of the last received message (scrollable, up to 20 hops) |
| Q | Back to channel picker |

### Channel Picker

Pressing **M** from the home screen opens the channel picker. All your channels and the DM inbox are shown in a single view with unread message badges, letting you jump directly to any channel instead of cycling through them one at a time. Pressing **Q** from a channel view (e.g. the Public feed) also returns to the channel picker.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down |
| Enter | Switch to selected channel |
| X | Delete message history for highlighted channel (v1.10+) |
| Q | Back to home screen |

Pressing **X** on any highlighted channel brings up a confirmation overlay. Press **Enter** to confirm deletion or **Q** to cancel. This clears all stored messages for that channel from the circular buffer and saves to SD. The channel itself is not removed -- only its message history.

On the T5S3, swiping left or right on the channel messages screen also opens the channel picker, which displays a **vertical bubble list** matching the Meck P4 aesthetic. Long-press a channel to bring up the delete history confirmation.

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

Flood-based hop estimates (`~D`, `~N`) are drawn from a cache of up to 1,000 recently heard adverts and reset to `?` on reboot until each contact re-advertises. Confirmed path values (`D`, `N`) persist until overwritten by a new path exchange.

**Normal mode controls**

| Key | Action |
|-----|--------|
| W / S | Scroll up / down through contacts |
| Shift+W / Shift+S | Page up / page down |
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

**Exporting and importing contacts**

In select mode, press **X** to export. If contacts are selected, only those contacts are exported; if none are selected, all contacts are exported. Contacts are saved as a JSON file to `/meshcore/` on the SD card with a timestamp in the filename. The JSON format is compatible with MeshCore companion apps — you can copy the file from the SD card and import it into the Android, iOS, or web companion app.

Press **R** on the contacts list (outside select mode) to import contacts from a JSON file on the SD card. The most recent export file in `/meshcore/` is used automatically.

**Contact limits:** All variants support up to 2,000 contacts (stored in PSRAM).

### Sending a Direct Message

Select a **Chat** contact in the contacts list and press **Enter** to start composing a direct message. The compose screen will show `DM: ContactName` in the header. Type your message and press **Enter** to send. The DM is sent encrypted directly to that contact (or flooded if no direct path is known). After sending or cancelling, you're returned to the contacts list.

Contacts with unread direct messages show a `*` marker next to their name in the contacts list.

**Reading received DMs:** From the home screen, press **M** to open the channel picker, then select the **DM Inbox** entry to view received direct messages. This shows all received direct messages with sender name and timestamp. Entering the DM inbox marks all DM messages as read and clears the unread indicator.

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

### Trace Route Screen (v1.9+)

The trace route screen lets you build a chain of repeaters and run a trace through them — the same feature available in the MeshCore companion app, but on-device. Each repeater in the chain that recognises its hash appends its receive SNR before forwarding the packet, giving you per-hop signal quality data for the whole route.

Press **R** from the home screen to open the trace screen. T5S3 users with a CardKB attached can use **R** as well.

**Building the path**

There are two ways to add hops to the path:

- **+ Add repeater** — opens a picker showing all known Repeater contacts. Press W/S to scroll, Enter to append the highlighted repeater to the path. The contact picker uses the repeater's stored public key bytes directly, so the hash is always correct regardless of mode.
- **Type Path** — opens an inline text editor for comma-separated decimal hash values, matching the format the companion app uses (e.g. `3601,2198,1244,2198,3601`). Useful when the repeater isn't in your contacts but you know its hash from the app or a community listing.

The path is shown as a numbered list below the menu. Individual hops can be edited or removed, and the whole path can be cleared if you need to start over.

**Hash mode**

Meck supports both 1-byte and 2-byte hash modes. The screen defaults to your device's `path.hash.mode` setting but can be toggled on the fly:

- 1-byte mode (`Mode: 1-byte`) — older networks, more collision-prone but smaller packets
- 2-byte mode (`Mode: 2-byte`) — current MeshCore default in most regions, fewer hash collisions

Use **A / D** on the mode row to toggle, or Enter to cycle.

**Running a trace**

Once at least one hop is in the path, scroll to **Run Trace** and press Enter. Meck creates a `PAYLOAD_TYPE_TRACE` packet and sends it direct-routed through the chain. The screen switches to a "Tracing..." state with an elapsed-time counter while waiting for the response.

When the trace completes, the screen shows:

- Each hop in order with its receive SNR (in dB)
- The final SNR of the response packet arriving back at your device
- Total round-trip time in milliseconds

If 30 seconds pass without a response, the trace times out and the screen returns to the build view so you can adjust the path and try again.

**Symmetric paths and direct visibility**

For a round-trip trace, the path needs to be symmetric. To trace through repeaters A → B → C and back, type `A,B,C,B,A`. The trace packet travels out through A→B→C, and the response comes back C→B→A.

You also need to be able to **hear the last repeater in the chain directly** — that's the one that sends the response back to your device. If the last hop is too far away to hear, the trace will time out even though the outbound leg succeeded.

| Key | Action |
|-----|--------|
| W / S | Navigate menu items |
| A / D | Toggle hash mode (1-byte / 2-byte) on the mode row |
| Enter | Select / confirm / open editor |
| 0–9 , | Path values (when the inline text editor is open) |
| Q | Cancel edit / back to home screen |

The screen supports up to **16 hops** per trace.

### Rx Log

The Rx Log is an on-device packet sniffer, mirroring the Rx Log in the MeshCore companion app. Open it from **Settings -> Rx Log >>**. It captures every packet the radio receives -- including relays destined for other nodes, since capture happens before filtering -- into a buffer of the most recent 100 packets, shown newest first.

Each entry shows the route type (flood or direct) and payload type, the receive time and wire size, the packet hash, the hop path, and the channel hash/name (for group messages) or the From/To node hashes (for addressed packets). For channel messages your device can decrypt, the decoded "sender: message" line is attached as well.

| Key | Action |
|-----|--------|
| W / S | Scroll through entries (newest at the top) |
| Q | Back to settings |

The log is held in RAM only and is cleared on reboot.

A running **RX packets** count also appears on the radio details page on the home screen (page through with **D**), just beneath the noise-floor reading. It counts flood and direct packets received since boot and resets on reboot or whenever you change radio parameters (frequency, bandwidth, or spreading factor).

### Delete Message History (v1.10+)

You can clear all stored messages for any individual channel or the DM inbox without removing the channel itself.

From the home screen, press **M** to open the channel picker. Navigate to the channel you want to clear and press **X**. A confirmation overlay appears asking "Delete message history?" -- press **Enter** to confirm or **Q** to cancel.

On the T5S3, long-press the channel in the channel picker to bring up the same confirmation. Tap to confirm or press the Boot button to cancel.

Messages are invalidated in the circular buffer and the change is saved to SD immediately. The unread counter is also reset. New messages will continue to appear as they arrive.

### Per-Channel Notification Preferences (v1.10+)

Each channel (and the DM inbox) can be individually set to one of three notification levels:

- **All** -- notify on every message (default)
- **@ (Mentions)** -- only notify when someone tags you with @YourNodeName or @[YourNodeName]
- **Off** -- completely muted (no buzzer, no keyboard flash, no screen wake, no toast)

Messages are always stored in history regardless of the notification setting -- only the alerts and unread badges are suppressed.

To change a channel's notification preference: from the home screen, press **S** to open settings, scroll down to the **Channels >>** section and open it. Navigate to the channel you want to configure, and press **N** to cycle through the three modes. The current setting is shown in the channel row hint as `N:All`, `N:@`, or `N:Off`.

### Custom Notification Tones (v1.10+)

Each channel can have its own notification tone instead of the default buzzer sound. When a message arrives on a channel with a custom tone assigned, that tone plays through the speaker instead of the RTTTL buzzer.

To assign a tone: from the home screen, press **S** to open settings, scroll down to the **Channels >>** section and open it. Navigate to the channel you want, and press **T**. The tone picker appears with a list of available sounds. Use **W/S** to browse, **Enter** to select, or **Q** to cancel. Select "Default (silent)" to remove a custom tone and revert to the standard buzzer.

**Audio variant (PCM5102A DAC):** A selection of bundled tones are copied to the `/alarms/` folder on the SD card on first boot. You can also add your own MP3 files to that folder -- they'll appear in the tone picker alongside the bundled options. This gives you complete flexibility to use any short MP3 as a notification sound.

**4G variant (A7682E modem):** Seven bundled notification tones are embedded in the firmware as 8kHz mono WAV files and transferred to the modem's internal filesystem on boot. Playback goes through the modem's own speaker amplifier via AT+CCMXPLAY. Custom user-supplied tones are not supported on the 4G variant -- only the bundled set is available.

**Available bundled tones:** Bell, Ding, High Trill, Low Soft Ding (x2), Mid Trill, and Soft Notif. All are short, 1-2 second alert sounds.

**T-Deck Max -- Buzzer (vibrate):** On the MAX, the tone picker includes an extra **Buzzer (vibrate)** option above the sound files. When selected, an incoming message on that channel pulses the MAX's DRV2605 haptic motor instead of playing a tone or the RTTTL buzzer -- useful for silent alerts. Because the MAX has its own ES8311 audio codec, it also supports custom MP3 tones from the `/alarms/` folder, exactly like the T-Deck Pro audio variant (and unlike the T-Deck Pro 4G variant). See [Buzzer (Vibrate) Notifications](#buzzer-vibrate-notifications).

### Games (v1.10+)

Press **J** from the home screen to open the games menu. Two classic games are included:

**Snake** -- the Nokia classic. Guide the snake around the screen to eat food and grow longer without crashing into the walls or your own tail. The game runs on the e-ink display at a pace suited to the refresh rate.

| Key | Action |
|-----|--------|
| W | Turn up |
| A | Turn left |
| S | Turn down |
| D | Turn right |
| Q | Quit to games menu |

**Minesweeper** -- clear the board without hitting a mine. Numbers reveal how many adjacent cells contain mines. Flag cells you suspect are mines to keep track.

| Key | Action |
|-----|--------|
| W / A / S / D | Move cursor |
| Enter | Reveal cell |
| F | Toggle flag on cell |
| Q | Quit to games menu |

On the T-Deck Max the board is larger, with a 15x20 grid and 50 mines.

### Private Channels (v1.11+)

Meck supports both public hashtag channels and private channels. The difference is in how the channel secret is generated:

- **Public (hashtag) channels** — type a name starting with `#` (e.g. `#camping`). The 16-byte secret is derived deterministically from the name via SHA-256, so anyone on any MeshCore device who creates the same hashtag name gets the same key and can communicate on that channel.
- **Private channels** — type a name **without** the `#` prefix (e.g. `team-alpha`). A cryptographically random 16-byte secret is generated, meaning only devices that have been explicitly given the key can participate.

**How to create a private channel:**

1. From the home screen, press **S** to open settings
2. Scroll down to the **Channels >>** section and press Enter to open it
3. Navigate to **+ Add Channel (# = public)** and press Enter
4. Type the channel name **without** a `#` prefix, then press Enter

The channel is created with a random secret. To let other Meck users join, use channel sharing (see below).

### Channel Sharing via DM (v1.11+)

You can share any channel — public or private — with another Meck user by sending them the channel name and secret as an encrypted direct message. This is particularly useful for private channels, where sharing the secret manually would mean typing 32 hex characters.

**How to share a channel:**

1. From the home screen, press **S** to open settings
2. Scroll down to the **Channels >>** section and press Enter to open it
3. Navigate to the channel you want to share
4. Press **C** to open the contact picker
5. Select a contact and press Enter to send

The recipient's device automatically adds the channel to their channel list (if it doesn't already exist and there's an empty slot). An alert confirms the channel was added. In the DM conversation, both sender and recipient see a sanitised message ("Shared channel: name") rather than the raw protocol data.

On the T5S3, channel sharing works the same way via the CardKB keyboard.

### Config Export/Import (v1.11+)

The **Export/Import >>** sub-screen in Settings lets you back up and restore your device configuration via JSON files on the SD card. This is useful for migrating settings to a new board, keeping a backup before reflashing, or cloning a configuration across multiple devices.

**Exporting:**

1. From the home screen, press **S** to open settings
2. Scroll down to **Export/Import >>** and press Enter
3. Select **Export to SD >>** and press Enter
4. Toggle which sections to include using Enter on each checkbox:
   - **Identity** — public and private key (full identity backup)
   - **Radio Settings** — frequency, bandwidth, spreading factor, coding rate, TX power, and GPS position
   - **Channels** — all channel names and secrets
   - **Contacts** — all contacts with public keys, type, flags, position, and timestamps
   - **Auto-Add Prefs** — contact auto-add mode and per-type toggles
5. Navigate to **>> Export Now** and press Enter

The config is saved as a timestamped JSON file in `/meshcore/` on the SD card (e.g. `meshcore_config_20260523_1430.json`). The format is compatible with the MeshCore companion app config export.

**Importing:**

Place a file named `import.json` in the `/meshcore/` folder on your SD card. Then either:

- **From settings:** go to **Export/Import >> → Import from SD** and press Enter
- **On boot:** the firmware automatically checks for `/meshcore/import.json` at startup and imports it if found

If the import includes a different identity (private key), the device reboots after applying the new identity. Channels and contacts are merged into the existing configuration.

### Settings Screen

Press **S** from the home screen to open settings. On first boot (when the device name is still the default hex ID), the settings screen launches automatically as an onboarding wizard to set your device name and radio preset.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down through settings |
| Shift+W / Shift+S | Page up / page down |
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
| Default Region | Text entry — type a region name (e.g. `au-nsw`), Enter to confirm. Empty = unscoped. See [Region Scope](#region-scope-v17). |
| Dark Mode | Toggle inverted display — white text on black background (Enter to toggle) |
| Larger Font | Toggle larger text size on channel messages, contacts, DM inbox, and repeater admin screens (Enter to toggle) |
| Font Style | A / D to cycle styles (Classic / Noto Sans / Montserrat), Enter to apply. See [Font Styles](#font-styles). |
| Auto Lock | A / D to cycle timeout (None / 2 / 5 / 10 / 15 / 30 min), Enter to confirm |
| Contacts >> | Opens the Contacts sub-screen (see below) |
| Channels >> | Opens the Channels sub-screen (see below) |
| OTA Tools >> | Opens the OTA sub-screen — Firmware Update and SD File Manager (see [OTA Firmware Update](#ota-firmware-update-v13)) |
| Export/Import >> | Opens the Export/Import sub-screen — export device config to SD or import from `/meshcore/import.json` (see [Config Export/Import](#config-exportimport-v111)) |
| Rx Log >> | Opens the Rx Log packet sniffer (see [Rx Log](#rx-log)) |
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

**Channels sub-screen** -- press Enter on the `Channels >>` row to open. Lists all current channels with their region scope tags (e.g. `[au-nsw]` or `[*]` for device default). The hint line for each channel shows its current notification preference (`N:All`, `N:@`, or `N:Off`) and available actions.

| Key | Action |
|-----|--------|
| Enter | Edit channel region scope |
| N | Cycle notification preference (All / Mentions / Off) -- see [Per-Channel Notification Preferences](#per-channel-notification-preferences-v110) |
| T | Open notification tone picker (audio and 4G variants) -- see [Custom Notification Tones](#custom-notification-tones-v110) |
| C | Share channel with a contact via DM -- see [Channel Sharing via DM](#channel-sharing-via-dm-v111) |
| X | Delete channel (non-primary channels only) |
| Q | Back to top-level settings |

The top-level settings screen also displays your node ID and firmware version. On the 4G variant, IMEI, carrier name, and APN details are shown here as well.

When adding a channel, type the channel name and press Enter. Names starting with `#` create a public hashtag channel (secret derived from the name via SHA-256, matching the standard MeshCore convention). Names without a `#` prefix create a private channel with a random secret — see [Private Channels](#private-channels-v111).

If you've changed radio parameters, pressing Q will prompt you to apply changes before exiting.

> **Tip:** All device settings (plus mesh tuning parameters not available on-screen) can also be configured via USB serial. See the [Serial Settings Guide](Serial_Settings_Guide.md) for complete documentation.

### Font Styles

Meck supports three font styles across the entire UI: **Classic** (the original FreeSans look), **Noto Sans** (clean, excellent Latin Extended coverage), and **Montserrat** (geometric, distinctive).

Change the font in **Settings → Font** — use A/D to cycle with a live preview, then Enter to apply. Press Q to cancel and revert. The selected style applies to all screens including channel messages, contacts, settings, and the e-reader.

Font styles are available in both Tiny and Larger text size modes. Custom fonts at Tiny size use 7pt glyphs; at Larger size, 9pt — matching the existing FreeSans layout. The font preference is saved and persists across reboots.

**Accented character support (v1.8+):** All three font styles now display accented and diacritical characters (Czech, Polish, French, German, etc.) instead of dropping them. **Noto Sans at Larger text size** renders diacritical marks natively (carons, accents, cedillas). All other font and size combinations fold accented characters to their ASCII base letter (ě→e, ž→z, ñ→n) — the letter is always visible, just without the diacritic mark.

### Compose Mode

| Key | Action |
|-----|--------|
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

While in compose mode, press the **$** key to open the emoji picker. A scrollable grid of 79 emoji is displayed in a 5-column layout, with faces and emotions grouped first. Scrolling wraps around — pressing W on the first row goes to the last row and vice versa.

| Key | Action |
|-----|--------|
| W / S | Navigate up / down |
| A / D | Navigate left / right |
| Enter | Insert selected emoji |
| $ / Q / Backspace | Cancel and return to compose |

### SMS & Phone App (4G only)

Press **T** from the home screen to open the SMS & Phone app. The app opens to a menu screen where you can choose between the **Phone** dialer (for calling any number) or the **SMS Inbox** (for messaging and calling saved contacts). The SMS Inbox entry shows the number of unread received messages in brackets (e.g. **SMS Inbox [3]**); the badge disappears once everything is read.

For full documentation including key mappings, dialpad usage, contacts management, and troubleshooting, see the [SMS & Phone App Guide](SMS___Phone_App_Guide.md).

### Web Browser & IRC

Press **B** from the home screen to open the web reader. This is available on the BLE, WiFi, and 4G variants (not the standalone audio variant, which excludes WiFi to preserve lowest-battery-usage design).

The web reader home screen provides access to the **IRC client**, the **URL bar**, and your **bookmarks** and **history**. Select IRC Chat and press Enter to configure and connect to an IRC server. Select the URL bar to enter a web address, or scroll down to open a bookmark or history entry.

The browser is a text-centric reader best suited to text-heavy websites. It also includes basic web search via DuckDuckGo Lite, and can download EPUB files — follow a link to an `.epub` and it will be saved to the books folder on your SD card for reading later in the e-book reader.

For full documentation including key mappings, WiFi setup, bookmarks, IRC configuration, and SD card structure, see the [Web App Guide](Web_App_Guide.md).

### Alarm Clock (Audio only)

Press **K** from the home screen to open the alarm clock. This is available on the T-Deck Pro Audio variant (PCM5102A DAC) and on the T-Deck Max (ES8311 codec). Set up to five daily alarms that play custom MP3 files through the headphone jack.

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

### Voice Notes Over LoRa (Audio only)

Press the **Microphone key** (the zero key on the keyboard) to open the Voice Messages screen. This is available on the T-Deck Pro Audio variant (PCM5102A DAC) and on the T-Deck Max (ES8311 codec).

Record and send voice messages of up to 12 seconds over LoRa. Audio is encoded on-device using Codec2 at 1200 bps, compressing each second of speech into a single 150-byte LoRa packet. Voice notes use very little airtime relative to what they deliver — a 5-second message is just 5 packets.

Voice notes can be sent to another T-Deck Pro Audio device (plays automatically through the headphone jack) or to any MeshCore companion device connected to the [Meck-Mycelium web app](https://pelgraine.github.io/Meck-Mycelium) (plays through your phone's speaker as a tappable bubble in the DM view).

**Sending a voice note:**

1. Press the **Microphone key** to open the Voice Messages screen
2. Press and **hold** the Microphone key to record — release to stop (max 12 seconds)
3. Press **S** to open the contact picker — contacts with a direct path appear at the top
4. Scroll to your contact and press **Enter** to send

Packets are sent with staggered 3-second delays to avoid congesting the channel. On a 62.5 kHz / SF7 radio preset (e.g. Australia Narrow), a 5-second voice note arrives in roughly 20 seconds and a 12-second recording in about 42 seconds.

**Receiving voice notes:**

* **On a T-Deck Pro Audio device:** the voice message screen opens automatically and the message plays through the headphone jack. **Headphones are recommended** — the built-in speaker is very quiet.
* **Via Meck-Mycelium:** voice messages appear as "🎙️ Voice message" bubbles in the DM view. Tap to play. Codec2 decoding happens entirely in the browser via WebAssembly.

> **Note:** Voice recording and sending requires the **Audio variant** hardware (PCM5102A DAC). Receiving and playback works on all Audio variants. Non-audio Meck devices can receive and relay voice packets but cannot play them locally — use the Meck-Mycelium web app for playback on those devices.

| Key | Action |
|-----|--------|
| Mic (hold) | Record voice note |
| Mic (release) | Stop recording |
| S | Open contact picker to send |
| Enter | Send to selected contact |
| Q | Back to home screen |

### Lock Screen (T-Deck Pro)

Double-click the Boot button to lock the screen. The lock screen shows the current time, battery percentage, and unread message count. The CPU drops to 40 MHz while locked to reduce power consumption.

Double-click the Boot button again to unlock and return to whatever screen you were on.

An auto-lock timer can be configured in **Settings → Auto Lock** (None / 2 / 5 / 10 / 15 / 30 minutes of idle time).

### Shutdown (T-Deck Pro)

The home screen includes a **Shutdown** page. Selecting it powers the device off completely — the ESP32-S3 enters deep sleep with no wake sources, peripheral power is cut, and the LoRa module is powered down. Only a hardware reset (reset button) or USB power-on will wake the device. This is distinct from the auto-lock hibernate, which maintains wake-on-LoRa capability.

---

## T-Deck Max

The LilyGo T-Deck Max is a close relative of the T-Deck Pro: same 240×320 e-ink panel and TCA8418 keyboard, same ESP32-S3, and the same on-device UI. **All the [T-Deck Pro](#t-deck-pro) keyboard controls and screens apply unchanged** — this section only covers what's different on the MAX.

The headline difference is that the MAX carries both an A7682E 4G modem **and** an ES8311 audio codec, wired through an XL9555 I/O expander so they can run at the same time. On the T-Deck Pro the audio DAC and the 4G modem share one hardware slot and are mutually exclusive; on the MAX you get the SMS & phone app, the audiobook player, the alarm clock, **and** cellular data on a single device. The MAX also adds a CST328 capacitive touchscreen, three capacitive front buttons, a DRV2605 haptic motor for vibrate alerts, an e-ink frontlight, and a 1500 mAh battery.

### T-Deck Max Build Variants

| Variant | Environment | BLE | WiFi | 4G Modem | Audio Codec | Web Reader | Max Contacts |
|---------|------------|-----|------|----------|-------------|------------|-------------|
| MAX + BLE | `meck_max_ble` | Yes | — | A7682E | ES8311 | Yes | 2,000 |
| MAX + WiFi | `meck_max_wifi` | — | Yes (TCP:5000) | A7682E | ES8311 | Yes | 2,000 |
| MAX + Standalone | `meck_max_standalone` | — | — | A7682E | ES8311 | Yes | 2,000 |

Unlike the T-Deck Pro, **every** MAX variant includes both the 4G modem and the audio codec — there is no separate "audio" vs "4G" split because the MAX runs both. The web reader is available on all three variants (it has a data path via the 4G modem, plus WiFi on the WiFi build), and all three support up to 2,000 contacts in PSRAM.

### 4G and Audio at the Same Time

Because the modem and codec are independently powered via the XL9555 expander, the MAX home screen exposes the full set of apps that are otherwise split across the T-Deck Pro's audio and 4G variants:

- **[P] Audiobooks / Audio** — the audiobook player (also plays music from `/audiobooks/music`)
- **[K] Alarm** — the alarm clock with custom MP3 sounds
- **[T] Phone** — the SMS & Phone app for calls and texts over the 4G modem
- **[B] Browser** — the web reader & IRC client (BLE / WiFi builds)
- **[F] Discover** — node discovery
- **Mic** — the voice messages screen (record and play on the MAX — see note below)

Audio output is routed through a shared speaker mux: when a call, ringtone, or modem notification tone plays, the MAX switches the speaker to the modem; for audiobooks, alarms, and notification MP3s it switches to the ES8311 codec.

> **Voice-note recording and playback both work on the MAX.** Capture runs through the ES8311's ADC and playback through the ES8311 output (the same path that drives audiobooks and alarms). Sending recorded notes over the mesh is implemented but has not yet been verified end to end on the MAX.

### Antenna (Internal / External)

The MAX has both an on-board internal antenna and an external MMCX antenna connector, with a switch in **Settings** to select between them. **Internal is the default** — if you want to use an external MMCX antenna, change the switch in Settings first.

### Buzzer (Vibrate) Notifications

The MAX has a DRV2605 haptic motor, so each channel (and the DM inbox) can be set to vibrate instead of playing a tone. In **Settings → Channels**, highlight a channel and press **T** to open the notification tone picker — on the MAX, a **Buzzer (vibrate)** entry appears just below "Default (silent)". Selecting it makes incoming messages on that channel pulse the motor rather than sounding the buzzer or a tone. See [Custom Notification Tones](#custom-notification-tones-v110).

### Capacitive Touch & Buttons

The MAX adds a CST328 capacitive touchscreen and **three capacitive buttons** along the bottom of the front bezel — none of which the T-Deck Pro has:

| Button | Action |
|--------|--------|
| **Heart** | Toggle the e-ink frontlight on/off (at the brightness configured in settings — see below) |
| **Speech bubble** | Quick access to the channel picker (same as pressing M) |
| **Paper plane** | Quick access to the DM inbox |

### Frontlight & Backlight Brightness

The MAX has an e-ink frontlight. The brightness it turns on to is set in **Settings → Backlight Brightness**, adjustable from **5% to 100%** in 5% steps (default 100%). The Heart capacitive button toggles the frontlight at that level. This setting only appears on the MAX.

### Keyboard Backlight

Press **both Shift keys together** to toggle the keyboard backlight on and off.

### Multi-Constellation GPS

The MAX's GPS is configured for multi-constellation positioning (GPS, Galileo, and BeiDou) via the `$PCAS04,7` command, for faster fixes and better coverage than single-constellation GPS.

---

## Remote Repeater (T-Deck Pro 4G)

> **TODO — This section needs full documentation.** The feature is implemented and shipping. Draft outline below.

The remote repeater variant (`meck_remote_repeater`) turns a T-Deck Pro 4G board into a dedicated MeshCore repeater with cellular MQTT remote management. The repeater functions as a normal MeshCore repeater on the mesh — it forwards packets and responds to guest logins — but **admin management (clock sync, send advert, reboot, get status, configuration) is performed remotely via MQTT** through the [Meck-Mycelium dashboard](https://pelgraine.github.io/Meck-Mycelium), not via the standard mesh admin password login. The device connects to an MQTT broker (HiveMQ Cloud recommended — free tier available) over cellular data, publishing telemetry (uptime, battery, signal strength, temperature, neighbour count) and subscribing to admin commands.

**Sections to write:**

- **Requirements** — T-Deck Pro 4G (A7682E), active SIM with data plan, SD card, MQTT broker account
- **Setting up HiveMQ Cloud** — step-by-step free account creation, cluster setup, credentials
- **SD card configuration** — `/remote/mqtt.cfg` (broker, port, username, password, device ID) and optional `/remote/apn.cfg`
- **Deploying** — flash `meck_remote_repeater`, insert SIM and SD, boot sequence, display status indicators
- **Dashboard usage** — connecting Meck-Mycelium to your MQTT broker, available commands (clock sync, send advert, reboot, get status), telemetry display
- **Troubleshooting** — common issues (SIM not registering, MQTT auth failures, APN auto-detection)

---

## WiFi Repeater

> **TODO — This section needs full documentation.** The feature is implemented. Draft outline below.

The WiFi repeater variants turn a device into a dedicated MeshCore repeater with WiFi MQTT remote management — similar to the cellular remote repeater but using WiFi instead of 4G. The repeater forwards mesh traffic and responds to guest logins as normal, but **admin management is performed remotely via MQTT** through the Meck-Mycelium dashboard, not via the standard mesh admin password login. Available for the following platforms:

| Variant | Environment | Platform |
|---------|------------|----------|
| T-Deck Pro WiFi Repeater | `meck_wifi_repeater` | LilyGo T-Deck Pro |
| T5S3 WiFi Repeater | `meck_wifi_repeater_t5s3` | LilyGo T5S3 E-Paper Pro |
| Heltec V3 WiFi Repeater | `meck_wifi_repeater_heltec_v3` | Heltec V3 |
| Heltec V4 WiFi Repeater | `meck_wifi_repeater_heltec_v4` | Heltec V4 |
| Heltec V4 WiFi Repeater (headless) | `meck_wifi_repeater_heltec_v4_headless` | Heltec V4 (no display) |

**Sections to write:**

- **Requirements** — device, WiFi network, MQTT broker account, SD card (T-Deck Pro / T5S3) or SPIFFS config (Heltec V4)
- **SD card configuration** — `/remote/wifi.cfg` (supports multiple SSIDs) and `/remote/mqtt.cfg`
- **Heltec V4 specifics** — no SD card slot, config stored in SPIFFS, headless vs display variant
- **OTA updates** — NTP time sync, HTTP firmware download over WiFi
- **Dashboard** — same Meck-Mycelium dashboard as the cellular remote repeater

---

## T5S3 E-Paper Pro

The LilyGo T5S3 E-Paper Pro (V2, H752-B) is a 4.7-inch e-ink device with capacitive touch and no physical keyboard. All navigation is done via touch gestures and the Boot button (GPIO0). The larger 960×540 display provides significantly more screen real estate than the T-Deck Pro's 240×320 panel.

### T5S3 Build Variants

| Variant | Environment | BLE | WiFi | Web Reader | Max Contacts |
|---------|------------|-----|------|------------|-------------|
| Standalone | `meck_t5s3_standalone` | — | — | No | 2,000 |
| BLE Companion | `meck_t5s3_ble` | Yes | — | No | 2,000 |
| WiFi Companion | `meck_t5s3_wifi` | — | Yes (TCP:5000) | Yes | 2,000 |
| WiFi Repeater | `meck_wifi_repeater_t5s3` | — | Yes (MQTT) | No | — |

The WiFi variant connects to the MeshCore web app or meshcore.js over your local network. The web reader shares the same WiFi connection — no extra setup needed. The WiFi Repeater variant is a dedicated remote repeater — see [WiFi Repeater](#wifi-repeater) for details on MQTT-based admin management.

### Touch Navigation

The T5S3 uses a combination of touch gestures and the Boot button for all interaction. There is no physical keyboard — text entry uses an on-screen virtual keyboard that appears when needed.

**Core gesture types:**

| Gesture | Description |
|---------|-------------|
| **Tap** | Touch and release quickly. Context-dependent: opens tiles on home screen, selects items in lists, advances pages in readers. |
| **Swipe** | Touch, drag at least 60 pixels, and release. Direction determines action (scroll, page turn, switch channel/filter). |
| **Long press (touch)** | Touch and hold for 500ms+. Context-dependent: compose messages, open DMs, delete bookmarks. |

### T5S3 Home Screen

The home screen displays a grid of tappable tiles across three rows:

| | Column 1 | Column 2 | Column 3 |
|---|----------|----------|----------|
| **Row 1** | Messages | Contacts | Settings |
| **Row 2** | Reader | Notes | Browser (WiFi) / Discover (other) |
| **Row 3** | Trace | Games | |

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
- Backspace (UTF-8 aware — correctly deletes multi-byte emoji) and Enter keys
- **Emoji picker** — tap the **$** key to open a scrollable 8-column grid of 79 emoji sprites with page indicators. Tap an emoji to insert it inline in your message. Tap **Back** to return to the keyboard. Faces and emotions are grouped first for quick access.
- Inline emoji rendering — emoji appear as pixel sprites in the text field as you type
- Phantom keystroke prevention (a brief cooldown after the keyboard opens prevents accidental taps)

Tap keys to type. Tap **Enter** to submit, or press the **Boot button** to cancel and close the keyboard.

### External Keyboard (CardKB)

The T5S3 supports the M5Stack CardKB (or compatible I2C keyboard) connected via the QWIIC port. When detected at boot, the CardKB can be used for all text input — composing messages, entering URLs, editing notes, and navigating menus — without the on-screen virtual keyboard.

The CardKB is auto-detected on the I2C bus at address `0x5F`. No configuration is needed — just plug it in.

### Display Settings

The T5S3 Settings screen includes display options shared with the T-Deck Pro, plus one T5S3-specific setting:

| Setting | Description |
|---------|-------------|
| **Dark Mode** | Inverts the display — white text on black background. Tap to toggle on/off. Available on both T-Deck Pro and T5S3. |
| **Larger Font** | Increases text size on channel messages, contacts, DM inbox, and repeater admin screens. Tap to toggle on/off. Available on both T-Deck Pro and T5S3. |
| **Font Style** | Choose between Classic (FreeSans), Noto Sans, or Montserrat. Swipe left/right to cycle, tap to apply. Available on both T-Deck Pro and T5S3. |
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
| Swipe left / right | Open channel picker — shows all channels and DM inbox in a vertical bubble list with unread badges. Tap to select. |
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

**Select mode** — tap any contact row to enter select mode. The tapped contact is pre-selected (shown with `*`). Swipe or tap to navigate and toggle selections. You can also use a **two-finger tap** anywhere on the contacts screen to toggle select mode on and off.

| Gesture | Action |
|---------|--------|
| Tap | Toggle selection on tapped row |
| Swipe left | Select all contacts in current filter |
| Swipe right | Deselect all |
| Two-finger tap | Toggle select mode on/off |
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

## Meck-Mycelium Web App

The [Meck-Mycelium web app](https://pelgraine.github.io/Meck-Mycelium) is a browser-based companion that connects to your MeshCore device via BLE (using WebBLE in Chrome) or to your MQTT broker for remote repeater management.

**Features:**

- **Voice message playback** — voice notes sent from a Meck Audio device appear as tappable playback bubbles in the DM view. Codec2 decoding happens entirely in the browser via WebAssembly — no app install or audio hardware needed on the receiving end.
- **Remote repeater dashboard** — connect to your MQTT broker to administer remote repeater devices (cellular or WiFi). View live telemetry, send admin commands (clock sync, send advert, reboot, get status), and manage repeaters that are out of LoRa range. This replaces the standard mesh admin password login for remote repeater variants.
- **Standard companion features** — messaging, contacts, channel messages via BLE.

Open **https://pelgraine.github.io/Meck-Mycelium** in Chrome on your phone or computer.

> **Note:** WebBLE requires Chrome (or a Chromium-based browser). Safari and Firefox do not support WebBLE.

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
- Meck-Mycelium: https://pelgraine.github.io/Meck-Mycelium (voice playback, remote repeater dashboard)
- Android: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- iOS: https://apps.apple.com/us/app/meshcore/id6742354151?platform=iphone
- NodeJS: https://github.com/liamcottle/meshcore.js
- Python: https://github.com/fdlamotte/meshcore-cli

## 🛠 Hardware Compatibility

MeshCore is designed for devices listed in the [MeshCore Flasher](https://flasher.meshcore.io). Meck specifically targets the LilyGo T-Deck Pro, LilyGo T-Deck Max, LilyGo T5S3 E-Paper Pro, Heltec V3 (remote repeater only), and Heltec V4 (remote repeater only).

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
- [X] Voice notes over LoRa (Codec2, audio variant)
- [X] Contact select mode with batch favourite, export, import, and delete
- [X] Path editor for manual contact route management
- [X] Remote repeater with cellular MQTT admin management (4G variant)
- [X] WiFi remote repeater with MQTT admin management
- [X] SD File Manager via OTA Tools
- [X] 2,000 contact support (PSRAM, all variants)
- [X] Channel picker screen with unread badges
- [X] Region scope (MeshCore v1.15+ compatibility)
- [X] Selectable font styles (Classic, Noto Sans, Montserrat)
- [X] Expanded emoji picker (79 emoji, reordered, wrap scrolling)
- [X] 1,000-entry advert path cache (PSRAM)
- [X] Accented character / diacritics support (Czech, Polish, French, German, Latin Extended)
- [X] Page scroll (Shift+W/S) on all list screens
- [X] True power off (deep sleep, no wake sources)
- [X] BLE 2M PHY, DLE, and faster write interval
- [X] Trace route screen with contact picker and typed-path entry (v1.9)
- [X] DM message persistence across reboots (v1.9)
- [X] Per-channel message history deletion (v1.10)
- [X] Per-channel notification preferences with @mention support (v1.10)
- [X] Custom notification tones per channel -- audio variant (MP3) and 4G variant (WAV via modem) (v1.10)
- [X] Games menu with Snake and Minesweeper (v1.10)
- [X] MAX_GROUP_CHANNELS expanded to 40 for all builds (v1.10)
- [X] Private channel support with random secret generation (v1.11)
- [X] Channel sharing via encrypted DM with auto-add on receive (v1.11)
- [X] Config export/import to SD card with selectable sections (v1.11)
- [X] Contact recency fix for nodes with stuck/behind clocks (v1.11)
- [X] Expanded emoji picker (79 emoji) (v1.11)
- [ ] Fix M4B rendering to enable chaptered audiobook playback
- [ ] Better JPEG and PNG decoding
- [ ] Improve EPUB rendering and EPUB format handling
- [ ] Incoming call ringer silence (hardware limitation -- A7682E drives speaker autonomously on RING, no software mute path available)

**T-Deck Max:**
- [X] Core port: e-ink display, TCA8418 keyboard, LoRa, battery, GPS (decoupled from the T-Deck Pro variant)
- [X] Companion radio: BLE, WiFi, and Standalone variants (`meck_max_ble` / `meck_max_wifi` / `meck_max_standalone`)
- [X] Combined 4G (A7682E) and audio (ES8311) running simultaneously via the XL9555 I/O expander
- [X] ES8311 audio codec init -- audiobooks, music, alarms, and notification MP3s
- [X] Music playback from `/audiobooks/music` (no bookmarks, WAV duration from header)
- [X] SMS & phone app available alongside the audio apps on the same device
- [X] CST328 capacitive touch support (vendored Hynitron driver)
- [X] Three capacitive front buttons -- Heart (frontlight), speech bubble (channel picker), paper plane (DM inbox)
- [X] E-ink frontlight with configurable brightness (Settings, 5--100%)
- [X] Keyboard backlight toggle (press both Shift keys)
- [X] Buzzer (vibrate) per-channel notification option via DRV2605 haptic motor
- [X] Multi-constellation GPS -- GPS, Galileo, BeiDou (`$PCAS04,7`)
- [X] Home-screen e-ink X-offset and word-wrap fixes
- [X] Voice-note playback (ES8311 output path)
- [X] Voice-note recording on MAX (ES8311 ADC capture path)
- [ ] BHI260AP gyroscope / IMU support (0x28) -- new on the MAX, not yet used by Meck

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
- [X] Contact select mode with batch favourite and delete
- [X] WiFi remote repeater with MQTT admin management
- [X] 2,000 contact support (PSRAM, all variants)
- [X] Channel picker screen with vertical bubble list layout
- [X] Region scope (MeshCore v1.15+ compatibility)
- [X] Selectable font styles (Classic, Noto Sans, Montserrat)
- [X] Virtual keyboard emoji grid with scrollable pages
- [X] Accented character / diacritics support (Czech, Polish, French, German, Latin Extended)
- [X] DM message persistence across reboots (v1.9)
- [X] Expanded Minesweeper grid to 14x14 with 25 mines (v1.11)
- [ ] Improve EPUB rendering and EPUB format handling

**Heltec V4:**
- [X] WiFi remote repeater with MQTT admin management
- [X] Headless WiFi repeater variant (no display)

**Heltec V3:**
- [X] WiFi remote repeater with MQTT admin management

## 📞 Get Support

- Join [MeshCore Discord](https://discord.gg/KWFeY45sN) to chat with the developers and get help from the community.

## 📜 License

The upstream [MeshCore](https://github.com/meshcore-dev/MeshCore) library is released under the **MIT License** (Copyright © 2025 Scott Powell / rippleradios.com). Meck-specific code (UI screens, display helpers, device integration) is also provided under the MIT License.

However, this firmware links against libraries with different license terms. Because some dependencies use the **GPL-3.0** copyleft license (GxEPD2, ESP32-audioI2S) and others use **LGPL-2.1** (Codec2, ESPAsyncWebServer, Arduino_LPS22HB), the combined firmware binary is effectively subject to GPL-3.0 obligations when distributed. Please review the individual licenses below if you intend to redistribute or modify this firmware.

### Third-Party Libraries

| Library | License | Author / Source |
|---------|---------|-----------------|
| [MeshCore](https://github.com/meshcore-dev/MeshCore) | MIT | Scott Powell / rippleradios.com |
| [RadioLib](https://github.com/jgromes/RadioLib) | MIT | Jan Gromeš |
| [GxEPD2](https://github.com/ZinggJM/GxEPD2) | GPL-3.0 | Jean-Marc Zingg (T-Deck Pro) |
| [FastEPD](https://github.com/bitbank2/FastEPD) | Apache-2.0 | Larry Bank / bitbank2 (T5S3) |
| [Adafruit GFX Library](https://github.com/adafruit/Adafruit-GFX-Library) | BSD | Adafruit |
| [SensorLib](https://github.com/lewisxhe/SensorLib) | MIT | Lewis He (T5S3 touch/RTC) |
| [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S) | GPL-3.0 | schreibfaul1 / Wolle |
| [Codec2](https://github.com/sh123/esp32_codec2_arduino) | LGPL-2.1 | sh123 (ESP32 port) |
| [JPEGDEC](https://github.com/bitbank2/JPEGDEC) | Apache-2.0 | Larry Bank / bitbank2 |
| [PNGdec](https://github.com/bitbank2/PNGdec) | Apache-2.0 | Larry Bank / bitbank2 |
| [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) | LGPL-2.1 | Hristo Gochkov / me-no-dev (OTA) |
| [PubSubClient](https://github.com/knolleary/pubsubclient) | MIT | Nick O'Leary (MQTT) |
| [Arduino Crypto](https://github.com/rweather/arduinolibs) | MIT | Rhys Weatherley |
| [base64](https://github.com/Densaugeo/base64_arduino) | MIT | densaugeo |
| [CRC32](https://github.com/bakercp/CRC32) | MIT | Christopher Baker |
| [RTClib](https://github.com/adafruit/RTClib) | MIT | Adafruit |
| [Melopero RV3028](https://github.com/melopero/Melopero_RV-3028_Arduino_Library) | MIT | Melopero |
| [MicroNMEA](https://github.com/stevemarple/MicroNMEA) | MIT | Steve Marple (GPS) |
| [CayenneLPP](https://github.com/ElectronicCats/CayenneLPP) | MIT | Electronic Cats |
| [Adafruit ST7735/ST7789](https://github.com/adafruit/Adafruit-ST7735-Library) | MIT | Adafruit (Heltec V4 TFT) |
| [INA226](https://github.com/RobTillaart/INA226) | MIT | Rob Tillaart |
| [Arduino_LPS22HB](https://github.com/arduino-libraries/Arduino_LPS22HB) | LGPL-2.1 | Arduino |
| Adafruit sensor drivers¹ | MIT / BSD | Adafruit |
| [Sensirion I2C SHT4x](https://github.com/Sensirion/arduino-i2c-sht4x) | BSD-3-Clause | Sensirion |

¹ Includes INA219, INA260, INA3221, AHTX0, BME280, BMP280, BME680, BMP085, SHTC3, MLX90614, VL53L0X — all MIT or BSD licensed. Used via the sensor manager for optional environmental monitoring.

Full license texts for each dependency are available in their respective repositories linked above.