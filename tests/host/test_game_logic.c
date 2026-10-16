/* Host test for src/game_logic.c. Run: tests/host/run.sh */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../src/game_logic.h"

static int s_fail;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);           \
            s_fail++;                                                        \
        }                                                                    \
    } while (0)

#define DT (1.0f / 30.0f)

static void run(gl_world_t *w, float seconds, bool thrust)
{
    for (float t = 0; t < seconds; t += DT) {
        gl_step(w, DT, thrust);
    }
}

static void clear_objects(gl_world_t *w)
{
    memset(w->zappers, 0, sizeof(w->zappers));
    memset(w->missiles, 0, sizeof(w->missiles));
    memset(w->coins, 0, sizeof(w->coins));
    memset(w->scientists, 0, sizeof(w->scientists));
}

static void test_reset(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    CHECK(!w.dead);
    CHECK(gl_meters(&w) == 0);
    CHECK(w.y >= GL_CEIL_Y && w.y + GL_PLAYER_H <= GL_FLOOR_Y);
    CHECK(w.speed > 100.0f);
}

static void test_gravity_and_thrust(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    run(&w, 1.0f, false);
    CHECK(fabsf(w.y + GL_PLAYER_H - GL_FLOOR_Y) < 0.5f); /* rests on the floor */
    CHECK(w.vy == 0.0f);
    CHECK(!w.dead); /* touching the floor does not kill */
    CHECK(!w.thrusting);

    run(&w, 1.0f, true);
    CHECK(fabsf(w.y - GL_CEIL_Y) < 0.5f); /* pressed against the ceiling */
    CHECK(w.thrusting);
    CHECK(!w.dead);
}

static void test_distance_and_speed(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    float s0 = w.speed;
    run(&w, 1.0f, false);
    uint32_t m1 = gl_meters(&w);
    CHECK(m1 >= 9 && m1 <= 13);
    run(&w, 1.0f, false);
    CHECK(gl_meters(&w) > m1);

    w.dist_px = 100000.0f * GL_PX_PER_M;
    gl_step(&w, DT, false);
    CHECK(w.speed > s0);
    CHECK(w.speed <= 420.0f);
}

static void test_deterministic(void)
{
    gl_world_t a, b;
    gl_reset(&a, 77);
    gl_reset(&b, 77);
    for (int i = 0; i < 600; i++) {
        bool t = (i / 7) % 2;
        gl_step(&a, DT, t);
        gl_step(&b, DT, t);
    }
    CHECK(memcmp(&a, &b, sizeof(a)) == 0);
}

static void test_zapper_hit(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    clear_objects(&w);
    /* a vertical beam through the player's column, from the ceiling to y = 150 */
    w.zappers[0] = (gl_zapper_t){true, GL_PLAYER_X + 8, GL_CEIL_Y, GL_PLAYER_X + 8, 150};
    CHECK(gl_hits_hazard(&w, 100));
    CHECK(!gl_hits_hazard(&w, 200));

    w.y = 100;
    w.vy = 0;
    gl_step(&w, DT, false);
    CHECK(w.dead);

    /* a dead world stays put */
    float d = w.dist_px, y = w.y;
    run(&w, 1.0f, true);
    CHECK(w.dist_px == d && w.y == y);
}

static void test_missile(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    clear_objects(&w);
    float y = 120;
    /* while the warning shows, the missile is harmless */
    w.missiles[0] = (gl_missile_t){true, 0.8f, 0, y + GL_PLAYER_H / 2};
    CHECK(!gl_hits_hazard(&w, y));
    /* flying, it overlaps the player's column */
    w.missiles[0] = (gl_missile_t){true, 0.0f, GL_PLAYER_X - 10, y + GL_PLAYER_H / 2};
    CHECK(gl_hits_hazard(&w, y));
    CHECK(!gl_hits_hazard(&w, y + 80));
}

