#include "touch_gate.h"

bool touch_gate_pressed(touch_gate_t *g, bool pressed, uint32_t now_ms, bool (*wake)(void))
{
    if (!pressed) {
        if (g->prev) {
            g->prev = false;
            g->released_at = now_ms;
        }
        if (g->ignoring && now_ms - g->released_at >= TOUCH_GATE_RELEASE_MS) {
            g->ignoring = false;
        }
        return false;
    }
    if (!g->prev) { /* a new touch, or the same finger after a missed sample */
        if (g->ignoring && now_ms - g->released_at >= TOUCH_GATE_RELEASE_MS) {
            g->ignoring = false; /* the finger really was up: no release sample came in between */
        }
        if (!g->ignoring && wake && wake()) {
            g->ignoring = true;
        }
    }
    g->prev = true;
    return !g->ignoring;
}
