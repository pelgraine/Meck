## SMS & Phone App (Pro 4G & Max) - Meck v1.12.6

Press **T** from the home screen to open the SMS & Phone app.

Available on the **T-Deck Pro 4G** and the **T-Deck Pro Max**. Requires a nano
SIM card in the A7682E modem slot and an SD card formatted as FAT32. The modem
registers on the cellular network automatically at boot -- the red LED on the
board indicates the modem is powered. The modem (and its red LED) can be
switched off and on from the settings screen. After each modem startup the
system clock syncs from the cellular network, which takes roughly 15 seconds.

**Note for US users:** the A7682E supports very limited US cellular bands, so
4G is almost entirely unusable with US SIMs. It is wholly unusable on T-Mobile
and may have very limited use on AT&T.

### App Menu

The app opens to a landing screen with four entries:

| Row | Description |
|-----|-------------|
| **Dial** | Keypad for calling any number |
| **Call Log** | Incoming, outgoing and missed calls, stored on the SD card |
| **Contacts** | Saved contacts -- add, edit, dial, message and delete |
| **SMS Inbox** | Message conversations |

Use **W / S** to move between rows and **Enter** to open one. Press **Q** to
return to the home screen.

Two rows carry a count in brackets:

- **Call Log [n]** -- missed calls you have not yet viewed. Clears when you
  open the Call Log.
- **SMS Inbox [n]** -- unread received messages. Clears per conversation as
  you read them.

Both counts are held on the SD card rather than in memory, so they survive a
reboot and only clear when you actually open the relevant screen.

### Key Mapping

| Context | Key | Action |
|---------|-----|--------|
| Home screen | T | Open SMS & Phone app |
| App menu | W / S | Move between the four rows |
| App menu | Enter | Open selected row |
| App menu | Q | Back to home screen |
| Call Log | W / S | Scroll entries |
| Call Log | Enter | Dial the highlighted entry back |
| Call Log | D | Delete the highlighted entry |
| Call Log | Q | Back to app menu |
| Contacts | W / S | Scroll contact list |
| Contacts | Enter | Open the contact page |
| Contacts | A | Add a new contact |
| Contacts | M | Compose an SMS to the selected contact |
| Contacts | F | Call the selected contact |
| Contacts | Q | Back to app menu (or to the inbox if opened from there) |
| Contact page | W / S | Move between Name, Number and Save |
| Contact page | Enter | Edit the highlighted field, or save from the Save row |
| Contact page | F | Dial this contact |
| Contact page | D | Delete this contact |
| Contact page | Q | Back without saving |
| Field editing | Enter | Commit the field |
| Field editing | Backspace | Delete last character |
| Field editing | Shift+Del | Abandon this field edit |
| Inbox | W / S | Scroll conversations |
| Inbox | Enter | Open conversation |
| Inbox | C | Compose new SMS (enter phone number) |
| Inbox | D | Open contacts list |
| Inbox | Q | Back to app menu |
| Conversation | W / S | Scroll messages |
| Conversation | C | Reply to this conversation |
| Conversation | F | Call this number |
| Conversation | A | Add or edit contact name for this number |
| Conversation | Q | Back to inbox |
| Compose | Enter | Send SMS (from body) / Confirm phone number (from phone input) |
| Compose | Shift+Del | Cancel and return |
| Phone Dialer | 0-9, *, +, # | Enter phone number (see input methods below) |
| Phone Dialer | Enter | Place call |
| Phone Dialer | Backspace | Delete last digit |
| Phone Dialer | Shift+Del | Back to app menu |
| Dialing | Enter or Shift+Del | Cancel / hang up |
| Incoming Call | Enter | Answer call |
| Incoming Call | Shift+Del | Reject call |
| In Call | Enter or Shift+Del | Hang up |
| In Call | W / S | Volume up / down (0-5) |
| In Call | 0-9, *, # | Send DTMF tone |

**Exit key:** across the T-Deck Pro and Max interface, **Q** is the back key.
The exception is anywhere you are typing -- while text or number entry is
active, Q is a normal character, so **Shift+Del** exits instead. That is why
the dialer and the call screens use Shift+Del rather than Q.

