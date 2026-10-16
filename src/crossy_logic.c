#include "crossy_logic.h"

#include <math.h>
#include <stdlib.h>
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

/* The cells of a lane that a hero can stand on (bit c = column c): free grass, any road or rail, a pad, or a log lane
 * (a hero on a log drifts to any column). */
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

/* No pocket for the one-button hero: from each run of cells it can walk along in the lane before, a tap must find a
 * free cell here, straight ahead or one column to the side. Otherwise it is stuck for good. */
static void open_pockets(cl_lane_t *l, const cl_lane_t *before)
{
    unsigned st = stand_bits(before);
    for (int a = 0; a < CL_COLS; a++) {
        if (!((st >> a) & 1) || (a > 0 && ((st >> (a - 1)) & 1))) {
            continue; /* not the start of a run */
        }
        int b = a;
        while (b + 1 < CL_COLS && ((st >> (b + 1)) & 1)) {
            b++;
        }
        int lo = a > 0 ? a - 1 : 0, hi = b < CL_COLS - 1 ? b + 1 : CL_COLS - 1;
        bool ok = false;
        for (int c = lo; c <= hi; c++) {
            ok |= prop_free(l, c);
        }
        if (!ok) {
            l->prop[(a > 0 ? a - 1 : b + 1) + CL_EDGE] = CL_P_NONE;
        }
    }
}

static void build_grass(cl_world_t *w, cl_lane_t *l, const cl_lane_t *before, int prev, int next)
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
    if (before) {
        open_pockets(l, before);
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
        /* A tap hops straight on or one column to the side: keep a pad within one column of every cell. */
        for (int c = 0; c < CL_COLS; c++) {
            uint16_t near = (uint16_t)(7u << (c > 0 ? c - 1 : 0));
            near = c == 0 ? 3u : near;
            if (!(l->pad_mask & near)) {
                l->pad_mask |= (uint16_t)(1u << (c < CL_COLS - 1 ? c + 1 : c));
            }
        }
        return;
    }
    l->dir = rint_(w, 0, 1) ? 1 : -1;
    const cl_lane_t *pl = cl_lane_atc(w, row - 1);
    bool follows_logs = row > 0 && pl->kind == CL_RIVER && pl->nobj > 0;
    if (follows_logs) {
        l->dir = (int8_t)-pl->dir; /* log lanes in a row run against each other, so the next log comes soon */
    }
    /* Slow to fast, and the fastest allowed grows with the distance. A lane next to a log lane takes a speed far
     * from it, so neighbours do not look alike. */
    const float lo = 0.5f, hi = 1.5f + 1.5f * (float)(row > 100 ? 100 : row) / 100.0f;
    float gap = 0.3f * (hi - lo);
    float s = rfl(w, lo, hi);
    if (follows_logs && fabsf(s - pl->speed) < gap) {
        float below = pl->speed - gap - lo, above = hi - pl->speed - gap; /* room on each side, 0 or less when none */
        below = below > 0.0f ? below : 0.0f;
        above = above > 0.0f ? above : 0.0f;
        float pick = rfl(w, 0.0f, below + above);
        s = pick < below ? lo + pick : pl->speed + gap + (pick - below);
    }
    l->speed = s;
    l->nobj = (uint8_t)rint_(w, 3, CL_MAX_OBJ);
    if (follows_logs) {
        l->nobj = CL_MAX_OBJ;
    }
    float slot = CL_LOOP / (float)l->nobj;
    for (int i = 0; i < l->nobj; i++) {
        float len = (float)rint_(w, follows_logs || l->speed >= 2.0f ? 3 : 2, 4); /* fast lanes: logs long enough to land on */
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
    case CL_GRASS: build_grass(w, l, row > 0 ? cl_lane_atc(w, row - 1) : NULL, prev, next); break;
    case CL_ROAD: build_road(w, l, diff); break;
    case CL_RIVER: build_river(w, l, prev, next, row); break;
    default:
        l->dir = rint_(w, 0, 1) ? 1 : -1;
        l->phase = rfl(w, 0.0f, CL_RAIL_PERIOD);
        break;
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
    w->seed = w->rng;
    w->run_kind = CL_GRASS;
    w->corr = CL_START_COL;
    w->x = CL_START_COL + 0.5f;
    w->from_x = w->x;
    cl_generate_to(w, CL_AHEAD);
    cl_place_coins(w);
}

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
    w->q_tap = false;
}

/* The cell under the hero, snapped to a column centre. */
static float snap(float x)
{
    float c = roundf(x - 0.5f) + 0.5f;
    return c < 0.5f ? 0.5f : (c > CL_COLS - 0.5f ? CL_COLS - 0.5f : c);
}

