## Audiobook Player

The audiobook player is available on the T-Deck Pro audio variant and on all
T-Deck Max variants. Press **P** from the home screen to open it.
Place `.mp3`, `.m4a`, or `.wav` files in `/audiobooks/` on the SD card.
Files can be organised into subfolders (e.g. by author) — use **Enter** to
browse into folders and **.. (up)** to go back.

| Key | Action |
|-----|--------|
| W / S | Scroll file list / Volume up-down |
| Enter | Select book or folder / Play-Pause |
| A | Seek back 30 seconds |
| D | Seek forward 30 seconds |
| [ | Previous chapter |
| ] | Next chapter |
| N | Next track |
| Z | Toggle 45-minute sleep timer |
| Q | Leave player (audio continues) / Close book (when paused) / Exit (from file list) |

### Recommended Format

**MP3 is the recommended format.** M4A files are supported but currently
have playback issues with the ESP32-audioI2S library — some files may fail to
decode or produce silence. MP3 files play reliably and are the safest choice.

MP3 files should be encoded at a **44100 Hz sample rate**. Lower sample rates
(e.g. 22050 Hz) can cause distortion or playback failure due to ESP32-S3 I2S
hardware limitations.

Check a file's sample rate with ffprobe:

```bash
ffprobe -i "yourfile.mp3" 2>&1 | grep "Audio:"
# Audio: mp3, 48000 Hz, stereo, fltp, 192 kb/s    ← won't play
# Audio: mp3, 44100 Hz, stereo, fltp, 192 kb/s    ← will play
```

Re-encode in place if needed:

```bash
ffmpeg -i input.mp3 -ar 44100 output.mp3
```

For a whole album folder:

```bash
cd "/path/to/Album"
mkdir converted
for f in *.mp3; do ffmpeg -i "$f" -ar 44100 "converted/$f"; done
```

Verify by re-running ffprobe on a converted file and confirming `44100 Hz`. Once
happy, move the contents of `converted/` up and delete the originals.

**Bookmarks** are saved automatically every 30 seconds during playback and when
you stop or exit. Reopening a book resumes from your last position.

**Metadata caching** — the first time you open the audiobook player, it reads
title and author tags from each file (which can take a few seconds with many
files). This metadata is cached to the SD card so subsequent visits load
near-instantly. If you add or remove files the cache updates automatically.

### WAV Files

WAV playback works, but the player has a narrower compatibility window than MP3.
Files must be:

- **Format code 0x0001** (linear PCM). Not 0xFFFE (`WAVE_FORMAT_EXTENSIBLE`),
  which is what most DAWs export by default for 24-bit, 32-bit, or
  multichannel sessions.
- **16-bit samples.** 24-bit and 32-bit PCM either produce silence or
  distortion.
- **44.1 kHz sample rate.** Same constraint as MP3.
- **Mono or stereo.** Multichannel files fail the format check.

A non-compliant WAV refuses to start. The decoder only accepts format code
0x0001, so anything in the `EXTENSIBLE` wrapper is rejected even when the
underlying samples are plain PCM.

Inspect a WAV's actual format with ffprobe:

```bash
ffprobe -i "yourfile.wav" 2>&1 | grep "Audio:"
# Audio: pcm_s24le, 48000 Hz, stereo, s32, 2304 kb/s    ← won't play
# Audio: pcm_s16le, 44100 Hz, stereo, s16, 1411 kb/s    ← will play
```

Re-encode a single file:

```bash
ffmpeg -i "input.wav" -acodec pcm_s16le -ar 44100 "output.wav"
```

For a whole album folder:

```bash
cd "/path/to/Album"
mkdir converted
for f in *.wav; do ffmpeg -i "$f" -acodec pcm_s16le -ar 44100 "converted/$f"; done
```

Verify with ffprobe and check the output line shows `pcm_s16le, 44100 Hz`. Once
you're happy, move the contents of `converted/` up to the album folder and
delete the originals.

### Album Art

Strip embedded album art (cover images) from your audio files before copying
them to the SD card. Large embedded artwork can make a track slow to start when
you open it, and in some cases fail or reboot the device, because the player has
to read past the embedded image data before it reaches the start of the audio.
The player does not currently display embedded cover art, so removing it costs
nothing and recovers SD card space.

Most tag editors can strip embedded artwork — for example Mp3tag, Kid3, or
MusicBrainz Picard. Remove the cover / picture field from each file and save.

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
headphone jack. On the T-Deck Max, it uses the ES8311 audio codec and is
available on every Max variant.

> **Note:** On the T-Deck Pro, the audiobook player is not available on the 4G
> modem variant due to I2S pin conflicts. The T-Deck Max runs both the modem
> and audio at once, so the player is available there regardless of variant.

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

### Troubleshooting

**A track stalls for several seconds when you open it, then fails or reboots the
device.** The file has a large embedded album-art image and the player is
working through it before it reaches the audio. Strip the embedded art — see
Album Art.

**An MP3 refuses to start.** Past the obvious "the file is corrupt" check, the
usual cause is a sample rate other than 44.1 kHz. Inspect it with ffprobe and
re-encode if needed — see Recommended Format.

**A WAV refuses to start.** The file is outside the compatibility window — most
often `pcm_s24le` or `pcm_s32le` at 48 kHz (the default DAW export), or a
`WAVE_FORMAT_EXTENSIBLE` wrapper. Check it with ffprobe and convert — see WAV
Files.