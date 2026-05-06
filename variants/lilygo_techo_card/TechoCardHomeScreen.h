// =============================================================================
// TechoCardHomeScreen -- 72x40 OLED home screen for LilyGo T-Echo Card
//
// Four-line layout using U8g2's 4x6 tom_thumb font (18 chars x 4 lines).
// U8g2's native SSD1306_72X40_ER support handles all GDDRAM offset mapping.
//
// Two-button navigation:  A (pin 42) = next page / long-press activate
//                         C (pin 24) = previous page
//
// Pages:  STATUS -> RADIO -> BLE -> ADVERT -> GPS -> COMPASS -> BATTERY -> HIBERNATE
// =============================================================================
#pragma once

#include <math.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/sensors/LocationProvider.h>
#include <target.h>
#include "MyMesh.h"
#include "UITask.h"

// =============================================================================
// Voice recording -- PDM mic -> Codec2 1200bps stream encoding
// =============================================================================
#if defined(HAS_MICROPHONE)
  #include <PDM.h>
  #include <codec2.h>

  // VE3 protocol constants (wire-compatible with ESP32 VoiceMessageScreen)
  #define VC_C2_MODE       CODEC2_MODE_1200
  #define VC_C2_MODE_ID    1       // Codec2 1200bps mode identifier
  #define VC_C2_FRAME_MS   40      // 40ms per frame at 1200bps
  #define VC_C2_FRAME_SAM  320     // 320 samples per frame at 8kHz
  #define VC_C2_FRAME_BYTES 6      // 6 encoded bytes per frame
  #define VC_MAX_SECONDS   5
  #define VC_MAX_FRAMES    (VC_MAX_SECONDS * 1000 / VC_C2_FRAME_MS)  // 125
  #define VC_MAX_BYTES     (VC_MAX_FRAMES * VC_C2_FRAME_BYTES)       // 750
  #define VC_MESH_PAYLOAD  150     // Usable codec2 bytes per mesh packet
  #define VC_PKT_MAGIC     0x56    // Voice packet magic byte
  #define VC_PKT_HDR_SIZE  6       // magic(1) + sessionId(4) + pktIdx(1)
  #define VC_PDM_RATE      16000
  #define VC_PDM_FRAME     (VC_C2_FRAME_SAM * 2)  // 640 16kHz samples per codec frame

  // PDM ring buffer -- 2 codec frames of headroom at 16kHz
  #define VC_PDM_BUF_SAMPLES  (VC_PDM_FRAME * 2)   // 1280 samples = 2560 bytes

  // Forward declaration for static callback
  class TechoCardHomeScreen;
  static TechoCardHomeScreen* _vcSelf = nullptr;
  static void _vcPdmISR();
#endif

class TechoCardHomeScreen : public UIScreen {
  enum Page {
    STATUS,
    RADIO,
#ifdef BLE_PIN_CODE
    BLE,
#endif
    ADVERT,
#if defined(HAS_MICROPHONE)
    VOICE,
#endif
#if ENV_INCLUDE_GPS == 1
    GPS,
#endif
    COMPASS,
    BATTERY,
    HIBERNATE,
    PAGE_COUNT
  };

  UITask* _task;
  mesh::RTCClock* _rtc;
  NodePrefs* _prefs;
  uint8_t _page;
  bool _shutdown_init;
  unsigned long _shutdown_at;

  // Compass state
  bool _compassInitDone;
  bool _compassOK;
  float _lastHeading;
  int16_t _lastMx, _lastMy, _lastMz;

  // Compass calibration state
  bool _calMode;
  unsigned long _calStart;
  uint16_t _calCount;
  int16_t _calMinX, _calMaxX;
  int16_t _calMinY, _calMaxY;
  int16_t _calMinZ, _calMaxZ;

  // Diagnostic counters (temporary)
  uint16_t _magOk;
  uint16_t _magFail;

#if defined(HAS_MICROPHONE)
  // Voice recording state
  enum VoiceState { V_IDLE, V_RECORDING, V_REVIEW };
  VoiceState _vState;
  struct CODEC2* _vCodec;

