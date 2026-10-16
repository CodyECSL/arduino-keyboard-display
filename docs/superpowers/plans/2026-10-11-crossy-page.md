# Crossy Page Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a "Crossy" page between the Game page and Settings: an isometric landscape hopping game (grass, road, river, rail lanes, coins, idle eagle) with all art drawn by code.

**Architecture:** Rules live in `src/crossy_logic.c` (no LVGL, host-tested). Art is rendered by `tools/crossy_art.py` (a small voxel renderer) and baked to `src/crossy_assets.c` as 2-byte chroma-keyed LVGL images. `src/crossy.c` shows the world with LVGL images: one big "world" container that moves with the camera, one container ("slot") per lane for depth order, and per-frame updates only for movers and the hero.

**Tech Stack:** C11, ESP-IDF 5.2.1 via PlatformIO, LVGL 8.4.0, Python 3 + Pillow, host tests built with `cc`.

**Spec:** `docs/superpowers/specs/2026-10-11-crossy-page-design.md`

## Global Constraints

- Page order: Media 0, Apps 1, AI 2, Game - LabEscape 3, Game - Crossy 4, Settings 5 (`PAGES` becomes 6). Naming scheme: every game page is "Game - <Name>". The existing jetpack game is "Game - LabEscape" (its title on screen is "LAB ESCAPE"); the new one is "Game - Crossy". Use these names in docs, UI text and new comments. Identifiers (`GAME_PAGE`, `game_*`) stay as they are.
- Speed rules taken from the Lab Escape work (README "Display speed", memory `macropad-display-limits`). The firmware-wide ones already apply (240 MHz, `-O2`, pointer-walk rotation, two full-screen PSRAM buffers with a flush task on core 0). The page must repeat the per-page ones: (1) no background of its own, and `ui_set_background(false)` while the page is up and not being swiped; (2) the sprite pixel data is copied to PSRAM once at page creation (Lab Escape does this for its wall: PSRAM is faster to read than flash); (3) no alpha-blended overlays, no dim layers, no opacity styles; HUD and cards are opaque fills; (4) swap an image source only if it changed (`set_src`); (5) nothing draws while the screensaver or dark screen is on; (6) never use partial refresh; (7) measure with a temporary on-screen counter in the real game, not in the demo. Target: within 20 % of Lab Escape's measured 25 updates/s, so at least 20/s.
- Landscape field 480 x 292 px (the bottom 28 px band holds the page dots, as on the Game page).
- 2:1 isometric, ground diamond 32 x 16 px. Forward = up and right on screen.
- No partial refresh, no alpha-blended overlays during play (see memory `macropad-display-limits`).
- New asset data at most 400 000 bytes. Pixel format `LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED`, RGB565 with `LV_COLOR_16_SWAP = 1` (high byte first), key colour pure green (RGB565 `0x07E0`).
- All art is original. Page title text is the placeholder "BIRD HOP"; no names or characters from the commercial game.
- Comment density and naming follow `src/game.c` and `src/game_logic.c` (short block comments, `s_` statics, `static` helpers).
- Never work on `main`. Branch is `CodyECSL/crossy-road-clone`.
- Every commit message ends with a second `-m` of `Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>`. Pull request bodies end with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
- PlatformIO: `~/.venvs/platformio/bin/pio`. Use it by full path.

---

## File Structure

| File | Create/Modify | Responsibility |
|---|---|---|
| `src/crossy_logic.h` | Create | Types, constants, API of the rules |
| `src/crossy_logic.c` | Create | Lane generator, hopping, hazards, river, eagle, camera |
| `tests/host/test_crossy_logic.c` | Create | Host tests for the rules |
| `tests/host/run.sh` | Modify | Run both host tests |
| `tools/crossy_art.py` | Create | Voxel renderer and every sprite model |
| `tools/test_crossy_art.py` | Create | Checks on the sprites (size, key colour, anchors, budget) |
| `tools/make_crossy_assets.py` | Create | Bakes sprites into `src/crossy_assets.c/.h`, writes a contact sheet |
| `src/crossy_assets.c/.h` | Generate | Sprite data (do not edit by hand) |
| `src/settings.c/.h` | Modify | NVS keys `cr_best`, `cr_coins` |
| `src/crossy.c/.h` | Create | LVGL view, input, states, HUD |
| `src/ui.c` | Modify | Page index, tile, hooks in `page_changed_cb` and `tile_scroll_cb` |
| `src/game.c` | Modify | `update_background` and `game_set_visible` so two game pages share the screen background correctly |
| `README.md` | Modify | One short "Crossy" section |

`src/CMakeLists.txt` globs `src/*.c`, so new `.c` files need no edit.

---

### Task 1: Rules core, lane generator, host-test harness

**Files:**
- Create: `src/crossy_logic.h`, `src/crossy_logic.c`, `tests/host/test_crossy_logic.c`
- Modify: `tests/host/run.sh`

**Interfaces:**
- Produces (used by all later tasks): everything in the header below. Names and types there are final.

- [ ] **Step 1: Write the header**

Create `src/crossy_logic.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Rules of the Crossy page: lanes, hopping, hazards, river, eagle and camera. No LVGL in here, so
 * tests/host can run it on a PC. A row is one lane; row 0 is the start and rows grow forward. A column
 * position x is in cells: column c spans x = c .. c + 1, and the hero stands at x = c + 0.5. */

#define CL_COLS 11       /* playable columns, 0 to 10 */
#define CL_START_COL 5
#define CL_EDGE 3        /* extra decoration columns on each side of a grass lane */
#define CL_PCOLS (CL_COLS + 2 * CL_EDGE)
#define CL_RING 64       /* lanes kept in memory */
#define CL_AHEAD 26      /* rows generated in front of the hero */
#define CL_LOOP 33.0f    /* road and river objects wrap over this many cells ... */
#define CL_X_MIN -10.0f  /* ... starting here, so they leave the screen before they wrap */
#define CL_MAX_OBJ 4

#define CL_HOP_S 0.13f
#define CL_IDLE_S 6.0f       /* seconds without progress until the eagle comes */
#define CL_EAGLE_S 1.3f      /* flight time of the eagle; it grabs the hero at the end */
#define CL_HERO_LEAD 2       /* the camera keeps the hero this many rows in front of its row */
#define CL_BEHIND_ROWS 2.5f  /* the eagle comes when the hero is this far behind the lead */
#define CL_BACK_LIMIT 6      /* rows the hero may hop back from its best row */

#define CL_TRAIN_LEN 8.0f
#define CL_RAIL_PERIOD 5.5f
#define CL_RAIL_WARN_AT 3.0f  /* the signal blinks from here ... */
#define CL_RAIL_TRAIN_AT 4.2f /* ... until the train has passed; it enters here */
#define CL_RAIL_CROSS_S 1.3f

typedef enum { CL_GRASS, CL_ROAD, CL_RIVER, CL_RAIL } cl_kind_t;
typedef enum { CL_P_NONE, CL_P_PINE, CL_P_ROUND, CL_P_TALL, CL_P_ROCK, CL_P_BUSH, CL_P_STUMP } cl_prop_t;
typedef enum { CL_V_SEDAN, CL_V_TAXI, CL_V_SPORT, CL_V_PICKUP, CL_V_TRUCK, CL_V_BUS, CL_V_COUNT } cl_vehicle_t;
typedef enum { CL_D_NONE, CL_D_SHEEP, CL_D_PIG, CL_D_COW } cl_deco_t;
typedef enum {
    CL_DEATH_NONE,
    CL_DEATH_CAR,
    CL_DEATH_TRAIN,
    CL_DEATH_WATER, /* hopped into the water */
    CL_DEATH_SWEPT, /* a log carried the hero off the field */
    CL_DEATH_EAGLE
} cl_death_t;

typedef struct {
    float x;    /* left edge, in cells */
    float len;  /* cells */
    uint8_t kind; /* road: cl_vehicle_t; river: unused (len says which log) */
} cl_obj_t;

typedef struct {
    int32_t row;
    uint8_t kind;  /* cl_kind_t */
    uint8_t nobj;  /* road: vehicles, river: logs, rail: 1 while the train is on the lane */
    int8_t dir;    /* +1: objects move toward higher x; -1: toward lower x */
    float speed;   /* cells per second, 0 or more */
    float phase;   /* rail: seconds into the cycle */
    bool signal;   /* rail: the signal is blinking */
    bool train;    /* rail: the train is on the lane (obj[0]) */
    uint8_t prop[CL_PCOLS]; /* grass: cl_prop_t per column, index = column + CL_EDGE */
    int8_t coin_col;        /* -1 for none */
    uint16_t pad_mask;      /* river: bit c = a lily pad in column c */
    uint8_t deco_kind;      /* cl_deco_t: an animal that blocks nothing */
    uint8_t deco_col;
    cl_obj_t obj[CL_MAX_OBJ];
} cl_lane_t;

typedef struct {
    cl_lane_t lanes[CL_RING];
    int32_t gen_row; /* rows below this exist */
    uint32_t rng;
    /* the generator */
    uint8_t run_kind;
    int run_left;
    int since_grass;
    int corr; /* a column kept free of obstacles, so a way forward always exists */
    /* the hero */
    int32_t row;
    float x;
    int32_t from_row; /* where the current hop began */
    float from_x;
    float hop_t;      /* seconds of the hop left; 0 when still */
    int8_t face;      /* 0 forward (+row), 1 toward +x, 2 back, 3 toward -x */
    bool q_has;       /* one hop can wait while another is going on */
    int8_t q_dr, q_dc;
    int32_t best_row; /* the score */
    int coins;
    float idle;       /* seconds without progress */
    float cam;        /* row at the bottom reference of the screen; the hero stands at cam + CL_HERO_LEAD */
    float eagle_t;    /* -1 when no eagle; else seconds since it appeared */
    bool moved;       /* the hero has hopped once: the camera starts to creep */
    bool dead;
    cl_death_t death;
    float death_t;
} cl_world_t;

/** Start a new run. The same seed gives the same run. */
void cl_reset(cl_world_t *w, uint32_t seed);

/** Make lanes up to and including `row`. Rows older than CL_RING are overwritten. */
void cl_generate_to(cl_world_t *w, int32_t row);

/** Move road, river and rail objects by dt seconds. cl_step calls this; the title demo calls it too. */
void cl_animate(cl_world_t *w, float dt);

/** Ask for a hop: dr = +1 forward, -1 back; dc = +1 toward +x, -1 toward -x. Others are 0. */
void cl_hop(cl_world_t *w, int dr, int dc);

/** Advance by dt seconds. Does little once dead (only death_t runs). */
void cl_step(cl_world_t *w, float dt);

/** The score: the furthest row reached. */
uint32_t cl_score(const cl_world_t *w);

/** Length of a vehicle in cells. */
float cl_vehicle_len(cl_vehicle_t kind);

/** Column that x is in (can be below 0 or above CL_COLS - 1). */
int cl_col(float x);

static inline cl_lane_t *cl_lane_at(cl_world_t *w, int32_t row) { return &w->lanes[row % CL_RING]; }
static inline const cl_lane_t *cl_lane_atc(const cl_world_t *w, int32_t row) { return &w->lanes[row % CL_RING]; }
```

- [ ] **Step 2: Write the failing tests for reset and generation**

Create `tests/host/test_crossy_logic.c`:

```c
/* Host test for src/crossy_logic.c. Run: tests/host/run.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/crossy_logic.h"

static int s_fail;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_fail++;                                                        \
        }                                                                    \
    } while (0)

#define DT (1.0f / 30.0f)

static void run(cl_world_t *w, float seconds)
{
    for (float t = 0; t < seconds; t += DT) {
        cl_step(w, DT);
    }
}

/* Rows 0 to 19 become plain grass with no obstacles; tests then build the lane they need. */
static void flat(cl_world_t *w)
{
    for (int32_t r = 0; r < 20; r++) {
        cl_lane_t *l = cl_lane_at(w, r);
        memset(l, 0, sizeof(*l));
        l->row = r;
        l->kind = CL_GRASS;
        l->coin_col = -1;
    }
}

static void fresh(cl_world_t *w)
{
    cl_reset(w, 7);
    flat(w);
}

static int free_cols(const cl_lane_t *l)
{
    int n = 0;
    for (int c = 0; c < CL_COLS; c++) {
        n += l->prop[c + CL_EDGE] == CL_P_NONE;
    }
    return n;
}

static void test_reset(void)
{
    cl_world_t w;
    cl_reset(&w, 1);
    CHECK(!w.dead);
    CHECK(w.row == 0 && w.x == CL_START_COL + 0.5f);
    CHECK(cl_score(&w) == 0);
    CHECK(w.eagle_t < 0.0f);
    for (int r = 0; r < 3; r++) {
        CHECK(cl_lane_atc(&w, r)->kind == CL_GRASS);
        CHECK(cl_lane_atc(&w, r)->row == r);
    }
    CHECK(cl_lane_atc(&w, 0)->prop[CL_START_COL + CL_EDGE] == CL_P_NONE);
    CHECK(w.gen_row > CL_AHEAD);
}

static void test_same_seed_same_world(void)
{
    cl_world_t a, b, c;
    cl_reset(&a, 42);
    cl_reset(&b, 42);
    cl_reset(&c, 43);
    CHECK(memcmp(a.lanes, b.lanes, sizeof(a.lanes)) == 0);
    CHECK(memcmp(a.lanes, c.lanes, sizeof(a.lanes)) != 0);
}

/* Every lane must leave a way forward. */
static void test_generator_invariants(void)
{
    for (uint32_t seed = 1; seed <= 20; seed++) {
        cl_world_t w;
        cl_reset(&w, seed);
        int non_grass = 0;
        for (int32_t r = 1; r < 400; r++) {
            cl_generate_to(&w, r);
            const cl_lane_t *l = cl_lane_atc(&w, r), *p = cl_lane_atc(&w, r - 1);
            CHECK(l->row == r);
            non_grass = l->kind == CL_GRASS ? 0 : non_grass + 1;
            CHECK(non_grass <= 8);
            if (l->kind == CL_GRASS) {
                CHECK(free_cols(l) >= 4);
                if (p->kind == CL_GRASS) { /* a column free in both lanes */
                    int both = 0;
                    for (int c = 0; c < CL_COLS; c++) {
                        both += l->prop[c + CL_EDGE] == 0 && p->prop[c + CL_EDGE] == 0;
                    }
                    CHECK(both >= 1);
                }
            } else {
                for (int i = 0; i < CL_PCOLS; i++) {
                    CHECK(l->prop[i] == 0);
                }
            }
            if (l->kind == CL_RIVER) {
                int pads = 0;
                for (int c = 0; c < CL_COLS; c++) {
                    pads += (l->pad_mask >> c) & 1;
                }
                CHECK(pads >= 6 || l->nobj >= 3);
            }
            if (l->kind == CL_ROAD) {
                CHECK(l->nobj >= 2 && l->nobj <= CL_MAX_OBJ && l->speed > 0.0f);
            }
            if (r < 3) {
                CHECK(l->kind == CL_GRASS);
            }
        }
    }
}

int main(void)
{
    test_reset();
    test_same_seed_same_world();
    test_generator_invariants();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("all crossy logic tests passed\n");
    return 0;
}
```

(The helper functions `run`, `flat`, `fresh` are used by later tasks. Until then the compiler warns about unused statics, so add `__attribute__((unused))` to `run` and `fresh` now: write them as `static void __attribute__((unused)) run(...)` and `static void __attribute__((unused)) fresh(...)`. Remove the attribute when the first test uses them.)

- [ ] **Step 3: Update the test runner**

Replace `tests/host/run.sh` with:

```sh
#!/bin/sh
# Build and run the host tests for the game rules (no board needed).
set -e
cd "$(dirname "$0")"
dir="$(mktemp -d)"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_game_logic.c ../../src/game_logic.c -lm -o "$dir/test_game_logic"
"$dir/test_game_logic"
${CC:-cc} -std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined \
    test_crossy_logic.c ../../src/crossy_logic.c -lm -o "$dir/test_crossy_logic"
"$dir/test_crossy_logic"
```

- [ ] **Step 4: Run to verify it fails**

Run: `sh tests/host/run.sh`
Expected: the first test passes, then the build of `test_crossy_logic` fails with `crossy_logic.c: No such file` (or undefined `cl_reset`).

- [ ] **Step 5: Write the generator**

Create `src/crossy_logic.c`:

```c
#include "crossy_logic.h"

#include <math.h>
#include <string.h>

static const float s_vlen[CL_V_COUNT] = {1.4f, 1.4f, 1.5f, 1.7f, 2.2f, 2.6f};

float cl_vehicle_len(cl_vehicle_t kind) { return s_vlen[kind]; }

int cl_col(float x) { return (int)floorf(x); }

uint32_t cl_score(const cl_world_t *w) { return (uint32_t)(w->best_row > 0 ? w->best_row : 0); }

/* ---- random numbers: xorshift32 ---- */

static uint32_t rnd(cl_world_t *w)
{
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w->rng = x;
    return x;
}

static int rint_(cl_world_t *w, int lo, int hi) { return lo + (int)(rnd(w) % (uint32_t)(hi - lo + 1)); }

static float rfl(cl_world_t *w, float lo, float hi) { return lo + (hi - lo) * ((float)(rnd(w) >> 8) / 16777216.0f); }

/* ---- the generator ---- */

static void next_run(cl_world_t *w, int32_t row)
{
    if (row < 3) { /* the first three lanes are safe grass */
        w->run_kind = CL_GRASS;
        w->run_left = 3 - (int)row;
        return;
    }
    int roll = rint_(w, 0, 99);
    cl_kind_t k;
    if (w->run_kind != CL_GRASS && (w->since_grass >= 4 || roll < 40)) {
        k = CL_GRASS;
    } else {
        int r2 = rint_(w, 0, 99);
        k = r2 < 50 ? CL_ROAD : (r2 < 78 ? CL_RIVER : CL_RAIL);
    }
    w->run_kind = (uint8_t)k;
    switch (k) {
    case CL_GRASS: w->run_left = rint_(w, 1, 3); break;
    case CL_ROAD: w->run_left = rint_(w, 1, 4); break;
    case CL_RIVER: w->run_left = rint_(w, 1, 3); break;
    default: w->run_left = rint_(w, 1, 2); break;
    }
}

static bool prop_free(const cl_lane_t *l, int col) { return l->prop[col + CL_EDGE] == CL_P_NONE; }

static void build_grass(cl_world_t *w, cl_lane_t *l, int prev, int next)
{
    for (int i = 0; i < CL_PCOLS; i++) {
        int col = i - CL_EDGE;
        bool edge = col < 0 || col >= CL_COLS;
        if (rint_(w, 0, 99) < (edge ? 70 : 25)) {
            l->prop[i] = (uint8_t)rint_(w, CL_P_PINE, CL_P_STUMP);
        }
    }
    /* Keep the corridor free in this lane: from the old column one can hop forward, then step to the new one. */
    l->prop[prev + CL_EDGE] = CL_P_NONE;
    l->prop[next + CL_EDGE] = CL_P_NONE;
    while (true) {
        int free_n = 0;
        for (int c = 0; c < CL_COLS; c++) {
            free_n += prop_free(l, c);
        }
        if (free_n >= 4) {
            break;
        }
        int c = rint_(w, 0, CL_COLS - 1);
        l->prop[c + CL_EDGE] = CL_P_NONE;
    }
    if (rnd(w) % 100 < 18) { /* an animal that blocks nothing */
        int c = rint_(w, 0, CL_COLS - 1);
        if (c != prev && c != next && prop_free(l, c)) {
            l->deco_kind = (uint8_t)rint_(w, CL_D_SHEEP, CL_D_COW);
            l->deco_col = (uint8_t)c;
        }
    }
}

static void build_road(cl_world_t *w, cl_lane_t *l, float diff)
{
    l->dir = rint_(w, 0, 1) ? 1 : -1;
    l->speed = rfl(w, 1.4f, 3.0f) * diff;
    l->nobj = (uint8_t)rint_(w, 2, CL_MAX_OBJ);
    float slot = CL_LOOP / (float)l->nobj;
    for (int i = 0; i < l->nobj; i++) {
        int kind = rint_(w, 0, CL_V_COUNT - 1);
        float len = cl_vehicle_len((cl_vehicle_t)kind);
        l->obj[i].kind = (uint8_t)kind;
        l->obj[i].len = len;
        l->obj[i].x = CL_X_MIN + slot * (float)i + rfl(w, 0.0f, slot - len - 2.5f); /* gaps of 2.5 cells or more */
    }
}

static void build_river(cl_world_t *w, cl_lane_t *l, int prev, int next, int32_t row)
{
    if (rint_(w, 0, 99) < 45) { /* lily pads; the corridor columns always have one */
        l->pad_mask = (uint16_t)((1u << prev) | (1u << next));
        int n = 2;
        if (prev == next) {
            n = 1;
        }
        while (n < 6) {
            uint16_t bit = (uint16_t)(1u << rint_(w, 0, CL_COLS - 1));
            if (!(l->pad_mask & bit)) {
                l->pad_mask |= bit;
                n++;
            }
        }
        return;
    }
    l->dir = rint_(w, 0, 1) ? 1 : -1;
    l->speed = rfl(w, 0.8f, 1.5f) * (1.0f + (float)(row > 100 ? 100 : row) / 250.0f);
    l->nobj = (uint8_t)rint_(w, 3, CL_MAX_OBJ);
    float slot = CL_LOOP / (float)l->nobj;
    for (int i = 0; i < l->nobj; i++) {
        float len = (float)rint_(w, 2, 4);
        l->obj[i].kind = 0;
        l->obj[i].len = len;
        l->obj[i].x = CL_X_MIN + slot * (float)i + rfl(w, 0.0f, slot - len - 1.5f);
    }
}

static void gen_lane(cl_world_t *w, int32_t row)
{
    cl_lane_t *l = cl_lane_at(w, row);
    memset(l, 0, sizeof(*l));
    l->row = row;
    l->coin_col = -1;
    if (w->run_left == 0) {
        next_run(w, row);
    }
    w->run_left--;
    l->kind = w->run_kind;
    w->since_grass = l->kind == CL_GRASS ? 0 : w->since_grass + 1;

    int prev = w->corr;
    int next = prev + rint_(w, -1, 1);
    next = next < 0 ? 0 : (next > CL_COLS - 1 ? CL_COLS - 1 : next);
    w->corr = next;
    float diff = 1.0f + (float)(row > 100 ? 100 : row) / 125.0f;

    switch (l->kind) {
    case CL_GRASS: build_grass(w, l, prev, next); break;
    case CL_ROAD: build_road(w, l, diff); break;
    case CL_RIVER: build_river(w, l, prev, next, row); break;
    default:
        l->dir = rint_(w, 0, 1) ? 1 : -1;
        l->phase = rfl(w, 0.0f, CL_RAIL_PERIOD);
        break;
    }
    if (l->kind != CL_RIVER && row >= 1 && rint_(w, 0, 99) < 14) {
        int c = rint_(w, 0, CL_COLS - 1);
        if (prop_free(l, c)) {
            l->coin_col = (int8_t)c;
        }
    }
}

void cl_generate_to(cl_world_t *w, int32_t row)
{
    while (w->gen_row <= row) {
        gen_lane(w, w->gen_row);
        w->gen_row++;
    }
}

void cl_reset(cl_world_t *w, uint32_t seed)
{
    memset(w, 0, sizeof(*w));
    w->rng = seed ? seed : 1;
    w->run_kind = CL_GRASS;
    w->corr = CL_START_COL;
    w->x = CL_START_COL + 0.5f;
    w->from_x = w->x;
    w->eagle_t = -1.0f;
    cl_generate_to(w, CL_AHEAD);
}
```

- [ ] **Step 6: Add temporary stubs so the test links**

The test only calls the functions above plus none of the later ones, so no stubs are needed. Run: `sh tests/host/run.sh`
Expected: `all crossy logic tests passed`.

If the generator-invariant test fails, read the failing line number, fix the generator (not the test), and re-run. Do not weaken a check.

- [ ] **Step 7: Commit**

```bash
git add src/crossy_logic.h src/crossy_logic.c tests/host/test_crossy_logic.c tests/host/run.sh
git commit -m "Add the Crossy lane generator and host tests" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Hopping, props, bounds, coins, score

**Files:**
- Modify: `src/crossy_logic.c`, `tests/host/test_crossy_logic.c`

**Interfaces:**
- Consumes: Task 1 header.
- Produces: `cl_hop`, a first `cl_step` (hop timing, landing, coin pick-up, score, camera follow, row generation), `cl_animate` as an empty-for-now function that Task 3 fills. Static helpers `start_hop`, `land`, `die` used by later tasks.

- [ ] **Step 1: Write the failing tests**

Add to `tests/host/test_crossy_logic.c` before `main` (remove the two `__attribute__((unused))` marks now):

```c
static void test_hop_forward(void)
{
    cl_world_t w;
    fresh(&w);
    cl_hop(&w, 1, 0);
    CHECK(w.row == 1 && w.hop_t > 0.0f && w.from_row == 0);
    CHECK(w.x == 5.5f && w.face == 0);
    cl_step(&w, 0.2f);
    CHECK(w.hop_t == 0.0f && w.row == 1);
    CHECK(cl_score(&w) == 1);
}

static void test_hop_sideways_and_bounds(void)
{
    cl_world_t w;
    fresh(&w);
    cl_hop(&w, 0, 1);
    CHECK(w.x == 6.5f && w.face == 1);
    cl_step(&w, 0.2f);
    w.x = 10.5f;
    cl_hop(&w, 0, 1); /* the edge */
    CHECK(w.x == 10.5f && w.hop_t == 0.0f);
    w.x = 0.5f;
    cl_hop(&w, 0, -1);
    CHECK(w.x == 0.5f && w.hop_t == 0.0f && w.face == 3);
}

static void test_hop_back_limits(void)
{
    cl_world_t w;
    fresh(&w);
    cl_hop(&w, -1, 0); /* row -1 does not exist */
    CHECK(w.row == 0 && w.face == 2);
    w.row = 10;
    w.best_row = 10;
    cl_hop(&w, -1, 0);
    CHECK(w.row == 9);
    cl_step(&w, 0.2f);
    w.row = 4; /* more than CL_BACK_LIMIT below the best row */
    cl_hop(&w, -1, 0);
    CHECK(w.row == 4);
}

static void test_prop_blocks(void)
{
    cl_world_t w;
    fresh(&w);
    cl_lane_at(&w, 1)->prop[5 + CL_EDGE] = CL_P_PINE;
    cl_hop(&w, 1, 0);
    CHECK(w.row == 0 && w.hop_t == 0.0f && w.face == 0);
    cl_hop(&w, 0, 1); /* the next column is free */
    CHECK(w.x == 6.5f);
}

static void test_hop_queue(void)
{
    cl_world_t w;
    fresh(&w);
    cl_hop(&w, 1, 0);
    cl_hop(&w, 1, 0); /* waits */
    CHECK(w.row == 1 && w.q_has);
    cl_step(&w, 0.14f); /* first hop ends, the queued one starts */
    CHECK(w.row == 2 && w.hop_t > 0.0f && !w.q_has);
}

static void test_coin(void)
{
    cl_world_t w;
    fresh(&w);
    cl_lane_at(&w, 1)->coin_col = 5;
    cl_hop(&w, 1, 0);
    cl_step(&w, 0.2f);
    CHECK(w.coins == 1 && cl_lane_at(&w, 1)->coin_col == -1);
}

static void test_camera_follows(void)
{
    cl_world_t w;
    fresh(&w);
    for (int i = 0; i < 5; i++) {
        cl_hop(&w, 1, 0);
        cl_step(&w, 0.14f);
    }
    CHECK(w.row == 5);
    CHECK(fabsf(w.cam - 3.0f) < 0.3f);
    CHECK(w.gen_row > w.row + CL_AHEAD);
}
```

and call them from `main` after `test_generator_invariants();`:

```c
    test_hop_forward();
    test_hop_sideways_and_bounds();
    test_hop_back_limits();
    test_prop_blocks();
    test_hop_queue();
    test_coin();
    test_camera_follows();
```

- [ ] **Step 2: Run to verify it fails**

Run: `sh tests/host/run.sh`
Expected: link error, undefined reference to `cl_hop` and `cl_step`.

- [ ] **Step 3: Write the implementation**

Append to `src/crossy_logic.c`:

```c
/* ---- the hero ---- */

static void die(cl_world_t *w, cl_death_t why)
{
    if (w->dead) {
        return;
    }
    w->dead = true;
    w->death = why;
    w->death_t = 0.0f;
    w->hop_t = 0.0f;
    w->q_has = false;
}

/* The cell under the hero, snapped to a column centre. */
static float snap(float x)
{
    float c = roundf(x - 0.5f) + 0.5f;
    return c < 0.5f ? 0.5f : (c > CL_COLS - 0.5f ? CL_COLS - 0.5f : c);
}

static void start_hop(cl_world_t *w, int dr, int dc)
{
    w->face = (int8_t)(dr > 0 ? 0 : (dc > 0 ? 1 : (dr < 0 ? 2 : 3)));
    int32_t nr = w->row + dr;
    if (nr < 0 || nr < w->best_row - CL_BACK_LIMIT) {
        return;
    }
    cl_generate_to(w, nr);
    const cl_lane_t *tl = cl_lane_atc(w, nr);
    float nx = dr != 0 ? snap(w->x) : w->x + (float)dc;
    if (tl->kind == CL_RIVER) {
        if (nx < 0.2f || nx > CL_COLS - 0.2f) {
            return;
        }
    } else {
        nx = snap(nx);
        int c = cl_col(nx);
        if (c < 0 || c >= CL_COLS || (dc != 0 && fabsf(nx - (w->x + (float)dc)) > 0.01f) ||
            tl->prop[c + CL_EDGE] != CL_P_NONE) {
            return;
        }
    }
    w->from_row = w->row;
    w->from_x = w->x;
    w->row = nr;
    w->x = nx;
    w->hop_t = CL_HOP_S;
    w->moved = true;
}

void cl_hop(cl_world_t *w, int dr, int dc)
{
    if (w->dead) {
        return;
    }
    if (w->hop_t > 0.0f) {
        w->q_has = true;
        w->q_dr = (int8_t)dr;
        w->q_dc = (int8_t)dc;
        return;
    }
    start_hop(w, dr, dc);
}

static void land(cl_world_t *w)
{
    cl_lane_t *l = cl_lane_at(w, w->row);
    int c = cl_col(w->x);
    if (l->coin_col >= 0 && c == l->coin_col) {
        l->coin_col = -1;
        w->coins++;
    }
    if (w->row > w->best_row) {
        w->best_row = w->row;
        w->idle = 0.0f;
    }
}

void cl_animate(cl_world_t *w, float dt)
{
    (void)w;
    (void)dt;
}

