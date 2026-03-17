#pragma once

// =============================================================================
// MapScreen — OSM Tile Map for T-Deck Pro E-Ink Display
// =============================================================================
//
// Renders standard OSM "slippy map" PNG tiles from SD card onto the e-ink
// display at native 240×320 resolution (bypassing the 128×128 logical grid).
//
// Tiles are B&W PNGs stored at /tiles/{zoom}/{x}/{y}.png — the same format
// used by Ripple, tdeck-maps, and MTD-Script tile downloaders.
//
// REQUIREMENTS:
//   1. Add PNGdec library to platformio.ini:
//        lib_deps = ... bitbank2/PNGdec@^1.0.1
//
//   2. Add raw display access to GxEPDDisplay.h (public section):
//        // --- Raw pixel access for MapScreen (bypasses scaling) ---
//        void drawPixelRaw(int16_t x, int16_t y, uint16_t color) {
//          display.drawPixel(x, y, color);
//        }
//        int16_t rawWidth()  { return display.width(); }
//        int16_t rawHeight() { return display.height(); }
//        // Force endFrame() to push to display even if CRC unchanged
//        // (needed because drawPixelRaw bypasses CRC tracking)
//        void invalidateFrameCRC() { last_display_crc_value = 0; }
//
//   3. Add to UITask.h:
//        #include "MapScreen.h"
//        UIScreen* map_screen;
//        void gotoMapScreen();
//        bool isOnMapScreen() const { return curr == map_screen; }
//        UIScreen* getMapScreen() const { return map_screen; }
//
//   4. Initialise in UITask::begin():
//        map_screen = new MapScreen(this);
//
//   5. Implement UITask::gotoMapScreen() following gotoTextReader() pattern.
//
//   6. Hook 'g' key in main.cpp for GPS/Map access:
//        case 'g':
//          if (ui_task.isOnMapScreen()) {
//            // Already on map — 'g' re-centers on GPS
//            ui_task.injectKey('g');
//          } else {
//            Serial.println("Opening map");
//            {
//              MapScreen* ms = (MapScreen*)ui_task.getMapScreen();
//              if (ms) {
//                ms->setSDReady(sdCardReady);
//                ms->setGPSPosition(sensors.node_lat,
//                                   sensors.node_lon);
//                // Populate contact markers via iterator
//                ms->clearMarkers();
//                ContactsIterator it = the_mesh.startContactsIterator();
//                ContactInfo ci;
//                while (it.hasNext(&the_mesh, ci)) {
//                  double lat = ((double)ci.gps_lat) / 1000000.0;
//                  double lon = ((double)ci.gps_lon) / 1000000.0;
//                  ms->addMarker(lat, lon, ci.name, ci.type);
//                }
//              }
//            }
//            ui_task.gotoMapScreen();
//          }
//          break;
//
//   7. Route WASD/zoom keys to map screen in main.cpp (in existing handlers):
//        For 'w', 's', 'a', 'd' cases, add:
//          if (ui_task.isOnMapScreen()) { ui_task.injectKey(key); break; }
//        For the default case, add map screen passthrough:
//          if (ui_task.isOnMapScreen()) { ui_task.injectKey(key); break; }
//        This covers +, -, i, o, g (re-center) keys too.
//
// TILE SOURCES (B&W recommended for e-ink):
//   - MTD-Script:    github.com/fistulareffigy/MTD-Script
//   - tdeck-maps:    github.com/JustDr00py/tdeck-maps
//   - Stamen Toner style gives best e-ink contrast
// =============================================================================

#include <Arduino.h>
#include <SD.h>
#include <PNGdec.h>
#undef local  // PNGdec's zutil.h defines 'local' as 'static' — breaks any variable named 'local'
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/GxEPDDisplay.h>

// ---------------------------------------------------------------------------
// Layout constants (physical pixel coordinates, 240×320 display)
// ---------------------------------------------------------------------------
#define MAP_DISPLAY_W       240
#define MAP_DISPLAY_H       320

// Footer bar occupies the bottom — matches other screens' setTextSize(1) footer
#define MAP_FOOTER_H        24      // ~24px at bottom for nav hints
#define MAP_VIEWPORT_Y      0       // Map starts at top
#define MAP_VIEWPORT_H      (MAP_DISPLAY_H - MAP_FOOTER_H)  // 296px for map

#define MAP_TILE_SIZE       256     // Standard OSM tile size in pixels
#define MAP_DEFAULT_ZOOM    13
#define MAP_MIN_ZOOM        1
#define MAP_MAX_ZOOM        17

