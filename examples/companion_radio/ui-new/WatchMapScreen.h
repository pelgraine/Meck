#pragma once

// WatchMapScreen -- T-Watch S3 Plus map screen.
//
// Renders standard OSM "slippy map" B&W PNG tiles from the LittleFS "maps"
// partition onto the LovyanGFX sprite buffer. Tiles are stored at
// /tiles/{zoom}/{x}/{y}.png -- the same layout as the e-ink MapScreen -- but
// read via LittleFS instead of SD. The maps partition is mounted at basePath
// "/maps" in main.cpp, and the Arduino VFS prepends that mountpoint
// automatically, so tile paths passed to LittleFS stay relative ("/tiles/..").
//
// This is a separate screen from the e-ink MapScreen: the watch draws into a
// 120x120 logical sprite (UI_ZOOM=2 scales it 2x to the 240x240 panel) via the
// generic DisplayDriver / LGFXDisplay interface, whereas MapScreen is welded to
// GxEPDDisplay's drawPixelRaw path. Coordinate math, tile stitching and the
// PNGdec decode are carried over unchanged; only the pixel writes, colours,
// dimensions, filesystem and input (touch instead of WASD) differ.

#include <Arduino.h>
#include <LittleFS.h>
#include <PNGdec.h>
#undef local  // PNGdec's zutil.h defines 'local' as 'static' -- breaks any variable named 'local'
#include <helpers/ui/UIScreen.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/LGFXDisplay.h>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
#define WMAP_TILE_SIZE       256     // Standard OSM tile size in pixels
#define WMAP_DEFAULT_ZOOM    13
#define WMAP_MIN_ZOOM        1
#define WMAP_MAX_ZOOM        17

// PNG file read buffer (PSRAM). Tiles stream row-by-row via PNGdec.
#define WMAP_PNG_BUF_SIZE    (65536)

// Tile path relative to the LittleFS "maps" mountpoint ("/maps" is prepended
// by the VFS, so the on-disk file is /maps/tiles/{z}/{x}/{y}.png).
#define WMAP_TILE_ROOT       "/tiles"

// Contact type (for label display -- matches AdvertDataHelpers.h)
#ifndef ADV_TYPE_REPEATER
  #define ADV_TYPE_REPEATER   2
#endif

// Pan step: fraction of viewport to move per press (unused for touch, kept for
// completeness / any future key routing)
#define WMAP_PAN_FRACTION    4

// Max contact markers (PSRAM-allocated)
#define WMAP_MAX_MARKERS     500

// Footer bar (logical pixels)
#define WMAP_FOOTER_H        16

// B&W tile colours (RGB565; the sprite is 8-bit but LovyanGFX converts)
#define WMAP_BLACK           0x0000
#define WMAP_WHITE           0xFFFF


class UITask;

class WatchMapScreen : public UIScreen {
public:
  WatchMapScreen(UITask* task)
    : _task(task),
      _lgfx(nullptr),
      _mapsReady(false),
      _needsRedraw(true),
      _hasFix(false),
      _centerLat(-33.8688),   // Default: Sydney
      _centerLon(151.2093),
      _gpsLat(0.0),
      _gpsLon(0.0),
      _zoom(WMAP_DEFAULT_ZOOM),
      _zoomMin(WMAP_MIN_ZOOM),
      _zoomMax(WMAP_MAX_ZOOM),
      _pngBuf(nullptr),
      _lineBuf(nullptr),
      _tileFound(false),
      _w(120),
      _h(120),
      _viewportH(120 - WMAP_FOOTER_H),
      _markers(nullptr),
      _numMarkers(0)
  {
  }

  ~WatchMapScreen() {
    if (_pngBuf) { free(_pngBuf); _pngBuf = nullptr; }
    if (_lineBuf) { free(_lineBuf); _lineBuf = nullptr; }
    if (_markers) { free(_markers); _markers = nullptr; }
  }

  // Whether the maps LittleFS partition mounted (set from main.cpp on open)
  void setMapsReady(bool ready) { _mapsReady = ready; }

