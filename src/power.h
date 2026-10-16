#pragma once

#include <stdbool.h>

/** Start the idle timer and apply the stored brightness. Call with the LVGL lock held. */
void power_init(void);

/** Store the brightness (percent) and apply it unless the screen is asleep. */
void power_set_level(int percent);

/** Call on every touch press. Wakes the screen. Returns true if it was asleep. */
bool power_touch(void);