// PNG decode buffer size — 256×256 RGB = 196KB, but PNGdec streams row-by-row
// We only need a line buffer. Allocate in PSRAM for safety.
#define MAP_PNG_BUF_SIZE    (65536)  // 64KB for PNG file read buffer

// Tile path on SD card
#define MAP_TILE_ROOT       "/tiles"

// Contact type (for label display — matches AdvertDataHelpers.h)
#ifndef ADV_TYPE_REPEATER
  #define ADV_TYPE_REPEATER   2
#endif

// Pan step: fraction of viewport to move per keypress
#define MAP_PAN_FRACTION    4       // 1/4 of viewport per press

// Max contact markers (PSRAM-allocated, ~37 bytes each)
#define MAP_MAX_MARKERS     500


class MapScreen : public UIScreen {
public:
  MapScreen(UITask* task)
    : _task(task),
      _einkDisplay(nullptr),
      _sdReady(false),
      _needsRedraw(true),
      _hasFix(false),
      _centerLat(-33.8688),   // Default: Sydney (most Ripple users)
      _centerLon(151.2093),
      _gpsLat(0.0),
      _gpsLon(0.0),
      _zoom(MAP_DEFAULT_ZOOM),
      _zoomMin(MAP_MIN_ZOOM),
      _zoomMax(MAP_MAX_ZOOM),
      _pngBuf(nullptr),
      _lineBuf(nullptr),
      _tileFound(false)
  {
    // Marker array and PNG buffers are deferred to enter() to avoid
    // consuming 20KB+ PSRAM at boot when the map may never be opened.
    _markers = nullptr;
    _numMarkers = 0;
  }

  ~MapScreen() {
    if (_pngBuf) { free(_pngBuf); _pngBuf = nullptr; }
    if (_lineBuf) { free(_lineBuf); _lineBuf = nullptr; }
    if (_markers) { free(_markers); _markers = nullptr; }
  }

  void setSDReady(bool ready) { _sdReady = ready; }

  // Set initial GPS position (called when opening map — centers viewport)
  void setGPSPosition(double lat, double lon) {
    if (lat != 0.0 || lon != 0.0) {
      _gpsLat = lat;
      _gpsLon = lon;
      _centerLat = lat;
      _centerLon = lon;
      _hasFix = true;
      _needsRedraw = true;
    }
  }

  // Update own GPS position without moving viewport (called periodically)
  void updateGPSPosition(double lat, double lon) {
    if (lat == 0.0 && lon == 0.0) return;
    if (lat != _gpsLat || lon != _gpsLon) {
      _gpsLat = lat;
      _gpsLon = lon;
      _hasFix = true;
      _needsRedraw = true;  // Redraw to move own-position marker
    }
  }

  // Add a location marker (call once per contact before entering map)
  void clearMarkers() { _numMarkers = 0; }
  void addMarker(double lat, double lon, const char* name = "", uint8_t type = 0) {
    // Lazy-allocate markers on first use (deferred from constructor)
    if (!_markers) {
      _markers = (MapMarker*)ps_calloc(MAP_MAX_MARKERS, sizeof(MapMarker));
      if (!_markers) return;  // Alloc failed — skip silently
    }
    if (_numMarkers >= MAP_MAX_MARKERS) return;
    if (lat == 0.0 && lon == 0.0) return;  // Skip no-location contacts
    _markers[_numMarkers].lat = lat;
    _markers[_numMarkers].lon = lon;
    _markers[_numMarkers].type = type;
    strncpy(_markers[_numMarkers].name, name, sizeof(_markers[0].name) - 1);
    _markers[_numMarkers].name[sizeof(_markers[0].name) - 1] = '\0';
    _numMarkers++;
  }

  // Refresh contact markers (called periodically from main loop)
  // Clears and rebuilds — caller iterates contacts and calls addMarker()
  int getNumMarkers() const { return _numMarkers; }

