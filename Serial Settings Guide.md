# Meck Serial Settings Guide

Configure your T-Deck Pro's Meck firmware over USB serial — no companion app needed. Plug in a USB-C cable, open a serial terminal, and you have full access to every setting on the device.

## Getting Started

### What You Need

- T-Deck Pro running Meck firmware
- USB-C cable
- A serial terminal application:
  - **Windows:** PuTTY, TeraTerm, or the Arduino IDE Serial Monitor
  - **macOS:** `screen`, CoolTerm, or the Arduino IDE Serial Monitor
  - **Linux:** `screen`, `minicom`, `picocom`, or the Arduino IDE Serial Monitor

### Connection Settings

| Parameter | Value |
|-----------|-------|
| Baud rate | 115200 |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Line ending | CR (carriage return) or CR+LF |

### Quick Start (macOS / Linux)

```
screen /dev/ttyACM0 115200
```

On macOS the port is typically `/dev/cu.usbmodem*`. On Linux it is usually `/dev/ttyACM0` or `/dev/ttyUSB0`.

### Quick Start (Arduino IDE)

Open **Tools → Serial Monitor**, set baud to **115200** and line ending to **Carriage Return** or **Both NL & CR**.

Once connected, type `help` and press Enter to confirm everything is working.

---

## Command Reference

All commands follow a simple pattern: `get` to read, `set` to write.

### Viewing Settings

| Command | Description |
|---------|-------------|
| `get all` | Dump every setting at once |
| `get name` | Device name |
| `get freq` | Radio frequency (MHz) |
| `get bw` | Bandwidth (kHz) |
| `get sf` | Spreading factor |
| `get cr` | Coding rate |
| `get tx` | TX power (dBm) |
| `get radio` | All radio params in one line |
| `get utc` | UTC offset (hours) |
| `get notify` | Keyboard flash notification (on/off) |
| `get gps` | GPS status and interval |
| `get pin` | BLE pairing PIN |
| `get channels` | List all channels with index numbers |
| `get presets` | List all radio presets with parameters |
| `get pubkey` | Device public key (hex) |
| `get firmware` | Firmware version string |

**4G variant only:**

| Command | Description |
|---------|-------------|
| `get modem` | Modem enabled/disabled |
| `get apn` | Current APN |
| `get imei` | Device IMEI |

### Changing Settings

#### Device Name

```
set name MyNode
```

Names cannot contain these characters: `[ ] / \ : , ? *`

#### Radio Parameters (Individual)

Each of these applies immediately — no reboot required.

```
set freq 910.525
set bw 62.5
set sf 7
set cr 5
set tx 22
```

Valid ranges:

| Parameter | Min | Max |
|-----------|-----|-----|
| freq | 400.0 | 928.0 |
| bw | 7.8 | 500.0 |
| sf | 5 | 12 |
| cr | 5 | 8 |
| tx | 1 | Board max (typically 22) |

#### Radio Parameters (All at Once)

Set frequency, bandwidth, spreading factor, and coding rate in a single command:

```
set radio 910.525 62.5 7 5
```

#### Radio Presets

The easiest way to configure your radio. First, list the available presets:

```
get presets
```

This prints a numbered list like:

```
  Available radio presets:
     0  Australia                       915.800 MHz  BW250.0  SF10  CR5  TX22
     1  Australia (Narrow)              916.575 MHz  BW62.5   SF7   CR8  TX22
    ...
    14  USA/Canada (Recommended)        910.525 MHz  BW62.5   SF7   CR5  TX22
    15  Vietnam                         920.250 MHz  BW250.0  SF11  CR5  TX22
```

Apply a preset by name or number:

```
set preset USA/Canada (Recommended)
set preset 14
```

Preset names are case-insensitive, so `set preset australia` works too. The preset applies all five radio parameters (freq, bw, sf, cr, tx) and takes effect immediately.

#### UTC Offset

```
set utc 10
```

Range: -12 to +14.

#### Keyboard Notification Flash

Toggle whether the keyboard backlight flashes when a new message arrives:

```
set notify on
set notify off
```

#### BLE PIN

```
set pin 123456
```

### Channel Management

#### List Channels

```
get channels
```

Output:

```
  [0] #public
  [1] #meck-test
  [2] #local-group
```

#### Add a Hashtag Channel

```
set channel.add meck-test
```

The `#` prefix is added automatically if you omit it. The channel's encryption key is derived from the name (SHA-256), matching the same method used by the on-device Settings screen and companion apps.

#### Delete a Channel

```
set channel.del 2
```

Channels are referenced by their index number (shown in `get channels`). Channel 0 (public) cannot be deleted. Remaining channels are automatically compacted after deletion.

### 4G Modem (4G Variant Only)

#### Enable / Disable Modem

```
set modem on
set modem off
```

#### Set APN

```
set apn telstra.internet
```

To clear a custom APN and revert to auto-detection on next boot:

```
set apn
```

### System Commands

| Command | Description |
|---------|-------------|
| `reboot` | Restart the device |
| `rebuild` | Erase filesystem, re-save identity + prefs + contacts + channels |
| `erase` | Format the filesystem (caution: loses everything) |
| `ls UserData/` | List files on internal filesystem |
| `ls ExtraFS/` | List files on secondary filesystem |
| `cat UserData/<path>` | Dump file contents as hex |
| `rm UserData/<path>` | Delete a file |
| `help` | Show command summary |

---

## Common Workflows

### First-Time Setup

Plug in your new T-Deck Pro and run through these commands to get on the air:

```
set name YourCallsign
set preset Australia
set utc 10
set channel.add local-group
get all
```

### Switching to a New Region

Moving from Australia to the US? One command:

```
set preset USA/Canada (Recommended)
```

Verify with:

```
get radio
```

### Custom Radio Configuration

If none of the presets match your local group or you need specific parameters, set them directly. You can do it all in one command:

```
set radio 916.575 62.5 8 8
set tx 20
```

Or one parameter at a time if you're only adjusting part of your config:

```
set freq 916.575
set bw 62.5
set sf 8
set cr 8
set tx 20
```

Both approaches apply immediately. Confirm with `get radio` to double-check everything took:

```
get radio
  > freq=916.575 bw=62.5 sf=8 cr=8 tx=20
```

### Troubleshooting Radio Settings

If you're not sure what went wrong, dump everything:

```
get all
```

Compare the radio section against what others in your area are using. If you need to match exact parameters from another node:

```
set radio 916.575 62.5 7 8
set tx 22
```

### Backing Up Your Settings

Use `get all` to capture a snapshot of your configuration. Copy the serial output and save it — you can manually re-enter the settings after a firmware update or device reset if your SD card backup isn't available.

---

## Tips

- **All radio changes apply live.** There is no need to reboot after changing frequency, bandwidth, spreading factor, coding rate, or TX power. The radio reconfigures on the fly.
- **Preset selection by number is faster.** Once you've seen `get presets`, use the index number instead of typing the full name.
- **Settings are persisted immediately.** Every `set` command writes to flash. If power is lost, your settings are safe.
- **SD card backup is automatic.** If your T-Deck Pro has an SD card inserted, settings are backed up after every change. On a fresh flash, settings restore automatically from the SD card.
- **The `get all` command is your friend.** When in doubt, dump everything and check.