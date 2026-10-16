#include "crossy.h"

#include <math.h>
#include <string.h>

#include "esp_log.h"

#include "crossy_assets.h"
#include "crossy_ground.h"
#include "crossy_logic.h"
#include "settings.h"
#include "ui.h"

/* The Game - Crossy Parkway page: an isometric hopping game. One button: a tap hops forward, or around an obstacle. The rules
 * are in crossy_logic.c; this file shows them and handles touch and the three states
 * (title, playing, game over). One timer moves everything.
 *
 * Drawing: one custom draw object (the "scene") paints the ground and every sprite of the world straight into
 * the draw buffer (crossy_ground.c). Each frame render() makes a list of the sprites to show, lane by lane
 * from far to near. Inside a lane the order is: layer (pads and logs, then animals, coins, props and the
 * hero, then vehicles, train and signals), then footprint centre x. So a nearer thing covers a farther one.
 * Only the HUD, the title and the game-over panel are LVGL objects. */

static const char *TAG = "crossy";

_Static_assert(CG_SWAP == LV_COLOR_16_SWAP, "the ground must pack pixels like LVGL does");

#define FIELD_W 480
#define FIELD_H 292
#define TICK_MS 33
#define RETRY_LOCK_MS 600
#define MAX_ITEMS 640
#define HERO_RESERVE 4 /* item slots that only the hero (and its effect) may use, so a full list never drops the hero */
/* The rows drawn and emitted are base - 12 .. base + WIN_HI. The rules make lanes to hero row + 26 and the camera is
 * at most about 1.4 rows ahead of the best row, so rows above base + 24 might not exist yet (and show nothing). */
#define WIN_HI 24
#define TILE_W 16
#define TILE_H 8
#define CX 160 /* where the camera row meets the screen */
#define CY 210
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

static lv_obj_t *s_field, *s_scene;
static state_t s_state = ST_TITLE;

static cg_item_t s_items[MAX_ITEMS]; /* the sprites of the frame, far lane first (see build_items) */
static cg_item_t s_overlay[1];       /* drawn after everything; empty for now */
static int s_n_items, s_n_overlay;
static cl_world_t s_world_state;
static cl_lane_t s_behind; /* the lane for rows below the start, see lane_for */
static bool s_visible;
static bool s_dragging;
static lv_timer_t *s_timer;
static uint32_t s_last, s_tick, s_runs;
static float s_cam_v; /* the camera as drawn: follows the rules' camera smoothly */
static int s_cam8;    /* the camera as drawn, in eighths of a row: the scene uses it */

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

static lv_obj_t *new_img(lv_obj_t *parent, const ca_sprite_t *s)
{
    lv_obj_t *o = lv_img_create(parent);
    lv_img_set_src(o, s->img);
    return inert(o);
}

/* Rows below the start have no lane in the rules: they are plain grass, made the same way every time.
 * Those rows share one static lane (s_behind): a caller must not hold two results for rows < 0 at once. */
static const cl_lane_t *lane_for(int32_t row)
{
    if (row >= 0) {
        return cl_lane_atc(&s_world_state, row);
    }
    memset(&s_behind, 0, sizeof s_behind);
    s_behind.kind = CL_GRASS;
    s_behind.coin_col = -1;
    for (int i = 0; i < CL_PCOLS; i++) {
        int col = i - CL_EDGE;
        uint32_t h = ((uint32_t)row * 2654435761u) ^ ((uint32_t)(i + 1) * 40503u);
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        bool edge = col < 0 || col > CL_COLS - 1;
        if (!edge && col >= 4 && col <= 6) {
            continue; /* keep the middle clear */
        }
        if (h % 100u < (edge ? 70u : 15u)) {
            s_behind.prop[i] = (uint8_t)(CL_P_PINE + (h >> 8) % (CL_P_STUMP - CL_P_PINE + 1));
        }
    }
    return &s_behind;
}

static float surface_z(int32_t row)
{
    return (float)ca_ground_z[lane_for(row)->kind];
}

/* ---- drawing ---- */

static uint8_t kind_cb(int32_t row, void *user)
{
    (void)user;
    return (uint8_t)lane_for(row)->kind;
}

