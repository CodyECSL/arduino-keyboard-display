# Media Macropad v1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Turn the JC3248W535EN touch board into a USB media macropad: 6 touch buttons (Prev, Play/Pause, Next, Vol-, Mute, Vol+), a brightness slider, and idle backlight sleep.

**Architecture:** Keep the working board code (`esp_bsp.*`, `lv_port.*`, AXS15231B driver). Add three small modules: `hid` (TinyUSB consumer-control keys, queue + task), `ui` (one LVGL screen), `power` (brightness level + idle sleep). `DEMO_LVGL.c` only wires them together.

**Tech Stack:** ESP-IDF 5.2.1 through PlatformIO (`espressif32@6.6.0`), LVGL 8.4.0, `espressif/esp_tinyusb` 2.x (managed component), C.

**Spec:** `docs/superpowers/specs/2026-10-09-media-macropad-design.md`

## Global Constraints

- Transport is USB HID consumer control. No Bluetooth. No host software. One-way only.
- Controls: Prev, Play/Pause, Next, Vol-, Mute, Vol+, brightness slider. Vol+ and Vol- repeat while held.
- Display is 480x320 landscape (`LVGL_PORT_ROTATION_DEGREE 90`).
- Usages: Play/Pause 0xCD, Next 0xB5, Prev 0xB6, Mute 0xE2, Vol+ 0xE9, Vol- 0xEA.
- Do not change `esp_bsp.*`, `lv_port.*`, `esp_lcd_axs15231b.*`, `esp_lcd_touch.*`, `lv_conf.h`.
- Work on branch `basicMacropad`. Never commit to `main`.
- Commit messages end with a second `-m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"`.
- Backlight function is `bsp_display_brightness_set(int percent)` (declared in `src/display.h`). The spec text that says `bsp_display_backlight_set` is wrong; Task 4 fixes it.

## Environment (read first)

- Board port: `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_<MAC>-if00` (usually `/dev/ttyACM0`). User is in group `uucp`, so no sudo is needed for flashing.
- PlatformIO is not installed system-wide. Install once, outside the repo:

```bash
python3 -m venv ~/.venvs/platformio && ~/.venvs/platformio/bin/pip install -q platformio
export PIO=~/.venvs/platformio/bin/pio
```

  Use `$PIO` in every command below. (The earlier session used a temporary venv that may be gone.)
- **Flashing after Task 1.** Once the firmware runs TinyUSB, the port `303a:1001` disappears and auto-reset flashing stops. To flash: hold BOOT, press and release RST, release BOOT. Check `lsusb | grep 303a:1001`, then run the `pio run -t upload` command. If `/dev/ttyACM0` changed, use the path in `/dev/serial/by-id/`. The first flash in Task 1 does not need this, because the old firmware still has USB-JTAG.
- **Camera:** Dell WB7022. Firefox may hold it. If `ffmpeg` says "Device or resource busy", run `fuser -v /dev/video4` and ask the user to close the camera use. Never kill the user's browser.
- **Host key events** need read access to `/dev/input/event*` (group `input`; the user is not in it). Run `tools/read_hid_events.py` with `sudo`. Ask the user to run it: `! sudo python3 tools/read_hid_events.py --seconds 20`.
- **Warning for the user:** test presses change the real volume and play state on this computer.

## File Structure

| File | Responsibility |
|---|---|
| `src/idf_component.yml` | Declares the `espressif/esp_tinyusb` dependency (Task 1) |
| `src/CMakeLists.txt` | Build only `*.c`; require `esp_tinyusb` (Task 1) |
| `src/hid.h`, `src/hid.c` | `media_key_t`, `hid_init`, `hid_send`, `hid_is_mounted`. Owns the USB descriptors, a queue and a task. (Task 1) |
| `src/ui.h`, `src/ui.c` | `ui_create()`: buttons, slider, USB status label (Task 2). Wake handling (Task 3) |
| `src/power.h`, `src/power.c` | `power_init`, `power_set_level`, `power_touch`. Idle sleep (Task 3) |
| `src/DEMO_LVGL.c` | Wiring only: init display, `hid_init`, `ui_create`, `power_init` |
| `sdkconfig.defaults`, `sdkconfig.LVGL-320-480` | TinyUSB options; secondary console off (Task 1) |
| `tools/read_hid_events.py` | Host-side check of key events (Task 1) |
| `tools/snap_display.sh` | Capture one webcam frame of the display (Task 2) |
| `README.md` | Build, flash, recovery (Task 4) |