void cl_step(cl_world_t *w, float dt)
{
    if (w->dead) {
        w->death_t += dt;
        return;
    }
    cl_animate(w, dt);
    cl_generate_to(w, w->row + CL_AHEAD);
    w->idle += dt;

    if (w->hop_t > 0.0f) {
        w->hop_t -= dt;
        if (w->hop_t <= 0.0f) {
            w->hop_t = 0.0f;
            land(w);
            if (!w->dead && w->q_has) {
                w->q_has = false;
                start_hop(w, w->q_dr, w->q_dc);
            }
        }
    }

    float lead = (float)w->row - CL_HERO_LEAD;
    if (w->cam < lead) {
        w->cam = lead;
    }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `sh tests/host/run.sh`
Expected: `all crossy logic tests passed`. If `test_hop_sideways_and_bounds` fails on the `x == 6.5f` check, the snap logic in `start_hop` is wrong: for a sideways hop on non-river lanes the target must be exactly `x + dc` (the hero is already on a column centre).

- [ ] **Step 5: Commit**

```bash
git add src/crossy_logic.c tests/host/test_crossy_logic.c
git commit -m "Crossy rules: hopping, props, coins, score" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Road and rail hazards

**Files:**
- Modify: `src/crossy_logic.c`, `tests/host/test_crossy_logic.c`

**Interfaces:**
- Consumes: `die`, `land`, `cl_animate` stub from Task 2.
- Produces: object movement in `cl_animate`, `hazard_hit()` (static), standing-hazard check in `cl_step`.

- [ ] **Step 1: Write the failing tests**

Add before `main`:

```c
static void make_road(cl_world_t *w, int32_t row, float x, float speed, int dir)
{
    cl_lane_t *l = cl_lane_at(w, row);
    memset(l, 0, sizeof(*l));
    l->row = row;
    l->kind = CL_ROAD;
    l->coin_col = -1;
    l->dir = (int8_t)dir;
    l->speed = speed;
    l->nobj = 1;
    l->obj[0].kind = CL_V_SEDAN;
    l->obj[0].len = 1.4f;
    l->obj[0].x = x;
}

static void test_car_kills_on_landing(void)
{
    cl_world_t w;
    fresh(&w);
    make_road(&w, 1, 5.0f, 0.0f, 1); /* parked on the hero's column */
    cl_hop(&w, 1, 0);
    cl_step(&w, 0.2f);
    CHECK(w.dead && w.death == CL_DEATH_CAR);
}

static void test_car_misses(void)
{
    cl_world_t w;
    fresh(&w);
    make_road(&w, 1, 0.0f, 0.0f, 1);
    cl_hop(&w, 1, 0);
    cl_step(&w, 0.2f);
    CHECK(!w.dead);
}

static void test_moving_car_hits_standing_hero(void)
{
    cl_world_t w;
    fresh(&w);
    make_road(&w, 1, 1.0f, 2.0f, 1); /* reaches the hero's column after about 1.4 s */
    cl_hop(&w, 1, 0);
    run(&w, 0.5f);
    CHECK(!w.dead);
    run(&w, 1.0f);
    CHECK(w.dead && w.death == CL_DEATH_CAR);
}

static void test_objects_wrap(void)
{
    cl_world_t w;
    fresh(&w);
    make_road(&w, 1, CL_X_MIN + CL_LOOP - 0.1f, 2.0f, 1);
    cl_animate(&w, 0.1f);
    CHECK(cl_lane_at(&w, 1)->obj[0].x < CL_X_MIN + 1.0f);
    make_road(&w, 2, CL_X_MIN + 0.1f, 2.0f, -1);
    cl_animate(&w, 0.1f);
    CHECK(cl_lane_at(&w, 2)->obj[0].x > CL_X_MIN + CL_LOOP - 1.0f);
}

static void make_rail(cl_world_t *w, int32_t row, float phase, int dir)
{
    cl_lane_t *l = cl_lane_at(w, row);
    memset(l, 0, sizeof(*l));
    l->row = row;
    l->kind = CL_RAIL;
    l->coin_col = -1;
    l->dir = (int8_t)dir;
    l->phase = phase;
}

static void test_rail_signal_and_train(void)
{
    cl_world_t w;
    fresh(&w);
    make_rail(&w, 3, 0.0f, 1);
    run(&w, 2.5f);
    CHECK(!cl_lane_at(&w, 3)->signal && !cl_lane_at(&w, 3)->train);
    run(&w, 1.0f); /* phase 3.5: the signal blinks, no train yet */
    CHECK(cl_lane_at(&w, 3)->signal && !cl_lane_at(&w, 3)->train);
    run(&w, 1.0f); /* phase 4.5: the train is on the lane */
    CHECK(cl_lane_at(&w, 3)->train && cl_lane_at(&w, 3)->nobj == 1);
    CHECK(w.idle < CL_IDLE_S + 1.0f);
}

static void test_train_kills_hero_on_rail(void)
{
    cl_world_t w;
    fresh(&w);
    make_rail(&w, 1, 0.0f, 1);
    cl_hop(&w, 1, 0);
    run(&w, 5.4f);
    CHECK(w.dead && w.death == CL_DEATH_TRAIN);
}

static void test_train_spares_hero_off_rail(void)
{
    cl_world_t w;
    fresh(&w);
    make_rail(&w, 1, 0.0f, 1);
    run(&w, 5.2f);
    CHECK(!w.dead || w.death == CL_DEATH_EAGLE); /* only the eagle may take a hero that waits */
    CHECK(!(w.dead && w.death == CL_DEATH_TRAIN));
}
```

and in `main`:

```c
    test_car_kills_on_landing();
    test_car_misses();
    test_moving_car_hits_standing_hero();
    test_objects_wrap();
    test_rail_signal_and_train();
    test_train_kills_hero_on_rail();
    test_train_spares_hero_off_rail();
```

- [ ] **Step 2: Run to verify it fails**

Run: `sh tests/host/run.sh`
Expected: FAIL lines for the car and train tests (nothing moves or kills yet).

- [ ] **Step 3: Write the implementation**

In `src/crossy_logic.c`, replace the empty `cl_animate` with:

```c
void cl_animate(cl_world_t *w, float dt)
{
    int32_t lo = w->gen_row - CL_RING;
    for (int32_t r = lo < 0 ? 0 : lo; r < w->gen_row; r++) {
        cl_lane_t *l = cl_lane_at(w, r);
        if (l->kind == CL_ROAD || l->kind == CL_RIVER) {
            for (int i = 0; i < l->nobj; i++) {
                l->obj[i].x += (float)l->dir * l->speed * dt;
                if (l->obj[i].x >= CL_X_MIN + CL_LOOP) {
                    l->obj[i].x -= CL_LOOP;
                } else if (l->obj[i].x < CL_X_MIN) {
                    l->obj[i].x += CL_LOOP;
                }
            }
        } else if (l->kind == CL_RAIL) {
            l->phase += dt;
            if (l->phase >= CL_RAIL_PERIOD) {
                l->phase -= CL_RAIL_PERIOD;
            }
            l->signal = l->phase >= CL_RAIL_WARN_AT && l->phase < CL_RAIL_TRAIN_AT + CL_RAIL_CROSS_S;
            l->train = l->phase >= CL_RAIL_TRAIN_AT && l->phase < CL_RAIL_TRAIN_AT + CL_RAIL_CROSS_S;
            l->nobj = l->train ? 1 : 0;
            if (l->train) {
                float t = (l->phase - CL_RAIL_TRAIN_AT) / CL_RAIL_CROSS_S;
                float travel = CL_COLS + 2.0f + CL_TRAIN_LEN;
                l->obj[0].len = CL_TRAIN_LEN;
                l->obj[0].kind = 0;
                l->obj[0].x = l->dir > 0 ? -CL_TRAIN_LEN - 1.0f + t * travel : CL_COLS + 1.0f - t * travel;
            }
        }
    }
}
```

Add above `land` (it is called from `land` and from `cl_step`):

```c
/* A vehicle or the train overlaps the hero. */
static cl_death_t hazard_hit(const cl_lane_t *l, float x)
{
    if (l->kind != CL_ROAD && l->kind != CL_RAIL) {
        return CL_DEATH_NONE;
    }
    for (int i = 0; i < l->nobj; i++) {
        const cl_obj_t *o = &l->obj[i];
        if (x + 0.3f > o->x + 0.05f && x - 0.3f < o->x + o->len - 0.05f) {
            return l->kind == CL_ROAD ? CL_DEATH_CAR : CL_DEATH_TRAIN;
        }
    }
    return CL_DEATH_NONE;
}
```

At the end of `land`, before the closing brace, add:

```c
    cl_death_t h = hazard_hit(l, w->x);
    if (h != CL_DEATH_NONE) {
        die(w, h);
    }
```

In `cl_step`, replace the line `float lead = ...` block's preceding `if (w->hop_t > 0.0f) {...}` with an `if/else` so a still hero is checked each step. The hop block becomes:

```c
    if (w->hop_t > 0.0f) {
        w->hop_t -= dt;
        if (w->hop_t <= 0.0f) {
            w->hop_t = 0.0f;
            land(w);
            if (!w->dead && w->q_has) {
                w->q_has = false;
                start_hop(w, w->q_dr, w->q_dc);
            }
        }
    } else {
        cl_death_t h = hazard_hit(cl_lane_atc(w, w->row), w->x);
        if (h != CL_DEATH_NONE) {
            die(w, h);
        }
    }
```

- [ ] **Step 4: Run to verify it passes**

Run: `sh tests/host/run.sh`
Expected: `all crossy logic tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/crossy_logic.c tests/host/test_crossy_logic.c
git commit -m "Crossy rules: road traffic and trains" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 4: River, logs and lily pads

**Files:**
- Modify: `src/crossy_logic.c`, `tests/host/test_crossy_logic.c`

**Interfaces:**
- Consumes: `die`, `land`, `hazard_hit`, `cl_animate` (logs already move there).
- Produces: `support()` (static), ride and sweep logic in `cl_step`, water death in `land`.

- [ ] **Step 1: Write the failing tests**

Add before `main`:

```c
static void make_river(cl_world_t *w, int32_t row)
{
    cl_lane_t *l = cl_lane_at(w, row);
    memset(l, 0, sizeof(*l));
    l->row = row;
    l->kind = CL_RIVER;
    l->coin_col = -1;
}

static void add_log(cl_lane_t *l, float x, float len, float speed, int dir)
{
    l->dir = (int8_t)dir;
    l->speed = speed;
    l->obj[l->nobj].x = x;
    l->obj[l->nobj].len = len;
    l->nobj++;
}

static void test_water_drowns(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    cl_hop(&w, 1, 0);
    cl_step(&w, 0.2f);
    CHECK(w.dead && w.death == CL_DEATH_WATER);
}

static void test_pad_is_safe(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    cl_lane_at(&w, 1)->pad_mask = 1u << 5;
    cl_hop(&w, 1, 0);
    run(&w, 1.0f);
    CHECK(!w.dead && w.x == 5.5f);
    cl_hop(&w, 0, 1); /* the next column has no pad */
    cl_step(&w, 0.2f);
    CHECK(w.dead && w.death == CL_DEATH_WATER);
}

static void test_log_carries(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    add_log(cl_lane_at(&w, 1), 4.0f, 3.0f, 1.0f, 1);
    cl_hop(&w, 1, 0);
    run(&w, 1.0f);
    CHECK(!w.dead);
    CHECK(w.x > 6.0f && w.x < 6.8f);
}

static void test_log_sweeps_off_field(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    add_log(cl_lane_at(&w, 1), 4.0f, 6.0f, 3.0f, 1);
    cl_hop(&w, 1, 0);
    run(&w, 3.0f);
    CHECK(w.dead && w.death == CL_DEATH_SWEPT);
}

static void test_hop_log_to_log(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    make_river(&w, 2);
    add_log(cl_lane_at(&w, 1), 3.0f, 5.0f, 0.5f, 1);
    add_log(cl_lane_at(&w, 2), 3.0f, 5.0f, 0.5f, -1);
    cl_hop(&w, 1, 0);
    cl_step(&w, 0.2f);
    cl_hop(&w, 1, 0);
    cl_step(&w, 0.2f);
    CHECK(!w.dead && w.row == 2);
}
```

and in `main`:

```c
    test_water_drowns();
    test_pad_is_safe();
    test_log_carries();
    test_log_sweeps_off_field();
    test_hop_log_to_log();
```

- [ ] **Step 2: Run to verify it fails**

Run: `sh tests/host/run.sh`
Expected: FAIL for `test_water_drowns`, `test_pad_is_safe`, `test_log_sweeps_off_field` (nothing checks water yet).

- [ ] **Step 3: Write the implementation**

Add above `land` in `src/crossy_logic.c`:

```c
/* What holds the hero on a river lane: a log (returned in *log) or a lily pad. */
static bool support(const cl_lane_t *l, float x, const cl_obj_t **log)
{
    *log = NULL;
    for (int i = 0; i < l->nobj; i++) {
        const cl_obj_t *o = &l->obj[i];
        if (x >= o->x && x <= o->x + o->len) {
            *log = o;
            return true;
        }
    }
    int c = cl_col(x);
    return c >= 0 && c < CL_COLS && ((l->pad_mask >> c) & 1);
}
```

In `land`, before the coin check, add:

```c
    if (l->kind == CL_RIVER) {
        const cl_obj_t *log;
        if (!support(l, w->x, &log)) {
            die(w, CL_DEATH_WATER);
            return;
        }
    }
```

Replace the `else { cl_death_t h = hazard_hit(...) ... }` branch in `cl_step` with:

```c
    } else {
        const cl_lane_t *l = cl_lane_atc(w, w->row);
        if (l->kind == CL_RIVER) {
            const cl_obj_t *log;
            if (!support(l, w->x, &log)) {
                die(w, CL_DEATH_WATER);
            } else if (log) {
                w->x += (float)l->dir * l->speed * dt;
                if (w->x < 0.2f || w->x > CL_COLS - 0.2f) {
                    die(w, CL_DEATH_SWEPT);
                }
            }
        } else {
            cl_death_t h = hazard_hit(l, w->x);
            if (h != CL_DEATH_NONE) {
                die(w, h);
            }
        }
    }
```

- [ ] **Step 4: Run to verify it passes**

Run: `sh tests/host/run.sh`
Expected: `all crossy logic tests passed`.

- [ ] **Step 5: Commit**

```bash
git add src/crossy_logic.c tests/host/test_crossy_logic.c
git commit -m "Crossy rules: river, logs and lily pads" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Eagle and camera creep

**Files:**
- Modify: `src/crossy_logic.c`, `tests/host/test_crossy_logic.c`

**Interfaces:**
- Consumes: `die`, camera follow in `cl_step`.
- Produces: eagle state (`eagle_t`) and `CL_DEATH_EAGLE`; camera creep.

- [ ] **Step 1: Write the failing tests**

Add before `main`:

```c
static void test_eagle_after_idle(void)
{
    cl_world_t w;
    fresh(&w);
    run(&w, CL_IDLE_S + 0.1f);
    CHECK(!w.dead && w.eagle_t >= 0.0f);
    run(&w, CL_EAGLE_S + 0.2f);
    CHECK(w.dead && w.death == CL_DEATH_EAGLE);
}

static void test_progress_keeps_eagle_away(void)
{
    cl_world_t w;
    fresh(&w);
    for (int i = 0; i < 12; i++) {
        cl_hop(&w, 1, 0);
        run(&w, 1.0f);
    }
    CHECK(!w.dead && w.eagle_t < 0.0f && w.row == 12);
}

static void test_eagle_when_far_behind(void)
{
    cl_world_t w;
    fresh(&w);
    w.row = 5;
    w.best_row = 5;
    w.cam = 10.0f;
    cl_step(&w, DT);
    CHECK(w.eagle_t >= 0.0f);
}

static void test_camera_creeps(void)
{
    cl_world_t w;
    fresh(&w);
    cl_hop(&w, 1, 0);
    run(&w, 3.0f);
    CHECK(w.cam > 0.9f);
}
```

and in `main`:

```c
    test_eagle_after_idle();
    test_progress_keeps_eagle_away();
    test_eagle_when_far_behind();
    test_camera_creeps();
```

- [ ] **Step 2: Run to verify it fails**

Run: `sh tests/host/run.sh`
Expected: FAIL in all four new tests.

- [ ] **Step 3: Write the implementation**

In `cl_step`, replace the final camera block (`float lead = ...` through the closing `}` of the `if`) with:

```c
    if (w->moved) { /* the screen creeps forward, faster the further one gets */
        float best = (float)(w->best_row > 100 ? 100 : w->best_row);
        w->cam += (0.3f + 0.004f * best) * dt;
    }
    float lead = (float)w->row - CL_HERO_LEAD;
    if (w->cam < lead) {
        w->cam = lead;
    }

    bool behind = (float)w->row < w->cam + CL_HERO_LEAD - CL_BEHIND_ROWS;
    if (w->eagle_t < 0.0f && !w->dead && (w->idle >= CL_IDLE_S || behind)) {
        w->eagle_t = 0.0f;
    }
    if (w->eagle_t >= 0.0f && !w->dead) {
        w->eagle_t += dt;
        if (w->eagle_t >= CL_EAGLE_S) {
            die(w, CL_DEATH_EAGLE);
        }
    }
```

- [ ] **Step 4: Run to verify it passes**

Run: `sh tests/host/run.sh`
Expected: `all crossy logic tests passed`.

If `test_progress_keeps_eagle_away` fails because the camera creep outruns the hero (the hero hops once per second, creep is 0.3 rows/s), the hero should still be ahead. A failure points to a bug in `behind`, not in the test.

- [ ] **Step 5: Commit**

```bash
git add src/crossy_logic.c tests/host/test_crossy_logic.c
git commit -m "Crossy rules: idle eagle and camera creep" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Settings keys for best row and coins

**Files:**
- Modify: `src/settings.h`, `src/settings.c`

**Interfaces:**
- Produces: `settings_get_crossy_best()`, `settings_set_crossy_best(uint32_t)`, `settings_get_crossy_coins()`, `settings_add_crossy_coins(uint32_t)`.

There is no host test for NVS code (the existing settings have none). Check by building in Task 12.

- [ ] **Step 1: Declare the functions**

In `src/settings.h`, before the closing `#ifdef __cplusplus`, add:

```c
/* Best distance of the Crossy page, in rows, and the coins collected over all runs. */
uint32_t settings_get_crossy_best(void);
/** Stores the value only if it beats the current one. */
void settings_set_crossy_best(uint32_t rows);
uint32_t settings_get_crossy_coins(void);
/** Adds to the total; writes flash only if n is above 0. */
void settings_add_crossy_coins(uint32_t n);
```

- [ ] **Step 2: Implement**

In `src/settings.c`: after `#define KEY_HISCORE "hi_score"` add

```c
#define KEY_CR_BEST "cr_best"
#define KEY_CR_COINS "cr_coins"
```

after `static uint32_t s_high_score;` add

```c
static uint32_t s_cr_best;
static uint32_t s_cr_coins;
```

in `settings_init`, after the `s_high_score = v;` line add

```c
    if (nvs_get_u32(h, KEY_CR_BEST, &v) != ESP_OK) {
        v = 0;
    }
    s_cr_best = v;
    if (nvs_get_u32(h, KEY_CR_COINS, &v) != ESP_OK) {
        v = 0;
    }
    s_cr_coins = v;
```

