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
| `get path.hash.mode` | Path hash size (0=1-byte, 1=2-byte, 2=3-byte) |
| `get rxdelay` | Rx delay base (0=disabled) |
| `get af` | Airtime factor |
| `get multi.acks` | Redundant ACKs (0 or 1) |
| `get int.thresh` | Interference threshold (0=disabled) |
| `get tx.fail.threshold` | TX fail reset threshold (0=disabled, default 3) |
| `get rx.fail.threshold` | RX stuck reboot threshold (0=disabled, default 3) |
| `get gps.baud` | GPS baud rate (0=compile-time default) |
| `get region` | Default region scope (e.g. `au-nsw`, or `none`) |
| `get channels` | List all channels with index numbers and region scopes |
| `get channel.scope <idx>` | Show region scope for a specific channel |
| `get presets` | List all radio presets with parameters |
| `get pubkey` | Device public key (hex) |
| `get firmware` | Firmware version string |
| `clock` | Current RTC time (UTC + epoch) |

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

#### Path Hash Mode

Controls the byte size of each repeater's identity stamp in forwarded flood packets. Larger hashes reduce collisions at the cost of fewer maximum hops.

```
set path.hash.mode 1
```

| Mode | Bytes/hop | Max hops | Notes |
|------|-----------|----------|-------|
| 0 | 1 | 64 | Legacy — prone to hash collisions in larger networks |
| 1 | 2 | 32 | Recommended — effectively eliminates collisions |
| 2 | 3 | 21 | Maximum precision, rarely needed |

Nodes with different modes can coexist — the mode only affects packets your node originates. The hash size is encoded in each packet's header, so receiving nodes adapt automatically.

### Mesh Tuning

These settings control how the device participates in the mesh network. They take effect immediately — no reboot required (except `gps.baud`).

#### Rx Delay (rxdelay)

Delays processing of flood packets based on signal quality. Stronger signals are processed first; weaker copies wait longer and are typically discarded as duplicates. Direct messages are always processed immediately.

```
set rxdelay 3
```