---

### Task 1: USB HID keys (build, enumerate, host sees keys)

**Files:**
- Create: `src/idf_component.yml`, `src/hid.h`, `src/hid.c`, `tools/read_hid_events.py`
- Modify: `src/CMakeLists.txt`, `src/DEMO_LVGL.c`, `sdkconfig.defaults`, `sdkconfig.LVGL-320-480:1038-1039`, `.gitignore`

**Interfaces:**
- Produces (used by Tasks 2 and 3):
  - `typedef enum { MEDIA_KEY_PLAY_PAUSE = 0xCD, MEDIA_KEY_NEXT = 0xB5, MEDIA_KEY_PREV = 0xB6, MEDIA_KEY_MUTE = 0xE2, MEDIA_KEY_VOL_UP = 0xE9, MEDIA_KEY_VOL_DOWN = 0xEA } media_key_t;`
  - `void hid_init(void);` call once, after `app_main` starts.
  - `bool hid_send(media_key_t key);` returns false if the queue is full. Non-blocking. Drops the key (with a log) if the host is not connected.
  - `bool hid_is_mounted(void);`

**Risk gate:** Step 1 proves PlatformIO can fetch and build a managed component. If Step 3 fails because the component is not downloaded, stop and report to the user. Do not invent a workaround. The fallback is to place `esp_tinyusb` and `tinyusb` sources in a `components/` folder.

- [ ] **Step 1: Create the component manifest and CMake change**

`src/idf_component.yml`:

```yaml
dependencies:
  espressif/esp_tinyusb: "2.0.1"
```

`src/CMakeLists.txt` (replace the whole file; the old glob would pull `idf_component.yml` and headers into `SRCS`):

```cmake
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(SRCS ${app_sources} REQUIRES esp_tinyusb)
```

- [ ] **Step 2: Set sdkconfig**

Append to `sdkconfig.defaults` (options from a new component are missing in `sdkconfig.LVGL-320-480`, so the defaults apply):

```
CONFIG_TINYUSB_HID_COUNT=1
CONFIG_TINYUSB_CDC_ENABLED=n
CONFIG_TINYUSB_MSC_ENABLED=n
```

In `sdkconfig.LVGL-320-480` change these two lines (USB-Serial-JTAG as a secondary console conflicts with TinyUSB on GPIO19/20):

```
# CONFIG_ESP_CONSOLE_SECONDARY_NONE is not set
CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED=y
```
to:
```
CONFIG_ESP_CONSOLE_SECONDARY_NONE=y
# CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG is not set
# CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG_ENABLED is not set
```

- [ ] **Step 3: Write `src/hid.h`**

```c
#pragma once

#include <stdbool.h>

typedef enum {
    MEDIA_KEY_PLAY_PAUSE = 0xCD,
    MEDIA_KEY_NEXT = 0xB5,
    MEDIA_KEY_PREV = 0xB6,
    MEDIA_KEY_MUTE = 0xE2,
    MEDIA_KEY_VOL_UP = 0xE9,
    MEDIA_KEY_VOL_DOWN = 0xEA,
} media_key_t;

/** Start TinyUSB and the HID sender task. Call once. */
void hid_init(void);

/** Queue one key press+release. Non-blocking. Returns false if the queue is full. */
bool hid_send(media_key_t key);

/** True when the USB host has configured the device. */
bool hid_is_mounted(void);
```

- [ ] **Step 4: Write `src/hid.c`**

