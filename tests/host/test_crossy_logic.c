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
/* The cells of a lane that a hero can stand on (bit c = column c). */
static unsigned stand_bits(const cl_lane_t *l)
{
    unsigned all = (1u << CL_COLS) - 1u, m = 0;
    if (l->kind == CL_GRASS) {
        for (int c = 0; c < CL_COLS; c++) {
            m |= (unsigned)(l->prop[c + CL_EDGE] == CL_P_NONE) << c;
        }
        return m;
    }
    return l->kind == CL_RIVER && l->nobj == 0 ? l->pad_mask : all;
}

/* No pocket: from each run of standable cells in lane p, a tap can reach a free cell of grass lane l. */
static bool has_way_on(const cl_lane_t *p, const cl_lane_t *l)
{
    unsigned st = stand_bits(p), fr = stand_bits(l);
    for (int a = 0; a < CL_COLS; a++) {
        if (!((st >> a) & 1) || (a > 0 && ((st >> (a - 1)) & 1))) {
            continue; /* not the start of a run */
        }
        int b = a;
        while (b + 1 < CL_COLS && ((st >> (b + 1)) & 1)) {
            b++;
        }
        bool ok = false;
        for (int c = a > 0 ? a - 1 : 0; c <= (b < CL_COLS - 1 ? b + 1 : CL_COLS - 1); c++) {
            ok |= (fr >> c) & 1;
        }
        if (!ok) {
            return false;
        }
    }
    return true;
}

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
                CHECK(has_way_on(p, l));
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
                if (l->nobj > 0 && p->kind == CL_RIVER && p->nobj > 0) { /* log lanes in a row: opposite ways, full logs */
                    CHECK(l->dir == -p->dir && l->nobj == CL_MAX_OBJ);
                }
                if (l->nobj == 0) { /* a pad lane: a pad within one column of every cell, so a tap always has a way */
                    for (int c = 0; c < CL_COLS; c++) {
                        unsigned near = 7u << (c > 0 ? c - 1 : 0);
                        near = c == 0 ? 3u : near;
                        CHECK((l->pad_mask & near) != 0);
                    }
                }
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
    CHECK(!w.dead);
}

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