  // Called when navigating to map screen
  void enter(DisplayDriver& display) {
    _einkDisplay = static_cast<GxEPDDisplay*>(&display);
    _needsRedraw = true;

    // Allocate marker array in PSRAM on first use (~20KB)
    if (!_markers) {
      _markers = (MapMarker*)ps_calloc(MAP_MAX_MARKERS, sizeof(MapMarker));
      if (_markers) {
        Serial.printf("MapScreen: markers allocated (%d × %d = %d bytes PSRAM)\n",
                       MAP_MAX_MARKERS, (int)sizeof(MapMarker),
                       MAP_MAX_MARKERS * (int)sizeof(MapMarker));
      } else {
        Serial.println("MapScreen: marker PSRAM alloc FAILED");
      }
    }

    // Allocate PNG read buffer in PSRAM on first use
    if (!_pngBuf) {
      _pngBuf = (uint8_t*)ps_malloc(MAP_PNG_BUF_SIZE);
      if (!_pngBuf) {
        Serial.println("MapScreen: PSRAM alloc failed, trying heap");
        _pngBuf = (uint8_t*)malloc(MAP_PNG_BUF_SIZE);
      }
      if (_pngBuf) {
        Serial.printf("MapScreen: PNG buffer allocated (%d bytes)\n", MAP_PNG_BUF_SIZE);
      } else {
        Serial.println("MapScreen: PNG buffer alloc FAILED");
      }
    }

    // Allocate scanline decode buffer in PSRAM (512 bytes — avoids stack
    // allocation inside the PNGdec callback which is called 256× per tile)
    if (!_lineBuf) {
      _lineBuf = (uint16_t*)ps_malloc(MAP_TILE_SIZE * sizeof(uint16_t));
      if (!_lineBuf) {
        _lineBuf = (uint16_t*)malloc(MAP_TILE_SIZE * sizeof(uint16_t));
      }
      if (_lineBuf) {
        Serial.println("MapScreen: lineBuf allocated");
      } else {
        Serial.println("MapScreen: lineBuf alloc FAILED");
      }
    }

    // Detect available zoom levels from SD card directories
    detectZoomRange();
  }

  // ---- UIScreen interface ----

  int render(DisplayDriver& display) override {
    if (!_einkDisplay) {
      _einkDisplay = static_cast<GxEPDDisplay*>(&display);
    }

    if (!_sdReady) {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(10, 20);
      display.print("SD card not found");
      display.setCursor(10, 35);
      display.print("Insert SD with");
      display.setCursor(10, 48);
      display.print("/tiles/{z}/{x}/{y}.png");
      return 5000;
    }

    // Always render tiles — UITask clears the buffer via startFrame() before
    // calling us, so we must redraw every time (e.g. after alert overlays)
    bool wasRedraw = _needsRedraw;
    _needsRedraw = false;

    // Render map tiles into the viewport
    renderMapViewport();

    // Overlay contact markers
    renderContactMarkers();

    // Crosshair at viewport center
    renderCrosshair();

    // Footer bar (uses normal display API with scaling)
    renderFooter(display);

    // Raw pixel writes bypass CRC tracking — force refresh
    _einkDisplay->invalidateFrameCRC();

    // If user panned/zoomed, allow quick re-render; otherwise idle longer
    return wasRedraw ? 1000 : 30000;
  }

  bool handleInput(char c) override {
    // Pan distances in degrees — adaptive to zoom level
    // At zoom Z, one tile covers 360/2^Z degrees of longitude
    double tileLonSpan = 360.0 / (1 << _zoom);
    double tileLatSpan = tileLonSpan * cos(_centerLat * PI / 180.0);  // Rough approx

    // Pan by 1/MAP_PAN_FRACTION of viewport (viewport ≈ 1 tile)
    double panLon = tileLonSpan / MAP_PAN_FRACTION;
    double panLat = tileLatSpan / MAP_PAN_FRACTION;

    switch (c) {
      // ---- WASD panning ----
      case 'w':
      case 'W':
        _centerLat += panLat;
        if (_centerLat > 85.05) _centerLat = 85.05;  // Web Mercator limit
        _needsRedraw = true;
        return true;

      case 's':
      case 'S':
        _centerLat -= panLat;
        if (_centerLat < -85.05) _centerLat = -85.05;
        _needsRedraw = true;
        return true;

      case 'a':
      case 'A':
        _centerLon -= panLon;
        if (_centerLon < -180.0) _centerLon += 360.0;
        _needsRedraw = true;
        return true;

      case 'd':
      case 'D':
        _centerLon += panLon;
        if (_centerLon > 180.0) _centerLon -= 360.0;
        _needsRedraw = true;
        return true;

      // ---- Zoom controls ----
      case 'z':
      case 'Z':
        if (_zoom < _zoomMax) {
          _zoom++;
          _needsRedraw = true;
          Serial.printf("MapScreen: zoom in -> %d\n", _zoom);
        }
        return true;

      case 'x':
      case 'X':
        if (_zoom > _zoomMin) {
          _zoom--;
          _needsRedraw = true;
          Serial.printf("MapScreen: zoom out -> %d\n", _zoom);
        }
        return true;

      // ---- Re-center on GPS fix ----
      case 'g':
        if (_hasFix) {
          _centerLat = _gpsLat;
          _centerLon = _gpsLon;
          _needsRedraw = true;
          Serial.println("MapScreen: re-center on GPS");
        }
        return true;

      default:
        return false;
    }
  }

private:
  UITask* _task;
  GxEPDDisplay* _einkDisplay;
  bool _sdReady;
  bool _needsRedraw;
  bool _hasFix;

