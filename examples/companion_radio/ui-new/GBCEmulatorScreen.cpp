// =============================================================================
// GBCEmulatorScreen.cpp -- Game Boy / Game Boy Color emulator for Meck
//
// Build 1 (T-Deck Max). See GBCEmulatorScreen.h for the user-facing summary.
//
// How it hangs together:
//
//   - The Peanut-GB core (peanut_gb.h, the same patched copy Meck-P4 ships)
//     runs in its own FreeRTOS task pinned to core 0, priority 2, 12 KB stack
//     from internal RAM (this Arduino core's config does not allow PSRAM task
//     stacks, unlike the P4 build). The task paces gb_run_frame() to the GBC's
//     59.73 Hz with an accumulated microsecond deadline and vTaskDelay(); the
//     tick here is 1 ms, so plain delays are accurate enough.
//
//   - frame_skip is switched on in the core: it draws every second frame and
//     skips all tile/sprite composition on the others. The e-ink shows about
//     one frame a second, so nothing visible is lost and the per-frame cost
//     drops.
//
//   - The core hands each drawn scanline to draw_line as 160 palette indices;
//     they are resolved through the core's own fixPalette into a 160x144
//     RGB555 buffer in PSRAM (the same work the speed probe measured).
//
//   - The UI task never reads that buffer. When render() wants a picture it
//     raises s_snap_req; the emulator task then converts the current frame to
//     luminance, thresholds it, packs it 8 pixels per byte into a work buffer,
//     and copies the 2880-byte result into the shared snapshot buffer inside a
//     short critical section. render() copies the snapshot out under the same
//     lock and draws it with GxEPDDisplay::drawXbmRaw, whose CRC tracking means
//     the panel only refreshes when the picture actually changed.
//
//   - Input: the Max keyboard driver's raw joypad mode. While on, every key
//     press and release updates a held-key bitmask (bit layout matches
//     Peanut-GB direct.joypad: a 0x01, b 0x02, select 0x04, start 0x08,
//     right 0x10, left 0x20, up 0x40, down 0x80) and nothing reaches the
//     normal key path, so no keystroke leaks into the UI mid-game. The
//     emulator task samples the mask once per frame. Shift+Backspace is a
//     press-edge exit latch the screen polls. The keyboard is normally read
//     from loop(), which spends ~650 ms blocked inside every e-ink refresh;
//     while a game runs a GxEPD2 busy callback keeps draining the keyboard
//     through the refresh, so input never waits for the panel.
//
//   - While a ROM runs the LoRa radio is put into standby and the mesh loop
//     paused, reusing the OTA update's otaPauseRadio()/otaResumeRadio(). That
//     keeps the shared SPI bus (LoRa, e-ink, SD) free for the panel and the
//     save file, and keeps the mesh loop out of the frame budget. The node
//     receives nothing while a game is running.
//
//   - Saves: raw cart RAM as a .sav sidecar next to the ROM, size from the
//     cartridge header (MBC2 carts declare 0 but carry 512 bytes). Loaded
//     after gb_init, written once on quit after the task has stopped
//     (flush-on-exit, the same decision as Meck-P4).
//
//   - Memory: core context (49952 bytes), cart RAM (128 KB), ROM arena
//     (reserved at 2 MB on first launch and retained for the life of the
//     boot, so a big game late in a long session never has to find a fresh
//     contiguous block), RGB555 frame and the two mono buffers all live in
//     PSRAM. Only the task stack is internal.
//
//   - No sound yet (build 3).
//
//   - Telemetry: the task prints a speed line every 5 s (frames in the
//     window, fps, and the cumulative average against 59.73) so whether the
//     core keeps up is read from the log, not guessed.
// =============================================================================

#if defined(LilyGo_TDeck_Pro_Max)

#include "GBCEmulatorScreen.h"
#include "UITask.h"
#include <helpers/ui/GxEPDDisplay.h>
#include "TCA8418Keyboard.h"

#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>

#define PEANUT_FULL_GBC_SUPPORT 1
#define ENABLE_LCD 1
#define ENABLE_SOUND 0
#include "peanut_gb.h"

// ---- Firmware hooks ---------------------------------------------------------
extern TCA8418Keyboard keyboard;          // main.cpp
#ifdef MECK_OTA_UPDATE
extern void otaPauseRadio();              // main.cpp
extern void otaResumeRadio();             // main.cpp
#endif

