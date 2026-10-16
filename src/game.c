#include "game.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "game_assets.h"
#include "game_logic.h"
#include "settings.h"
#include "ui.h"

/* The Game page: a jetpack flyer. Press and hold to fly up, let go to fall. The rules are in
 * game_logic.c; this file shows them with LVGL objects and handles touch and the three states
 * (demo, playing, game over). One timer moves everything. */

static const char *TAG = "game";

#define FIELD_H 292
#define TICK_MS 33
#define RETRY_LOCK_MS 600 /* ignore taps right after a crash */
#define WALL_PARALLAX 0.35f
#define BEAM_PTS 8
#define BUBBLE_D 58 /* the shield bubble around the hero */
#define PIP_D 14

#define COLOR_BG lv_color_hex(0x0B0D12)
#define COLOR_CARD lv_color_hex(0x1A1D26)
#define COLOR_BORDER lv_color_hex(0x2A2F3C)
#define COLOR_TEXT lv_color_hex(0xE5E7EB)
#define COLOR_MUTED lv_color_hex(0x8B93A5)
#define COLOR_OK lv_color_hex(0x22C55E)
#define COLOR_GOLD lv_color_hex(0xF5B921)
#define COLOR_BEAM lv_color_hex(0xE8FEFF)
#define COLOR_GLOW lv_color_hex(0x7DF9FF)

typedef enum { ST_DEMO, ST_PLAY, ST_DEAD } state_t;

static lv_obj_t *s_field;
static lv_obj_t *s_wall, *s_ceil, *s_floor, *s_shadow;
static lv_obj_t *s_sci[GL_MAX_SCIENTISTS];
static lv_obj_t *s_coin[GL_MAX_COINS];
static lv_obj_t *s_glow[GL_MAX_ZAPPERS], *s_beam[GL_MAX_ZAPPERS], *s_node[GL_MAX_ZAPPERS][2];
static lv_obj_t *s_missile[GL_MAX_MISSILES], *s_mflame[GL_MAX_MISSILES], *s_warn[GL_MAX_MISSILES];
static lv_obj_t *s_flame, *s_hero;
static lv_obj_t *s_score, *s_best;
static lv_obj_t *s_bubble, *s_pip[GL_MAX_SHIELDS], *s_coins; /* shield bubble and HUD */
static lv_obj_t *s_dim, *s_title1, *s_title2, *s_tap;
static lv_obj_t *s_panel, *s_panel_score, *s_panel_best, *s_panel_retry;

static lv_point_t s_pts[GL_MAX_ZAPPERS][BEAM_PTS]; /* the beams keep pointers to these */
static lv_img_dsc_t s_wall_dsc;                    /* the wall, copied to PSRAM: faster to read than flash */
static lv_timer_t *s_timer;

static gl_world_t s_world;
static state_t s_state = ST_DEMO;
static bool s_visible;
static bool s_dragging; /* a swipe between pages is going on */
static bool s_down;     /* a finger is on the page */
static bool s_edge;     /* the finger just lifted: a tap, if it was not a swipe or a wake-up touch */
static state_t s_press_state; /* the state when the finger landed; a tap must end in the same one */
static uint32_t s_last; /* lv_tick of the last step */
static uint32_t s_tick; /* steps since the page opened; drives the frame animations */
static uint32_t s_runs;
static uint32_t s_dead_ms;
static bool s_new_best;
static float s_lane;     /* the demo's chosen lane */
static float s_wall_off; /* scroll positions, in pixels */
static float s_strip_off;
static float s_dead_y, s_dead_vy; /* the hero's fall after a crash */
static uint32_t s_score_m = UINT32_MAX; /* what the score label shows */
static int s_coins_shown = -1;          /* what the coin label shows */

/* ---- small helpers ---- */

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

static void place(lv_obj_t *o, float x, float y) { lv_obj_set_pos(o, (lv_coord_t)lroundf(x), (lv_coord_t)lroundf(y)); }

static lv_obj_t *img(const lv_img_dsc_t *src)
{
    lv_obj_t *o = lv_img_create(s_field);
    lv_img_set_src(o, src);
    inert(o);
    return o;
}

/* Swapping a frame costs a redraw; skip it when the picture is already that frame. */
static void set_src(lv_obj_t *o, const lv_img_dsc_t *src)
{
    if (lv_img_get_src(o) != src) {
        lv_img_set_src(o, src);
    }
}

static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return inert(l);
}