static void start_hop(cl_world_t *w, int dr, int dc)
{
    bool diag = dr != 0 && dc != 0;
    w->face = (int8_t)(diag ? (dc > 0 ? 1 : 3) : (dr > 0 ? 0 : (dc > 0 ? 1 : (dr < 0 ? 2 : 3))));
    int32_t nr = w->row + dr;
    if (nr < 0 || nr < w->best_row - CL_BACK_LIMIT) {
        return;
    }
    cl_generate_to(w, nr);
    const cl_lane_t *tl = cl_lane_atc(w, nr);
    float nx = dr != 0 && dc == 0 ? snap(w->x) : (diag ? snap(w->x) + (float)dc : w->x + (float)dc);
    if (tl->kind == CL_RIVER) {
        if (nx < 0.2f || nx > CL_COLS - 0.2f) {
            return;
        }
    } else {
        nx = snap(nx);
        int c = cl_col(nx);
        float want = (diag ? snap(w->x) : w->x) + (float)dc; /* a diagonal hop starts from the cell centre */
        if (c < 0 || c >= CL_COLS || (dc != 0 && fabsf(nx - want) > 0.01f) ||
            tl->prop[c + CL_EDGE] != CL_P_NONE) {
            return;
        }
    }
    w->from_row = w->row;
    w->from_x = w->x;
    w->row = nr;
    w->x = nx;
    w->hop_t = CL_HOP_S;
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
        w->q_tap = false;
        return;
    }
    start_hop(w, dr, dc);
}

/* A cell of the lane that a hero can stand on, as far as props go (props exist only on grass lanes). */
static bool cell_free(const cl_lane_t *l, int col)
{
    return col >= 0 && col < CL_COLS && (l->kind == CL_RIVER || l->prop[col + CL_EDGE] == CL_P_NONE);
}

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

/* A sideways step from or to a cell of the current lane: on grass and road the props decide; on a river the
 * place at x must have a pad or a log. */
static bool stand_ok(const cl_lane_t *l, int col, float x)
{
    if (l->kind != CL_RIVER) {
        return cell_free(l, col);
    }
    const cl_obj_t *log;
    return col >= 0 && col < CL_COLS && support(l, x, &log);
}

/* A tap may land on this cell of the lane ahead: free of props, or on a pad lane a pad. A lane with logs is a
 * timing lane like road and rail: the tap hops, and a bad timing is the player's. */
static bool landing_ok(const cl_lane_t *l, int col)
{
    if (l->kind != CL_RIVER || l->nobj > 0) {
        return cell_free(l, col);
    }
    return col >= 0 && col < CL_COLS && ((l->pad_mask >> col) & 1);
}

/* Choose the hop of a tap from where the hero stands now. Returns false when there is none. */
static bool plan_tap(const cl_lane_t *cur, const cl_lane_t *nl, int c, float x, uint8_t *side, int *dr, int *dc)
{
    *dr = 1;
    *dc = 0;
    if (landing_ok(nl, c)) {
        return true;
    }
    bool left = landing_ok(nl, c - 1);
    bool right = landing_ok(nl, c + 1);
    if (left || right) {
        if (left && right) {
            if (c == CL_START_COL) {
                right = *side == 0;
                left = !right;
                *side ^= 1u;
            } else {
                right = c < CL_START_COL;
                left = !right;
            }
        }
        *dc = right ? 1 : -1;
        return true;
    }
    /* Forward and both diagonals are blocked: step sideways toward the nearest column with a way on (forward or
     * diagonal). Only a column that the hero can walk to counts: a prop (or water) in between shuts the way, so look
     * to both sides. */
    int best = -1;
    for (int side_dir = -1; side_dir <= 1; side_dir += 2) {
        for (int c2 = c + side_dir; c2 >= 0 && c2 < CL_COLS; c2 += side_dir) {
            if (!stand_ok(cur, c2, (float)c2 + 0.5f)) {
                break;
            }
            if (!landing_ok(nl, c2) && !landing_ok(nl, c2 - 1) && !landing_ok(nl, c2 + 1)) {
                continue; /* a hop is possible from here when forward or a diagonal is free */
            }
            int d = abs(c2 - c);
            int bd = best < 0 ? 0 : abs(best - c);
            int m = abs(c2 - CL_START_COL);
            int bm = best < 0 ? 0 : abs(best - CL_START_COL);
            if (best < 0 || d < bd || (d == bd && m < bm)) {
                best = c2;
            }
            break;
        }
    }
    if (best < 0) {
        return false;
    }
    int step = best > c ? 1 : -1;
    if (!stand_ok(cur, c + step, x + (float)step)) {
        return false;
    }
    *dr = 0;
    *dc = step;
    return true;
}