// ---- Geometry and constants -------------------------------------------------
#define GB_W            160
#define GB_H            144
#define GB_MONO_STRIDE  (GB_W / 8)                    // 20 bytes per row
#define GB_MONO_BYTES   (GB_MONO_STRIDE * GB_H)       // 2880

#define GBC_ROM_DIR     "/roms"
#define GBC_CRAM_SIZE   0x20000                       // 128 KB, largest standard bank set
#define GBC_ROM_RESERVE 0x200000                      // 2 MB arena on first launch
#define GBC_FRAME_US    16742                         // 59.73 Hz
#define GBC_TASK_STACK  12288                         // bytes, internal RAM
#define GBC_TASK_PRIO   2
#define GBC_TASK_CORE   0

// Panel placement (physical 240x320 portrait pixels).
#define GBC_IMG_X       40
#define GBC_IMG_Y       88

// ---- Module state (single emulator instance) -------------------------------
static struct gb_s     *s_gb        = NULL;   // PSRAM
static uint8_t         *s_rom       = NULL;   // PSRAM arena, retained
static size_t           s_rom_cap   = 0;
static size_t           s_rom_size  = 0;
static uint8_t         *s_cram      = NULL;   // PSRAM
static uint16_t        *s_fb        = NULL;   // PSRAM, GB_W*GB_H RGB555
static uint8_t         *s_mono_work = NULL;   // PSRAM, emulator task only
static uint8_t         *s_mono_show = NULL;   // PSRAM, shared under s_mux
static uint8_t         *s_mono_ui   = NULL;   // PSRAM, UI task only

static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool    s_snap_req     = false;
static volatile bool    s_stop         = false;
static volatile bool    s_task_stopped = false;
static volatile bool    s_core_error   = false;
static volatile unsigned long s_frames = 0;
static TaskHandle_t     s_task = NULL;

static size_t           s_save_size = 0;
static char             s_save_path[96];

// ---- Core callbacks ---------------------------------------------------------
static uint8_t rom_read(struct gb_s *gb, const uint_fast32_t addr) {
  (void)gb;
  return s_rom[addr];
}

static uint8_t cram_read(struct gb_s *gb, const uint_fast32_t addr) {
  (void)gb;
  return s_cram[addr];
}

static void cram_write(struct gb_s *gb, const uint_fast32_t addr, const uint8_t val) {
  (void)gb;
  s_cram[addr] = val;
}

static void gb_error_cb(struct gb_s *gb, const enum gb_error_e err, const uint16_t addr) {
  (void)gb;
  // Core fault mid-run. Flag the task to stop; the screen's poll() unwinds.
  Serial.printf("[GBC] core error %d at 0x%04X -- stopping\n", (int)err, (unsigned)addr);
  s_core_error = true;
  s_stop = true;
}

static void draw_line(struct gb_s *gb, const uint8_t *pixels, const uint_fast8_t line) {
  uint16_t *dst = &s_fb[(size_t)line * GB_W];
  for (int x = 0; x < GB_W; x++) dst[x] = gb->cgb.fixPalette[pixels[x]];
}

// ---- Black-and-white conversion (emulator task) -----------------------------
// RGB555 with red in the high bits. Integer luminance weights 77/151/28 over
// 256 give 0..31; threshold at the midpoint; bit 7 is the leftmost pixel,
// matching the MSB-first order drawXbmRaw expects. A set bit is a dark pixel.
static void mono_convert(uint8_t *dst_buf) {
  for (int y = 0; y < GB_H; y++) {
    const uint16_t *src = &s_fb[(size_t)y * GB_W];
    uint8_t        *dst = &dst_buf[(size_t)y * GB_MONO_STRIDE];
    for (int bx = 0; bx < GB_MONO_STRIDE; bx++) {
      uint8_t byte = 0;
      for (int b = 0; b < 8; b++) {
        const uint16_t v  = src[bx * 8 + b];
        const uint32_t r  = (v >> 10) & 0x1F;
        const uint32_t g  = (v >> 5)  & 0x1F;
        const uint32_t bl =  v        & 0x1F;
        const uint32_t lum = (r * 77 + g * 151 + bl * 28) >> 8;
        if (lum < 16) byte |= (uint8_t)(0x80 >> b);
      }
      dst[bx] = byte;
    }
  }
}