static void test_log_holds_hero_at_edge_until_it_passes(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    add_log(cl_lane_at(&w, 1), 4.0f, 6.0f, 3.0f, 1);
    cl_hop(&w, 1, 0);
    run(&w, 2.0f); /* the log reaches the edge: the hero stays at the edge, alive */
    CHECK(!w.dead && w.x > 10.7f && w.x <= CL_COLS - 0.2f + 0.001f);
    run(&w, 4.0f); /* the log has slid out from under the hero */
    CHECK(w.dead && w.death == CL_DEATH_WATER);
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

/* Speed limits of a log lane at a row, as the generator uses them. */
static float log_speed_hi(int32_t row) { return 1.5f + 1.5f * (float)(row > 100 ? 100 : row) / 100.0f; }

static void test_log_speeds_vary(void)
{
    for (uint32_t seed = 1; seed <= 40; seed++) {
        cl_world_t w;
        cl_reset(&w, seed);
        float speeds[512];
        int n = 0;
        float prev = -1.0f;
        for (int32_t r = 3; r < 600; r++) {
            cl_generate_to(&w, r);
            const cl_lane_t *l = cl_lane_atc(&w, r), *p = cl_lane_atc(&w, r - 1);
            if (l->kind != CL_RIVER || l->nobj == 0) {
                continue;
            }
            float hi = log_speed_hi(r);
            CHECK(l->speed >= 0.5f - 0.001f && l->speed <= hi + 0.001f);
            if (p->kind == CL_RIVER && p->nobj > 0) { /* neighbours differ clearly */
                CHECK(fabsf(l->speed - prev) >= 0.3f * (hi - 0.5f) - 0.001f);
            }
            /* the time a log covers a column centre is far above one hop, in every lane */
            float shortest = l->obj[0].len;
            for (int i = 1; i < l->nobj; i++) {
                shortest = l->obj[i].len < shortest ? l->obj[i].len : shortest;
            }
            CHECK((shortest - 0.4f) / l->speed >= 0.6f);
            prev = l->speed;
            if (n < 512) {
                speeds[n++] = l->speed;
            }
        }
        for (int a = 0; a + 20 <= n; a += 20) { /* in each 20 log lanes the fastest is 2 times the slowest */
            float lo = speeds[a], hi = speeds[a];
            for (int i = a; i < a + 20; i++) {
                lo = speeds[i] < lo ? speeds[i] : lo;
                hi = speeds[i] > hi ? speeds[i] : hi;
            }
            CHECK(hi >= 2.0f * lo);
        }
    }
}

/* Roads, rails and log lanes become plain lanes, so a bot that only taps meets no hazard. */
static void calm_lanes(cl_world_t *w)
{
    for (int32_t r = w->row; r <= w->row + CL_AHEAD; r++) {
        cl_lane_t *l = cl_lane_at(w, r);
        if (l->kind == CL_ROAD || l->kind == CL_RAIL) {
            l->kind = CL_GRASS;
            l->nobj = 0;
        } else if (l->kind == CL_RIVER && l->nobj > 0) {
            l->nobj = 0;
            l->pad_mask = (uint16_t)((1u << CL_COLS) - 1u);
        }
    }
}

/* Every coin lies where the tap rules take the bird, so a tap-only bot collects each coin it passes. */
static void test_coins_are_on_the_tap_path(void)
{
    int collected = 0, checked = 0;
    for (uint32_t seed = 1; seed <= 150; seed++) {
        cl_world_t w;
        cl_reset(&w, seed);
        calm_lanes(&w);
        int32_t before = w.row;
        for (int i = 0; i < 4000 && !w.dead; i++) {
            if (i % 6 == 0) {
                cl_tap(&w);
            }
            cl_step(&w, DT);
            calm_lanes(&w);
            if (w.row > before) { /* the bird left the lane `before`: its coin, if any, was picked up */
                CHECK(cl_lane_at(&w, before)->coin_col == -1);
                checked++;
            }
            before = w.row;
        }
        CHECK(!w.dead && w.row > 100); /* taps alone never trap the bird */
        collected += w.coins;
    }
    CHECK(checked > 3000);
    CHECK(collected > 300); /* coins exist in good number: about 14 % of the lanes */
}

/* Past a log lane the bird's column depends on the player, so no coin is put there. */
static void test_coins_stop_at_a_log_lane(void)
{
    int before_log = 0;
    for (uint32_t seed = 1; seed <= 100; seed++) {
        cl_world_t w;
        cl_reset(&w, seed);
        flat(&w);
        make_river(&w, 4);
        add_log(cl_lane_at(&w, 4), 4.0f, 3.0f, 1.0f, 1);
        w.coin_row = 0;
        cl_place_coins(&w);
        for (int32_t r = 1; r <= 3; r++) {
            before_log += cl_lane_at(&w, r)->coin_col >= 0;
        }
        for (int32_t r = 4; r <= 12; r++) {
            CHECK(cl_lane_at(&w, r)->coin_col == -1);
        }
    }
    CHECK(before_log > 20);
}

static void test_waiting_never_kills(void)
{
    cl_world_t w;
    fresh(&w);
    run(&w, 120.0f); /* nothing takes a hero that waits on safe grass */
    CHECK(!w.dead && w.row == 0);
    cl_hop(&w, 1, 0);
    run(&w, 120.0f);
    CHECK(!w.dead && w.row == 1);
}

static void test_camera_follows_only_the_hero(void)
{
    cl_world_t w;
    fresh(&w);
    for (int i = 0; i < 4; i++) {
        cl_hop(&w, 1, 0);
        run(&w, 0.3f);
    }
    CHECK(w.row == 4 && w.cam == (float)(4 - CL_HERO_LEAD));
    run(&w, 10.0f); /* the screen does not creep on its own */
    CHECK(w.cam == (float)(4 - CL_HERO_LEAD));
}

/* ---- the one-button tap ---- */

static void block(cl_world_t *w, int32_t row, int col) { cl_lane_at(w, row)->prop[col + CL_EDGE] = CL_P_PINE; }

static void test_tap_forward(void)
{
    cl_world_t w;
    fresh(&w);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 5.5f && w.face == 0 && w.hop_t > 0.0f);
}

static void test_tap_dead_does_nothing(void)
{
    cl_world_t w;
    fresh(&w);
    w.dead = true;
    cl_tap(&w);
    CHECK(w.row == 0 && w.hop_t == 0.0f && !w.q_has);
}

static void test_tap_diagonal_alternates_in_middle(void)
{
    cl_world_t w;
    fresh(&w);
    block(&w, 1, 5);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 6.5f && w.face == 1); /* first time: right */
    run(&w, 0.2f);
    CHECK(w.hop_t == 0.0f && !w.dead);
    w.x = 5.5f; /* back to the middle column; the next lane has a tree ahead too */
    block(&w, 2, 5);
    cl_tap(&w);
    CHECK(w.row == 2 && w.x == 4.5f && w.face == 3); /* second time: left */
}