```c
#include "hid.h"

#include <stdint.h>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"

static const char *TAG = "hid";

#define KEY_QUEUE_LEN 8
#define PRESS_MS 20
#define READY_POLL_MS 5
#define READY_POLL_TRIES 20
#define RELEASE_TRIES 5

static QueueHandle_t s_queue;

/* One 16-bit consumer-control usage per report, no report ID. */
static const uint8_t s_report_desc[] = {TUD_HID_REPORT_DESC_CONSUMER()};

static const char *s_strings[] = {
    (char[]){0x09, 0x04}, /* 0: language = English (0x0409) */
    "macropadv2",         /* 1: manufacturer */
    "Macropad",           /* 2: product */
    "000001",             /* 3: serial */
    "Media Keys",         /* 4: HID interface */
};

#define CFG_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t s_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CFG_DESC_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(s_report_desc), 0x81, 16, 10),
};

/* TinyUSB callbacks required by the HID class. */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

static bool wait_ready(void)
{
    for (int i = 0; i < READY_POLL_TRIES; i++) {
        if (tud_hid_ready()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(READY_POLL_MS));
    }
    return false;
}

static void press(uint16_t usage)
{
    if (wait_ready() && tud_hid_report(0, &usage, sizeof(usage))) {
        return;
    }
    ESP_LOGW(TAG, "press 0x%04x not sent", usage);
}

/* A key must never stay down: retry the release. */
static void release(void)
{
    uint16_t zero = 0;
    for (int i = 0; i < RELEASE_TRIES; i++) {
        if (wait_ready() && tud_hid_report(0, &zero, sizeof(zero))) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "release failed");
}

static void hid_task(void *arg)
{
    (void)arg;
    uint16_t usage;
    for (;;) {
        if (xQueueReceive(s_queue, &usage, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!tud_mounted()) {
            ESP_LOGW(TAG, "host not connected, dropped 0x%04x", usage);
            continue;
        }
        press(usage);
        vTaskDelay(pdMS_TO_TICKS(PRESS_MS));
        release();
    }
}

void hid_init(void)
{
    s_queue = xQueueCreate(KEY_QUEUE_LEN, sizeof(uint16_t));
    configASSERT(s_queue);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    cfg.descriptor.full_speed_config = s_cfg_desc;
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));

    xTaskCreate(hid_task, "hid", 3072, NULL, 5, NULL);
    ESP_LOGI(TAG, "HID started");
}

bool hid_send(media_key_t key)
{
    uint16_t usage = (uint16_t)key;
    return xQueueSend(s_queue, &usage, 0) == pdTRUE;
}

bool hid_is_mounted(void)
{
    return tud_mounted();
}
```

If the compiler reports unknown fields in `tinyusb_config_t` or a different `TINYUSB_DEFAULT_CONFIG` form, open the component header (`find . -name tinyusb.h -path "*esp_tinyusb*"`) and adjust only `hid_init()`. Do not change the descriptors.

- [ ] **Step 5: Add a temporary self-test to `src/DEMO_LVGL.c`**

Add near the other includes: `#include "hid.h"`, `#include "freertos/FreeRTOS.h"`, `#include "freertos/task.h"`.

In `setup()`, before `logSection("Initialize panel device");` add `hid_init();`.

After `logSection("LVGL porting example end");` add (TEMPORARY, removed in Task 2; sends Vol+ then Vol-, net zero change):

```c
  /* TEMPORARY self-test: removed in Task 2 */
  for (int i = 0; i < 100 && !hid_is_mounted(); i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }
  hid_send(MEDIA_KEY_VOL_UP);
  hid_send(MEDIA_KEY_VOL_DOWN);
```

- [ ] **Step 6: Create `tools/read_hid_events.py`**