### Call Log

The Call Log records every call the device handles, stored on the SD card at
`/sms/calllog.dat` so it survives reboots. The **32 most recent** calls are
kept; older entries are trimmed automatically as new ones arrive.

Each entry shows the contact name where the number is saved (the raw number
otherwise), the call type, the duration for calls that connected, and the
local date and time.

| Type | Meaning |
|------|---------|
| **Missed** | Rang but was not answered |
| **In** | Incoming call that connected, with duration |
| **Out** | Outgoing call that connected, with duration |

Outgoing attempts that never connect -- busy, or no answer -- are logged as
**Out** with no duration. Calls that arrive with no caller ID are still
logged, listed as **Unknown**; Enter does nothing on those entries as there is
no number to dial back.

Press **Enter** on an entry to call that number back, or **D** to delete the
entry. Opening the Call Log clears the missed-call badge on the app menu and
on the lock screen.

### Contacts

Contacts assign display names to phone numbers. Names appear in the inbox
list, conversation headers, call screens, the call log and the compose screen
instead of raw numbers.

Open **Contacts** from the app menu to see the saved list. From there:

- **A** adds a new contact.
- **Enter** opens the highlighted contact.
- **M** composes an SMS to them.
- **F** calls them directly.

**The contact page** is used for both adding and editing. It has three rows --
**Name**, **Number** and **Save**. Move between them with W / S. Press
**Enter** on Name or Number to type into that field: Backspace deletes,
**Enter** commits the field, and **Shift+Del** abandons that edit. Press
**Enter** on **Save** to write the contact to the SD card.

A contact cannot be saved without a number, since the number is what the
entry is stored against -- attempting to do so leaves you on the page with a
"A number is required" prompt. Changing the number of an existing contact
replaces the old entry rather than creating a second one.

Press **F** on a saved contact to dial it, or **D** to delete it. **Q** leaves
the page **without saving**, so commit your changes on the Save row first.

Contacts can also still be created from a conversation: press **A** while
reading messages from a number to name it.

Contacts are stored as a plain text file at `/sms/contacts.txt` in
`phone=Display Name` format -- one per line, human-editable. Up to **30**
contacts are supported, and names can be up to **23** characters.

### Sending an SMS

There are four ways to start a new message:

1. **From the inbox** -- press **C**, type the destination number, press
   **Enter**, then type your message and press **Enter** to send.
2. **From a conversation** -- press **C** to reply. The recipient is
   pre-filled, so you go straight to the message body.
3. **From the contacts list** -- press **M** on a contact. The compose screen
   opens with the number pre-filled.
4. **From the inbox contacts view** -- press **D** from the inbox to reach the
   same contacts list.

Messages are limited to 160 characters. A character counter is shown in the
footer while composing.

### Making a Phone Call

There are four ways to start a call:

1. **From the dialer** -- select **Dial** from the app menu, enter a number
   and press **Enter**.
2. **From the Call Log** -- press **Enter** on any entry to call it back.
3. **From a conversation** -- press **F** to call the number you are reading.
4. **From contacts** -- press **F** on the list, or **F** on the contact page.

The display switches to a dialing screen showing the contact name (or number)
and an animated progress indicator. Once the remote party answers, the screen
transitions to the in-call view with a live call timer.

During a call, **W** and **S** adjust the speaker volume (0-5). The number
keys **0-9**, **\***, and **#** send DTMF tones for navigating phone menus and
voicemail. Press **Enter** or **Shift+Del** to hang up.

Audio is routed through the A7682E modem's internal codec to the board speaker
and microphone -- no headphones or external audio hardware are required.

### Phone Dialer Input Methods

The dialer supports three ways to enter digits:

1. **Direct key press** -- press the keyboard letter that corresponds to each
   number using the silk-screened labels on the keys:

   | Key | Digit | | Key | Digit | | Key | Digit |
   |-----|-------|-|-----|-------|-|-----|-------|
   | W | 1 | | S | 4 | | Z | 7 |
   | E | 2 | | D | 5 | | X | 8 |
   | R | 3 | | F | 6 | | C | 9 |
   | A | * | | O | + | | Mic | 0 |