// ---- Keyboard poll during e-ink refresh -------------------------------------
// GxEPD2 calls this in place of its 1 ms sleep while the panel's busy line is
// high, on the UI task, so the same task that normally reads the keyboard
// keeps reading it through the ~650 ms refresh. In raw joypad mode readKey()
// updates the held-key mask and returns 0, so nothing is lost or misrouted.
static void gbc_busy_poll(const void *arg) {
  (void)arg;
  keyboard.readKey();
  delay(1);
}

// ---- Emulator task ----------------------------------------------------------
static void gbc_task(void *arg) {
  (void)arg;
  Serial.printf("[GBC] task start on core %d\n", (int)xPortGetCoreID());
  const int64_t t_start = esp_timer_get_time();
  int64_t  next  = t_start;
  int64_t  t_win = t_start;
  unsigned long f_win = 0;
  unsigned frames_since_delay = 0;

  while (!s_stop) {
    // Peanut-GB's joypad register is active-low.
    s_gb->direct.joypad = (uint8_t)~keyboard.rawJoypad();
    gb_run_frame(s_gb);
    s_frames = s_frames + 1;
    f_win++;
    {
      const int64_t t = esp_timer_get_time();
      if (t - t_win >= 5000000) {
        const float win_fps = (float)f_win * 1000000.0f / (float)(t - t_win);
        const float avg_fps = (float)s_frames * 1000000.0f / (float)(t - t_start);
        Serial.printf("[GBC] speed: %.1f fps last 5 s, %.1f fps average (%.2fx real-time)\n",
                      win_fps, avg_fps, avg_fps / 59.73f);
        t_win = t;
        f_win = 0;
      }
    }

    if (s_snap_req) {
      mono_convert(s_mono_work);
      taskENTER_CRITICAL(&s_mux);
      memcpy(s_mono_show, s_mono_work, GB_MONO_BYTES);
      taskEXIT_CRITICAL(&s_mux);
      s_snap_req = false;
    }

    // Pace to the accumulated deadline. Rounding the wait down to whole
    // milliseconds runs a frame slightly early; the deadline keeps advancing
    // by exactly one frame, so the average rate is exact. If we fall behind,
    // resync rather than spiral. Either way the idle task on this core must
    // get a slice now and then or the task watchdog (5 s) reboots the board:
    // after 30 frames without a delay, give it 1 ms.
    next += GBC_FRAME_US;
    const int64_t now = esp_timer_get_time();
    if (next > now) {
      const int64_t rem_ms = (next - now) / 1000;
      if (rem_ms >= 1) {
        vTaskDelay(pdMS_TO_TICKS(rem_ms));
        frames_since_delay = 0;
        continue;
      }
    } else {
      next = now;
    }
    if (++frames_since_delay >= 30) {
      vTaskDelay(1);
      frames_since_delay = 0;
    }
  }

  Serial.printf("[GBC] task stop: %lu frames, stack high-water mark %u bytes free of %u\n",
                (unsigned long)s_frames,
                (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)),
                (unsigned)GBC_TASK_STACK);
  s_task_stopped = true;
  vTaskDelete(NULL);
}

// ---- Buffers ----------------------------------------------------------------
static bool gbc_alloc_buffers() {
  if (!s_gb)        s_gb        = (struct gb_s*)heap_caps_malloc(sizeof(struct gb_s), MALLOC_CAP_SPIRAM);
  if (!s_cram)      s_cram      = (uint8_t*)heap_caps_malloc(GBC_CRAM_SIZE, MALLOC_CAP_SPIRAM);
  if (!s_fb)        s_fb        = (uint16_t*)heap_caps_malloc((size_t)GB_W * GB_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!s_mono_work) s_mono_work = (uint8_t*)heap_caps_malloc(GB_MONO_BYTES, MALLOC_CAP_SPIRAM);
  if (!s_mono_show) s_mono_show = (uint8_t*)heap_caps_malloc(GB_MONO_BYTES, MALLOC_CAP_SPIRAM);
  if (!s_mono_ui)   s_mono_ui   = (uint8_t*)heap_caps_malloc(GB_MONO_BYTES, MALLOC_CAP_SPIRAM);
  if (!s_gb)        Serial.println("[GBC] gb_s alloc failed");
  if (!s_cram)      Serial.println("[GBC] cram alloc failed");
  if (!s_fb)        Serial.println("[GBC] fb alloc failed");
  if (!s_mono_work) Serial.println("[GBC] mono work alloc failed");
  if (!s_mono_show) Serial.println("[GBC] mono show alloc failed");
  if (!s_mono_ui)   Serial.println("[GBC] mono ui alloc failed");
  return s_gb && s_cram && s_fb && s_mono_work && s_mono_show && s_mono_ui;
}

