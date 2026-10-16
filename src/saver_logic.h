#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Pure rules of the multi-video screensaver (host tests: tests/host/run.sh). */

/* Index of the video that plays after `cur` in a list of `count` videos. `cur` is -1 when the
 * current video is not in the list. `rnd` is a random number, used only when `random_order` is set;
 * a random pick never repeats `cur`. Returns -1 when count is 0. */
int saver_next_index(int count, int cur, bool random_order, uint32_t rnd);

/* The set of chosen videos is one string: file names, one per line ('\n'). */
bool saver_set_has(const char *set, const char *name);
/** Add or remove `name`. Returns false, and changes nothing, when it does not fit in `cap` bytes. */
bool saver_set_toggle(char *set, size_t cap, const char *name, bool on);

/* Fade: weight 0 = only `still`, 32 = only the live frame. Both are RGB565 with swapped bytes (what the
 * panel takes). Writes the mix over `live`. */
#define SAVER_BLEND_MAX 32
void saver_blend565be(uint16_t *live, const uint16_t *still, size_t pixels, int weight);

#ifdef __cplusplus
}
#endif