  // PDM sample accumulator (filled by ISR, consumed by poll)
  int16_t  _vPdmBuf[VC_PDM_BUF_SAMPLES];
  volatile int _vPdmCount;
  volatile uint32_t _vIsrCount;

  // Codec2 encoded output
  uint8_t  _vEncoded[VC_MAX_BYTES];   // 750 bytes max
  uint16_t _vEncBytes;
  uint16_t _vEncFrames;
  unsigned long _vRecStart;

  // VE3 outgoing session
  uint32_t _vSessionId;
  bool     _vSessionActive;
#endif

  // Four lines at 9px spacing within 40px display.
  // U8g2 handles panel offset natively -- y=0 is the true visible top.
  static const int Y0 = 2;
  static const int Y1 = 11;
  static const int Y2 = 20;
  static const int Y3 = 29;

  int battPercent() {
    uint16_t mv = _task->getBattMilliVolts();
    if (mv == 0) return 0;
    int pct = ((int)mv - 3000) * 100 / 1160;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
  }

  const char* cardinal(float deg) {
    if (deg >= 337.5f || deg < 22.5f) return "N";
    if (deg < 67.5f) return "NE";
    if (deg < 112.5f) return "E";
    if (deg < 157.5f) return "SE";
    if (deg < 202.5f) return "S";
    if (deg < 247.5f) return "SW";
    if (deg < 292.5f) return "W";
    return "NW";
  }

public:
  TechoCardHomeScreen(UITask* task, mesh::RTCClock* rtc, NodePrefs* prefs)
    : _task(task), _rtc(rtc), _prefs(prefs),
      _page(STATUS), _shutdown_init(false), _shutdown_at(0),
      _compassInitDone(false), _compassOK(false),
      _lastHeading(0), _lastMx(0), _lastMy(0), _lastMz(0),
      _calMode(false), _calStart(0), _calCount(0),
      _calMinX(0), _calMaxX(0),
      _calMinY(0), _calMaxY(0),
      _calMinZ(0), _calMaxZ(0),
      _magOk(0), _magFail(0)
#if defined(HAS_MICROPHONE)
      , _vState(V_IDLE), _vCodec(nullptr), _vPdmCount(0), _vIsrCount(0),
      _vEncBytes(0), _vEncFrames(0), _vRecStart(0),
      _vSessionId(0), _vSessionActive(false)
#endif
  {}

  void cancelEditing() { _shutdown_init = false; }

#if defined(HAS_MICROPHONE)
  // --- Voice recording helpers ---

  void onPDMData() {
    int avail = PDM.available();
    if (avail <= 0) return;
    int samples = avail / (int)sizeof(int16_t);
    int space = VC_PDM_BUF_SAMPLES - _vPdmCount;
    if (samples > space) samples = space;
    if (samples > 0) {
      PDM.read(&_vPdmBuf[_vPdmCount], samples * sizeof(int16_t));
      _vPdmCount += samples;
      _vIsrCount++;
    }
  }

  bool voiceStartRecording() {
    if (_vState == V_RECORDING) return false;

    // Codec2 created lazily on first VOICE page visit (see render)
    if (!_vCodec) {
      Serial.println("Voice: no codec2 instance!");
      return false;
    }

    // Reset buffers
    _vPdmCount = 0;
    _vIsrCount = 0;
    _vEncBytes = 0;
    _vEncFrames = 0;

    // Enable speaker amp power rail (RT9080 powers both speaker and mic)
    Serial.println("Voice: enabling speaker rail...");
    board.enableSpeaker(true);
    delay(50);

    // Start PDM capture
    Serial.println("Voice: starting PDM...");
    _vcSelf = this;
    PDM.setPins(PIN_MIC_DATA, PIN_MIC_CLK, -1);
    PDM.onReceive(_vcPdmISR);
    if (!PDM.begin(1, VC_PDM_RATE)) {
      Serial.println("Voice: PDM.begin failed");
      codec2_destroy(_vCodec);
      _vCodec = nullptr;
      board.enableSpeaker(false);
      return false;
    }
    PDM.setGain(80);
    Serial.println("Voice: PDM started OK");

    _vState = V_RECORDING;
    _vRecStart = millis();
    Serial.println("Voice: Recording started");
    return true;
  }