/* The scene is drawn here, straight into the draw buffer: ground spans and keyed sprite copies, no images. */
static void scene_draw_cb(lv_event_t *e)
{
    lv_draw_ctx_t *ctx = lv_event_get_draw_ctx(e);
    lv_area_t fa;
    lv_obj_get_coords(s_field, &fa);
    cg_target_t t = {.buf = ctx->buf,
                     .stride = lv_area_get_width(ctx->buf_area),
                     .buf_x1 = ctx->buf_area->x1,
                     .buf_y1 = ctx->buf_area->y1,
                     .clip_x1 = ctx->clip_area->x1,
                     .clip_y1 = ctx->clip_area->y1,
                     .clip_x2 = ctx->clip_area->x2,
                     .clip_y2 = ctx->clip_area->y2};
    int32_t base = (int32_t)floorf((float)s_cam8 / 8.0f);
    cg_render(&t, fa.x1, fa.y1, s_cam8, base - 12, base + WIN_HI, kind_cb, NULL, ca_ground_z, s_items, s_n_items, s_overlay,
              s_n_overlay, 0u);
}

/* Screen position (field-relative) of a world point (row, x) at height z, with the camera as drawn. */
static void screen_of(float row, float x, float z, int *sx, int *sy)
{
    float a = row - (float)s_cam8 / 8.0f, b = x - CENTRE_X;
    *sx = (int)lroundf(CX + TILE_W * (a + b));
    *sy = (int)lroundf(CY - TILE_H * a + TILE_H * b - z);
}

/* Make an item for a sprite whose cell corner (row, x = left edge) is at height z. Not for the list yet.
 * Returns false when it is completely outside the field. key_x: the footprint centre x. */
static bool make_item(cg_item_t *it, int32_t lane, uint8_t layer, const ca_sprite_t *spr, float row, float x, float z,
                      float key_x)
{
    int sx, sy;
    screen_of(row, x, z, &sx, &sy);
    int ix = sx - spr->ax, iy = sy - spr->ay;
    int w = spr->img->header.w, h = spr->img->header.h;
    if (ix >= FIELD_W || ix + w <= 0 || iy >= FIELD_H || iy + h <= 0) {
        return false;
    }
    it->x = (int16_t)ix;
    it->y = (int16_t)iy;
    it->w = (uint16_t)w;
    it->h = (uint16_t)h;
    it->px = (const uint16_t *)spr->img->data;
    it->row = lane;
    it->layer = layer;
    it->key = (int16_t)(int)(key_x * 8.0f);
    return true;
}

static void emit(int32_t lane, uint8_t layer, const ca_sprite_t *spr, float row, float x, float z, float key_x)
{
    if (s_n_items < MAX_ITEMS - HERO_RESERVE && make_item(&s_items[s_n_items], lane, layer, spr, row, x, z, key_x)) {
        s_n_items++;
    }
}

/* Order the items first .. s_n_items - 1 of one lane by layer, then key (insertion sort: a lane has few). */
static void sort_lane(int first)
{
    for (int i = first + 1; i < s_n_items; i++) {
        cg_item_t v = s_items[i];
        int j = i - 1;
        while (j >= first && (s_items[j].layer > v.layer || (s_items[j].layer == v.layer && s_items[j].key > v.key))) {
            s_items[j + 1] = s_items[j];
            j--;
        }
        s_items[j + 1] = v;
    }
}

/* The things that belong to the hero or its death: the hero, a flat hero or a splash (all in the lane that
 * the hero stands in or hops past). */
typedef struct {
    cg_item_t it[2];
    int n;
} hero_items_t;

static void hero_items(hero_items_t *out)
{
    const cl_world_t *w = &s_world_state;
    out->n = 0;
    bool dead = w->dead;
    cl_death_t d = w->death;
    bool hero_gone = dead && (d == CL_DEATH_WATER || d == CL_DEATH_SWEPT);
    bool flat = dead && (d == CL_DEATH_CAR || d == CL_DEATH_TRAIN);
    bool title = s_state == ST_TITLE; /* the title has no hero */
    if (!hero_gone && !flat && !title) {
        /* from_row and from_x are only set when a hop starts and stay after it ends: use them only while hopping. */
        bool hop = w->hop_t > 0.0f;
        float row = (float)w->row, x = w->x, gz = surface_z(w->row), arc = 0.0f;
        int32_t lane = w->row; /* standing: its own lane, so nearer props to its right cover it */
        if (hop) {
            float p = 1.0f - w->hop_t / CL_HOP_S;
            row = (float)w->from_row + (float)(w->row - w->from_row) * p;
            x = w->from_x + (w->x - w->from_x) * p;
            gz = surface_z(w->from_row) + (surface_z(w->row) - surface_z(w->from_row)) * p;
            arc = HOP_ARC_PX * sinf(3.14159265f * p);
            lane = w->row < w->from_row ? w->row : w->from_row; /* in front of the far lane, behind the near one */
        }
        const ca_sprite_t *spr = &ca_hero[0][hop ? 1 : 0]; /* always forward: the face never turns to the camera */
        if (make_item(&out->it[out->n], lane, 1, spr, row, x - 0.5f, gz + arc, x)) {
            out->n++;
        }
    }
    const ca_sprite_t *fx = NULL;
    if (flat) {
        fx = &ca_hero_flat;
    } else if (dead && (d == CL_DEATH_WATER || d == CL_DEATH_SWEPT) && w->death_t < 0.5f) {
        fx = &ca_splash[w->death_t < 0.2f ? 0 : 1];
    }
    if (fx && make_item(&out->it[out->n], w->row, 1, fx, (float)w->row, w->x - 0.5f, surface_z(w->row), w->x)) {
        out->n++;
    }
}

