# Media Macropad (v1) Design

Date: 2026-10-09
Branch: `basicMacropad`

## Goal

Use the JC3248W535EN (ESP32-S3, 320x480 AXS15231B touch LCD) as a USB media
macropad. The PlatformIO/ESP-IDF setup comes from
https://github.com/NorthernMan54/JC3248W535EN (validated: builds, flashes and
boots on the target board; touch verified by serial log).

## Decisions

| Topic | Decision |
|---|---|
| Transport | USB HID (consumer control) through TinyUSB (`esp_tinyusb`) |
| Controls | Prev, Play/Pause, Next, Vol-, Mute, Vol+, brightness slider |
| Feedback from host | None. One-way: touch sends keys. No host software. |
| Approach | Trim the demo to one screen and add HID. Keep the board code unchanged. |

## Verified facts

- USB-C enumerates as `303a:1001` (USB-Serial-JTAG), so it uses native USB pins GPIO19/20.
- Display and touch use GPIO 1, 4, 8, 21, 38, 39, 40, 45, 47, 48 (`src/esp_bsp.h`). Nothing in `src/` uses GPIO19/20.
- The demo rotation is 90 degrees, so the UI area is 480x320 landscape.
- Touch gives full press/move/release sequences with coordinates (serial log, 2026-10-09).
- Flash use of the stock demo is 70.2% of the 1 MB app partition.

## Hardware checks

Verified (board docs `docs/GPIO_pinout.jpg`, `docs/ESP32_schematic.jpg` in the upstream repo, and `src/esp_bsp.c`):

1. BOOT switch (IO0 to GND) and RST switch exist on the board. Download mode after TinyUSB takes over GPIO19/20 is possible.
2. Connector P1 is `+5VIN, TXD, RXD, GND` (U0TXD/U0RXD). Logs can go to UART0.
3. `bsp_display_brightness_set(percent)` uses LEDC PWM, 10-bit duty. The slider can call it directly.
4. Schematic routes USB-DN/USB-DP to IO19/IO20.

Still unverified (check in the plan):

1. The BOOT button is reachable on the finished board (the user checks the device).
2. The host OS maps the HID usages to the user's media player.

## Architecture

- Keep: `esp_bsp.*`, `lv_port.*`, `esp_lcd_axs15231b.*`, `esp_lcd_touch.*`, `lv_conf.h`.
- Replace the `lv_demo_widgets()` call in `src/DEMO_LVGL.c` with one landscape screen:
  a 3x2 grid of buttons (Prev, Play/Pause, Next / Vol-, Mute, Vol+) and a brightness slider.
- New `src/hid.c`, `src/hid.h`: `hid_send(usage)` sends one consumer-control press and release.
  Usages: Play/Pause 0xCD, Next 0xB5, Prev 0xB6, Mute 0xE2, Vol+ 0xE9, Vol- 0xEA.

## Behaviour

- Button press calls `hid_send` from the LVGL event callback.
- Vol+ and Vol- repeat while held.
- The slider sets the backlight level.
- After an idle timeout the backlight turns off. The first touch wakes it and sends no key.
- Out of scope: now-playing info, pages or layers, Bluetooth.

## Error handling

- HID not mounted (`tud_mounted()` false): drop the press, log it, show "USB not connected".
- Each send is press, short delay, release. A failed release is retried so no key stays down.
- Touch or display init failure: log the error and halt visibly.

## Testing

1. Host side: `evtest` / `libinput debug-events` on the new HID device. Each button must give the matching key event (`KEY_PLAYPAUSE`, `KEY_NEXTSONG`, `KEY_PREVIOUSSONG`, `KEY_MUTE`, `KEY_VOLUMEUP`, `KEY_VOLUMEDOWN`).
2. Visual: capture the display with the Dell WB7022 webcam (`/dev/video4`, `ffmpeg`) to check layout and pressed state. Dimming and wake are judged by comparing before and after frames. Firefox held the camera during brainstorming, so it must be released first.
3. Touch: the user taps while host input events are watched.

## Flashing workflow

After TinyUSB, auto-reset flashing does not work. Decided workflow: use the flash-mode gesture
first (see addendum), with BOOT-at-plug as the fallback. The README documents both.
`--before usb_reset` was not needed.

## Repo notes

- LVGL comes from `lib_deps = lvgl/lvgl@8.4.0` (not vendored; the upstream copy was 157 MB). The registry package does not build `demos/`, so the stock `lv_demo_widgets()` call was replaced by a plain label in the base commit.
- `LOG_LOCAL_LEVEL` was lowered to INFO. `build_type` remains debug.

## Addendum (2026-10-09): flash-mode gesture

Reason: once TinyUSB owns the USB pins, flashing needs BOOT at plug-in, and the user finds the
BOOT/RST buttons very hard to press. Decision: holding the USB status label for 3 seconds
shows "Entering flash mode..." and reboots into ROM download mode
(`RTC_CNTL_FORCE_DOWNLOAD_BOOT` then `esp_restart()`), so `pio run -t upload` works with no
button. New files `src/flash_mode.h`, `src/flash_mode.c`; the hold detection lives in `src/ui.c`.
Hardware result: the gesture mode is `303a:0009` (ROM USB-OTG, "ESP32-S3"), and the board
returns as `303a:4004` after flashing. A flash from BOOT-at-plug mode and the RST button can
leave a stale `303a:1001` entry until the cable is replugged.
