// =============================================================================
// GBCEmulatorScreen.cpp -- Game Boy / Game Boy Color emulator for Meck
//
// T-Deck Pro and Max. See GBCEmulatorScreen.h for the user-facing summary.
//
// How it hangs together:
//
//   - The Peanut-GB core (peanut_gb.h, the same patched copy Meck-P4 ships)
//     runs in its own FreeRTOS task pinned to core 0, priority 2, on an 8 KB
//     stack that is a static array reserved at boot. It has to be internal
//     RAM (this Arduino core's config does not allow PSRAM task stacks, unlike
//     the P4 build), and it is static rather than allocated at launch because
//     once Bluetooth has been switched on in a boot its stack stays resident
//     and can leave under 8 KB of internal RAM in one piece; a stack claimed
//     before that happens is never at its mercy. The measured high-water
//     mark is about 2 KB. The task paces gb_run_frame() to the GBC's
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
//     raises s_snap_req; the emulator task then scales the current frame to
//     1.5x (240x216, the full panel width, nearest neighbour), converts it to
//     luminance, dithers it to black and white with an ordered 4x4 Bayer
//     pattern (about 17 tones; the pattern is fixed to panel position so it
//     does not crawl between refreshes), packs it 8 pixels per byte into a
//     work buffer,
//     and copies the 6480-byte result into the shared snapshot buffer inside a
//     short critical section. render() copies the snapshot out under the same
//     lock and draws it with GxEPDDisplay::drawXbmRaw, whose CRC tracking means
//     the panel only refreshes when the picture actually changed. The first
//     picture after launch is a normal full-screen partial refresh (it has
//     to clear the ROM list); every picture after that refreshes only the
//     216-line game window, so the panel moves less ink per picture. When a game
//     quits the next refresh is a full one (the black/white flash) so the
//     residue partial refreshes leave behind does not follow the user back
//     to the ROM list and home screen. There is no periodic full refresh
//     during play: it was tried and the flash was too distracting.
//
//   - Input: the Max keyboard driver's raw joypad mode. While on, every key
//     press and release updates a held-key bitmask (bit layout matches
//     Peanut-GB direct.joypad: a 0x01, b 0x02, select 0x04, start 0x08,
//     right 0x10, left 0x20, up 0x40, down 0x80) and nothing reaches the
//     normal key path, so no keystroke leaks into the UI mid-game. The
//     emulator task samples the mask once per frame. Q or Shift+Backspace is
//     a press-edge exit latch the screen polls. On the Max the both-shifts
//     keyboard backlight chord is the one key raw mode lets through; it is
//     acted on via main.cpp's toggleKeyboardBacklight(). The keyboard is normally read
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
//     after gb_init. Written in the background: every cart-RAM write by
//     the game marks the save dirty, and poll() (UI task, so it owns the
//     SD card) writes the file once the game has left its cart RAM alone
//     for GBC_SAVE_QUIET_MS -- in practice a couple of seconds after the
//     user saves in-game. Quit writes only if something is still dirty,
//     so quitting is normally instant. (Build 9 wrote only on quit.)
//
//   - Memory: cart RAM (128 KB), ROM arena (reserved at 2 MB on first launch
//     and retained for the life of the boot, so a big game late in a long
//     session never has to find a fresh contiguous block), RGB555 frame and
//     the three mono buffers live in PSRAM. The core context (49952 bytes),
//     which the core touches on every instruction, is allocated from
//     internal RAM for the duration of a game and freed on quit, and also
//     freed on every failed launch so a failure never holds memory; if no
//     internal block is available it goes to PSRAM instead and the launch
//     log says so.
//
//   - No sound yet (build 3).
//
//   - Telemetry: the task prints a speed line every 5 s (frames in the
//     window, fps, and the cumulative average against 59.73) so whether the
//     core keeps up is read from the log, not guessed.
// =============================================================================

#if defined(LilyGo_TDeck_Pro)

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
#if defined(LilyGo_TDeck_Pro_Max)
extern void toggleKeyboardBacklight();    // main.cpp (both-shifts chord, Max keyboard backlight)
#endif
#ifdef MECK_OTA_UPDATE
extern void otaPauseRadio();              // main.cpp
extern void otaResumeRadio();             // main.cpp
#endif