/* A strip or wall that repeats sideways. It is wider than the field by one period and moves left. */
static lv_obj_t *tiled(const lv_img_dsc_t *src, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(s_field);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_img_src(o, src, 0);
    lv_obj_set_style_bg_img_tiled(o, true, 0);
    return inert(o);
}

static void pulse_cb(void *obj, int32_t v) { lv_obj_set_style_text_opa(obj, (lv_opa_t)v, 0); }

static void pulse_start(lv_obj_t *o, uint32_t half_ms)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, pulse_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_time(&a, half_ms);
    lv_anim_set_playback_time(&a, half_ms);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);
}

static void pulse_stop(lv_obj_t *o)
{
    lv_anim_del(o, pulse_cb);
    lv_obj_set_style_text_opa(o, LV_OPA_COVER, 0);
}

static void panel_y_cb(void *obj, int32_t v) { lv_obj_set_y(obj, v); }

static void panel_slide_in(void)
{
    lv_obj_set_y(s_panel, -190);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_panel);
    lv_anim_set_exec_cb(&a, panel_y_cb);
    lv_anim_set_values(&a, -190, 58);
    lv_anim_set_time(&a, 320);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_start(&a);
}

static void update_best(void) { lv_label_set_text_fmt(s_best, "BEST %u m", (unsigned)settings_get_high_score()); }

static void update_hud(bool on)
{
    for (int i = 0; i < GL_MAX_SHIELDS; i++) {
        show(s_pip[i], on);
        lv_obj_set_style_bg_opa(s_pip[i], i < s_world.shields ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    }
    show(s_coins, on);
    if (on && s_world.coins_held != s_coins_shown) {
        s_coins_shown = s_world.coins_held;
        lv_label_set_text_fmt(s_coins, "%d/%d", s_coins_shown, GL_COINS_PER_SHIELD);
    }
}

/* The wall covers the whole page, so the screen's own background fill is wasted work while this page is
 * up. It is needed while two pages are on screen (a swipe). */
static void update_background(void)
{
    if (s_visible) {
        ui_set_background(s_dragging);
    }
}

/* No page swipe during a run, so a finger that drifts sideways keeps the thrust. After a crash the lock
 * stays until the finger that was down during the run lifts; in the demo and on the Game Over panel a swipe leaves the page. */
static void sync_swipe_lock(void)
{
    ui_set_swipe_lock(s_visible && (s_state == ST_PLAY || (s_state == ST_DEAD && s_down && s_press_state == ST_PLAY)));
}

/* ---- states ---- */

static uint32_t next_seed(void) { return lv_tick_get() ^ (++s_runs * 2654435761u); }

static void enter_demo(void)
{
    s_state = ST_DEMO;
    gl_reset(&s_world, next_seed());
    s_lane = s_world.y;
    update_best();
    show(s_score, false);
    update_hud(false);
    show(s_best, true);
    show(s_dim, true);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_50, 0);
    show(s_title1, true);
    show(s_title2, true);
    show(s_tap, true);
    pulse_start(s_tap, 700);
    show(s_panel, false);
    pulse_stop(s_panel_best);
    sync_swipe_lock();
}

static void start_play(void)
{
    s_state = ST_PLAY;
    gl_reset(&s_world, next_seed());
    pulse_stop(s_tap);
    pulse_stop(s_panel_best);
    show(s_title1, false);
    show(s_title2, false);
    show(s_tap, false);
    show(s_dim, false);
    show(s_panel, false);
    lv_label_set_text(s_score, "0 m");
    s_score_m = 0;
    show(s_score, true);
    s_coins_shown = -1;
    update_hud(true);
    update_best();
    sync_swipe_lock();
}

static void enter_dead(void)
{
    s_state = ST_DEAD;
    s_dead_ms = lv_tick_get();
    s_dead_y = s_world.y;
    s_dead_vy = 0;
    uint32_t m = gl_meters(&s_world);
    s_new_best = m > settings_get_high_score();
    settings_set_high_score(m); /* writes flash only for a new record */
    lv_label_set_text_fmt(s_panel_score, "%u m", (unsigned)m);
    if (s_new_best) {
        lv_label_set_text(s_panel_best, "NEW BEST!");
        lv_obj_set_style_text_color(s_panel_best, COLOR_OK, 0);
        pulse_start(s_panel_best, 350);
    } else {
        lv_label_set_text_fmt(s_panel_best, "BEST %u m", (unsigned)settings_get_high_score());
        lv_obj_set_style_text_color(s_panel_best, COLOR_MUTED, 0);
    }
    update_best();
    show(s_dim, true);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_60, 0);
    show(s_panel, true);
    panel_slide_in();
    sync_swipe_lock();
}