```python
#!/usr/bin/env python3
"""Print key events from the macropad's HID device. Needs read access to /dev/input (use sudo)."""
import argparse
import select
import struct
import time

KEYS = {113: "MUTE", 114: "VOLUMEDOWN", 115: "VOLUMEUP", 163: "NEXTSONG", 164: "PLAYPAUSE", 165: "PREVIOUSSONG"}
EV_KEY = 1
EVENT = struct.Struct("llHHi")  # 64-bit Linux struct input_event


def find_event_files(name_part):
    found = []
    block = {}
    with open("/proc/bus/input/devices") as f:
        for line in list(f) + ["\n"]:
            if line.strip() == "":
                if name_part in block.get("N", "").lower():
                    for tok in block.get("H", "").split():
                        if tok.startswith("event"):
                            found.append("/dev/input/" + tok)
                block = {}
            elif line[0] in "NH":
                block[line[0]] = line
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seconds", type=float, default=20)
    ap.add_argument("--name", default="macropad")
    args = ap.parse_args()

    paths = find_event_files(args.name.lower())
    if not paths:
        raise SystemExit(f"no input device with '{args.name}' in its name")
    files = [open(p, "rb", buffering=0) for p in paths]
    print("watching", paths, flush=True)
    end = time.time() + args.seconds
    while time.time() < end:
        ready, _, _ = select.select(files, [], [], 0.2)
        for f in ready:
            data = f.read(EVENT.size)
            if len(data) < EVENT.size:
                continue
            _, _, etype, code, value = EVENT.unpack(data)
            if etype == EV_KEY:
                state = {0: "up", 1: "down", 2: "repeat"}.get(value, str(value))
                print(f"KEY_{KEYS.get(code, code)} {state}", flush=True)


main()
```

- [ ] **Step 7: Build**

Run: `$PIO run 2>&1 | tail -20`
Expected: `[SUCCESS]`. Then run `grep -n "TINYUSB_HID_COUNT\|CONSOLE_SECONDARY" sdkconfig.LVGL-320-480`. Expected: `CONFIG_TINYUSB_HID_COUNT=1` and `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`.
If the build prints `fatal error: tinyusb.h: No such file`, the managed component was not fetched: stop (Risk gate).

- [ ] **Step 8: Check `git status` for generated files**

Run: `git status --short`
Add any generated folder (for example `managed_components/`, `dependencies.lock`) to `.gitignore`. Do not commit generated files.

- [ ] **Step 9: Flash (old firmware still has USB-JTAG, so auto-reset works) and check enumeration**

Run: `$PIO run -t upload --upload-port /dev/ttyACM0 2>&1 | tail -4`
Expected: `[SUCCESS]`.
Wait 5 seconds, then run: `lsusb | grep -i "macropad"`
Expected: a line with `macropadv2 Macropad` and a vendor ID that is not `303a:1001`. If the `303a:1001` device is still there, run `dmesg | tail -20` and report.

- [ ] **Step 10: Verify the host sees the keys**

Ask the user to run (the self-test fires at boot, so press the RST button on the board right after starting this):
`! sudo python3 tools/read_hid_events.py --seconds 20`
Expected (order and two pairs):
```
KEY_VOLUMEUP down
KEY_VOLUMEUP up
KEY_VOLUMEDOWN down
KEY_VOLUMEDOWN up
```
If the self-test already ran before the script started, press RST again.

- [ ] **Step 11: Commit**

```bash
git add src/idf_component.yml src/CMakeLists.txt src/hid.h src/hid.c src/DEMO_LVGL.c sdkconfig.defaults sdkconfig.LVGL-320-480 tools/read_hid_events.py .gitignore
git commit -m "Add USB HID consumer-control sender" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Touch UI that sends keys

**Files:**
- Create: `src/ui.h`, `src/ui.c`, `tools/snap_display.sh`
- Modify: `src/DEMO_LVGL.c` (remove the self-test and the label; call `ui_create()`; check display start)

**Interfaces:**
- Consumes: `hid_send(media_key_t)`, `hid_is_mounted()`, `MEDIA_KEY_*` from Task 1; `bsp_display_brightness_set(int)` from `display.h`.
- Produces (used by Task 3): `void ui_create(void);` Call with the LVGL lock held. Task 3 replaces the slider's direct brightness call with `power_set_level`.

- [ ] **Step 1: Write `tools/snap_display.sh`**

```sh
#!/bin/sh
# Usage: tools/snap_display.sh OUTPUT.jpg
# Captures one frame from the Dell WB7022 webcam.
set -e
out="$1"
[ -n "$out" ] || { echo "usage: $0 OUTPUT.jpg" >&2; exit 2; }
dev=""
for d in /sys/class/video4linux/video*; do
  if grep -q "Dell Webcam WB7022" "$d/name" && [ "$(cat "$d/index")" = "0" ]; then
    dev="/dev/$(basename "$d")"
    break
  fi
