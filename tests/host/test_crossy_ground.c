/* Host test for src/crossy_ground.c. Run: tests/host/run.sh (builds it with CG_SWAP 0 and 1)
 * Set CG_OUT_DIR to a directory to get pictures of one frame (ground.ppm, sprites.ppm). */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/crossy_ground.h"

static int s_fail;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_fail++;                                                        \
        }                                                                    \
    } while (0)

#define W 480
#define H 292
#define MARGIN 16
#define BW (W + 2 * MARGIN)
#define BH (H + 2 * MARGIN)

static const int8_t GROUND_Z[4] = {0, -2, -3, -2};

/* a pattern with all four kinds, rows below 0 included */
static uint8_t kind_of(int32_t row, void *user)
{
    static const uint8_t pat[11] = {0, 1, 1, 0, 2, 2, 2, 0, 3, 1, 0};
    (void)user;
    return pat[((row % 11) + 11) % 11];
}

static uint16_t s_buf[BW * BH], s_copy[BW * BH];
static uint16_t s_empty, s_guard;
static uint16_t s_big[W * H];

/* Fill the buffer: guard everywhere, then the clip with the empty sentinel. */
static void prepare(uint16_t *b, int x1, int y1, int x2, int y2)
{
    for (int i = 0; i < BW * BH; i++) {
        b[i] = s_guard;
    }
    for (int y = y1; y <= y2; y++) {
        for (int x = x1; x <= x2; x++) {
            b[(y + MARGIN) * BW + x + MARGIN] = s_empty;
        }
    }
}

static void render(uint16_t *b, int ox, int oy, int cam8, int x1, int y1, int x2, int y2)
{
    cg_target_t t = {b + MARGIN * BW + MARGIN, BW, 0, 0, x1, y1, x2, y2};
    int base = (int)floorf((float)cam8 / 8.0f);
    cg_render(&t, ox, oy, cam8, base - 12, base + 26, kind_of, NULL, GROUND_Z, NULL, 0, NULL, 0, 0);
}

/* Full-field render with items, an overlay and flags. cam8 0: rows -12 .. 26. */
static void render_items(uint16_t *b, const cg_item_t *it, int n, const cg_item_t *ov, int nov, unsigned flags)
{
    cg_target_t t = {b + MARGIN * BW + MARGIN, BW, 0, 0, 0, 0, W - 1, H - 1};
    cg_render(&t, 0, 0, 0, -12, 26, kind_of, NULL, GROUND_Z, it, n, ov, nov, flags);
}

static uint16_t at(const uint16_t *b, int x, int y) { return b[(y + MARGIN) * BW + x + MARGIN]; }

/* A solid sprite of one colour, w x h. */
static uint16_t s_solid[8][64 * 64];
static const uint16_t *solid(int i, int w, int h, uint16_t c)
{
    for (int k = 0; k < w * h; k++) {
        s_solid[i][k] = c;
    }
    return s_solid[i];
}

