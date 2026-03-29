#pragma once

// =============================================================================
// VoiceMessageScreen.h — Voice message recorder for LilyGo T-Deck Pro
//
// PROTOTYPE: Proves the PDM mic → PSRAM → WAV/SD → DAC playback pipeline.
// Codec2 encoding and LoRa transmission will be added in a later phase.
//
// Features:
//   - PDM microphone capture via I2S_NUM_0 (time-shared with DAC)
//   - 16kHz / 16-bit mono recording to PSRAM ring buffer
//   - Save as WAV files on SD card (/voice/ directory)
//   - Playback through PCM5102A DAC via shared Audio* object
//   - Hold-to-talk: hold mic key to record, release to stop
//   - 5-second max recording with progress bar
//   - Review before send: play / re-record / delete
//
// Keyboard controls:
//   MESSAGE_LIST:  W/S = scroll, Enter = play selected, D = delete, Q = exit
//   RECORDING:     Mic release or 5s timeout stops recording
//   REVIEW:        Enter = play, Mic = re-record, D = delete, Q = back to list
//
// Guard: MECK_AUDIO_VARIANT (audio variant only — needs I2S DAC + PDM mic)
// =============================================================================

#ifdef MECK_AUDIO_VARIANT

#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <SD.h>
#include <driver/i2s.h>
#include "Audio.h"
#include "variant.h"

// Codec2 low-bitrate voice codec
#include <codec2.h>

// Forward declarations
class UITask;
class MyMesh;

// ---------------------------------------------------------------------------
// dz0ny VE3 voice protocol constants
// ---------------------------------------------------------------------------
#define VOICE_PKT_MAGIC     0x56  // 'V' — voice data packet
#define VOICE_FETCH_MAGIC   0x72  // 'r' — voice fetch request
#define VOICE_PKT_HDR_SIZE  6     // magic(1) + sessionID(4) + index(1)
#define VOICE_SESSION_TTL_MS 900000  // 15 minutes cache TTL
#define VOICE_C2_MODE_ID    1     // Codec2 1200bps mode ID for VE3 protocol

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------
#define VOICE_FOLDER        "/voice"
#define VOICE_MAX_SECONDS   12
#define VOICE_SAMPLE_RATE   16000
#define VOICE_BITS          16
#define VOICE_CHANNELS      1

// Codec2 encoding config
#define VOICE_C2_MODE       CODEC2_MODE_1200  // 1200bps — AM radio quality
#define VOICE_C2_RATE       8000              // Codec2 native sample rate
#define VOICE_C2_FRAME_MS   40                // Frame duration at 1200bps
#define VOICE_C2_FRAME_SAM  320               // Samples per frame (8kHz × 40ms)
#define VOICE_C2_FRAME_BYTES 6                // Encoded bytes per frame (48 bits)
// Max encoded size: 5 seconds = 125 frames × 6 bytes = 750 bytes
#define VOICE_C2_MAX_BYTES  ((VOICE_MAX_SECONDS * 1000 / VOICE_C2_FRAME_MS) * VOICE_C2_FRAME_BYTES)
// Usable codec2 data per raw voice packet.
// Keep under ~150 to avoid hitting MAX_PACKET_PAYLOAD (184) boundary issues
// with radio/SPI. Total packet = VOICE_PKT_HDR_SIZE(6) + data.
#define VOICE_MESH_PAYLOAD  150

// Buffer: 16kHz × 16-bit × 5s = 160,000 bytes — fits easily in PSRAM
#define VOICE_BUF_SAMPLES   (VOICE_SAMPLE_RATE * VOICE_MAX_SECONDS)
#define VOICE_BUF_BYTES     (VOICE_BUF_SAMPLES * sizeof(int16_t))

// I2S port for PDM mic — ESP32-S3 only supports PDM RX on I2S_NUM_0.
// This conflicts with ESP32-audioI2S (DAC output), so we time-share:
// stop audio before recording, uninstall driver after, let audio lib reclaim.
#define VOICE_I2S_PORT      I2S_NUM_0

// DMA buffer config for mic capture
// E-ink refreshes block the CPU for ~650ms. At 16kHz, that's 10,400 samples.
// We need enough DMA buffer to hold audio during those blocks.
// 16 × 1024 = 16,384 samples ≈ 1 second — survives one full refresh cycle.
#define VOICE_DMA_BUF_COUNT 16
#define VOICE_DMA_BUF_LEN   1024

// Max files shown in the list
#define VOICE_MAX_FILES     50

// ---------------------------------------------------------------------------
// WAV header writer (44-byte RIFF/WAVE PCM header)
// ---------------------------------------------------------------------------
static void writeWavHeader(File& f, uint32_t dataBytes, uint32_t sampleRate,
                           uint16_t bitsPerSample, uint16_t channels) {
  uint32_t byteRate = sampleRate * channels * (bitsPerSample / 8);
  uint16_t blockAlign = channels * (bitsPerSample / 8);
  uint32_t chunkSize = 36 + dataBytes;

  f.write((const uint8_t*)"RIFF", 4);
  f.write((const uint8_t*)&chunkSize, 4);
  f.write((const uint8_t*)"WAVE", 4);
  f.write((const uint8_t*)"fmt ", 4);
  uint32_t fmtSize = 16;
  f.write((const uint8_t*)&fmtSize, 4);
  uint16_t audioFmt = 1;  // PCM
  f.write((const uint8_t*)&audioFmt, 2);
  f.write((const uint8_t*)&channels, 2);
  f.write((const uint8_t*)&sampleRate, 4);
  f.write((const uint8_t*)&byteRate, 4);
  f.write((const uint8_t*)&blockAlign, 2);
  f.write((const uint8_t*)&bitsPerSample, 2);
  f.write((const uint8_t*)"data", 4);
  f.write((const uint8_t*)&dataBytes, 4);
}

// ---------------------------------------------------------------------------
// VoiceFileEntry — one file in the /voice/ directory
// ---------------------------------------------------------------------------
struct VoiceFileEntry {
  char name[64];       // Filename only (no path)
  uint32_t sizeBytes;  // File size
  float    durationSec;
};

// ---------------------------------------------------------------------------
// VoiceMessageScreen
// ---------------------------------------------------------------------------
class VoiceMessageScreen : public UIScreen {
public:
  enum Mode { MESSAGE_LIST, RECORDING, REVIEW, CONTACT_PICK };

  // Contact picker entry — populated by main.cpp from the_mesh contacts
  struct PickContact {
    int     meshIdx;    // Index in the_mesh contacts array
    char    name[32];
    uint8_t type;       // ADV_TYPE_*
    bool    hasDirect;  // Has direct path (not OUT_PATH_UNKNOWN)
  };

private:
  UITask*        _task;
  Audio*         _audio;         // Shared Audio* for playback (set from main.cpp)
  Mode           _mode;
  bool           _sdReady;
  bool           _i2sInitialized;   // DAC I2S init (via Audio*)
  bool           _micInitialized;   // PDM mic I2S init
  bool           _dacPowered;
  DisplayDriver* _displayRef;

  // File browser
  std::vector<VoiceFileEntry> _fileList;
  int  _selectedFile;
  int  _scrollOffset;

  // Recording state
  int16_t* _recBuffer;           // PSRAM-allocated capture buffer
  uint32_t _recSamples;          // Samples captured so far
  bool     _recording;           // Currently capturing
  unsigned long _recStartMillis; // When recording started

  // Review state — just-recorded file
  char _reviewFilename[64];      // Filename of the just-recorded WAV
  bool _reviewPlaying;           // Currently playing back the review file
  bool _reviewDirty;             // Screen needs redraw after playback state change