/* ---- drawing the world ---- */

static void set_beam(int i, const gl_zapper_t *z, int variant)
{
    float dx = z->x2 - z->x1, dy = z->y2 - z->y1;
    float len = sqrtf(dx * dx + dy * dy);
    if (len < 1.0f) {
        len = 1.0f;
    }
    float nx = -dy / len, ny = dx / len; /* the beam zigzags sideways of its own direction */
    for (int k = 0; k < BEAM_PTS; k++) {
        float t = (float)k / (float)(BEAM_PTS - 1);
        float off = (k == 0 || k == BEAM_PTS - 1) ? 0.0f : (((k + variant) & 1) ? 7.0f : -7.0f);
        s_pts[i][k].x = (lv_coord_t)lroundf(z->x1 + dx * t + nx * off);
        s_pts[i][k].y = (lv_coord_t)lroundf(z->y1 + dy * t + ny * off);
    }
    lv_line_set_points(s_glow[i], s_pts[i], BEAM_PTS);
    lv_line_set_points(s_beam[i], s_pts[i], BEAM_PTS);
}

static void set_hero(const lv_img_dsc_t *src)
{
    set_src(s_hero, src);
}

static void render(void)
{
    const gl_world_t *w = &s_world;

    lv_obj_set_x(s_wall, -(lv_coord_t)fmodf(s_wall_off, GA_WALL_W));
    lv_obj_set_x(s_ceil, -(lv_coord_t)fmodf(s_strip_off, GA_STRIP_W));
    lv_obj_set_x(s_floor, -(lv_coord_t)fmodf(s_strip_off, GA_STRIP_W));

    for (int i = 0; i < GL_MAX_SCIENTISTS; i++) {
        const gl_scientist_t *s = &w->scientists[i];
        show(s_sci[i], s->active);
        if (s->active) {
            set_src(s_sci[i], ga_sci[(s_tick / 4 + (uint32_t)i) & 1]);
            place(s_sci[i], s->x - GA_SCI_W / 2, GL_FLOOR_Y - GA_SCI_H);
        }
    }
    for (int i = 0; i < GL_MAX_COINS; i++) {
        const gl_coin_t *c = &w->coins[i];
        show(s_coin[i], c->active);
        if (c->active) {
            set_src(s_coin[i], ga_coin[(s_tick / 3 + (uint32_t)i) % 6]);
            place(s_coin[i], c->x - GA_COIN / 2, c->y - GA_COIN / 2);
        }
    }
    int variant = (int)((s_tick / 4) & 1);
    for (int i = 0; i < GL_MAX_ZAPPERS; i++) {
        const gl_zapper_t *z = &w->zappers[i];
        show(s_glow[i], z->active);
        show(s_beam[i], z->active);
        for (int k = 0; k < 2; k++) {
            show(s_node[i][k], z->active);
        }
        if (z->active) {
            set_beam(i, z, variant);
            const lv_img_dsc_t *nf = ga_node[(s_tick / 8) & 1];
            set_src(s_node[i][0], nf);
            set_src(s_node[i][1], nf);
            place(s_node[i][0], z->x1 - GA_NODE / 2, z->y1 - GA_NODE / 2);
            place(s_node[i][1], z->x2 - GA_NODE / 2, z->y2 - GA_NODE / 2);
        }
    }
    for (int i = 0; i < GL_MAX_MISSILES; i++) {
        const gl_missile_t *m = &w->missiles[i];
        bool warning = m->active && m->warn > 0.0f;
        bool flying = m->active && m->warn <= 0.0f;
        show(s_warn[i], warning && ((s_tick / 9) & 1) == 0);
        show(s_missile[i], flying);
        show(s_mflame[i], flying);
        if (warning) {
            place(s_warn[i], GL_FIELD_W - GA_WARN_W - 6, m->y - GA_WARN_H / 2);
        }
        if (flying) {
            place(s_missile[i], m->x - 2, m->y - GA_MISSILE_H / 2);
            set_src(s_mflame[i], ga_mflame[(s_tick / 2) & 1]);
            place(s_mflame[i], m->x + GA_MISSILE_W - 2, m->y - GA_MFLAME_H / 2);
        }
    }

    /* The hero's shadow: darkest on the floor, almost gone at the ceiling. */
    float hero_y = (s_state == ST_DEAD) ? s_dead_y : w->y;
    float height = (float)(GL_FLOOR_Y - GL_PLAYER_H - hero_y) / (float)(GL_FLOOR_Y - GL_PLAYER_H - GL_CEIL_Y);
    lv_obj_set_style_img_opa(s_shadow, (lv_opa_t)(255.0f - 205.0f * height), 0);
    place(s_shadow, GL_PLAYER_X + GL_PLAYER_W / 2 - GA_SHADOW_W / 2, GL_FLOOR_Y + 4);

    /* The hero: its hitbox is centred on the body of the picture. */
    if (s_state == ST_DEAD) {
        set_hero(&ga_hero_dead);
        show(s_flame, false);
        show(s_bubble, false);
        lv_obj_set_style_img_opa(s_hero, LV_OPA_COVER, 0);
        place(s_hero, GL_PLAYER_X + GL_PLAYER_W / 2 - GA_DEAD_W / 2, s_dead_y + GL_PLAYER_H - GA_DEAD_H);
    } else {
        bool on_floor = w->y + GL_PLAYER_H >= GL_FLOOR_Y - 0.5f;
        bool run_b = on_floor && ((s_tick / 4) & 1);
        set_hero(w->thrusting || (!on_floor && w->vy < 120.0f) || (on_floor && !run_b) ? &ga_hero_fly
                                                                                      : &ga_hero_fall);
        float hx = GL_PLAYER_X + GL_PLAYER_W / 2 - 28;
        float hy = w->y - 9;
        place(s_hero, hx, hy);
        /* Invincible time: the hero blinks. */
        lv_obj_set_style_img_opa(s_hero, w->invuln > 0.0f && ((s_tick / 3) & 1) ? LV_OPA_30 : LV_OPA_COVER, 0);
        show(s_bubble, w->shields > 0);
        place(s_bubble, GL_PLAYER_X + GL_PLAYER_W / 2 - BUBBLE_D / 2, w->y + GL_PLAYER_H / 2 - BUBBLE_D / 2);
        show(s_flame, w->thrusting);
        if (w->thrusting) {
            static const uint8_t seq[4] = {0, 1, 2, 1};
            set_src(s_flame, ga_flame[seq[(s_tick / 2) & 3]]);
            place(s_flame, hx + GA_NOZZLE_X - GA_FLAME_W / 2, hy + GA_NOZZLE_Y);
        }
    }
}





