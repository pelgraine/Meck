#pragma once

// =============================================================================
// ES8311.h -- Minimal self-contained ES8311 codec init for T-Deck Pro MAX
//
// The MAX uses an ES8311 I2C-configured audio codec (the Pro V1.1 used a
// PCM5102A "dumb" DAC that needed no config). The ES8311 will not produce
// sound until its registers are configured over I2C. This driver does exactly
// that, over Arduino Wire, with no esp_codec_dev / Cpp_Bus_Driver framework.
//
// The register sequence and clock-coefficient table are taken from, and
// cross-verified between, Espressif's esp_codec_dev es8311.c and LilyGo's P4
// Cpp_Bus_Driver Es8311 -- both agree on every clock register, so the sequence
// here is not reverse-engineered.
//
// Configuration (fixed for Stage 1):
//   - ES8311 in SLAVE mode (ESP32 I2S is master: drives MCLK/BCLK/LRCK)
//   - use_mclk = TRUE: a real MCLK is driven on BOARD_I2S_MCLK (GPIO38) by the
//     I2S peripheral (APLL on, auto-derived 256 * fs). This mirrors the working
//     Ripple firmware, which drives the ES8311 with MCLK on this exact platform.
//   - 44100 Hz, 16-bit, DAC (playback) only; MCLK = 256 * sample_rate
//   - Register sequence transcribed from the proven esp_codec_dev es8311.c
//     (es8311_open + es8311_set_bits_per_sample + es8311_config_sample +
//      es8311_start) for the use_mclk == true path
//
// Routing/amplifier (XL9555: AUDIO_SEL low, AMPLIFIER high) is handled by the
// board class (board.selectAudioES8311(); board.amplifierEnable()), NOT here.
//
// I2C: the ES8311 is at 0x18 on the shared Wire bus (SDA13/SCL14), already
// initialised by board.begin(). This driver only uses Wire, never re-inits it.
//
// Guard: HAS_ES8311_AUDIO
// =============================================================================

#ifdef HAS_ES8311_AUDIO

#ifndef MECK_ES8311_H
#define MECK_ES8311_H

#include <Arduino.h>
#include <Wire.h>
#include "variant.h"

// ES8311 register addresses (subset used here)
#define ES8311_REG00_RESET        0x00
#define ES8311_REG01_CLK_MANAGER  0x01
#define ES8311_REG02_CLK_MANAGER  0x02
#define ES8311_REG03_CLK_MANAGER  0x03
#define ES8311_REG04_CLK_MANAGER  0x04
#define ES8311_REG05_CLK_MANAGER  0x05
#define ES8311_REG06_CLK_MANAGER  0x06
#define ES8311_REG07_CLK_MANAGER  0x07
#define ES8311_REG08_CLK_MANAGER  0x08
#define ES8311_REG09_SDPIN        0x09
#define ES8311_REG0A_SDPOUT       0x0A
#define ES8311_REG0B_SYSTEM       0x0B
#define ES8311_REG0C_SYSTEM       0x0C
#define ES8311_REG0D_SYSTEM       0x0D
#define ES8311_REG0E_SYSTEM       0x0E
#define ES8311_REG10_SYSTEM       0x10
#define ES8311_REG11_SYSTEM       0x11
#define ES8311_REG12_SYSTEM       0x12
#define ES8311_REG13_SYSTEM       0x13
#define ES8311_REG14_SYSTEM       0x14
#define ES8311_REG15_ADC          0x15
#define ES8311_REG16_ADC          0x16
#define ES8311_REG17_ADC          0x17
#define ES8311_REG1B_ADC          0x1B
#define ES8311_REG1C_ADC          0x1C
#define ES8311_REG31_DAC          0x31
#define ES8311_REG32_DAC          0x32
#define ES8311_REG37_DAC          0x37
#define ES8311_REG44_GPIO         0x44
#define ES8311_REG45_GP           0x45
#define ES8311_REGFD_CHIPID1      0xFD

#ifndef I2C_ADDR_ES8311
#define I2C_ADDR_ES8311 0x18
#endif

// --- low-level I2C register access over Wire ---