static void test_objects_scroll_and_go(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    clear_objects(&w);
    w.spawn_left = w.coin_left = w.sci_left = 1.0e9f; /* no new objects */
    w.coins[0] = (gl_coin_t){true, 300, 100};
    w.scientists[0] = (gl_scientist_t){true, 300};
    w.zappers[0] = (gl_zapper_t){true, 400, 30, 400, 100};
    run(&w, 0.5f, false);
    CHECK(w.coins[0].x < 300 && w.scientists[0].x < 300 && w.zappers[0].x1 < 400);
    run(&w, 6.0f, false);
    CHECK(!w.coins[0].active && !w.scientists[0].active && !w.zappers[0].active);
}

static void test_coin_collect(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    clear_objects(&w);
    w.spawn_left = w.coin_left = w.sci_left = 1.0e9f;
    w.y = 100;
    w.coins[0] = (gl_coin_t){true, GL_PLAYER_X + 5, 120};
    w.coins[1] = (gl_coin_t){true, GL_PLAYER_X + 5, 250};
    gl_step(&w, DT, false);
    CHECK(!w.coins[0].active); /* picked up */
    CHECK(w.coins[1].active);
}

static void test_shields(void)
{
    gl_world_t w;
    gl_reset(&w, 1);
    CHECK(w.coins_held == 0 && w.shields == 0 && w.invuln == 0.0f);

    /* 10 coins make 1 shield */
    clear_objects(&w);
    w.spawn_left = w.coin_left = w.sci_left = 1.0e9f;
    w.y = 100;
    for (int n = 1; n <= 10; n++) {
        w.coins[0] = (gl_coin_t){true, GL_PLAYER_X + 5, 120};
        gl_step(&w, DT, false);
        w.y = 100;
        w.vy = 0;
        CHECK(w.coins_held == (n % 10) && w.shields == (n / 10));
    }

    /* the cap is 3; coins past it are lost */
    w.shields = GL_MAX_SHIELDS;
    w.coins_held = 9;
    w.coins[0] = (gl_coin_t){true, GL_PLAYER_X + 5, 120};
    gl_step(&w, DT, false);
    CHECK(w.shields == GL_MAX_SHIELDS && w.coins_held == 0);

    /* a hit with a shield costs the shield, gives invincible time and does not kill */
    clear_objects(&w);
    w.y = 100;
    w.vy = 0;
    w.shields = 2;
    w.zappers[0] = (gl_zapper_t){true, GL_PLAYER_X + 8, GL_CEIL_Y, GL_PLAYER_X + 8, 150};
    gl_step(&w, DT, false);
    CHECK(!w.dead && w.shields == 1 && w.invuln > 0.9f);

    /* a hit during invincible time is free */
    w.zappers[0] = (gl_zapper_t){true, GL_PLAYER_X + 8, GL_CEIL_Y, GL_PLAYER_X + 8, 150};
    w.y = 100;
    gl_step(&w, DT, false);
    CHECK(!w.dead && w.shields == 1);

    /* the invincible time runs out; then the next hit costs the last shield, and the one after kills */
    clear_objects(&w);
    run(&w, 1.1f, false);
    CHECK(w.invuln == 0.0f);
    for (int k = 0; k < 2; k++) {
        w.zappers[0] = (gl_zapper_t){true, GL_PLAYER_X + 8, 100, GL_PLAYER_X + 8, 275};
        w.y = 200;
        gl_step(&w, DT, false);
        CHECK(w.dead == (k == 1));
        w.invuln = 0.0f;
    }
    CHECK(w.shields == 0);

    /* a new run starts clean */
    gl_reset(&w, 2);
    CHECK(w.coins_held == 0 && w.shields == 0 && w.invuln == 0.0f);
}