static void test_tap_diagonal_toward_middle(void)
{
    cl_world_t w;
    fresh(&w);
    w.x = w.from_x = 8.5f;
    block(&w, 1, 8);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 7.5f && w.face == 3);
    fresh(&w);
    w.x = w.from_x = 2.5f;
    block(&w, 1, 2);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 3.5f && w.face == 1);
}

static void test_tap_only_one_diagonal_free(void)
{
    cl_world_t w;
    fresh(&w);
    block(&w, 1, 5);
    block(&w, 1, 6);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 4.5f && w.face == 3);
    fresh(&w);
    block(&w, 1, 5);
    block(&w, 1, 4);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 6.5f && w.face == 1);
}

static void test_tap_border(void)
{
    cl_world_t w;
    fresh(&w);
    w.x = w.from_x = 0.5f;
    block(&w, 1, 0);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 1.5f && w.face == 1);
    fresh(&w);
    w.x = w.from_x = CL_COLS - 0.5f;
    block(&w, 1, CL_COLS - 1);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == CL_COLS - 1.5f && w.face == 3);
}

static void test_tap_sideways_step(void)
{
    cl_world_t w;
    fresh(&w);
    for (int c = 4; c <= CL_COLS - 1; c++) { /* lane 1: columns 4 to 10 blocked; 0 to 3 free */
        block(&w, 1, c);
    }
    cl_tap(&w);
    CHECK(w.row == 0 && w.x == 4.5f && w.hop_t > 0.0f && w.face == 3);
    /* the mirror: columns 0 to 6 blocked; the nearest usable column is 7 */
    fresh(&w);
    for (int c = 0; c <= 6; c++) {
        block(&w, 1, c);
    }
    cl_tap(&w);
    CHECK(w.row == 0 && w.x == 6.5f && w.hop_t > 0.0f && w.face == 1);
}

static void test_tap_sideways_step_blocked(void)
{
    cl_world_t w;
    fresh(&w);
    for (int c = 4; c <= CL_COLS - 1; c++) {
        block(&w, 1, c);
    }
    block(&w, 0, 4); /* the step itself is blocked */
    cl_tap(&w);
    CHECK(w.row == 0 && w.x == 5.5f && w.hop_t == 0.0f);
}

static void test_tap_fully_blocked(void)
{
    cl_world_t w;
    fresh(&w);
    for (int c = 0; c < CL_COLS; c++) {
        block(&w, 1, c);
    }
    cl_tap(&w);
    CHECK(w.row == 0 && w.x == 5.5f && w.hop_t == 0.0f);
}

/* Forward and both diagonals are blocked, and the nearest column with a way on is behind a prop of this lane:
 * the tap goes the other way, to the nearest column that it can reach. */
static void test_tap_sideways_around_a_prop_in_the_lane(void)
{
    cl_world_t w;
    fresh(&w);
    for (int c = 3; c <= 6; c++) {
        block(&w, 1, c); /* free columns ahead: 0 to 2 and 7 to 10 */
    }
    block(&w, 0, 6); /* the way right is shut in this lane */
    cl_tap(&w);
    CHECK(w.row == 0 && w.x == 4.5f && w.face == 3 && w.hop_t > 0.0f);
    for (int i = 0; i < 6 && w.row == 0; i++) { /* more taps: along the lane, then diagonally forward */
        run(&w, 0.3f);
        cl_tap(&w);
    }
    run(&w, 0.3f);
    CHECK(w.row == 1 && !w.dead);
}

static void test_tap_decided_when_queued_hop_starts(void)
{
    cl_world_t w;
    fresh(&w);
    block(&w, 2, 5); /* the hero stands in column 5 after the first hop */
    cl_tap(&w);
    cl_tap(&w); /* waits */
    CHECK(w.row == 1 && w.q_has);
    cl_step(&w, 0.14f);
    CHECK(w.row == 2 && w.x == 6.5f && w.face == 1 && w.hop_t > 0.0f && !w.q_has);
}

static void test_tap_replaces_queued_hop(void)
{
    cl_world_t w;
    fresh(&w);
    cl_hop(&w, 1, 0);
    cl_tap(&w);
    CHECK(w.q_has && w.q_tap);
    cl_hop(&w, 1, 0);
    CHECK(w.q_has && !w.q_tap);
}