/* ---- the timer ---- */

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
    bool swallowed = ui_touch_swallowed(); /* a swipe, or the touch that only woke the screen */
    bool tap = s_edge && !swallowed;
    bool held = s_down && !swallowed;
    s_edge = false;
    s_tick++;

    switch (s_state) {
    case ST_DEMO:
        if (tap) {
            start_play();
            break;
        }
        if (s_tick % 3 == 0) {
            s_lane = gl_autopilot_lane(&s_world);
        }
        gl_step(&s_world, dt, gl_autopilot_thrust(&s_world, s_lane));
        if (s_world.dead) {
            gl_reset(&s_world, next_seed()); /* the demo just starts again */
            s_lane = s_world.y;
        }
        break;
    case ST_PLAY:
        if (held) {
            lv_disp_trig_activity(NULL); /* a long hold must not start the screensaver */
        }
        gl_step(&s_world, dt, held);
        if (gl_meters(&s_world) != s_score_m) {
            s_score_m = gl_meters(&s_world);
            lv_label_set_text_fmt(s_score, "%u m", (unsigned)s_score_m);
        }
        update_hud(true);
        if (s_world.dead) {
            enter_dead();
        }
        break;
    case ST_DEAD:
        s_dead_vy += 900.0f * dt;
        s_dead_y += s_dead_vy * dt;
        if (s_dead_y > GL_FLOOR_Y - GL_PLAYER_H) {
            s_dead_y = GL_FLOOR_Y - GL_PLAYER_H;
            s_dead_vy = 0;
        }
        if (tap && now - s_dead_ms >= RETRY_LOCK_MS) {
            start_play();
        }
        break;
    }

    if (s_state != ST_DEAD) {
        s_wall_off += s_world.speed * WALL_PARALLAX * dt;
        s_strip_off += s_world.speed * dt;
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
        break;
    case LV_EVENT_RELEASED:
        s_down = false;
        /* A run starts when the finger lifts, not when it lands: a swipe starts like a tap, and the swipe
         * lock of a run would block it. A finger that held the thrust until the crash is no retry. */
        s_edge = s_press_state == s_state;
        break;
    case LV_EVENT_PRESS_LOST: /* the touch turned into a swipe */
        s_down = false;
        break;
    default:
        break;
    }
}

