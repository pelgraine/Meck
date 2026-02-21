# Web Reader - Integration Summary

### Conditional Compilation
All web reader code is wrapped in `#ifdef MECK_WEB_READER` guards. The flag is set:
- **meck_audio_ble**: Yes (`-D MECK_WEB_READER=1`) — WiFi available via BLE radio stack
- **meck_4g_ble**: Yes (`-D MECK_WEB_READER=1`) — WiFi now, PPP via A7682E in future
- **meck_audio_standalone**: No — excluded to preserve zero-radio-power design

### 4G Modem / PPP Support
The web reader uses `isNetworkAvailable()` which checks both WiFi and (future) PPP connectivity. The `fetchPage()` method uses ESP32's standard `HTTPClient` which routes through whatever network interface is active — WiFi or PPP.

When PPP support is added to the 4G modem driver, the web reader will work over cellular automatically without code changes. The `isNetworkAvailable()` method has a `TODO` placeholder for the PPP status check.

---

## Key Bindings

### From Home Screen
| Key | Action |
|-----|--------|
| `b` | Open web reader |

### Web Reader - Home View
| Key | Action |
|-----|--------|
| `w` / `s` | Navigate up/down in bookmarks/history |
| `Enter` | Select URL bar or bookmark/history item |
| Type | Enter URL (when URL bar is active) |
| `q` | Exit to firmware home |

### Web Reader - Reading View
| Key | Action |
|-----|--------|
| `w` / `a` | Previous page |
| `s` / `d` / `Space` | Next page |
| `l` or `Enter` | Enter link selection (type link number) |
| `g` | Go to new URL (return to web reader home) |
| `k` | Bookmark current page |
| `q` | Back to web reader home |

### Web Reader - WiFi Setup
| Key | Action |
|-----|--------|
| `w` / `s` | Navigate SSID list |
| `Enter` | Select SSID / submit password / retry |
| Type | Enter WiFi password |
| `q` | Back |

---

## SD Card Structure
```
/web/
  wifi.cfg         - Saved WiFi credentials (auto-reconnect)
  bookmarks.txt    - One URL per line
  history.txt      - Recent URLs, newest first
```