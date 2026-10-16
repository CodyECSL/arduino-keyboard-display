#include "crossy_ground.h"

/* Geometry (the same as crossy.c): a world point (row r, x cells, height z) is at
 *   sx = CX + 16 * (a + b),  sy = CY - 8 * a + 8 * b - z,  a = r - cam, b = x - 5.5.
 * With A = r * 8 - cam8 (eighths of a row) the top face of a lane crosses every scanline y in one run of
 * 32 px that starts at xl = CX + 4 * A + 2 * (y - CY + z). Along the run a and b both grow by one cell.
 * The front wall lies left of the run: [xl - 2 * h, xl), h = z + 6. Lanes go far to near, so a nearer run
 * covers a farther wall. */

#define CX 160
#define CY 210
#define RUN 32
#define SLAB_Z (-6)
#define MID_CELL8 44 /* 5.5 cells in eighths */

enum { GRASS, ROAD, RIVER, RAIL };

static uint16_t s_grass[2][2], s_grass_wall[2]; /* [row parity][tone] */
static uint16_t s_road[2], s_road_wall, s_line;
static uint16_t s_river[2], s_river_wall;
static uint16_t s_bed, s_bed_wall, s_sleeper, s_rail;
static int s_ready;

uint16_t cg_pack(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
#if CG_SWAP
    v = (uint16_t)((v >> 8) | (v << 8));
#endif
    return v;
}

/* A colour scaled by pct percent. */
static uint16_t shade(int r, int g, int b, int pct)
{
    r = r * pct / 100;
    g = g * pct / 100;
    b = b * pct / 100;
    return cg_pack((uint8_t)(r > 255 ? 255 : r), (uint8_t)(g > 255 ? 255 : g), (uint8_t)(b > 255 ? 255 : b));
}

static void init(void)
{
    s_grass[0][0] = shade(112, 200, 84, 97);
    s_grass[0][1] = shade(112, 200, 84, 103);
    s_grass[1][0] = shade(98, 186, 72, 97);
    s_grass[1][1] = shade(98, 186, 72, 103);
    s_grass_wall[0] = shade(112, 200, 84, 78);
    s_grass_wall[1] = shade(98, 186, 72, 78);
    s_road[0] = shade(90, 94, 106, 97);
    s_road[1] = shade(90, 94, 106, 103);
    s_road_wall = shade(90, 94, 106, 78);
    s_line = cg_pack(236, 236, 232);
    s_river[0] = shade(74, 164, 232, 98);
    s_river[1] = shade(74, 164, 232, 106);
    s_river_wall = shade(74, 164, 232, 78);
    s_bed = cg_pack(122, 100, 84);
    s_bed_wall = shade(122, 100, 84, 78);
    s_sleeper = cg_pack(84, 68, 60);
    s_rail = cg_pack(190, 192, 200);
    s_ready = 1;
}

static int floor_div2(int v) { return (v - (v & 1)) / 2; }
static int ceil_div2(int v) { return floor_div2(v + 1); }

/* Fill pixels x1 .. x2 - 1 (absolute) of a buffer line, cut to [lo, hi). */
static inline void span(uint16_t *line, int bx, int lo, int hi, int x1, int x2, uint16_t c)
{
    x1 = x1 < lo ? lo : x1;
    x2 = x2 > hi ? hi : x2;
    if (x1 >= x2) {
        return;
    }
    uint16_t *p = line + (x1 - bx);
    uint16_t *e = line + (x2 - bx);
    while (p < e) {
        *p++ = c;
    }
}

/* Draw one sprite, clipped; the key colour leaves the pixel under it alone. */
static void blit(const cg_target_t *t, int origin_x, int origin_y, const cg_item_t *it)
{
    int x0 = origin_x + it->x, y0 = origin_y + it->y;
    int x1 = x0 > t->clip_x1 ? x0 : t->clip_x1;
    int x2 = x0 + it->w - 1 < t->clip_x2 ? x0 + it->w - 1 : t->clip_x2;
    int y1 = y0 > t->clip_y1 ? y0 : t->clip_y1;
    int y2 = y0 + it->h - 1 < t->clip_y2 ? y0 + it->h - 1 : t->clip_y2;
    if (x1 > x2 || y1 > y2) {
        return;
    }
    int n = x2 - x1 + 1;
    for (int y = y1; y <= y2; y++) {
        const uint16_t *s = it->px + (long)(y - y0) * it->w + (x1 - x0);
        uint16_t *d = t->buf + (long)(y - t->buf_y1) * t->stride + (x1 - t->buf_x1);
        const uint16_t *e = s + n;
        while (s < e) {
            uint16_t p = *s++;
            if (p != CG_KEY) {
                *d = p;
            }
            d++;
        }
    }
}

