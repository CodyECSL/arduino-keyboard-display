#pragma once

#include <stdbool.h>

#include <lvgl.h>

/** Build the Crossy page on `tile`. Call with the LVGL lock held, after settings_init(). */
void crossy_create(lv_obj_t *tile);

/** Tell the page whether it is the one on screen. Hidden, it stops its timer and goes back to the title. */
void crossy_set_visible(bool visible);

/** A swipe between pages starts or ends. */
void crossy_set_dragging(bool dragging);