done
[ -n "$dev" ] || { echo "Dell WB7022 capture node not found" >&2; exit 1; }
ffmpeg -loglevel error -y -f v4l2 -i "$dev" -frames:v 20 -update 1 "$out"
```

Run: `chmod +x tools/snap_display.sh`

- [ ] **Step 2: Write `src/ui.h`**

```c
#pragma once

/** Build the macropad screen. Call with the LVGL lock held (bsp_display_lock). */
void ui_create(void);
```

- [ ] **Step 3: Write `src/ui.c`**

```c
#include "ui.h"

#include <lvgl.h>

#include "display.h"
#include "hid.h"

#define SCREEN_W 480
#define MARGIN 10
#define COLS 3
#define BTN_W ((SCREEN_W - (COLS + 1) * MARGIN) / COLS)
#define BTN_H 110
#define ROW0_Y MARGIN
#define ROW1_Y (MARGIN + BTN_H + MARGIN)
#define SLIDER_Y 262
#define STATUS_Y 292

typedef struct {
    const char *text;
    media_key_t key;
    bool repeat;
} button_def_t;

static const button_def_t s_buttons[] = {
    {LV_SYMBOL_PREV, MEDIA_KEY_PREV, false},
    {LV_SYMBOL_PLAY LV_SYMBOL_PAUSE, MEDIA_KEY_PLAY_PAUSE, false},
    {LV_SYMBOL_NEXT, MEDIA_KEY_NEXT, false},
    {LV_SYMBOL_VOLUME_MID, MEDIA_KEY_VOL_DOWN, true},
    {LV_SYMBOL_MUTE, MEDIA_KEY_MUTE, false},
    {LV_SYMBOL_VOLUME_MAX, MEDIA_KEY_VOL_UP, true},
};

static lv_obj_t *s_status;

static void button_cb(lv_event_t *e)
{
    const button_def_t *def = lv_event_get_user_data(e);
    hid_send(def->key);
}

static void slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bsp_display_brightness_set(lv_slider_get_value(slider));
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    lv_label_set_text(s_status, hid_is_mounted() ? "USB connected" : "USB not connected");
}

static void create_button(lv_obj_t *parent, int index)
{
    const button_def_t *def = &s_buttons[index];
    int col = index % COLS;
    int row = index / COLS;

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_pos(btn, MARGIN + col * (BTN_W + MARGIN), row == 0 ? ROW0_Y : ROW1_Y);
    lv_obj_add_event_cb(btn, button_cb, LV_EVENT_SHORT_CLICKED, (void *)def);
    if (def->repeat) {
        lv_obj_add_event_cb(btn, button_cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *)def);
    }

    lv_obj_t *label = lv_label_create(btn);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_label_set_text(label, def->text);
    lv_obj_center(label);
}

