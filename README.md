## Meshcore + Fork = Meck 
This fork was created specifically to focus on enabling BLE companion firmware for the LilyGo T-Deck Pro. Created with the assistance of Claude AI using Meshcore v1.11 code. 

⭐ ***Please note as of 1 Feb 2026, the T-Deck Pro repeater & usb firmware has not been finalised nor confirmed as functioning.*** ⭐

## T-Deck Pro Keyboard Controls

The T-Deck Pro BLE companion firmware includes full keyboard support for standalone messaging without a phone.

### Navigation (Home Screen)

| Key | Action |
|-----|--------|
| W / A | Previous page |
| S / D | Next page |
| Enter | Select / Confirm |
| M | Open channel messages |
| N | Open contacts list |
| R | Open e-book reader |
| Q | Back to home screen |

### Channel Message Screen

| Key | Action |
|-----|--------|
| W / S | Scroll messages up/down |
| A / D | Switch between channels |
| C | Compose new message |
| Q | Back to home screen |

### Contacts Screen

Press **N** from the home screen to open the contacts list. All known mesh contacts are shown sorted by most recently seen, with their type (Chat, Repeater, Room, Sensor), hop count, and time since last advert.

| Key | Action |
|-----|--------|
| W / S | Scroll up / down through contacts |
| A / D | Cycle filter: All → Chat → Repeater → Room → Sensor |
| Enter | Open DM compose (Chat contact) or repeater admin (Repeater contact) |
| C | Open DM compose to selected chat contact |
| Q | Back to home screen |

### Sending a Direct Message

Select a **Chat** contact in the contacts list and press **Enter** or **C** to start composing a direct message. The compose screen will show `DM: ContactName` in the header. Type your message and press **Enter** to send. The DM is sent encrypted directly to that contact (or flooded if no direct path is known). After sending or cancelling, you're returned to the contacts list.

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
| C | Enter compose mode (send raw CLI command) |
| Q | Back to contacts (from menu) or cancel login |

Command responses are displayed in a scrollable view. Use **W / S** to scroll long responses and **Q** to return to the menu.

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

The companion firmware can be connected to via BLE. USB is planned for a future update.

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
- [ ] Companion radio: USB
- [ ] Simple Repeater firmware for the T-Deck Pro
- [ ] Get pin 45 with the screen backlight functioning for the T-Deck Pro v1.1
- [ ] Canned messages function for Companion BLE firmware

## 📞 Get Support

- Join [MeshCore Discord](https://discord.gg/BMwCtwHj5V) to chat with the developers and get help from the community.