static void print_heaps(const char *when) {
  Serial.printf("[GBC] heap %s: internal free %u (largest %u), PSRAM free %u (largest %u)\n",
                when,
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}

// =============================================================================
// GBCEmulatorScreen
// =============================================================================
GBCEmulatorScreen::GBCEmulatorScreen(UITask* task)
  : _task(task), _wantsExit(false), _mode(BROWSER), _cursor(0), _scroll(0),
    _romCount(0), _playing(-1), _statusUntil(0), _eink(NULL), _busyHooked(false) {
  memset(_romNames, 0, sizeof(_romNames));
  _status[0] = 0;
}

void GBCEmulatorScreen::enter() {
  _wantsExit = false;
  if (_mode == BROWSER) scanRoms();
}

void GBCEmulatorScreen::setStatus(const char* msg) {
  snprintf(_status, sizeof(_status), "%s", msg);
  _statusUntil = millis() + 3000;
}

// List .gb/.gbc files in /roms (no subfolders). Skips the "._Name" metadata
// files macOS leaves on FAT cards.
void GBCEmulatorScreen::scanRoms() {
  _romCount = 0;
  File dir = SD.open(GBC_ROM_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    Serial.println("[GBC] " GBC_ROM_DIR " not found on SD");
    if (_cursor > 0) _cursor = 0;
    _scroll = 0;
    return;
  }
  File f = dir.openNextFile();
  while (f && _romCount < GBC_MAX_ROMS) {
    if (!f.isDirectory()) {
      const char* n = f.name();
      const size_t len = strlen(n);
      bool ok = len < GBC_NAME_MAX && !(n[0] == '.' && n[1] == '_');
      if (ok) {
        bool is_rom = false;
        if (len >= 5 && strcasecmp(n + len - 4, ".gbc") == 0) is_rom = true;
        else if (len >= 4 && strcasecmp(n + len - 3, ".gb") == 0) is_rom = true;
        ok = is_rom;
      }
      if (ok) {
        snprintf(_romNames[_romCount], GBC_NAME_MAX, "%s", n);
        _romCount++;
      }
    }
    f.close();
    f = dir.openNextFile();
  }
  if (f) f.close();
  dir.close();
  if (_cursor >= _romCount) _cursor = (_romCount > 0) ? _romCount - 1 : 0;
  if (_scroll > _cursor) _scroll = _cursor;
  Serial.printf("[GBC] %d ROM(s) in " GBC_ROM_DIR "\n", _romCount);
}

// ---- Launch (UI task) -------------------------------------------------------
bool GBCEmulatorScreen::launch(int idx) {
  if (idx < 0 || idx >= _romCount) return false;
  char path[80];
  snprintf(path, sizeof(path), GBC_ROM_DIR "/%s", _romNames[idx]);
  Serial.printf("[GBC] launch %s\n", path);
  print_heaps("before launch");

  if (!gbc_alloc_buffers()) {
    setStatus("Out of memory");
    return false;
  }

  // ---- ROM into the PSRAM arena ----
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[GBC] cannot open %s\n", path);
    setStatus("Cannot open ROM");
    return false;
  }
  const size_t sz = (size_t)f.size();
  if (sz < 0x8000) {
    f.close();
    Serial.println("[GBC] file too small to be a ROM");
    setStatus("Not a ROM");
    return false;
  }
  if (s_rom && s_rom_cap < sz) {
    heap_caps_free(s_rom);
    s_rom = NULL;
    s_rom_cap = 0;
  }
  if (!s_rom) {
    size_t want = (sz > GBC_ROM_RESERVE) ? sz : GBC_ROM_RESERVE;
    s_rom = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM);
    if (!s_rom && want > sz) {
      want = sz;
      s_rom = (uint8_t*)heap_caps_malloc(want, MALLOC_CAP_SPIRAM);
    }
    if (!s_rom) {
      f.close();
      Serial.printf("[GBC] ROM alloc failed (%u bytes; largest free PSRAM block %u)\n",
                    (unsigned)sz, (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
      setStatus("Out of PSRAM");
      return false;
    }
    s_rom_cap = want;
    Serial.printf("[GBC] ROM arena reserved: %u bytes\n", (unsigned)want);
  }
  const size_t got = f.read(s_rom, sz);
  f.close();
  if (got != sz) {
    Serial.printf("[GBC] short read (%u/%u)\n", (unsigned)got, (unsigned)sz);
    setStatus("ROM read failed");
    return false;
  }
  s_rom_size = sz;
  memset(s_cram, 0, GBC_CRAM_SIZE);

  // ---- Save file path and size from the cartridge header ----
  {
    static const size_t ram_sz[6] = { 0, 2048, 8192, 32768, 131072, 65536 };
    const uint8_t code = s_rom[0x149];
    s_save_size = (code < 6) ? ram_sz[code] : 0;
    if (s_save_size > GBC_CRAM_SIZE) s_save_size = GBC_CRAM_SIZE;
    snprintf(s_save_path, sizeof(s_save_path), "%s", path);
    char *dot = strrchr(s_save_path, '.');
    if (dot) snprintf(dot, sizeof(s_save_path) - (dot - s_save_path), ".sav");
    else     s_save_size = 0;
  }

  // ---- Core ----
  const enum gb_init_error_e e = gb_init(s_gb, rom_read, cram_read, cram_write, gb_error_cb, NULL);
  if (e != GB_INIT_NO_ERROR) {
    Serial.printf("[GBC] gb_init failed: %d\n", (int)e);
    s_rom_size = 0;
    setStatus("Unsupported ROM");
    return false;
  }
  if (s_gb->mbc == 2 && s_save_size == 0) s_save_size = 512;
  if (s_save_size > 0) {
    File sf = SD.open(s_save_path, FILE_READ);
    if (sf) {
      const size_t got_sv = sf.read(s_cram, s_save_size);
      sf.close();
      Serial.printf("[GBC] save loaded: %u bytes from %s\n", (unsigned)got_sv, s_save_path);
    } else {
      Serial.println("[GBC] no save file (fresh start)");
    }
  }
  gb_init_lcd(s_gb, draw_line);
  s_gb->direct.frame_skip = 1;
  memset(s_fb, 0, (size_t)GB_W * GB_H * sizeof(uint16_t));
  memset(s_mono_show, 0, GB_MONO_BYTES);
  Serial.printf("[GBC] cgbMode=%d mbc=%d rom=%u bytes save=%u bytes\n",
                (int)s_gb->cgb.cgbMode, (int)s_gb->mbc, (unsigned)s_rom_size, (unsigned)s_save_size);

  // ---- Hand the keyboard and the radio over, then start the task ----
  keyboard.setRawJoypad(true);
#ifdef MECK_OTA_UPDATE
  otaPauseRadio();
#endif
  s_stop = false;
  s_task_stopped = false;
  s_core_error = false;
  s_frames = 0;
  s_snap_req = true;
  print_heaps("before task create");
  if (xTaskCreatePinnedToCore(gbc_task, "meck_gbc", GBC_TASK_STACK, NULL,
                              GBC_TASK_PRIO, &s_task, GBC_TASK_CORE) != pdPASS) {
    Serial.println("[GBC] task create failed");
    s_task = NULL;
    keyboard.setRawJoypad(false);
#ifdef MECK_OTA_UPDATE
    otaResumeRadio();
#endif
    setStatus("Task create failed");
    return false;
  }
  _playing = idx;
  _mode = PLAYING;
  return true;
}

// ---- Stop (UI task, after the emulator task has parked) --------------------
void GBCEmulatorScreen::finishStop() {
  s_task = NULL;
  bool saved = false;
  if (s_save_size > 0) {
    if (SD.exists(s_save_path)) SD.remove(s_save_path);
    File sf = SD.open(s_save_path, FILE_WRITE);
    if (sf) {
      const size_t put = sf.write(s_cram, s_save_size);
      sf.close();
      saved = (put == s_save_size);
      Serial.printf("[GBC] save written: %u bytes to %s\n", (unsigned)put, s_save_path);
    } else {
      Serial.printf("[GBC] save write FAILED: cannot open %s\n", s_save_path);
    }
  }
  if (_busyHooked && _eink) {
    _eink->setBusyCallback(NULL, 0);
    _busyHooked = false;
  }
  keyboard.setRawJoypad(false);
#ifdef MECK_OTA_UPDATE
  otaResumeRadio();
#endif
  s_rom_size = 0;                 // arena retained
  _playing = -1;
  _mode = BROWSER;
  if (s_core_error)  setStatus("Game crashed");
  else if (saved)    setStatus("Saved");
  print_heaps("after stop");
  _task->forceRefresh();
}

// ---- Per-loop poll (UI task) ------------------------------------------------
void GBCEmulatorScreen::poll() {
  if (_mode == PLAYING) {
    if (keyboard.rawExitPressed() || s_stop) {
      s_stop = true;
      _mode = STOPPING;
      Serial.println("[GBC] stop requested");
    }
  }
  if (_mode == STOPPING && s_task_stopped) {
    finishStop();
  }
  // In-game input never passes through injectKey(), so the auto-lock idle
  // timer would otherwise expire mid-game and lock the screen over the top
  // of a running emulator.
  if (_mode != BROWSER) _task->resetIdleTimer();
}

// ---- Input (browser only; raw mode swallows keys while playing) -------------
bool GBCEmulatorScreen::handleInput(char c) {
  if (_mode != BROWSER) return false;
  switch (c) {
    case 'w': case 'W':
      if (_cursor > 0) _cursor--;
      if (_cursor < _scroll) _scroll = _cursor;
      return true;
    case 's': case 'S':
      if (_cursor < _romCount - 1) _cursor++;
      return true;
    case '\r':
      if (_romCount == 0) { setStatus("No ROMs in /roms"); return true; }
      launch(_cursor);
      return true;
    case KEY_CANCEL:
      _wantsExit = true;
      return true;
    default:
      return false;
  }
}

// ---- Render -----------------------------------------------------------------
int GBCEmulatorScreen::render(DisplayDriver& display) {
  if (_mode == BROWSER) return renderBrowser(display);
  return renderGame(display);
}

int GBCEmulatorScreen::renderBrowser(DisplayDriver& display) {
  display.startFrame();
  display.setTextSize(1);

  display.setColor(DisplayDriver::GREEN);
  display.setCursor(2, 2);
  display.print("Game Boy");
  display.setColor(DisplayDriver::LIGHT);
  display.drawRect(0, 12, display.width(), 1);

  const int y0 = 18;
  const int lineH = 16;
  const int rows = (display.height() - y0 - 14) / lineH;
  if (_cursor >= _scroll + rows) _scroll = _cursor - rows + 1;
  if (_scroll < 0) _scroll = 0;

  if (_romCount == 0) {
    display.setColor(DisplayDriver::LIGHT);
    display.setCursor(6, y0 + 2);
    display.print("No .gb/.gbc files");
    display.setCursor(6, y0 + 14);
    display.print("in /roms on SD");
  } else {
    int y = y0;
    for (int i = _scroll; i < _romCount && i < _scroll + rows; i++) {
      if (i == _cursor) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y - 1, display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.drawTextEllipsized(6, y + 2, display.width() - 12, _romNames[i]);
      y += lineH;
    }
  }

  display.setColor(DisplayDriver::LIGHT);
  const int fy = display.height() - 12;
  display.drawRect(0, fy - 2, display.width(), 1);
  display.setCursor(2, fy);
  if (_statusUntil != 0 && millis() < _statusUntil) {
    display.print(_status);
    return 500;
  }
  display.print("Enter:Play  Sh+Del:Back");
  return 5000;
}

int GBCEmulatorScreen::renderGame(DisplayDriver& display) {
  GxEPDDisplay& d = static_cast<GxEPDDisplay&>(display);
  _eink = &d;
  if (!_busyHooked && _mode == PLAYING) {
    d.setBusyCallback(gbc_busy_poll, 0);
    _busyHooked = true;
  }
  display.startFrame();

  // Ask the emulator task for a fresh picture for next time, and draw the
  // one it prepared last time.
  s_snap_req = true;
  taskENTER_CRITICAL(&s_mux);
  memcpy(s_mono_ui, s_mono_show, GB_MONO_BYTES);
  taskEXIT_CRITICAL(&s_mux);

  const uint16_t fg = d.rawFgColor();
  d.drawXbmRaw(GBC_IMG_X, GBC_IMG_Y, s_mono_ui, GB_W, GB_H, fg);
  d.drawTextRaw(2, 4, (_playing >= 0) ? _romNames[_playing] : "", fg);
  d.drawTextRaw(2, 308, (_mode == STOPPING) ? "Saving..." : "Sh+Del: Quit", fg);
  return 100;   // the UI loop's 800 ms e-ink floor sets the real cadence
}

#endif // LilyGo_TDeck_Pro_Max