void ui_create(void)
{
    lv_obj_t *scr = lv_scr_act();

    for (int i = 0; i < (int)(sizeof(s_buttons) / sizeof(s_buttons[0])); i++) {
        create_button(scr, i);
    }

    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_size(slider, SCREEN_W - 4 * MARGIN, 20);
    lv_obj_set_pos(slider, 2 * MARGIN, SLIDER_Y);
    lv_slider_set_range(slider, 5, 100); /* never 0: the screen must stay visible */
    lv_slider_set_value(slider, 100, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_status = lv_label_create(scr);
    lv_obj_set_pos(s_status, 2 * MARGIN, STATUS_Y);
    lv_label_set_text(s_status, "USB not connected");
    lv_timer_create(status_timer_cb, 500, NULL);
}
```

- [ ] **Step 4: Wire `src/DEMO_LVGL.c`**

- Add `#include "ui.h"`.
- Delete the temporary self-test block from Task 1 and the `freertos` includes if nothing else uses them.
- Replace the three label lines (`lv_obj_t *label = ...` / `lv_label_set_text(label, "Macropad base");` / `lv_obj_center(label);`) with `ui_create();`.
- Replace `bsp_display_start_with_config(&cfg);` with:

```c
  lv_disp_t *disp = bsp_display_start_with_config(&cfg);
  if (disp == NULL) {
    ESP_LOGE(TAG, "display/touch init failed");
    abort(); /* visible: the board resets and the log shows this line */
  }
```

- [ ] **Step 5: Build, then flash using the BOOT procedure**

Run: `$PIO run 2>&1 | tail -6`. Expected: `[SUCCESS]`.
Put the board in download mode (see Environment), then run: `$PIO run -t upload --upload-port /dev/ttyACM0 2>&1 | tail -4`. Expected: `[SUCCESS]`.
If the board does not appear in download mode, stop and ask the user.

- [ ] **Step 6: Visual check**

Run: `tools/snap_display.sh <scratchpad>/ui.jpg` then read the image.
Expected: 2 rows of 3 buttons, a slider near the bottom, and the text "USB connected" (or "USB not connected" if the cable is not on a host). Report any button that is cut off.

- [ ] **Step 7: Touch check against host events**

Ask the user to run `! sudo python3 tools/read_hid_events.py --seconds 30` and tap in this order: Prev, Play/Pause, Next, Vol-, Mute, Vol+, then hold Vol+ for 2 seconds.
Expected: `KEY_PREVIOUSSONG`, `KEY_PLAYPAUSE`, `KEY_NEXTSONG`, `KEY_VOLUMEDOWN`, `KEY_MUTE`, `KEY_VOLUMEUP` each as one down/up pair, and several `KEY_VOLUMEUP` pairs during the hold. Tell the user to tap Mute again and Play/Pause again to undo the state change.

- [ ] **Step 8: Brightness slider check**

Drag the slider to the left end (the user does this). Run `tools/snap_display.sh` before and after and compare the image brightness. Expected: visibly dimmer, but not black (minimum 5%).

- [ ] **Step 9: Commit**

```bash
git add src/ui.h src/ui.c src/DEMO_LVGL.c tools/snap_display.sh
git commit -m "Add touch UI for media keys and brightness" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Idle sleep and wake without a key press

**Files:**
- Create: `src/power.h`, `src/power.c`
- Modify: `src/ui.c`, `src/DEMO_LVGL.c`

**Interfaces:**
- Consumes: `bsp_display_brightness_set(int)`, LVGL `lv_disp_get_inactive_time(NULL)`.
- Produces:
  - `void power_init(void);` call with the LVGL lock held, after the display starts. Sets brightness to the stored level and starts the idle timer.
  - `void power_set_level(int percent);` stores the level; applies it unless asleep.
  - `bool power_touch(void);` call on every press. Returns true if the screen was asleep (it wakes it).

- [ ] **Step 1: Write `src/power.h`**

```c
#pragma once

#include <stdbool.h>

/** Start the idle timer and apply the stored brightness. Call with the LVGL lock held. */
void power_init(void);

/** Store the brightness (percent) and apply it unless the screen is asleep. */
void power_set_level(int percent);

/** Call on every touch press. Wakes the screen. Returns true if it was asleep. */
bool power_touch(void);
```

- [ ] **Step 2: Write `src/power.c`**

```c
#include "power.h"

#include <lvgl.h>

#include "display.h"

#define IDLE_TIMEOUT_MS 30000
#define CHECK_PERIOD_MS 1000

static int s_level = 100;
static bool s_asleep;

static void idle_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_asleep && lv_disp_get_inactive_time(NULL) > IDLE_TIMEOUT_MS) {
        s_asleep = true;
        bsp_display_brightness_set(0);
    }
}

