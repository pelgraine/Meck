/**
 * minigb_apu is released under the terms listed within the LICENSE file.
 *
 * minigb_apu emulates the audio processing unit (APU) of the Game Boy. This
 * project is based on MiniGBS by Alex Baines: https://github.com/baines/MiniGBS
 */

#pragma once

#include <stdint.h>

/* MECK PATCH: upstream runs the APU at the Game Boy's native 32768 Hz,
 * which the ES8311 codec's clock table on the T-Display P4 does not
 * support ("search _clock_coeff_list fail" in the bus driver, codec left
 * unclocked, silent playback). 44100 Hz is the audiobook player's proven
 * rate on this codec. Every APU timing quantity derives from this macro
 * (FREQ_INC_REF = rate * 16, all integer maths), so the synthesis stays
 * exact; the per-frame sample count becomes 738. */
// Meck (T-Deck): 22050, half the P4's 44100. Halves the APU's per-frame
// cost on the ESP32-S3 (measured 3.6-4.0 ms at 44100 against a 16.7 ms
// frame); the 4-bit Game Boy sound loses nothing audible at 22 kHz.
#define AUDIO_SAMPLE_RATE	22050

#define DMG_CLOCK_FREQ		4194304.0
#define SCREEN_REFRESH_CYCLES	70224.0
#define VERTICAL_SYNC		(DMG_CLOCK_FREQ/SCREEN_REFRESH_CYCLES)

#define AUDIO_SAMPLES		((unsigned)(AUDIO_SAMPLE_RATE / VERTICAL_SYNC))

/**
 * Fill allocated buffer "data" with "len" number of 32-bit floating point
 * samples (native endian order) in stereo interleaved format.
 */
void audio_callback(void *ptr, uint8_t *data, int len);

/**
 * Read audio register at given address "addr".
 */
uint8_t audio_read(const uint16_t addr);

/**
 * Write "val" to audio register at given address "addr".
 */
void audio_write(const uint16_t addr, const uint8_t val);

/**
 * Initialise audio driver.
 */
void audio_init(void);