static inline bool es8311_write(uint8_t reg, uint8_t val) {
  Wire.beginTransmission((uint8_t)I2C_ADDR_ES8311);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}

static inline uint8_t es8311_read(uint8_t reg) {
  Wire.beginTransmission((uint8_t)I2C_ADDR_ES8311);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0;
  if (Wire.requestFrom((uint8_t)I2C_ADDR_ES8311, (uint8_t)1) != 1) return 0;
  return Wire.read();
}

// Initialise the ES8311 for 44100 Hz, 16-bit, slave mode, MCLK = 256 * fs.
// Returns true if the chip ID read back as 0x83 (ES8311 reports ID 0x8311
// across REGFD/REGFE; REGFD high byte is 0x83).
static inline bool es8311_init_44100_16bit() {
  // Chip presence / ID check (REGFD should read 0x83 on ES8311)
  uint8_t id = es8311_read(ES8311_REGFD_CHIPID1);
  Serial.printf("[ES8311] chip ID REGFD = 0x%02X (expect 0x83)\n", id);

  // ---- open(): base register setup (from esp_codec_dev es8311_open) ----
  // GPIO REG44 written twice: ES8311 first I2C write can be unreliable.
  es8311_write(ES8311_REG44_GPIO, 0x08);
  es8311_write(ES8311_REG44_GPIO, 0x08);

  es8311_write(ES8311_REG01_CLK_MANAGER, 0x30);
  es8311_write(ES8311_REG02_CLK_MANAGER, 0x00);
  es8311_write(ES8311_REG03_CLK_MANAGER, 0x10);
  es8311_write(ES8311_REG16_ADC,         0x24);
  es8311_write(ES8311_REG04_CLK_MANAGER, 0x10);
  es8311_write(ES8311_REG05_CLK_MANAGER, 0x00);
  es8311_write(ES8311_REG0B_SYSTEM,      0x00);
  es8311_write(ES8311_REG0C_SYSTEM,      0x00);
  es8311_write(ES8311_REG10_SYSTEM,      0x1F);
  es8311_write(ES8311_REG11_SYSTEM,      0x7F);
  es8311_write(ES8311_REG00_RESET,       0x80);  // CSM power up

  // Slave mode: REG00 bit6 = 0. (read-modify-write)
  {
    uint8_t regv = es8311_read(ES8311_REG00_RESET);
    regv &= 0xBF;  // slave
    es8311_write(ES8311_REG00_RESET, regv);
  }

  // Clock source for internal MCLK (REG01), transcribed from the proven
  // esp_codec_dev es8311.c es8311_open() for use_mclk == true:
  //   regv = 0x3F; use_mclk true -> regv &= 0x7F;  invert_mclk false -> regv &= ~0x40
  //   => REG01 = 0x3F
  // The codec uses the externally supplied MCLK (driven on GPIO38 via APLL),
  // mirroring the working Ripple firmware on this platform.
  {
    uint8_t regv = 0x3F;
    regv &= 0x7F;    // use_mclk = true
    regv &= ~0x40;   // mclk not inverted
    es8311_write(ES8311_REG01_CLK_MANAGER, regv);
  }

  // SCLK not inverted (REG06)
  {
    uint8_t regv = es8311_read(ES8311_REG06_CLK_MANAGER);
    regv &= ~0x20;
    es8311_write(ES8311_REG06_CLK_MANAGER, regv);
  }

  es8311_write(ES8311_REG13_SYSTEM, 0x10);
  es8311_write(ES8311_REG1B_ADC,    0x0A);
  es8311_write(ES8311_REG1C_ADC,    0x6A);
  // no_dac_ref == false: set internal reference signal (ADCL + DACR) -> REG44 = 0x58.
  // (es8311.c writes 0x58 here for DAC playback; the earlier 0x08 writes were the
  // I2C-noise-immunity double-write.)
  es8311_write(ES8311_REG44_GPIO,   0x58);

  // ---- config_sample(): clock dividers, transcribed from es8311_config_sample ----
  // For use_mclk == true the driver looks up the coeff row by
  // (sample_rate * mclk_div, sample_rate) with mclk_div = 256 default, i.e. the
  // {11289600, 44100} row: pre_div=1, pre_multi=1, adc_div=1, dac_div=1,
  // fs_mode=0, lrck_h=0x00, lrck_l=0xff, bclk_div=0x04, adc_osr=0x10, dac_osr=0x10.
  // pre_multi == 1 -> datmp = 0 (the use_mclk==false datmp=3 override does NOT
  // apply here). So REG02 = (pre_div-1)<<5 | 0<<3 = 0x00.
  {
    const uint8_t pre_div = 1, pre_multi_enc = 0;   // datmp=0 (pre_multi=1, use_mclk=true)
    const uint8_t adc_div = 1, dac_div = 1, fs_mode = 0;
    const uint8_t lrck_h = 0x00, lrck_l = 0xFF, bclk_div = 0x04;
    const uint8_t adc_osr = 0x10, dac_osr = 0x10;

    uint8_t regv = es8311_read(ES8311_REG02_CLK_MANAGER);
    regv &= 0x07;
    regv |= (pre_div - 1) << 5;
    regv |= pre_multi_enc << 3;
    es8311_write(ES8311_REG02_CLK_MANAGER, regv);

    regv = (fs_mode << 6) | adc_osr;
    es8311_write(ES8311_REG03_CLK_MANAGER, regv);

    es8311_write(ES8311_REG04_CLK_MANAGER, dac_osr);

    regv = ((adc_div - 1) << 4) | (dac_div - 1);
    es8311_write(ES8311_REG05_CLK_MANAGER, regv);

    regv = es8311_read(ES8311_REG06_CLK_MANAGER);
    regv &= 0xE0;
    if (bclk_div < 19) regv |= (bclk_div - 1);
    else               regv |= bclk_div;
    es8311_write(ES8311_REG06_CLK_MANAGER, regv);

    regv = es8311_read(ES8311_REG07_CLK_MANAGER);
    regv &= 0xC0;
    regv |= lrck_h;
    es8311_write(ES8311_REG07_CLK_MANAGER, regv);

    es8311_write(ES8311_REG08_CLK_MANAGER, lrck_l);
  }

  // ---- format + bits per sample: 16-bit, I2S (REG09 DAC SDP) ----
  // 16-bit -> SDP word length field = 0x0C in bits [4:2]; I2S format = 0x00.
  {
    uint8_t dac_iface = es8311_read(ES8311_REG09_SDPIN);
    dac_iface &= 0xE3;        // clear word-length field bits [4:2] only
    dac_iface |= (0x03 << 2); // 0x0C = 16-bit (bits [4:2] = 011)
    es8311_write(ES8311_REG09_SDPIN, dac_iface);
  }

  // ---- start(): power up DAC path (from es8311_start, DAC mode) ----
  {
    uint8_t regv = 0x80;       // CSM on
    regv &= 0xBF;              // slave
    es8311_write(ES8311_REG00_RESET, regv);

    // REG01 clock source (from es8311_start): regv=0x3F; use_mclk true -> &=0x7F;
    // invert_mclk false -> &=~0x40  => 0x3F. Re-applied here exactly as the proven
    // start sequence does, so it stays consistent with the open() write.
    {
      uint8_t r01 = 0x3F;
      r01 &= 0x7F;     // use_mclk = true
      r01 &= ~0x40;    // not inverted
      es8311_write(ES8311_REG01_CLK_MANAGER, r01);
    }

    // DAC interface power on (REG09 bit6 = 0)
    uint8_t dac_iface = es8311_read(ES8311_REG09_SDPIN);
    dac_iface &= ~(1 << 6);
    es8311_write(ES8311_REG09_SDPIN, dac_iface);

    es8311_write(ES8311_REG17_ADC,    0xBF);
    es8311_write(ES8311_REG0E_SYSTEM,  0x02);
    es8311_write(ES8311_REG12_SYSTEM,  0x00);  // enable DAC
    es8311_write(ES8311_REG14_SYSTEM,  0x1A);
    es8311_write(ES8311_REG0D_SYSTEM,  0x01);
    es8311_write(ES8311_REG15_ADC,     0x40);
    es8311_write(ES8311_REG37_DAC,     0x08);
    es8311_write(ES8311_REG45_GP,      0x00);
  }

  // Unmute DAC and set a sane default volume.
  es8311_write(ES8311_REG31_DAC, 0x00);   // unmute
  es8311_write(ES8311_REG32_DAC, 0xBF);   // ~0 dB (191 = 0 dB)

  // DIAG: read back key registers to confirm the power-up/format writes landed.
  // REG00 (CSM/reset), REG01 (clock src), REG02 (clk div), REG09 (SDP/DAC iface),
  // REG0D (power up/down), REG12 (DAC enable), REG31 (DAC mute), REG32 (DAC vol).
  Serial.printf("[ES8311] readback R00=%02X R01=%02X R02=%02X R09=%02X R0D=%02X R12=%02X R31=%02X R32=%02X\n",
                es8311_read(ES8311_REG00_RESET),
                es8311_read(ES8311_REG01_CLK_MANAGER),
                es8311_read(ES8311_REG02_CLK_MANAGER),
                es8311_read(ES8311_REG09_SDPIN),
                es8311_read(ES8311_REG0D_SYSTEM),
                es8311_read(ES8311_REG12_SYSTEM),
                es8311_read(ES8311_REG31_DAC),
                es8311_read(ES8311_REG32_DAC));

  return id == 0x83;
}

