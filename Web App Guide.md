# Web Reader & IRC - Meck v0.9.5

Press **B** from the home screen to open the web reader. The web reader is
available on the BLE and 4G variants. It is excluded from the standalone audio
variant to preserve zero-radio-power design.

The web reader home screen provides access to the **IRC client**, the **URL
bar**, your **bookmarks**, and browsing **history**. Use **W / S** to navigate
the list and **Enter** to select an item.

## Web Browser

A text-centric web browser ("reader mode") that fetches pages over WiFi,
strips HTML to readable text, extracts links as numbered references, and
paginates content for the e-ink display. Still very much in development, but
already useful for text-heavy websites.

Includes basic web search via **DuckDuckGo Lite** — type a search query into
the URL bar and it will be sent to DuckDuckGo.

### EPUB Downloads

If you follow a link to an `.epub` file, it will be saved directly to the
`/books/` folder on your SD card. You can then read it in the e-book reader
(press **E** from the home screen).

### Bookmarks

Press **K** while on a page to save a bookmark. Bookmarks appear on the web
reader home screen below the URL bar. To delete a bookmark, open the browser
home screen, scroll down to the bookmark, and press **Delete**.

### Cookies & History

Press **X** to clear cookies and browsing history.

---

## IRC Client

The IRC client lets you connect to IRC networks directly from the device. It
is accessed from the web reader home screen — select **IRC Chat** (the first
item) and press **Enter**.

If you are not currently connected, the IRC setup screen opens where you can
configure the server, port, nickname, and channel. If you are already
connected, you go straight to the chat view.

### IRC Setup

The setup screen has five fields. Use **W / S** to navigate between them and
press **Enter** to edit a field (type the value, then **Enter** to confirm).

| Field | Description | Default |
|-------|-------------|---------|
| Host | IRC server hostname (e.g. `irc.libera.chat`) | — |
| Port | Server port. Use `6697` for TLS or `6667` for plain | 6697 |
| Nick | Your IRC nickname (max 16 characters) | — |
| Channel | Channel to join, including the `#` (e.g. `#meshcore`) | — |
| Connect | Select and press Enter to connect | — |

TLS is used automatically when the port is 6697. Other ports connect without
encryption.

Configuration is saved to the SD card at `/web/irc.cfg` and restored on next
launch, so you only need to enter server details once.

If WiFi is not connected when you press Connect, you'll be taken to the WiFi
setup screen first.

### IRC Chat View

Once connected and joined to the channel, you'll see messages in a scrollable
chat view. The channel name and connection status are shown at the top.

| Key | Action |
|-----|--------|
| Enter | Start composing a message (type, then Enter to send) |
| Backspace | Delete last character while composing; exit compose if empty |
| W / S | Scroll up (older) / down (newer) through messages |
| X | Disconnect from IRC and return to web reader home |
| Q | Return to web reader home (connection stays alive in background) |

The IRC connection remains active when you press **Q** to go back to the web
reader home screen. You'll see the connection status and channel name displayed
on the IRC Chat line. Select it and press Enter to return to the chat. Press
**X** from the chat view to disconnect.

The client automatically reconnects if the connection drops (10-second delay
between attempts) and detects dead connections after 5 minutes of inactivity
via ping timeout.

Messages are stored in a circular buffer of 64 messages. Older messages are
discarded as new ones arrive.

---

## Key Bindings

### From Home Screen
| Key | Action |
|-----|--------|
| `b` | Open web reader |

### Web Reader - Home View
| Key | Action |
|-----|--------|
| `w` / `s` | Navigate up/down in IRC / URL bar / bookmarks / history |
| `Enter` | Select IRC Chat, activate URL bar, or open bookmark/history item |
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
| `x` | Clear cookies and history |
| `q` | Back to web reader home |

### Web Reader - WiFi Setup
| Key | Action |
|-----|--------|
| `w` / `s` | Navigate SSID list |
| `Enter` | Select SSID / submit password / retry |
| Type | Enter WiFi password |
| `q` | Back |

### IRC - Setup View
| Key | Action |
|-----|--------|
| `w` / `s` | Navigate fields (Host / Port / Nick / Channel / Connect) |
| `Enter` | Edit selected field, or connect (when on Connect button) |
| Type | Enter field value (when editing) |
| `Backspace` | Delete last character (when editing) |
| `q` | Back to web reader home |

### IRC - Chat View
| Key | Action |
|-----|--------|
| `Enter` | Start composing / send message |
| `Backspace` | Delete character / exit compose if empty |
| `w` / `s` | Scroll older / newer messages |
| `x` | Disconnect and return to web reader home |
| `q` | Back to web reader home (stays connected) |

---

## WiFi

The web reader and IRC client both use WiFi for network access. On first use,
you'll be taken to the WiFi setup screen to scan for networks and enter a
password. Credentials are saved to `/web/wifi.cfg` on the SD card and used for
auto-reconnect on subsequent launches.

On the 4G variant, the web reader currently uses WiFi. A future update will add
PPP support via the A7682E cellular modem, allowing the browser and IRC to work
over cellular data without WiFi.

---

## SD Card Structure
```
/web/
  wifi.cfg         - Saved WiFi credentials (auto-reconnect)
  bookmarks.txt    - One URL per line
  history.txt      - Recent URLs, newest first
  irc.cfg          - IRC server/port/nick/channel config
```

---

## Conditional Compilation
All web reader code is wrapped in `#ifdef MECK_WEB_READER` guards. The flag is set:
- **meck_audio_ble**: Yes (`-D MECK_WEB_READER=1`) — WiFi available via BLE radio stack
- **meck_4g_ble**: Yes (`-D MECK_WEB_READER=1`) — WiFi now, PPP via A7682E in future
- **meck_4g_standalone**: Yes (`-D MECK_WEB_READER=1`) — WiFi works better without BLE (no teardown needed, more free heap)
- **meck_audio_standalone**: No — excluded to preserve zero-radio-power design