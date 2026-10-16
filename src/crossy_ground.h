#pragma once

#include <stdint.h>

/* The scene renderer of the Crossy page: it draws the ground as plain opaque pixel spans, one run per lane and
 * scanline, and blits every keyed sprite (and the overlay on top) in per-lane order. Pure C
 * with no LVGL, so tests/host can run it on a PC. */

/* The firmware swaps the two bytes of each pixel (LV_COLOR_16_SWAP); the host build does not. */
#ifndef CG_SWAP
#ifdef ESP_PLATFORM
#include "lv_conf.h"
#define CG_SWAP LV_COLOR_16_SWAP
#else
#define CG_SWAP 0
#endif
#endif

typedef struct {
    uint16_t *buf;      /* pixel buffer, RGB565 */
    int stride;         /* pixels per buffer row */
    int buf_x1, buf_y1; /* absolute screen position of buf[0] */
    int clip_x1, clip_y1, clip_x2, clip_y2; /* absolute, inclusive; only these pixels are written */
} cg_target_t;

/* The transparent colour of the sprites: cg_pack(0, 255, 0), RGB565 0x07E0 (swapped when CG_SWAP). */
#define CG_KEY ((uint16_t)(CG_SWAP ? 0xE007 : 0x07E0))

/* A sprite to draw over the ground. Order inside one frame: lanes far to near; in each lane its ground,
 * then its items. */
typedef struct {
    int16_t x, y;       /* field-relative position of the sprite's top-left pixel */
    uint16_t w, h;
    const uint16_t *px; /* w*h RGB565 pixels in the buffer's own byte order, row by row; CG_KEY is transparent */
    int32_t row;        /* lane the item belongs to */
    uint8_t layer;      /* order inside a lane: 0 pads and logs, 1 deco, coins, props and the hero, 2 vehicles, train and signals */
    int16_t key;        /* order inside a layer: smaller is drawn first (footprint centre x in 1/8 cells) */
} cg_item_t;

enum { CG_NO_GROUND = 1 }; /* flags: skip the ground fill (speed test) */

/* Draw the lanes row_hi down to row_lo (far to near). origin: absolute screen position of the field's
 * top-left corner. cam8: the camera in eighths of a row. kind_of gives the lane kind (0 grass, 1 road,
 * 2 river, 3 rail) of a row. ground_z: surface height in pixels of each kind.
 * items: n_items sprites. The CALLER sorts them by row DESCENDING, then layer ascending, then key
 * ascending. Items with a row above row_hi are skipped, items with a row below row_lo are ignored.
 * overlay: n_overlay sprites drawn last, over everything, in array order. */
void cg_render(const cg_target_t *t, int origin_x, int origin_y, int cam8, int32_t row_lo, int32_t row_hi,
               uint8_t (*kind_of)(int32_t row, void *user), void *user, const int8_t ground_z[4],
               const cg_item_t *items, int n_items, const cg_item_t *overlay, int n_overlay, unsigned flags);

uint16_t cg_pack(uint8_t r, uint8_t g, uint8_t b);