  // Playback from list
  bool _listPlaying;
  int  _listPlayIdx;

  // Playback finished detection (for UI refresh)
  bool _playbackJustFinished;

  // Codec2 encoded data (from last recording)
  uint8_t  _c2Data[VOICE_C2_MAX_BYTES];  // Encoded Codec2 frames
  uint32_t _c2Bytes;                      // Total encoded bytes
  uint32_t _c2Frames;                     // Number of encoded frames
  bool     _c2Valid;                      // Encoding succeeded

  // --- VE3 voice session cache (outgoing) ---
  // Cached after send so we can serve fetch requests from the receiver.
  struct VoiceSession {
    uint32_t sessionId;
    uint8_t  data[VOICE_C2_MAX_BYTES];
    uint32_t dataBytes;
    uint8_t  totalPackets;
    uint8_t  durationSec;
    unsigned long cachedAt;   // millis() when cached
    bool     active;
  };
  VoiceSession _outSession;  // Single outgoing session (most recent send)

  // --- Incoming voice session (received from another device) ---
  struct IncomingSession {
    uint32_t sessionId;
    uint8_t  data[VOICE_C2_MAX_BYTES];   // Accumulated Codec2 data
    uint16_t pktOffset[16];              // Byte offset for each packet's data
    uint16_t pktSize[16];                // Byte count for each packet's codec2 chunk
    uint8_t  totalPackets;               // Expected total (from VE3 envelope)
    uint16_t receivedBitmap;             // Bitmask of received packet indices
    uint8_t  receivedCount;              // Number of distinct packets received
    uint32_t dataBytes;                  // Total accumulated bytes
    uint8_t  durationSec;
    char     senderName[32];
    unsigned long startedAt;
    bool     active;
    bool     complete;                   // All packets received
    bool     playTriggered;             // Auto-play already fired
  };
  IncomingSession _inSession;

  // --- Contact picker state ---
  std::vector<PickContact> _pickList;
  int _pickSelected;
  int _pickScroll;
  int _pendingSendIdx;  // Contact idx for pending send (-1 = none)

  // DAC power control (same as AudiobookPlayerScreen)
  void enableDAC() {
    pinMode(41, OUTPUT);
    digitalWrite(41, HIGH);
    if (!_dacPowered) delay(50);
    _dacPowered = true;
  }

  void disableDAC() {
    digitalWrite(41, LOW);
    _dacPowered = false;
  }

  // ---------------------------------------------------------------------------
  // PDM Microphone — I2S_NUM_0 in PDM RX mode
  // ESP32-S3 only supports PDM on I2S_NUM_0, which ESP32-audioI2S also uses.
  // We must stop audio playback and tear down the existing driver first.
  // ---------------------------------------------------------------------------
  bool initMic() {
    if (_micInitialized) return true;

    // Stop any active audio playback — we're about to take over I2S_NUM_0
    if (_audio) {
      if (_audio->isRunning()) {
        _audio->stopSong();
        Serial.println("Voice: Stopped audio playback to free I2S_NUM_0");
      }
    }

    // Tear down any existing I2S driver on port 0 (ESP32-audioI2S leaves it installed).
    // Ignore errors — it might not be installed yet.
    i2s_driver_uninstall(VOICE_I2S_PORT);
    delay(10);  // Let hardware settle

    // After we release I2S_NUM_0 back, ESP32-audioI2S will need to reconfigure
    _i2sInitialized = false;

    i2s_config_t mic_cfg = {};
    mic_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX | I2S_MODE_PDM);
    mic_cfg.sample_rate = VOICE_SAMPLE_RATE;
    mic_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    mic_cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
    mic_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    mic_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    mic_cfg.dma_buf_count = VOICE_DMA_BUF_COUNT;
    mic_cfg.dma_buf_len = VOICE_DMA_BUF_LEN;
    mic_cfg.use_apll = false;
    mic_cfg.tx_desc_auto_clear = false;
    mic_cfg.fixed_mclk = 0;

    esp_err_t err = i2s_driver_install(VOICE_I2S_PORT, &mic_cfg, 0, NULL);
    if (err != ESP_OK) {
      Serial.printf("Voice: i2s_driver_install failed: %d\n", err);
      return false;
    }

    i2s_pin_config_t mic_pins = {};
    mic_pins.bck_io_num = I2S_PIN_NO_CHANGE;
    mic_pins.ws_io_num = BOARD_MIC_CLOCK;   // GPIO 18 — PDM CLK
    mic_pins.data_out_num = I2S_PIN_NO_CHANGE;
    mic_pins.data_in_num = BOARD_MIC_DATA;  // GPIO 17 — PDM DATA

    err = i2s_set_pin(VOICE_I2S_PORT, &mic_pins);
    if (err != ESP_OK) {
      Serial.printf("Voice: i2s_set_pin failed: %d\n", err);
      i2s_driver_uninstall(VOICE_I2S_PORT);
      return false;
    }