/* The ground of one lane. */
static void ground_lane(const cg_target_t *t, int origin_x, int origin_y, int cam8, int32_t row,
                        uint8_t (*kind_of)(int32_t row, void *user), void *user, const int8_t ground_z[4])
{
    int cx1 = t->clip_x1 - origin_x, cx2 = t->clip_x2 - origin_x; /* field-relative clip */
    int cy1 = t->clip_y1 - origin_y, cy2 = t->clip_y2 - origin_y;
    int lo = t->clip_x1, hi = t->clip_x2 + 1;
    int kind = kind_of(row, user) & 3;
    int z = ground_z[kind], h = z - SLAB_Z;
    int A = (int)row * 8 - cam8;
    int c0 = CX + 4 * A + 2 * (z - CY); /* xl = c0 + 2 * y */
    int y1 = ceil_div2(cx1 - (RUN - 1) - c0), y2 = floor_div2(cx2 + 2 * h - c0); /* the run or wall touches the clip */
    y1 = y1 < cy1 ? cy1 : y1;
    y2 = y2 > cy2 ? cy2 : y2;
    int rp = (int)(row & 1);
    for (int y = y1; y <= y2; y++) {
        int xa = origin_x + c0 + 2 * y; /* run start, absolute */
        uint16_t *line = t->buf + (long)(origin_y + y - t->buf_y1) * t->stride;
        int xc8 = y - CY + z + A + MID_CELL8; /* the cell position at the run start, in eighths */
        int frac8 = xc8 & 7;
        int k = (xc8 - frac8) / 8; /* the cell at the run start (floor) */
        int cut = 4 * (8 - frac8);   /* px of cell k before cell k + 1 starts */
        int bx = t->buf_x1;
        uint16_t c1, c2, wall;
        switch (kind) {
        case GRASS:
            c1 = s_grass[rp][(k + rp) & 1];
            c2 = s_grass[rp][(k + rp + 1) & 1];
            wall = s_grass_wall[rp];
            break;
        case ROAD:
            c1 = s_road[k & 1];
            c2 = s_road[(k + 1) & 1];
            wall = s_road_wall;
            break;
        case RIVER:
            c1 = s_river[k & 1];
            c2 = s_river[(k + 1) & 1];
            wall = s_river_wall;
            break;
        default:
            c1 = c2 = s_bed;
            wall = s_bed_wall;
            break;
        }
        span(line, bx, lo, hi, xa - 2 * h, xa, wall);
        span(line, bx, lo, hi, xa, xa + cut, c1);
        span(line, bx, lo, hi, xa + cut, xa + RUN, c2);
        if (kind == ROAD) { /* a dash in every second cell, across the middle of the lane */
            int m = 4 * frac8 + 16; /* position along the cell at the dash, in px of 32 */
            int cell = k + (m >> 5);
            m &= 31;
            if (!(cell & 1) && m >= 5 && m <= 27) {
                span(line, bx, lo, hi, xa + 14, xa + 18, s_line);
            }
        } else if (kind == RAIL) { /* a sleeper in every cell, then the two rails */
            int s0 = 6 - 4 * frac8;
            if (s0 < 0) {
                s0 += 32;
            }
            for (int s = s0 - 32; s <= s0; s += 32) {
                int a = s < 3 ? 3 : s, b = s + 6 > 29 ? 29 : s + 6;
                span(line, bx, lo, hi, xa + a, xa + b, s_sleeper);
            }
            span(line, bx, lo, hi, xa + 6, xa + 9, s_rail);
            span(line, bx, lo, hi, xa + 24, xa + 27, s_rail);
        }
    }
}

void cg_render(const cg_target_t *t, int origin_x, int origin_y, int cam8, int32_t row_lo, int32_t row_hi,
               uint8_t (*kind_of)(int32_t row, void *user), void *user, const int8_t ground_z[4],
               const cg_item_t *items, int n_items, const cg_item_t *overlay, int n_overlay, unsigned flags)
{
    if (!s_ready) {
        init();
    }
    int ii = 0;
    while (ii < n_items && items[ii].row > row_hi) {
        ii++;
    }
    for (int32_t row = row_hi; row >= row_lo; row--) {
        if (!(flags & CG_NO_GROUND)) {
            ground_lane(t, origin_x, origin_y, cam8, row, kind_of, user, ground_z);
        }
        for (; ii < n_items && items[ii].row == row; ii++) {
            blit(t, origin_x, origin_y, &items[ii]);
        }
        while (ii < n_items && items[ii].row > row) { /* unsorted input: do not stall */
            ii++;
        }
    }
    for (int i = 0; i < n_overlay; i++) {
        blit(t, origin_x, origin_y, &overlay[i]);
    }
}