static cg_item_t item(int x, int y, int w, int h, const uint16_t *px, int32_t row, uint8_t layer, int key)
{
    cg_item_t it = {(int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h, px, row, layer, (int16_t)key};
    return it;
}

/* the order the caller must give: row descending, layer ascending, key ascending */
static int item_cmp(const void *pa, const void *pb)
{
    const cg_item_t *a = pa, *b = pb;
    if (a->row != b->row) {
        return a->row > b->row ? -1 : 1;
    }
    if (a->layer != b->layer) {
        return a->layer < b->layer ? -1 : 1;
    }
    return (a->key > b->key) - (a->key < b->key);
}

/* Check no empty pixel remains in the clip and nothing outside the clip changed. */
static void check_frame(const uint16_t *b, int x1, int y1, int x2, int y2, const char *what, int cam8)
{
    int empty = 0, stray = 0;
    for (int y = -MARGIN; y < BH - MARGIN; y++) {
        for (int x = -MARGIN; x < BW - MARGIN; x++) {
            uint16_t v = b[(y + MARGIN) * BW + x + MARGIN];
            int in = x >= x1 && x <= x2 && y >= y1 && y <= y2;
            if (in && v == s_empty) {
                if (empty++ < 3) {
                    printf("  hole at %d,%d\n", x, y);
                }
            }
            if (!in && v != s_guard) {
                stray++;
            }
        }
    }
    if (empty || stray) {
        printf("  %s cam8=%d: %d holes, %d stray pixels\n", what, cam8, empty, stray);
    }
    CHECK(empty == 0);
    CHECK(stray == 0);
}

static void write_ppm(const char *dir, const char *name, const uint16_t *b)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    if (!f) {
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint16_t v = b[(y + MARGIN) * BW + x + MARGIN];
#if CG_SWAP
            v = (uint16_t)((v >> 8) | (v << 8));
#endif
            unsigned char px[3] = {(unsigned char)(((v >> 11) & 31) * 255 / 31), (unsigned char)(((v >> 5) & 63) * 255 / 63),
                                   (unsigned char)((v & 31) * 255 / 31)};
            fwrite(px, 1, 3, f);
        }
    }
    fclose(f);
}

/* A frame with ground and a few synthetic sprites (a tree, a car, a bird) placed with the view's formulas. */
static void put_sprite(cg_item_t *it, uint16_t *px, int w, int h, int ax, int ay, float row, float x, float z, int32_t lane, uint8_t layer)
{
    float a = row, b = x - 5.5f;
    int sx = (int)lroundf(160 + 16 * (a + b)), sy = (int)lroundf(210 - 8 * a + 8 * b - z);
    *it = item(sx - ax, sy - ay, w, h, px, lane, layer, (int)((x + 0.5f) * 8));
}

static void write_sprites_ppm(const char *dir)
{
    static uint16_t tree[24 * 40], car[44 * 24], bird[16 * 20];
    for (int i = 0; i < 24 * 40; i++) {
        int x = i % 24, y = i / 24;
        tree[i] = y >= 28 ? (x >= 10 && x < 14 ? cg_pack(110, 70, 40) : CG_KEY)
                          : (abs(x - 12) <= y / 2 + 1 ? cg_pack(40, 130, 60) : CG_KEY);
    }
    for (int i = 0; i < 44 * 24; i++) {
        int x = i % 44, y = i / 44;
        car[i] = (y >= 8 && x >= 2 && x < 42) ? (y < 12 ? cg_pack(250, 250, 250) : cg_pack(220, 40, 40)) : CG_KEY;
    }
    for (int i = 0; i < 16 * 20; i++) {
        int x = i % 16, y = i / 16;
        int dx = x - 8, dy = y - 11;
        bird[i] = dx * dx + dy * dy <= 49 ? ((dx > 1 && dy < 0 && dx < 5 && dy > -4) ? cg_pack(30, 30, 30) : cg_pack(250, 240, 120)) : CG_KEY;
    }
    cg_item_t it[8];
    int n = 0;
    /* cam 0; lane 0 grass, 1-2 road, 3 grass, 4-6 river, 7 grass, 8 rail */
    put_sprite(&it[n++], tree, 24, 40, 12, 38, 7, 9, 0, 7, 1);
    put_sprite(&it[n++], tree, 24, 40, 12, 38, 7, 3, 0, 7, 1);
    put_sprite(&it[n++], car, 44, 24, 22, 20, 2, 4, -2, 2, 2);
    put_sprite(&it[n++], car, 44, 24, 22, 20, 1, 7, -2, 1, 2);
    put_sprite(&it[n++], tree, 24, 40, 12, 38, 3, 5, 0, 3, 1);
    put_sprite(&it[n++], bird, 16, 20, 8, 18, 3, 4.5f, 0, 3, 1);
    put_sprite(&it[n++], tree, 24, 40, 12, 38, 3, 6, 0, 3, 1);
    qsort(it, (size_t)n, sizeof it[0], item_cmp);
    prepare(s_buf, 0, 0, W - 1, H - 1);
    render_items(s_buf, it, n, NULL, 0, 0);
    write_ppm(dir, "sprites.ppm", s_buf);
}