    _micInitialized = true;
    Serial.println("Voice: PDM mic initialised on I2S_NUM_0");
    return true;
  }

  void deinitMic() {
    if (!_micInitialized) return;
    i2s_driver_uninstall(VOICE_I2S_PORT);
    _micInitialized = false;
    // _i2sInitialized already cleared in initMic() — ESP32-audioI2S
    // will reconfigure I2S_NUM_0 on next connecttoFS() call.
    Serial.println("Voice: PDM mic deinitialised, I2S_NUM_0 released");
  }

  // Allocate PSRAM capture buffer (once, reused across recordings)
  bool ensureRecBuffer() {
    if (_recBuffer) return true;
    _recBuffer = (int16_t*)ps_calloc(VOICE_BUF_SAMPLES, sizeof(int16_t));
    if (!_recBuffer) {
      Serial.println("Voice: PSRAM alloc failed for rec buffer");
      return false;
    }
    Serial.printf("Voice: Allocated %d bytes PSRAM for recording buffer\n", VOICE_BUF_BYTES);
    return true;
  }

  // ---------------------------------------------------------------------------
  // Recording
  // ---------------------------------------------------------------------------
  bool startRecording() {
    if (!ensureRecBuffer()) return false;
    if (!initMic()) return false;

    // Flush any stale DMA data
    uint8_t flush[512];
    size_t bytesRead;
    for (int i = 0; i < 10; i++) {
      i2s_read(VOICE_I2S_PORT, flush, sizeof(flush), &bytesRead, 0);
    }

    _recSamples = 0;
    _recording = true;
    _recStartMillis = millis();
    Serial.println("Voice: Recording started");
    return true;
  }

  // Called from voiceTick() — drains all accumulated DMA data into PSRAM buffer.
  // After e-ink refreshes (~650ms blocking), the DMA may hold thousands of
  // samples. We loop until the read times out to drain everything.
  void captureChunk() {
    if (!_recording) return;

    for (;;) {
      uint32_t remaining = VOICE_BUF_SAMPLES - _recSamples;
      if (remaining == 0) break;

      size_t bytesRead = 0;
      uint32_t toRead = remaining < 2048 ? remaining : 2048;
      esp_err_t err = i2s_read(VOICE_I2S_PORT, 
                                &_recBuffer[_recSamples],
                                toRead * sizeof(int16_t),
                                &bytesRead, 
                                5);  // 5ms timeout — short so we yield quickly when empty
      if (err != ESP_OK || bytesRead == 0) break;  // DMA empty, done for now
      _recSamples += bytesRead / sizeof(int16_t);
    }

    // Auto-stop at max duration — save immediately and enter review
    if (_recSamples >= VOICE_BUF_SAMPLES) {
      stopRecording();
      if (saveRecordingToSD()) {
        _mode = REVIEW;
      } else {
        _mode = MESSAGE_LIST;
      }
    }
  }

  void stopRecording() {
    if (!_recording) return;
    _recording = false;
    
    unsigned long elapsed = millis() - _recStartMillis;
    float secs = _recSamples / (float)VOICE_SAMPLE_RATE;
    Serial.printf("Voice: Recording stopped — %d samples (%.1fs, %lums elapsed)\n",
                  _recSamples, secs, elapsed);

    // Deinit mic to free I2S port while not recording
    deinitMic();
  }

  // ---------------------------------------------------------------------------
  // Save to SD as WAV
  // ---------------------------------------------------------------------------

  // Normalize recorded audio to near-maximum amplitude.
  // PDM mics often capture at low levels; this brings the signal up
  // so playback is audible through the line-level PCM5102A DAC.
  void normalizeRecording() {
    if (_recSamples == 0) return;

    // Find peak absolute value
    int16_t peak = 0;
    for (uint32_t i = 0; i < _recSamples; i++) {
      int16_t s = _recBuffer[i];
      int16_t absS = (s < 0) ? -s : s;
      if (absS > peak) peak = absS;
    }

    if (peak < 100) {
      Serial.println("Voice: Recording is near-silent, skipping normalization");
      return;
    }

    // Target peak at 90% of max to avoid clipping artefacts
    // Use fixed-point: gain = (29491 << 16) / peak, apply as (sample * gain) >> 16
    int32_t target = 29491;  // 0.9 * 32767
    int32_t gain16 = (target << 16) / peak;  // Fixed-point 16.16

    Serial.printf("Voice: Normalizing — peak=%d, gain=%.1fx\n", 
                  peak, gain16 / 65536.0f);

    for (uint32_t i = 0; i < _recSamples; i++) {
      int32_t amplified = ((int32_t)_recBuffer[i] * gain16) >> 16;
      // Clamp to int16_t range (shouldn't be needed with 90% target, but safe)
      if (amplified > 32767) amplified = 32767;
      if (amplified < -32768) amplified = -32768;
      _recBuffer[i] = (int16_t)amplified;
    }
  }

  // ---------------------------------------------------------------------------
  // Codec2 encoding — downsample 16kHz→8kHz, encode at 1200bps
  // Processes one frame at a time to avoid needing a large scratch buffer.
  // ---------------------------------------------------------------------------
  void encodeCodec2() {
    _c2Bytes = 0;
    _c2Frames = 0;
    _c2Valid = false;

    if (_recSamples < VOICE_C2_FRAME_SAM * 2) {
      Serial.println("Voice: Too few samples for Codec2 encoding");
      return;
    }

    // Create Codec2 encoder
    struct CODEC2* c2 = codec2_create(VOICE_C2_MODE);
    if (!c2) {
      Serial.println("Voice: codec2_create failed");
      return;
    }

    int frameSamples = codec2_samples_per_frame(c2);  // 320 at 8kHz
    int frameBytes = (codec2_bits_per_frame(c2) + 7) / 8;  // 6 at 1200bps
    // Each 8kHz frame needs 2× as many 16kHz source samples
    int srcSamplesPerFrame = frameSamples * 2;  // 640 at 16kHz

    // Pad to complete frame boundary so the last few hundred ms aren't lost.
    // PSRAM buffer was allocated with ps_calloc (zero-filled), so any samples
    // beyond _recSamples are already silence.
    uint32_t remainder = _recSamples % srcSamplesPerFrame;
    if (remainder > 0 && _recSamples + (srcSamplesPerFrame - remainder) <= VOICE_BUF_SAMPLES) {
      _recSamples += (srcSamplesPerFrame - remainder);
    }

    Serial.printf("Voice: Codec2 1200bps — %d samples/frame (8kHz), %d bytes/frame\n",
                  frameSamples, frameBytes);

    // Downsample + encode one frame at a time
    int16_t frameBuf[VOICE_C2_FRAME_SAM];  // 320 × 2 = 640 bytes on stack — fine
    uint32_t srcPos = 0;

    while (srcPos + srcSamplesPerFrame <= _recSamples &&
           _c2Bytes + frameBytes <= VOICE_C2_MAX_BYTES) {
      // Downsample this frame: average pairs of 16kHz samples → 8kHz
      for (int i = 0; i < frameSamples; i++) {
        int32_t sum = (int32_t)_recBuffer[srcPos + i * 2]
                    + (int32_t)_recBuffer[srcPos + i * 2 + 1];
        frameBuf[i] = (int16_t)(sum / 2);
      }

      // Encode this frame
      codec2_encode(c2, &_c2Data[_c2Bytes], frameBuf);
      _c2Bytes += frameBytes;
      _c2Frames++;
      srcPos += srcSamplesPerFrame;
    }

    codec2_destroy(c2);
    _c2Valid = (_c2Frames > 0);

    int packets = (_c2Bytes + VOICE_MESH_PAYLOAD - 1) / VOICE_MESH_PAYLOAD;
    Serial.printf("Voice: Codec2 encoded — %d frames, %d bytes (%.1fs, %d mesh packets)\n",
                  _c2Frames, _c2Bytes,
                  _c2Frames * VOICE_C2_FRAME_MS / 1000.0f,
                  packets);
  }

  bool saveRecordingToSD() {
    if (_recSamples < VOICE_SAMPLE_RATE / 4) {
      // Less than 250ms — too short, discard
      Serial.println("Voice: Recording too short, discarding");
      return false;
    }

    // Ensure /voice/ directory exists
    if (!SD.exists(VOICE_FOLDER)) {
      SD.mkdir(VOICE_FOLDER);
    }

    // Generate filename: voice_YYYYMMDD_HHMMSS.wav
    // (we don't have strftime, so use millis-based counter as fallback)
    uint32_t ts = millis() / 1000;
    snprintf(_reviewFilename, sizeof(_reviewFilename),
             "voice_%06lu.wav", ts % 1000000UL);

    char fullPath[96];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", VOICE_FOLDER, _reviewFilename);

    File f = SD.open(fullPath, FILE_WRITE);
    if (!f) {
      Serial.printf("Voice: Failed to create %s\n", fullPath);
      return false;
    }

    // Normalize audio levels before saving
    normalizeRecording();

    // Save WAV with actual recorded samples (before Codec2 frame padding)
    uint32_t dataBytes = _recSamples * sizeof(int16_t);
    writeWavHeader(f, dataBytes, VOICE_SAMPLE_RATE, VOICE_BITS, VOICE_CHANNELS);
    f.write((const uint8_t*)_recBuffer, dataBytes);
    f.close();

    Serial.printf("Voice: Saved %s (%d bytes, %.1fs)\n",
                  fullPath, 44 + dataBytes, _recSamples / (float)VOICE_SAMPLE_RATE);

    // Encode to Codec2 (pads _recSamples to frame boundary — after WAV save)
    encodeCodec2();

    return true;
  }

  // ---------------------------------------------------------------------------
  // Playback via shared Audio* (ESP32-audioI2S)
  // After recording, deinitMic() uninstalled the I2S driver on port 0.
  // The Audio object still holds internal state expecting it to be there.
  // We must reinstall the I2S driver in TX mode BEFORE calling any Audio
  // methods (including stopSong, setVolume, connecttoFS) because they all
  // eventually call i2s_zero_dma_buffer() which crashes on a missing driver.
  // ---------------------------------------------------------------------------
  void reinstallI2SForPlayback() {
    // Ensure I2S_NUM_0 has a valid TX driver so Audio destructor won't crash
    i2s_driver_uninstall(VOICE_I2S_PORT);  // May fail — that's OK
    delay(5);

    i2s_config_t tx_cfg = {};
    tx_cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    tx_cfg.sample_rate = 44100;
    tx_cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    tx_cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    tx_cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    tx_cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    tx_cfg.dma_buf_count = 8;
    tx_cfg.dma_buf_len = 1024;
    tx_cfg.use_apll = false;
    tx_cfg.tx_desc_auto_clear = true;

    esp_err_t err = i2s_driver_install(VOICE_I2S_PORT, &tx_cfg, 0, NULL);
    if (err != ESP_OK) {
      Serial.printf("Voice: reinstall I2S TX failed: %d\n", err);
    }
    Serial.println("Voice: Reinstalled I2S_NUM_0 in TX mode");
  }

  bool playFile(const char* filename) {
    if (!_audio) {
      Serial.println("Voice: No Audio* object for playback");
      return false;
    }

    enableDAC();

    // If mic recording tore down I2S_NUM_0, the Audio object's internal I2S
    // state is stale — it can't reconfigure sample rate for the WAV file.
    // Fix: reinstall a valid TX driver first (so Audio destructor won't crash
    // on i2s_zero_dma_buffer), then delete and recreate the Audio object
    // to get fresh internal state. This matches the audiobook's pattern
    // where new Audio() after voice recording works correctly.
    if (!_i2sInitialized) {
      reinstallI2SForPlayback();
      Serial.println("Voice: Recreating Audio object for clean I2S state");
      delete _audio;
      _audio = new Audio();
      bool ok = _audio->setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT, 0);
      if (!ok) ok = _audio->setPinout(BOARD_I2S_BCLK, BOARD_I2S_LRC, BOARD_I2S_DOUT);
      if (!ok) Serial.println("Voice: DAC setPinout FAILED");
      _i2sInitialized = true;
    }

    char fullPath[96];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", VOICE_FOLDER, filename);

    _audio->setVolume(21);  // Max volume for voice playback
    bool ok = _audio->connecttoFS(SD, fullPath);
    if (!ok) {
      Serial.printf("Voice: Failed to open %s for playback\n", fullPath);
      return false;
    }

    Serial.printf("Voice: Playing %s\n", fullPath);
    return true;
  }

  void stopPlayback() {
    if (_audio && _i2sInitialized) {
      _audio->stopSong();
    }
    _reviewPlaying = false;
    _listPlaying = false;
  }

  // ---------------------------------------------------------------------------
  // File list scanning
  // ---------------------------------------------------------------------------
  void scanVoiceFolder() {
    _fileList.clear();
    _selectedFile = 0;
    _scrollOffset = 0;

    if (!SD.exists(VOICE_FOLDER)) {
      SD.mkdir(VOICE_FOLDER);
      return;
    }

    File dir = SD.open(VOICE_FOLDER);
    if (!dir || !dir.isDirectory()) return;

    File entry;
    while ((entry = dir.openNextFile()) && _fileList.size() < VOICE_MAX_FILES) {
      String name = entry.name();
      // Only show .wav files
      if (!name.endsWith(".wav") && !name.endsWith(".WAV")) {
        entry.close();
        continue;
      }

      VoiceFileEntry vfe;
      strncpy(vfe.name, name.c_str(), sizeof(vfe.name) - 1);
      vfe.name[sizeof(vfe.name) - 1] = '\0';
      vfe.sizeBytes = entry.size();
      // Estimate duration from file size (subtract 44-byte header)
      uint32_t dataBytes = (vfe.sizeBytes > 44) ? (vfe.sizeBytes - 44) : 0;
      vfe.durationSec = dataBytes / (float)(VOICE_SAMPLE_RATE * sizeof(int16_t));

      _fileList.push_back(vfe);
      entry.close();
    }
    dir.close();

    // Sort by name (newest first, since filenames are timestamp-based)
    std::sort(_fileList.begin(), _fileList.end(),
              [](const VoiceFileEntry& a, const VoiceFileEntry& b) {
                return strcmp(a.name, b.name) > 0;  // Descending
              });

    Serial.printf("Voice: Scanned %d files in %s\n", _fileList.size(), VOICE_FOLDER);
  }

  bool deleteFile(const char* filename) {
    char fullPath[96];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", VOICE_FOLDER, filename);
    if (SD.remove(fullPath)) {
      Serial.printf("Voice: Deleted %s\n", fullPath);
      return true;
    }
    Serial.printf("Voice: Failed to delete %s\n", fullPath);
    return false;
  }

  // Load a WAV file from the voice folder into the recording buffer,
  // then Codec2-encode it so it can be sent via the contact picker.
  bool loadWavForSend(const char* filename) {
    char fullPath[96];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", VOICE_FOLDER, filename);

    File f = SD.open(fullPath, FILE_READ);
    if (!f) {
      Serial.printf("Voice: Failed to open %s for send\n", fullPath);
      return false;
    }

    uint32_t fileSize = f.size();
    if (fileSize <= 44) {
      Serial.printf("Voice: File too small: %d bytes\n", fileSize);
      f.close();
      return false;
    }

    // Skip 44-byte WAV header
    f.seek(44);
    uint32_t dataBytes = fileSize - 44;
    uint32_t samples = dataBytes / sizeof(int16_t);

    // Clamp to buffer size
    if (samples > VOICE_BUF_SAMPLES) samples = VOICE_BUF_SAMPLES;

    // Ensure PSRAM buffer exists
    if (!ensureRecBuffer()) {
      f.close();
      return false;
    }

    f.read((uint8_t*)_recBuffer, samples * sizeof(int16_t));
    f.close();

    _recSamples = samples;
    Serial.printf("Voice: Loaded %s — %d samples (%.1fs)\n",
                  filename, samples, samples / (float)VOICE_SAMPLE_RATE);

    // Encode to Codec2
    encodeCodec2();
    if (!_c2Valid) {
      Serial.println("Voice: Codec2 encode failed for loaded file");
      return false;
    }

    // Set review filename so the UI can display it
    strncpy(_reviewFilename, filename, sizeof(_reviewFilename) - 1);
    _reviewFilename[sizeof(_reviewFilename) - 1] = '\0';

    return true;
  }

  // ---------------------------------------------------------------------------
  // VE3 Voice Protocol (dz0ny) — helpers
  // ---------------------------------------------------------------------------

  // Base36 encode a uint32 into a string buffer (compact wire format)
  static int toBase36(uint32_t val, char* buf, int bufLen) {
    if (bufLen < 2) return 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[16];
    int pos = 0;
    while (val > 0 && pos < 15) {
      uint8_t d = val % 36;
      tmp[pos++] = d < 10 ? ('0' + d) : ('a' + d - 10);
      val /= 36;
    }
    // Reverse
    int len = pos < bufLen - 1 ? pos : bufLen - 1;
    for (int i = 0; i < len; i++) buf[i] = tmp[pos - 1 - i];
    buf[len] = '\0';
    return len;
  }

  // Format VE3 envelope string: VE3:{sid}:{mode}:{total}:{durS}
  void formatVE3(char* buf, int bufLen, uint32_t sessionId,
                 uint8_t totalPackets, uint8_t durationSec) {
    char sid[12], mode[4], total[4], dur[4];
    toBase36(sessionId, sid, sizeof(sid));
    toBase36(VOICE_C2_MODE_ID, mode, sizeof(mode));
    toBase36(totalPackets, total, sizeof(total));
    toBase36(durationSec, dur, sizeof(dur));
    snprintf(buf, bufLen, "VE3:%s:%s:%s:%s", sid, mode, total, dur);
  }

  // Cache outgoing session for serving fetch requests
  void cacheOutSession(uint32_t sessionId) {
    _outSession.sessionId = sessionId;
    memcpy(_outSession.data, _c2Data, _c2Bytes);
    _outSession.dataBytes = _c2Bytes;
    // Calculate packet count
    int payloadPerPkt = VOICE_MESH_PAYLOAD;
    _outSession.totalPackets = (_c2Bytes + payloadPerPkt - 1) / payloadPerPkt;
    _outSession.durationSec = (uint8_t)(_c2Frames * VOICE_C2_FRAME_MS / 1000);
    _outSession.cachedAt = millis();
    _outSession.active = true;
    Serial.printf("Voice: Session 0x%08X cached — %d bytes, %d packets, %ds\n",
                  sessionId, _c2Bytes, _outSession.totalPackets, _outSession.durationSec);
  }

  // Base36 decode (compact wire format from VE3 envelope)
  static uint32_t fromBase36(const char* s) {
    uint32_t val = 0;
    while (*s) {
      val *= 36;
      char c = *s++;
      if (c >= '0' && c <= '9') val += c - '0';
      else if (c >= 'a' && c <= 'z') val += 10 + c - 'a';
      else if (c >= 'A' && c <= 'Z') val += 10 + c - 'A';
    }
    return val;
  }

  // ---------------------------------------------------------------------------
  // Incoming voice session — accumulate packets, decode when complete
  // ---------------------------------------------------------------------------

  // Decode Codec2 data into WAV and play through DAC
  bool decodeAndPlayIncoming() {
    if (!_inSession.complete || _inSession.dataBytes == 0) return false;

    Serial.printf("Voice: Decoding incoming session 0x%08X — %d bytes from %s\n",
                  _inSession.sessionId, _inSession.dataBytes, _inSession.senderName);

    // Reassemble codec2 data in packet order (packets may arrive out of order)
    uint8_t ordered[VOICE_C2_MAX_BYTES];
    uint32_t orderedLen = 0;
    for (int p = 0; p < _inSession.totalPackets && p < 16; p++) {
      if (_inSession.pktSize[p] > 0 && orderedLen + _inSession.pktSize[p] <= VOICE_C2_MAX_BYTES) {
        memcpy(&ordered[orderedLen], &_inSession.data[_inSession.pktOffset[p]], _inSession.pktSize[p]);
        orderedLen += _inSession.pktSize[p];
      }
    }

    Serial.printf("Voice: Reassembled %d bytes in order from %d packets\n",
                  orderedLen, _inSession.totalPackets);

    // Create Codec2 decoder
    struct CODEC2* c2 = codec2_create(VOICE_C2_MODE);
    if (!c2) {
      Serial.println("Voice: codec2_create failed for decode");
      return false;
    }

    int frameSamples = codec2_samples_per_frame(c2);  // 320 at 8kHz
    int frameBytes = (codec2_bits_per_frame(c2) + 7) / 8;  // 6 at 1200bps

    // Decode all frames into PSRAM buffer (reuse _recBuffer, upsampled to 16kHz)
    if (!ensureRecBuffer()) {
      codec2_destroy(c2);
      return false;
    }

    uint32_t srcPos = 0;
    uint32_t dstPos = 0;
    int16_t frameBuf[VOICE_C2_FRAME_SAM];  // 320 samples at 8kHz

    while (srcPos + frameBytes <= orderedLen &&
           dstPos + frameSamples * 2 <= VOICE_BUF_SAMPLES) {
      // Decode one frame to 8kHz
      codec2_decode(c2, frameBuf, &ordered[srcPos]);
      srcPos += frameBytes;

      // Upsample 8kHz → 16kHz by duplicating each sample
      for (int i = 0; i < frameSamples; i++) {
        _recBuffer[dstPos++] = frameBuf[i];
        _recBuffer[dstPos++] = frameBuf[i];
      }
    }

    codec2_destroy(c2);
    _recSamples = dstPos;

    float secs = _recSamples / (float)VOICE_SAMPLE_RATE;
    Serial.printf("Voice: Decoded %d frames → %d samples (%.1fs at 16kHz)\n",
                  (int)(srcPos / frameBytes), _recSamples, secs);

    // Save as WAV for playback
    uint32_t ts = millis() / 1000;
    snprintf(_reviewFilename, sizeof(_reviewFilename),
             "voice_rx_%06lu.wav", ts % 1000000UL);

    char fullPath[96];
    snprintf(fullPath, sizeof(fullPath), "%s/%s", VOICE_FOLDER, _reviewFilename);

    if (!SD.exists(VOICE_FOLDER)) SD.mkdir(VOICE_FOLDER);
    File f = SD.open(fullPath, FILE_WRITE);
    if (!f) {
      Serial.printf("Voice: Failed to create %s\n", fullPath);
      return false;
    }

    uint32_t dataBytes = _recSamples * sizeof(int16_t);
    writeWavHeader(f, dataBytes, VOICE_SAMPLE_RATE, VOICE_BITS, VOICE_CHANNELS);
    f.write((const uint8_t*)_recBuffer, dataBytes);
    f.close();
    Serial.printf("Voice: Saved decoded voice: %s\n", fullPath);

    // Play it
    if (playFile(_reviewFilename)) {
      _reviewPlaying = true;
      _mode = REVIEW;
      return true;
    }
    return false;
  }

  // ---------------------------------------------------------------------------
  // Contact picker — populate from main.cpp (avoids MyMesh dependency)
  // ---------------------------------------------------------------------------
  void enterContactPick() {
    _mode = CONTACT_PICK;
    _pickSelected = 0;
    _pickScroll = 0;
    _pendingSendIdx = -1;
  }

  // ---------------------------------------------------------------------------
  // Contact picker rendering
  // ---------------------------------------------------------------------------
  void renderContactPick(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("Send Voice To:");
    display.fillRect(0, 11, display.width(), 1);

    if (_pickList.empty()) {
      display.setCursor(10, 50);
      display.print("No contacts with");
      display.setCursor(10, 65);
      display.print("direct path.");
    } else {
      int y = 14;
      int lineH = 12;
      int visibleLines = (display.height() - 14 - 14) / lineH;

      if (_pickSelected < _pickScroll) _pickScroll = _pickSelected;
      if (_pickSelected >= _pickScroll + visibleLines)
        _pickScroll = _pickSelected - visibleLines + 1;

      for (int i = _pickScroll;
           i < (int)_pickList.size() && i < _pickScroll + visibleLines;
           i++) {
        int yy = y + (i - _pickScroll) * lineH;

        if (i == _pickSelected) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, yy - 1, display.width(), lineH);
          display.setColor(DisplayDriver::DARK);
        }

        // Type indicator + name
        char line[40];
        snprintf(line, sizeof(line), "%c %s",
                 _pickList[i].hasDirect ? '>' : ' ',
                 _pickList[i].name);
        display.setCursor(2, yy);
        display.print(line);

        if (i == _pickSelected) {
          display.setColor(DisplayDriver::GREEN);
        }
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.setCursor(0, footerY);
    display.print("Ent:Send Q:Cancel");
  }

  // Contact picker input
  void handlePickInput(char key) {
    switch (key) {
      case 'w': case 'W': case 0xF0:
        if (_pickSelected > 0) _pickSelected--;
        break;
      case 's': case 'S': case 0xF1:
        if (_pickSelected < (int)_pickList.size() - 1) _pickSelected++;
        break;
      case '\r':  // Enter — confirm send
        if (!_pickList.empty() && _pickSelected < (int)_pickList.size()) {
          if (_pickList[_pickSelected].hasDirect) {
            _pendingSendIdx = _pickList[_pickSelected].meshIdx;
            _mode = REVIEW;  // Dismiss picker immediately
            Serial.printf("Voice: Send confirmed to contact idx %d (%s)\n",
                          _pendingSendIdx, _pickList[_pickSelected].name);
          } else {
            Serial.println("Voice: Contact has no direct path — cannot send");
          }
        }
        break;
      case 'q': case 'Q':
        _mode = REVIEW;
        break;
    }
  }

  // ---------------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------------
  void renderMessageList(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);

    // Title bar
    display.setCursor(0, 0);
    display.print("Voice Messages");
    
    char countStr[16];
    snprintf(countStr, sizeof(countStr), "%d files", (int)_fileList.size());
    display.setCursor(display.width() - display.getTextWidth(countStr) - 2, 0);
    display.print(countStr);

    display.fillRect(0, 11, display.width(), 1);  // horizontal rule

    if (_fileList.empty()) {
      display.setCursor(10, 60);
      display.print("No voice messages.");
      display.setCursor(10, 80);
      display.print("Hold Mic key to record.");
    } else {
      // File list
      int y = 14;
      int lineH = 12;
      int visibleLines = (display.height() - 14 - 14) / lineH;  // header + footer

      // Ensure selection is visible
      if (_selectedFile < _scrollOffset) _scrollOffset = _selectedFile;
      if (_selectedFile >= _scrollOffset + visibleLines)
        _scrollOffset = _selectedFile - visibleLines + 1;

      for (int i = _scrollOffset;
           i < (int)_fileList.size() && i < _scrollOffset + visibleLines;
           i++) {
        int yy = y + (i - _scrollOffset) * lineH;

        if (i == _selectedFile) {
          // Highlight: fill with LIGHT, draw DARK text
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, yy - 1, display.width(), lineH);
          display.setColor(DisplayDriver::DARK);
        }

        display.setCursor(2, yy);
        display.print(_fileList[i].name);

        // Duration on right
        char dur[16];
        snprintf(dur, sizeof(dur), "%.1fs", _fileList[i].durationSec);
        display.setCursor(display.width() - display.getTextWidth(dur) - 4, yy);
        display.print(dur);

        if (i == _selectedFile) {
          display.setColor(DisplayDriver::GREEN);
        }
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.setCursor(0, footerY);
    if (_listPlaying) {
      display.print("Playing... Q:Stop");
    } else if (!_fileList.empty()) {
      display.print("Mic:Rec Ent:Ply F:Snd D:Del");
    } else {
      display.print("Mic:Record Q:Exit");
    }
  }

  void renderRecording(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("RECORDING");

    // Elapsed time
    float elapsed = _recSamples / (float)VOICE_SAMPLE_RATE;
    char timeStr[16];
    snprintf(timeStr, sizeof(timeStr), "%.1f / %ds", elapsed, VOICE_MAX_SECONDS);
    display.setCursor(display.width() - display.getTextWidth(timeStr) - 2, 0);
    display.print(timeStr);

    display.fillRect(0, 11, display.width(), 1);  // horizontal rule

    // Large centred "recording" indicator
    display.setTextSize(2);
    const char* recLabel = "REC";
    int labelW = display.getTextWidth(recLabel);
    display.setCursor((display.width() - labelW) / 2, 50);
    display.print(recLabel);
    display.setTextSize(1);

    // Progress bar
    int barX = 10;
    int barY = 90;
    int barW = display.width() - 20;
    int barH = 12;
    float progress = (float)_recSamples / VOICE_BUF_SAMPLES;
    if (progress > 1.0f) progress = 1.0f;

    display.drawRect(barX, barY, barW, barH);
    int fillW = (int)(progress * (barW - 2));
    if (fillW > 0) {
      display.fillRect(barX + 1, barY + 1, fillW, barH - 2);
    }

    // Simple level meter — average of last 256 samples
    if (_recSamples > 256) {
      int32_t sum = 0;
      for (uint32_t i = _recSamples - 256; i < _recSamples; i++) {
        int16_t s = _recBuffer[i];
        sum += (s < 0) ? -s : s;
      }
      int avgLevel = sum / 256;
      int meterW = (avgLevel * (barW - 2)) / 16384;  // Scale to bar width
      if (meterW > barW - 2) meterW = barW - 2;

      int meterY = barY + barH + 8;
      display.drawRect(barX, meterY, barW, 8);
      if (meterW > 0) {
        display.fillRect(barX + 1, meterY + 1, meterW, 6);
      }
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.setCursor(0, footerY);
    display.print("Release Mic to stop");
  }

  void renderReview(DisplayDriver& display) {
    display.setColor(DisplayDriver::GREEN);
    display.setTextSize(1);

    display.setCursor(0, 0);
    display.print("Review Recording");
    display.fillRect(0, 11, display.width(), 1);  // horizontal rule

    // Filename
    display.setCursor(10, 30);
    display.print(_reviewFilename);

    // Duration
    float secs = _recSamples / (float)VOICE_SAMPLE_RATE;
    char durStr[32];
    snprintf(durStr, sizeof(durStr), "Duration: %.1f seconds", secs);
    display.setCursor(10, 50);
    display.print(durStr);

    // Size
    uint32_t sizeBytes = 44 + _recSamples * sizeof(int16_t);
    char sizeStr[32];
    if (sizeBytes > 1024) {
      snprintf(sizeStr, sizeof(sizeStr), "Size: %.1f KB", sizeBytes / 1024.0f);
    } else {
      snprintf(sizeStr, sizeof(sizeStr), "Size: %d bytes", sizeBytes);
    }
    display.setCursor(10, 70);
    display.print(sizeStr);

    // Codec2 encoding results
    if (_c2Valid) {
      int packets = (_c2Bytes + VOICE_MESH_PAYLOAD - 1) / VOICE_MESH_PAYLOAD;
      char c2Str[48];
      snprintf(c2Str, sizeof(c2Str), "Codec2: %d bytes (%d pkt%s)",
               _c2Bytes, packets, packets == 1 ? "" : "s");
      display.setCursor(10, 90);
      display.print(c2Str);
    } else {
      display.setCursor(10, 90);
      display.print("Codec2: encode failed");
    }

    // Status
    display.setCursor(10, 110);
    if (_reviewPlaying) {
      display.print("Playing...");
    } else {
      display.print("Ready");
    }

    // Footer
    int footerY = display.height() - 12;
    display.setTextSize(1);
    display.setCursor(0, footerY);
    if (_reviewPlaying) {
      display.print("Q:Stop");
    } else if (_c2Valid) {
      display.print("S:Send Ent:Play Mic:Redo Q:List");
    } else {
      display.print("Ent:Play Mic:Redo D:Del Q:List");
    }
  }

public:
  VoiceMessageScreen(UITask* task, Audio* audioObj)
    : _task(task), _audio(audioObj), _mode(MESSAGE_LIST),
      _sdReady(false), _i2sInitialized(false), _micInitialized(false),
      _dacPowered(false), _displayRef(nullptr),
      _selectedFile(0), _scrollOffset(0),
      _recBuffer(nullptr), _recSamples(0), _recording(false), _recStartMillis(0),
      _reviewPlaying(false), _reviewDirty(false),
      _listPlaying(false), _listPlayIdx(-1),
      _playbackJustFinished(false),
      _c2Bytes(0), _c2Frames(0), _c2Valid(false),
      _pickSelected(0), _pickScroll(0), _pendingSendIdx(-1) {
    _reviewFilename[0] = '\0';
    _outSession.active = false;
    _inSession.active = false;
    _inSession.complete = false;
    _inSession.playTriggered = false;
  }

  void setSDReady(bool v) { _sdReady = v; }
  void setAudio(Audio* a) { _audio = a; }
  Audio* getAudio() const { return _audio; }
  bool isRecording() const { return _recording; }
  Mode getMode() const { return _mode; }

  // Codec2 encoded data access
  bool hasCodec2Data() const { return _c2Valid; }
  const uint8_t* getCodec2Data() const { return _c2Data; }
  uint32_t getCodec2Bytes() const { return _c2Bytes; }
  uint32_t getCodec2Frames() const { return _c2Frames; }

  // --- VE3 send protocol: main.cpp polls these to drive the send ---

  // Check if user confirmed a send target in contact picker
  // Returns contact mesh index, or -1 if no pending send.
  // Consuming clears the pending state.
  int consumePendingSend() {
    int idx = _pendingSendIdx;
    _pendingSendIdx = -1;
    return idx;
  }

  // Format the VE3 envelope text for a DM (called by main.cpp before send)
  void formatEnvelope(char* buf, int bufLen, uint32_t sessionId) {
    if (!_c2Valid) { buf[0] = '\0'; return; }
    int payloadPerPkt = VOICE_MESH_PAYLOAD;
    uint8_t totalPkts = (_c2Bytes + payloadPerPkt - 1) / payloadPerPkt;
    uint8_t durSec = (uint8_t)(_c2Frames * VOICE_C2_FRAME_MS / 1000);
    formatVE3(buf, bufLen, sessionId, totalPkts, durSec);
    // Cache session for serving fetch requests
    cacheOutSession(sessionId);
  }

  // Build a single raw voice packet for transmission
  // Returns packet length, or 0 if index is out of range
  int buildVoicePacket(uint8_t* buf, int bufLen, uint32_t sessionId, uint8_t pktIdx) {
    if (!_outSession.active || _outSession.sessionId != sessionId) return 0;
    int payloadPerPkt = VOICE_MESH_PAYLOAD;
    uint32_t offset = (uint32_t)pktIdx * payloadPerPkt;
    if (offset >= _outSession.dataBytes) return 0;
    uint32_t chunkLen = _outSession.dataBytes - offset;
    if (chunkLen > (uint32_t)payloadPerPkt) chunkLen = payloadPerPkt;
    if ((int)(VOICE_PKT_HDR_SIZE + chunkLen) > bufLen) return 0;

    buf[0] = VOICE_PKT_MAGIC;  // 0x56
    memcpy(&buf[1], &sessionId, 4);
    buf[5] = pktIdx;
    memcpy(&buf[6], &_outSession.data[offset], chunkLen);
    return VOICE_PKT_HDR_SIZE + chunkLen;
  }

  uint8_t getOutSessionPacketCount() const {
    return _outSession.active ? _outSession.totalPackets : 0;
  }
  uint32_t getOutSessionId() const {
    return _outSession.active ? _outSession.sessionId : 0;
  }
  bool hasValidOutSession() const {
    return _outSession.active &&
           (millis() - _outSession.cachedAt) < VOICE_SESSION_TTL_MS;
  }

  // Called when send completes — return to review with status
  void onSendComplete(bool success) {
    _mode = REVIEW;
    Serial.printf("Voice: Send %s\n", success ? "complete" : "failed");
  }

  // --- Incoming voice session API (called from main.cpp callbacks) ---

  // Parse a VE3 envelope and set up incoming session
  // Format: VE3:{sid}:{mode}:{total}:{durS} (base36 fields)
  void onVE3Received(const char* senderName, const char* ve3Text) {
    // Parse: skip "VE3:" prefix, split on ':'
    const char* p = ve3Text + 4;  // skip "VE3:"
    char fields[4][16];
    int fieldIdx = 0;
    int charIdx = 0;
    memset(fields, 0, sizeof(fields));
    while (*p && fieldIdx < 4) {
      if (*p == ':') {
        fields[fieldIdx][charIdx] = '\0';
        fieldIdx++;
        charIdx = 0;
      } else if (charIdx < 15) {
        fields[fieldIdx][charIdx++] = *p;
      }
      p++;
    }
    if (fieldIdx < 3) {
      Serial.printf("Voice: VE3 parse failed — only %d fields\n", fieldIdx + 1);
      return;
    }
    fields[fieldIdx][charIdx] = '\0';  // terminate last field

    uint32_t sessionId = fromBase36(fields[0]);
    // uint8_t  codecMode = (uint8_t)fromBase36(fields[1]);  // not used yet
    uint8_t  totalPkts = (uint8_t)fromBase36(fields[2]);
    uint8_t  durSec    = (fieldIdx >= 3) ? (uint8_t)fromBase36(fields[3]) : 0;

    if (totalPkts == 0 || totalPkts > 16) {
      Serial.printf("Voice: VE3 invalid packet count: %d\n", totalPkts);
      return;
    }

    // Set up incoming session
    _inSession.sessionId = sessionId;
    _inSession.totalPackets = totalPkts;
    _inSession.receivedBitmap = 0;
    _inSession.receivedCount = 0;
    _inSession.dataBytes = 0;
    _inSession.durationSec = durSec;
    strncpy(_inSession.senderName, senderName, 31);
    _inSession.senderName[31] = '\0';
    _inSession.startedAt = millis();
    _inSession.active = true;
    _inSession.complete = false;
    _inSession.playTriggered = false;
    memset(_inSession.pktOffset, 0, sizeof(_inSession.pktOffset));
    memset(_inSession.pktSize, 0, sizeof(_inSession.pktSize));

    Serial.printf("Voice: Incoming session 0x%08X from %s — expecting %d packets (%ds)\n",
                  sessionId, senderName, totalPkts, durSec);
  }

  // Add a received voice data packet to the incoming session
  // payload: [0x56][sessionId:4B][index:1B][codec2 data...]
  void onVoicePacketReceived(const uint8_t* payload, uint8_t len) {
    if (len < 7) return;  // Need at least header + 1 byte data
    uint32_t sessionId;
    memcpy(&sessionId, &payload[1], 4);
    uint8_t pktIdx = payload[5];
    uint8_t dataLen = len - VOICE_PKT_HDR_SIZE;

    if (!_inSession.active || _inSession.sessionId != sessionId) {
      Serial.printf("Voice: Ignoring packet for unknown session 0x%08X\n", sessionId);
      return;
    }
    if (pktIdx >= _inSession.totalPackets || pktIdx >= 16) {
      Serial.printf("Voice: Packet index %d out of range (total=%d)\n",
                    pktIdx, _inSession.totalPackets);
      return;
    }
    if (_inSession.receivedBitmap & (1 << pktIdx)) {
      // Already have this packet (duplicate)
      return;
    }
    if (_inSession.dataBytes + dataLen > VOICE_C2_MAX_BYTES) {
      Serial.println("Voice: Incoming session data overflow");
      return;
    }

    // Store the codec2 data at the next available offset
    _inSession.pktOffset[pktIdx] = _inSession.dataBytes;
    _inSession.pktSize[pktIdx] = dataLen;
    memcpy(&_inSession.data[_inSession.dataBytes], &payload[VOICE_PKT_HDR_SIZE], dataLen);
    _inSession.dataBytes += dataLen;
    _inSession.receivedBitmap |= (1 << pktIdx);
    _inSession.receivedCount++;

    Serial.printf("Voice: Received packet %d/%d (%d bytes, total %d bytes)\n",
                  _inSession.receivedCount, _inSession.totalPackets,
                  dataLen, _inSession.dataBytes);

    // Check if all packets received
    if (_inSession.receivedCount >= _inSession.totalPackets) {
      _inSession.complete = true;
      Serial.printf("Voice: Session 0x%08X complete — %d bytes from %s\n",
                    sessionId, _inSession.dataBytes, _inSession.senderName);
    }
  }

  // Check if incoming session is complete and ready for playback
  bool isIncomingReady() const {
    return _inSession.active && _inSession.complete && !_inSession.playTriggered;
  }

  // Trigger decode + playback of completed incoming session
  bool playIncoming() {
    if (!isIncomingReady()) return false;
    _inSession.playTriggered = true;
    return decodeAndPlayIncoming();
  }

  // Load contacts for the picker (called from main.cpp)
  void loadPickContacts(const PickContact* contacts, int count) {
    _pickList.clear();
    for (int i = 0; i < count; i++) {
      _pickList.push_back(contacts[i]);
    }
    _pickSelected = 0;
    _pickScroll = 0;
    Serial.printf("Voice: Contact picker loaded %d contacts\n", count);
  }

  // Called by main.cpp loop to detect end-of-playback and refresh UI
  void checkPlaybackFinished() {
    if (!_i2sInitialized) return;  // I2S torn down by mic, no playback possible
    if ((_reviewPlaying || _listPlaying) && _audio && !_audio->isRunning()) {
      Serial.println("Voice: Playback finished");
      _reviewPlaying = false;
      _listPlaying = false;
      _playbackJustFinished = true;
    }
  }

  // Check and clear the playback-finished flag (for main.cpp refresh trigger)
  bool consumePlaybackFinished() {
    if (_playbackJustFinished) {
      _playbackJustFinished = false;
      return true;
    }
    return false;
  }

  // Called from main.cpp loop — services mic DMA reads during recording
  // and audio decode during playback (like audiobook audioTick)
  void voiceTick() {
    if (_recording) {
      captureChunk();
    }
    // Audio playback is serviced by audio->loop() in the shared audioTick path
  }

  // Check if DAC audio is active (for CPU boost in main loop)
  bool isAudioActive() const {
    return _i2sInitialized && _audio && _audio->isRunning();
  }

  // ---------------------------------------------------------------------------
  // UIScreen interface
  // ---------------------------------------------------------------------------
  void enter(DisplayDriver& display) {
    _displayRef = &display;
    _mode = MESSAGE_LIST;
    _reviewPlaying = false;
    _listPlaying = false;
    scanVoiceFolder();
  }

  int render(DisplayDriver& display) override {
    switch (_mode) {
      case MESSAGE_LIST:  renderMessageList(display);  break;
      case RECORDING:     renderRecording(display);    break;
      case REVIEW:        renderReview(display);       break;
      case CONTACT_PICK:  renderContactPick(display);  break;
    }
    return 0;
  }

  // ---------------------------------------------------------------------------
  // Mic key press — start recording (called from main.cpp on KB_KEY_MIC)
  // ---------------------------------------------------------------------------
  void onMicPress() {
    if (_mode == MESSAGE_LIST || _mode == REVIEW) {
      // Stop any playback first
      stopPlayback();
      
      // Start recording
      _mode = RECORDING;
      if (!startRecording()) {
        Serial.println("Voice: Failed to start recording");
        _mode = MESSAGE_LIST;
      }
    }
    // If already recording, ignore (release will stop it)
  }

  // ---------------------------------------------------------------------------
  // Mic key release — stop recording, save, enter review
  // ---------------------------------------------------------------------------
  void onMicRelease() {
    if (_mode == RECORDING && _recording) {
      stopRecording();

      if (_recSamples < VOICE_SAMPLE_RATE / 4) {
        // Too short — discard and return to list
        Serial.println("Voice: Recording too short, discarding");
        _mode = MESSAGE_LIST;
        return;
      }

      // Save to SD
      if (saveRecordingToSD()) {
        _mode = REVIEW;
      } else {
        _mode = MESSAGE_LIST;
      }
    }
    // If mode is already REVIEW (auto-stop filled buffer), mic release is a no-op
  }

  // ---------------------------------------------------------------------------
  // Key input handler (UIScreen interface + direct calls from main.cpp)
  // ---------------------------------------------------------------------------
  bool handleInput(char key) override {
    switch (_mode) {
      case MESSAGE_LIST:
        handleListInput(key);
        return true;
      case RECORDING:
        if (key == 'q' || key == 'Q') {
          stopRecording();
          _mode = MESSAGE_LIST;
        }
        return true;
      case REVIEW:
        handleReviewInput(key);
        return true;
      case CONTACT_PICK:
        handlePickInput(key);
        return true;
    }
    return false;
  }

private:
  void handleListInput(char key) {
    switch (key) {
      case 'w': case 'W':
      case 0xF0:  // KEY_PREV
        if (_selectedFile > 0) _selectedFile--;
        break;

      case 's': case 'S':
      case 0xF1:  // KEY_NEXT
        if (_selectedFile < (int)_fileList.size() - 1) _selectedFile++;
        break;

      case '\r':  // Enter — play selected
        if (!_fileList.empty() && _selectedFile < (int)_fileList.size()) {
          if (_listPlaying) {
            stopPlayback();
          } else {
            if (playFile(_fileList[_selectedFile].name)) {
              _listPlaying = true;
              _listPlayIdx = _selectedFile;
            }
          }
        }
        break;

      case 'd': case 'D':  // Delete selected
        if (!_fileList.empty() && _selectedFile < (int)_fileList.size()) {
          stopPlayback();
          deleteFile(_fileList[_selectedFile].name);
          scanVoiceFolder();
          if (_selectedFile >= (int)_fileList.size() && _selectedFile > 0) {
            _selectedFile--;
          }
        }
        break;

      case 'f': case 'F':  // Forward/send selected file
        if (!_fileList.empty() && _selectedFile < (int)_fileList.size()) {
          stopPlayback();
          if (loadWavForSend(_fileList[_selectedFile].name)) {
            enterContactPick();
            // main.cpp will detect CONTACT_PICK mode and load contacts
          }
        }
        break;

      // q/Q handled by main.cpp (exits voice screen)
    }
  }

  void handleReviewInput(char key) {
    switch (key) {
      case '\r':  // Enter — play/stop review
        if (_reviewPlaying) {
          stopPlayback();
        } else {
          if (playFile(_reviewFilename)) {
            _reviewPlaying = true;
          }
        }
        break;

      case 'd': case 'D':  // Delete and go back to list
        stopPlayback();
        deleteFile(_reviewFilename);
        _reviewFilename[0] = '\0';
        _mode = MESSAGE_LIST;
        scanVoiceFolder();
        break;

      case 'q': case 'Q':  // Back to list (keep the file)
        stopPlayback();
        _mode = MESSAGE_LIST;
        scanVoiceFolder();
        break;

      case 's': case 'S':  // Send — enter contact picker
        if (_c2Valid) {
          stopPlayback();
          enterContactPick();
          // main.cpp will detect CONTACT_PICK mode and call loadPickContacts()
        }
        break;

      // Mic key re-record is handled via onMicPress() from main.cpp
    }
  }
};

#endif // MECK_AUDIO_VARIANT