  // Set initial GPS position (called when opening map -- centers viewport)
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
      _needsRedraw = true;
    }
  }

  // Marker management (call once per contact before/while on map)
  void clearMarkers() { _numMarkers = 0; }
  void addMarker(double lat, double lon, const char* name = "", uint8_t type = 0) {
    if (!_markers) {
      _markers = (MapMarker*)ps_calloc(WMAP_MAX_MARKERS, sizeof(MapMarker));
      if (!_markers) return;
    }
    if (_numMarkers >= WMAP_MAX_MARKERS) return;
    if (lat == 0.0 && lon == 0.0) return;
    _markers[_numMarkers].lat = lat;
    _markers[_numMarkers].lon = lon;
    _markers[_numMarkers].type = type;
    strncpy(_markers[_numMarkers].name, name, sizeof(_markers[0].name) - 1);
    _markers[_numMarkers].name[sizeof(_markers[0].name) - 1] = '\0';
    _numMarkers++;
  }
  int getNumMarkers() const { return _numMarkers; }

  // Called when navigating to the map screen
  void enter(DisplayDriver& display) {
    _lgfx = (LGFXDisplay*)&display;
    _w = display.width();
    _h = display.height();
    _viewportH = _h - WMAP_FOOTER_H;
    _needsRedraw = true;

    if (!_markers) {
      _markers = (MapMarker*)ps_calloc(WMAP_MAX_MARKERS, sizeof(MapMarker));
    }
    if (!_pngBuf) {
      _pngBuf = (uint8_t*)ps_malloc(WMAP_PNG_BUF_SIZE);
      if (!_pngBuf) _pngBuf = (uint8_t*)malloc(WMAP_PNG_BUF_SIZE);
    }
    if (!_lineBuf) {
      _lineBuf = (uint16_t*)ps_malloc(WMAP_TILE_SIZE * sizeof(uint16_t));
      if (!_lineBuf) _lineBuf = (uint16_t*)malloc(WMAP_TILE_SIZE * sizeof(uint16_t));
    }

    detectZoomRange();
  }

  // ---- Touch actions (called from main.cpp gesture dispatch) ----

  void zoomIn() {
    if (_zoom < _zoomMax) { _zoom++; _needsRedraw = true; }
  }
  void zoomOut() {
    if (_zoom > _zoomMin) { _zoom--; _needsRedraw = true; }
  }
  void recenter() {
    if (_hasFix) {
      _centerLat = _gpsLat;
      _centerLon = _gpsLon;
      _needsRedraw = true;
    }
  }

  // Pan the viewport by a logical-pixel delta (swipe). Exact: convert centre to
  // global pixel space, offset, convert back.
  void panByPixels(int dx, int dy) {
    int ctX, ctY, cpX, cpY;
    latLonToTileXY(_centerLat, _centerLon, _zoom, ctX, ctY, cpX, cpY);

    long globalX = (long)ctX * WMAP_TILE_SIZE + cpX - dx;
    long globalY = (long)ctY * WMAP_TILE_SIZE + cpY - dy;

    long span = (long)(1 << _zoom) * WMAP_TILE_SIZE;
    // Longitude wraps
    while (globalX < 0) globalX += span;
    while (globalX >= span) globalX -= span;
    // Latitude clamps
    if (globalY < 0) globalY = 0;
    if (globalY > span - 1) globalY = span - 1;

    int nTileX = (int)(globalX / WMAP_TILE_SIZE);
    int nPixX  = (int)(globalX % WMAP_TILE_SIZE);
    int nTileY = (int)(globalY / WMAP_TILE_SIZE);
    int nPixY  = (int)(globalY % WMAP_TILE_SIZE);

    tileXYToLatLon(nTileX, nTileY, nPixX, nPixY, _zoom, _centerLat, _centerLon);
    _needsRedraw = true;
  }

  // Handle a discrete tap at logical coords. Footer +/- buttons zoom; a tap in
  // the map area recenters on GPS. Returns true if the tap was consumed.
  bool handleTap(int x, int y) {
    int footerY = _h - WMAP_FOOTER_H;
    if (y >= footerY) {
      if (inRect(x, y, _plusX, _btnY, _btnW, _btnH))  { zoomIn();  return true; }
      if (inRect(x, y, _minusX, _btnY, _btnW, _btnH)) { zoomOut(); return true; }
      return false;  // footer, but not on a button
    }
    // Tap in the map area -> recenter on own position
    recenter();
    return true;
  }

  // ---- UIScreen interface ----

  int render(DisplayDriver& display) override {
    _lgfx = (LGFXDisplay*)&display;
    _w = display.width();
    _h = display.height();
    _viewportH = _h - WMAP_FOOTER_H;

    if (!_mapsReady) {
      display.setTextSize(1);
      display.setColor(DisplayDriver::LIGHT);
      display.setCursor(4, 20);
      display.print("No map tiles");
      renderFooter(display);
      return 5000;
    }

    bool wasRedraw = _needsRedraw;
    _needsRedraw = false;

    renderMapViewport();
    renderContactMarkers();
    renderCrosshair();
    renderFooter(display);

    return wasRedraw ? 500 : 30000;
  }