and after `settings_clear_high_score` add

```c
uint32_t settings_get_crossy_best(void) { return s_cr_best; }

void settings_set_crossy_best(uint32_t rows)
{
    if (rows > s_cr_best) {
        s_cr_best = rows;
        save(KEY_CR_BEST, rows, NULL);
    }
}

uint32_t settings_get_crossy_coins(void) { return s_cr_coins; }

void settings_add_crossy_coins(uint32_t n)
{
    if (n > 0) {
        s_cr_coins += n;
        save(KEY_CR_COINS, s_cr_coins, NULL);
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add src/settings.h src/settings.c
git commit -m "Store the Crossy best distance and coin total in NVS" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Voxel renderer, ground chunks and scenery

**Files:**
- Create: `tools/crossy_art.py`, `tools/test_crossy_art.py`

**Interfaces:**
- Produces: `crossy_art.build()` returning `(sprites, groups)`:
  - `sprites`: `OrderedDict[name -> (PIL RGBA Image, (ax, ay))]`. `(ax, ay)` is the pixel inside the image that is the projection of model point (a=0, b=0, z=0).
  - `groups`: list of `(c_name, dims, names)`; `dims` is `()`, `(n,)` or `(n, m)`; `names` is a flat list in row-major order with `None` for "no sprite".
  - constants `CHUNK_CELLS = 8`, `GROUND_Z = (0, -2, -3, -2)`, `KEY565 = 0x07E0`.
- Later tasks add more sprites to `build()`; this task adds the renderer, ground, scenery.

Projection (same as the preview mockup): `x = 16 * (a + b)`, `y = -8 * a + 8 * b - z` (a grows up-right, b grows down-right, z is pixels up).

- [ ] **Step 1: Write the failing test**

Create `tools/test_crossy_art.py`:

```python
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import crossy_art as art


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