static void test_tap_diagonal_from_drifting_log(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 0);
    add_log(cl_lane_at(&w, 0), 2.0f, 4.0f, 0.0f, 1); /* covers x = 2 to 6 */
    w.x = w.from_x = 5.8f;
    block(&w, 1, 5);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 6.5f && w.face == 1 && w.hop_t > 0.0f);
}

static void test_tap_sideways_needs_free_cell_ahead(void)
{
    cl_world_t w;
    fresh(&w);
    for (int c = 0; c <= 7; c++) { /* free in lane 0 but blocked ahead: columns 0 to 7 */
        block(&w, 1, c);
    }
    cl_tap(&w); /* column 3 is nearest but blocked ahead; the nearest usable one is 8 */
    CHECK(w.row == 0 && w.x == 6.5f && w.face == 1 && w.hop_t > 0.0f);
}

static void test_tap_side_only_toggles_on_tie(void)
{
    cl_world_t w;
    fresh(&w);
    block(&w, 1, 5);
    block(&w, 1, 6); /* one diagonal free */
    cl_tap(&w);
    CHECK(w.tap_side == 0);
    fresh(&w);
    w.x = w.from_x = 8.5f; /* not the middle column */
    block(&w, 1, 8);
    cl_tap(&w);
    CHECK(w.tap_side == 0 && w.x == 7.5f);
    fresh(&w);
    block(&w, 1, 5); /* the real tie-break */
    cl_tap(&w);
    CHECK(w.tap_side == 1);
}

static void test_tap_river_pad_ahead(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    cl_lane_at(&w, 1)->pad_mask = 1u << 5;
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 5.5f && w.face == 0 && w.hop_t > 0.0f);
}

static void test_tap_river_diagonal_to_pad(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    cl_lane_at(&w, 1)->pad_mask = 1u << 6; /* no pad straight ahead */
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 6.5f && w.face == 1 && w.hop_t > 0.0f);
    run(&w, 1.0f);
    CHECK(!w.dead);
}

static void test_tap_no_hop_into_water(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    cl_lane_at(&w, 1)->pad_mask = 1u << 9; /* too far away: the tap steps along the lane toward it */
    cl_tap(&w);
    CHECK(w.row == 0 && w.x == 6.5f && w.face == 1);
    run(&w, 0.5f);
    CHECK(!w.dead && w.row == 0);
}

static void test_tap_log_ahead(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    add_log(cl_lane_at(&w, 1), 4.0f, 3.0f, 1.0f, 1); /* covers x = 4 to 7 */
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 5.5f && w.hop_t > 0.0f);
    run(&w, 0.5f);
    CHECK(!w.dead);
}

static void test_tap_log_mistimed_drowns(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    add_log(cl_lane_at(&w, 1), 8.0f, 2.0f, 1.0f, 1); /* a log lane is a timing lane: the log is not ahead */
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 5.5f && w.hop_t > 0.0f);
    run(&w, 0.5f);
    CHECK(w.dead && w.death == CL_DEATH_WATER);
}

static void test_tap_river_after_river(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    make_river(&w, 2);
    cl_lane_at(&w, 1)->pad_mask = 1u << 5;
    cl_lane_at(&w, 2)->pad_mask = 1u << 2; /* far from the pad the hero stands on */
    cl_tap(&w);
    run(&w, 0.5f);
    CHECK(w.row == 1 && !w.dead);
    cl_tap(&w); /* the pad ahead is out of reach: wait, do not drown */
    run(&w, 0.5f);
    CHECK(w.row == 1 && w.x == 5.5f && !w.dead);
}

/* A tap-only bot taps at a steady rate. Wherever a tap would be fatal, a tap must do nothing instead. */
static void test_tap_bot_never_drowns(void)
{
    for (uint32_t seed = 1; seed <= 300; seed++) {
        cl_world_t w;
        cl_reset(&w, seed);
        for (int i = 0; i < 20000 && !w.dead; i++) {
            if (i % 6 == 0) {
                cl_tap(&w);
            }
            cl_step(&w, DT);
        }
        /* a log lane is a timing lane; only a pad lane must never drown the bot */
        CHECK(!w.dead || w.death != CL_DEATH_WATER || cl_lane_atc(&w, w.row)->nobj > 0);
    }
}

static void test_tap_queued_without_move_is_dropped(void)
{
    cl_world_t w;
    fresh(&w);
    for (int c = 0; c < CL_COLS; c++) {
        block(&w, 2, c);
    }
    cl_tap(&w);
    cl_tap(&w);
    cl_step(&w, 0.14f);
    CHECK(w.row == 1 && !w.q_has && !w.q_tap && w.hop_t == 0.0f);
}