  void voiceStopRecording() {
    if (_vState != V_RECORDING) return;

    // Stop PDM
    PDM.end();
    _vcSelf = nullptr;

    // Encode any remaining samples
    voiceProcessSamples();

    // Keep Codec2 alive -- don't destroy between recordings
    // (destroying and re-creating causes heap fragmentation)
    // if (_vCodec) { codec2_destroy(_vCodec); _vCodec = nullptr; }

    // Power down mic/speaker rail
    board.enableSpeaker(false);

    unsigned long dur = millis() - _vRecStart;
    Serial.printf("Voice: Stopped -- %d frames, %d bytes, %lums\n",
                  _vEncFrames, _vEncBytes, dur);

    _vState = (_vEncFrames > 0) ? V_REVIEW : V_IDLE;
  }

  // Process accumulated PDM samples into Codec2 frames.
  // Called from poll() during recording.
  void voiceProcessSamples() {
    if (_vPdmCount < VC_PDM_FRAME) return;
    if (_vEncBytes + VC_C2_FRAME_BYTES > VC_MAX_BYTES) return;

    // TEST MODE: drain PDM buffer WITHOUT Codec2 encoding
    // If this works, PDM pipeline is fine and issue is Codec2
    int remaining = _vPdmCount - VC_PDM_FRAME;
    if (remaining > 0) {
      memmove((void*)_vPdmBuf, (const void*)&_vPdmBuf[VC_PDM_FRAME],
              remaining * sizeof(int16_t));
    }
    _vPdmCount = remaining;
    _vEncFrames++;
    _vEncBytes += VC_C2_FRAME_BYTES;  // fake it for display
  }

  // VE3 base36 encoding (compact wire format)
  static int toBase36(uint32_t val, char* buf, int bufLen) {
    static const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    if (bufLen < 2) return 0;
    if (val == 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12]; int pos = 0;
    while (val > 0 && pos < 11) { tmp[pos++] = digits[val % 36]; val /= 36; }
    if (pos >= bufLen) pos = bufLen - 1;
    for (int i = 0; i < pos; i++) buf[i] = tmp[pos - 1 - i];
    buf[pos] = '\0';
    return pos;
  }

  // --- Public voice API for main.cpp ---
public:
  // Codec2 must be created from main loop (shallow stack).
  // render() call chain is too deep for codec2_create's 3KB+ stack needs.
  bool needsCodec2() const {
#if defined(HAS_MICROPHONE)
    return _page == VOICE && !_vCodec;
#else
    return false;
#endif
  }
  void setCodec2Instance(struct CODEC2* c2) {
#if defined(HAS_MICROPHONE)
    _vCodec = c2;
#endif
  }

  bool isVoiceReview() const {
#if defined(HAS_MICROPHONE)
    return _vState == V_REVIEW && _vEncFrames > 0;
#else
    return false;
#endif
  }

  // Format VE3 envelope: "VE3:{sid}:{mode}:{total}:{dur}"
  void voiceFormatEnvelope(char* buf, int bufLen, uint32_t sessionId) {
    int payloadPerPkt = VC_MESH_PAYLOAD;
    uint8_t totalPkts = (_vEncBytes + payloadPerPkt - 1) / payloadPerPkt;
    uint8_t durSec = (uint8_t)(_vEncFrames * VC_C2_FRAME_MS / 1000);
    char sid[12], mode[4], total[4], dur[4];
    toBase36(sessionId, sid, sizeof(sid));
    toBase36(VC_C2_MODE_ID, mode, sizeof(mode));
    toBase36(totalPkts, total, sizeof(total));
    toBase36(durSec, dur, sizeof(dur));
    snprintf(buf, bufLen, "VE3:%s:%s:%s:%s", sid, mode, total, dur);

    // Cache session
    _vSessionId = sessionId;
    _vSessionActive = true;
  }

