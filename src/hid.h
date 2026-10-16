#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    MEDIA_KEY_PLAY_PAUSE = 0xCD,
    MEDIA_KEY_NEXT = 0xB5,
    MEDIA_KEY_PREV = 0xB6,
    MEDIA_KEY_MUTE = 0xE2,
    MEDIA_KEY_VOL_UP = 0xE9,
    MEDIA_KEY_VOL_DOWN = 0xEA,
} media_key_t;

/* Keyboard modifier bits (same values as the USB HID modifier byte). */
#define HID_MOD_CTRL 0x01
#define HID_MOD_SHIFT 0x02
#define HID_MOD_ALT 0x04
#define HID_MOD_GUI 0x08 /* Cmd on macOS, Super on Linux */

typedef enum { HID_ACT_CONSUMER, HID_ACT_COMBO, HID_ACT_TYPE, HID_ACT_DELAY } hid_action_type_t;

/** One step of a macro. Build with the HID_* initializers below. */
typedef struct {
    hid_action_type_t type;
    uint16_t value; /* consumer usage, or delay in ms */
    uint8_t mods;   /* HID_MOD_* for HID_ACT_COMBO */
    uint8_t key;    /* USB HID keyboard usage for HID_ACT_COMBO; 0 = modifiers only */
    const char *text;
} hid_action_t;

#define HID_CONSUMER(usage) {.type = HID_ACT_CONSUMER, .value = (usage)}
#define HID_COMBO(modifiers, keycode) {.type = HID_ACT_COMBO, .mods = (modifiers), .key = (keycode)}
#define HID_TYPE(str) {.type = HID_ACT_TYPE, .text = (str)}
#define HID_DELAY(ms) {.type = HID_ACT_DELAY, .value = (ms)}

typedef struct {
    const hid_action_t *actions;
    size_t count;
} hid_macro_t;

/** Start TinyUSB and the HID sender task. Call once. */
void hid_init(void);

/** Queue one key press+release. Non-blocking. Returns false if the queue is full. */
bool hid_send(media_key_t key);

/**
 * Queue a macro (key combos, typed text, delays). Non-blocking. Returns false if another macro
 * is still running or the queue is full. The macro must stay valid (use static data).
 */
bool hid_send_macro(const hid_macro_t *macro);

/** True when the USB host has configured the device. */
bool hid_is_mounted(void);

/** Drop the USB D+ pull-up so the host sees a disconnect. Used before a reset. */
void hid_detach(void);