int main(void)
{
    s_empty = cg_pack(1, 2, 3);
    s_guard = cg_pack(250, 5, 250);
    const int cams[] = {0, 3, 8, 77, 1000, -40};

    /* the whole screen is covered, and nothing outside the clip is written */
    for (unsigned i = 0; i < sizeof cams / sizeof cams[0]; i++) {
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render(s_buf, 0, 0, cams[i], 0, 0, W - 1, H - 1);
        check_frame(s_buf, 0, 0, W - 1, H - 1, "full", cams[i]);
        /* a page swiped half way: the field starts at x = 240, only the right half is drawn */
        prepare(s_buf, 240, 0, W - 1, H - 1);
        render(s_buf, 240, 0, cams[i], 240, 0, W - 1, H - 1);
        check_frame(s_buf, 240, 0, W - 1, H - 1, "right half", cams[i]);
        /* the field starts at x = -240: the left half of the screen shows its right half */
        prepare(s_buf, 0, 0, 239, H - 1);
        render(s_buf, -240, 0, cams[i], 0, 0, 239, H - 1);
        check_frame(s_buf, 0, 0, 239, H - 1, "field moved left", cams[i]);
        /* a small clip in the middle and one in a corner */
        prepare(s_buf, 100, 50, 130, 70);
        render(s_buf, 0, 0, cams[i], 100, 50, 130, 70);
        check_frame(s_buf, 100, 50, 130, 70, "small clip", cams[i]);
        prepare(s_buf, W - 5, H - 5, W - 1, H - 1);
        render(s_buf, 0, 0, cams[i], W - 5, H - 5, W - 1, H - 1);
        check_frame(s_buf, W - 5, H - 5, W - 1, H - 1, "corner", cams[i]);
        /* a vertical offset of the field */
        prepare(s_buf, 0, 20, W - 1, H - 1);
        render(s_buf, 0, 20, cams[i], 0, 20, W - 1, H - 1);
        check_frame(s_buf, 0, 20, W - 1, H - 1, "field moved down", cams[i]);
    }

    /* the same input gives the same output, and a clipped draw matches the full draw */
    prepare(s_buf, 0, 0, W - 1, H - 1);
    render(s_buf, 0, 0, 77, 0, 0, W - 1, H - 1);
    prepare(s_copy, 0, 0, W - 1, H - 1);
    render(s_copy, 0, 0, 77, 0, 0, W - 1, H - 1);
    CHECK(memcmp(s_buf, s_copy, sizeof s_buf) == 0);
    prepare(s_copy, 100, 50, 230, 170);
    render(s_copy, 0, 0, 77, 100, 50, 230, 170);
    int diff = 0;
    for (int y = 50; y <= 170; y++) {
        for (int x = 100; x <= 230; x++) {
            diff += s_buf[(y + MARGIN) * BW + x + MARGIN] != s_copy[(y + MARGIN) * BW + x + MARGIN];
        }
    }
    CHECK(diff == 0);
    /* the field moved by (240, 0) draws the same pixels, shifted */
    prepare(s_copy, 240, 0, W - 1, H - 1);
    render(s_copy, 240, 0, 77, 240, 0, W - 1, H - 1);
    diff = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W - 240; x++) {
            diff += s_buf[(y + MARGIN) * BW + x + MARGIN] != s_copy[(y + MARGIN) * BW + x + 240 + MARGIN];
        }
    }
    CHECK(diff == 0);


    /* ---- sprites ---- */
    const uint16_t RED = cg_pack(255, 0, 0), BLUE = cg_pack(0, 0, 255), WHITE = cg_pack(255, 255, 255);

    /* the key colour is what the sprite generator writes: RGB565 0x07E0, high byte first */
    CHECK(CG_KEY == cg_pack(0, 255, 0));