class SpriteChecks(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sprites, cls.groups = art.build()

    def test_every_sprite_has_pixels_and_a_valid_anchor(self):
        for name, (img, (ax, ay)) in self.sprites.items():
            self.assertTrue(1 <= img.width <= 260 and 1 <= img.height <= 130, name)
            self.assertTrue(0 <= ax <= img.width and 0 <= ay <= img.height, name)
            self.assertTrue(any(p[3] for p in img.getdata()), name)

    def test_no_art_pixel_uses_the_key_colour(self):
        for name, (img, _) in self.sprites.items():
            for r, g, b, a in img.getdata():
                if a:
                    self.assertNotEqual(rgb565(r, g, b), art.KEY565, name)

    def test_no_partial_alpha(self):
        for name, (img, _) in self.sprites.items():
            self.assertTrue(all(p[3] in (0, 255) for p in img.getdata()), name)

    def test_groups_name_existing_sprites(self):
        for cname, dims, names in self.groups:
            n = 1
            for d in dims:
                n *= d
            self.assertEqual(len(names), n, cname)
            for nm in names:
                self.assertTrue(nm is None or nm in self.sprites, (cname, nm))

    def test_every_sprite_is_in_a_group(self):
        used = {nm for _, _, names in self.groups for nm in names if nm}
        self.assertEqual(used, set(self.sprites), set(self.sprites) ^ used)

    def test_ground_chunks(self):
        for name in ("grass_a", "grass_b", "road", "river", "rail"):
            self.assertIn(name, self.sprites)
        img, _ = self.sprites["road"]
        self.assertEqual(img.width, 16 * (art.CHUNK_CELLS + 1) + 2) # 2 px margin

    def test_budget(self):
        total = sum(img.width * img.height * 2 for img, _ in self.sprites.values())
        self.assertLessEqual(total, 400000, total)

    def test_deterministic(self):
        again, _ = art.build()
        for name, (img, anchor) in self.sprites.items():
            self.assertEqual(img.tobytes(), again[name][0].tobytes(), name)
            self.assertEqual(anchor, again[name][1], name)


if __name__ == "__main__":
    unittest.main()
```

Note: `test_every_sprite_is_in_a_group` and the group tests only pass once Tasks 8 and 9 have finished adding sprites and groups. During this task they pass because `build()` returns groups for every sprite it makes. Keep that rule: every new sprite goes into a group in the same step.

- [ ] **Step 2: Run to verify it fails**

Run: `python3 -m unittest discover -s tools -p 'test_crossy_art.py'`
Expected: `ModuleNotFoundError: No module named 'crossy_art'`.

- [ ] **Step 3: Write the renderer, ground and scenery**

Create `tools/crossy_art.py`:

```python
"""Art of the Crossy page. Original voxel art, drawn with code: boxes with three shaded faces, projected
2:1 isometric. build() returns every sprite and the tables the firmware needs.

Axes: a grows up and to the right on screen (forward), b grows down and to the right, z is pixels up.
Projection: x = 16 * (a + b), y = -8 * a + 8 * b - z. Light comes from the top: the top face is the base
colour, the face toward -a is 78 %, the face toward +b is 60 %.

Each sprite has an anchor: the image pixel where model point (a=0, b=0, z=0) lands. The firmware puts a sprite
by placing that pixel on the projected corner of its cell.

make_crossy_assets.py turns the result into C arrays.
"""
import math
from collections import OrderedDict

from PIL import Image, ImageDraw

TW, TH = 16, 8
CHUNK_CELLS = 8
GROUND_Z = (0, -2, -3, -2)  # top of grass, road, river, rail relative to z = 0
KEY565 = 0x07E0


def sh(c, f):
    return tuple(max(0, min(255, int(v * f))) for v in c)


def project(a, b, z):
    return TW * (a + b), -TH * a + TH * b - z


class Model:
    def __init__(self):
        self.boxes = []

    def box(self, a0, b0, z0, da, db, dz, top, left=None, right=None, layer=0):
        left = left or sh(top, 0.78)
        right = right or sh(top, 0.6)
        self.boxes.append((layer, (b0 + db / 2) - (a0 + da / 2), z0, (a0, b0, z0, da, db, dz), top, left, right))

    def sprite(self):
        pts = []
        for _, _, _, (a0, b0, z0, da, db, dz), *_ in self.boxes:
            for a in (a0, a0 + da):
                for b in (b0, b0 + db):
                    for z in (z0, z0 + dz):
                        pts.append(project(a, b, z))
        minx = math.floor(min(p[0] for p in pts))
        miny = math.floor(min(p[1] for p in pts))
        maxx = math.ceil(max(p[0] for p in pts))
        maxy = math.ceil(max(p[1] for p in pts))
        w, h = maxx - minx + 2, maxy - miny + 2
        ax, ay = 1 - minx, 1 - miny
        im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(im)

        def P(a, b, z):
            x, y = project(a, b, z)
            return (round(x) + ax, round(y) + ay)

        for _, _, _, (a0, b0, z0, da, db, dz), top, left, right in sorted(self.boxes, key=lambda t: t[:3]):
            a1, b1, zt = a0 + da, b0 + db, z0 + dz
            d.polygon([P(a0, b0, zt), P(a1, b0, zt), P(a1, b1, zt), P(a0, b1, zt)], fill=top + (255,))
            d.polygon([P(a0, b0, zt), P(a0, b1, zt), P(a0, b1, z0), P(a0, b0, z0)], fill=left + (255,))
            d.polygon([P(a0, b1, zt), P(a1, b1, zt), P(a1, b1, z0), P(a0, b1, z0)], fill=right + (255,))
        return im, (ax, ay)


# ---- palette ----

GRASS = {"grass_a": (112, 200, 84), "grass_b": (98, 186, 72)}
ROAD = (90, 94, 106)
LINE = (236, 236, 232)
WATER = (74, 164, 232)
BED = (122, 100, 84)
SLEEPER = (84, 68, 60)
RAILC = (190, 192, 200)
TRUNK = (128, 84, 52)
LEAF = (62, 152, 72)


# ---- ground: one chunk of CHUNK_CELLS cells along b, one cell deep ----

def ground_chunk(kind):
    m = Model()
    for c in range(CHUNK_CELLS):
        alt = 1.03 if c & 1 else 0.97
        if kind in GRASS:
            m.box(0, c, -6, 1, 1, 6, sh(GRASS[kind], alt))
        elif kind == "road":
            m.box(0, c, -6, 1, 1, 4, sh(ROAD, alt))
            if c % 2 == 0:
                m.box(0.44, c + 0.15, -2, 0.12, 0.7, 0.4, LINE, layer=1)
        elif kind == "river":
            m.box(0, c, -6, 1, 1, 3, sh(WATER, 1.06 if c & 1 else 0.98))
        else:  # rail
            m.box(0, c, -6, 1, 1, 4, BED)
            m.box(0.1, c + 0.2, -2, 0.8, 0.18, 0.6, SLEEPER, layer=1)
            m.box(0.18, c, -1.4, 0.08, 1, 1.4, RAILC, layer=2)
            m.box(0.74, c, -1.4, 0.08, 1, 1.4, RAILC, layer=2)
    return m.sprite()


# ---- scenery ----

def pine():
    m = Model()
    m.box(0.4, 0.4, 0, 0.2, 0.2, 6, TRUNK)
    for i, s in enumerate((0.9, 0.7, 0.45)):
        m.box(0.5 - s / 2, 0.5 - s / 2, 6 + i * 6, s, s, 6, sh(LEAF, 1 + 0.1 * i))
    return m.sprite()


def round_tree():
    m = Model()
    m.box(0.4, 0.4, 0, 0.2, 0.2, 6, TRUNK)
    m.box(0.1, 0.1, 6, 0.8, 0.8, 13, (74, 170, 84))
    return m.sprite()


def tall_tree():
    m = Model()
    m.box(0.4, 0.4, 0, 0.2, 0.2, 8, TRUNK)
    m.box(0.15, 0.15, 8, 0.7, 0.7, 10, (60, 140, 70))
    m.box(0.25, 0.25, 18, 0.5, 0.5, 8, (82, 182, 92))
    return m.sprite()


def rock():
    m = Model()
    m.box(0.15, 0.15, 0, 0.7, 0.7, 5, (160, 162, 172))
    m.box(0.3, 0.3, 5, 0.4, 0.4, 3, (178, 180, 190))
    return m.sprite()


def bush():
    m = Model()
    m.box(0.15, 0.15, 0, 0.7, 0.7, 6, (76, 168, 82))
    m.box(0.3, 0.3, 6, 0.4, 0.4, 2, (92, 188, 96))
    return m.sprite()


def stump():
    m = Model()
    m.box(0.25, 0.25, 0, 0.5, 0.5, 5, (170, 124, 78), TRUNK, sh(TRUNK, 0.8))
    return m.sprite()


def log(length):
    m = Model()
    m.box(0.1, 0, 3, 0.8, length, 5, (150, 100, 55), (125, 82, 45), (214, 170, 110))
    return m.sprite()


def pad():
    m = Model()
    m.box(0.08, 0.08, 3, 0.84, 0.84, 1.5, (78, 190, 92))
    m.box(0.4, 0.4, 4.5, 0.2, 0.2, 1.5, (250, 150, 190))
    return m.sprite()


def build():
    sprites = OrderedDict()
    groups = []

    def add(name, spr):
        sprites[name] = spr
        return name

    kinds = ("grass_a", "grass_b", "road", "river", "rail")
    groups.append(("ca_ground", (5,), [add(k, ground_chunk(k)) for k in kinds]))
    props = [None, add("pine", pine()), add("round", round_tree()), add("tall", tall_tree()),
             add("rock", rock()), add("bush", bush()), add("stump", stump())]
    groups.append(("ca_prop", (7,), props))
    groups.append(("ca_log", (3,), [add("log%d" % n, log(n)) for n in (2, 3, 4)]))
    groups.append(("ca_pad", (), [add("pad", pad())]))
    return sprites, groups
```

- [ ] **Step 4: Run to verify it passes**

Run: `python3 -m unittest discover -s tools -p 'test_crossy_art.py'`
Expected: all tests pass (`OK`).

- [ ] **Step 5: Look at it**

Run: `python3 - <<'EOF'
import sys
sys.path.insert(0, "tools")
import crossy_art as a
from PIL import Image
spr, _ = a.build()
sheet = Image.new("RGBA", (900, 300), (150, 210, 150, 255))
x = 4
for name, (img, _) in spr.items():
    if img.width > 200:
        continue
    sheet.alpha_composite(img, (x, 20))
    x += img.width + 6
sheet.resize((1800, 600), Image.NEAREST).save("/tmp/sheet1.png")
EOF`
Then open the PNG with the Read tool. Expected: pine, round, tall trees, rock, bush, stump, three logs, a lily pad, all readable. Fix any wrong-looking model before committing.

- [ ] **Step 6: Commit**

```bash
git add tools/crossy_art.py tools/test_crossy_art.py
git commit -m "Crossy art: voxel renderer, ground chunks and scenery" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Hero, eagle, coin and animals

**Files:**
- Modify: `tools/crossy_art.py`

**Interfaces:**
- Consumes: `Model`, `sh`, `build()` from Task 7.
- Produces (new groups, exactly these names and shapes):
  - `ca_hero` `(4, 2)`: `[face][frame]`, face 0 forward (+row), 1 toward +x (+b), 2 back, 3 toward -x; frame 0 idle, 1 hop
  - `ca_hero_flat` `()`, `ca_splash` `(2,)`, `ca_eagle` `(2,)`, `ca_eagle_carry` `()`
  - `ca_deco` `(4, 2)`: `[cl_deco_t][frame]`, row 0 is `None, None`
  - `ca_coin` `(4,)`

- [ ] **Step 1: Add the hero builder with a rotation helper**

In `tools/crossy_art.py`, above `def build()`, add:

```python
def rot(face, u0, v0, du, dv):
    """A box given for a hero that looks toward +a (u forward, v right), turned to face 0..3.
    Turns about the centre of a 1 x 1 cell. Returns a0, b0, da, db."""
    pts = []
    for u in (u0, u0 + du):
        for v in (v0, v0 + dv):
            cu, cv = u - 0.5, v - 0.5
            a, b = ((cu, cv), (-cv, cu), (-cu, -cv), (cv, -cu))[face]
            pts.append((a + 0.5, b + 0.5))
    a0 = min(p[0] for p in pts)
    b0 = min(p[1] for p in pts)
    return a0, b0, max(p[0] for p in pts) - a0, max(p[1] for p in pts) - b0


WHITE = (250, 250, 248)
ORANGE = (255, 160, 40)
RED = (232, 44, 44)
DARK = (28, 28, 34)


def hero(face, hop):
    m = Model()

    def rb(u0, v0, z0, du, dv, dz, top, **kw):
        a0, b0, da, db = rot(face, u0, v0, du, dv)
        m.box(a0, b0, z0, da, db, dz, top, **kw)

    lift = 4 if hop else 0
    legs = 1 if hop else 2
    rb(0.3, 0.28, lift, 0.1, 0.1, legs, ORANGE)
    rb(0.3, 0.62, lift, 0.1, 0.1, legs, ORANGE)
    rb(0.2, 0.2, lift + legs, 0.6, 0.6, 12 - (1 if hop else 0), WHITE)
    rb(0.35, 0.12, lift + 5, 0.3, 0.08, 5, sh(WHITE, 0.92))  # wings
    rb(0.35, 0.8, lift + 5, 0.3, 0.08, 5, sh(WHITE, 0.92))
    rb(0.78, 0.3, lift + 9, 0.2, 0.4, 3, ORANGE)               # beak
    rb(0.42, 0.4, lift + legs + 12, 0.16, 0.2, 3, RED)         # comb
    rb(0.72, 0.18, lift + 10, 0.1, 0.1, 2, DARK)               # eyes
    rb(0.72, 0.72, lift + 10, 0.1, 0.1, 2, DARK)
    return m.sprite()


def hero_flat():
    m = Model()
    m.box(0.1, 0.1, 0, 0.8, 0.8, 2, WHITE)
    m.box(0.4, 0.4, 2, 0.2, 0.2, 0.5, RED)
    return m.sprite()


def splash(frame):
    m = Model()
    r = 0.25 + 0.2 * frame
    for k in range(8):
        ang = k * math.pi / 4
        a = 0.5 + r * math.cos(ang) - 0.06
        b = 0.5 + r * math.sin(ang) - 0.06
        m.box(a, b, 3 + (2 if frame == 0 else 0), 0.12, 0.12, 3 + 2 * (1 - frame), (190, 228, 255))
    return m.sprite()


def eagle(frame, carry=False):
    m = Model()
    brown, light = (110, 76, 52), (238, 232, 220)
    m.box(0.3, 0.3, 14, 0.4, 0.5, 6, brown)
    m.box(0.62, 0.4, 18, 0.22, 0.28, 5, light)             # head
    m.box(0.84, 0.46, 19, 0.16, 0.16, 2, (240, 190, 40))   # beak
    wing_z = 20 if frame == 0 else 12
    m.box(-0.5, 0.0, wing_z, 1.0, 0.3, 2, sh(brown, 0.9))
    m.box(-0.5, 0.7, wing_z, 1.0, 0.3, 2, sh(brown, 0.9))
    m.box(0.3, 0.1, 15, 0.4, 0.1, 3, brown)
    if carry:
        m.box(0.38, 0.38, 6, 0.24, 0.24, 8, WHITE)
        m.box(0.44, 0.44, 14, 0.12, 0.12, 1, ORANGE)
    return m.sprite()


def animal(kind, frame):
    m = Model()
    bob = frame
    if kind == "sheep":
        m.box(0.2, 0.15, 0, 0.6, 0.7, 7, (246, 246, 240))
        m.box(0.28, 0.8, 3 + bob, 0.44, 0.3, 6, (52, 52, 58))
    elif kind == "pig":
        m.box(0.2, 0.15, 0, 0.6, 0.7, 6, (250, 170, 186))
        m.box(0.28, 0.8, 2 + bob, 0.44, 0.3, 5, (240, 150, 172))
    else:
        m.box(0.15, 0.1, 0, 0.7, 0.8, 9, (246, 246, 246))
        m.box(0.15, 0.3, 0, 0.7, 0.25, 9.2, (62, 52, 52))
        m.box(0.25, 0.9, 4 + bob, 0.5, 0.3, 6, (232, 202, 192))
    return m.sprite()


def coin(frame):
    m = Model()
    face = (0.6, 0.42, 0.14, 0.42)[frame]
    m.box(0.5 - face / 2, 0.44, 5, face, 0.12, 8, (252, 206, 52), (232, 172, 32), (206, 144, 22))
    return m.sprite()
```

- [ ] **Step 2: Register them in `build()`**

In `build()`, before `return sprites, groups`, add:

```python
    faces = ("f", "r", "b", "l")
    groups.append(("ca_hero", (4, 2), [add("hero_%s%d" % (f, h), hero(i, h)) for i, f in enumerate(faces) for h in (0, 1)]))
    groups.append(("ca_hero_flat", (), [add("hero_flat", hero_flat())]))
    groups.append(("ca_splash", (2,), [add("splash%d" % i, splash(i)) for i in (0, 1)]))
    groups.append(("ca_eagle", (2,), [add("eagle%d" % i, eagle(i)) for i in (0, 1)]))
    groups.append(("ca_eagle_carry", (), [add("eagle_carry", eagle(1, carry=True))]))
    deco = [None, None]
    for k in ("sheep", "pig", "cow"):
        deco += [add("%s%d" % (k, i), animal(k, i)) for i in (0, 1)]
    groups.append(("ca_deco", (4, 2), deco))
    groups.append(("ca_coin", (4,), [add("coin%d" % i, coin(i)) for i in range(4)]))
```

- [ ] **Step 3: Run the tests**

Run: `python3 -m unittest discover -s tools -p 'test_crossy_art.py'`
Expected: `OK`.

- [ ] **Step 4: Look at it**

Render a sheet as in Task 7 Step 5 (all sprites that are narrower than 200 px, at 4x). Open it with the Read tool. Expected: the four hero facings are clearly different (beak points toward the facing), the hop frame is taller with tucked legs, the eagle has two wing positions, the three animals are distinct. Fix wrong-looking models.

- [ ] **Step 5: Commit**

```bash
git add tools/crossy_art.py
git commit -m "Crossy art: hero, eagle, coin and animals" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 9: Vehicles, train and signal

**Files:**
- Modify: `tools/crossy_art.py`

**Interfaces:**
- Produces (new groups):
  - `ca_vehicle` `(6, 2)`: `[cl_vehicle_t][heading]`; heading 0 moves toward +x (+b), 1 toward -x
  - `ca_loco` `(2,)`: `[heading]`; `ca_carriage` `()`; `ca_signal` `(3,)`: off, on frame 0, on frame 1

- [ ] **Step 1: Add the builders**

Above `def build()` add:

```python
GLASS = ((200, 230, 255), (170, 200, 230), (140, 170, 200))


def mirror_b(m_boxes, length):
    """Mirror a list of (a0, b0, z0, da, db, dz, top, left, right) along b, for the other heading."""
    out = []
    for a0, b0, z0, da, db, dz, top, left, right in m_boxes:
        out.append((a0, length - b0 - db, z0, da, db, dz, top, left, right))
    return out


def vehicle_boxes(kind):
    """Boxes of a vehicle that heads toward +b. Returns (boxes, length in cells)."""
    L = {"sedan": 1.4, "taxi": 1.4, "sport": 1.5, "pickup": 1.7, "truck": 2.2, "bus": 2.6}[kind]
    B = []

    def box(a0, b0, z0, da, db, dz, top, left=None, right=None):
        B.append((a0, b0, z0, da, db, dz, top, left or sh(top, 0.78), right or sh(top, 0.6)))

    def wheels():
        for wb in (0.15, L - 0.45):
            box(0.05, wb, 0, 0.1, 0.3, 3, DARK)
            box(0.85, wb, 0, 0.1, 0.3, 3, DARK)

    def lights():
        box(0.15, L - 0.02, 2, 0.18, 0.04, 1.5, (255, 236, 150))
        box(0.67, L - 0.02, 2, 0.18, 0.04, 1.5, (255, 236, 150))
        box(0.15, -0.02, 2, 0.18, 0.04, 1.5, (210, 40, 40))
        box(0.67, -0.02, 2, 0.18, 0.04, 1.5, (210, 40, 40))

    if kind == "sedan":
        box(0.12, 0, 0, 0.76, L, 6, (226, 52, 52))
        box(0.2, 0.3, 6, 0.6, 0.7, 5, GLASS[0], GLASS[1], GLASS[2])
    elif kind == "taxi":
        box(0.12, 0, 0, 0.76, L, 6, (250, 204, 40))
        box(0.2, 0.3, 6, 0.6, 0.7, 5, GLASS[0], GLASS[1], GLASS[2])
        box(0.4, 0.5, 11, 0.2, 0.3, 2, (250, 250, 250))
    elif kind == "sport":
        box(0.1, 0, 0, 0.8, L, 5, (52, 110, 232))
        box(0.22, 0.4, 5, 0.56, 0.55, 3.5, GLASS[0], GLASS[1], GLASS[2])
        box(0.42, 0.05, 5, 0.16, L - 0.1, 0.5, (240, 240, 240))
    elif kind == "pickup":
        box(0.12, 0, 0, 0.76, L, 5, (60, 160, 90))
        box(0.2, L - 0.8, 5, 0.6, 0.7, 4.5, GLASS[0], GLASS[1], GLASS[2])
        box(0.16, 0.08, 5, 0.68, 0.7, 2, (150, 150, 158))
    elif kind == "truck":
        box(0.12, 0, 0, 0.76, L, 4, (90, 90, 100))
        box(0.12, L - 0.7, 4, 0.76, 0.7, 5, (240, 150, 40))
        box(0.2, L - 0.5, 7, 0.6, 0.3, 2.5, GLASS[0], GLASS[1], GLASS[2])
        box(0.12, 0.02, 4, 0.76, L - 0.8, 8, (240, 240, 244))
    else:  # bus
        box(0.12, 0, 0, 0.76, L, 9, (228, 60, 56), (190, 225, 250), (150, 185, 215))
        box(0.12, 0, 9, 0.76, L, 1.5, (246, 246, 246))
        box(0.12, 0, 4, 0.76, L, 0.8, (246, 246, 246))
    wheels()
    lights()
    return B, L


def draw_boxes(boxes):
    m = Model()
    for a0, b0, z0, da, db, dz, top, left, right in boxes:
        m.box(a0, b0, z0, da, db, dz, top, left, right)
    return m.sprite()


def loco(heading):
    B = []
    L = 2.0

    def box(a0, b0, z0, da, db, dz, top):
        B.append((a0, b0, z0, da, db, dz, top, sh(top, 0.78), sh(top, 0.6)))

    box(0.1, 0, 2, 0.8, L, 8, (52, 86, 170))
    box(0.1, 0, 10, 0.8, L, 2, (236, 236, 238))
    box(0.1, 0, 2, 0.8, 0.7, 12, (46, 74, 150))
    box(0.14, 0.5, 8, 0.72, 0.2, 3, GLASS[0])
    box(0.4, 1.2, 12, 0.2, 0.2, 5, DARK)
    box(0.15, L - 0.04, 5, 0.18, 0.05, 2.5, (255, 236, 150))
    box(0.67, L - 0.04, 5, 0.18, 0.05, 2.5, (255, 236, 150))
    box(0.1, L - 0.4, 2, 0.8, 0.4, 1, (200, 50, 50))
    return draw_boxes(B if heading == 0 else mirror_b(B, L))


def carriage():
    m = Model()
    m.box(0.1, 0, 2, 0.8, 2.0, 9, (52, 86, 170), (46, 74, 150), (36, 60, 120))
    m.box(0.1, 0, 11, 0.8, 2.0, 2, (236, 236, 238))
    for k in range(3):
        m.box(0.08, 0.2 + k * 0.6, 5, 0.04, 0.4, 4, (255, 230, 120), layer=1)
    return m.sprite()


def signal(state):
    m = Model()
    m.box(0.45, 0.45, 0, 0.1, 0.1, 14, (82, 82, 88))
    m.box(0.3, 0.3, 14, 0.4, 0.4, 5, (40, 40, 46))
    off, on = (80, 40, 40), (255, 70, 70)
    left = on if state == 1 else off
    right = on if state == 2 else off
    m.box(0.28, 0.34, 16, 0.06, 0.1, 2, left, layer=1)
    m.box(0.28, 0.56, 16, 0.06, 0.1, 2, right, layer=1)
    return m.sprite()
```

- [ ] **Step 2: Register them in `build()`**

Before `return sprites, groups` add:

```python
    veh = []
    for kind in ("sedan", "taxi", "sport", "pickup", "truck", "bus"):
        boxes, length = vehicle_boxes(kind)
        veh.append(add("car_%s_p" % kind, draw_boxes(boxes)))
        veh.append(add("car_%s_m" % kind, draw_boxes(mirror_b(boxes, length))))
    groups.append(("ca_vehicle", (6, 2), veh))
    groups.append(("ca_loco", (2,), [add("loco_p", loco(0)), add("loco_m", loco(1))]))
    groups.append(("ca_carriage", (), [add("carriage", carriage())]))
    groups.append(("ca_signal", (3,), [add("signal%d" % i, signal(i)) for i in range(3)]))
```

- [ ] **Step 3: Run the tests**

Run: `python3 -m unittest discover -s tools -p 'test_crossy_art.py'`
Expected: `OK`, including `test_budget` (at most 400 000 bytes). If the budget fails, shrink `CHUNK_CELLS`' chunk count in Task 11 is not an option (the layout assumes 8). Reduce the number of ground variants or sprite height instead, and tell the user.

- [ ] **Step 4: Look at it**

Render a sheet of all vehicles (both headings), the loco, carriage and signals at 4x, and open it with the Read tool. Expected: each of the six vehicles is clearly different; heading `_p` shows headlights on the right face, `_m` shows tail lights there. Fix wrong-looking models.

- [ ] **Step 5: Commit**

```bash
git add tools/crossy_art.py
git commit -m "Crossy art: six vehicles, train and signal" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Bake the sprites into C, with a contact sheet

**Files:**
- Create: `tools/make_crossy_assets.py`
- Generate: `src/crossy_assets.c`, `src/crossy_assets.h`

**Interfaces:**
- Consumes: `crossy_art.build()` groups and `CHUNK_CELLS`, `GROUND_Z`, `KEY565`.
- Produces for `src/crossy.c`:

```c
typedef struct { const lv_img_dsc_t *img; int16_t ax, ay; } ca_sprite_t;
#define CA_CHUNK_CELLS 8
extern const int8_t ca_ground_z[4];            /* by cl_kind_t */
extern const ca_sprite_t ca_ground[5];         /* grass_a, grass_b, road, river, rail */
extern const ca_sprite_t ca_prop[7];           /* by cl_prop_t; [0].img is NULL */
extern const ca_sprite_t ca_log[3];            /* length 2, 3, 4 */
extern const ca_sprite_t ca_pad;
extern const ca_sprite_t ca_hero[4][2];
extern const ca_sprite_t ca_hero_flat;
extern const ca_sprite_t ca_splash[2];
extern const ca_sprite_t ca_eagle[2];
extern const ca_sprite_t ca_eagle_carry;
extern const ca_sprite_t ca_deco[4][2];        /* by cl_deco_t; [0][*].img is NULL */
extern const ca_sprite_t ca_coin[4];
extern const ca_sprite_t ca_vehicle[6][2];     /* by cl_vehicle_t, heading 0 = +x, 1 = -x */
extern const ca_sprite_t ca_loco[2];
extern const ca_sprite_t ca_carriage;
extern const ca_sprite_t ca_signal[3];
void ca_to_psram(void);                         /* copy all pixel data to PSRAM; call once at page creation */
```

- [ ] **Step 1: Write the baker**

Create `tools/make_crossy_assets.py`:

```python
#!/usr/bin/env python3
"""Write the art of the Crossy page as LVGL 8 images: src/crossy_assets.c and src/crossy_assets.h.

Usage: tools/make_crossy_assets.py                 (needs Pillow)
       tools/make_crossy_assets.py --sheet FILE    (also writes a contact sheet PNG, for a look)

The art is in tools/crossy_art.py. Pixel format (see src/lv_conf.h: LV_COLOR_16_SWAP = 1):
LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED, 2 bytes per pixel, RGB565 high byte first. Transparent pixels are the
key colour, pure green (RGB565 0x07E0).
"""
import argparse
import os

from PIL import Image

import crossy_art as art

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def pixels(img):
    data = bytearray()
    for r, g, b, a in zip(*[iter(img.tobytes())] * 4):
        c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3) if a else art.KEY565
        data += bytes((c >> 8, c & 0xFF))
    return data


def c_init(dims, names, sprites):
    def one(n):
        if n is None:
            return "{NULL, 0, 0}"
        return "{&ca_img_%s, %d, %d}" % (n, *sprites[n][1])

    if not dims:
        return one(names[0])
    if len(dims) == 1:
        return "{" + ", ".join(one(n) for n in names) + "}"
    rows, cols = dims
    return "{" + ", ".join("{" + ", ".join(one(n) for n in names[r * cols:(r + 1) * cols]) + "}"
                           for r in range(rows)) + "}"


def c_decl(cname, dims):
    return "const ca_sprite_t %s%s" % (cname, "".join("[%d]" % d for d in dims))


def write_c(path, sprites, groups):
    out = ["/* Generated by tools/make_crossy_assets.py. Do not edit. */", "#include <string.h>", "",
           '#include "esp_heap_caps.h"', '#include "esp_log.h"', "", '#include "crossy_assets.h"', ""]
    for name, (img, _) in sprites.items():
        data = pixels(img)
        out.append("static const uint8_t ca_map_%s[] = {" % name)
        for i in range(0, len(data), 24):
            out.append("    " + ", ".join("0x%02x" % v for v in data[i:i + 24]) + ",")
        out.append("};")
        out.append("static lv_img_dsc_t ca_img_%s = {" % name)
        out.append("    .header = {.cf = LV_IMG_CF_TRUE_COLOR_CHROMA_KEYED, .always_zero = 0, .reserved = 0, "
                   ".w = %d, .h = %d}," % img.size)
        out.append("    .data_size = %d,\n    .data = ca_map_%s,\n};\n" % (len(data), name))
    out.append("const int8_t ca_ground_z[4] = {%s};\n" % ", ".join(str(z) for z in art.GROUND_Z))
    for cname, dims, names in groups:
        out.append("%s = %s;" % (c_decl(cname, dims), c_init(dims, names, sprites)))
    out += ["", "/* The sprites are read from flash at first. PSRAM is faster to read, so the page moves them there once. */",
            "static lv_img_dsc_t *const ca_all[] = {" + ", ".join("&ca_img_%s" % n for n in sprites) + "};", "",
            "void ca_to_psram(void)", "{", "    size_t total = 0;",
            "    for (size_t i = 0; i < sizeof(ca_all) / sizeof(ca_all[0]); i++) {",
            "        total += ca_all[i]->data_size;", "    }",
            "    uint8_t *p = heap_caps_malloc(total, MALLOC_CAP_SPIRAM);", "    if (!p) {",
            '        ESP_LOGW("crossy", "no PSRAM for the sprites; drawing them from flash");', "        return;", "    }",
            "    for (size_t i = 0; i < sizeof(ca_all) / sizeof(ca_all[0]); i++) {",
            "        memcpy(p, ca_all[i]->data, ca_all[i]->data_size);", "        ca_all[i]->data = p;",
            "        p += ca_all[i]->data_size;", "    }", "}"]
    open(path, "w").write("\n".join(out) + "\n")


def write_h(path, groups):
    h = ["/* Generated by tools/make_crossy_assets.py. Do not edit. */", "#pragma once", "", "#include <stdint.h>",
         "", "#include <lvgl.h>", "",
         "/* A sprite and its anchor: the image pixel where the corner of its cell lands. */",
         "typedef struct {", "    const lv_img_dsc_t *img;", "    int16_t ax, ay;", "} ca_sprite_t;", "",
         "#define CA_CHUNK_CELLS %d /* a ground chunk covers this many cells along a lane */" % art.CHUNK_CELLS, "",
         "/* Height of the lane surface (grass, road, river, rail) against the grass top, in pixels. */",
         "extern const int8_t ca_ground_z[4];", ""]
    for cname, dims, _ in groups:
        h.append("extern %s;" % c_decl(cname, dims))
    open(path, "w").write("\n".join(h) + "\n")


def sheet(path, sprites):
    names = list(sprites)
    cols, pad = 10, 8
    cw = max(i.width for i, _ in sprites.values() if i.width < 200) + pad
    ch = max(i.height for i, _ in sprites.values() if i.width < 200) + pad
    small = [n for n in names if sprites[n][0].width < 200]
    rows = (len(small) + cols - 1) // cols
    im = Image.new("RGBA", (cols * cw, rows * ch), (150, 210, 150, 255))
    for k, n in enumerate(small):
        img = sprites[n][0]
        im.alpha_composite(img, ((k % cols) * cw + pad // 2, (k // cols) * ch + pad // 2))
    im.resize((im.width * 3, im.height * 3), Image.NEAREST).save(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--sheet", metavar="FILE", help="also write a contact sheet PNG")
    args = ap.parse_args()
    sprites, groups = art.build()
    write_c(os.path.join(ROOT, "src", "crossy_assets.c"), sprites, groups)
    write_h(os.path.join(ROOT, "src", "crossy_assets.h"), groups)
    total = sum(len(pixels(i)) for i, _ in sprites.values())
    print("%d images, %d bytes of pixel data" % (len(sprites), total))
    if args.sheet:
        sheet(args.sheet, sprites)


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it**

Run: `python3 tools/make_crossy_assets.py --sheet /tmp/sheet.png`
Expected: a line like `NN images, NNNNNN bytes of pixel data` with the byte count at most 400 000, and the two generated files exist.

- [ ] **Step 3: Syntax-check the generated C**

Run: `printf '#include <stdint.h>\ntypedef struct {int w;} lv_img_dsc_t;\n' > /dev/null; sed -n 1,20p src/crossy_assets.h`
Then confirm by reading the header that every table from the Interfaces block above is declared with the shapes given. (The C file is compiled for real in Task 12 with LVGL available.)

- [ ] **Step 4: Show the contact sheet to the user**

Open the sheet PNG with the Read tool and look at it. Tell the user the path and ask for art changes before the view is built on top of it. The user approved the look in the earlier mockup; this is the first look at the real sprites.

- [ ] **Step 5: Commit**

```bash
git add tools/make_crossy_assets.py src/crossy_assets.c src/crossy_assets.h
git commit -m "Bake the Crossy sprites into C arrays" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 11: View, stage 1: world, lanes, props, hero, camera

**Files:**
- Create: `src/crossy.h`, `src/crossy.c`

**Interfaces:**
- Consumes: Task 1 to 5 logic API, Task 10 `ca_*` tables, Task 6 settings (stage 3 uses them), `ui_touch_swallowed()`, `ui_set_swipe_lock()`, `ui_set_background()`.
- Produces: `crossy_create(lv_obj_t *tile)`, `crossy_set_visible(bool)`, `crossy_set_dragging(bool)`. Stage 1 shows ground, props and the hero, and runs the title demo scroll plus a play state; movers, coins, deaths and HUD arrive in stages 2 and 3.

The geometry (all derived once, keep these constants):

- Screen position of a point: `sx = CX + 16 * (a + b)`, `sy = CY - 8 * a + 8 * b - z`, with `a = row - cam`, `b = x - 5.5`, `CX = 160`, `CY = 210`. The hero (row = cam + 2, x = 5.5) lands near (200, 190).
- Rows that can reach the screen: `cam - 11` to `cam + 24`.
- Objects live in world coordinates inside one big container: `wx = WX0 + 16 * (a_rel + b)`, `wy = WY0 - 8 * a_rel + 8 * b - z`, with `a_rel = row - base`, `WX0 = 260`, `WY0 = 1000`. The container is `WORLD_W x WORLD_H = 1800 x 1300`, and moves each frame to `x = CX - WX0 + 16 * (base - cam)`, `y = CY - WY0 + 8 * (cam - base)`. All world coordinates stay positive (LVGL coordinates are 16 bit and children outside a parent are clipped), so `base` is moved forward (all lanes rebuilt) once `cam - base > 68`.

- [ ] **Step 1: Write the header**

Create `src/crossy.h`:

```c
#pragma once

#include <stdbool.h>

#include <lvgl.h>

/** Build the Crossy page on `tile`. Call with the LVGL lock held, after settings_init(). */
void crossy_create(lv_obj_t *tile);

/** Tell the page whether it is the one on screen. Hidden, it stops its timer and goes back to the title. */
void crossy_set_visible(bool visible);

/** A swipe between pages starts or ends. */
void crossy_set_dragging(bool dragging);
```

- [ ] **Step 2: Write `src/crossy.c` (stage 1)**

```c
#include "crossy.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "crossy_assets.h"
#include "crossy_logic.h"
#include "settings.h"
#include "ui.h"

/* The Crossy page: an isometric hopping game. Tap to hop forward, swipe to hop sideways or back. The rules
 * are in crossy_logic.c; this file shows them with LVGL images and handles touch and the three states
 * (title, playing, game over). One timer moves everything.
 *
 * Drawing: one big container ("world") holds one container per lane ("slot"). Slots are stacked far to
 * near, so a nearer lane covers a farther one. The world container moves with the camera; objects inside
 * it keep their place, and only moving things are updated each frame. */

static const char *TAG = "crossy";

#define FIELD_W 480
#define FIELD_H 292
#define TICK_MS 33
#define RETRY_LOCK_MS 600
#define SWIPE_MIN_PX 14
#define SLOTS 40
#define ROWS_BEHIND 11
#define ROWS_AHEAD 24
#define REBASE_AT 68
#define TILE_W 16
#define TILE_H 8
#define CX 160 /* where the camera row meets the screen */
#define CY 210
#define WX0 260 /* world container: origin of world coordinates, size */
#define WY0 1000
#define WORLD_W 1800
#define WORLD_H 1300
#define CENTRE_X (CL_COLS / 2.0f)
#define HOP_ARC_PX 10.0f

#define COLOR_BG lv_color_hex(0x0B0D12)
#define COLOR_CARD lv_color_hex(0x1A1D26)
#define COLOR_BORDER lv_color_hex(0x2A2F3C)
#define COLOR_TEXT lv_color_hex(0xE5E7EB)
#define COLOR_MUTED lv_color_hex(0x8B93A5)
#define COLOR_OK lv_color_hex(0x22C55E)
#define COLOR_GOLD lv_color_hex(0xF5B921)

typedef enum { ST_TITLE, ST_PLAY, ST_DEAD } state_t;

typedef struct {
    int32_t row; /* INT32_MIN: nothing built */
    lv_obj_t *box;
} slot_t;

static lv_obj_t *s_field, *s_world, *s_hero;
static slot_t s_slot[SLOTS];

static cl_world_t s_world_state;
static state_t s_state = ST_TITLE;
static bool s_visible;
static bool s_dragging;
static lv_timer_t *s_timer;
static uint32_t s_last, s_tick, s_runs;
static float s_cam_v;  /* the camera as drawn: follows the rules' camera smoothly */
static int32_t s_base; /* row that world coordinates count from */

/* ---- helpers ---- */

static lv_obj_t *inert(lv_obj_t *o)
{
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static void show(lv_obj_t *o, bool on)
{
    if (on == !lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) {
        return;
    }
    if (on) {
        lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
}

static void set_src(lv_obj_t *o, const ca_sprite_t *s)
{
    if (lv_img_get_src(o) != s->img) {
        lv_img_set_src(o, s->img);
    }
}

static lv_obj_t *new_img(lv_obj_t *parent, const ca_sprite_t *s)
{
    lv_obj_t *o = lv_img_create(parent);
    lv_img_set_src(o, s->img);
    return inert(o);
}

/* Put a sprite so that the corner of its cell (row, x = left edge) is at height z. World coordinates. */
static void put(lv_obj_t *o, const ca_sprite_t *s, float row, float x, float z)
{
    float a = row - (float)s_base, b = x - CENTRE_X;
    int wx = (int)lroundf(WX0 + TILE_W * (a + b));
    int wy = (int)lroundf(WY0 - TILE_H * a + TILE_H * b - z);
    lv_obj_set_pos(o, wx - s->ax, wy - s->ay);
}

static float surface_z(int32_t row)
{
    return (float)ca_ground_z[cl_lane_atc(&s_world_state, row)->kind];
}

/* ---- lanes ---- */

static slot_t *slot_of(int32_t row) { return &s_slot[row % SLOTS]; }

static void build_slot(slot_t *s, int32_t row)
{
    if (lv_obj_get_parent(s_hero) == s->box) {
        lv_obj_set_parent(s_hero, s_world); /* the hero must not die with the lane */
    }
    lv_obj_clean(s->box);
    s->row = row;
    lv_obj_move_background(s->box); /* a lane built later is farther: it goes to the back */

    const cl_lane_t *l = cl_lane_atc(&s_world_state, row);
    float gz = (float)ca_ground_z[l->kind];
    const ca_sprite_t *g = &ca_ground[l->kind == CL_GRASS ? (row & 1) : l->kind + 1];
    for (int k = 0; k < 4; k++) {
        put(new_img(s->box, g), g, (float)row, CL_X_MIN + (float)(k * CA_CHUNK_CELLS), 0.0f);
    }
    for (int i = 0; i < CL_PCOLS; i++) {
        if (l->prop[i]) {
            const ca_sprite_t *p = &ca_prop[l->prop[i]];
            put(new_img(s->box, p), p, (float)row, (float)(i - CL_EDGE), gz);
        }
    }
}

static void rebase(void)
{
    s_base = (int32_t)floorf(s_cam_v) - ROWS_BEHIND - 1;
    for (int i = 0; i < SLOTS; i++) {
        s_slot[i].row = INT32_MIN;
    }
}

static void sync_slots(void)
{
    int32_t lo = (int32_t)floorf(s_cam_v) - ROWS_BEHIND, hi = (int32_t)floorf(s_cam_v) + ROWS_AHEAD;
    if (lo < 0) {
        lo = 0;
    }
    for (int32_t r = lo; r <= hi; r++) { /* ascending: the farthest lane ends at the back */
        slot_t *s = slot_of(r);
        if (s->row != r) {
            build_slot(s, r);
        }
    }
}

/* ---- the hero ---- */

static void place_hero(void)
{
    const cl_world_t *w = &s_world_state;
    float p = w->hop_t > 0.0f ? 1.0f - w->hop_t / CL_HOP_S : 1.0f;
    float row = (float)w->from_row + (float)(w->row - w->from_row) * p;
    float x = w->from_x + (w->x - w->from_x) * p;
    float gz = surface_z(w->from_row) + (surface_z(w->row) - surface_z(w->from_row)) * p;
    float arc = w->hop_t > 0.0f ? HOP_ARC_PX * sinf(3.14159265f * p) : 0.0f;
    const ca_sprite_t *spr = &ca_hero[w->face][w->hop_t > 0.0f ? 1 : 0];
    set_src(s_hero, spr);
    int32_t front = w->row < w->from_row ? w->row : w->from_row; /* in front of both lanes it passes */
    lv_obj_t *box = slot_of(front)->box;
    if (lv_obj_get_parent(s_hero) != box) {
        lv_obj_set_parent(s_hero, box);
    }
    put(s_hero, spr, row, x - 0.5f, gz + arc);
}

/* ---- drawing ---- */

static void render(void)
{
    float diff = s_world_state.cam - s_cam_v;
    s_cam_v = fabsf(diff) > 4.0f ? s_world_state.cam : s_cam_v + diff * 0.35f;
    if (s_cam_v - (float)s_base > REBASE_AT) {
        rebase();
    }
    sync_slots();
    lv_obj_set_pos(s_world, (lv_coord_t)lroundf(CX - WX0 + TILE_W * ((float)s_base - s_cam_v)),
                   (lv_coord_t)lroundf(CY - WY0 + TILE_H * (s_cam_v - (float)s_base)));
    place_hero();
}

/* ---- states and timer (stage 1: a plain run, no HUD yet) ---- */

static uint32_t next_seed(void) { return lv_tick_get() ^ (++s_runs * 2654435761u); }

static void new_run(void)
{
    cl_reset(&s_world_state, next_seed());
    s_cam_v = s_world_state.cam;
    rebase();
}

static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    float dt = (float)(now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.05f) {
        dt = 0.05f;
    }
    if (lv_disp_get_inactive_time(NULL) > settings_get_timeout_s() * 1000u) {
        return; /* the screensaver or the dark screen is on: nothing here may draw */
    }
    s_tick++;
    cl_step(&s_world_state, dt);
    render();
}

/* ---- building the page ---- */

void crossy_create(lv_obj_t *tile)
{
    ca_to_psram(); /* before any image is made: PSRAM is faster to read than flash */
    s_field = inert(lv_obj_create(tile));
    lv_obj_remove_style_all(s_field);
    lv_obj_set_size(s_field, FIELD_W, FIELD_H);
    lv_obj_set_pos(s_field, 0, 0);

    lv_obj_t *band = inert(lv_obj_create(tile)); /* hides the page dots' strip behind the world */
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, FIELD_W, 320 - FIELD_H);
    lv_obj_set_pos(band, 0, FIELD_H);
    lv_obj_set_style_bg_color(band, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);

    s_world = inert(lv_obj_create(s_field));
    lv_obj_remove_style_all(s_world);
    lv_obj_set_size(s_world, WORLD_W, WORLD_H);
    for (int i = 0; i < SLOTS; i++) {
        s_slot[i].row = INT32_MIN;
        s_slot[i].box = inert(lv_obj_create(s_world));
        lv_obj_remove_style_all(s_slot[i].box);
        lv_obj_set_size(s_slot[i].box, WORLD_W, WORLD_H);
        lv_obj_set_pos(s_slot[i].box, 0, 0);
    }
    s_hero = new_img(s_world, &ca_hero[0][0]);

    s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_timer_pause(s_timer);
    new_run();
    render();
    ESP_LOGI(TAG, "page built");
}

void crossy_set_dragging(bool dragging) { s_dragging = dragging; }

void crossy_set_visible(bool visible)
{
    if (visible == s_visible) {
        return;
    }
    s_visible = visible;
    if (visible) {
        s_last = lv_tick_get();
        lv_timer_resume(s_timer);
    } else {
        lv_timer_pause(s_timer);
        new_run();
        render();
    }
}
```

Stage 1 is deliberately plain: the hero cannot be steered yet, so it will stand and the eagle will end the run. That is enough to check the look and the speed of the ground on hardware (Task 14). Touch input, states and HUD come in stage 3.

- [ ] **Step 3: Commit**

```bash
git add src/crossy.h src/crossy.c
git commit -m "Crossy view, stage 1: world, lanes, props and hero" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Wire the page into the UI, fix the shared background, build

**Files:**
- Modify: `src/ui.c`, `src/game.c`

**Interfaces:**
- Consumes: `crossy_create`, `crossy_set_visible`, `crossy_set_dragging`.

The Game page sets the screen background from its own visible flag. Two game pages would overwrite each other (the page that hides could turn the background on again after the other page turned it off, and a drag end on a hidden page does the same). Rule after this task: a page touches the background only while it is visible, plus once when it hides. The hide call must run before the show call of the next page (`page_changed_cb` below does that).

- [ ] **Step 1: `game.c`: background only while visible**

In `src/game.c`, replace `update_background` with:

```c
static void update_background(void)
{
    if (s_visible) {
        ui_set_background(s_dragging);
    }
}
```

and in `game_set_visible`, in the `else` branch (page hidden), add `ui_set_background(true);` as the first statement. The `else` branch then starts:

```c
    } else {
        ui_set_background(true); /* the next page decides again when it shows */
        lv_timer_pause(s_timer);
```

(`game_set_visible` already calls `update_background()` right after `s_visible = visible;`, which now does nothing when hiding. That is correct.)

- [ ] **Step 2: `ui.c`: page count, tile, hooks**

In `src/ui.c`:

1. Include: after `#include "game.h"` add `#include "crossy.h"`.
2. Replace

```c
#define PAGES 5
#define GAME_PAGE 3 /* Media, Apps, AI, Game, Settings */
```

with

```c
#define PAGES 6
#define GAME_PAGE 3 /* Media, Apps, AI, Game, Crossy, Settings */
#define CROSSY_PAGE 4
```

3. Naming (see Global Constraints): change the comment of `PAGES` to `/* Media, Apps, AI, Game - LabEscape, Game - Crossy, Settings */`; change the Settings sidebar entry `menu_entry(menu, root, "Game", game_page);` to `"Game - LabEscape"`, the comment `/* Game: the best score ...` to `/* Game - LabEscape: ...`, and the clear-score message to `"The best score of Game - LabEscape goes back to 0 m."`. If the longer entry text does not fit the sidebar on the board (Task 13 checks), use `"LabEscape"` and tell the user.
4. In `tile_scroll_cb` add a second line:

```c
static void tile_scroll_cb(lv_event_t *e)
{
    bool drag = lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN;
    game_set_dragging(drag);
    crossy_set_dragging(drag);
}
```

5. In `page_changed_cb`, replace `game_set_visible(active == GAME_PAGE);` with:

```c
    /* The page that hides goes first: each game page sets the screen background when it shows. */
    if (active == CROSSY_PAGE) {
        game_set_visible(false);
        crossy_set_visible(true);
    } else {
        crossy_set_visible(false);
        game_set_visible(active == GAME_PAGE);
    }
```

6. In `ui_create`, after the `game` tile line add

```c
    lv_obj_t *crossy = tile_prepare(lv_tileview_add_tile(s_tileview, CROSSY_PAGE, 0, LV_DIR_HOR));
```

and after `game_create(game);` add `crossy_create(crossy);`.

The Settings tile already uses `PAGES - 1`, which is now 5. The edge wrap-around code uses `PAGES - 1` too.

- [ ] **Step 3: Build**

Run: `~/.venvs/platformio/bin/pio run 2>&1 | tail -40`
Expected: `SUCCESS`, and a size report with the flash use under 100 % of the 1.5 MB app slot. Record the numbers. Fix any compile error in the new files (the most likely: LVGL API names. Check against `.pio/libdeps/LVGL-320-480/lvgl/src` after the first build).

While the build runs for the first time it fetches dependencies (needs network).

- [ ] **Step 4: Check three LVGL assumptions in the library source**

Run: `grep -rn "CHROMA_KEYED" .pio/libdeps/LVGL-320-480/lvgl/src/draw/sw/lv_draw_sw_img.c | head -5` and `grep -n "lv_obj_move_background\|lv_obj_clean" .pio/libdeps/LVGL-320-480/lvgl/src/core/lv_obj_tree.h`
Expected: chroma-keyed drawing exists in the SW image path, and both functions exist. If not, stop and tell the user before going on.

- [ ] **Step 5: Commit**

```bash
git add src/ui.c src/game.c
git commit -m "Add the Crossy page before Settings and share the screen background" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 13: Hardware check 1: ground speed and look

**Files:** none changed unless the check fails.

This check decides whether the ground drawing is fast enough, before more is built on it. The plan assumes big chroma-keyed lane images draw fast enough on this panel. That is not measured yet.

- [ ] **Step 1: Add a temporary on-screen frame counter**

In `src/crossy.c` add (temporary, marked `TEMP` so it is easy to find) a label updated in `tick_cb`: count `tick_cb` runs per second using `lv_tick_get()` and show `"%u/s"` in the top-left corner of `s_field`, font `lv_font_montserrat_20`, white. Also make the hero hop forward once per second in `tick_cb` (`if (s_tick % 30 == 0) cl_hop(&s_world_state, 1, 0);`, also marked `TEMP`) so the camera scrolls and lanes are rebuilt, as in a real run.

- [ ] **Step 2: Build and ask the user to flash**

Run: `~/.venvs/platformio/bin/pio run 2>&1 | tail -5`

Then tell the user, in plain text:
"Hold the on-screen 'USB connected' label for 3 s until the board reboots into download mode. Tell me when it is done, then I run the upload."
After the user confirms, run: `~/.venvs/platformio/bin/pio run -t upload --upload-port /dev/ttyACM0 2>&1 | tail -15`

- [ ] **Step 3: Ask the user for the numbers and a look**

Ask the user to swipe to the Crossy page (page 5 of 6, just before Settings) and tell me (a) the number on the screen (b) whether the ground, trees and hero look right (c) whether swiping to and from the page leaves ghosting, (d) whether the Settings entry "Game - LabEscape" fits the sidebar without cut-off text.

To judge the speed fairly, also read the same counter on "Game - LabEscape" while a run is going (add the same TEMP counter there, marked `TEMP`, and remove it after) and report both numbers. The target for Crossy is within 20 % of it.

- [ ] **Step 4: Decide**

- Within 20 % of the Lab Escape number (and 15/s or more) and no drawing faults: go on to Task 14.
- Slower: stop. Do not build more on this. Candidate fixes, in this order, each measured the same way: (1) cut each ground chunk into two images of 4 cells (less overdraw of empty corners), (2) draw lane tops with `lv_draw_polygon` in one custom draw callback instead of images, (3) lower `ROWS_AHEAD`, (4) hide objects that are fully off screen (`show(o, false)`) when a lane is built or updated. The PSRAM copy and the background skip are already in. Tell the user which one was tried and the numbers.
- Wrong colours or green edges around sprites: the chroma key is not working. Switch the baker to `LV_IMG_CF_TRUE_COLOR_ALPHA` (3 bytes per pixel, as `tools/make_game_assets.py` does) and check the 400 000-byte budget again.

- [ ] **Step 5: Remove the TEMP code and commit any real fix**

```bash
git add -A src tools
git commit -m "Crossy view: <what changed, only if a fix was needed>" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

If nothing changed besides TEMP code, run `git checkout src/crossy.c` instead of committing.

---

### Task 14: View, stage 2: vehicles, logs, pads, trains, signals, coins, animals, deaths, eagle

**Files:**
- Modify: `src/crossy.c`

**Interfaces:**
- Consumes: lane fields `obj`, `nobj`, `dir`, `pad_mask`, `signal`, `train`, `coin_col`, `deco_kind`, `deco_col`; `ca_vehicle`, `ca_log`, `ca_pad`, `ca_loco`, `ca_carriage`, `ca_signal`, `ca_coin`, `ca_deco`, `ca_hero_flat`, `ca_splash`, `ca_eagle`, `ca_eagle_carry`.

- [ ] **Step 1: Add the per-lane object handles**

In `slot_t` add:

```c
    lv_obj_t *mover[CL_MAX_OBJ]; /* vehicles or logs */
    lv_obj_t *train[4];          /* locomotive and three carriages */
    lv_obj_t *signal[2];
    lv_obj_t *coin, *deco;
```

- [ ] **Step 2: Build them in `build_slot`**

In `build_slot`, after `lv_obj_clean(s->box);` add `memset(s->mover, 0, sizeof(s->mover)); memset(s->train, 0, sizeof(s->train)); memset(s->signal, 0, sizeof(s->signal)); s->coin = s->deco = NULL;`. At the end of `build_slot` add (pads first, then deco, coin, movers, so movers draw over pads and props):

```c
    for (int c = 0; c < CL_COLS; c++) {
        if ((l->pad_mask >> c) & 1) {
            put(new_img(s->box, &ca_pad), &ca_pad, (float)row, (float)c, gz);
        }
    }
    if (l->deco_kind) {
        s->deco = new_img(s->box, &ca_deco[l->deco_kind][0]);
        put(s->deco, &ca_deco[l->deco_kind][0], (float)row, (float)l->deco_col, gz);
    }
    if (l->coin_col >= 0) {
        s->coin = new_img(s->box, &ca_coin[0]);
    }
    if (l->kind == CL_ROAD) {
        for (int i = 0; i < l->nobj; i++) {
            s->mover[i] = new_img(s->box, &ca_vehicle[l->obj[i].kind][l->dir > 0 ? 0 : 1]);
        }
    } else if (l->kind == CL_RIVER) {
        for (int i = 0; i < l->nobj; i++) {
            s->mover[i] = new_img(s->box, &ca_log[(int)l->obj[i].len - 2]);
        }
    } else if (l->kind == CL_RAIL) {
        for (int i = 0; i < 4; i++) {
            s->train[i] = new_img(s->box, i == 0 ? &ca_loco[l->dir > 0 ? 0 : 1] : &ca_carriage);
            show(s->train[i], false);
        }
        for (int i = 0; i < 2; i++) {
            s->signal[i] = new_img(s->box, &ca_signal[0]);
            put(s->signal[i], &ca_signal[0], (float)row, i == 0 ? -1.0f : (float)CL_COLS, gz);
        }
    }
```

- [ ] **Step 3: Update the moving things each frame**

Add above `render`:

```c
static void update_slot(slot_t *s)
{
    const cl_lane_t *l = cl_lane_atc(&s_world_state, s->row);
    float gz = (float)ca_ground_z[l->kind];
    float row = (float)s->row;
    if (l->kind == CL_ROAD || l->kind == CL_RIVER) {
        for (int i = 0; i < l->nobj; i++) {
            const ca_sprite_t *spr = l->kind == CL_ROAD ? &ca_vehicle[l->obj[i].kind][l->dir > 0 ? 0 : 1]
                                                         : &ca_log[(int)l->obj[i].len - 2];
            put(s->mover[i], spr, row, l->obj[i].x, gz);
        }
    } else if (l->kind == CL_RAIL) {
        for (int i = 0; i < 4; i++) {
            show(s->train[i], l->train);
        }
        if (l->train) {
            float x = l->obj[0].x;
            for (int i = 0; i < 4; i++) { /* the locomotive leads, whatever the direction */
                float cell = l->dir > 0 ? x + CL_TRAIN_LEN - 2.0f - 2.0f * (float)i : x + 2.0f * (float)i;
                put(s->train[i], i == 0 ? &ca_loco[l->dir > 0 ? 0 : 1] : &ca_carriage, row, cell, gz);
            }
        }
        const ca_sprite_t *sig = &ca_signal[l->signal ? 1 + ((s_tick / 6) & 1) : 0];
        for (int i = 0; i < 2; i++) {
            set_src(s->signal[i], sig);
        }
    }
    if (s->coin) {
        bool there = l->coin_col >= 0;
        show(s->coin, there);
        if (there) {
            const ca_sprite_t *c = &ca_coin[(s_tick / 3) & 3];
            set_src(s->coin, c);
            put(s->coin, c, row, (float)l->coin_col, gz);
        }
    }
    if (s->deco) {
        const ca_sprite_t *d = &ca_deco[l->deco_kind][((s_tick / 12) + (uint32_t)l->deco_col) & 1];
        set_src(s->deco, d);
        put(s->deco, d, row, (float)l->deco_col, gz);
    }
}
```

In `render`, after `sync_slots();` add:

```c
    int32_t lo = (int32_t)floorf(s_cam_v) - ROWS_BEHIND, hi = (int32_t)floorf(s_cam_v) + ROWS_AHEAD;
    for (int32_t r = lo < 0 ? 0 : lo; r <= hi; r++) {
        update_slot(slot_of(r));
    }
```

Note on the coin sprite: the moved coin at `put(..., coin_col, ...)` is placed per frame, so a coin picked up (the logic sets `coin_col = -1`) disappears at once.

- [ ] **Step 4: Deaths and the eagle**

Add three objects to the field (not the world), created in `crossy_create` after `s_hero`:

```c
static lv_obj_t *s_eagle, *s_fx; /* the eagle, and a splash or flat hero */
```

```c
    s_fx = new_img(s_world, &ca_splash[0]);
    show(s_fx, false);
    s_eagle = new_img(s_field, &ca_eagle[0]);
    show(s_eagle, false);
```

Add above `render` and call at its end:

```c
/* Screen position of a world point (row, x) at height z: the camera is already in the world container. */
static void screen_of(float row, float x, float z, int *sx, int *sy)
{
    float a = row - s_cam_v, b = x - CENTRE_X;
    *sx = (int)lroundf(CX + TILE_W * (a + b));
    *sy = (int)lroundf(CY - TILE_H * a + TILE_H * b - z);
}

static void fx_into(lv_obj_t *box)
{
    if (lv_obj_get_parent(s_fx) != box) {
        lv_obj_set_parent(s_fx, box);
    }
}

static void place_effects(void)
{
    const cl_world_t *w = &s_world_state;
    bool dead = w->dead;
    cl_death_t d = w->death;
    bool hero_gone = dead && (d == CL_DEATH_WATER || d == CL_DEATH_SWEPT || d == CL_DEATH_EAGLE);
    bool flat = dead && (d == CL_DEATH_CAR || d == CL_DEATH_TRAIN);
    show(s_hero, !hero_gone && !flat);
    if (flat) {
        set_src(s_fx, &ca_hero_flat);
        fx_into(slot_of(w->row)->box);
        show(s_fx, true);
        put(s_fx, &ca_hero_flat, (float)w->row, w->x - 0.5f, surface_z(w->row));
    } else if (dead && (d == CL_DEATH_WATER || d == CL_DEATH_SWEPT)) {
        const ca_sprite_t *sp = &ca_splash[w->death_t < 0.2f ? 0 : 1];
        set_src(s_fx, sp);
        fx_into(slot_of(w->row)->box);
        put(s_fx, sp, (float)w->row, w->x - 0.5f, surface_z(w->row));
        show(s_fx, w->death_t < 0.5f);
    } else {
        show(s_fx, false);
    }

    /* The eagle swoops in from the upper left toward the hero, then carries it off to the upper right. */
    int hx, hy;
    screen_of((float)w->row, w->x - 0.5f, surface_z(w->row), &hx, &hy);
    if (dead && d == CL_DEATH_EAGLE) {
        float t = w->death_t / 1.0f;
        const ca_sprite_t *e = &ca_eagle_carry;
        set_src(s_eagle, e);
        lv_obj_set_pos(s_eagle, hx + (int)(300.0f * t) - e->ax, hy - (int)(260.0f * t) - e->ay);
        show(s_eagle, t < 1.0f);
    } else if (!dead && w->eagle_t >= 0.0f) {
        float t = w->eagle_t / CL_EAGLE_S;
        t = t * t;
        const ca_sprite_t *e = &ca_eagle[(s_tick / 3) & 1];
        set_src(s_eagle, e);
        lv_obj_set_pos(s_eagle, hx - (int)(260.0f * (1.0f - t)) - e->ax, hy - (int)(220.0f * (1.0f - t)) - e->ay);
        show(s_eagle, true);
    } else {
        show(s_eagle, false);
    }
}
```

and add `place_effects();` as the last line of `render`.

- [ ] **Step 5: Build, then commit**

Run: `~/.venvs/platformio/bin/pio run 2>&1 | tail -15`
Expected: `SUCCESS`.

```bash
git add src/crossy.c
git commit -m "Crossy view, stage 2: vehicles, logs, trains, coins, animals, deaths, eagle" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 15: View, stage 3: input, states, HUD, title, game-over panel

**Files:**
- Modify: `src/crossy.c`

**Interfaces:**
- Consumes: `ui_touch_swallowed()`, `ui_set_swipe_lock(bool)`, `ui_set_background(bool)`, `settings_get_crossy_best/set_crossy_best/add_crossy_coins/get_crossy_coins`, `cl_hop`, `cl_step`, `cl_animate`, `cl_generate_to`, `cl_score`.

Controls: a tap hops forward; a swipe up hops forward, a swipe left or right hops toward -x or +x (up-left or down-right on screen), a swipe down hops back. A finger movement under 14 px is a tap. The hop is sent when the finger lifts.

- [ ] **Step 1: Replace the stage 1 state code**

Remove the stage 1 `new_run`, `tick_cb`, TEMP code and `crossy_set_*` bodies, and add these. First the extra statics (near the other statics):

```c
static lv_obj_t *s_score, *s_score_sh, *s_coins, *s_coin_icon;
static lv_obj_t *s_title, *s_title_name, *s_title_tap, *s_title_best;
static lv_obj_t *s_panel, *s_panel_score, *s_panel_best, *s_panel_coins;
static bool s_down, s_edge;
static state_t s_press_state;
static lv_point_t s_p0;
static int s_tap_dr, s_tap_dc;
static uint32_t s_dead_ms;
static uint32_t s_score_shown = UINT32_MAX;
static int s_coins_shown = -1;
```

Helpers (same style as `src/game.c`):

```c
static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return inert(l);
}

