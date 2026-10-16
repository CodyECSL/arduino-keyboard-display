#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A touch that wakes the screen must do nothing else. This gate sits between the touch controller and LVGL:
 * when a new touch starts and `wake` says it woke the screen, the touch is hidden from LVGL until the finger
 * has been up for TOUCH_GATE_RELEASE_MS. No widget sees a press, so nothing highlights, clicks or swipes. */
#define TOUCH_GATE_RELEASE_MS 120

typedef struct {
    bool prev;          /* the last sample was a press */
    bool ignoring;      /* the current touch woke the screen: hide it */
    uint32_t released_at;
} touch_gate_t;

/** `pressed`: the raw sample. `now_ms`: a millisecond tick. `wake`: called once at the start of each touch; it wakes
 * the screen if it was asleep and returns true in that case (may be NULL). Returns true if LVGL may see a press. */
bool touch_gate_pressed(touch_gate_t *g, bool pressed, uint32_t now_ms, bool (*wake)(void));
