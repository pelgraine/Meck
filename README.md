## Meshcore + Fork = Meck 
This fork was created specifically to focus on enabling BLE companion firmware for the LilyGo T-Deck Pro. Created with the assistance of Claude AI using Meshcore v1.11 code. 

### Contents
- [T-Deck Pro Keyboard Controls](#t-deck-pro-keyboard-controls)
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
- [About MeshCore](#about-meshcore)
- [What is MeshCore?](#what-is-meshcore)
- [Key Features](#key-features)
- [What Can You Use MeshCore For?](#what-can-you-use-meshcore-for)
- [How to Get Started](#how-to-get-started)
- [MeshCore Flasher](#meshcore-flasher)
- [MeshCore Clients](#meshcore-clients)
- [Hardware Compatibility](#-hardware-compatibility)
- [License](#-license)
- [Contributing](#contributing)
- [Road-Map / To-Do](#road-map--to-do)
- [Get Support](#-get-support)

## T-Deck Pro Keyboard Controls

The T-Deck Pro BLE companion firmware includes full keyboard support for standalone messaging without a phone.

### Navigation (Home Screen)

| Key | Action |
|-----|--------|
| W / A | Previous page |
| D | Next page |
| Enter | Select / Confirm |
| M | Open channel messages |
| C | Open contacts list |
| E | Open e-book reader |
| S | Open settings |
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
| Q | Back to home screen |

### Contacts Screen

Press **C** from the home screen to open the contacts list. All known mesh contacts are shown sorted by most recently seen, with their type (Chat, Repeater, Room, Sensor), hop count, and time since last advert.

| Key | Action |
|-----|--------|
| W / S | Scroll up / down through contacts |
| A / D | Cycle filter: All → Chat → Repeater → Room → Sensor |
| Enter | Open DM compose (Chat contact) or repeater admin (Repeater contact) |
| Q | Back to home screen |

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
| Channels | View existing channels, add hashtag channels, or delete non-primary channels (X) |
| Device Info | Public key and firmware version (read-only) |

When adding a hashtag channel, type the channel name and press Enter. The channel secret is automatically derived from the name via SHA-256, matching the standard MeshCore hashtag convention.

If you've changed radio parameters, pressing Q will prompt you to apply changes before exiting.

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

## About MeshCore

MeshCore is a lightweight, portable C++ library that enables multi-hop packet routing for embedded projects using LoRa and other packet radios. It is designed for developers who want to create resilient, decentralized communication networks that work without the internet.

## What is MeshCore?

MeshCore now supports a range of LoRa devices, allowing for easy flashing without the need to compile firmware manually. Users can flash a pre-built binary using tools like Adafruit ESPTool and interact with the network through a serial console.
MeshCore provides the ability to create wireless mesh networks, similar to Meshtastic and Reticulum but with a focus on lightweight multi-hop packet routing for embedded projects. Unlike Meshtastic, which is tailored for casual LoRa communication, or Reticulum, which offers advanced networking, MeshCore balances simplicity with scalability, making it ideal for custom embedded solutions., where devices (nodes) can communicate over long distances by relaying messages through intermediate nodes. This is especially useful in off-grid, emergency, or tactical situations where traditional communication infrastructure is unavailable.

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
- Flash the MeshCore firmware on a supported device.
- Connect with a supported client.

For developers;

- Install [PlatformIO](https://docs.platformio.org) in [Visual Studio Code](https://code.visualstudio.com).
- Clone and open the MeshCore repository in Visual Studio Code.
- See the example applications you can modify and run:
  - [Companion Radio](./examples/companion_radio) - For use with an external chat app, over BLE, USB or WiFi.

## MeshCore Flasher

Download a copy of the Meck firmware bin from https://github.com/pelgraine/Meck/releases, then:

- Launch https://flasher.meshcore.co.uk
- Select Custom Firmware
- Select the .bin file you just downloaded, and click Open or press Enter.
- Click Flash, then select your device in the popup window (eg. USB JTAG/serial debug unit cu.usbmodem101 as an example), then click Connect.
- Once flashing is complete, you can connect with one of the MeshCore clients below.

## MeshCore Clients

**Companion Firmware**

The companion firmware can be connected to via BLE.

> **Note:** On the T-Deck Pro, BLE is disabled by default at boot. Navigate to the Bluetooth home page and press Enter to enable BLE before connecting with a companion app.

- Web: https://app.meshcore.nz
- Android: https://play.google.com/store/apps/details?id=com.liamcottle.meshcore.android
- iOS: https://apps.apple.com/us/app/meshcore/id6742354151?platform=iphone
- NodeJS: https://github.com/liamcottle/meshcore.js
- Python: https://github.com/fdlamotte/meshcore-cli

## 🛠 Hardware Compatibility

MeshCore is designed for devices listed in the [MeshCore Flasher](https://flasher.meshcore.co.uk)

## 📜 License

MeshCore is open-source software released under the MIT License. You are free to use, modify, and distribute it for personal and commercial projects.

## Contributing

Please submit PR's using 'dev' as the base branch!
For minor changes just submit your PR and I'll try to review it, but for anything more 'impactful' please open an Issue first and start a discussion. Is better to sound out what it is you want to achieve first, and try to come to a consensus on what the best approach is, especially when it impacts the structure or architecture of this codebase.

Here are some general principals you should try to adhere to:
* Keep it simple. Please, don't think like a high-level lang programmer. Think embedded, and keep code concise, without any unnecessary layers.
* No dynamic memory allocation, except during setup/begin functions.
* Use the same brace and indenting style that's in the core source modules. (A .clang-format is prob going to be added soon, but please do NOT retroactively re-format existing code. This just creates unnecessary diffs that make finding problems harder)

## Road-Map / To-Do

There are a number of fairly major features in the pipeline, with no particular time-frames attached yet. In partly chronological order:
- [X] Companion radio: BLE
- [X] Text entry for Public channel messages Companion BLE firmware
- [X] View and compose all channel messages Companion BLE firmware
- [X] Standalone DM functionality for Companion BLE firmware
- [X] Contacts list with filtering for Companion BLE firmware
- [X] Standalone repeater admin access for Companion BLE firmware
- [X] GPS time sync with on-device timezone setting
- [X] Settings screen with radio presets, channel management, and first-boot onboarding
- [ ] Fix M4B rendering to enable chaptered audiobook playback
- [ ] Expand SMS app to enable phone calls
- [ ] Better JPEG and PNG decoding
- [ ] Improve EPUB rendering and EPUB format handling
- [ ] Map support with GPS
- [ ] Basic web reader app for text-centric websites

## 📞 Get Support

- Join [MeshCore Discord](https://discord.gg/BMwCtwHj5V) to chat with the developers and get help from the community.