## Audiobook Player (Audio variant only)

Press **P** from the home screen to open the audiobook player.
Place `.mp3`, `.m4b`, `.m4a`, or `.wav` files in `/audiobooks/` on the SD card.
Files can be organised into subfolders (e.g. by author) — use **Enter** to
browse into folders and **.. (up)** to go back.

| Key | Action |
|-----|--------|
| W / S | Scroll file list / Volume up-down |
| Enter | Select book or folder / Play-Pause |
| A | Seek back 30 seconds |
| D | Seek forward 30 seconds |
| [ | Previous chapter (M4B only) |
| ] | Next chapter (M4B only) |
| Q | Leave player (audio continues) / Close book (when paused) / Exit (from file list) |

### Recommended Format

**MP3 is the recommended format.** M4B/M4A files are supported but currently
have playback issues with the ESP32-audioI2S library — some files may fail to
decode or produce silence. MP3 files play reliably and are the safest choice.

MP3 files should be encoded at a **44100 Hz sample rate**. Lower sample rates
(e.g. 22050 Hz) can cause distortion or playback failure due to ESP32-S3 I2S
hardware limitations.

**Bookmarks** are saved automatically every 30 seconds during playback and when
you stop or exit. Reopening a book resumes from your last position.

**Cover art** from M4B files is displayed as dithered monochrome on the e-ink
screen, along with title, author, and chapter information.

**Metadata caching** — the first time you open the audiobook player, it reads
title and author tags from each file (which can take a few seconds with many
files). This metadata is cached to the SD card so subsequent visits load
near-instantly. If you add or remove files the cache updates automatically.

### Background Playback

Audio continues playing when you leave the audiobook player screen. Press **Q**
while audio is playing to return to the home screen — a **>>** indicator will
appear in the status bar next to the battery icon to show that audio is active
in the background. Press **P** at any time to return to the player screen and
resume control.

If you pause or stop playback first and then press **Q**, the book is closed
and you're returned to the file list instead.

### Audio Hardware

The audiobook player uses the PCM5102A I2S DAC on the audio variant of the
T-Deck Pro (I2S pins: BCLK=7, DOUT=8, LRC=9). Audio is output via the 3.5mm
headphone jack.

> **Note:** The audiobook player is not available on the 4G modem variant
> due to I2S pin conflicts.

### SD Card Folder Structure

```
SD Card
├── audiobooks/
│   ├── .bookmarks/          (auto-created, stores resume positions)
│   │   ├── mybook.bmk
│   │   └── another.bmk
│   ├── .metacache            (auto-created, speeds up file list loading)
│   ├── Ann Leckie/
│   │   ├── Ancillary Justice.mp3
│   │   └── Ancillary Sword.mp3
│   ├── Iain M. Banks/
│   │   └── The Algebraist.mp3
│   ├── mybook.mp3
│   └── podcast.mp3
├── books/                   (existing — text reader)
│   └── ...
└── ...
```