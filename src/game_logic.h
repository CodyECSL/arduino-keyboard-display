#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Rules of the Game page: physics, obstacles, collisions and the demo autopilot. No LVGL in here,
 * so tests/host can run it on a PC. Units are pixels and seconds; x grows to the right, y downwards. */

#define GL_FIELD_W 480
#define GL_CEIL_Y 17   /* the play area starts under the ceiling strip */
#define GL_FLOOR_Y 275 /* and ends above the floor strip */

/* The hitbox of the player. Its left edge is fixed; only y changes. */
#define GL_PLAYER_X 92
#define GL_PLAYER_W 18
#define GL_PLAYER_H 40

#define GL_PX_PER_M 20.0f /* score: 20 pixels of flight are 1 m */

#define GL_MAX_ZAPPERS 3
#define GL_MAX_MISSILES 2
#define GL_MAX_COINS 12
#define GL_MAX_SCIENTISTS 3

#define GL_MISSILE_W 42
#define GL_MISSILE_H 14
#define GL_COIN_R 8
#define GL_COINS_PER_SHIELD 10
#define GL_MAX_SHIELDS 3

typedef struct {
    bool active;
    float x1, y1, x2, y2; /* the two nodes; the beam runs between them */
} gl_zapper_t;

typedef struct {
    bool active;
    float warn; /* seconds of warning left; the missile flies once this is 0 */
    float x;    /* left edge, once flying */
    float y;    /* centre */
} gl_missile_t;

typedef struct {
    bool active;
    float x, y; /* centre */
} gl_coin_t;

typedef struct {
    bool active;
    float x; /* centre; they stand on the floor */
} gl_scientist_t;

typedef struct {
    float y, vy;      /* top of the player's hitbox, and its speed */
    float dist_px;    /* distance flown */
    float speed;      /* scroll speed in px/s */
    bool dead;
    int coins_held;   /* coins toward the next shield, 0 to GL_COINS_PER_SHIELD - 1 */
    int shields;      /* hits the hero can take, 0 to GL_MAX_SHIELDS */
    float invuln;     /* seconds left in which a hit does nothing; set when a shield takes a hit */
    bool thrusting;   /* the thrust input of the last step; the flame follows it */
    float spawn_left; /* pixels until the next obstacle */
    float coin_left;  /* pixels until the next coin pattern */
    float sci_left;   /* pixels until the next scientist */
    uint32_t rng;
    gl_zapper_t zappers[GL_MAX_ZAPPERS];
    gl_missile_t missiles[GL_MAX_MISSILES];
    gl_coin_t coins[GL_MAX_COINS];
    gl_scientist_t scientists[GL_MAX_SCIENTISTS];
} gl_world_t;

/** Start a new run. The same seed gives the same run. */
void gl_reset(gl_world_t *w, uint32_t seed);

/** Advance by dt seconds with the thrust input held or not. Does nothing once dead. */
void gl_step(gl_world_t *w, float dt, bool thrust);

/** Distance in whole meters. */
uint32_t gl_meters(const gl_world_t *w);

/** True if the player's hitbox, placed with its top at y, touches a zapper or a flying missile. */
bool gl_hits_hazard(const gl_world_t *w, float y);

/* The demo autopilot. It picks a lane (the hitbox top) that avoids the hazards, then steers to it.
 * Choosing the lane costs more than steering, so the caller may reuse a lane for a few steps. */
float gl_autopilot_lane(const gl_world_t *w);
bool gl_autopilot_thrust(const gl_world_t *w, float lane);
bool gl_autopilot(const gl_world_t *w); /* both in one call */