static lv_obj_t *card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *c = inert(lv_obj_create(parent));
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, w, h);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_bg_color(c, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 2, 0);
    lv_obj_set_style_border_color(c, COLOR_BORDER, 0);
    return c;
}

/* The screen background is only needed while two pages are on screen (a swipe); the world covers the field. */
static void update_background(void)
{
    if (s_visible) {
        ui_set_background(s_dragging);
    }
}

/* No page swipe during a run. After a crash the lock stays until the finger that was down lifts. */
static void sync_swipe_lock(void)
{
    ui_set_swipe_lock(s_visible && (s_state == ST_PLAY || (s_state == ST_DEAD && s_down && s_press_state == ST_PLAY)));
}

static void update_best_label(void)
{
    lv_label_set_text_fmt(s_title_best, "BEST %u   COINS %u", (unsigned)settings_get_crossy_best(),
                          (unsigned)settings_get_crossy_coins());
}
```

State changes:

```c
static uint32_t next_seed(void) { return lv_tick_get() ^ (++s_runs * 2654435761u); }

static void new_world(void)
{
    cl_reset(&s_world_state, next_seed());
    s_cam_v = s_world_state.cam;
    rebase();
}

static void enter_title(void)
{
    s_state = ST_TITLE;
    new_world();
    update_best_label();
    show(s_score, false);
    show(s_score_sh, false);
    show(s_coins, false);
    show(s_coin_icon, false);
    show(s_panel, false);
    show(s_title, true);
    sync_swipe_lock();
}