Range: 0–20 (0 = disabled, default). Higher values create larger timing differences between strong and weak signals. Values below 1.0 have no practical effect. See the [MeshSydney wiki](https://meshsydney.com/wiki) for detailed tuning profiles.

#### Airtime Factor (af)

Adjusts how long certain internal timing windows remain open. Does not change the LoRa radio parameters (SF, BW, CR) — those remain as configured.

```
set af 1.0
```

Range: 0–9 (default: 1.0). Keep this value consistent across nodes in your mesh for best coherence.

#### Multiple Acknowledgments (multi.acks)

Sends redundant ACK packets for direct messages. When enabled, two ACKs are sent (a multi-ack first, then the standard ACK), improving delivery confirmation reliability.

```
set multi.acks 1
```

Values: 0 (single ACK) or 1 (redundant ACKs, default).

#### Interference Threshold (int.thresh)

Enables channel activity scanning before transmitting. Not recommended unless your device is in a high RF interference environment — specifically where the noise floor is low but shows significant fluctuations indicating interference. Enabling this adds approximately 4 seconds of receive delay per packet.

```
set int.thresh 14
set int.thresh 0
```

Values: 0 (disabled, default) or 14+ (14 is the typical setting). Values between 1–13 are not functional and will be rejected.

#### TX Fail Reset Threshold (tx.fail.reset)

Automatically resets the radio hardware after this many consecutive failed transmission attempts. This recovers from "zombie radio" states where the SX1262 stops responding to send commands.

```
set tx.fail.threshold 3
set tx.fail.threshold 0
```

Values: 0 (disabled) or 1–10 (default: 3). After the threshold is reached, the radio is reset and the failed packet is re-queued.

#### RX Stuck Reboot Threshold (rx.fail.reboot)

Automatically reboots the device after this many consecutive RX-stuck recovery failures. An RX-stuck event occurs when the radio is not in receive mode for 8 seconds despite automatic recovery attempts.

```
set rx.fail.threshold 3
set rx.fail.threshold 0
```

Values: 0 (disabled) or 1–10 (default: 3). A full device reboot is a last resort — this should only trigger in rare cases of persistent radio hardware malfunction.

#### GPS Baud Rate (gps.baud)

Override the GPS serial baud rate. The default (0) uses the compile-time value of 38400. **Requires a reboot to take effect** — the GPS serial port is only configured at startup.

```
set gps.baud 9600
set gps.baud 0
```

Valid rates: 0 (default), 4800, 9600, 19200, 38400, 57600, 115200.

#### Backlight (T5S3 E-Paper Pro Only)

Control the front-light on the T5S3 display:

```
set backlight on
set backlight off
set backlight 128
```

Values: `on`, `off`, or a brightness level from 0–255.

### Channel Management

#### List Channels

```
get channels
```

Output:

```
  [0] #public [*]
  [1] #meck-test [au-nsw]
  [2] #local-group [*]
```

Each channel shows its region scope in brackets. `[*]` means the channel uses the device default region (or unscoped if no default is set). A specific name like `[au-nsw]` means that channel has its own region override.

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

### Region Scope

Regions limit how far your flood messages propagate through the mesh. When you set a region, outgoing messages are tagged with a transport code that repeaters use to decide whether to forward them. Messages sent without a region reach all repeaters via the default wildcard, same as always.

Meck does not pre-set any region on a fresh flash. Region names are determined by your local mesh community — check with your local group for the names in use. Common patterns follow ISO 3166 country/subdivision codes (e.g. `au` for Australia, `gb-eng` for England, `us-ca` for California), but communities may also use custom names for their area.

Region names must be lowercase alphanumeric characters and hyphens only, max 29 characters.

#### View Default Region

```
get region
```

Output:

```
  > au-nsw
```

Or if no region is set:

```
  > (none — unscoped)
```

#### Set Default Region

```
set region au-nsw
```

This applies to all channels and DMs unless a channel has its own region override.

#### Clear Default Region

```
set region none
```

Returns to unscoped mode — messages reach all repeaters.

#### View Channel Region

```
get channel.scope 2
```

Output:

```
  > #local-group scope: au-syd
```

Or if the channel uses the device default:

```
  > #local-group scope: (device default)
```

#### Set Channel Region

```
set channel.scope 2 au-syd
```

This overrides the device default for that specific channel.

#### Clear Channel Region

```
set channel.scope 2 none
```

Returns the channel to using the device default region.

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

### Clock Sync

Set the device's real-time clock from a Unix timestamp. This is especially important for the T5S3 E-Paper Pro which has no GPS to auto-set the clock. These are standalone commands (not `get`/`set` prefixed) — matching the same `clock sync` command used on MeshCore repeaters.

#### View Current Time

```
clock
```

Output:

```
  > 2026-03-13 04:22:15 UTC (epoch: 1773554535)
```

If the clock has never been set:

```
  > not set (epoch: 0)
```

#### Sync Clock from Serial

```
clock sync 1773554535
```

The value must be a Unix epoch timestamp in the 2024–2036 range.

**Quick one-liner from your terminal (macOS / Linux / WSL):**

```
echo "clock sync $(date +%s)" > /dev/ttyACM0
```

Or paste directly into the Arduino IDE Serial Monitor:

```
clock sync 1773554535
```

**Tip:** On macOS/Linux, run `date +%s` to get the current epoch. On Windows PowerShell: `[int](Get-Date -UFormat %s)`.

#### Boot-Time Auto-Sync (T5S3)

When the T5S3 boots with no valid RTC time and detects a USB serial host is connected, it sends a `MECK_CLOCK_REQ` handshake over serial. If you're using PlatformIO's serial monitor (`pio device monitor`), the built-in `clock_sync` monitor filter responds automatically with the host computer's current time — no user action required. The sync appears transparently in the boot log:

```
MECK_CLOCK_REQ
  (Waiting 3s for clock sync from host...)
  > Clock synced to 1773554535
```

If no USB host is connected (e.g. running on battery), the sync window is skipped entirely with no boot delay.

**Manual fallback:** If you're using a serial terminal that doesn't have the filter (e.g. `screen`, PuTTY), you can paste a `clock sync` command during the 3-second window, or any time after boot:

```
clock sync $(date +%s)
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

### Switching to a Different Radio Preset

Moving from Australia to the US? One command:

```
set preset USA/Canada (Recommended)
```

Verify with:

```
get radio
```

### Setting Up Regions

Check with your local mesh community for the region names in use, then set your device default:

```
set region au-nsw
```

If you want a specific channel to use a different region (e.g. a nationwide channel):

```
set channel.scope 1 au
```

Verify everything:

```
get region
get channels
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