  int voiceBuildPacket(uint8_t* buf, int bufLen, uint32_t sessionId, uint8_t pktIdx) {
    if (!_vSessionActive || _vSessionId != sessionId) return 0;
    uint32_t offset = (uint32_t)pktIdx * VC_MESH_PAYLOAD;
    if (offset >= _vEncBytes) return 0;
    uint32_t chunkLen = _vEncBytes - offset;
    if (chunkLen > VC_MESH_PAYLOAD) chunkLen = VC_MESH_PAYLOAD;
    if ((int)(VC_PKT_HDR_SIZE + chunkLen) > bufLen) return 0;
    buf[0] = VC_PKT_MAGIC;
    memcpy(&buf[1], &sessionId, 4);
    buf[5] = pktIdx;
    memcpy(&buf[6], &_vEncoded[offset], chunkLen);
    return VC_PKT_HDR_SIZE + chunkLen;
  }

  uint8_t voiceGetPacketCount() const {
    if (!_vSessionActive) return 0;
    return (_vEncBytes + VC_MESH_PAYLOAD - 1) / VC_MESH_PAYLOAD;
  }

  void voiceOnSendComplete() {
    _vSessionActive = false;
    _vState = V_IDLE;
    _vEncBytes = 0;
    _vEncFrames = 0;
  }

  void voiceDiscard() {
    _vSessionActive = false;
    _vState = V_IDLE;
    _vEncBytes = 0;
    _vEncFrames = 0;
  }
private:
#endif // HAS_MICROPHONE

  int render(DisplayDriver& display) override {
    char tmp[32];
    display.setTextSize(1);

    switch (_page) {

    // ----- STATUS -----
    case STATUS: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      char filtered_name[sizeof(_prefs->node_name)];
      display.translateUTF8ToBlocks(filtered_name, _prefs->node_name,
                                    sizeof(filtered_name));
      display.print(filtered_name);

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "MSG: %d", _task->getMsgCount());
      display.print(tmp);

      snprintf(tmp, sizeof(tmp), "%d%%", battPercent());
      display.drawTextRightAlign(display.width() - 1, Y1, tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      if (_task->hasConnection()) {
        display.print("Connected");
      } else if (_task->isSerialEnabled()) {
        display.print("BLE: On");
      } else {
        display.print("BLE: Off");
      }
      break;
    }

    // ----- RADIO -----
    case RADIO: {
      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y0);
      snprintf(tmp, sizeof(tmp), "%.1f MHz  SF%d",
               _prefs->freq, _prefs->sf);
      display.print(tmp);

      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "BW %.0f  CR %d",
               _prefs->bw, _prefs->cr);
      display.print(tmp);

      display.setCursor(0, Y2);
      snprintf(tmp, sizeof(tmp), "TX: %d dBm",
               _prefs->tx_power_dbm);
      display.print(tmp);

      display.setCursor(0, Y3);
      snprintf(tmp, sizeof(tmp), "NF: %d",
               radio_driver.getNoiseFloor());
      display.print(tmp);
      break;
    }

#ifdef BLE_PIN_CODE
    // ----- BLE -----
    case BLE: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print(_task->isSerialEnabled() ? "BLE: ON" : "BLE: OFF");

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "PIN: %lu",
               (unsigned long)the_mesh.getBLEPin());
      display.print(tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y3);
      display.print("Hold A: toggle");
      break;
    }
#endif

    // ----- ADVERT -----
    case ADVERT: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print("Advert");

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      display.print("Hold A: send");
      break;
    }