// =============================================================================
// es8311_init_capture_16k() -- ANALOGUE-MIC CAPTURE (ADC) init, 16 kHz, 16-bit
//
// Recording counterpart to es8311_init_44100_16bit(). Brings up the ES8311 ADC
// path (mic -> ADC -> I2S SDOUT/ASDOUT) so the ESP32 I2S RX can read PCM. It
// does NOT power the DAC; call es8311_init_44100_16bit() afterwards to restore
// the playback path.
//
// Same authority as the DAC init: transcribed from esp_codec_dev es8311.c
//   (es8311_open + es8311_set_bits_per_sample(16) + es8311_config_fmt(I2S) +
//    es8311_config_sample(16000) + es8311_start) for the configuration:
//     master_mode = false (ESP32 I2S is master, drives MCLK/BCLK/LRCK)
//     use_mclk    = true  (real MCLK on BOARD_I2S_MCLK / GPIO38, 256 * fs)
//     invert_mclk = false, invert_sclk = false
//     codec_mode  = ADC,  digital_mic = false (analogue mic), no_dac_ref = false
//     mclk_div    = 256   -> MCLK = 16000 * 256 = 4.096 MHz
//   using the {4096000, 16000} coefficient row:
//     pre_div=1 pre_multi=1 adc_div=1 dac_div=1 fs_mode=0
//     lrck_h=0x00 lrck_l=0xFF bclk_div=0x04 adc_osr=0x10 dac_osr=0x20
//
// mic_gain -> REG16 ADC PGA gain (ES8311 mic-gain enum, low 3 bits):
//   0=0dB 1=6dB 2=12dB 3=18dB 4=24dB 5=30dB 6=36dB 7=42dB
// 30 dB (5) is a reasonable starting point for the MAX analogue mic; this is
// the first knob to tune on the bench if recordings are too quiet or clip.
//
// Returns true if the chip ID read back as 0x83.
// =============================================================================
static inline bool es8311_init_capture_16k(uint8_t mic_gain = 0x05) {
  uint8_t id = es8311_read(ES8311_REGFD_CHIPID1);
  Serial.printf("[ES8311] (capture) chip ID REGFD = 0x%02X (expect 0x83)\n", id);

  // ---- open(): base register setup (from es8311_open) ----
  es8311_write(ES8311_REG44_GPIO, 0x08);   // double write: first I2C write can be unreliable
  es8311_write(ES8311_REG44_GPIO, 0x08);

  es8311_write(ES8311_REG01_CLK_MANAGER, 0x30);
  es8311_write(ES8311_REG02_CLK_MANAGER, 0x00);
  es8311_write(ES8311_REG03_CLK_MANAGER, 0x10);
  es8311_write(ES8311_REG16_ADC,         0x24);
  es8311_write(ES8311_REG04_CLK_MANAGER, 0x10);
  es8311_write(ES8311_REG05_CLK_MANAGER, 0x00);
  es8311_write(ES8311_REG0B_SYSTEM,      0x00);
  es8311_write(ES8311_REG0C_SYSTEM,      0x00);
  es8311_write(ES8311_REG10_SYSTEM,      0x1F);
  es8311_write(ES8311_REG11_SYSTEM,      0x7F);
  es8311_write(ES8311_REG00_RESET,       0x80);  // CSM power up

  // Slave mode: REG00 bit6 = 0 (read-modify-write)
  {
    uint8_t regv = es8311_read(ES8311_REG00_RESET);
    regv &= 0xBF;
    es8311_write(ES8311_REG00_RESET, regv);
  }

  // REG01 clock source: use_mclk=true (&=0x7F), not inverted (&=~0x40) => 0x3F
  {
    uint8_t regv = 0x3F;
    regv &= 0x7F;
    regv &= ~0x40;
    es8311_write(ES8311_REG01_CLK_MANAGER, regv);
  }

  // SCLK not inverted (REG06)
  {
    uint8_t regv = es8311_read(ES8311_REG06_CLK_MANAGER);
    regv &= ~0x20;
    es8311_write(ES8311_REG06_CLK_MANAGER, regv);
  }

  es8311_write(ES8311_REG13_SYSTEM, 0x10);
  es8311_write(ES8311_REG1B_ADC,    0x0A);
  es8311_write(ES8311_REG1C_ADC,    0x6A);
  // no_dac_ref == false: internal reference signal (ADCL + DACR) -> REG44 = 0x58
  es8311_write(ES8311_REG44_GPIO,   0x58);

  // ---- set_bits_per_sample(16): 16-bit on both SDP regs (REG09 DAC, REG0A ADC) ----
  {
    uint8_t dac_iface = es8311_read(ES8311_REG09_SDPIN);
    uint8_t adc_iface = es8311_read(ES8311_REG0A_SDPOUT);
    dac_iface |= 0x0C;
    adc_iface |= 0x0C;
    es8311_write(ES8311_REG09_SDPIN,  dac_iface);
    es8311_write(ES8311_REG0A_SDPOUT, adc_iface);
  }

  // ---- config_fmt(ES_I2S_NORMAL): clear format bits [1:0] on REG09/REG0A ----
  {
    uint8_t dac_iface = es8311_read(ES8311_REG09_SDPIN);
    uint8_t adc_iface = es8311_read(ES8311_REG0A_SDPOUT);
    dac_iface &= 0xFC;
    adc_iface &= 0xFC;
    es8311_write(ES8311_REG09_SDPIN,  dac_iface);
    es8311_write(ES8311_REG0A_SDPOUT, adc_iface);
  }

  // ---- config_sample(16000): {4096000,16000} coeff row, write order from es8311.c ----
  {
    const uint8_t pre_div = 1, datmp = 0;   // pre_multi=1, use_mclk=true -> datmp=0
    const uint8_t adc_div = 1, dac_div = 1, fs_mode = 0;
    const uint8_t lrck_h = 0x00, lrck_l = 0xFF, bclk_div = 0x04;
    const uint8_t adc_osr = 0x10, dac_osr = 0x20;

    uint8_t regv = es8311_read(ES8311_REG02_CLK_MANAGER);
    regv &= 0x07;
    regv |= (pre_div - 1) << 5;
    regv |= datmp << 3;
    es8311_write(ES8311_REG02_CLK_MANAGER, regv);

    regv = ((adc_div - 1) << 4) | (dac_div - 1);
    es8311_write(ES8311_REG05_CLK_MANAGER, regv);

    regv = es8311_read(ES8311_REG03_CLK_MANAGER);
    regv &= 0x80;
    regv |= (fs_mode << 6) | adc_osr;
    es8311_write(ES8311_REG03_CLK_MANAGER, regv);

    regv = es8311_read(ES8311_REG04_CLK_MANAGER);
    regv &= 0x80;
    regv |= dac_osr;
    es8311_write(ES8311_REG04_CLK_MANAGER, regv);

    regv = es8311_read(ES8311_REG07_CLK_MANAGER);
    regv &= 0xC0;
    regv |= lrck_h;
    es8311_write(ES8311_REG07_CLK_MANAGER, regv);

    es8311_write(ES8311_REG08_CLK_MANAGER, lrck_l);

    regv = es8311_read(ES8311_REG06_CLK_MANAGER);
    regv &= 0xE0;
    if (bclk_div < 19) regv |= (bclk_div - 1);
    else               regv |= bclk_div;
    es8311_write(ES8311_REG06_CLK_MANAGER, regv);
  }

  // ---- start(): ADC-mode power-up (from es8311_start, codec_mode == ADC) ----
  {
    uint8_t regv = 0x80;
    regv &= 0xBF;             // slave
    es8311_write(ES8311_REG00_RESET, regv);

    uint8_t r01 = 0x3F;
    r01 &= 0x7F;              // use_mclk = true
    r01 &= ~0x40;             // not inverted
    es8311_write(ES8311_REG01_CLK_MANAGER, r01);

    uint8_t dac_iface = es8311_read(ES8311_REG09_SDPIN);
    uint8_t adc_iface = es8311_read(ES8311_REG0A_SDPOUT);
    dac_iface &= 0xBF;
    adc_iface &= 0xBF;
    adc_iface &= ~(1 << 6);   // ADC mode: power up ADC serial-port output
    es8311_write(ES8311_REG09_SDPIN,  dac_iface);
    es8311_write(ES8311_REG0A_SDPOUT, adc_iface);

    es8311_write(ES8311_REG17_ADC,    0xBF);  // ADC full-scale / volume
    es8311_write(ES8311_REG0E_SYSTEM,  0x02);
    // REG12 (DAC enable) intentionally NOT written -- this is the ADC-only path.
    es8311_write(ES8311_REG14_SYSTEM,  0x1A);

    // digital_mic == false: clear DMIC-enable bit (0x40) on REG14 (analogue mic)
    {
      uint8_t r14 = es8311_read(ES8311_REG14_SYSTEM);
      r14 &= ~0x40;
      es8311_write(ES8311_REG14_SYSTEM, r14);
    }

    es8311_write(ES8311_REG0D_SYSTEM,  0x01);
    es8311_write(ES8311_REG15_ADC,     0x40);
    es8311_write(ES8311_REG37_DAC,     0x08);
    es8311_write(ES8311_REG45_GP,      0x00);
  }

  // ADC PGA / mic gain (REG16). Overwrites the open() default (0x24), mirroring
  // esp_codec_dev es8311_set_mic_gain which writes the raw enum (0..7) to REG16.
  es8311_write(ES8311_REG16_ADC, (uint8_t)(mic_gain & 0x07));

  // DIAG: read back the key ADC/clock/format registers to confirm the writes.
  Serial.printf("[ES8311] (capture) readback R00=%02X R01=%02X R02=%02X R09=%02X R0A=%02X R14=%02X R15=%02X R16=%02X R17=%02X\n",
                es8311_read(ES8311_REG00_RESET),
                es8311_read(ES8311_REG01_CLK_MANAGER),
                es8311_read(ES8311_REG02_CLK_MANAGER),
                es8311_read(ES8311_REG09_SDPIN),
                es8311_read(ES8311_REG0A_SDPOUT),
                es8311_read(ES8311_REG14_SYSTEM),
                es8311_read(ES8311_REG15_ADC),
                es8311_read(ES8311_REG16_ADC),
                es8311_read(ES8311_REG17_ADC));

  return id == 0x83;
}

// DAC volume, 0..255 (0 = mute-ish, 191 = 0 dB, 255 = +32 dB).
static inline void es8311_set_dac_volume(uint8_t vol) {
  es8311_write(ES8311_REG32_DAC, vol);
}

// Hard mute / unmute the DAC.
static inline void es8311_set_mute(bool mute) {
  uint8_t regv = es8311_read(ES8311_REG31_DAC);
  regv &= 0x9F;
  es8311_write(ES8311_REG31_DAC, mute ? (regv | 0x60) : regv);
}

#endif // MECK_ES8311_H
#endif // HAS_ES8311_AUDIO