/* Add all sprites of one lane (not the hero) in no particular order. */
static void emit_lane(int32_t row)
{
    const cl_lane_t *l = lane_for(row);
    float gz = (float)ca_ground_z[l->kind];
    float rf = (float)row;
    for (int c = 0; c < CL_COLS; c++) {
        if ((l->pad_mask >> c) & 1) {
            emit(row, 0, &ca_pad, rf, (float)c, gz, (float)c + 0.5f);
        }
    }
    if (l->kind == CL_RIVER) {
        for (int i = 0; i < l->nobj && i < CL_MAX_OBJ; i++) {
            emit(row, 0, &ca_log[(int)l->obj[i].len - 2], rf, l->obj[i].x, gz, l->obj[i].x + l->obj[i].len * 0.5f);
        }
    }
    if (l->deco_kind) {
        const ca_sprite_t *d = &ca_deco[l->deco_kind][((s_tick / 12) + (uint32_t)l->deco_col) & 1];
        emit(row, 1, d, rf, (float)l->deco_col, gz, (float)l->deco_col + 0.5f);
    }
    if (l->coin_col >= 0) {
        emit(row, 1, &ca_coin[(s_tick / 3) & 3], rf, (float)l->coin_col, gz, (float)l->coin_col + 0.5f);
    }
    for (int i = 0; i < CL_PCOLS; i++) {
        if (l->prop[i]) {
            float c = (float)(i - CL_EDGE);
            emit(row, 1, &ca_prop[l->prop[i]], rf, c, gz, c + 0.5f);
        }
    }
    if (l->kind == CL_ROAD) {
        for (int i = 0; i < l->nobj && i < CL_MAX_OBJ; i++) {
            emit(row, 2, &ca_vehicle[l->obj[i].kind][l->dir > 0 ? 0 : 1], rf, l->obj[i].x, gz,
                 l->obj[i].x + l->obj[i].len * 0.5f);
        }
    } else if (l->kind == CL_RAIL) {
        if (l->train) {
            float x = l->obj[0].x;
            for (int i = 0; i < 4; i++) { /* the locomotive leads, whatever the direction */
                float cell = l->dir > 0 ? x + CL_TRAIN_LEN - 2.0f - 2.0f * (float)i : x + 2.0f * (float)i;
                emit(row, 2, i == 0 ? &ca_loco[l->dir > 0 ? 0 : 1] : &ca_carriage, rf, cell, gz, cell + 1.0f);
            }
        }
        const ca_sprite_t *sig = &ca_signal[l->signal ? 1 + ((s_tick / 6) & 1) : 0];
        emit(row, 2, sig, rf, -1.0f, gz, -0.5f);
        emit(row, 2, sig, rf, (float)CL_COLS, gz, (float)CL_COLS + 0.5f);
    }
}

/* Make the list of sprites for the frame: rows from far to near, each row sorted by layer and key. The
 * whole list is then in the order cg_render needs. No allocation, no sort of the whole list. */
static void build_items(void)
{
    int32_t base = (int32_t)floorf((float)s_cam8 / 8.0f); /* the window of the ground */
    hero_items_t hero;
    hero_items(&hero);
    s_n_items = 0;
    for (int32_t row = base + WIN_HI; row >= base - 12; row--) {
        int first = s_n_items;
        emit_lane(row);
        for (int i = 0; i < hero.n; i++) {
            if (hero.it[i].row == row && s_n_items < MAX_ITEMS) {
                s_items[s_n_items++] = hero.it[i];
            }
        }
        sort_lane(first);
    }
    s_n_overlay = 0; /* nothing is drawn over the field now; the eagle sprites in crossy_assets are kept for later */
}