private:
  UITask* _task;
  LGFXDisplay* _lgfx;
  bool _mapsReady;
  bool _needsRedraw;
  bool _hasFix;

  double _centerLat;
  double _centerLon;
  double _gpsLat;
  double _gpsLon;
  int _zoom;
  int _zoomMin;
  int _zoomMax;

  uint8_t* _pngBuf;
  uint16_t* _lineBuf;
  bool _tileFound;

  int _w;
  int _h;
  int _viewportH;

  // Footer button rects (set in renderFooter)
  int _plusX = 0, _minusX = 0, _btnY = 0, _btnW = 18, _btnH = 14;

  PNG _png;

  struct MapMarker {
    double lat;
    double lon;
    char name[20];
    uint8_t type;
  };
  MapMarker* _markers;
  int _numMarkers;

  struct DrawContext {
    LGFXDisplay* display;
    PNG* png;
    int offsetX;
    int offsetY;
    int viewportH;
    int dispW;
    uint16_t* lineBuf;
  };
  DrawContext _drawCtx;

  static bool inRect(int px, int py, int x, int y, int w, int h) {
    return px >= x && px < x + w && py >= y && py < y + h;
  }

  // ==========================================================================
  // Detect available zoom levels from /tiles/{z}/ directories on LittleFS
  // ==========================================================================
  void detectZoomRange() {
    if (!_mapsReady) return;

    _zoomMin = WMAP_MAX_ZOOM;
    _zoomMax = WMAP_MIN_ZOOM;

    char path[32];
    for (int z = WMAP_MIN_ZOOM; z <= WMAP_MAX_ZOOM; z++) {
      snprintf(path, sizeof(path), WMAP_TILE_ROOT "/%d", z);
      if (LittleFS.exists(path)) {
        if (z < _zoomMin) _zoomMin = z;
        if (z > _zoomMax) _zoomMax = z;
      }
    }

    if (_zoomMin > _zoomMax) {
      _zoomMin = WMAP_MIN_ZOOM;
      _zoomMax = WMAP_MAX_ZOOM;
      Serial.println("WatchMapScreen: no tile directories found");
    } else {
      Serial.printf("WatchMapScreen: detected zoom range %d-%d\n", _zoomMin, _zoomMax);
    }

    if (_zoom > _zoomMax) _zoom = _zoomMax;
    if (_zoom < _zoomMin) _zoom = _zoomMin;
  }

  // ==========================================================================
  // Tile coordinate math (Web Mercator / Slippy Map) -- identical to MapScreen
  // ==========================================================================
  static void latLonToTileXY(double lat, double lon, int zoom,
                              int& tileX, int& tileY,
                              int& pixelX, int& pixelY)
  {
    int n = 1 << zoom;
    double x = (lon + 180.0) / 360.0 * n;
    tileX = (int)floor(x);
    pixelX = (int)((x - tileX) * WMAP_TILE_SIZE);

    double latRad = lat * PI / 180.0;
    double y = (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / PI) / 2.0 * n;
    tileY = (int)floor(y);
    pixelY = (int)((y - tileY) * WMAP_TILE_SIZE);
  }

  static void tileXYToLatLon(int tileX, int tileY, int pixelX, int pixelY,
                              int zoom, double& lat, double& lon)
  {
    int n = 1 << zoom;
    double x = tileX + (double)pixelX / WMAP_TILE_SIZE;
    double y = tileY + (double)pixelY / WMAP_TILE_SIZE;

    lon = x / n * 360.0 - 180.0;
    double latRad = atan(sinh(PI * (1.0 - 2.0 * y / n)));
    lat = latRad * 180.0 / PI;
  }

  bool latLonToScreen(double lat, double lon, int& screenX, int& screenY) {
    int centerTileX, centerTileY, centerPixelX, centerPixelY;
    latLonToTileXY(_centerLat, _centerLon, _zoom,
                   centerTileX, centerTileY, centerPixelX, centerPixelY);

    int targetTileX, targetTileY, targetPixelX, targetPixelY;
    latLonToTileXY(lat, lon, _zoom,
                   targetTileX, targetTileY, targetPixelX, targetPixelY);

    int dx = (targetTileX - centerTileX) * WMAP_TILE_SIZE + (targetPixelX - centerPixelX);
    int dy = (targetTileY - centerTileY) * WMAP_TILE_SIZE + (targetPixelY - centerPixelY);

    screenX = _w / 2 + dx;
    screenY = _viewportH / 2 + dy;

    return (screenX >= 0 && screenX < _w &&
            screenY >= 0 && screenY < _viewportH);
  }

  // ==========================================================================
  // Tile loading and rendering
  // ==========================================================================
  static void buildTilePath(char* buf, int bufSize, int zoom, int x, int y) {
    snprintf(buf, bufSize, WMAP_TILE_ROOT "/%d/%d/%d.png", zoom, x, y);
  }

  bool loadAndRenderTile(int tileX, int tileY, int screenX, int screenY) {
    if (!_pngBuf || !_lineBuf || !_lgfx) return false;

    char path[64];
    buildTilePath(path, sizeof(path), _zoom, tileX, tileY);

    if (!LittleFS.exists(path)) return false;

    File f = LittleFS.open(path, FILE_READ);
    if (!f) return false;

    int fileSize = f.size();
    if (fileSize > WMAP_PNG_BUF_SIZE) {
      Serial.printf("WatchMapScreen: tile too large: %s (%d bytes)\n", path, fileSize);
      f.close();
      return false;
    }

    int bytesRead = f.read(_pngBuf, fileSize);
    f.close();

    if (bytesRead != fileSize) {
      Serial.printf("WatchMapScreen: short read: %s (%d/%d)\n", path, bytesRead, fileSize);
      return false;
    }

    _drawCtx.display   = _lgfx;
    _drawCtx.png       = &_png;
    _drawCtx.offsetX   = screenX;
    _drawCtx.offsetY   = screenY;
    _drawCtx.viewportH = _viewportH;
    _drawCtx.dispW     = _w;
    _drawCtx.lineBuf   = _lineBuf;

    int rc = _png.openRAM(_pngBuf, fileSize, pngDrawCallback);
    if (rc != PNG_SUCCESS) {
      Serial.printf("WatchMapScreen: PNG open failed: %s (rc=%d)\n", path, rc);
      return false;
    }

    rc = _png.decode(&_drawCtx, 0);
    _png.close();

    if (rc != PNG_SUCCESS) {
      Serial.printf("WatchMapScreen: PNG decode failed: %s (rc=%d)\n", path, rc);
      return false;
    }

    return true;
  }

  // PNGdec scanline callback -- draws one decoded row into the sprite buffer.
  static int pngDrawCallback(PNGDRAW* pDraw) {
    DrawContext* ctx = (DrawContext*)pDraw->pUser;
    if (!ctx || !ctx->display || !ctx->png || !ctx->lineBuf) return 0;

    int screenY = ctx->offsetY + pDraw->y;
    if (screenY < 0 || screenY >= ctx->viewportH) return 1;

    uint16_t lineWidth = pDraw->iWidth;
    if (lineWidth > WMAP_TILE_SIZE) lineWidth = WMAP_TILE_SIZE;
    ctx->png->getLineAsRGB565(pDraw, ctx->lineBuf, PNG_RGB565_LITTLE_ENDIAN, 0xFFFFFFFF);

    for (int x = 0; x < lineWidth; x++) {
      int screenX = ctx->offsetX + x;
      if (screenX < 0 || screenX >= ctx->dispW) continue;

      uint16_t pixel = ctx->lineBuf[x];
      uint16_t color = (pixel > 0x7FFF) ? WMAP_WHITE : WMAP_BLACK;
      ctx->display->drawPixelRaw(screenX, screenY, color);
    }
    return 1;
  }

  // ==========================================================================
  // Viewport rendering -- stitch tiles to fill the map area
  // ==========================================================================
  void renderMapViewport() {
    if (!_lgfx) return;

    int centerTileX, centerTileY, centerPixelX, centerPixelY;
    latLonToTileXY(_centerLat, _centerLon, _zoom,
                   centerTileX, centerTileY, centerPixelX, centerPixelY);

    int viewCenterX = _w / 2;
    int viewCenterY = _viewportH / 2;

    int baseTileScreenX = viewCenterX - centerPixelX;
    int baseTileScreenY = viewCenterY - centerPixelY;

    int startDX = 0, startDY = 0;
    int endDX = 0, endDY = 0;

    while (baseTileScreenX + startDX * WMAP_TILE_SIZE > 0) startDX--;
    while (baseTileScreenY + startDY * WMAP_TILE_SIZE > 0) startDY--;
    while (baseTileScreenX + (endDX + 1) * WMAP_TILE_SIZE < _w) endDX++;
    while (baseTileScreenY + (endDY + 1) * WMAP_TILE_SIZE < _viewportH) endDY++;

    int maxTile = (1 << _zoom) - 1;
    int loaded = 0, missing = 0;

    for (int dy = startDY; dy <= endDY; dy++) {
      for (int dx = startDX; dx <= endDX; dx++) {
        int tx = centerTileX + dx;
        int ty = centerTileY + dy;

        if (tx < 0) tx += (1 << _zoom);
        if (tx > maxTile) tx -= (1 << _zoom);
        if (ty < 0 || ty > maxTile) continue;

        int screenX = baseTileScreenX + dx * WMAP_TILE_SIZE;
        int screenY = baseTileScreenY + dy * WMAP_TILE_SIZE;

        if (loadAndRenderTile(tx, ty, screenX, screenY)) loaded++;
        else missing++;
        yield();
      }
    }

    Serial.printf("WatchMapScreen: rendered %d tiles, %d missing\n", loaded, missing);
    _tileFound = (loaded > 0);
  }

  // ==========================================================================
  // Contact marker overlay
  // ==========================================================================
  void renderContactMarkers() {
    if (!_lgfx || !_markers) return;

    for (int i = 0; i < _numMarkers; i++) {
      int sx, sy;
      if (latLonToScreen(_markers[i].lat, _markers[i].lon, sx, sy)) {
        int r = markerRadius();
        drawDiamond(sx, sy, r);
        if (_markers[i].name[0] != '\0' &&
            (_markers[i].type == ADV_TYPE_REPEATER || _zoom >= 14)) {
          drawLabel(sx, sy - r - 2, _markers[i].name);
        }
      }
    }

    if (_hasFix) {
      int sx, sy;
      if (latLonToScreen(_gpsLat, _gpsLon, sx, sy)) {
        drawOwnPosition(sx, sy);
      }
    }
  }

  // Marker radius scaled by zoom (smaller than e-ink -- 120px canvas)
  int markerRadius() {
    int r = _zoom - 9;
    if (r < 2) r = 2;
    if (r > 6) r = 6;
    return r;
  }

  void drawDiamond(int cx, int cy, int r) {
    // White outline (1px larger than fill)
    for (int dy = -(r + 1); dy <= (r + 1); dy++) {
      int span = (r + 1) - abs(dy);
      int innerSpan = r - abs(dy);
      for (int dx = -span; dx <= span; dx++) {
        if (abs(dy) <= r && abs(dx) <= innerSpan) continue;
        int px = cx + dx, py = cy + dy;
        if (px >= 0 && px < _w && py >= 0 && py < _viewportH) {
          _lgfx->drawPixelRaw(px, py, WMAP_WHITE);
        }
      }
    }
    // Filled black diamond
    for (int dy = -r; dy <= r; dy++) {
      int span = r - abs(dy);
      for (int dx = -span; dx <= span; dx++) {
        int px = cx + dx, py = cy + dy;
        if (px >= 0 && px < _w && py >= 0 && py < _viewportH) {
          _lgfx->drawPixelRaw(px, py, WMAP_BLACK);
        }
      }
    }
  }

  static int extractAsciiLabel(const char* src, char* dest, int destSize) {
    int j = 0;
    for (int i = 0; src[i] != '\0' && j < destSize - 1; i++) {
      uint8_t ch = (uint8_t)src[i];
      if (ch >= 0x20 && ch <= 0x7E) dest[j++] = src[i];
    }
    dest[j] = '\0';
    int start = 0;
    while (dest[start] == ' ') start++;
    if (start > 0) {
      memmove(dest, dest + start, j - start + 1);
      j -= start;
    }
    return j;
  }

  // Draw a marker label with a white background box, using the normal buffered
  // text API (Font0 6x8). Kept short -- the canvas is only 120px wide.
  void drawLabel(int cx, int topY, const char* text) {
    if (!_lgfx) return;
    char clean[16];
    int len = extractAsciiLabel(text, clean, sizeof(clean));
    if (len == 0) return;
    if (len > 10) { len = 10; clean[len] = '\0'; }

    int textW = len * 6;
    int textH = 8;
    int lx = cx - textW / 2;
    int ly = topY - textH;

    if (lx < 1) lx = 1;
    if (lx + textW >= _w - 1) lx = _w - textW - 1;
    if (ly < 0) ly = 0;
    if (ly + textH >= _viewportH) return;

    _lgfx->setColor(DisplayDriver::LIGHT);
    _lgfx->fillRect(lx - 1, ly - 1, textW + 2, textH + 2);
    _lgfx->setColor(DisplayDriver::DARK);
    _lgfx->setTextSize(1);
    _lgfx->setCursor(lx, ly);
    _lgfx->print(clean);
  }

  // Own-position marker: bold circle with filled centre dot
  void drawOwnPosition(int cx, int cy) {
    int r = 6;
    // White halo
    for (int dy = -(r + 2); dy <= (r + 2); dy++) {
      for (int dx = -(r + 2); dx <= (r + 2); dx++) {
        if (dx * dx + dy * dy <= (r + 2) * (r + 2)) {
          int px = cx + dx, py = cy + dy;
          if (px >= 0 && px < _w && py >= 0 && py < _viewportH)
            _lgfx->drawPixelRaw(px, py, WMAP_WHITE);
        }
      }
    }
    // Black ring
    for (int dy = -r; dy <= r; dy++) {
      for (int dx = -r; dx <= r; dx++) {
        int d2 = dx * dx + dy * dy;
        if (d2 >= (r - 2) * (r - 2) && d2 <= r * r) {
          int px = cx + dx, py = cy + dy;
          if (px >= 0 && px < _w && py >= 0 && py < _viewportH)
            _lgfx->drawPixelRaw(px, py, WMAP_BLACK);
        }
      }
    }
    // Centre dot
    for (int dy = -2; dy <= 2; dy++) {
      for (int dx = -2; dx <= 2; dx++) {
        if (dx * dx + dy * dy <= 4) {
          int px = cx + dx, py = cy + dy;
          if (px >= 0 && px < _w && py >= 0 && py < _viewportH)
            _lgfx->drawPixelRaw(px, py, WMAP_BLACK);
        }
      }
    }
  }

  // ==========================================================================
  // Crosshair at viewport centre
  // ==========================================================================
  void renderCrosshair() {
    if (!_lgfx) return;

    int cx = _w / 2;
    int cy = _viewportH / 2;
    int len = markerRadius() + 2;

    for (int x = cx - len; x <= cx + len; x++) {
      if (x >= 0 && x < _w) {
        if (cy - 1 >= 0)          _lgfx->drawPixelRaw(x, cy - 1, WMAP_WHITE);
        if (cy + 1 < _viewportH)  _lgfx->drawPixelRaw(x, cy + 1, WMAP_WHITE);
        _lgfx->drawPixelRaw(x, cy, WMAP_BLACK);
      }
    }
    for (int y = cy - len; y <= cy + len; y++) {
      if (y >= 0 && y < _viewportH) {
        if (cx - 1 >= 0)     _lgfx->drawPixelRaw(cx - 1, y, WMAP_WHITE);
        if (cx + 1 < _w)     _lgfx->drawPixelRaw(cx + 1, y, WMAP_WHITE);
        _lgfx->drawPixelRaw(cx, y, WMAP_BLACK);
      }
    }
  }

  // ==========================================================================
  // Footer -- zoom level + on-screen +/- zoom buttons
  // ==========================================================================
  void renderFooter(DisplayDriver& display) {
    int footerY = _h - WMAP_FOOTER_H;

    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(0, footerY - 1, _w, 1);

    // Left: zoom level
    char left[8];
    snprintf(left, sizeof(left), "Z%d", _zoom);
    display.setCursor(1, footerY + 4);
    display.print(left);

    // Right: [-] [+] buttons
    _btnW = 18;
    _btnH = WMAP_FOOTER_H - 2;
    _btnY = footerY + 1;
    _plusX  = _w - _btnW - 1;
    _minusX = _plusX - _btnW - 2;

    display.setColor(DisplayDriver::LIGHT);
    display.drawRect(_minusX, _btnY, _btnW, _btnH);
    display.drawRect(_plusX,  _btnY, _btnW, _btnH);
    display.setCursor(_minusX + 6, _btnY + 3);
    display.print("-");
    display.setCursor(_plusX + 5, _btnY + 3);
    display.print("+");
  }
};