#if defined(HAS_MICROPHONE)
    // ----- VOICE -----
    case VOICE: {
      switch (_vState) {
      case V_IDLE:
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(0, Y0);
        display.print("Voice");

        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, Y2);
        display.print("Hold A: record");
        break;

      case V_RECORDING: {
        unsigned long elapsed = (millis() - _vRecStart) / 1000;
        int remaining = VC_MAX_SECONDS - (int)elapsed;
        if (remaining < 0) remaining = 0;

        display.setColor(DisplayDriver::RED);
        display.setCursor(0, Y0);
        display.print("RECORDING");

        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(0, Y1);
        snprintf(tmp, sizeof(tmp), "%ds left", remaining);
        display.print(tmp);

        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, Y2);
        snprintf(tmp, sizeof(tmp), "Frames: %d", _vEncFrames);
        display.print(tmp);

        display.setCursor(0, Y3);
        snprintf(tmp, sizeof(tmp), "%d bytes", _vEncBytes);
        display.print(tmp);

        return 200;  // Fast refresh during recording
      }

      case V_REVIEW: {
        float durSec = _vEncFrames * VC_C2_FRAME_MS / 1000.0f;
        int packets = (_vEncBytes + VC_MESH_PAYLOAD - 1) / VC_MESH_PAYLOAD;

        display.setColor(DisplayDriver::GREEN);
        display.setCursor(0, Y0);
        display.print("Review");

        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(0, Y1);
        snprintf(tmp, sizeof(tmp), "%.1fs  %d pkt", durSec, packets);
        display.print(tmp);

        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, Y2);
        snprintf(tmp, sizeof(tmp), "%d bytes", _vEncBytes);
        display.print(tmp);

        display.setCursor(0, Y3);
        display.print("A:disc C:disc");
        break;
      }
      } // voice state switch
      break;
    }
#endif

#if ENV_INCLUDE_GPS == 1
    // ----- GPS -----
    case GPS: {
      LocationProvider* loc = sensors.getLocationProvider();

      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      if (!_prefs->gps_enabled) {
        display.print("GPS: OFF");
        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, Y2);
        display.print("Hold A: toggle");
        break;
      }

      display.print("GPS: ON");
      if (loc) {
        snprintf(tmp, sizeof(tmp), "S: %d",
                 loc->satellitesCount());
        display.drawTextRightAlign(display.width() - 1, Y0, tmp);

        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(0, Y1);
        display.print(loc->isValid() ? "Fix: 3D" : "No fix");

        if (loc->isValid()) {
          display.setColor(DisplayDriver::LIGHT);
          display.setCursor(0, Y2);
          snprintf(tmp, sizeof(tmp), "%.4f",
                   loc->getLatitude() / 1000000.0);
          display.print(tmp);

          display.setCursor(0, Y3);
          snprintf(tmp, sizeof(tmp), "%.4f",
                   loc->getLongitude() / 1000000.0);
          display.print(tmp);
        } else {
          // No fix yet -- show NMEA sentence rate to confirm the chip is talking.
          // If this stays at 0, GPS is silent (baud rate wrong, RF off, etc).
          display.setCursor(0, Y2);
          snprintf(tmp, sizeof(tmp), "NMEA: %u/s",
                   (unsigned)gpsStream.getSentencesPerSec());
          display.print(tmp);

          display.setColor(DisplayDriver::LIGHT);
          display.setCursor(0, Y3);
          display.print("Hold A: toggle");
        }
      }
      break;
    }