  // Map state
  double _centerLat;
  double _centerLon;
  double _gpsLat;       // Own GPS position (separate from viewport center)
  double _gpsLon;
  int _zoom;
  int _zoomMin;     // Detected from SD card
  int _zoomMax;     // Detected from SD card

  // PNG decode buffer (PSRAM)
  uint8_t* _pngBuf;
  uint16_t* _lineBuf;  // Scanline RGB565 buffer for PNG decode (PSRAM)
  bool _tileFound;      // Did last tile load succeed?

  // PNGdec instance
  PNG _png;

  // Contacts for marker overlay
  struct MapMarker {
    double lat;
    double lon;
    char name[20];    // Truncated display name
    uint8_t type;     // ADV_TYPE_CHAT, ADV_TYPE_REPEATER, etc.
  };
  MapMarker* _markers = nullptr;  // PSRAM-allocated
  int _numMarkers = 0;

  // ---- Rendering state passed to PNG callback ----
  // PNGdec calls our callback per scanline — we need to know where to draw.
  // Also carries a PNG* so the static callback can call getLineAsRGB565().
  struct DrawContext {
    GxEPDDisplay* display;
    PNG* png;         // Pointer to the decoder (for getLineAsRGB565)
    int offsetX;      // Screen X offset for this tile
    int offsetY;      // Screen Y offset for this tile
    int viewportY;    // Top of viewport (MAP_VIEWPORT_Y)
    int viewportH;    // Height of viewport (MAP_VIEWPORT_H)
    uint16_t* lineBuf; // Scanline decode buffer (PSRAM-allocated, avoids 512B stack usage per callback)
  };
  DrawContext _drawCtx;

  // ==========================================================================
  // Detect available zoom levels from /tiles/{z}/ directories on SD
  // ==========================================================================

  void detectZoomRange() {
    if (!_sdReady) return;

    _zoomMin = MAP_MAX_ZOOM;
    _zoomMax = MAP_MIN_ZOOM;

    char path[32];
    for (int z = MAP_MIN_ZOOM; z <= MAP_MAX_ZOOM; z++) {
      snprintf(path, sizeof(path), MAP_TILE_ROOT "/%d", z);
      if (SD.exists(path)) {
        if (z < _zoomMin) _zoomMin = z;
        if (z > _zoomMax) _zoomMax = z;
      }
    }

    // If no tiles found, reset to defaults
    if (_zoomMin > _zoomMax) {
      _zoomMin = MAP_MIN_ZOOM;
      _zoomMax = MAP_MAX_ZOOM;
      Serial.println("MapScreen: no tile directories found");
    } else {
      Serial.printf("MapScreen: detected zoom range %d-%d\n", _zoomMin, _zoomMax);
    }

    // Clamp current zoom to available range
    if (_zoom > _zoomMax) _zoom = _zoomMax;
    if (_zoom < _zoomMin) _zoom = _zoomMin;
  }

  // ==========================================================================
  // Tile coordinate math (Web Mercator / Slippy Map convention)
  // ==========================================================================