static bool decide_tap(cl_world_t *w, int *dr, int *dc)
{
    cl_generate_to(w, w->row + 1);
    return plan_tap(cl_lane_atc(w, w->row), cl_lane_atc(w, w->row + 1), cl_col(snap(w->x)), w->x, &w->tap_side, dr, dc);
}

#define CL_COIN_AHEAD 7 /* rows in front of the hero that get their coins */
#define CL_COIN_PCT 14

/* Not the generator's random numbers, so the lanes of a seed do not change. */
static bool coin_roll(const cl_world_t *w, int32_t row)
{
    uint32_t h = w->seed ^ ((uint32_t)row * 2654435761u);
    h ^= h >> 15;
    h *= 2246822519u;
    h ^= h >> 13;
    h *= 3266489917u;
    h ^= h >> 16;
    return h % 100 < CL_COIN_PCT;
}

static bool log_lane(const cl_lane_t *l) { return l->kind == CL_RIVER && l->nobj > 0; }

void cl_place_coins(cl_world_t *w)
{
    if (log_lane(cl_lane_atc(w, w->row))) { /* the hero drifts: its next column is not known */
        return;
    }
    cl_generate_to(w, w->row + CL_COIN_AHEAD + 1);
    int c = cl_col(snap(w->x));
    uint8_t side = w->tap_side;
    for (int32_t n = w->row + 1; n <= w->row + CL_COIN_AHEAD; n++) {
        const cl_lane_t *cur = cl_lane_atc(w, n - 1), *nl = cl_lane_atc(w, n);
        if (log_lane(nl)) {
            return;
        }
        for (int step = 0;; step++) { /* taps until the hero is in lane n */
            int dr, dc;
            if (step > CL_COLS || !plan_tap(cur, nl, c, (float)c + 0.5f, &side, &dr, &dc)) {
                return;
            }
            c += dc;
            if (dr != 0) {
                break;
            }
        }
        if (n > w->coin_row) {
            w->coin_row = n;
            if (nl->kind != CL_RIVER && coin_roll(w, n)) {
                cl_lane_at(w, n)->coin_col = (int8_t)c;
            }
        }
    }
}

void cl_tap(cl_world_t *w)
{
    if (w->dead) {
        return;
    }
    if (w->hop_t > 0.0f) {
        w->q_has = true;
        w->q_tap = true;
        return;
    }
    int dr, dc;
    if (decide_tap(w, &dr, &dc)) {
        start_hop(w, dr, dc);
    }
}

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

static void land(cl_world_t *w)
{
    cl_lane_t *l = cl_lane_at(w, w->row);
    int c = cl_col(w->x);
    if (l->kind == CL_RIVER) {
        const cl_obj_t *log;
        if (!support(l, w->x, &log)) {
            die(w, CL_DEATH_WATER);
            return;
        }
    }
    if (l->coin_col >= 0 && c == l->coin_col) {
        l->coin_col = -1;
        w->coins++;
    }
    if (w->row > w->best_row) {
        w->best_row = w->row;
    }
    cl_death_t h = hazard_hit(l, w->x);
    if (h != CL_DEATH_NONE) {
        die(w, h);
    } else {
        cl_place_coins(w);
    }
}

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


void cl_step(cl_world_t *w, float dt)
{
    if (w->dead) {
        w->death_t += dt;
        return;
    }
    cl_animate(w, dt);
    cl_generate_to(w, w->row + CL_AHEAD);

    if (w->hop_t > 0.0f) {
        w->hop_t -= dt;
        if (w->hop_t <= 0.0f) {
            w->hop_t = 0.0f;
            land(w);
            if (!w->dead && w->q_has) {
                w->q_has = false;
                int dr = w->q_dr, dc = w->q_dc;
                if (w->q_tap) {
                    w->q_tap = false;
                    if (!decide_tap(w, &dr, &dc)) {
                        dr = dc = 0;
                    }
                }
                if (dr != 0 || dc != 0) {
                    start_hop(w, dr, dc);
                }
            }
        }
    } else {
        const cl_lane_t *l = cl_lane_atc(w, w->row);
        if (l->kind == CL_RIVER) {
            const cl_obj_t *log;
            if (!support(l, w->x, &log)) {
                die(w, CL_DEATH_WATER);
            } else if (log) {
                w->x += (float)l->dir * l->speed * dt;
                if (w->x < 0.2f || w->x > CL_COLS - 0.2f) {
                    w->x = w->x < 0.2f ? 0.2f : CL_COLS - 0.2f; /* held at the edge while the log lasts */
                }
            }
        } else {
            cl_death_t h = hazard_hit(l, w->x);
            if (h != CL_DEATH_NONE) {
                die(w, h);
            }
        }
    }

    float lead = (float)w->row - CL_HERO_LEAD;
    if (w->cam < lead) {
        w->cam = lead;
    }
}