2. **Touchscreen tap** -- tap the on-screen buttons directly. The bottom row
   of the keypad carries **+**, **Backspace** and **Call**, so a number can be
   dialled entirely by touch. Note that this currently requires fairly precise
   taps on the buttons themselves.

3. **Sym+key** -- the standard symbol entry method (e.g. Sym+W for 1).

### Receiving a Phone Call

When an incoming call arrives, the app switches to the incoming call screen
regardless of which view is active. A short alert and buzzer notification are
triggered. The caller's name is shown if saved in contacts, otherwise the raw
number is displayed.

If the device is locked when a call arrives, it unlocks itself so the call can
be answered, and stays unlocked afterwards.

Press **Enter** to answer or **Shift+Del** to reject. If the call is not
answered it is recorded in the Call Log as a missed call, a "Missed: ..."
alert is shown briefly, and the missed-call count appears on the app menu and
lock screen.

### Lock Screen Notifications

On Pro 4G and Max builds, the lock screen shows any outstanding **unread SMS**
and **missed calls** below the battery and mesh unread line, for example
`SMS: 2  Missed: 1`. Either part is hidden when its count is zero.

These are read from the inbox and the call log rather than tallied while the
screen is locked, so they persist across locking, unlocking and rebooting.
They clear only when you open the relevant conversation or the Call Log.

### Conversation History

Messages are saved to the SD card automatically and persist across reboots.
Each phone number gets its own file under `/sms/`. The inbox shows the most
recent 20 conversations sorted by last activity. Within a conversation the
most recent 30 messages are loaded, newest at the bottom. Sent messages are
shown with `>>>` and received messages with `<<<`.

Message timestamps use the cellular network clock (synced via NITZ roughly 15
seconds after each modem startup) and display as relative times (e.g. 5m, 2h,
1d). If the modem is toggled off and back on, the clock re-syncs
automatically.

### Modem Power Control

The 4G modem can be toggled on or off from the settings screen. Scroll to
**4G Modem: ON/OFF** and press **Enter** to toggle. Switching the modem off
kills its red status LED and stops all cellular activity. The setting persists
to SD and is respected on subsequent boots -- if disabled, the modem and LED
stay off until re-enabled. The SMS & Phone app remains accessible when the
modem is off, but cannot send or receive messages or calls.

### Signal Indicator

A signal strength indicator is shown in the top-right corner of all SMS and
call screens. Bars are derived from the modem's CSQ (signal quality) reading,
updated every 30 seconds. The modem state (REG, READY, OFF, etc.) is shown
when not yet connected. During a call the indicator remains visible.

### IMEI, Carrier & APN

The modem's IMEI, current carrier name, and APN are displayed at the bottom of
the settings screen (press **S** from the home screen), alongside your node ID
and firmware version.

### SD Card Structure

```
SD Card
├── sms/
│   ├── contacts.txt           (plain text, phone=Name format)
│   ├── calllog.dat            (binary, 32 most recent calls)
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
| Red LED stays on after disabling modem | Toggle the setting off, then reboot -- the boot sequence ensures power is cut when disabled |
| SMS sends but no delivery | Check signal strength; below 5 bars is marginal. Move to better coverage |
| Call drops immediately after dialing | Check signal strength and ensure the SIM plan supports voice calls |
| No audio during call | The A7682E routes audio through its own codec; ensure the board speaker is not obstructed. Try adjusting volume with W/S |
| Contact will not save | The Number field is empty -- a contact is stored against its number, so one is required |
| Call Log entry shows "Unknown" | The network withheld the caller ID, or it did not arrive before the caller rang off. The call is still logged |
| Missed-call count will not clear | It clears when the Call Log is opened, not when the device is unlocked |

> **Note:** The SMS & Phone app is available on the 4G variants of the T-Deck
> Pro and on the T-Deck Pro Max. It is not present on the audio or standalone
> BLE builds of the T-Deck Pro, due to shared GPIO pin conflicts between the
> A7682E modem and the PCM5102A DAC. The Max avoids that conflict by wiring
> its modem and ES8311 codec through an XL9555 I/O expander, so it has both.