  // Convert lat/lon to tile X,Y and sub-tile pixel offset at given zoom
  static void latLonToTileXY(double lat, double lon, int zoom,
                              int& tileX, int& tileY,
                              int& pixelX, int& pixelY)
  {
    int n = 1 << zoom;

    // Tile X (longitude is linear)
    double x = (lon + 180.0) / 360.0 * n;
    tileX = (int)floor(x);
    pixelX = (int)((x - tileX) * MAP_TILE_SIZE);

    // Tile Y (latitude uses Mercator projection)
    double latRad = lat * PI / 180.0;
    double y = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / PI) / 2.0 * n;
    tileY = (int)floor(y);
    pixelY = (int)((y - tileY) * MAP_TILE_SIZE);
  }

  // Convert tile X,Y + pixel offset back to lat/lon
  static void tileXYToLatLon(int tileX, int tileY, int pixelX, int pixelY,
                              int zoom, double& lat, double& lon)
  {
    int n = 1 << zoom;
    double x = tileX + (double)pixelX / MAP_TILE_SIZE;
    double y = tileY + (double)pixelY / MAP_TILE_SIZE;

    lon = x / n * 360.0 - 180.0;
    double latRad = atan(sinh(PI * (1.0 - 2.0 * y / n)));
    lat = latRad * 180.0 / PI;
  }

  // Convert a lat/lon to pixel position within the current viewport
  // Returns false if off-screen
  bool latLonToScreen(double lat, double lon, int& screenX, int& screenY) {
    int centerTileX, centerTileY, centerPixelX, centerPixelY;
    latLonToTileXY(_centerLat, _centerLon, _zoom,
                   centerTileX, centerTileY, centerPixelX, centerPixelY);

    int targetTileX, targetTileY, targetPixelX, targetPixelY;
    latLonToTileXY(lat, lon, _zoom,
                   targetTileX, targetTileY, targetPixelX, targetPixelY);

    // Calculate pixel delta from center
    int dx = (targetTileX - centerTileX) * MAP_TILE_SIZE + (targetPixelX - centerPixelX);
    int dy = (targetTileY - centerTileY) * MAP_TILE_SIZE + (targetPixelY - centerPixelY);

    screenX = MAP_DISPLAY_W / 2 + dx;
    screenY = MAP_VIEWPORT_Y + MAP_VIEWPORT_H / 2 + dy;

    return (screenX >= 0 && screenX < MAP_DISPLAY_W &&
            screenY >= MAP_VIEWPORT_Y && screenY < MAP_VIEWPORT_Y + MAP_VIEWPORT_H);
  }

  // ==========================================================================
  // Tile loading and rendering
  // ==========================================================================

  // Build tile file path: /tiles/{zoom}/{x}/{y}.png
  static void buildTilePath(char* buf, int bufSize, int zoom, int x, int y) {
    snprintf(buf, bufSize, MAP_TILE_ROOT "/%d/%d/%d.png", zoom, x, y);
  }

  // Load a PNG tile from SD and decode it directly to the display
  // screenX, screenY = top-left corner on display where this tile goes
  bool loadAndRenderTile(int tileX, int tileY, int screenX, int screenY) {
    if (!_pngBuf || !_lineBuf || !_einkDisplay) return false;

    char path[64];
    buildTilePath(path, sizeof(path), _zoom, tileX, tileY);

    // Check existence first to avoid noisy ESP32 VFS error logs
    if (!SD.exists(path)) return false;

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    // Read entire PNG into buffer
    int fileSize = f.size();
    if (fileSize > MAP_PNG_BUF_SIZE) {
      Serial.printf("MapScreen: tile too large: %s (%d bytes)\n", path, fileSize);
      f.close();
      return false;
    }

    int bytesRead = f.read(_pngBuf, fileSize);
    f.close();

    if (bytesRead != fileSize) {
      Serial.printf("MapScreen: short read: %s (%d/%d)\n", path, bytesRead, fileSize);
      return false;
    }

    // Set up draw context for the PNG callback
    _drawCtx.display   = _einkDisplay;
    _drawCtx.png       = &_png;
    _drawCtx.offsetX   = screenX;
    _drawCtx.offsetY   = screenY;
    _drawCtx.viewportY = MAP_VIEWPORT_Y;
    _drawCtx.viewportH = MAP_VIEWPORT_H;
    _drawCtx.lineBuf   = _lineBuf;

    // Open PNG from memory buffer
    int rc = _png.openRAM(_pngBuf, fileSize, pngDrawCallback);
    if (rc != PNG_SUCCESS) {
      Serial.printf("MapScreen: PNG open failed: %s (rc=%d)\n", path, rc);
      return false;
    }

    // Decode — triggers pngDrawCallback for each scanline.
    // First arg is user pointer, passed as pDraw->pUser in callback.
    rc = _png.decode(&_drawCtx, 0);
    _png.close();

    if (rc != PNG_SUCCESS) {
      Serial.printf("MapScreen: PNG decode failed: %s (rc=%d)\n", path, rc);
      return false;
    }

    return true;
  }

  // PNGdec scanline callback — called once per row of the decoded image.
  // Draws directly to the e-ink display at raw pixel coordinates.
  // Uses getLineAsRGB565 with correct (little) endianness for ESP32.
  static int pngDrawCallback(PNGDRAW* pDraw) {
    DrawContext* ctx = (DrawContext*)pDraw->pUser;
    if (!ctx || !ctx->display || !ctx->png || !ctx->lineBuf) return 0;

    int screenY = ctx->offsetY + pDraw->y;

    // Clip to viewport vertically
    if (screenY < ctx->viewportY || screenY >= ctx->viewportY + ctx->viewportH) return 1;

    // Debug: log format on first row of first tile only
    if (pDraw->y == 0 && ctx->offsetX >= 0 && ctx->offsetY >= 0) {
      static bool logged = false;
      if (!logged) {
        Serial.printf("MapScreen: PNG iBpp=%d iWidth=%d\n", pDraw->iBpp, pDraw->iWidth);
        logged = true;
      }
    }

    uint16_t lineWidth = pDraw->iWidth;
    if (lineWidth > MAP_TILE_SIZE) lineWidth = MAP_TILE_SIZE;
    ctx->png->getLineAsRGB565(pDraw, ctx->lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    for (int x = 0; x < lineWidth; x++) {
      int screenX = ctx->offsetX + x;
      if (screenX < 0 || screenX >= MAP_DISPLAY_W) continue;

      // RGB565 little-endian on ESP32: standard bit layout
      // R[15:11] G[10:5] B[4:0]
      uint16_t pixel = ctx->lineBuf[x];

      // For B&W tiles this is 0x0000 (black) or 0xFFFF (white)
      // Simple threshold on full 16-bit value handles both cleanly
      uint16_t color = (pixel > 0x7FFF) ? GxEPD_WHITE : GxEPD_BLACK;
      ctx->display->drawPixelRaw(screenX, screenY, color);
    }
    return 1;
  }

  // ==========================================================================
  // Viewport rendering — stitch tiles to fill the screen
  // ==========================================================================

  void renderMapViewport() {
    if (!_einkDisplay) return;

    // Find which tile the center point falls in
    int centerTileX, centerTileY, centerPixelX, centerPixelY;
    latLonToTileXY(_centerLat, _centerLon, _zoom,
                   centerTileX, centerTileY, centerPixelX, centerPixelY);

    Serial.printf("MapScreen: center tile %d/%d/%d  px(%d,%d)\n",
                  _zoom, centerTileX, centerTileY, centerPixelX, centerPixelY);

    // Screen position where the center tile's (0,0) corner should be placed
    // such that the GPS point ends up at viewport center
    int viewCenterX = MAP_DISPLAY_W / 2;
    int viewCenterY = MAP_VIEWPORT_Y + MAP_VIEWPORT_H / 2;

    int baseTileScreenX = viewCenterX - centerPixelX;
    int baseTileScreenY = viewCenterY - centerPixelY;

    // Determine tile grid range needed to cover the entire viewport
    int startDX = 0, startDY = 0;
    int endDX = 0, endDY = 0;

    while (baseTileScreenX + startDX * MAP_TILE_SIZE > 0) startDX--;
    while (baseTileScreenY + startDY * MAP_TILE_SIZE > MAP_VIEWPORT_Y) startDY--;
    while (baseTileScreenX + (endDX + 1) * MAP_TILE_SIZE < MAP_DISPLAY_W) endDX++;
    while (baseTileScreenY + (endDY + 1) * MAP_TILE_SIZE < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) endDY++;

    int maxTile = (1 << _zoom) - 1;
    int loaded = 0, missing = 0;

    for (int dy = startDY; dy <= endDY; dy++) {
      for (int dx = startDX; dx <= endDX; dx++) {
        int tx = centerTileX + dx;
        int ty = centerTileY + dy;

        // Longitude wraps
        if (tx < 0) tx += (1 << _zoom);
        if (tx > maxTile) tx -= (1 << _zoom);

        // Latitude doesn't wrap — skip out-of-range
        if (ty < 0 || ty > maxTile) continue;

        int screenX = baseTileScreenX + dx * MAP_TILE_SIZE;
        int screenY = baseTileScreenY + dy * MAP_TILE_SIZE;

        if (loadAndRenderTile(tx, ty, screenX, screenY)) {
          loaded++;
        } else {
          missing++;
        }
        yield();  // Feed WDT between tiles — each tile can take 1-2s at 80MHz
      }
    }

    Serial.printf("MapScreen: rendered %d tiles, %d missing\n", loaded, missing);
    _tileFound = (loaded > 0);
  }

  // ==========================================================================
  // Contact marker overlay
  // ==========================================================================

  void renderContactMarkers() {
    if (!_einkDisplay || !_markers) return;

    int visible = 0;
    for (int i = 0; i < _numMarkers; i++) {
      int sx, sy;
      if (latLonToScreen(_markers[i].lat, _markers[i].lon, sx, sy)) {
        int r = markerRadius();
        drawDiamond(sx, sy, r);

        // Draw name label for repeaters (and at higher zoom for all contacts)
        if (_markers[i].name[0] != '\0' &&
            (_markers[i].type == ADV_TYPE_REPEATER || _zoom >= 14)) {
          drawLabel(sx, sy - r - 2, _markers[i].name);
        }
        visible++;
      }
    }

    // Render own GPS position as a distinct marker (circle)
    if (_hasFix) {
      int sx, sy;
      if (latLonToScreen(_gpsLat, _gpsLon, sx, sy)) {
        drawOwnPosition(sx, sy);
        visible++;
      }
    }
  }

  // Marker radius scaled by zoom level
  // z10→3px, z11→4, z12→5, z13→6, z14→7, z15→8, z16→9, z17→10
  int markerRadius() {
    int r = _zoom - 7;
    if (r < 3) r = 3;
    if (r > 10) r = 10;
    return r;
  }

  // Draw a filled diamond marker at screen coordinates with given radius
  void drawDiamond(int cx, int cy, int r) {
    // White outline first (1px larger than fill)
    for (int dy = -(r + 1); dy <= (r + 1); dy++) {
      int span = (r + 1) - abs(dy);
      int innerSpan = r - abs(dy);
      for (int dx = -span; dx <= span; dx++) {
        if (abs(dy) <= r && abs(dx) <= innerSpan) continue;
        int px = cx + dx, py = cy + dy;
        if (px >= 0 && px < MAP_DISPLAY_W &&
            py >= MAP_VIEWPORT_Y && py < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) {
          _einkDisplay->drawPixelRaw(px, py, GxEPD_WHITE);
        }
      }
    }

    // Filled black diamond
    for (int dy = -r; dy <= r; dy++) {
      int span = r - abs(dy);
      for (int dx = -span; dx <= span; dx++) {
        int px = cx + dx, py = cy + dy;
        if (px >= 0 && px < MAP_DISPLAY_W &&
            py >= MAP_VIEWPORT_Y && py < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) {
          _einkDisplay->drawPixelRaw(px, py, GxEPD_BLACK);
        }
      }
    }
  }

  // Strip non-ASCII characters (emoji, flags, symbols) from label text.
  // Copies only printable ASCII (0x20-0x7E) into dest buffer.
  // Skips leading whitespace after stripping. Returns length.
  static int extractAsciiLabel(const char* src, char* dest, int destSize) {
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < destSize - 1; i++) {
      uint8_t ch = (uint8_t)src[i];
      if (ch >= 0x20 && ch <= 0x7E) {
        dest[j++] = src[i];
      }
      // Skip continuation bytes of multi-byte UTF-8 sequences
    }
    dest[j] = '\0';

    // Trim leading spaces (left after stripping emoji prefix)
    int start = 0;
    while (dest[start] == ' ') start++;
    if (start > 0) {
      memmove(dest, dest + start, j - start + 1);
      j -= start;
    }
    return j;
  }

  // Draw a text label above a marker with white background for readability
  // Built-in font is 5×7 pixels per character
  void drawLabel(int cx, int topY, const char* text) {
    // Clean emoji/non-ASCII from label
    char clean[24];
    int len = extractAsciiLabel(text, clean, sizeof(clean));
    if (len == 0) return;  // Nothing printable
    if (len > 14) len = 14;  // Truncate long names
    clean[len] = '\0';

    int textW = len * 6;     // 5px char + 1px spacing
    int textH = 8;           // 7px + 1px padding

    int lx = cx - textW / 2;
    int ly = topY - textH;

    // Clamp to viewport
    if (lx < 1) lx = 1;
    if (lx + textW >= MAP_DISPLAY_W - 1) lx = MAP_DISPLAY_W - textW - 1;
    if (ly < MAP_VIEWPORT_Y) ly = MAP_VIEWPORT_Y;
    if (ly + textH >= MAP_VIEWPORT_Y + MAP_VIEWPORT_H) return;

    // White background rectangle
    for (int y = ly - 1; y <= ly + textH; y++) {
      for (int x = lx - 1; x <= lx + textW; x++) {
        if (x >= 0 && x < MAP_DISPLAY_W &&
            y >= MAP_VIEWPORT_Y && y < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) {
          _einkDisplay->drawPixelRaw(x, y, GxEPD_WHITE);
        }
      }
    }

    // Draw text using raw font rendering
    _einkDisplay->drawTextRaw(lx, ly, clean, GxEPD_BLACK);
  }

  // Draw own-position marker: bold circle with filled center dot
  // Fixed size (doesn't scale with zoom) so it's always clearly visible
  void drawOwnPosition(int cx, int cy) {
    int r = 8;  // Outer radius — always prominent

    // White halo (clears map underneath)
    for (int dy = -(r + 2); dy <= (r + 2); dy++) {
      for (int dx = -(r + 2); dx <= (r + 2); dx++) {
        if (dx * dx + dy * dy <= (r + 2) * (r + 2)) {
          int px = cx + dx, py = cy + dy;
          if (px >= 0 && px < MAP_DISPLAY_W &&
              py >= MAP_VIEWPORT_Y && py < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) {
            _einkDisplay->drawPixelRaw(px, py, GxEPD_WHITE);
          }
        }
      }
    }

    // Thick black circle outline (2px wide ring)
    for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
        int d2 = dx * dx + dy * dy;
        if (d2 >= (r - 2) * (r - 2) && d2 <= r * r) {
          int px = cx + dx, py = cy + dy;
          if (px >= 0 && px < MAP_DISPLAY_W &&
              py >= MAP_VIEWPORT_Y && py < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) {
            _einkDisplay->drawPixelRaw(px, py, GxEPD_BLACK);
          }
        }
      }
    }

    // Filled black center dot (radius 3)
    for (int dy = -3; dy <= 3; dy++) {
      for (int dx = -3; dx <= 3; dx++) {
        if (dx * dx + dy * dy <= 9) {
          int px = cx + dx, py = cy + dy;
          if (px >= 0 && px < MAP_DISPLAY_W &&
              py >= MAP_VIEWPORT_Y && py < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) {
            _einkDisplay->drawPixelRaw(px, py, GxEPD_BLACK);
          }
        }
      }
    }
  }

  // ==========================================================================
  // Crosshair at viewport center
  // ==========================================================================

  void renderCrosshair() {
    if (!_einkDisplay) return;

    int cx = MAP_DISPLAY_W / 2;
    int cy = MAP_VIEWPORT_Y + MAP_VIEWPORT_H / 2;
    int len = markerRadius() + 2;  // Scales with zoom

    // Draw thin crosshair: black line with white border for contrast
    // Horizontal arm
    for (int x = cx - len; x <= cx + len; x++) {
      if (x >= 0 && x < MAP_DISPLAY_W) {
        if (cy - 1 >= MAP_VIEWPORT_Y)
          _einkDisplay->drawPixelRaw(x, cy - 1, GxEPD_WHITE);
        if (cy + 1 < MAP_VIEWPORT_Y + MAP_VIEWPORT_H)
          _einkDisplay->drawPixelRaw(x, cy + 1, GxEPD_WHITE);
        _einkDisplay->drawPixelRaw(x, cy, GxEPD_BLACK);
      }
    }

    // Vertical arm
    for (int y = cy - len; y <= cy + len; y++) {
      if (y >= MAP_VIEWPORT_Y && y < MAP_VIEWPORT_Y + MAP_VIEWPORT_H) {
        if (cx - 1 >= 0)
          _einkDisplay->drawPixelRaw(cx - 1, y, GxEPD_WHITE);
        if (cx + 1 < MAP_DISPLAY_W)
          _einkDisplay->drawPixelRaw(cx + 1, y, GxEPD_WHITE);
        _einkDisplay->drawPixelRaw(cx, y, GxEPD_BLACK);
      }
    }
  }

  // ==========================================================================
  // Footer bar — zoom level, GPS status, navigation hints
  // ==========================================================================

  void renderFooter(DisplayDriver& display) {
    // Use the standard footer pattern: setTextSize(1) at height()-12
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    int footerY = display.height() - 12;

    // Separator line
    display.drawRect(0, footerY - 2, display.width(), 1);

    // Left: zoom level
    char left[8];
    snprintf(left, sizeof(left), "Z%d", _zoom);
    display.setCursor(0, footerY);
    display.print(left);

    // Right: navigation hint
    const char* right = "WASD:pan Z/X:zoom";
    display.setCursor(display.width() - display.getTextWidth(right) - 2, footerY);
    display.print(right);
  }
};