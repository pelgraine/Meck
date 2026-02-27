## SMS & Phone App (4G variant only) - Meck v0.9.5

Press **T** from the home screen to open the SMS & Phone app.
Requires a nano SIM card inserted in the T-Deck Pro V1.1 4G modem slot and an
SD card formatted as FAT32. The modem registers on the cellular network
automatically at boot — the red LED on the board indicates the modem is
powered. The modem (and its red LED) can be switched off and on from the
settings screen. After each modem startup, the system clock syncs from the
cellular network, which takes roughly 15 seconds.

### App Menu

The SMS & Phone app opens to a landing screen with two options:

| Option | Description |
|--------|-------------|
| **Phone** | Open the phone dialer to call any number |
| **SMS Inbox** | Open the SMS inbox for messaging and calling saved contacts |

Use **W / S** to select an option and **Enter** to confirm. Press **Q** to
return to the home screen.

### Key Mapping

| Context | Key | Action |
|---------|-----|--------|
| Home screen | T | Open SMS & Phone app |
| App menu | W / S | Select Phone or SMS Inbox |
| App menu | Enter | Open selected option |
| App menu | Q | Back to home screen |
| Inbox | W / S | Scroll conversations |
| Inbox | Enter | Open conversation |
| Inbox | C | Compose new SMS (enter phone number) |
| Inbox | D | Open contacts directory |
| Inbox | Q | Back to app menu |
| Conversation | W / S | Scroll messages |
| Conversation | C | Reply to this conversation |
| Conversation | F | Call this number |
| Conversation | A | Add or edit contact name for this number |
| Conversation | Q | Back to inbox |
| Compose | Enter | Send SMS (from body) / Confirm phone number (from phone input) |
| Compose | Shift+Del | Cancel and return |
| Contacts | W / S | Scroll contact list |
| Contacts | Enter | Compose SMS to selected contact |
| Contacts | F | Call selected contact |
| Contacts | Q | Back to inbox |
| Edit Contact | Enter | Save contact name |
| Edit Contact | Shift+Del | Cancel without saving |
| Phone Dialer | 0–9, *, +, # | Enter phone number (see input methods below) |
| Phone Dialer | Enter | Place call |
| Phone Dialer | Backspace | Delete last digit |
| Phone Dialer | Q | Back to app menu |
| Dialing | Enter or Q | Cancel / hang up |
| Incoming Call | Enter | Answer call |
| Incoming Call | Q | Reject call |
| In Call | Enter or Q | Hang up |
| In Call | W / S | Volume up / down (0–5) |
| In Call | 0–9, *, # | Send DTMF tone |

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

### Making a Phone Call

There are three ways to start a call:

1. **From the phone dialer** — select **Phone** from the app menu to open the
   dialer. Enter a phone number and press **Enter** to call. This is the
   easiest way to call a number you haven't messaged before.
2. **From a conversation** — open a conversation and press **F**. You can call
   any number you have previously exchanged messages with, whether or not it is
   saved as a named contact.
3. **From the contacts directory** — press **D** from the inbox, scroll to a
   contact, and press **F**.

The display switches to a dialing screen showing the contact name (or phone
number) and an animated progress indicator. Once the remote party answers, the
screen transitions to the in-call view with a live call timer.

During an active call, **W** and **S** adjust the speaker volume (0–5). The
number keys **0–9**, **\***, and **#** send DTMF tones for navigating phone
menus and voicemail systems. Press **Enter** or **Q** to hang up.

Audio is routed through the A7682E modem's internal codec to the board speaker
and microphone — no headphones or external audio hardware are required.

### Phone Dialer Input Methods

The phone dialer supports three ways to enter digits:

1. **Direct key press** — press the keyboard letter that corresponds to each
   number using the silk-screened labels on the T-Deck Pro keys:

   | Key | Digit | | Key | Digit | | Key | Digit |
   |-----|-------|-|-----|-------|-|-----|-------|
   | W | 1 | | S | 4 | | Z | 7 |
   | E | 2 | | D | 5 | | X | 8 |
   | R | 3 | | F | 6 | | C | 9 |
   | A | * | | O | + | | Mic | 0 |

2. **Touchscreen tap** — tap the on-screen number buttons directly. Note: this
   currently requires fairly precise taps on the numbers themselves.

3. **Sym+key** — the standard symbol entry method (e.g. Sym+W for 1, Sym+S for
   4, etc.)

### Receiving a Phone Call

When an incoming call arrives, the app automatically switches to the incoming
call screen regardless of which view is active. A short alert and buzzer
notification are triggered. The caller's name is shown if saved in contacts,
otherwise the raw phone number is displayed.

Press **Enter** to answer or **Q** to reject the call. If the call is not
answered it is logged as a missed call and a "Missed: ..." alert is shown
briefly.

### Contacts

The contacts directory lets you assign display names to phone numbers.
Names appear in the inbox list, conversation headers, call screens, and
compose screen instead of raw numbers.

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
LED stay off until re-enabled. The SMS & Phone app remains accessible when the
modem is off but will not be able to send or receive messages or calls.

### Signal Indicator

A signal strength indicator is shown in the top-right corner of all SMS and
call screens. Bars are derived from the modem's CSQ (signal quality) reading,
updated every 30 seconds. The modem state (REG, READY, OFF, etc.) is shown
when not yet connected. During a call, the signal indicator remains visible.

### IMEI, Carrier & APN

The 4G modem's IMEI, current carrier name, and APN are displayed at the bottom
of the settings screen (press **S** from the home screen), alongside your node
ID and firmware version.

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
| Modem icon stays at REG / never reaches READY | SIM not inserted, no signal, or SIM requires PIN unlock (not currently supported) |
| Timestamps show `---` | Modem clock hasn't synced yet (wait ~15 seconds after modem startup), or messages were saved before clock sync was available |
| Red LED stays on after disabling modem | Toggle the setting off, then reboot — the boot sequence ensures power is cut when disabled |
| SMS sends but no delivery | Check signal strength; below 5 bars is marginal. Move to better coverage |
| Call drops immediately after dialing | Check signal strength and ensure the SIM plan supports voice calls |
| No audio during call | The A7682E routes audio through its own codec; ensure the board speaker is not obstructed. Try adjusting volume with W/S |

> **Note:** The SMS & Phone app is only available on the 4G modem variant of
> the T-Deck Pro. It is not present on the audio or standalone BLE builds due
> to shared GPIO pin conflicts between the A7682E modem and PCM5102A DAC.