static void test_tap_death_clears_queue(void)
{
    cl_world_t w;
    fresh(&w);
    make_rail(&w, 1, 4.8f, 1); /* the train is over column 5 when the hero lands */
    cl_tap(&w);
    cl_tap(&w);
    CHECK(w.q_has && w.q_tap);
    cl_step(&w, 0.14f);
    CHECK(w.dead && !w.q_has && !w.q_tap);
}

static void test_tap_border_both_diagonals_blocked(void)
{
    cl_world_t w;
    fresh(&w);
    w.x = w.from_x = 0.5f;
    block(&w, 1, 0);
    block(&w, 1, 1);
    cl_tap(&w);
    CHECK(w.row == 0 && w.x == 1.5f && w.face == 1 && w.hop_t > 0.0f);
}

static void test_tap_no_sideways_step_into_water_from_pad(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    cl_lane_at(&w, 1)->pad_mask = 1u << 5;
    for (int c = 4; c <= 6; c++) {
        block(&w, 2, c);
    }
    cl_hop(&w, 1, 0);
    run(&w, 1.0f);
    CHECK(!w.dead && w.row == 1 && w.x == 5.5f);
    cl_tap(&w); /* ahead and both diagonals are blocked; the only sideways cells are water */
    CHECK(w.row == 1 && w.x == 5.5f && w.hop_t == 0.0f);
    run(&w, 0.5f);
    CHECK(!w.dead && w.row == 1 && w.x == 5.5f);
}

static void test_tap_no_sideways_step_into_water_from_log(void)
{
    cl_world_t w;
    fresh(&w);
    make_river(&w, 1);
    add_log(cl_lane_at(&w, 1), 4.6f, 1.8f, 0.0f, 1); /* covers 4.6 to 6.4: neither neighbour cell centre */
    for (int c = 4; c <= 6; c++) {
        block(&w, 2, c);
    }
    cl_hop(&w, 1, 0);
    run(&w, 1.0f);
    CHECK(!w.dead && w.row == 1 && w.x == 5.5f);
    cl_tap(&w);
    CHECK(w.row == 1 && w.x == 5.5f && w.hop_t == 0.0f);
    run(&w, 0.5f);
    CHECK(!w.dead && w.row == 1 && w.x == 5.5f);
}

int main(void)
{
    test_reset();
    test_same_seed_same_world();
    test_generator_invariants();
    test_hop_forward();
    test_hop_sideways_and_bounds();
    test_hop_back_limits();
    test_prop_blocks();
    test_hop_queue();
    test_coin();
    test_coins_are_on_the_tap_path();
    test_coins_stop_at_a_log_lane();
    test_camera_follows();
    test_car_kills_on_landing();
    test_car_misses();
    test_moving_car_hits_standing_hero();
    test_objects_wrap();
    test_rail_signal_and_train();
    test_train_kills_hero_on_rail();
    test_train_spares_hero_off_rail();
    test_water_drowns();
    test_pad_is_safe();
    test_log_carries();
    test_log_holds_hero_at_edge_until_it_passes();
    test_hop_log_to_log();
    test_log_speeds_vary();
    test_waiting_never_kills();
    test_camera_follows_only_the_hero();
    test_tap_forward();
    test_tap_dead_does_nothing();
    test_tap_diagonal_alternates_in_middle();
    test_tap_diagonal_toward_middle();
    test_tap_only_one_diagonal_free();
    test_tap_border();
    test_tap_sideways_step();
    test_tap_sideways_step_blocked();
    test_tap_fully_blocked();
    test_tap_sideways_around_a_prop_in_the_lane();
    test_tap_decided_when_queued_hop_starts();
    test_tap_replaces_queued_hop();
    test_tap_diagonal_from_drifting_log();
    test_tap_sideways_needs_free_cell_ahead();
    test_tap_side_only_toggles_on_tie();
    test_tap_river_pad_ahead();
    test_tap_river_diagonal_to_pad();
    test_tap_no_hop_into_water();
    test_tap_log_ahead();
    test_tap_log_mistimed_drowns();
    test_tap_river_after_river();
    test_tap_bot_never_drowns();
    test_tap_queued_without_move_is_dropped();
    test_tap_death_clears_queue();
    test_tap_border_both_diagonals_blocked();
    test_tap_no_sideways_step_into_water_from_pad();
    test_tap_no_sideways_step_into_water_from_log();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("all crossy logic tests passed\n");
    return 0;
}