// ---- Geometry and constants -------------------------------------------------
#define GB_W            160
#define GB_H            144
// Output picture: 1.5x nearest-neighbour scale, the full 240 px panel width.
#define OUT_W           240
#define OUT_H           216
#define OUT_MONO_STRIDE (OUT_W / 8)                   // 30 bytes per row
#define OUT_MONO_BYTES  (OUT_MONO_STRIDE * OUT_H)     // 6480

#define GBC_ROM_DIR     "/roms"
#define GBC_CRAM_SIZE   0x20000                       // 128 KB, largest standard bank set
#define GBC_ROM_RESERVE 0x200000                      // 2 MB arena on first launch
#define GBC_FRAME_US    16742                         // 59.73 Hz
#define GBC_MAX_DEFICIT_US 250000                     // behind by more than this = stall, resync
#define GBC_SAVE_QUIET_MS  2000                       // cart RAM untouched this long -> write .sav
#define GBC_STOP_WAIT_MS   200                        // max wait for the task to park on quit
#define GBC_TASK_STACK  8192                          // bytes, static, reserved at boot
#define GBC_TASK_PRIO   2
#define GBC_TASK_CORE   0

// Panel placement (physical 240x320 portrait pixels): full width, centred in
// the space above the quit hint.
#define GBC_IMG_X       0
#define GBC_IMG_Y       42

// ---- Module state (single emulator instance) -------------------------------
static struct gb_s     *s_gb        = NULL;   // internal RAM per game, PSRAM if that fails
static bool             s_gb_internal = false;
static uint8_t         *s_rom       = NULL;   // PSRAM arena, retained
static size_t           s_rom_cap   = 0;
static size_t           s_rom_size  = 0;
static uint8_t         *s_cram      = NULL;   // PSRAM
static uint16_t        *s_fb        = NULL;   // PSRAM, GB_W*GB_H RGB555
static uint8_t         *s_mono_work = NULL;   // PSRAM, emulator task only (OUT_MONO_BYTES)
static uint8_t         *s_mono_show = NULL;   // PSRAM, shared under s_mux
static uint8_t         *s_mono_ui   = NULL;   // PSRAM, UI task only

static portMUX_TYPE     s_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool    s_snap_req     = false;
static volatile bool    s_stop         = false;
static volatile bool    s_task_stopped = false;
static volatile bool    s_core_error   = false;
#if defined(LilyGo_TDeck_Pro_Max)
static volatile bool    s_kbd_bl_req   = false;   // both-shifts chord seen by the busy poll
#endif
static volatile unsigned long s_frames = 0;
static unsigned         s_pictures = 0;       // pictures drawn this game (first one is full-screen)
static TaskHandle_t     s_task = NULL;
static StackType_t      s_task_stack[GBC_TASK_STACK / sizeof(StackType_t)];   // .bss, internal RAM
static StaticTask_t     s_task_tcb;

static size_t           s_save_size = 0;
static volatile bool    s_cram_dirty = false;    // set by the game's cart-RAM writes (emulator task)
static volatile uint32_t s_cram_dirty_ms = 0;   // millis() of the last such write
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
  s_cram_dirty = true;
  s_cram_dirty_ms = (uint32_t)millis();
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

// ---- Scale, dither and pack (emulator task) ---------------------------------
// 160x144 -> 240x216 by nearest neighbour (every output pixel maps to source
// pixel x*2/3, so source pixels alternate between one and two output pixels).
// RGB555 with red in the high bits. Integer luminance weights 77/151/28 give
// 0..248 on a 0..255 scale. Each panel pixel is then compared against the
// 4x4 Bayer threshold for its screen position: a flat mid-grey area comes
// out as a regular pattern of 7 dark pixels in 16, pure white stays white,
// pure black stays black, and the game's four-shade palette ramps map to
// distinguishable stipples. Bit 7 is the leftmost pixel, matching the
// MSB-first order drawXbmRaw expects. A set bit is a dark pixel.
static const uint8_t s_bayer4[4][4] = {
  {  0,  8,  2, 10 },
  { 12,  4, 14,  6 },
  {  3, 11,  1,  9 },
  { 15,  7, 13,  5 },
};