static void render(void)
{
    float diff = s_world_state.cam - s_cam_v;
    s_cam_v = fabsf(diff) > 4.0f ? s_world_state.cam : s_cam_v + diff * 0.35f;
    /* Place the world in steps of 1/8 row. One step is exactly 2 px across and 1 px down, so x and y always
     * change together and the diagonal scroll stays smooth (separate rounding made them change in turns). */
    s_cam8 = (int)floorf(s_cam_v * 8.0f + 0.5f);
    build_items();
    lv_obj_invalidate(s_scene);
}

/* ---- states and timer ---- */

static uint32_t next_seed(void) { return lv_tick_get() ^ (++s_runs * 2654435761u); }

static void new_world(void)
{
    cl_reset(&s_world_state, next_seed());
    s_cam_v = s_world_state.cam;
}

/* The screen background is only needed while two pages are on screen (a swipe); the world covers the field. */
static void update_background(void)
{
    if (s_visible) {
        ui_set_background(s_dragging);
    }
}

static lv_obj_t *s_score, *s_score_sh, *s_coins, *s_coin_icon;
static lv_obj_t *s_title, *s_title_best;
static lv_obj_t *s_panel, *s_panel_score, *s_panel_best, *s_panel_coins;
static bool s_down;           /* a finger is on the page */
static bool s_edge;           /* the finger just lifted: a tap, if it was not a swipe or a wake-up touch */
static bool s_hop;            /* the finger just landed during a run: hop */
static bool s_panel_up;       /* the game-over panel is shown */
static bool s_saved;          /* the result of this death is stored (once per death) */
static bool s_new_best;       /* this run beat the best score; decided before it was stored */
static state_t s_press_state; /* the state when the finger landed; a lift must end in the same one */
static uint32_t s_dead_ms;    /* when the game-over panel appeared */
static uint32_t s_score_shown = UINT32_MAX;
static int s_coins_shown = -1;


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

static void show_hud(bool on)
{
    show(s_score, on);
    show(s_score_sh, on);
    show(s_coins, on);
    show(s_coin_icon, on);
}

static void enter_title(void)
{
    s_state = ST_TITLE;
    s_panel_up = false;
    new_world();
    update_best_label();
    show_hud(false);
    show(s_panel, false);
    show(s_title, true);
    sync_swipe_lock();
}

static void start_play(void)
{
    s_state = ST_PLAY;
    s_panel_up = false;
    s_saved = false;
    new_world();
    s_score_shown = UINT32_MAX;
    s_coins_shown = -1;
    lv_label_set_text(s_score, "0");
    lv_label_set_text(s_score_sh, "0");
    lv_label_set_text(s_coins, "0");
    show(s_title, false);
    show(s_panel, false);
    show_hud(true);
    sync_swipe_lock();
}

static void panel_y_cb(void *obj, int32_t v) { lv_obj_set_y(obj, v); }

/* The crash is over (the flat hero or the splash was seen): show the panel. The result is already stored. */
static void show_panel(void)
{
    uint32_t score = cl_score(&s_world_state);
    bool best = s_new_best;
    lv_label_set_text_fmt(s_panel_score, "%u", (unsigned)score);
    if (best) {
        lv_label_set_text(s_panel_best, "NEW BEST!");
    } else {
        lv_label_set_text_fmt(s_panel_best, "BEST %u", (unsigned)settings_get_crossy_best());
    }
    lv_obj_set_style_text_color(s_panel_best, best ? COLOR_OK : COLOR_MUTED, 0);
    lv_label_set_text_fmt(s_panel_coins, s_world_state.coins == 1 ? "+%d coin" : "+%d coins", s_world_state.coins);
    show(s_panel, true);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, panel_y_cb);
    lv_anim_set_values(&a, -190, 58);
    lv_anim_set_time(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
    s_panel_up = true;
    s_dead_ms = lv_tick_get();
}