static void start_play(void)
{
    s_state = ST_PLAY;
    new_world();
    s_score_shown = UINT32_MAX;
    s_coins_shown = -1;
    show(s_title, false);
    show(s_panel, false);
    show(s_score, true);
    show(s_score_sh, true);
    show(s_coins, true);
    show(s_coin_icon, true);
    sync_swipe_lock();
}

static void panel_y_cb(void *obj, int32_t v) { lv_obj_set_y(obj, v); }

static void enter_dead(void)
{
    s_state = ST_DEAD;
    s_dead_ms = lv_tick_get();
    uint32_t score = cl_score(&s_world_state);
    bool best = score > settings_get_crossy_best();
    settings_set_crossy_best(score); /* flash is written only for a new record */
    settings_add_crossy_coins((uint32_t)s_world_state.coins);
    lv_label_set_text_fmt(s_panel_score, "%u", (unsigned)score);
    lv_label_set_text(s_panel_best, best ? "NEW BEST!" : "");
    if (!best) {
        lv_label_set_text_fmt(s_panel_best, "BEST %u", (unsigned)settings_get_crossy_best());
    }
    lv_obj_set_style_text_color(s_panel_best, best ? COLOR_OK : COLOR_MUTED, 0);
    lv_label_set_text_fmt(s_panel_coins, "+%d coins", s_world_state.coins);
    show(s_panel, true);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, panel_y_cb);
    lv_anim_set_values(&a, -190, 58);
    lv_anim_set_time(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    sync_swipe_lock();
}
```

Timer:

```c
static void tick_cb(lv_timer_t *t)
{
    (void)t;
    uint32_t now = lv_tick_get();
    float dt = (float)(now - s_last) / 1000.0f;
    s_last = now;
    if (dt > 0.05f) {
        dt = 0.05f; /* after a pause or a slow frame: do not jump */
    }
    /* While the screensaver or the dark screen is on, nothing here may draw. */
    if (lv_disp_get_inactive_time(NULL) > settings_get_timeout_s() * 1000u) {
        s_edge = false;
        return;
    }
    bool tap = s_edge && !ui_touch_swallowed(); /* a swipe between pages, or the touch that only woke the screen */
    s_edge = false;
    s_tick++;

    switch (s_state) {
    case ST_TITLE: /* the camera drifts over the generated lanes behind the title */
        if (tap) {
            start_play();
            break;
        }
        cl_animate(&s_world_state, dt);
        s_world_state.cam += 0.8f * dt;
        cl_generate_to(&s_world_state, (int32_t)s_world_state.cam + CL_AHEAD);
        show(s_hero, false);
        break;
    case ST_PLAY:
        if (tap) {
            cl_hop(&s_world_state, s_tap_dr, s_tap_dc);
        }
        cl_step(&s_world_state, dt);
        if (cl_score(&s_world_state) != s_score_shown) {
            s_score_shown = cl_score(&s_world_state);
            lv_label_set_text_fmt(s_score, "%u", (unsigned)s_score_shown);
            lv_label_set_text_fmt(s_score_sh, "%u", (unsigned)s_score_shown);
        }
        if (s_world_state.coins != s_coins_shown) {
            s_coins_shown = s_world_state.coins;
            lv_label_set_text_fmt(s_coins, "%d", s_coins_shown);
        }
        if (s_world_state.dead) {
            enter_dead();
        }
        break;
    case ST_DEAD:
        cl_step(&s_world_state, dt); /* only the death animation runs */
        if (tap && now - s_dead_ms >= RETRY_LOCK_MS) {
            start_play();
        }
        break;
    }
    sync_swipe_lock();
    render();
    if (s_state == ST_TITLE) {
        show(s_hero, false); /* render() places the hero; the title has none */
    }
}
```

Touch:

```c
static void touch_cb(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_down = true;
        s_press_state = s_state;
        lv_indev_get_point(lv_indev_get_act(), &s_p0);
        break;
    case LV_EVENT_RELEASED: {
        lv_point_t p;
        lv_indev_get_point(lv_indev_get_act(), &p);
        s_down = false;
        int dx = p.x - s_p0.x, dy = p.y - s_p0.y;
        if (abs(dx) < SWIPE_MIN_PX && abs(dy) < SWIPE_MIN_PX) { /* a tap hops forward */
            s_tap_dr = 1;
            s_tap_dc = 0;
        } else if (abs(dx) > abs(dy)) { /* left and right swipes: toward -x and +x */
            s_tap_dr = 0;
            s_tap_dc = dx > 0 ? 1 : -1;
        } else { /* up: forward, down: back */
            s_tap_dr = dy < 0 ? 1 : -1;
            s_tap_dc = 0;
        }
        /* An action starts when the finger lifts, so a swipe can tell itself from a tap. A finger that
         * was down during the run until the crash is no retry. */
        s_edge = s_press_state == s_state;
        break;
    }
    case LV_EVENT_PRESS_LOST: /* the touch turned into a swipe between pages */
        s_down = false;
        break;
    default:
        break;
    }
}
```

Note: `abs` needs `#include <stdlib.h>`.