void power_init(void)
{
    bsp_display_brightness_set(s_level);
    lv_timer_create(idle_timer_cb, CHECK_PERIOD_MS, NULL);
}

void power_set_level(int percent)
{
    s_level = percent;
    if (!s_asleep) {
        bsp_display_brightness_set(s_level);
    }
}

bool power_touch(void)
{
    if (!s_asleep) {
        return false;
    }
    s_asleep = false;
    bsp_display_brightness_set(s_level);
    return true;
}
```

- [ ] **Step 3: Change `src/ui.c`**

- Add `#include "power.h"`.
- Add above `button_cb`: `static bool s_swallow; /* the current touch only woke the screen */`
- In `button_cb` add as the first line: `if (s_swallow) { return; }`
- In `slider_cb` replace the body with:

```c
    if (s_swallow) {
        return;
    }
    power_set_level(lv_slider_get_value(lv_event_get_target(e)));
```

- Add this function above `ui_create`:

```c
/* PRESSED from any widget bubbles up to the screen. Wake first; a waking touch sends no key. */
static void screen_pressed_cb(lv_event_t *e)
{
    (void)e;
    s_swallow = power_touch();
}
```

- In `create_button`, after `lv_obj_set_pos(...)`, add `lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);`
- In `ui_create`, after `lv_obj_t *scr = lv_scr_act();` add:

```c
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_pressed_cb, LV_EVENT_PRESSED, NULL);
```

  and after the slider is created add `lv_obj_add_flag(slider, LV_OBJ_FLAG_EVENT_BUBBLE);`

- [ ] **Step 4: Change `src/DEMO_LVGL.c`**

Add `#include "power.h"`. In `setup()`, after `ui_create();` add `power_init();`. Remove the line `bsp_display_backlight_on();` (the power module now sets the level).

- [ ] **Step 5: Build and flash (BOOT procedure)**

Run: `$PIO run 2>&1 | tail -6`. Expected: `[SUCCESS]`. Flash as in Task 2 Step 5.

- [ ] **Step 6: Sleep check with the camera**

Do not touch the screen. Run `tools/snap_display.sh <scratchpad>/awake.jpg`, wait 35 seconds, run `tools/snap_display.sh <scratchpad>/asleep.jpg`, then read both images.
Expected: the second image is dark (backlight off).

- [ ] **Step 7: Wake check without a key**

Ask the user to run `! sudo python3 tools/read_hid_events.py --seconds 30` and, while the screen is asleep, tap the middle of the Play/Pause button once. Then tap it again.
Expected: after the first tap the screen is lit and no `KEY_*` line appears. After the second tap, one `KEY_PLAYPAUSE down/up` pair. Tell the user to tap Play/Pause once more to undo the state change.

- [ ] **Step 8: Brightness is remembered**

Set the slider to about half, wait 35 seconds, wake with a tap on empty space, and compare with `tools/snap_display.sh`. Expected: the screen returns at the dim level, not 100%.

- [ ] **Step 9: Commit**

```bash
git add src/power.h src/power.c src/ui.c src/DEMO_LVGL.c
git commit -m "Add idle backlight sleep with wake-without-keypress" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Config cleanup, README, spec fix, final check

**Files:**
- Modify: `platformio.ini`, `docs/superpowers/specs/2026-10-09-media-macropad-design.md`
- Create: `README.md`

- [ ] **Step 1: Lower log level and remove the missing data dir**

In `platformio.ini` change `-D LOG_LOCAL_LEVEL=ESP_LOG_VERBOSE` to `-D LOG_LOCAL_LEVEL=ESP_LOG_INFO`, and delete the line `data_dir = littlefs` (the folder does not exist).
Run: `$PIO run 2>&1 | tail -4`. Expected: `[SUCCESS]`.

- [ ] **Step 2: Fix the spec**

In the spec, replace `bsp_display_backlight_set(percent)` with `bsp_display_brightness_set(percent)` (in "Hardware checks", item 3).

- [ ] **Step 3: Write `README.md`**

```markdown
# macropadv2