/* The run ends: store the result now, once, so a swipe to another page before the panel shows loses nothing. */
static void enter_dead(void)
{
    s_state = ST_DEAD;
    s_panel_up = false;
    if (!s_saved) {
        s_saved = true;
        uint32_t score = cl_score(&s_world_state);
        s_new_best = score > settings_get_crossy_best(); /* decided before the store */
        settings_set_crossy_best(score); /* flash is written only for a new record */
        settings_add_crossy_coins((uint32_t)s_world_state.coins);
    }
    sync_swipe_lock();
}

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
        s_hop = false;
        return;
    }
    bool swallowed = ui_touch_swallowed(); /* a swipe between pages, or the touch that only woke the screen */
    bool tap = s_edge && !swallowed;
    bool hop = s_hop && !swallowed;
    s_edge = false;
    s_hop = false;
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
        break;
    case ST_PLAY:
        if (hop) {
            cl_tap(&s_world_state);
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
        if (!s_panel_up && s_world_state.death_t >= 0.6f) {
            show_panel();
        }
        if (tap && s_panel_up && now - s_dead_ms >= RETRY_LOCK_MS) {
            start_play();
        }
        break;
    }
    sync_swipe_lock();
    render();
}

/* ---- touch ---- */

static void touch_cb(lv_event_t *e)
{
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        s_down = true;
        s_press_state = s_state;
        s_hop = s_state == ST_PLAY; /* a hop game needs low latency: the hop goes at once, not at the lift */
        break;
    case LV_EVENT_RELEASED:
        s_down = false;
        /* Start and retry go when the finger lifts: a swipe between pages starts like a tap. A finger that
         * was down during the run until the crash is no retry. */
        s_edge = s_press_state == s_state;
        break;
    case LV_EVENT_PRESS_LOST: /* the touch turned into a swipe between pages */
        s_down = false;
        break;
    default:
        break;
    }
}

/* ---- building the page ---- */

void crossy_create(lv_obj_t *tile)
{
    ca_to_psram(); /* PSRAM is faster to read than flash; the scene copies sprite pixels from there */
    s_field = inert(lv_obj_create(tile));
    lv_obj_remove_style_all(s_field);
    lv_obj_set_size(s_field, FIELD_W, FIELD_H);
    lv_obj_set_pos(s_field, 0, 0);
    lv_obj_add_flag(s_field, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_field, touch_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *band = inert(lv_obj_create(tile)); /* hides the page dots' strip behind the world */
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, FIELD_W, 320 - FIELD_H);
    lv_obj_set_pos(band, 0, FIELD_H);
    lv_obj_set_style_bg_color(band, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);

    s_scene = inert(lv_obj_create(s_field)); /* first child: everything else is on top of it */
    lv_obj_remove_style_all(s_scene);
    lv_obj_set_size(s_scene, FIELD_W, FIELD_H);
    lv_obj_set_pos(s_scene, 0, 0);
    lv_obj_add_event_cb(s_scene, scene_draw_cb, LV_EVENT_DRAW_MAIN, NULL);

    s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_timer_pause(s_timer);

    s_score_sh = label(s_field, "0", &lv_font_montserrat_28, lv_color_hex(0x203040));
    lv_obj_set_pos(s_score_sh, 18, 14);
    s_score = label(s_field, "0", &lv_font_montserrat_28, lv_color_white());
    lv_obj_set_pos(s_score, 16, 12);
    s_coin_icon = new_img(s_field, &ca_coin[0]);
    lv_obj_set_pos(s_coin_icon, 396, 4); /* room for a 4 digit count at the right edge */
    s_coins = label(s_field, "0", &lv_font_montserrat_20, COLOR_GOLD);
    lv_obj_set_width(s_coins, 60);
    lv_obj_set_style_text_align(s_coins, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_coins, LV_ALIGN_TOP_RIGHT, -10, 14);

    s_title = card(s_field, 260, 206);
    lv_obj_set_pos(s_title, 110, 40);
    lv_obj_t *name = label(s_title, "CROSSY\nPARKWAY", &lv_font_montserrat_44, COLOR_GOLD); /* two lines: one would not fit */
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(name, LV_ALIGN_TOP_MID, 0, 12);
    lv_obj_t *start = label(s_title, "Tap to Start", &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_align(start, LV_ALIGN_TOP_MID, 0, 120);
    s_title_best = label(s_title, "", &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_align(s_title_best, LV_ALIGN_TOP_MID, 0, 160);

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

    enter_title();
    render();

    ESP_LOGI(TAG, "page built");
}

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
        s_last = lv_tick_get();
        update_best_label(); /* only a run changes it, but a new page entry is a cheap place to refresh */
        lv_timer_resume(s_timer);
    } else {
        ui_set_background(true); /* the next page decides again when it shows */
        lv_timer_pause(s_timer);
        s_down = false;
        s_edge = false;
        s_hop = false;
        enter_title(); /* the page that scrolls back in shows the title again */
        render();
    }
}