- [ ] **Step 2: Build the HUD, title card and panel in `crossy_create`**

In `crossy_create`: make the field clickable and attach the touch handler right after `lv_obj_set_pos(s_field, 0, 0);` (replace the `inert(...)` use there):

```c
    s_field = lv_obj_create(tile);
    lv_obj_remove_style_all(s_field);
    lv_obj_set_size(s_field, FIELD_W, FIELD_H);
    lv_obj_set_pos(s_field, 0, 0);
    lv_obj_clear_flag(s_field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_field, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_field, touch_cb, LV_EVENT_ALL, NULL);
```

After the eagle, add the HUD and cards:

```c
    s_score_sh = label(s_field, "0", &lv_font_montserrat_28, lv_color_hex(0x203040));
    lv_obj_set_pos(s_score_sh, 18, 14);
    s_score = label(s_field, "0", &lv_font_montserrat_28, lv_color_white());
    lv_obj_set_pos(s_score, 16, 12);
    s_coin_icon = new_img(s_field, &ca_coin[0]);
    lv_obj_set_pos(s_coin_icon, 420, 4);
    s_coins = label(s_field, "0", &lv_font_montserrat_20, COLOR_GOLD);
    lv_obj_set_pos(s_coins, 446, 14);

    s_title = card(s_field, 260, 150);
    lv_obj_set_pos(s_title, 110, 56);
    s_title_name = label(s_title, "BIRD HOP", &lv_font_montserrat_44, COLOR_GOLD);
    lv_obj_align(s_title_name, LV_ALIGN_TOP_MID, 0, 14);
    s_title_tap = label(s_title, "Tap to Start", &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_align(s_title_tap, LV_ALIGN_TOP_MID, 0, 74);
    s_title_best = label(s_title, "", &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_align(s_title_best, LV_ALIGN_TOP_MID, 0, 114);

    s_panel = card(s_field, 240, 176);
    lv_obj_set_pos(s_panel, 120, 58);
    lv_obj_t *over = label(s_panel, "Game Over", &lv_font_montserrat_20, COLOR_MUTED);
    lv_obj_align(over, LV_ALIGN_TOP_MID, 0, 14);
    s_panel_score = label(s_panel, "0", &lv_font_montserrat_48, COLOR_TEXT);
    lv_obj_align(s_panel_score, LV_ALIGN_TOP_MID, 0, 40);
    s_panel_best = label(s_panel, "", &lv_font_montserrat_20, COLOR_MUTED);
    lv_obj_align(s_panel_best, LV_ALIGN_TOP_MID, 0, 100);
    s_panel_coins = label(s_panel, "", &lv_font_montserrat_16, COLOR_GOLD);
    lv_obj_align(s_panel_coins, LV_ALIGN_TOP_MID, 0, 128);
    lv_obj_t *retry = label(s_panel, "Tap to retry", &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_align(retry, LV_ALIGN_TOP_MID, 0, 150);
```

and replace the end of `crossy_create` (the `new_run(); render();` lines) with `enter_title(); render();`.

- [ ] **Step 3: Visibility hooks**

```c
void crossy_set_dragging(bool dragging)
{
    s_dragging = dragging;
    update_background();
}

void crossy_set_visible(bool visible)
{
    if (visible == s_visible) {
        return;
    }
    s_visible = visible;
    update_background();
    if (visible) {
        update_best_label(); /* only a run changes it, but a new page entry is a cheap place to refresh */
        s_last = lv_tick_get();
        lv_timer_resume(s_timer);
    } else {
        ui_set_background(true); /* the next page decides again when it shows */
        lv_timer_pause(s_timer);
        s_down = false;
        s_edge = false;
        enter_title(); /* the page that scrolls back in shows the title again */
        render();
        show(s_hero, false);
    }
}
```

- [ ] **Step 4: Build, then commit**

Run: `~/.venvs/platformio/bin/pio run 2>&1 | tail -15`
Expected: `SUCCESS`. Fix compile errors (a likely one: `render()` is declared after `enter_title`; move the `render` prototype up with `static void render(void);` near the top).

```bash
git add src/crossy.c
git commit -m "Crossy view, stage 3: touch, states, HUD, title and game-over panel" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 16: Hardware check 2 and a speed number

**Files:** none changed unless a check fails.

- [ ] **Step 1: Add a temporary measurement table**

Add (marked `TEMP`) a label in the corner of the field that shows ticks per second, as in Task 13 Step 1, plus the number of live LVGL objects is not needed. Keep it only for this check.

- [ ] **Step 2: Build and flash**

Run: `~/.venvs/platformio/bin/pio run 2>&1 | tail -6`
Tell the user, in plain text: "Hold the on-screen 'USB connected' label for 3 s until the board reboots into download mode. Tell me when it is done." After the user confirms, run `~/.venvs/platformio/bin/pio run -t upload --upload-port /dev/ttyACM0 2>&1 | tail -15`.

- [ ] **Step 3: Ask the user to check, and report back**

Ask the user to check each item and answer yes or no, with the number from the screen:

1. Swiping to and from the Crossy page shows no ghosting; Settings is still the last page; page dots show 6.
2. Title shows, the lanes drift behind it, a tap starts a run.
3. A tap hops forward; swipes left, right and down hop that way; a swipe does not change the page during a run.
4. Cars kill; a log carries the bird; water kills; a train kills after the signal blinks; standing still brings the eagle.
5. A coin is picked up and the count rises; the game-over panel shows the score, best and coins.
6. The best score and coin total are still there after a power cycle.
7. Updates per second, from the number on the screen (target at least 20, and within 20 % of "Game - LabEscape").
8. "Game - LabEscape" still looks and plays as before, at its usual speed.

- [ ] **Step 4: Fix what failed**

For each "no": use superpowers:systematic-debugging. Do not guess. If the speed is under 20/s but over 15/s, report it and ask the user whether to tune further. Remove the TEMP label before the last commit.

- [ ] **Step 5: Commit any fixes**

```bash
git add -A src tools
git commit -m "Crossy: <what was fixed>" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
```

---

### Task 17: Documentation, memory, pull request

**Files:**
- Modify: `README.md`

- [ ] **Step 1: README section**

In `README.md`: (a) rename the heading `## Game` to `## Game - LabEscape`, the bullet `- **Game:**` to `- **Game - LabEscape:**`, `See "Game" below` to `See "Game - LabEscape" below`, and "While the Game page is up" to "While the Game - LabEscape page is up"; add a bullet for Crossy after it in the page list at the top, named `- **Game - Crossy:**`. (b) After the whole LabEscape section (it ends before "### Display speed" or the next "##"; read the file first), add:

```markdown
## Game - Crossy

An isometric hopping game on its own page, just before Settings. Tap to hop forward, swipe left or right to
hop sideways, swipe down to hop back. Cross grass, roads, rivers (ride the logs, use the lily pads) and
rails (watch the signal), collect coins, and keep moving: after about 6 s without progress an eagle takes the
bird. The best distance and the coin total are kept in NVS.

The rules are in `src/crossy_logic.c` and are tested on a PC with `tests/host/run.sh`. The speed measures of
"Display speed" apply to it too, and its sprites are copied to PSRAM at start. All art is original
voxel art drawn by `tools/crossy_art.py`; `tools/make_crossy_assets.py` bakes it into `src/crossy_assets.c`
(add `--sheet FILE.png` for a contact sheet of every sprite).
```

- [ ] **Step 2: Final checks**

Run: `sh tests/host/run.sh && python3 -m unittest discover -s tools -p 'test_crossy_art.py' && ~/.venvs/platformio/bin/pio run 2>&1 | tail -8`
Expected: both test suites pass and the build is `SUCCESS`. Quote the actual output in the report to the user.

- [ ] **Step 3: Save project memory**

Update `<Claude memory dir>/macropad-project-state.md`: pages are now Media, Apps, AI, Game, Crossy, Settings; add one paragraph for the Crossy page (files, controls, the measured speed from Task 16, what the user verified, what is open). Update the one-line hook in `MEMORY.md` to match. Do not duplicate what the repo already records.

- [ ] **Step 4: Commit and open the pull request**

```bash
git add README.md
git commit -m "Document the Crossy page" -m "Co-Authored-By: Claude Sonnet 5.5 <noreply@anthropic.com>"
git push -u origin CodyECSL/crossy-road-clone
gh pr create --base main --title "Add the Crossy page" --body "<summary, the test and measurement results from Task 16, and the open items>

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

Ask the user before the push and the pull request (outward-facing step).

---

## Self-Review

**Spec coverage:**
- Page order and `ui.c` change: Task 12.
- Files list: logic T1-5, assets T7-10, view T11/14/15, settings T6, README T17.
- View (isometric 2:1, bands, depth order, hop, camera): T11, T14. No partial refresh or overlays: Global Constraints; HUD and cards are opaque.
- Rules: input T15; grass props T1/T2; road T3; river T4; rail T3; eagle T5; score, seed, difficulty T1/T2 (`diff` in the generator); title demo T15; dead panel and 600 ms lock T15.
- Assets: hero T8, scenery T7, vehicles and rail T9, animals T8, eagle T8, coin T8.
- Size and speed: budget test T7 and T9; speed checks T13 and T16.
- Checks: host tests T1-5, contact sheet T10, build T12, hardware T13/T16.
- Out of scope items: nothing in the plan builds skins, sound or multiplayer, and nothing changes the Game page except the background fix, which the plan flags as needed.

**Gaps noted:** the spec says "ambient animals" and "settings.c NVS keys"; both are covered. There is no Settings-page UI for Crossy scores (the spec does not ask for one).

**Type consistency check:** `cl_lane_at` (non-const) and `cl_lane_atc` (const) are used consistently; tests use `cl_lane_at`, the view uses `cl_lane_atc`. `ca_*` table names and shapes in Task 10 match the group names in Tasks 7 to 9 and the uses in Tasks 11, 14, 15. `coin_col` (an `int8_t`, -1 for none) replaced a coin mask everywhere. `hazard_hit`, `support`, `die`, `land`, `start_hop` are static in `crossy_logic.c` only.

**Known risks, in the plan on purpose:** (1) the speed of the big chroma-keyed lane images is unmeasured; Task 13 measures it before more work and lists fallbacks. (2) The chroma key depends on LVGL treating `0x07E0` as transparent in the swapped colour format; Task 12 Step 4 checks the library source and Task 13 checks on the screen. (3) A world container of 1800 x 1300 px with about 40 lane containers is a new pattern here; if LVGL clips or lags, the fallback is to place objects in screen coordinates every frame (slower, but simple).
