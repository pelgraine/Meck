# SOS / emergency mode

For trips without mobile signal: send your position to everyone in LoRa
range with one deliberate gesture, keep repeating it, and call it off just
as reliably.

## Before the trip

1. **Settings > SOS**: the public channel always gets the SOS. Switch on any
   other channel that people you trust listen to (a group channel with
   friends or family, a regional emergency channel), up to 8. They only see
   it if their node has the same channel. The choice follows the channel
   itself, so deleting or reordering channels does not move it.
2. Tell them that an SOS from you looks like
   `SOS! Need help. [LOC]45.598765,24.612345 fix 2m bat 63% #3`.
3. Get a GPS fix once before you set off (Home > GPS). A warm GPS finds the
   next fix much faster.
4. Charge the battery. Light sleep while locked (Settings > Power saving
   when locked) has to be on for the SOS to save power between sends.
5. Try the countdown and cancel it (see below). Nothing is sent if you cancel
   within the countdown.

## Sending an SOS

**Hold the top button for 3 seconds.** It works locked, with the screen
off and in light sleep. A 10-second countdown starts: the screen shows
"SENDING SOS IN 10", the keyboard light flashes and the motor buzzes every
second (T-Deck MAX). **Any key, touch, pad or a new press of the button
cancels it.** When the countdown ends the SOS goes out.

Without the button: Home tiles, last page, **SOS**. Press the top button
until **SOS** is selected, then **Space**. That countdown is 5 seconds.

## While the SOS runs

- Sent at once, then after 1, 2 and 5 minutes, then every 10 minutes, until
  you stop it.
- Before each send the GPS is switched on to find a fix, and off again once
  it has one. The device sleeps between sends.
- The position in the message is at most 2 hours old, with its age. An
  older one is not sent ("Position unknown, last fix 3h10m ago"). When a
  send went out without a fresh position and a fix comes in afterwards, an
  update is sent at once.
- The screen (also the lock screen) shows the position in large type,
  how many times the SOS went out and when the next send is, how many
  repeaters passed it on, the battery and the last message that came in
  (a DM, or the public or an SOS channel), even if the channel is muted.
- **"Heard by: nobody yet"** means no repeater was heard passing it on.
  Your SOS may still have reached a node directly, but nothing confirms it.

The device locks itself after a minute without input, so it can sleep. The
e-ink screen keeps showing the SOS status meanwhile.

## I'm OK / Stop

Open the menu: hold the top button 3 s again (or unlock with a double
click). Nothing is selected at first. Press the top button to select:

- **I'm OK** + Space: the SOS stops and "I'm OK - cancel SOS." goes out
  instead, on the same schedule, until it was sent at least twice **and** a
  repeater was heard passing it on. Until then the screen says
  "NOT confirmed": a rescue may still be on its way.
- **Stop** + Space: ends everything without a message.
- **SOS** + Space (while I'm OK is being sent): a new SOS.

Without a running SOS, **I'm OK** sends a single check-in with your
position.

Q backs out of the menu. Enter does nothing on this screen.

## Limits

- LoRa reaches only nodes and repeaters in range. In remote mountains nobody
  may hear it at all.
- A repeater echo proves the message entered the mesh, not that a person
  read it.
- The public channel is not a rescue service. In Romania call **112** as
  soon as you have any signal (an emergency call uses any network that is
  available, not only your own); it alerts the mountain rescue (Salvamont).
  Read out the coordinates from the screen. Look up and save Salvamont's
  own number before the trip.
- Battery: GPS fixes and the radio waking for packets still cost power. With
  light sleep between sends it lasts much longer than staying awake; measure
  it before relying on it (`pwrlog`).
- If you switch GPS on yourself while the SOS runs, the SOS takes it over;
  your GPS setting applies again once the SOS ends.

## Debug CLI (meck_max_ui2)

`sos` prints the state. `sos dry 1` turns sending into a serial print and
silences the buzz and the flashing; `sos hold` and `sos menu` need it.
`sos fix <lat> <lon>` fakes a GPS fix (`sos fix` alone ends it and forgets
the made-up position), `sos text`
prints the messages as they would go out now, `sos stop` ends the SOS.
On the MAX, `sos haptic` prints the motor driver's state without a buzz;
`sos pulse` (one countdown pulse) and `sos vib e <effect>` / `sos vib rtp
<ms>` do buzz.

The countdown pulse drives the motor at full strength for half a second
(real-time mode) rather than playing a library effect, which was hard to
feel.

Unit tests of the logic: `test/sos/test_sos.cpp` (plain C++, see the
command at the top of the file).
