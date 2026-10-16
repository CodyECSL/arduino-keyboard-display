#include "game_logic.h"

#include <math.h>
#include <string.h>

#define GRAVITY 900.0f      /* px/s^2, down */
#define THRUST_ACCEL 1500.0f /* px/s^2, up while the finger is down */
#define VY_MAX_DOWN 420.0f
#define VY_MAX_UP 330.0f

#define SPEED_START 220.0f
#define SPEED_MAX 420.0f
#define SPEED_PER_M 0.8f

#define SPAWN_X (GL_FIELD_W + 30)
#define MISSILE_EXTRA 240.0f /* a missile is faster than the scenery */
#define WARN_TIME 1.0f
#define SCIENTIST_RUN 60.0f  /* scientists run in the flight direction at this speed */
#define BEAM_R 6.0f          /* how close to a beam still counts as touching */

#define WAVE_COINS 3
#define COIN_GAP 26.0f /* between neighbours in a group */
#define INVULN_TIME 1.0f /* after a shield takes a hit */

static uint32_t rnd(gl_world_t *w)
{
    uint32_t x = w->rng;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    w->rng = x;
    return x;
}

static float rnd_range(gl_world_t *w, float lo, float hi)
{
    return lo + (hi - lo) * ((float)(rnd(w) >> 8) / 16777216.0f);
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void gl_reset(gl_world_t *w, uint32_t seed)
{
    memset(w, 0, sizeof(*w));
    w->rng = seed ? seed : 1;
    w->y = (GL_CEIL_Y + GL_FLOOR_Y - GL_PLAYER_H) / 2.0f;
    w->speed = SPEED_START;
    w->spawn_left = 400.0f; /* the first obstacle comes after about 2 s */
    w->coin_left = 250.0f;
    w->sci_left = 100.0f;
}

uint32_t gl_meters(const gl_world_t *w) { return (uint32_t)(w->dist_px / GL_PX_PER_M); }

/* ---- collisions ---- */

static bool seg_near_box(float x1, float y1, float x2, float y2, float bx0, float by0, float bx1, float by1)
{
    float len = sqrtf((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
    int n = (int)(len / 5.0f) + 1;
    for (int i = 0; i <= n; i++) {
        float t = (float)i / (float)n;
        float px = x1 + (x2 - x1) * t, py = y1 + (y2 - y1) * t;
        if (px >= bx0 - BEAM_R && px <= bx1 + BEAM_R && py >= by0 - BEAM_R && py <= by1 + BEAM_R) {
            return true;
        }
    }
    return false;
}

/* Does a hazard touch the box `t` seconds from now? `margin` widens the box. The player is not
 * moved: t = 0 is the real situation, larger t is used by the autopilot to look ahead. */
static bool hazards_hit_box(const gl_world_t *w, float t, float margin, float bx0, float by0, float bx1,
                            float by1)
{
    bx0 -= margin;
    by0 -= margin;
    bx1 += margin;
    by1 += margin;
    float shift = w->speed * t;
    for (int i = 0; i < GL_MAX_ZAPPERS; i++) {
        const gl_zapper_t *z = &w->zappers[i];
        if (z->active && seg_near_box(z->x1 - shift, z->y1, z->x2 - shift, z->y2, bx0, by0, bx1, by1)) {
            return true;
        }
    }
    for (int i = 0; i < GL_MAX_MISSILES; i++) {
        const gl_missile_t *m = &w->missiles[i];
        if (!m->active) {
            continue;
        }
        float x;
        if (m->warn > 0.0f) {
            if (t < m->warn) {
                continue;
            }
            x = m->x - (w->speed + MISSILE_EXTRA) * (t - m->warn);
        } else {
            x = m->x - (w->speed + MISSILE_EXTRA) * t;
        }
        if (x < bx1 && x + GL_MISSILE_W > bx0 && m->y - GL_MISSILE_H / 2.0f < by1 && m->y + GL_MISSILE_H / 2.0f > by0) {
            return true;
        }
    }
    return false;
}

bool gl_hits_hazard(const gl_world_t *w, float y)
{
    return hazards_hit_box(w, 0.0f, 0.0f, GL_PLAYER_X, y, GL_PLAYER_X + GL_PLAYER_W, y + GL_PLAYER_H);
}

static bool coin_touches(const gl_coin_t *c, float y)
{
    float cx = clampf(c->x, GL_PLAYER_X, GL_PLAYER_X + GL_PLAYER_W);
    float cy = clampf(c->y, y, y + GL_PLAYER_H);
    float dx = c->x - cx, dy = c->y - cy;
    return dx * dx + dy * dy <= (float)(GL_COIN_R * GL_COIN_R);
}

/* ---- spawning ---- */

static int free_slot_zapper(const gl_world_t *w)
{
    for (int i = 0; i < GL_MAX_ZAPPERS; i++) {
        if (!w->zappers[i].active) {
            return i;
        }
    }
    return -1;
}

static void spawn_obstacle(gl_world_t *w)
{
    int kind = (int)(rnd(w) % 4);
    float top = GL_CEIL_Y, bottom = GL_FLOOR_Y;
    if (kind == 3) {
        for (int i = 0; i < GL_MAX_MISSILES; i++) {
            if (!w->missiles[i].active) {
                w->missiles[i] =
                    (gl_missile_t){true, WARN_TIME, SPAWN_X, rnd_range(w, top + 25.0f, bottom - 25.0f)};
                return;
            }
        }
        kind = 0; /* both missile slots busy */
    }
    int slot = free_slot_zapper(w);
    if (slot < 0) {
        return;
    }
    gl_zapper_t *z = &w->zappers[slot];
    z->active = true;
    if (kind == 0) { /* vertical beam; free space above or below stays at least 74 px */
        float len = rnd_range(w, 70.0f, 110.0f);
        float y1 = rnd_range(w, top, bottom - len);
        *z = (gl_zapper_t){true, SPAWN_X, y1, SPAWN_X, y1 + len};
    } else if (kind == 1) { /* horizontal beam */
        float len = rnd_range(w, 80.0f, 130.0f);
        float y = rnd_range(w, top + 70.0f, bottom - 70.0f);
        *z = (gl_zapper_t){true, SPAWN_X, y, SPAWN_X + len, y};
    } else { /* diagonal beam */
        float dx = rnd_range(w, 60.0f, 90.0f);
        float dy = rnd_range(w, 60.0f, 90.0f);
        float y1 = rnd_range(w, top, bottom - dy);
        if (rnd(w) & 1) {
            *z = (gl_zapper_t){true, SPAWN_X, y1, SPAWN_X + dx, y1 + dy};
        } else {
            *z = (gl_zapper_t){true, SPAWN_X, y1 + dy, SPAWN_X + dx, y1};
        }
    }
}

static void spawn_coins(gl_world_t *w)
{
    int free_idx[GL_MAX_COINS], n = 0;
    for (int i = 0; i < GL_MAX_COINS; i++) {
        if (!w->coins[i].active) {
            free_idx[n++] = i;
        }
    }
    if (n < WAVE_COINS) {
        return;
    }
    /* A straight line of coins at a random angle, from nearly flat to steep, rising or falling. */
    float yc = rnd_range(w, GL_CEIL_Y + 50.0f, GL_FLOOR_Y - 50.0f);
    float angle = rnd_range(w, -1.2f, 1.2f); /* radians; about +-70 degrees */
    for (int i = 0; i < WAVE_COINS; i++) {
        float d = (float)(i - (WAVE_COINS - 1) / 2) * COIN_GAP;
        w->coins[free_idx[i]] = (gl_coin_t){true, SPAWN_X + 30.0f + d * cosf(angle), yc + d * sinf(angle)};
    }
}

static void spawn_scientist(gl_world_t *w)
{
    for (int i = 0; i < GL_MAX_SCIENTISTS; i++) {
        if (!w->scientists[i].active) {
            w->scientists[i] = (gl_scientist_t){true, SPAWN_X};
            return;
        }
    }
}

/* ---- the step ---- */

void gl_step(gl_world_t *w, float dt, bool thrust)
{
    if (w->dead) {
        return;
    }
    w->thrusting = thrust;
    w->speed = fminf(SPEED_MAX, SPEED_START + SPEED_PER_M * (w->dist_px / GL_PX_PER_M));

    w->vy += (thrust ? -THRUST_ACCEL : GRAVITY) * dt;
    w->vy = clampf(w->vy, -VY_MAX_UP, VY_MAX_DOWN);
    w->y += w->vy * dt;
    if (w->y < GL_CEIL_Y) {
        w->y = GL_CEIL_Y;
        w->vy = 0.0f;
    }
    if (w->y + GL_PLAYER_H > GL_FLOOR_Y) {
        w->y = GL_FLOOR_Y - GL_PLAYER_H;
        w->vy = 0.0f;
    }

    float dx = w->speed * dt;
    w->dist_px += dx;

    for (int i = 0; i < GL_MAX_ZAPPERS; i++) {
        gl_zapper_t *z = &w->zappers[i];
        if (z->active) {
            z->x1 -= dx;
            z->x2 -= dx;
            if (fmaxf(z->x1, z->x2) < -20.0f) {
                z->active = false;
            }
        }
    }
    for (int i = 0; i < GL_MAX_MISSILES; i++) {
        gl_missile_t *m = &w->missiles[i];
        if (!m->active) {
            continue;
        }
        if (m->warn > 0.0f) {
            m->warn -= dt;
            if (m->warn < 0.0f) {
                m->warn = 0.0f;
            }
        } else {
            m->x -= dx + MISSILE_EXTRA * dt;
            if (m->x + GL_MISSILE_W < -10.0f) {
                m->active = false;
            }
        }
    }
    for (int i = 0; i < GL_MAX_COINS; i++) {
        gl_coin_t *c = &w->coins[i];
        if (c->active) {
            c->x -= dx;
            if (c->x < -20.0f) {
                c->active = false;
            } else if (coin_touches(c, w->y)) {
                c->active = false;
                if (++w->coins_held >= GL_COINS_PER_SHIELD) {
                    w->coins_held = 0; /* past the cap the coins are lost */
                    if (w->shields < GL_MAX_SHIELDS) {
                        w->shields++;
                    }
                }
            }
        }
    }
    for (int i = 0; i < GL_MAX_SCIENTISTS; i++) {
        gl_scientist_t *s = &w->scientists[i];
        if (s->active) {
            s->x -= dx - SCIENTIST_RUN * dt;
            if (s->x < -20.0f) {
                s->active = false;
            }
        }
    }

    w->spawn_left -= dx;
    if (w->spawn_left <= 0.0f) {
        spawn_obstacle(w);
        w->spawn_left = w->speed * rnd_range(w, 0.9f, 1.4f); /* the pace stays the same as it speeds up */
    }
    w->coin_left -= dx;
    if (w->coin_left <= 0.0f) {
        spawn_coins(w);
        w->coin_left = rnd_range(w, 500.0f, 900.0f);
    }
    w->sci_left -= dx;
    if (w->sci_left <= 0.0f) {
        spawn_scientist(w);
        w->sci_left = rnd_range(w, 300.0f, 700.0f);
    }

    w->invuln = fmaxf(0.0f, w->invuln - dt);
    if (w->invuln <= 0.0f && gl_hits_hazard(w, w->y)) {
        if (w->shields > 0) {
            w->shields--;
            w->invuln = INVULN_TIME;
        } else {
            w->dead = true;
            w->thrusting = false;
        }
    }
}

/* ---- autopilot ---- */

/* Measured on 200 seeds: 0.8 s gave 96% of demo runs lasting 30 s; 1.1 s gave 90%; 1.5 s gave 81%. A cost
 * for changing lane did not help. */
#define PLAN_HORIZON_S 0.8f
#define PLAN_STEP_S 0.04f
#define LANE_STEP 8.0f

/* The thrust that steers toward a lane (the top of the hitbox). It looks a moment ahead, so the
 * player brakes before the lane instead of swinging through it. */
static bool steer(float y, float vy, float lane) { return y + vy * 0.1f > lane; }

bool gl_autopilot_thrust(const gl_world_t *w, float lane) { return steer(w->y, w->vy, lane); }

/* Try every lane: fly toward it in a copy of the flight and count the moments it touches a hazard.
 * The cheapest lane wins; among safe lanes the one near the next coin, or else near the middle. */
float gl_autopilot_lane(const gl_world_t *w)
{
    float best_lane = w->y, best_cost = 1.0e30f;
    float coin_y = -1.0f, coin_x = 1.0e9f;
    for (int i = 0; i < GL_MAX_COINS; i++) {
        const gl_coin_t *c = &w->coins[i];
        if (c->active && c->x > GL_PLAYER_X && c->x < coin_x) {
            coin_x = c->x;
            coin_y = c->y;
        }
    }
    float mid = (GL_CEIL_Y + GL_FLOOR_Y - GL_PLAYER_H) / 2.0f;
    for (float lane = GL_CEIL_Y; lane <= GL_FLOOR_Y - GL_PLAYER_H; lane += LANE_STEP) {
        float y = w->y, vy = w->vy;
        int hits = 0;
        for (float t = PLAN_STEP_S; t <= PLAN_HORIZON_S; t += PLAN_STEP_S) {
            vy += (steer(y, vy, lane) ? -THRUST_ACCEL : GRAVITY) * PLAN_STEP_S;
            vy = clampf(vy, -VY_MAX_UP, VY_MAX_DOWN);
            y = clampf(y + vy * PLAN_STEP_S, GL_CEIL_Y, GL_FLOOR_Y - GL_PLAYER_H);
            if (hazards_hit_box(w, t, 4.0f, GL_PLAYER_X, y, GL_PLAYER_X + GL_PLAYER_W, y + GL_PLAYER_H)) {
                hits++;
            }
        }
        float cost = (float)hits * 1000.0f + 0.1f * fabsf(lane - w->y);
        cost += 0.5f * fabsf(coin_y >= 0.0f ? lane + GL_PLAYER_H / 2.0f - coin_y : lane - mid);
        if (cost < best_cost) {
            best_cost = cost;
            best_lane = lane;
        }
    }
    return best_lane;
}

bool gl_autopilot(const gl_world_t *w) { return steer(w->y, w->vy, gl_autopilot_lane(w)); }