static void test_coin_groups(void)
{
    /* Groups of 3 coins on a straight line, and the angle changes from group to group. */
    gl_world_t w;
    gl_reset(&w, 5);
    clear_objects(&w);
    float slopes[40];
    int groups = 0;
    w.spawn_left = w.sci_left = 1.0e9f;
    for (int i = 0; i < 30 * 600 && groups < 40; i++) {
        w.y = 150; /* keep clear of the coins: only their placing is looked at */
        w.coin_left = (i % 60 == 0) ? 0.0f : 1.0e9f;
        if (i % 60 == 0) {
            memset(w.coins, 0, sizeof(w.coins));
        }
        gl_step(&w, 0.0f, false); /* dt 0: spawn, no scroll */
        if (i % 60 == 0) {
            int n = 0;
            const gl_coin_t *c[GL_MAX_COINS];
            for (int k = 0; k < GL_MAX_COINS; k++) {
                if (w.coins[k].active) {
                    c[n++] = &w.coins[k];
                }
            }
            CHECK(n == 3);
            if (n == 3) {
                float s1 = (c[1]->y - c[0]->y) / (c[1]->x - c[0]->x);
                float s2 = (c[2]->y - c[1]->y) / (c[2]->x - c[1]->x);
                CHECK(fabsf(s1 - s2) < 0.01f * (1.0f + fabsf(s1))); /* one line */
                for (int k = 0; k < 3; k++) {
                    CHECK(c[k]->y > GL_CEIL_Y && c[k]->y < GL_FLOOR_Y);
                }
                slopes[groups++] = s1;
            }
        }
    }
    int different = 0;
    for (int k = 1; k < groups; k++) {
        different += fabsf(slopes[k] - slopes[0]) > 0.1f;
    }
    CHECK(groups == 40 && different > 20);
}

static void test_spawns_fairly(void)
{
    /* Everything that spawns stays inside the play area, and the player can always get past. */
    for (uint32_t seed = 1; seed <= 30; seed++) {
        gl_world_t w;
        gl_reset(&w, seed);
        for (int i = 0; i < 30 * 120 && !w.dead; i++) {
            gl_step(&w, DT, gl_autopilot(&w));
            for (int k = 0; k < GL_MAX_ZAPPERS; k++) {
                const gl_zapper_t *z = &w.zappers[k];
                if (z->active) {
                    CHECK(z->y1 >= GL_CEIL_Y - 0.01f && z->y2 <= GL_FLOOR_Y + 0.01f);
                    CHECK(z->y1 >= GL_CEIL_Y - 0.01f && z->y1 <= GL_FLOOR_Y + 0.01f);
                }
            }
            for (int k = 0; k < GL_MAX_MISSILES; k++) {
                const gl_missile_t *m = &w.missiles[k];
                if (m->active) {
                    CHECK(m->y >= GL_CEIL_Y && m->y <= GL_FLOOR_Y);
                }
            }
        }
    }
}

static void test_autopilot_survives(void)
{
    int survived = 0, total = 200;
    float worst = 1e9f, seconds_total = 0;
    for (uint32_t seed = 1; seed <= (uint32_t)total; seed++) {
        gl_world_t w;
        gl_reset(&w, seed);
        float t = 0;
        for (; t < 90.0f && !w.dead; t += DT) {
            gl_step(&w, DT, gl_autopilot(&w));
        }
        if (t >= 30.0f) {
            survived++;
        }
        if (t < worst) {
            worst = t;
        }
        seconds_total += t;
    }
    printf("autopilot: %d/%d runs lasted 30 s, worst %.1f s, mean %.1f s\n", survived, total, worst,
           seconds_total / total);
    /* The demo restarts after a crash, so it only has to look alive: most runs long, none instant. */
    CHECK(survived >= total * 85 / 100);
    CHECK(worst >= 3.0f);
}

int main(void)
{
    test_reset();
    test_gravity_and_thrust();
    test_distance_and_speed();
    test_deterministic();
    test_zapper_hit();
    test_missile();
    test_objects_scroll_and_go();
    test_coin_collect();
    test_shields();
    test_coin_groups();
    test_spawns_fairly();
    test_autopilot_survives();
    if (s_fail) {
        printf("%d check(s) failed\n", s_fail);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