#endif

    // ----- COMPASS -----
    case COMPASS: {
      if (!_compassInitDone) {
        _compassOK = board.initCompass();
        board.loadCalibration();
        _compassInitDone = true;
      }

      // --- Calibration mode ---
      if (_calMode) {
        int16_t mx, my, mz;
        if (_compassOK && board.readMag(mx, my, mz)) {
          if (_calCount == 0) {
            _calMinX = _calMaxX = mx;
            _calMinY = _calMaxY = my;
            _calMinZ = _calMaxZ = mz;
          } else {
            if (mx < _calMinX) _calMinX = mx;
            if (mx > _calMaxX) _calMaxX = mx;
            if (my < _calMinY) _calMinY = my;
            if (my > _calMaxY) _calMaxY = my;
            if (mz < _calMinZ) _calMinZ = mz;
            if (mz > _calMaxZ) _calMaxZ = mz;
          }
          _calCount++;
        }

        int spreadX = _calMaxX - _calMinX;
        int spreadY = _calMaxY - _calMinY;
        int spreadZ = _calMaxZ - _calMinZ;
        unsigned long elapsed = millis() - _calStart;
        bool adequate = (spreadX >= 100 && spreadY >= 100 && _calCount >= 150);
        bool timeout = (elapsed >= 30000);

        if (adequate || (timeout && spreadX >= 50 && spreadY >= 50)) {
          // Compute and save calibration
          CompassCalibration cal;
          cal.off_x = (_calMinX + _calMaxX) / 2;
          cal.off_y = (_calMinY + _calMaxY) / 2;
          cal.off_z = (_calMinZ + _calMaxZ) / 2;
          float avgRange = ((float)spreadX + (float)spreadY) / 2.0f;
          cal.scale_x = (spreadX > 0) ? avgRange / (float)spreadX : 1.0f;
          cal.scale_y = (spreadY > 0) ? avgRange / (float)spreadY : 1.0f;
          cal.scale_z = (spreadZ > 30) ? avgRange / (float)spreadZ : 1.0f;
          cal.magic = COMPASS_CAL_MAGIC;
          board.saveCalibration(cal);
          _calMode = false;
          _task->showAlert("Cal saved!", 800);
          return 500;
        }

        if (timeout) {
          _calMode = false;
          _task->showAlert("Try again", 800);
          return 500;
        }

        // Calibration progress display
        display.setColor(DisplayDriver::GREEN);
        display.setCursor(0, Y0);
        display.print("Calibrate");

        display.setColor(DisplayDriver::YELLOW);
        display.setCursor(0, Y1);
        display.print("Rotate slowly...");

        display.setColor(DisplayDriver::LIGHT);
        display.setCursor(0, Y2);
        snprintf(tmp, sizeof(tmp), "Samples: %u", _calCount);
        display.print(tmp);

        display.setCursor(0, Y3);
        snprintf(tmp, sizeof(tmp), "X:%d Y:%d", spreadX, spreadY);
        display.print(tmp);

        return 100; // fast sample collection
      }

      // --- Normal compass display ---
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print("Compass");
      if (board.isCalibrated()) {
        display.drawTextRightAlign(display.width() - 1, Y0, "CAL");
      }

      if (!_compassOK) {
        display.setColor(DisplayDriver::RED);
        display.setCursor(0, Y2);
        display.print("IMU not found");
        break;
      }

      int16_t mx, my, mz;
      if (board.readMag(mx, my, mz)) {
        _magOk++;
        // Exponential moving average: 7/8 old + 1/8 new (settles in ~2s)
        if (_magOk == 1) {
          _lastMx = mx; _lastMy = my; _lastMz = mz;
        } else {
          _lastMx = (_lastMx * 7 + mx + 4) >> 3;
          _lastMy = (_lastMy * 7 + my + 4) >> 3;
          _lastMz = (_lastMz * 7 + mz + 4) >> 3;
        }
        float cx = (float)_lastMx;
        float cy = (float)_lastMy;
        if (board.isCalibrated()) {
          const CompassCalibration& cal = board.getCalibration();
          cx = ((float)_lastMx - cal.off_x) * cal.scale_x;
          cy = ((float)_lastMy - cal.off_y) * cal.scale_y;
        }
        // Y axis is inverted relative to compass convention on this PCB
        _lastHeading = atan2f(-cy, cx) * 180.0f / (float)M_PI;
        if (_lastHeading < 0) _lastHeading += 360.0f;
      } else {
        _magFail++;
      }

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "%.0f %s",
               _lastHeading, cardinal(_lastHeading));
      display.print(tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      snprintf(tmp, sizeof(tmp), "X:%d Y:%d", _lastMx, _lastMy);
      display.print(tmp);

      display.setCursor(0, Y3);
      snprintf(tmp, sizeof(tmp), "Z:%d", _lastMz);
      display.print(tmp);

      return 250; // smooth readable refresh
    }

    // ----- BATTERY -----
    case BATTERY: {
      display.setColor(DisplayDriver::GREEN);
      display.setCursor(0, Y0);
      display.print("Battery");

      uint16_t mv = _task->getBattMilliVolts();
      snprintf(tmp, sizeof(tmp), "%d%%", battPercent());
      display.drawTextRightAlign(display.width() - 1, Y0, tmp);

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y1);
      snprintf(tmp, sizeof(tmp), "%d.%02dV", mv / 1000, (mv % 1000) / 10);
      display.print(tmp);

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      {
        float dieTemp = board.getMCUTemperature();
        snprintf(tmp, sizeof(tmp), "Temp: %.0fC", dieTemp);
        display.print(tmp);
      }
      break;
    }

    // ----- HIBERNATE -----
    case HIBERNATE: {
      if (_shutdown_init) {
        display.setColor(DisplayDriver::RED);
        display.setCursor(0, Y1);
        display.print("Shutting down...");
        return 200;
      }

      display.setColor(DisplayDriver::YELLOW);
      display.setCursor(0, Y0);
      display.print("Hibernate");

      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(0, Y2);
      display.print("Hold A: sleep");
      break;
    }
    } // switch

    return 5000;
  }

  bool handleInput(char c) override {
    if (_shutdown_init) {
      _shutdown_init = false;
      return true;
    }

    // Any input during calibration cancels it
    if (_calMode) {
      _calMode = false;
      _task->showAlert("Cancelled", 500);
      return true;
    }

#if defined(HAS_MICROPHONE)
    // During recording: any button press stops recording
    if (_vState == V_RECORDING) {
      voiceStopRecording();
      return true;
    }
#endif

    if (c == KEY_NEXT || c == 'd') {
      _page = (_page + 1) % PAGE_COUNT;
      return true;
    }
    if (c == KEY_PREV || c == 'a') {
      _page = (_page + PAGE_COUNT - 1) % PAGE_COUNT;
      return true;
    }

    if (c == KEY_ENTER) {
      switch (_page) {
#ifdef BLE_PIN_CODE
      case BLE:
        if (_task->isSerialEnabled()) {
          _task->disableSerial();
          _task->showAlert("BLE Off", 800);
        } else {
          _task->enableSerial();
          _task->showAlert("BLE On", 800);
        }
        return true;
#endif

      case ADVERT:
        _task->notify(UIEventType::ack);
        if (the_mesh.advert()) {
          _task->showAlert("Sent!", 800);
        } else {
          _task->showAlert("Failed", 800);
        }
        return true;

#if defined(HAS_MICROPHONE)
      case VOICE:
        if (_vState == V_IDLE) {
          if (voiceStartRecording()) {
            return true;
          } else {
            _task->showAlert("Mic fail", 800);
            return true;
          }
        } else if (_vState == V_RECORDING) {
          voiceStopRecording();
          return true;
        } else if (_vState == V_REVIEW) {
          voiceDiscard();
          return true;
        }
        return false;
#endif

#if ENV_INCLUDE_GPS == 1
      case GPS:
        _task->toggleGPS();
        return true;
#endif

      case COMPASS:
        if (!_compassOK) return false;
        _calMode = true;
        _calStart = millis();
        _calCount = 0;
        return true;

      case HIBERNATE:
        _shutdown_init = true;
        _shutdown_at = millis() + 500;
        return true;

      default:
        return false;
      }
    }

    return false;
  }

  void poll() override {
    if (_shutdown_init && millis() >= _shutdown_at) {
      if (!_task->isButtonPressed()) {
        _task->shutdown();
      }
    }

#if defined(HAS_MICROPHONE)
    // Stream-encode PDM samples during recording
    if (_vState == V_RECORDING) {
      // Periodic diagnostic — is PDM delivering data?
      static unsigned long lastDiag = 0;
      static uint32_t pollCount = 0;
      pollCount++;
      if (millis() - lastDiag > 500) {
        lastDiag = millis();
        Serial.printf("Voice poll #%lu: isr=%lu pdm=%d frames=%d\n",
                      (unsigned long)pollCount, (unsigned long)_vIsrCount,
                      _vPdmCount, _vEncFrames);
      }

      voiceProcessSamples();

      // Auto-stop at max duration
      if (_vEncFrames >= VC_MAX_FRAMES ||
          (millis() - _vRecStart) >= (unsigned long)(VC_MAX_SECONDS * 1000 + 200)) {
        voiceStopRecording();
      }
    }
#endif
  }
};

// Static PDM callback -- must be defined after class so onPDMData() is visible
#if defined(HAS_MICROPHONE)
static void _vcPdmISR() {
  if (_vcSelf) _vcSelf->onPDMData();
}
#endif