static void mono_convert(uint8_t *dst_buf) {
  for (int y = 0; y < OUT_H; y++) {
    const uint16_t *src = &s_fb[(size_t)((y * 2) / 3) * GB_W];
    uint8_t        *dst = &dst_buf[(size_t)y * OUT_MONO_STRIDE];
    const uint8_t  *brow = s_bayer4[y & 3];
    for (int bx = 0; bx < OUT_MONO_STRIDE; bx++) {
      uint8_t byte = 0;
      for (int b = 0; b < 8; b++) {
        const int      x  = bx * 8 + b;
        const uint16_t v  = src[(x * 2) / 3];
        const uint32_t r  = (v >> 10) & 0x1F;
        const uint32_t g  = (v >> 5)  & 0x1F;
        const uint32_t bl =  v        & 0x1F;
        const uint32_t lum = (r * 77 + g * 151 + bl * 28) >> 5;   // 0..248
        const uint32_t thr = (uint32_t)brow[x & 3] * 16 + 8;      // 8..248
        if (lum < thr) byte |= (uint8_t)(0x80 >> b);
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
  // Raw mode returns 0 for everything except (on the Max) the both-shifts
  // backlight chord. Latch that here and act on it from poll(), outside the
  // panel's busy wait.
#if defined(LilyGo_TDeck_Pro_Max)
  if (keyboard.readKey() == KB_KEY_KBD_BACKLIGHT) s_kbd_bl_req = true;
#else
  keyboard.readKey();
#endif
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
      memcpy(s_mono_show, s_mono_work, OUT_MONO_BYTES);
      taskEXIT_CRITICAL(&s_mux);
      s_snap_req = false;
    }

    // Pace to the accumulated deadline. Rounding the wait down to whole
    // milliseconds runs a frame slightly early; the deadline keeps advancing
    // by exactly one frame, so the average rate is exact. If we are behind
    // (the BLE stack on this core preempted us and a frame finished late),
    // KEEP the deficit and run frames back-to-back until it is paid off --
    // the bench says the core has the headroom. Build 2 reset the deadline
    // to "now" on any lateness, forgiving the lost time each time, and ran
    // at 0.96x. Only a real stall (behind by more than GBC_MAX_DEFICIT_US)
    // resyncs. Either way the idle task on this core must get a slice now
    // and then or the task watchdog (5 s) reboots the board: after 30
    // frames without a delay, give it 1 ms.
    next += GBC_FRAME_US;
    const int64_t now = esp_timer_get_time();
    if (next > now) {
      const int64_t rem_ms = (next - now) / 1000;
      if (rem_ms >= 1) {
        vTaskDelay(pdMS_TO_TICKS(rem_ms));
        frames_since_delay = 0;
        continue;
      }
    } else if (now - next > GBC_MAX_DEFICIT_US) {
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
// Give the core context back. Called on quit and on every failed launch, so
// a failure never leaves 50 KB (usually internal RAM) held.
static void gbc_release_core() {
  if (s_gb) { heap_caps_free(s_gb); s_gb = NULL; s_gb_internal = false; }
}

static bool gbc_alloc_buffers() {
  // Core context: internal RAM for this game if a block is free, else PSRAM.
  if (!s_gb) {
    s_gb = (struct gb_s*)heap_caps_malloc(sizeof(struct gb_s), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    s_gb_internal = (s_gb != NULL);
    if (!s_gb) s_gb = (struct gb_s*)heap_caps_malloc(sizeof(struct gb_s), MALLOC_CAP_SPIRAM);
    if (s_gb) Serial.printf("[GBC] core context (%u bytes) in %s\n", (unsigned)sizeof(struct gb_s),
                            s_gb_internal ? "internal RAM" : "PSRAM (no internal block free)");
  }
  if (!s_cram)      s_cram      = (uint8_t*)heap_caps_malloc(GBC_CRAM_SIZE, MALLOC_CAP_SPIRAM);
  if (!s_fb)        s_fb        = (uint16_t*)heap_caps_malloc((size_t)GB_W * GB_H * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
  if (!s_mono_work) s_mono_work = (uint8_t*)heap_caps_malloc(OUT_MONO_BYTES, MALLOC_CAP_SPIRAM);
  if (!s_mono_show) s_mono_show = (uint8_t*)heap_caps_malloc(OUT_MONO_BYTES, MALLOC_CAP_SPIRAM);
  if (!s_mono_ui)   s_mono_ui   = (uint8_t*)heap_caps_malloc(OUT_MONO_BYTES, MALLOC_CAP_SPIRAM);
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

// Write cart RAM to the .sav file. UI task only (the SD card is the UI
// task's). Returns true if the whole file was written.
static bool gbc_write_save(void) {
  if (s_save_size == 0) return false;
  if (SD.exists(s_save_path)) SD.remove(s_save_path);
  File sf = SD.open(s_save_path, FILE_WRITE);
  if (!sf) {
    Serial.printf("[GBC] save write FAILED: cannot open %s\n", s_save_path);
    return false;
  }
  const size_t put = sf.write(s_cram, s_save_size);
  sf.close();
  Serial.printf("[GBC] save written: %u bytes to %s\n", (unsigned)put, s_save_path);
  return put == s_save_size;
}

// =============================================================================
// GBCEmulatorScreen
// =============================================================================
GBCEmulatorScreen::GBCEmulatorScreen(UITask* task)
  : _task(task), _wantsExit(false), _mode(BROWSER), _cursor(0), _scroll(0),
    _romCount(0), _playing(-1), _statusUntil(0), _eink(NULL), _busyHooked(false),
    _releaseKbAfterDraw(false), _browserDrawn(false) {
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
    gbc_release_core();
    setStatus("Out of memory");
    return false;
  }

  // ---- ROM into the PSRAM arena ----
  File f = SD.open(path, FILE_READ);
  if (!f) {
    Serial.printf("[GBC] cannot open %s\n", path);
    gbc_release_core();
    setStatus("Cannot open ROM");
    return false;
  }
  const size_t sz = (size_t)f.size();
  if (sz < 0x8000) {
    f.close();
    Serial.println("[GBC] file too small to be a ROM");
    gbc_release_core();
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
      gbc_release_core();
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
    gbc_release_core();
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
    gbc_release_core();
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
  s_cram_dirty = false;      // the load above is not the game writing
  gb_init_lcd(s_gb, draw_line);
  s_gb->direct.frame_skip = 1;
  memset(s_fb, 0, (size_t)GB_W * GB_H * sizeof(uint16_t));
  memset(s_mono_show, 0, OUT_MONO_BYTES);
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
  s_pictures = 0;
  s_snap_req = true;
  print_heaps("before task create");
  s_task = xTaskCreateStaticPinnedToCore(gbc_task, "meck_gbc", GBC_TASK_STACK, NULL,
                                         GBC_TASK_PRIO, s_task_stack, &s_task_tcb,
                                         GBC_TASK_CORE);
  if (s_task == NULL) {
    Serial.println("[GBC] task create failed");
    keyboard.setRawJoypad(false);
#ifdef MECK_OTA_UPDATE
    otaResumeRadio();
#endif
    gbc_release_core();
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
  if (s_cram_dirty) {              // normally already written in the background
    s_cram_dirty = false;
    saved = gbc_write_save();
  }
  // The keyboard stays in raw joypad mode, and keeps being drained by the
  // busy poll, until the ROM list has actually been drawn (poll() releases
  // it). Without this, keys pressed during the ~1 s full refresh that
  // follows a quit queue up in the keyboard chip and are replayed as
  // navigation afterwards -- one impatient Q became four screens of "back".
  _releaseKbAfterDraw = true;
  _browserDrawn = false;
#ifdef MECK_OTA_UPDATE
  otaResumeRadio();
#endif
  s_rom_size = 0;                 // arena retained
  gbc_release_core();
  _playing = -1;
  _mode = BROWSER;
  if (s_core_error)  setStatus("Game crashed");
  else if (saved)    setStatus("Saved");
  print_heaps("after stop");
  if (_eink) _eink->requestFullRefresh();   // wipe the game's ghosting off the panel
  _task->forceRefresh();
}

// ---- Per-loop poll (UI task) ------------------------------------------------
void GBCEmulatorScreen::poll() {
  if (_mode == PLAYING) {
    // Background save: the game has not touched its cart RAM for a while,
    // so write the .sav now (a few hundred ms of SD work on this task; the
    // game keeps running on core 0 meanwhile).
    if (s_cram_dirty && (uint32_t)(millis() - s_cram_dirty_ms) >= GBC_SAVE_QUIET_MS) {
      s_cram_dirty = false;
      gbc_write_save();
    }
    if (keyboard.rawExitPressed() || s_stop) {
      s_stop = true;
      _mode = STOPPING;
      Serial.println("[GBC] stop requested");
      // The task parks within a frame. Wait for it here (bounded) so the
      // quit completes in this poll, without an intermediate "Saving..."
      // frame costing a 650 ms refresh of its own.
      const uint32_t t0 = millis();
      while (!s_task_stopped && (uint32_t)(millis() - t0) < GBC_STOP_WAIT_MS) delay(1);
    }
  }
  if (_mode == STOPPING && s_task_stopped) {
    finishStop();
  }
  if (_mode == BROWSER && _releaseKbAfterDraw && _browserDrawn) {
    if (_busyHooked && _eink) {
      _eink->setBusyCallback(NULL, 0);
      _busyHooked = false;
    }
    keyboard.setRawJoypad(false);      // also clears any quit presses latched meanwhile
    _releaseKbAfterDraw = false;
  }
#if defined(LilyGo_TDeck_Pro_Max)
  if (s_kbd_bl_req) {
    s_kbd_bl_req = false;
    toggleKeyboardBacklight();
  }
#endif
  // In-game input never passes through injectKey(), so the auto-lock idle
  // timer would otherwise expire mid-game and lock the screen over the top
  // of a running emulator.
  if (_mode != BROWSER) _task->resetIdleTimer();
}

// ---- Input (browser only; raw mode swallows keys while playing) -------------
bool GBCEmulatorScreen::handleInput(char c) {
  if (_mode != BROWSER || _releaseKbAfterDraw) return false;
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
  // Rows follow the user's font size and style via the NodePrefs helpers,
  // the same way the channel picker and games menu do.
  NodePrefs* prefs = _task->getNodePrefs();
  const int lineH = prefs->smallLineH();
  const int hlOff = prefs->smallHighlightOff();
  const int headerH = 14;
  const int footerH = 14;
  int rows = (display.height() - footerH - headerH) / lineH;
  if (rows < 3) rows = 3;

  display.startFrame();
  display.setTextSize(1);

  display.setColor(DisplayDriver::GREEN);
  display.setCursor(2, 2);
  display.print("Game Boy");
  display.setColor(DisplayDriver::LIGHT);
  display.drawRect(0, 12, display.width(), 1);

  if (_cursor >= _scroll + rows) _scroll = _cursor - rows + 1;
  if (_scroll < 0) _scroll = 0;

  display.setTextSize(prefs->smallTextSize());
  if (_romCount == 0) {
    display.setColor(DisplayDriver::LIGHT);
    display.drawTextEllipsized(6, headerH, display.width() - 12, "No .gb/.gbc files");
    display.drawTextEllipsized(6, headerH + lineH, display.width() - 12, "in /roms on SD");
  } else {
    int y = headerH;
    for (int i = _scroll; i < _romCount && i < _scroll + rows; i++) {
      if (i == _cursor) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(0, y + hlOff, display.width(), lineH);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
      }
      display.drawTextEllipsized(6, y, display.width() - 12, _romNames[i]);
      y += lineH;
    }
  }

  _browserDrawn = true;

  display.setTextSize(1);
  display.setColor(DisplayDriver::LIGHT);
  const int fy = display.height() - 12;
  display.drawRect(0, fy - 2, display.width(), 1);
  display.setCursor(2, fy);
  if (_statusUntil != 0 && millis() < _statusUntil) {
    display.print(_status);
    return 500;
  }
  display.print("Enter:Play  Q:Back");
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
  memcpy(s_mono_ui, s_mono_show, OUT_MONO_BYTES);
  taskEXIT_CRITICAL(&s_mux);

  const uint16_t fg = d.rawFgColor();
  d.drawXbmRaw(GBC_IMG_X, GBC_IMG_Y, s_mono_ui, OUT_W, OUT_H, fg);
  d.drawTextRaw(2, 308, (_mode == STOPPING) ? "Saving..." : "Q: Quit", fg);
  if (_mode == PLAYING) {
    if (s_pictures > 0) d.requestWindowRefresh(GBC_IMG_X, GBC_IMG_Y, OUT_W, OUT_H);
    s_pictures++;
  }
  return 100;   // the UI loop's 800 ms e-ink floor sets the real cadence
}

#endif // LilyGo_TDeck_Pro