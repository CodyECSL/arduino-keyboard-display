# macropadv2

Macropad on the JC3248W535EN (ESP32-S3, 3.5" 320x480 touch LCD). It shows as a USB keyboard
with media keys. Swipe left and right between six pages (Media, Apps, AI, Game - LabEscape, Game - Crossy Parkway, Settings) (the dots at the bottom show which).
Swiping past the last page wraps to the first, and the other way round. The USB status at the
bottom-left shows on every page; hold it for 3 seconds to enter flash mode.

- **Media:** Prev, Play/Pause, Next, Vol-, Mute, Vol+ (Vol +/- repeat while held).
- **Apps:** Copy, Paste, Screenshot, Undo, Redo, Switch (task switcher). The shortcuts depend on the Host OS setting.
- **AI Mode:** tap the key to open a terminal and run `claude -p "Create my website and make
  no mistakes"`. Linux: Ctrl+Alt+T. Mac: Cmd+Space, `terminal`, Enter. Tapping again while it runs does nothing.
- **Game - LabEscape:** an endless jetpack flyer, "LAB ESCAPE". A demo plays behind the title and
  "Tap to Start". Tap to start; press and hold to fly up, let go to fall. Dodge zappers and missiles. 10 coins make
  a shield bubble (up to 3); a hit costs one shield and gives 1 s of invincible time. The score is the distance in
  meters. The best score is stored in flash (NVS) and survives power cycles and normal flashes. Page swipes are
  locked during a run; swipe away from the page in the demo and on the Game Over panel. See "Game - LabEscape" below.
- **Game - Crossy Parkway:** an isometric hopping game. Tap to hop forward; the same tap hops diagonally
  around a tree or rock. Cross grass, roads, rivers (ride logs, use lily pads) and rails (watch the signal), and collect
  coins. Waiting never kills the bird. Page swipes are locked during a run. The best
  distance and the coin total are kept. See "Game - Crossy Parkway" below.
- **Settings:** Host OS (Linux or Mac, default Linux), brightness slider (5 to 100 %), screensaver timeout
  (5 s to 5 min, 11 steps, default 30 s) and the screensaver video (any `*.mjpeg` file in `/mjpeg/` on the SD card). Games shows the best score of Game - LabEscape and of Game - Crossy Parkway, each with a
  "Reset High Score" button that asks first (the Crossy Parkway coin total stays).

All Settings values are stored in flash (NVS) and survive power cycles and normal flashes
(`pio run -t upload`). A full chip erase resets them. If the stored video is gone, the firmware
uses `SAVER.mjpeg`, then the first `*.mjpeg` file on the card.

After the screensaver timeout with no touch, a video screensaver plays (see below). The first touch
stops it without sending a key.

The icon source is `tools/assets/claude_icon.svg`. Regenerate the image with
`python3 tools/make_icon.py tools/assets/claude_icon.svg src/claude_icon.c --size 120`.

Base setup from https://github.com/NorthernMan54/JC3248W535EN (ESP-IDF 5.2.1, LVGL 8.4.0).

## Screensaver

The screensaver plays the chosen video from `/mjpeg/` on the SD card, in a loop (default `SAVER.mjpeg`). If there is no card or no
video, the screen goes dark after the timeout instead (backlight off).

The file is a raw stream of baseline 4:2:0 JPEG frames, 480x320, the same format as the mjpeg files
that came with the device. Make one with ffmpeg through the helper script:

    python3 tools/make_screensaver.py INPUT.mp4 SAVER.mjpeg --fps 30 --quality 2

Copy the result to the card (FAT32) in `/mjpeg/`. Pick it on the Settings page. The card is read at boot, so
power-cycle the board after you insert it or add files; file names are listed each time you open the Settings page.

- The stream has no timing. `--fps` must match `TARGET_FPS` in `src/screensaver.cpp` (30).
- The decoder is Espressif's `esp_new_jpeg` (ESP32-S3 optimized). It rotates the frame 270 degrees while it
  decodes. Espressif lists 36 fps for 480x320 4:2:0 at JPEG quality 60. Not measured on this board with
  other settings: `--quality 2` at 30 fps (about 19 KB per frame on a 1080p beach video) plays smoothly.
- Frames larger than 64 KB (`MAX_FRAME`) are skipped. `--quality 1` gives about twice the file size for a small gain.
- The panel takes RGB565 (65k colours), so smooth gradients can show banding at any JPEG quality.
- `esp_new_jpeg` adds `-Wl,--start-group`/`--end-group` to the link line. PlatformIO sorts the flags and breaks
  the pair, so `tools/strip_link_groups.py` removes them.
- `--speed` makes the motion faster (time-lapse). A mostly still scene looks better at 2x or more.
- The SD card is wired as SPI on GPIO 10 (CS), 11 (MOSI), 12 (SCLK), 13 (MISO). These pins come
  from a third-party board page, not from a schematic, and they work on this board.

## Game - LabEscape

The rules are in `src/game_logic.c` (no LVGL, so a PC can test them); the page is `src/game.c`. The art is original and
is drawn by `tools/make_game_assets.py`, which writes `src/game_assets.c` and `src/game_assets.h`
(`python3 tools/make_game_assets.py`; needs `rsvg-convert` and Pillow; `--preview DIR` writes PNGs). Test the rules
without the board with `tests/host/run.sh`.

The app slot is 3 MB (`partitions_macropad.csv`): the art needs the room. NVS keeps its address, so stored
settings and the best score survive the change.

### Display speed

The game redraws the whole screen on every frame. Measured on the board with the real game (no overlays), it went from
6-7 updates per second to about 25:

- The CPU runs at 240 MHz and the firmware is built with `-O2` (it was 160 MHz and `-Og`).
- The 90 degree rotation in `lv_port.c` walks pointers, not indexes (28 ms down to about 10 ms per frame).
- LVGL has two full-screen buffers in PSRAM. A separate flush task on core 0 rotates and sends one frame while LVGL
  (core 1) draws the next.
- While the Game - LabEscape page is up, the screen's own background fill is switched off (`ui_set_background`), because the wall
  covers it. A swipe switches it back on.

Do not try partial updates (strips or dirty areas). The panel driver `esp_lcd_axs15231b.c` writes a stream of
full-width rows and never sets the row address, so anything else corrupts the picture.

## Game - Crossy Parkway

An isometric hopping game ("CROSSY PARKWAY" on the title card). It has one control: tap to hop forward. If a tree,
rock or bush blocks the lane ahead, the same tap hops diagonally forward around it. There are no swipes. Cross grass,
roads, rivers (ride logs, use lily pads) and rails (watch the signal), and collect coins. Waiting never kills the bird. The best distance and the coin total are stored in flash.

The rules are in `src/crossy_logic.c` (no LVGL, so a PC can test them with `tests/host/run.sh`). The scene is drawn by a
hand-written renderer in `src/crossy_ground.c`. The view and the input are in `src/crossy.c`. All art is original voxel
art drawn by `tools/crossy_art.py` and baked to `src/crossy_assets.c` by `tools/make_crossy_assets.py`. That script also
writes a contact sheet with `--sheet FILE.png`.

The app slot is 3 MB (`partitions_macropad.csv`). NVS keeps its address, so stored settings and best scores survive.

### Display speed

This page uses the same measures as Game - LabEscape (240 MHz, `-O2`, two PSRAM buffers with a flush task, screen
background fill off while the page is up). It adds two more:

- The sprite pixels are copied to PSRAM at start.
- The whole world (ground and sprites) is drawn by one custom draw callback with a hand-written scanline renderer, not
  one LVGL image object per sprite. LVGL's chroma-key image drawing cost about 90 ns per pixel.

A frame went from about 54 ms to about 35 ms. Measured on the board: 26 updates per second, the same as Game - LabEscape.

## Wi-Fi debug link

Log lines and screenshots over Wi-Fi, so you do not need the UART or the on-screen debug text.

1. Settings > Wi-Fi. Turn **Wi-Fi debug** on. The board restarts (Wi-Fi can only start early in the boot, see below).
2. Tap **Scan**, pick your network in the list, tap **Connect**, type the password, tap the OK key.
3. The page shows the IP and `macropad.local`. The saved network and the switch survive power cycles; the board
   reconnects by itself at boot.

From the PC:

    curl -N http://macropad.local/log                    # the last 4 KB of log, then new lines (one client at a time)
    curl -o shot.bmp http://macropad.local/shot          # 480 x 320, 24 bit BMP of the screen

Limits:
- No password or other protection: anyone on the same network can read the log and the screen while it is on.
- `/shot` draws the active screen, not the top layer: the password overlay, the flash-mode picture and the wake fade
  are missing from the picture.
- The first request after a boot (or after a long idle time) can fail once. Run it again.
- The log has ANSI colour codes. Strip them with `sed 's/\x1b\[[0-9;]*m//g'`.
- Wi-Fi needs about 80 KB of internal RAM in one block. After the boot the display, the UI and the screensaver hold
  it, so Wi-Fi starts before the screensaver and the screensaver's JPEG buffer moves to PSRAM when Wi-Fi is on
  (slower decode). With Wi-Fi on about 42 KB of internal RAM stay free (minimum 33 KB). The static Wi-Fi buffers
  are cut to 6 + 6 and power save is off for this reason.

### Debug build (logs over USB, flashing without a gesture)

    ~/.venvs/platformio/bin/pio run -e LVGL-320-480-dbg -t upload --upload-port /dev/ttyACM0

This build has no HID (the keys do nothing). The USB port is the chip's USB-Serial-JTAG (`303a:1001`, `/dev/ttyACM0`):
it prints the ESP log and crash backtraces, and flashing works without any gesture. After a crash it stops on purpose;
replug the cable. Decode a backtrace with
`~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-addr2line -pfiaC -e .pio/build/LVGL-320-480-dbg/firmware.elf <addresses>`.
From this build you can flash the normal one (`-e LVGL-320-480`) without a gesture too.

## Build

    python3 -m venv ~/.venvs/platformio && ~/.venvs/platformio/bin/pip install platformio
    ~/.venvs/platformio/bin/pio run

## Flash

The USB-C port is the keys port, so auto-reset flashing does not work.

Preferred: swipe to the Settings page and hold the "USB connected" status label for 3 seconds. The board shows
a flash-mode picture (cable to laptop) and reboots into download mode (USB id `303a:1001`, or `303a:0009` on older firmware). Then:

    ~/.venvs/platformio/bin/pio run -t upload --upload-port /dev/ttyACM0

After flashing, the board comes back as `303a:4004 macropadv2 Macropad` on its own. `tools/clear_force_download.py` (a PlatformIO post-upload step) clears the force-download bit that the gesture sets, so the next reset boots the app.

To leave download mode without flashing, replug the USB cable.

Fallback (first flash or a crashed firmware): unplug USB-C, hold BOOT, plug in while holding BOOT,
release after about 2 seconds, then upload as above. (Hold BOOT and tap RST also works but is
harder.) After a flash from BOOT-at-plug mode the computer may keep a stale `303a:1001` entry
instead of `303a:4004 macropadv2 Macropad`. Replug the USB cable.

Known limitation: pressing the RST button or a flash from BOOT-at-plug mode can leave the host with a stale `303a:1001` entry.
Replug the cable.

## Logs

Logs go to UART0 (connector P1: +5V, TXD, RXD, GND), 115200 baud.

## Check keys on Linux

Run the reader from the repo root, for example:

    sudo python3 tools/read_hid_events.py --seconds 20
    tools/snap_display.sh out.jpg   # webcam picture of the display