#if CG_SWAP
    {
        const unsigned char bytes[2] = {0x07, 0xE0};
        uint16_t v;
        memcpy(&v, bytes, 2);
        const unsigned short one = 1;
        if (*(const unsigned char *)&one == 1) { /* little-endian CPU, as the ESP32-S3 */
            CHECK(CG_KEY == v);
        }
    }
#endif

    /* (1) transparent pixels leave the buffer alone, opaque pixels overwrite */
    {
        uint16_t spr[4 * 4];
        for (int i = 0; i < 16; i++) {
            spr[i] = (i % 2) ? CG_KEY : RED;
        }
        cg_item_t it = item(50, 60, 4, 4, spr, 3, 1, 0);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, &it, 1, NULL, 0, CG_NO_GROUND);
        int bad = 0, red = 0;
        for (int y = 0; y < 4; y++) {
            for (int x = 0; x < 4; x++) {
                uint16_t v = at(s_buf, 50 + x, 60 + y);
                if ((y * 4 + x) % 2) {
                    bad += v != s_empty;
                } else {
                    red += v == RED;
                }
            }
        }
        CHECK(bad == 0);
        CHECK(red == 8);
        CHECK(at(s_buf, 49, 60) == s_empty && at(s_buf, 54, 60) == s_empty && at(s_buf, 50, 64) == s_empty);
    }

    /* (2) draw order */
    {
        const uint16_t *pa = solid(0, 8, 8, RED), *pb = solid(1, 8, 8, BLUE), *pc = solid(2, 8, 8, WHITE);
        /* two items on one lane: the larger key ends on top (given to cg_render in sorted order) */
        cg_item_t two[2] = {item(100, 100, 8, 8, pb, 3, 1, 20), item(104, 100, 8, 8, pa, 3, 1, 10)};
        qsort(two, 2, sizeof two[0], item_cmp);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, two, 2, NULL, 0, CG_NO_GROUND);
        CHECK(at(s_buf, 105, 102) == BLUE && at(s_buf, 101, 102) == BLUE);
        CHECK(at(s_buf, 100, 100) == BLUE && at(s_buf, 111, 100) == RED);
        /* layers: 2 over 1 over 0 on the same lane, whatever the keys */
        cg_item_t lay[3] = {item(100, 100, 8, 8, pc, 3, 2, -50), item(100, 100, 8, 8, pa, 3, 0, 900),
                            item(100, 100, 8, 8, pb, 3, 1, 400)};
        qsort(lay, 3, sizeof lay[0], item_cmp);
        CHECK(lay[0].layer == 0 && lay[1].layer == 1 && lay[2].layer == 2);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, lay, 3, NULL, 0, CG_NO_GROUND);
        CHECK(at(s_buf, 103, 103) == WHITE);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, lay, 2, NULL, 0, CG_NO_GROUND);
        CHECK(at(s_buf, 103, 103) == BLUE);
        /* a nearer lane (lower row) ends on top of a farther one */
        cg_item_t rows[2] = {item(100, 100, 8, 8, pa, 2, 0, 0), item(100, 100, 8, 8, pb, 7, 2, 0)};
        qsort(rows, 2, sizeof rows[0], item_cmp);
        CHECK(rows[0].row == 7);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, rows, 2, NULL, 0, CG_NO_GROUND);
        CHECK(at(s_buf, 103, 103) == RED);
    }

    /* the painter order: a farther sprite is overdrawn by the ground of a nearer lane (intended) */
    {
        uint16_t *big = s_big;
        for (int i = 0; i < W * H; i++) {
            big[i] = BLUE;
        }
        cg_item_t it = item(0, 0, W, H, big, 6, 1, 0);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, NULL, 0, NULL, 0, 0);
        memcpy(s_copy, s_buf, sizeof s_buf);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, &it, 1, NULL, 0, 0);
        int blue = 0, ground = 0, wrong = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                uint16_t v = at(s_buf, x, y);
                if (v == BLUE) {
                    blue++;
                } else {
                    ground++;
                    wrong += v != at(s_copy, x, y);
                }
            }
        }
        CHECK(blue > 0);   /* the lane's own ground is covered */
        CHECK(ground > 0); /* nearer lanes drew their ground over the sprite */
        CHECK(wrong == 0); /* and what shows through is exactly the ground-only picture */
    }

    /* a prop to the right of the hero (key 60 > 40) in the same lane and layer ends on top of it: the view relies on this */
    {
        const uint16_t *hero = solid(0, 8, 8, RED), *prop = solid(1, 8, 8, BLUE);
        cg_item_t it[2] = {item(100, 100, 8, 8, prop, 5, 1, 60), item(98, 100, 8, 8, hero, 5, 1, 40)};
        qsort(it, 2, sizeof it[0], item_cmp);
        CHECK(it[0].key == 40);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, it, 2, NULL, 0, CG_NO_GROUND);
        CHECK(at(s_buf, 99, 103) == RED && at(s_buf, 103, 103) == BLUE);
    }

    /* (3) clip safety: items partly or fully outside the clip, negative positions, sizes past the buffer edge */
    {
        uint16_t *px = (uint16_t *)solid(3, 60, 60, RED);
        px[0] = CG_KEY;
        cg_item_t its[] = {item(-30, -30, 60, 60, px, 4, 1, 0),  item(W - 10, H - 10, 60, 60, px, 4, 1, 1),
                           item(-100, 10, 60, 60, px, 4, 1, 2), item(W + 5, 10, 60, 60, px, 4, 1, 3),
                           item(10, -100, 60, 60, px, 4, 1, 4), item(10, H + 3, 60, 60, px, 4, 1, 5),
                           item(-59, -59, 60, 60, px, 3, 1, 0), item(200, 100, 60, 60, px, 3, 1, 1),
                           item(-32768, -32768, 60, 60, px, 2, 1, 0), item(32767 - 10, 32767 - 10, 60, 60, px, 2, 1, 1)};
        int n = (int)(sizeof its / sizeof its[0]);
        qsort(its, (size_t)n, sizeof its[0], item_cmp);
        prepare(s_copy, 0, 0, W - 1, H - 1);
        render_items(s_copy, its, n, NULL, 0, CG_NO_GROUND);
        for (int y = -MARGIN; y < BH - MARGIN; y++) { /* nothing outside the field */
            for (int x = -MARGIN; x < BW - MARGIN; x++) {
                if (x < 0 || x >= W || y < 0 || y >= H) {
                    CHECK(at(s_copy, x, y) == s_guard);
                }
            }
        }
        static const int clips[][4] = {{0, 0, 30, 30}, {100, 50, 130, 70}, {W - 20, H - 20, W - 1, H - 1}, {220, 110, 221, 111}, {0, 0, 0, 0}};
        for (unsigned c = 0; c < sizeof clips / sizeof clips[0]; c++) {
            const int *cl = clips[c];
            prepare(s_buf, cl[0], cl[1], cl[2], cl[3]);
            cg_target_t t = {s_buf + MARGIN * BW + MARGIN, BW, 0, 0, cl[0], cl[1], cl[2], cl[3]};
            cg_render(&t, 0, 0, 0, -12, 26, kind_of, NULL, GROUND_Z, its, n, NULL, 0, CG_NO_GROUND);
            int stray = 0, diff = 0;
            for (int y = -MARGIN; y < BH - MARGIN; y++) {
                for (int x = -MARGIN; x < BW - MARGIN; x++) {
                    int in = x >= cl[0] && x <= cl[2] && y >= cl[1] && y <= cl[3];
                    if (!in) {
                        stray += at(s_buf, x, y) != s_guard;
                    } else if (at(s_buf, x, y) != at(s_copy, x, y)) {
                        diff++; /* a clipped draw matches the full draw */
                    }
                }
            }
            CHECK(stray == 0);
            CHECK(diff == 0);
        }
        /* the field is moved: the origin shifts the items too */
        prepare(s_buf, 240, 0, W - 1, H - 1);
        cg_target_t t = {s_buf + MARGIN * BW + MARGIN, BW, 0, 0, 240, 0, W - 1, H - 1};
        cg_render(&t, 240, 0, 0, -12, 26, kind_of, NULL, GROUND_Z, its, n, NULL, 0, CG_NO_GROUND);
        int stray = 0;
        for (int y = -MARGIN; y < BH - MARGIN; y++) {
            for (int x = -MARGIN; x < 240; x++) {
                stray += at(s_buf, x, y) != s_guard;
            }
        }
        CHECK(stray == 0);
    }

    /* (4) the overlay is drawn last and over everything, in array order */
    {
        const uint16_t *pa = solid(0, 8, 8, RED), *pb = solid(1, 8, 8, BLUE), *pc = solid(2, 8, 8, WHITE);
        cg_item_t it[2] = {item(100, 100, 8, 8, pa, -12, 2, 0), item(100, 100, 8, 8, pa, 26, 2, 0)};
        qsort(it, 2, sizeof it[0], item_cmp);
        cg_item_t ov[2] = {item(102, 100, 8, 8, pb, 1000, 0, 0), item(104, 100, 8, 8, pc, -1000, 0, 0)};
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, it, 2, ov, 2, 0);
        CHECK(at(s_buf, 103, 103) == BLUE && at(s_buf, 105, 103) == WHITE && at(s_buf, 100, 103) == RED);
    }

    /* (5) CG_NO_GROUND leaves the buffer alone except under items */
    {
        const uint16_t *pa = solid(0, 8, 8, RED);
        cg_item_t it = item(10, 10, 8, 8, pa, 0, 1, 0);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, &it, 1, NULL, 0, CG_NO_GROUND);
        int red = 0, empty = 0;
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                red += at(s_buf, x, y) == RED;
                empty += at(s_buf, x, y) == s_empty;
            }
        }
        CHECK(red == 64 && empty == W * H - 64);
    }

    /* (6) rows outside [row_lo, row_hi] are skipped (hi 26, lo -12 here) */
    {
        const uint16_t *pa = solid(0, 8, 8, RED);
        cg_item_t it[4] = {item(10, 10, 8, 8, pa, 27, 1, 0), item(30, 10, 8, 8, pa, 26, 1, 0),
                           item(50, 10, 8, 8, pa, -12, 1, 0), item(70, 10, 8, 8, pa, -13, 1, 0)};
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, it, 4, NULL, 0, CG_NO_GROUND);
        CHECK(at(s_buf, 12, 12) == s_empty);
        CHECK(at(s_buf, 32, 12) == RED);
        CHECK(at(s_buf, 52, 12) == RED);
        CHECK(at(s_buf, 72, 12) == s_empty);
    }

    /* (7) determinism, ground and sprites */
    {
        const uint16_t *pa = solid(0, 8, 8, RED), *pb = solid(1, 8, 8, BLUE);
        cg_item_t it[3] = {item(100, 100, 8, 8, pa, 9, 1, 0), item(103, 100, 8, 8, pb, 9, 2, 0), item(90, 95, 8, 8, pa, 4, 0, 5)};
        qsort(it, 3, sizeof it[0], item_cmp);
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render_items(s_buf, it, 3, &it[0], 1, 0);
        prepare(s_copy, 0, 0, W - 1, H - 1);
        render_items(s_copy, it, 3, &it[0], 1, 0);
        CHECK(memcmp(s_buf, s_copy, sizeof s_buf) == 0);
    }

    const char *dir = getenv("CG_OUT_DIR");
    if (dir) {
        prepare(s_buf, 0, 0, W - 1, H - 1);
        render(s_buf, 0, 0, 20, 0, 0, W - 1, H - 1);
        write_ppm(dir, "ground.ppm", s_buf);
        write_sprites_ppm(dir);
    }

    printf(s_fail ? "crossy_ground: %d FAILED\n" : "crossy_ground: all passed\n", s_fail);
    return s_fail ? 1 : 0;
}
