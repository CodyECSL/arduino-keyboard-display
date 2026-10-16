#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Rules of the Crossy page: lanes, hopping, hazards, river and camera. No LVGL in here, so
 * tests/host can run it on a PC. A row is one lane; row 0 is the start and rows grow forward. A column
 * position x is in cells: column c spans x = c .. c + 1, and the hero stands at x = c + 0.5. */

#define CL_COLS 11       /* playable columns, 0 to 10 */
#define CL_START_COL 5
#define CL_EDGE 3        /* extra decoration columns on each side of a grass lane */
#define CL_PCOLS (CL_COLS + 2 * CL_EDGE)
#define CL_RING 64       /* lanes kept in memory */
#define CL_AHEAD 26      /* rows generated in front of the hero */
#define CL_LOOP 38.0f    /* road and river objects wrap over this many cells ... */
#define CL_X_MIN -14.0f  /* ... starting here, so they leave the screen before they wrap */
#define CL_MAX_OBJ 4

#define CL_HOP_S 0.13f
#define CL_HERO_LEAD 2       /* the camera keeps the hero this many rows in front of its row */
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
    CL_DEATH_SWEPT /* a log carried the hero off the field; no longer happens, a hero at the edge is held */
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
    uint32_t seed;
    int32_t coin_row; /* coins are already decided for the rows up to here */
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
    bool q_tap;       /* the queued request is a tap, decided when it starts */
    uint8_t tap_side; /* alternates the side taken when both diagonals are free and the hero is in the middle column */
    int32_t best_row; /* the score */
    int coins;
    float cam;        /* row at the bottom reference of the screen; the hero stands at cam + CL_HERO_LEAD */
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

/** Ask for a hop: dr = +1 forward, -1 back; dc = +1 toward +x, -1 toward -x. Others are 0.
 * This is the low-level hop used by tests and cl_tap; the firmware itself only calls cl_tap. */
void cl_hop(cl_world_t *w, int dr, int dc);

/** The one-button control. Hops forward; when a prop blocks the lane ahead, hops diagonally forward around it. */
void cl_tap(cl_world_t *w);

/** Put coins in the lanes ahead, on the cells that taps lead the hero to (the same rules as cl_tap). It stops at a
 * log lane, because the hero's column after it depends on the player. cl_reset and every landing call it. */
void cl_place_coins(cl_world_t *w);

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