Media macropad on the JC3248W535EN (ESP32-S3, 3.5" 320x480 touch LCD). It shows as a USB keyboard
with media keys: Prev, Play/Pause, Next, Vol-, Mute, Vol+. A slider sets the brightness. The
screen turns off after 30 seconds. The first touch wakes it without sending a key.

Base setup from https://github.com/NorthernMan54/JC3248W535EN (ESP-IDF 5.2.1, LVGL 8.4.0).

## Build

    python3 -m venv ~/.venvs/platformio && ~/.venvs/platformio/bin/pip install platformio
    ~/.venvs/platformio/bin/pio run

## Flash

The USB-C port is the keys port, so auto-reset flashing does not work.

1. Hold BOOT, press and release RST, release BOOT. `lsusb` shows `303a:1001`.
2. `~/.venvs/platformio/bin/pio run -t upload --upload-port /dev/ttyACM0`

## Logs

Logs go to UART0 (connector P1: +5V, TXD, RXD, GND), 115200 baud.

## Check keys on Linux

    sudo python3 tools/read_hid_events.py --seconds 20
    tools/snap_display.sh out.jpg   # webcam picture of the display
```

- [ ] **Step 4: Final end-to-end check**

Do a full pass on a fresh flash (BOOT procedure), then confirm each item with evidence:
1. `lsusb | grep -i macropad` shows the device.
2. `tools/snap_display.sh` image shows the 6 buttons, the slider and the USB status.
3. The user taps all six buttons; `tools/read_hid_events.py` prints the six matching keys.
4. Sleep and wake as in Task 3 Steps 6-7.
5. Report any step that you could not do. Do not claim a pass without the output.

- [ ] **Step 5: Commit**

```bash
git add platformio.ini README.md docs/superpowers/specs/2026-10-09-media-macropad-design.md
git commit -m "Lower log level, add README, fix spec function name" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

## Self-review against the spec

| Spec requirement | Task |
|---|---|
| USB HID consumer control, 6 usages | 1 |
| 3x2 grid, slider, landscape | 2 |
| Vol+/Vol- repeat while held | 2 (`LV_EVENT_LONG_PRESSED_REPEAT`) |
| HID not mounted: drop, log, show "USB not connected" | 1 (`hid_task`), 2 (status label) |
| Press, delay, release; retry release | 1 (`press`, `release`) |
| Touch/display init failure: log and halt visibly | 2 Step 4 |
| Idle sleep; first touch wakes without key | 3 |
| Host test with key events | 1, 2, 3 (`read_hid_events.py`) |
| Visual test with Dell webcam | 2, 3 (`snap_display.sh`) |
| Flash workflow and recovery docs | Environment, 4 (README) |
| Lower log level, remove `data_dir` | 4 |

Known gaps and risks:
- `esp_tinyusb` 2.0.1 config field names were not checked against the installed header. Task 1 Step 4 says how to adjust.
- PlatformIO fetching managed components is unverified. Task 1 Step 7 is the gate.
- The `Kconfig` option names `CONFIG_TINYUSB_HID_COUNT`, `CONFIG_TINYUSB_CDC_ENABLED` and `CONFIG_TINYUSB_MSC_ENABLED` are unverified until Task 1 Step 7 greps the generated sdkconfig.
- The BOOT button must be reachable on the finished board (user check).

---

## Addendum: Task 3b - Flash-mode gesture (added 2026-10-09)

See the spec addendum "flash-mode gesture". Files: create `src/flash_mode.h`, `src/flash_mode.c`; modify `src/ui.c`.
Interface: `void flash_mode_enter(void);` (does not return). The SDD brief for this task is in the workspace.
