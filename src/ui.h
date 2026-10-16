#pragma once

#include <stdbool.h>

/** Build the macropad screen. Call with the LVGL lock held (bsp_display_lock). */
void ui_create(void);

/** True while the current touch must not act: it only woke the screen, or it became a swipe.
 * Valid from the end of the PRESSED event dispatch on, so read it from a timer, not from a press handler. */
bool ui_touch_swallowed(void);

/** Lock or unlock the swipe between pages (the Game page locks it during a run). */
void ui_set_swipe_lock(bool lock);

/** The screen paints COLOR_BG under every page. A page that covers the whole screen with its own opaque
 * picture can turn that off: the full-screen fill is skipped, which saves about 10 ms per frame. */
void ui_set_background(bool opaque);
