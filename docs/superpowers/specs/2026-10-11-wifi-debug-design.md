# Wi-Fi debug link: design

Date: 2026-10-11. Branch: `CodyECSL/wifi-debugging`.

## Goal

Let the PC read the board's log lines and take a screenshot of the display over Wi-Fi. Today the USB port runs HID only (`CONFIG_TINYUSB_CDC_ENABLED=n`), so there is no serial log, and display checks need on-screen text or a camera. Wi-Fi OTA flashing is out of scope (later spec; it needs a two-slot partition table).

## Decisions

| Topic | Decision |
|---|---|
| Wi-Fi on/off | Switch in Settings, stored in NVS. Off by default. |
| Credentials | Typed on the touchscreen, stored in NVS. Nothing in git. |
| Access control | None. Anyone on the same network can read while the switch is on. |
| Discovery | mDNS name `macropad.local`; the Wi-Fi page also shows the IP. |
| Transport | Board is an HTTP server on port 80. One server for logs and screenshots. |

## Files

| File | Purpose |
|---|---|
| `src/wifi_debug.c/.h` | Station connect, mDNS, HTTP server, log hook. `wifi_debug_start()`, `wifi_debug_stop()`, `wifi_debug_status()`. |
| `src/log_ring.c/.h` | Fixed-size ring buffer of log text. No ESP-IDF calls. Host-testable. |
| `src/settings.c/.h` | New NVS keys: `wifi_en`, `wifi_ssid`, `wifi_pass`. |
| `src/ui.c` | "Wi-Fi" entry in the Settings sidebar and its sub-page. |
| `tests/host/test_log_ring.c` | Host tests. Added to `tests/host/run.sh`. |
| `src/CMakeLists.txt`, `sdkconfig.defaults` | Components (`esp_wifi`, `esp_http_server`, `mdns`) and Wi-Fi/lwIP options. |

## Wi-Fi page (Settings > Wi-Fi)

- Switch: on/off. The state is saved; at boot the firmware starts Wi-Fi only if the switch was on.
- Network list from a scan (button "Scan"). Tap a network to pick the SSID; an `lv_keyboard` and a text field take the password. "Connect" saves both and connects.
- Status line: `Off`, `Connecting...`, `Connected  192.168.x.x  macropad.local`, or `Failed: <reason>`.
- Page code stays small: it calls `wifi_debug_*` and `settings_*` only.

## Log stream

- `wifi_debug_start()` installs `esp_log_set_vprintf()`. The hook formats each line, calls the original vprintf (so the UART console still works), and appends the text to the ring (4 KB).
- `GET /log` sends a chunked text response. It first sends the current ring content, then new text as it arrives, until the client disconnects. One client at a time; a second client gets HTTP 503.
- When no client is connected the ring simply overwrites its oldest text. The hook never blocks and never allocates.
- `wifi_debug_stop()` restores the original vprintf.

## Screenshot

- `GET /shot` takes `lv_snapshot_take()` of the active screen with the LVGL lock held, converts it, sends it, and frees the buffer.
- Format: BMP, 24 bit or 16 bit RGB565 with bitfield header. No encoder needed; the PC can read it with any viewer. The exact variant is chosen during implementation, by what Pillow and the Read tool open correctly.
- The snapshot buffer (about 300 KB at 320 x 480 RGB565) is allocated in PSRAM, and only for the request. If the allocation fails, the server answers HTTP 503.
- The game pages draw inside an LVGL draw callback, so the snapshot should include them. This is not yet verified on the board.

## Risks, measured before relying on the feature

1. Free internal RAM after Wi-Fi start (`heap_caps_get_free_size`), with LVGL and a game page up.
2. Game update rate with Wi-Fi on. It was 26 updates/s without. Target: no visible change.
3. USB HID and Wi-Fi at the same time (radio/USB interference on the S3 is not expected, but I have not tested it here).
4. A snapshot while a game runs: time taken, and any display glitch.
5. Snapshot content for the game pages (see above).

If risk 1 or 2 fails, the fallback is to stop the log hook buffer and use a smaller Wi-Fi buffer configuration, or to run the game paused during `/shot`.

## Testing

- Host: `log_ring` (append, overflow wraps oldest text, read from an offset, empty ring).
- Board, by the user once: switch Wi-Fi on, pick the network, type the password.
- Board, by Claude: `curl -N http://macropad.local/log` shows lines while the user presses keys; `curl -o shot.bmp http://macropad.local/shot` and compare with `tools/snap_display.sh` (camera) on a known page.
- Failure cases: wrong password (status shows Failed), router off (reconnect with back-off), switch off (server and radio stop; RAM returns).

## Measured (board test, 2026-10-11)

1. Free internal RAM: at boot 265 KB; the display takes 84 KB, the UI 36 KB, the screensaver 94 KB. Starting Wi-Fi after the screensaver failed (`ESP_ERR_NO_MEM`, 43 KB left). Wi-Fi now starts before the screensaver; with HID, static Wi-Fi buffers 6 + 6 and power save off, 42 KB stay free after connecting (minimum 33 KB). **Fail first, fixed.** Wi-Fi cannot start at runtime: the switch saves the setting and restarts the board.
2. Game speed with Wi-Fi on: not measured as a number. The user played both games with Wi-Fi on and saw no slowdown; the games lag only while a `/shot` or a `/log` request is running (the user's report).
3. USB HID with Wi-Fi on: the board enumerates as `303a:4004` and the status label shows "USB connected" with Wi-Fi up and during `/shot`. The user confirms the HID keys work with Wi-Fi on; the `tools/read_hid_events.py` test was not run (the user did not ask for it).
4. `/shot` time during a game: 0.75 to 1.1 s each (6 of 6 per game); 2 to 5 s with the debug build. No error. Whether the display flickered was not reported.
5. Snapshot content of game pages: LabEscape (hero, rockets, coins, zapper, score) and Crossy Parkway (isometric road, river, trains, score; and the title card) show correctly. **Pass.**
6. Also found on the board: the LVGL task stack of 4096 bytes overflowed on the Wi-Fi page (free stack 856 to 40 bytes, then a corrupted mutex and a crash after 10-20 s). It is 6144 bytes now; the page peaks at about 4.1 KB.
7. Known: the first `/shot` after a boot or a long idle time can fail once (httpd send timeout of 1 s). 20 rounds of `/log` for 2 s followed by `/shot` gave 20 valid BMPs.

## Not in scope

- OTA flashing, touch injection, authentication, TLS, multiple clients, ring buffer persistence across reboots.
- Changes to any existing page except adding the Settings entry.
