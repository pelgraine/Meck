## SMS App (4G variant only) - Meck v0.9.2

Press **T** from the home screen to open the SMS app.
Requires a nano SIM card inserted in the T-Deck Pro V1.1 4G modem slot and an
SD card formatted as FAT32. The modem registers on the cellular network
automatically at boot — the red LED on the board indicates the modem is
powered. The modem (and its red LED) can be switched off and on from the
settings screen. After each modem startup, the system clock syncs from the
cellular network, which takes roughly 15 seconds.

### Key Mapping

| Context | Key | Action |
|---------|-----|--------|
| Home screen | T | Open SMS app |
| Inbox | W / S | Scroll conversations |
| Inbox | Enter | Open conversation |
| Inbox | C | Compose new SMS (enter phone number) |
| Inbox | D | Open contacts directory |
| Inbox | Q | Back to home screen |
| Conversation | W / S | Scroll messages |
| Conversation | C | Reply to this conversation |
| Conversation | A | Add or edit contact name for this number |
| Conversation | Q | Back to inbox |
| Compose | Enter | Send SMS (from body) / Confirm phone number (from phone input) |
| Compose | Shift+Del | Cancel and return |
| Contacts | W / S | Scroll contact list |
| Contacts | Enter | Compose SMS to selected contact |
| Contacts | Q | Back to inbox |
| Edit Contact | Enter | Save contact name |
| Edit Contact | Shift+Del | Cancel without saving |

### Sending an SMS

There are three ways to start a new message:

1. **From inbox** — press **C**, type the destination phone number, press
   **Enter**, then type your message and press **Enter** to send.
2. **From a conversation** — press **C** to reply. The recipient is
   pre-filled so you go straight to typing the message body.
3. **From the contacts directory** — press **D** from the inbox, scroll to a
   contact, and press **Enter**. The compose screen opens with the number
   pre-filled.

Messages are limited to 160 characters (standard SMS). A character counter is
shown in the footer while composing.

### Contacts

The contacts directory lets you assign display names to phone numbers.
Names appear in the inbox list, conversation headers, and compose screen
instead of raw numbers.

To add or edit a contact, open a conversation with that number and press **A**.
Type the display name and press **Enter** to save. Names can be up to 23
characters long.

Contacts are stored as a plain text file at `/sms/contacts.txt` on the SD card
in `phone=Display Name` format — one per line, human-editable. Up to 30
contacts are supported.

### Conversation History

Messages are saved to the SD card automatically and persist across reboots.
Each phone number gets its own file under `/sms/` on the SD card. The inbox
shows the most recent 20 conversations sorted by last activity. Within a
conversation, the most recent 30 messages are loaded with the newest at the
bottom (chat-style). Sent messages are shown with `>>>` and received messages
with `<<<`.

Message timestamps use the cellular network clock (synced via NITZ roughly 15
seconds after each modem startup) and display as relative times (e.g. 5m, 2h,
1d). If the modem is toggled off and back on, the clock re-syncs automatically.

### Modem Power Control

The 4G modem can be toggled on or off from the settings screen. Scroll to
**4G Modem: ON/OFF** and press **Enter** to toggle. Switching the modem off
kills its red status LED and stops all cellular activity. The setting persists
to SD card and is respected on subsequent boots — if disabled, the modem and
LED stay off until re-enabled. The SMS app remains accessible when the modem
is off but will not be able to send or receive messages.

### Signal Indicator

A signal strength indicator is shown in the top-right corner of all SMS
screens. Bars are derived from the modem's CSQ (signal quality) reading,
updated every 30 seconds. The modem state (REG, READY, OFF, etc.) is shown
when not yet connected.

### SD Card Structure

```
SD Card
├── sms/
│   ├── contacts.txt           (plain text, phone=Name format)
│   ├── modem.cfg              (0 or 1, modem enable state)
│   ├── 0412345678.sms         (binary message log per phone number)
│   └── 0498765432.sms
├── books/                     (text reader)
├── audiobooks/                (audio variant only)
└── ...
```

### Troubleshooting

| Symptom | Likely Cause |
|---------|-------------|
| Modem stays at REG / never reaches READY | SIM not inserted, no signal, or SIM requires PIN unlock (not currently supported) |
| Timestamps show `---` | Modem clock hasn't synced yet (wait ~15 seconds after modem startup), or messages were saved before clock sync was available |
| Red LED stays on after disabling modem | Toggle the setting off, then reboot — the boot sequence ensures power is cut when disabled |
| SMS sends but no delivery | Check signal strength; CSQ below 5 is marginal. Move to better coverage |

> **Note:** The SMS app is only available on the 4G modem variant of the
> T-Deck Pro. It is not present on the audio or standalone BLE builds due to
> shared GPIO pin conflicts between the A7682E modem and PCM5102A DAC.