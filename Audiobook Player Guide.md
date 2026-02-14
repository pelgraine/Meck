## Audiobook Player (Audio variant only)

Press **P** from the home screen to open the audiobook player.
Place `.m4b`, `.m4a`, `.mp3`, or `.wav` files in `/audiobooks/` on the SD card.

| Key | Action |
|-----|--------|
| W / S | Scroll file list / Volume up-down |
| Enter | Select book / Play-Pause |
| A | Seek back 30 seconds |
| D | Seek forward 30 seconds |
| [ | Previous chapter (M4B only) |
| ] | Next chapter (M4B only) |
| Q | Leave player (audio continues) / Close book (when paused) / Exit (from file list) |

**Bookmarks** are saved automatically every 30 seconds during playback and when
you stop or exit. Reopening a book resumes from your last position.

**Cover art** from M4B files is displayed as dithered monochrome on the e-ink
screen, along with title, author, and chapter information.

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
> due to I2S pin conflicts. MP3 format is recommended over M4B for best
> compatibility with the ESP32-audioI2S library.

### SD Card Folder Structure

```
SD Card
├── audiobooks/
│   ├── .bookmarks/          (auto-created, stores resume positions)
│   │   ├── mybook.bmk
│   │   └── another.bmk
│   ├── mybook.m4b
│   ├── another.m4b
│   └── podcast.mp3
├── books/                   (existing — text reader)
│   └── ...
└── ...
```