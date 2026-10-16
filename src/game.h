#pragma once

#include <stdbool.h>

#include <lvgl.h>

/** Build the Game page on `tile`. Call with the LVGL lock held, after settings_init(). */
void game_create(lv_obj_t *tile);

/** Tell the game whether its page is the one on screen. Hidden, it stops its timer and resets to the demo. */
void game_set_visible(bool visible);

/** A swipe between pages starts or ends. While one is going on, the screen background must paint. */
void game_set_dragging(bool dragging);