/* ---- building the page ---- */

static void wall_to_psram(void)
{
    s_wall_dsc = ga_wall;
    void *p = heap_caps_malloc(ga_wall.data_size, MALLOC_CAP_SPIRAM);
    if (p) {
        memcpy(p, ga_wall.data, ga_wall.data_size);
        s_wall_dsc.data = p;
    } else {
        ESP_LOGW(TAG, "no PSRAM for the wall; drawing it from flash");
    }
}

static lv_obj_t *beam_line(lv_coord_t width, lv_color_t color, lv_opa_t opa)
{
    lv_obj_t *l = lv_line_create(s_field);
    lv_obj_set_style_line_width(l, width, 0);
    lv_obj_set_style_line_color(l, color, 0);
    lv_obj_set_style_line_opa(l, opa, 0);
    lv_obj_set_style_line_rounded(l, true, 0);
    lv_obj_set_pos(l, 0, 0);
    return inert(l);
}

void game_create(lv_obj_t *tile)
{
    wall_to_psram();

    s_field = lv_obj_create(tile);
    lv_obj_remove_style_all(s_field);
    lv_obj_set_size(s_field, GL_FIELD_W, FIELD_H);
    lv_obj_set_pos(s_field, 0, 0);
    /* No background of its own: the screen paints COLOR_BG, and the wall covers the field. A second
     * full-screen fill cost about 10 ms per frame. */
    lv_obj_clear_flag(s_field, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_field, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_field, touch_cb, LV_EVENT_ALL, NULL);

    lv_obj_t *band = inert(lv_obj_create(tile));
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, GL_FIELD_W, 320 - FIELD_H);
    lv_obj_set_pos(band, 0, FIELD_H);
    lv_obj_set_style_bg_color(band, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);

    /* Back to front. */
    s_wall = tiled(&s_wall_dsc, GL_FIELD_W + GA_WALL_W, GA_WALL_H);
    for (int i = 0; i < GL_MAX_SCIENTISTS; i++) {
        s_sci[i] = img(ga_sci[0]);
    }
    for (int i = 0; i < GL_MAX_COINS; i++) {
        s_coin[i] = img(ga_coin[0]);
    }
    for (int i = 0; i < GL_MAX_ZAPPERS; i++) {
        s_glow[i] = beam_line(9, COLOR_GLOW, LV_OPA_30);
        s_beam[i] = beam_line(3, COLOR_BEAM, LV_OPA_COVER);
        s_node[i][0] = img(ga_node[0]);
        s_node[i][1] = img(ga_node[0]);
    }
    for (int i = 0; i < GL_MAX_MISSILES; i++) {
        s_mflame[i] = img(ga_mflame[0]);
        s_missile[i] = img(&ga_missile);
        s_warn[i] = img(&ga_warn);
    }
    s_flame = img(ga_flame[0]);
    s_hero = img(&ga_hero_fly);
    s_bubble = inert(lv_obj_create(s_field)); /* a ring: no fill, so it costs little to draw */
    lv_obj_remove_style_all(s_bubble);
    lv_obj_set_size(s_bubble, BUBBLE_D, BUBBLE_D);
    lv_obj_set_style_radius(s_bubble, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(s_bubble, 3, 0);
    lv_obj_set_style_border_color(s_bubble, COLOR_GLOW, 0);
    lv_obj_set_style_border_opa(s_bubble, LV_OPA_70, 0);
    show(s_bubble, false);
    s_ceil = tiled(&ga_ceil, GL_FIELD_W + GA_STRIP_W, GA_STRIP_H);
    s_floor = tiled(&ga_floor, GL_FIELD_W + GA_STRIP_W, GA_STRIP_H);
    lv_obj_set_y(s_floor, FIELD_H - GA_STRIP_H);
    s_shadow = img(&ga_shadow); /* on the floor plate; fades as the hero climbs */

    s_dim = inert(lv_obj_create(s_field));
    lv_obj_remove_style_all(s_dim);
    lv_obj_set_size(s_dim, GL_FIELD_W, FIELD_H);
    lv_obj_set_style_bg_color(s_dim, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_dim, LV_OPA_50, 0);

    s_score = label(s_field, "0 m", &lv_font_montserrat_28, COLOR_TEXT);
    lv_obj_align(s_score, LV_ALIGN_TOP_LEFT, 16, 24);
    for (int i = 0; i < GL_MAX_SHIELDS; i++) { /* a filled pip is a shield held */
        s_pip[i] = inert(lv_obj_create(s_field));
        lv_obj_remove_style_all(s_pip[i]);
        lv_obj_set_size(s_pip[i], PIP_D, PIP_D);
        lv_obj_set_pos(s_pip[i], 18 + i * (PIP_D + 6), 64);
        lv_obj_set_style_radius(s_pip[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_pip[i], 2, 0);
        lv_obj_set_style_border_color(s_pip[i], COLOR_GLOW, 0);
        lv_obj_set_style_bg_color(s_pip[i], COLOR_GLOW, 0);
        show(s_pip[i], false);
    }
    s_coins = label(s_field, "0/10", &lv_font_montserrat_16, COLOR_GOLD);
    lv_obj_set_pos(s_coins, 18 + GL_MAX_SHIELDS * (PIP_D + 6) + 4, 62);
    show(s_coins, false);
    s_best = label(s_field, "BEST 0 m", &lv_font_montserrat_20, COLOR_MUTED);
    lv_obj_align(s_best, LV_ALIGN_TOP_RIGHT, -16, 28);

    s_title1 = label(s_field, "LAB", &lv_font_montserrat_44, COLOR_GOLD);
    lv_obj_set_style_text_letter_space(s_title1, 6, 0);
    lv_obj_align(s_title1, LV_ALIGN_TOP_MID, 0, 52);
    s_title2 = label(s_field, "ESCAPE", &lv_font_montserrat_44, COLOR_TEXT);
    lv_obj_set_style_text_letter_space(s_title2, 6, 0);
    lv_obj_align(s_title2, LV_ALIGN_TOP_MID, 0, 104);
    s_tap = label(s_field, "Tap to Start", &lv_font_montserrat_24, COLOR_MUTED);
    lv_obj_align(s_tap, LV_ALIGN_TOP_MID, 0, 200);

    s_panel = inert(lv_obj_create(s_field));
    lv_obj_remove_style_all(s_panel);
    lv_obj_set_size(s_panel, 240, 176);
    lv_obj_set_pos(s_panel, 120, 58);
    lv_obj_set_style_radius(s_panel, 12, 0);
    lv_obj_set_style_bg_color(s_panel, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(s_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel, 2, 0);
    lv_obj_set_style_border_color(s_panel, COLOR_BORDER, 0);
    lv_obj_t *over = label(s_panel, "Game Over", &lv_font_montserrat_20, COLOR_MUTED);
    lv_obj_align(over, LV_ALIGN_TOP_MID, 0, 14);
    s_panel_score = label(s_panel, "0 m", &lv_font_montserrat_48, COLOR_TEXT);
    lv_obj_align(s_panel_score, LV_ALIGN_TOP_MID, 0, 46);
    s_panel_best = label(s_panel, "BEST 0 m", &lv_font_montserrat_20, COLOR_MUTED);
    lv_obj_align(s_panel_best, LV_ALIGN_TOP_MID, 0, 108);
    s_panel_retry = label(s_panel, "Tap to retry", &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_align(s_panel_retry, LV_ALIGN_TOP_MID, 0, 142);

    s_timer = lv_timer_create(tick_cb, TICK_MS, NULL);
    lv_timer_pause(s_timer);
    enter_demo();
    render();

    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    ESP_LOGI(TAG, "page built; LVGL heap %u bytes free of %u", (unsigned)mon.free_size,
             (unsigned)(mon.total_size));
}

void game_set_dragging(bool dragging)
{
    s_dragging = dragging;
    update_background();
}

void game_set_visible(bool visible)
{
    if (visible == s_visible) {
        return;
    }
    s_visible = visible;
    update_background();
    if (visible) {
        update_best(); /* the Settings page can clear it */
        s_last = lv_tick_get();
        lv_timer_resume(s_timer);
    } else {
        ui_set_background(true); /* the next page decides again when it shows */
        lv_timer_pause(s_timer);
        s_down = false;
        s_edge = false;
        enter_demo(); /* the page that scrolls back in shows the title again */
        render();
    }
}
