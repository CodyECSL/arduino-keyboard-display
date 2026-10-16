#include "ui.h"

#include <stdio.h>
#include <string.h>

#include <lvgl.h>
#include "esp_heap_caps.h"

#include "display.h"
#include "flash_mode.h"
#include "game.h"
#include "crossy.h"
#include "hid.h"
#include "keymap.h"
#include "power.h"
#include "saver_logic.h"
#include "screensaver.h"
#include "settings.h"
#include "touch_gate.h"
#include "wifi_debug.h"

#define SCREEN_W 480
#define MARGIN 10
#define KNOB_PAD 14 /* the slider knob is drawn this far past the track; the row clips it */
#define COLS 3
#define BTN_W ((SCREEN_W - (COLS + 1) * MARGIN) / COLS)
#define BTN_H 130
#define ROW0_Y MARGIN
#define ROW1_Y (MARGIN + BTN_H + MARGIN)
#define PAGES 6 /* Media, Apps, AI, Game - LabEscape, Game - Crossy Parkway, Settings */
#define GAME_PAGE 3
#define CROSSY_PAGE 4
#define DOTS_Y 298
#define DOT_SIZE 8
#define DOT_GAP 12
#define FLASH_HOLD_MS 3000
#define FLASH_REDRAW_MS 800
#define FLASH_BAR_H 6
#define DIM_AFTER_MS 5000
#define WAKE_FADE_MS 250

#define COLOR_BG lv_color_hex(0x0B0D12)
#define COLOR_CARD lv_color_hex(0x1A1D26)
#define COLOR_BORDER lv_color_hex(0x2A2F3C)
#define COLOR_TEXT lv_color_hex(0xE5E7EB)
#define COLOR_MUTED lv_color_hex(0x8B93A5)
#define COLOR_ACCENT lv_color_hex(0x3B82F6)
#define COLOR_ACCENT_LIGHT lv_color_hex(0x60A5FA)
#define COLOR_DANGER lv_color_hex(0xEF4444)
#define COLOR_OK lv_color_hex(0x22C55E)
#define COLOR_WARN lv_color_hex(0xF59E0B)

typedef enum { KIND_CARD, KIND_PRIMARY, KIND_DANGER } button_kind_t;

/* A grid button sends either a media key (app == -1) or the Apps shortcut `app`. */
typedef struct {
    const char *text;
    media_key_t key;
    int app;
    bool repeat;
    button_kind_t kind;
} button_def_t;

static const button_def_t s_media_buttons[] = {
    {LV_SYMBOL_PREV, MEDIA_KEY_PREV, -1, false, KIND_CARD},
    {LV_SYMBOL_PLAY LV_SYMBOL_PAUSE, MEDIA_KEY_PLAY_PAUSE, -1, false, KIND_PRIMARY},
    {LV_SYMBOL_NEXT, MEDIA_KEY_NEXT, -1, false, KIND_CARD},
    {LV_SYMBOL_VOLUME_MID, MEDIA_KEY_VOL_DOWN, -1, true, KIND_CARD},
    {LV_SYMBOL_MUTE, MEDIA_KEY_MUTE, -1, false, KIND_DANGER},
    {LV_SYMBOL_VOLUME_MAX, MEDIA_KEY_VOL_UP, -1, true, KIND_CARD},
};

static const button_def_t s_app_buttons[] = {
    {LV_SYMBOL_COPY "\nCopy", 0, APP_COPY, false, KIND_CARD},
    {LV_SYMBOL_PASTE "\nPaste", 0, APP_PASTE, false, KIND_CARD},
    {LV_SYMBOL_IMAGE "\nShot", 0, APP_SCREENSHOT, false, KIND_CARD},
    {LV_SYMBOL_LEFT "\nUndo", 0, APP_UNDO, false, KIND_CARD},
    {LV_SYMBOL_RIGHT "\nRedo", 0, APP_REDO, false, KIND_CARD},
    {LV_SYMBOL_SHUFFLE "\nSwitch", 0, APP_SWITCH, false, KIND_CARD},
};

extern const lv_img_dsc_t claude_icon;

static lv_obj_t *s_status;
static lv_obj_t *s_dot;
static lv_obj_t *s_tileview;
static lv_obj_t *s_flash_bar; /* grows left to right while the status label is held */
static lv_obj_t *s_timeout_label;
static lv_obj_t *s_brightness_label;
static lv_obj_t *s_video_dd;
static lv_obj_t *s_off_label, *s_off_slider; /* Display: screen off */
static lv_obj_t *s_multi_sw, *s_video_single_rows[2], *s_video_multi_rows[2], *s_video_list;
static lv_obj_t *s_best_label;        /* Settings, Games: LabEscape best */
static lv_obj_t *s_crossy_best_label; /* Settings, Games: Crossy Parkway best */
static lv_obj_t *s_wifi_switch, *s_wifi_status, *s_wifi_dd, *s_wifi_overlay, *s_wifi_ta;
static lv_timer_t *s_wifi_timer;
static char s_wifi_pick[SETTINGS_WIFI_SSID_MAX + 1]; /* network chosen for the next Connect */
static wifi_debug_ap_t s_wifi_aps[16];
static int s_wifi_ap_count;
static bool s_wifi_waiting_scan; /* set by the Scan button; the timer clears it when the results are in */
static void show_best_score(void);
#define MAX_VIDEOS 16
static char s_video_names[MAX_VIDEOS][SAVER_NAME_MAX];
static char s_video_opts[MAX_VIDEOS * SAVER_NAME_MAX];
static int s_video_count;
static lv_obj_t *s_page_dots[PAGES];
static lv_obj_t *s_fade; /* black overlay on the top layer; fades out after a wake */

static lv_style_t s_st_key_base, s_st_key_face, s_st_key_press;
static lv_style_t s_st_card, s_st_press, s_st_press_card, s_st_primary, s_st_press_primary, s_st_press_danger;

static void wake_fade(void);

static bool s_swallow; /* the current touch only woke the screen */

static lv_timer_t *s_hold_timer; /* 3 s hold detection; NULL when not running */
static bool s_label_woke;        /* the label handler already woke the screen for this touch */
static bool s_flash_pending;     /* "Entering flash mode..." is shown; keep the text */

static bool s_swipe_lock; /* a game page asks for no page swipes (both game pages) */

bool ui_touch_swallowed(void) { return s_swallow; }

void ui_set_swipe_lock(bool lock)
{
    if (lock == s_swipe_lock) {
        return;
    }
    s_swipe_lock = lock;
    if (lock) {
        lv_obj_clear_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    }
}

void ui_set_background(bool opaque)
{
    lv_obj_set_style_bg_opa(lv_scr_act(), opaque ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

/* A swipe shows two pages at once, so the background must be on while the finger moves the tiles. */
static void tile_scroll_cb(lv_event_t *e)
{
    bool drag = lv_event_get_code(e) == LV_EVENT_SCROLL_BEGIN;
    game_set_dragging(drag);
    crossy_set_dragging(drag);
}

static void button_cb(lv_event_t *e)
{
    if (s_swallow) {
        return;
    }
    const button_def_t *def = lv_event_get_user_data(e);
    if (def->app >= 0) {
        hid_send_macro(keymap_app((app_button_t)def->app));
    } else {
        hid_send(def->key);
    }
}

static void ai_key_cb(lv_event_t *e)
{
    (void)e;
    if (s_swallow) {
        return;
    }
    hid_send_macro(keymap_ai_mode());
}

static void os_matrix_cb(lv_event_t *e)
{
    lv_obj_t *m = lv_event_get_target(e);
    keymap_set_os(lv_btnmatrix_get_selected_btn(m) == 1 ? OS_MAC : OS_LINUX);
}

/* A slider drag must not also swipe the page. */
static void slider_lock_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_PRESSED) {
        lv_obj_clear_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void format_timeout(uint32_t sec, char *buf, size_t n)
{
    if (sec < 60) {
        snprintf(buf, n, "%u s", (unsigned)sec);
    } else if (sec % 60 == 0) {
        snprintf(buf, n, "%u min", (unsigned)(sec / 60));
    } else {
        snprintf(buf, n, "%u min %u s", (unsigned)(sec / 60), (unsigned)(sec % 60));
    }
}

/* The slider picks a preset index. The label follows the finger; the value is stored on release. */
static void timeout_slider_cb(lv_event_t *e)
{
    uint32_t sec = settings_timeout_preset(lv_slider_get_value(lv_event_get_target(e)));
    char text[24];
    format_timeout(sec, text, sizeof(text));
    lv_label_set_text(s_timeout_label, text);
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED && sec != settings_get_timeout_s()) {
        settings_set_timeout_s(sec);
    }
}

static void format_off(uint32_t min, char *buf, size_t n)
{
    if (min < 60) {
        snprintf(buf, n, "%u min", (unsigned)min);
    } else if (min % 60 == 0) {
        snprintf(buf, n, "%u h", (unsigned)(min / 60));
    } else {
        snprintf(buf, n, "%u.%u h", (unsigned)(min / 60), (unsigned)(min % 60 * 10 / 60));
    }
}

/* Same as the screensaver slider: the label follows the finger, the value is stored on release. */
static void screen_off_slider_cb(lv_event_t *e)
{
    uint32_t min = settings_screen_off_preset(lv_slider_get_value(lv_event_get_target(e)));
    char text[24];
    format_off(min, text, sizeof(text));
    lv_label_set_text(s_off_label, text);
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED && min != settings_get_screen_off_min()) {
        settings_set_screen_off_min(min);
    }
}

/* Show the slider and its value as usable or not, to match the switch. */
static void screen_off_show_enabled(bool on)
{
    if (on) {
        lv_obj_clear_state(s_off_slider, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_off_slider, LV_STATE_DISABLED);
    }
    lv_obj_set_style_text_color(s_off_label, on ? COLOR_ACCENT : COLOR_MUTED, 0);
}

/* Put a switch back to the stored value (a touch that only woke the screen changes nothing). */
static void switch_restore(lv_obj_t *sw, bool on)
{
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    } else {
        lv_obj_clear_state(sw, LV_STATE_CHECKED);
    }
}

static void screen_off_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (s_swallow) {
        switch_restore(sw, settings_get_screen_off_enabled());
        return;
    }
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_set_screen_off_enabled(on);
    screen_off_show_enabled(on);
}

/* Fill the dropdown from the SD card. Called before it opens, so a swapped card needs no reboot. */
static void video_refresh(void)
{
    s_video_count = screensaver_list_files(s_video_names, MAX_VIDEOS);
    if (s_video_count == 0) {
        lv_dropdown_set_options(s_video_dd, "No video found");
        return;
    }
    s_video_opts[0] = '\0';
    int selected = 0;
    for (int i = 0; i < s_video_count; i++) {
        if (i > 0) {
            strcat(s_video_opts, "\n");
        }
        strcat(s_video_opts, s_video_names[i]);
        if (strcmp(s_video_names[i], screensaver_current_file()) == 0) {
            selected = i;
        }
    }
    lv_dropdown_set_options(s_video_dd, s_video_opts);
    lv_dropdown_set_selected(s_video_dd, selected);
}

static void video_dd_cb(lv_event_t *e)
{
    lv_obj_t *dd = lv_event_get_target(e);
    switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
        if (!lv_dropdown_is_open(dd)) {
            video_refresh();
        }
        break;
    case LV_EVENT_READY: { /* the list was just created: style it */
        lv_obj_t *list = lv_dropdown_get_list(dd);
        lv_obj_set_style_bg_color(list, COLOR_CARD, 0);
        lv_obj_set_style_border_color(list, COLOR_BORDER, 0);
        lv_obj_set_style_border_width(list, 1, 0);
        lv_obj_set_style_radius(list, 14, 0);
        lv_obj_set_style_text_color(list, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
        lv_obj_set_style_max_height(list, 180, 0);
        lv_obj_set_style_bg_color(list, COLOR_ACCENT, LV_PART_SELECTED | LV_STATE_CHECKED);
        break;
    }
    case LV_EVENT_VALUE_CHANGED: {
        int i = lv_dropdown_get_selected(dd);
        if (s_video_count > 0 && i < s_video_count && !screensaver_select_file(s_video_names[i])) {
            video_refresh(); /* could not open it: show the video still in use */
        }
        break;
    }
    default:
        break;
    }
}

/* The list of videos to play, one checkbox each. Rebuilt from the SD card when the switch goes on. */
static void video_check_cb(lv_event_t *e)
{
    lv_obj_t *cb = lv_event_get_target(e);
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    bool on = lv_obj_has_state(cb, LV_STATE_CHECKED);
    char *set = (char *)heap_caps_malloc(SETTINGS_SAVER_SET_MAX, MALLOC_CAP_SPIRAM);
    bool ok = !s_swallow && set && i < s_video_count;
    if (ok) {
        settings_get_saver_set(set, SETTINGS_SAVER_SET_MAX);
        ok = saver_set_toggle(set, SETTINGS_SAVER_SET_MAX, s_video_names[i], on);
    }
    if (ok) {
        settings_set_saver_set(set);
    } else {
        switch_restore(cb, !on); /* woke the screen, or the list is full: undo */
    }
    heap_caps_free(set);
}

static void video_list_build(void)
{
    lv_obj_clean(s_video_list);
    s_video_count = screensaver_list_files(s_video_names, MAX_VIDEOS);
    char *set = (char *)heap_caps_malloc(SETTINGS_SAVER_SET_MAX, MALLOC_CAP_SPIRAM);
    if (set) {
        settings_get_saver_set(set, SETTINGS_SAVER_SET_MAX);
    }
    if (s_video_count == 0) {
        lv_obj_t *l = lv_label_create(s_video_list);
        lv_obj_set_style_text_color(l, COLOR_MUTED, 0);
        lv_label_set_text(l, "No video found");
    }
    for (int i = 0; i < s_video_count; i++) {
        lv_obj_t *cb = lv_checkbox_create(s_video_list);
        lv_checkbox_set_text(cb, s_video_names[i]);
        lv_obj_set_style_text_color(cb, COLOR_TEXT, 0);
        lv_obj_set_style_text_font(cb, &lv_font_montserrat_20, 0);
        lv_obj_set_style_bg_color(cb, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_set_style_text_color(cb, lv_color_white(), LV_PART_INDICATOR | LV_STATE_CHECKED);
        lv_obj_add_flag(cb, LV_OBJ_FLAG_EVENT_BUBBLE);
        if (set && saver_set_has(set, s_video_names[i])) {
            lv_obj_add_state(cb, LV_STATE_CHECKED);
        }
        lv_obj_add_event_cb(cb, video_check_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)i);
    }
    heap_caps_free(set);
}

static void video_show_mode(bool multi)
{
    for (int i = 0; i < 2; i++) {
        if (multi) {
            lv_obj_add_flag(s_video_single_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_video_multi_rows[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_video_single_rows[i], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_video_multi_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void multi_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (s_swallow) {
        switch_restore(sw, settings_get_saver_multi());
        return;
    }
    bool on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    settings_set_saver_multi(on);
    if (on) {
        video_list_build();
    }
    video_show_mode(on);
}

static void random_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (s_swallow) {
        switch_restore(sw, settings_get_saver_random());
        return;
    }
    settings_set_saver_random(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static void page_changed_cb(lv_event_t *e);

/* The tileview refuses to scroll past the first and last page, so such a swipe arrives here as a
 * gesture on the screen. Use it to wrap around. Every gesture also cancels the click that the
 * release would otherwise send to the button the swipe started on. */
static void screen_gesture_cb(lv_event_t *e)
{
    (void)e;
    if (s_swipe_lock) {
        return; /* a drifting finger in a run is no gesture; the thrust must go on */
    }
    s_swallow = true;
    if (!lv_obj_has_flag(s_tileview, LV_OBJ_FLAG_SCROLLABLE)) {
        return; /* a slider is being dragged */
    }
    if (lv_dropdown_is_open(s_video_dd)) {
        return;
    }
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    int active = lv_obj_get_x(lv_tileview_get_tile_act(s_tileview)) / SCREEN_W;
    int target;
    if (dir == LV_DIR_LEFT && active == PAGES - 1) {
        target = 0;
    } else if (dir == LV_DIR_RIGHT && active == 0) {
        target = PAGES - 1;
    } else {
        return;
    }
    lv_obj_set_tile_id(s_tileview, target, 0, LV_ANIM_OFF);
    page_changed_cb(NULL);
    /* The finger is still down. Without this, the rest of the drag would scroll the new page
     * back by one (first -> last would end on the third page). */
    lv_indev_wait_release(lv_indev_get_act());
}

static void page_changed_cb(lv_event_t *e)
{
    (void)e;
    int active = lv_obj_get_x(lv_tileview_get_tile_act(s_tileview)) / SCREEN_W;
    /* The page that hides goes first: each game page sets the screen background when it shows. */
    if (active == CROSSY_PAGE) {
        game_set_visible(false);
        crossy_set_visible(true);
    } else {
        crossy_set_visible(false);
        game_set_visible(active == GAME_PAGE);
    }
    if (active == PAGES - 1) {
        show_best_score();
    }
    if (active == PAGES - 1 && !lv_dropdown_is_open(s_video_dd)) {
        video_refresh(); /* Settings page: pick up the SD card state */
    }
    for (int i = 0; i < PAGES; i++) {
        lv_obj_set_style_bg_opa(s_page_dots[i], i == active ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

/* Apply the brightness while dragging; store it once, when the finger lifts. */
static void slider_cb(lv_event_t *e)
{
    int value = lv_slider_get_value(lv_event_get_target(e));
    lv_label_set_text_fmt(s_brightness_label, "%d%%", value);
    if (lv_event_get_code(e) == LV_EVENT_VALUE_CHANGED) {
        power_set_level(value);
    } else {
        settings_set_brightness(value);
    }
}

static void status_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (s_flash_pending) {
        return;
    }
    static int s_last = -1; /* -1: first call always sets the text */
    int mounted = hid_is_mounted() ? 1 : 0;
    if (mounted != s_last) {
        s_last = mounted;
        lv_label_set_text(s_status, mounted ? "USB connected" : "USB not connected");
        lv_obj_set_style_bg_color(s_dot, mounted ? COLOR_OK : COLOR_WARN, 0);
    }
    /* Dim the status line when idle; full contrast again on the next touch. */
    lv_opa_t opa = lv_disp_get_inactive_time(NULL) > DIM_AFTER_MS ? LV_OPA_40 : LV_OPA_COVER;
    lv_obj_set_style_text_opa(s_status, opa, 0);
    lv_obj_set_style_bg_opa(s_dot, opa, 0);
}

static void flash_enter_cb(lv_timer_t *timer)
{
    (void)timer;
    flash_mode_enter(); /* does not return */
}

static lv_obj_t *flash_shape(lv_obj_t *parent, int x, int y, int w, int h, int radius, lv_color_t fill)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(o, fill, 0);
    return o;
}

/* Full-screen picture: cable -> laptop, so the last frame says "plug in and flash". */
static void flash_screen_show(void)
{
    lv_obj_t *bg = flash_shape(lv_layer_top(), 0, 0, SCREEN_W, 320, 0, lv_color_black());

    lv_obj_t *title = lv_label_create(bg);
    lv_label_set_text(title, "Flash mode");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

    lv_obj_t *hint = lv_label_create(bg);
    lv_label_set_text(hint, "pio run -t upload");
    lv_obj_set_style_text_color(hint, COLOR_MUTED, 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -16);

    /* Laptop: outlined screen and a base bar. */
    lv_obj_t *screen = flash_shape(bg, 270, 105, 160, 100, 10, lv_color_white());
    lv_obj_t *inner = flash_shape(screen, 6, 6, 148, 88, 5, lv_color_hex(0x15171C));
    (void)inner;
    flash_shape(bg, 245, 213, 210, 10, 5, lv_color_white());

    lv_obj_t *arrow = lv_label_create(bg);
    lv_label_set_text(arrow, LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_font(arrow, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(arrow, lv_color_white(), 0);
    lv_obj_set_pos(arrow, 175, 130);

    /* USB cable entering from the left edge: cable, plug body, plug tip. */
    flash_shape(bg, 0, 156, 70, 14, 7, lv_color_white());
    flash_shape(bg, 70, 146, 40, 34, 4, lv_color_white());
    flash_shape(bg, 110, 154, 14, 18, 2, lv_color_hex(0x8B8F98));
}

static void flash_bar_anim_cb(void *obj, int32_t w)
{
    lv_obj_set_width(obj, w);
}

/* Show that the hold was seen: a blue bar fills the top edge in FLASH_HOLD_MS. */
static void flash_bar_start(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_flash_bar);
    lv_anim_set_exec_cb(&a, flash_bar_anim_cb);
    lv_anim_set_values(&a, 1, SCREEN_W);
    lv_anim_set_time(&a, FLASH_HOLD_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_start(&a);
}

static void flash_bar_reset(void)
{
    lv_anim_del(s_flash_bar, flash_bar_anim_cb);
    lv_obj_set_width(s_flash_bar, 0);
}

static void hold_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    s_hold_timer = NULL; /* repeat_count 1: LVGL deletes this timer after the callback */
    s_flash_pending = true;
    lv_label_set_text(s_status, "Entering flash mode...");
    flash_screen_show();
    lv_timer_t *t = lv_timer_create(flash_enter_cb, FLASH_REDRAW_MS, NULL);
    lv_timer_set_repeat_count(t, 1);
}

static void status_hold_cb(lv_event_t *e)
{
    if (s_flash_pending) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (power_touch()) { /* screen was asleep: this touch only wakes, never arms the hold */
            s_label_woke = true;
            wake_fade();
            return;
        }
        if (s_hold_timer == NULL) {
            s_hold_timer = lv_timer_create(hold_timer_cb, FLASH_HOLD_MS, NULL);
            lv_timer_set_repeat_count(s_hold_timer, 1);
            flash_bar_start();
        }
    } else if (s_hold_timer != NULL) { /* RELEASED or PRESS_LOST */
        lv_timer_del(s_hold_timer);
        s_hold_timer = NULL;
        flash_bar_reset();
    }
}

static void styles_init(void)
{
    /* Base card. No transitions: animated fades showed a half-faded button on the right column. */
    lv_style_init(&s_st_card);
    lv_style_set_radius(&s_st_card, 18);
    lv_style_set_bg_opa(&s_st_card, LV_OPA_COVER);
    lv_style_set_bg_color(&s_st_card, COLOR_CARD);
    lv_style_set_border_width(&s_st_card, 1);
    lv_style_set_border_color(&s_st_card, COLOR_BORDER);
    lv_style_set_text_color(&s_st_card, COLOR_TEXT);
    lv_style_set_outline_width(&s_st_card, 0);
    lv_style_set_outline_opa(&s_st_card, LV_OPA_TRANSP);
    lv_style_set_shadow_width(&s_st_card, 0);
    lv_style_set_shadow_opa(&s_st_card, LV_OPA_TRANSP);

    lv_style_init(&s_st_primary);
    lv_style_set_bg_color(&s_st_primary, COLOR_ACCENT);
    lv_style_set_border_width(&s_st_primary, 0);
    lv_style_set_text_color(&s_st_primary, lv_color_white());

    /* Pressed look shared by all buttons: shrink, glow, outline ring. */
    lv_style_init(&s_st_press);
    lv_style_set_transform_width(&s_st_press, -4);
    lv_style_set_transform_height(&s_st_press, -4);
    lv_style_set_shadow_width(&s_st_press, 28);
    lv_style_set_shadow_opa(&s_st_press, LV_OPA_60);
    lv_style_set_outline_width(&s_st_press, 3);
    lv_style_set_outline_opa(&s_st_press, LV_OPA_70);
    lv_style_set_text_color(&s_st_press, lv_color_white());

    lv_style_init(&s_st_press_card);
    lv_style_set_bg_color(&s_st_press_card, COLOR_ACCENT);
    lv_style_set_shadow_color(&s_st_press_card, COLOR_ACCENT);
    lv_style_set_outline_color(&s_st_press_card, COLOR_ACCENT_LIGHT);

    lv_style_init(&s_st_press_primary);
    lv_style_set_bg_color(&s_st_press_primary, COLOR_ACCENT_LIGHT);
    lv_style_set_shadow_color(&s_st_press_primary, COLOR_ACCENT);
    lv_style_set_outline_color(&s_st_press_primary, COLOR_ACCENT_LIGHT);

    /* Keycap seen from above at an angle: a wide dark base (the key's sides) and a smaller,
     * lighter top plate with a gradient. The plate sinks toward the base when pressed. */
    lv_style_init(&s_st_key_base);
    lv_style_set_radius(&s_st_key_base, 30);
    lv_style_set_bg_opa(&s_st_key_base, LV_OPA_COVER);
    lv_style_set_bg_color(&s_st_key_base, lv_color_hex(0x2B2F3A));
    lv_style_set_bg_grad_color(&s_st_key_base, lv_color_hex(0x0A0C10));
    lv_style_set_bg_grad_dir(&s_st_key_base, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&s_st_key_base, 2);
    lv_style_set_border_color(&s_st_key_base, lv_color_hex(0x3B4152));
    lv_style_set_shadow_width(&s_st_key_base, 24);
    lv_style_set_shadow_ofs_y(&s_st_key_base, 8);
    lv_style_set_shadow_color(&s_st_key_base, lv_color_black());
    lv_style_set_shadow_opa(&s_st_key_base, LV_OPA_60);

    lv_style_init(&s_st_key_face);
    lv_style_set_radius(&s_st_key_face, 20);
    lv_style_set_bg_opa(&s_st_key_face, LV_OPA_COVER);
    lv_style_set_bg_color(&s_st_key_face, lv_color_hex(0x4A5166));
    lv_style_set_bg_grad_color(&s_st_key_face, lv_color_hex(0x2E3342));
    lv_style_set_bg_grad_dir(&s_st_key_face, LV_GRAD_DIR_VER);
    lv_style_set_border_width(&s_st_key_face, 2);
    lv_style_set_border_color(&s_st_key_face, lv_color_hex(0x6B7390));
    lv_style_set_border_side(&s_st_key_face, LV_BORDER_SIDE_FULL);
    lv_style_set_text_color(&s_st_key_face, COLOR_TEXT);
    lv_style_set_outline_width(&s_st_key_face, 0);
    lv_style_set_shadow_width(&s_st_key_face, 0);

    lv_style_init(&s_st_key_press);
    lv_style_set_translate_y(&s_st_key_press, 10);
    lv_style_set_bg_color(&s_st_key_press, COLOR_ACCENT_LIGHT);
    lv_style_set_bg_grad_color(&s_st_key_press, COLOR_ACCENT);
    lv_style_set_border_color(&s_st_key_press, lv_color_white());
    lv_style_set_text_color(&s_st_key_press, lv_color_white());

    lv_style_init(&s_st_press_danger);
    lv_style_set_bg_color(&s_st_press_danger, COLOR_DANGER);
    lv_style_set_shadow_color(&s_st_press_danger, COLOR_DANGER);
    lv_style_set_outline_color(&s_st_press_danger, COLOR_DANGER);
}

static void create_button(lv_obj_t *parent, const button_def_t *def, int index)
{
    int col = index % COLS;
    int row = index / COLS;

    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_remove_style_all(btn); /* drop the theme look; styles below replace it */
    lv_obj_add_style(btn, &s_st_card, 0);
    lv_obj_add_style(btn, &s_st_press, LV_STATE_PRESSED);
    if (def->kind == KIND_PRIMARY) {
        lv_obj_add_style(btn, &s_st_primary, 0);
        lv_obj_add_style(btn, &s_st_press_primary, LV_STATE_PRESSED);
    } else if (def->kind == KIND_DANGER) {
        lv_obj_add_style(btn, &s_st_press_danger, LV_STATE_PRESSED);
    } else {
        lv_obj_add_style(btn, &s_st_press_card, LV_STATE_PRESSED);
    }
    lv_obj_set_size(btn, BTN_W, BTN_H);
    lv_obj_set_pos(btn, MARGIN + col * (BTN_W + MARGIN), row == 0 ? ROW0_Y : ROW1_Y);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
    if (def->repeat) {
        /* Short tap: SHORT_CLICKED. Hold: one key at the long-press threshold, then repeats.
         * SHORT_CLICKED is not sent after a long press, so a hold gives no extra key on release. */
        lv_obj_add_event_cb(btn, button_cb, LV_EVENT_SHORT_CLICKED, (void *)def);
        lv_obj_add_event_cb(btn, button_cb, LV_EVENT_LONG_PRESSED, (void *)def);
        lv_obj_add_event_cb(btn, button_cb, LV_EVENT_LONG_PRESSED_REPEAT, (void *)def);
    } else {
        /* CLICKED is sent on release, also after a long press, but not after a scroll. */
        lv_obj_add_event_cb(btn, button_cb, LV_EVENT_CLICKED, (void *)def);
    }

    lv_obj_t *label = lv_label_create(btn);
    bool text_button = def->app >= 0;
    lv_obj_set_style_text_font(label, text_button ? &lv_font_montserrat_22
                                       : def->kind == KIND_PRIMARY ? &lv_font_montserrat_40
                                                                    : &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, def->text);
    lv_obj_center(label);
}

/* A page that lets touches bubble up to the screen (wake handling). */
static lv_obj_t *tile_prepare(lv_obj_t *tile)
{
    /* Not lv_obj_remove_style_all(): size and position of a tile are local styles set by the tileview. */
    lv_obj_set_style_bg_opa(tile, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_outline_width(tile, 0, 0);
    lv_obj_set_style_shadow_width(tile, 0, 0);
    lv_obj_set_style_pad_all(tile, 0, 0);
    lv_obj_set_style_radius(tile, 0, 0);
    lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);
    return tile;
}

static void create_grid_page(lv_obj_t *tile, const button_def_t *defs, int count)
{
    for (int i = 0; i < count; i++) {
        create_button(tile, &defs[i], i);
    }
}

/* AI Mode page: Claude icon on the left half, one keyboard key on the right half. */
static void create_ai_page(lv_obj_t *tile)
{
    lv_obj_t *icon = lv_img_create(tile);
    lv_img_set_src(icon, &claude_icon);
    lv_obj_align(icon, LV_ALIGN_LEFT_MID, SCREEN_W / 4 - 60, -10);

    const int base_w = 224, base_h = 196;       /* the key body */
    const int plate_w = 184, plate_h = 142;     /* the top plate */
    int base_x = SCREEN_W * 3 / 4 - base_w / 2;
    int base_y = (292 - base_h) / 2;
    int plate_x = base_x + (base_w - plate_w) / 2;
    int plate_y = base_y + 14; /* closer to the top edge: the lip below shows the key's front */

    lv_obj_t *base = lv_obj_create(tile);
    lv_obj_remove_style_all(base);
    lv_obj_add_style(base, &s_st_key_base, 0);
    lv_obj_set_size(base, base_w, base_h);
    lv_obj_set_pos(base, base_x, base_y);
    lv_obj_clear_flag(base, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *face = lv_btn_create(tile);
    lv_obj_remove_style_all(face);
    lv_obj_add_style(face, &s_st_key_face, 0);
    lv_obj_add_style(face, &s_st_key_press, LV_STATE_PRESSED);
    lv_obj_set_size(face, plate_w, plate_h);
    lv_obj_set_pos(face, plate_x, plate_y);
    lv_obj_set_ext_click_area(face, 12);
    lv_obj_add_flag(face, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(face, ai_key_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(face);
    lv_obj_set_width(label, plate_w - 28);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(label, AI_PROMPT);
    lv_obj_center(label);
}

static void style_slider(lv_obj_t *slider)
{
    lv_obj_set_style_bg_color(slider, COLOR_CARD, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, COLOR_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 12, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, COLOR_ACCENT, LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_50, LV_PART_KNOB);
    lv_obj_add_flag(slider, LV_OBJ_FLAG_EVENT_BUBBLE);
}

/* Settings page: host OS, brightness, screensaver timeout and video. */
static void create_status(lv_obj_t *scr)
{
    s_dot = lv_obj_create(scr);
    lv_obj_remove_style_all(s_dot);
    lv_obj_set_size(s_dot, 10, 10);
    lv_obj_set_pos(s_dot, 2 * MARGIN, DOTS_Y - 1);
    lv_obj_set_style_radius(s_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_dot, COLOR_WARN, 0);
    lv_obj_clear_flag(s_dot, LV_OBJ_FLAG_CLICKABLE);

    /* On every page. Hold it for 3 seconds to enter flash mode. */
    s_status = lv_label_create(scr);
    lv_obj_set_pos(s_status, 2 * MARGIN + 20, DOTS_Y - 4);
    lv_obj_set_style_text_color(s_status, COLOR_MUTED, 0);
    lv_label_set_text(s_status, "USB not connected");
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(s_status, 8);
    lv_obj_add_flag(s_status, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_status, status_hold_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_status, status_hold_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_status, status_hold_cb, LV_EVENT_PRESS_LOST, NULL);
}

/* A touch on any widget of the menu must reach the screen (wake handling). */
static void bubble_all(lv_obj_t *obj)
{
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    uint32_t n = lv_obj_get_child_cnt(obj);
    for (uint32_t i = 0; i < n; i++) {
        bubble_all(lv_obj_get_child(obj, i));
    }
}

static lv_obj_t *menu_subpage(lv_obj_t *menu)
{
    lv_obj_t *page = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_bg_opa(page, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(page, MARGIN, 0);
    lv_obj_set_style_pad_row(page, MARGIN, 0);
    return page;
}

/* One row of a page. Transparent, so the page background shows. */
static lv_obj_t *menu_row(lv_obj_t *page)
{
    lv_obj_t *row = lv_menu_cont_create(page);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, MARGIN, 0);
    return row;
}

static lv_obj_t *menu_label(lv_obj_t *row, const char *text, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(row);
    lv_obj_set_style_text_color(l, color, 0);
    lv_label_set_text(l, text);
    return l;
}

/* A switch at the end of a row. `cb` runs on every change. */
static lv_obj_t *menu_switch(lv_obj_t *row, bool on, lv_event_cb_t cb)
{
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_style_bg_color(sw, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) {
        lv_obj_add_state(sw, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

/* An entry of the side panel. The checked entry shows the page on the right. */
static void menu_entry(lv_obj_t *menu, lv_obj_t *root, const char *text, lv_obj_t *page)
{
    lv_obj_t *cont = lv_menu_cont_create(root);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(cont, COLOR_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_radius(cont, 10, 0);
    lv_obj_t *l = menu_label(cont, text, COLOR_TEXT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_menu_set_load_page_event(menu, cont, page);
}

typedef enum { GAME_LABESCAPE, GAME_CROSSY } game_t;

static void show_best_score(void)
{
    if (s_best_label) {
        lv_label_set_text_fmt(s_best_label, "%u m", (unsigned)settings_get_high_score());
    }
    if (s_crossy_best_label) {
        lv_label_set_text_fmt(s_crossy_best_label, "%u", (unsigned)settings_get_crossy_best());
    }
}

static void clear_confirm_cb(lv_event_t *e)
{
    lv_obj_t *box = lv_event_get_current_target(e);
    if (lv_msgbox_get_active_btn(box) == 1) {
        if ((game_t)(intptr_t)lv_event_get_user_data(e) == GAME_CROSSY) {
            settings_clear_crossy_best(); /* the coin total stays; the title card reads the value when its page shows */
        } else {
            settings_clear_high_score();
        }
        show_best_score();
    }
    lv_msgbox_close(box);
}

static void clear_best_cb(lv_event_t *e)
{
    if (s_swallow) {
        return;
    }
    game_t game = (game_t)(intptr_t)lv_event_get_user_data(e);
    static const char *btns[] = {"Cancel", "Reset", ""};
    lv_obj_t *box = lv_msgbox_create(NULL, "Reset high score?",
                                     game == GAME_CROSSY ? "The best score of Game - Crossy Parkway goes back to 0. The coins stay."
                                                         : "The best score of Game - LabEscape goes back to 0 m.",
                                     btns, false);
    lv_obj_set_width(box, 380);
    lv_obj_set_style_bg_color(box, COLOR_CARD, 0);
    lv_obj_set_style_border_color(box, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_text_color(box, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(box, &lv_font_montserrat_20, 0);
    lv_obj_t *bar = lv_msgbox_get_btns(box);
    lv_obj_set_height(bar, 48);
    lv_obj_set_style_bg_color(bar, COLOR_BG, LV_PART_ITEMS);
    lv_obj_set_style_text_color(bar, COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(bar, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(bar, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(bar, COLOR_DANGER, LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(box, clear_confirm_cb, LV_EVENT_VALUE_CHANGED, (void *)(intptr_t)game);
    lv_obj_center(box);
}

/* The rows of one game on the Games page: "<name> best  <value>", then the reset button. Returns the value label. */
static lv_obj_t *game_block(lv_obj_t *page, const char *name, game_t game)
{
    char text[48];
    snprintf(text, sizeof(text), LV_SYMBOL_OK "  %s", name);
    lv_obj_t *row = menu_row(page);
    lv_obj_t *l = menu_label(row, text, COLOR_TEXT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_t *value = menu_label(row, "", COLOR_ACCENT);
    lv_obj_set_style_text_font(value, &lv_font_montserrat_20, 0);

    row = menu_row(page);
    lv_obj_t *reset = lv_btn_create(row);
    lv_obj_remove_style_all(reset);
    lv_obj_add_style(reset, &s_st_card, 0);
    lv_obj_add_style(reset, &s_st_press_danger, LV_STATE_PRESSED);
    lv_obj_set_height(reset, 48);
    lv_obj_set_flex_grow(reset, 1);
    lv_obj_t *cl = lv_label_create(reset);
    lv_label_set_text(cl, LV_SYMBOL_TRASH "  Reset High Score");
    lv_obj_set_style_text_font(cl, &lv_font_montserrat_20, 0);
    lv_obj_center(cl);
    lv_obj_add_event_cb(reset, clear_best_cb, LV_EVENT_CLICKED, (void *)(intptr_t)game);
    return value;
}

/* Settings page: a menu with a side panel. Display, Screensaver and System. */
/* ---- Settings > Wi-Fi ------------------------------------------------------------------------------------- */

static void wifi_refresh_status(void)
{
    wifi_debug_status_t st;
    wifi_debug_get_status(&st);
    char text[64];
    switch (st.state) {
    case WIFI_DEBUG_CONNECTING:
        snprintf(text, sizeof(text), "Connecting...");
        break;
    case WIFI_DEBUG_CONNECTED:
        snprintf(text, sizeof(text), "%s\nmacropad.local", st.ip);
        break;
    case WIFI_DEBUG_FAILED:
        snprintf(text, sizeof(text), "Failed: %s\nretrying", st.reason);
        break;
    case WIFI_DEBUG_OFF:
    default:
        snprintf(text, sizeof(text), "%s", st.reason[0] ? st.reason : "Off");
        break;
    }
    if (strcmp(lv_label_get_text(s_wifi_status), text) != 0) {
        lv_label_set_text(s_wifi_status, text);
    }
}

/* Fills the dropdown when a scan has finished. Also keeps the status line current. */
static void wifi_timer_cb(lv_timer_t *t)
{
    (void)t;
    wifi_refresh_status();
    if (!s_wifi_waiting_scan) {
        return;
    }
    bool done;
    int n = wifi_debug_scan_result(s_wifi_aps, 16, &done);
    if (!done) {
        return;
    }
    s_wifi_waiting_scan = false;
    s_wifi_ap_count = n;
    static char opts[16 * 36];
    opts[0] = '\0';
    for (int i = 0; i < n; i++) {
        strcat(opts, s_wifi_aps[i].ssid);
        if (i + 1 < n) {
            strcat(opts, "\n");
        }
    }
    lv_dropdown_set_options(s_wifi_dd, n ? opts : "No networks found");
    if (n) {
        snprintf(s_wifi_pick, sizeof(s_wifi_pick), "%s", s_wifi_aps[0].ssid);
    }
}

static void wifi_scan_cb(lv_event_t *e)
{
    (void)e;
    if (s_swallow) {
        return;
    }
    if (wifi_debug_scan_start()) {
        s_wifi_ap_count = 0;
        s_wifi_waiting_scan = true;
        lv_dropdown_set_options(s_wifi_dd, "Scanning...");
    }
}

static void wifi_switch_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_target(e);
    if (s_swallow) {
        if (settings_get_wifi_enabled()) {
            lv_obj_add_state(sw, LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(sw, LV_STATE_CHECKED);
        }
        return;
    }
    wifi_debug_set_enabled(lv_obj_has_state(sw, LV_STATE_CHECKED));
    wifi_refresh_status();
}

static void wifi_dd_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_VALUE_CHANGED) {
        return;
    }
    uint16_t sel = lv_dropdown_get_selected(s_wifi_dd);
    if (sel < s_wifi_ap_count) {
        snprintf(s_wifi_pick, sizeof(s_wifi_pick), "%s", s_wifi_aps[sel].ssid);
    }
}

static void wifi_overlay_close(void)
{
    if (s_wifi_overlay) {
        lv_obj_del(s_wifi_overlay);
        s_wifi_overlay = NULL;
    }
}

static void wifi_overlay_cancel_cb(lv_event_t *e)
{
    (void)e;
    if (s_swallow) {
        return;
    }
    wifi_overlay_close();
}

static void wifi_overlay_connect_cb(lv_event_t *e)
{
    (void)e;
    if (s_swallow) {
        return;
    }
    wifi_debug_connect(s_wifi_pick, lv_textarea_get_text(s_wifi_ta));
    wifi_overlay_close();
    wifi_refresh_status();
}

/* The overlay is on the top layer, not under the screen, so screen_pressed_cb never sees its touches.
 * Wake the display here. A waking touch sends nothing: s_swallow is set and the touch is released at once. */
static void wifi_overlay_press_cb(lv_event_t *e)
{
    (void)e;
    bool woke = power_touch();
    s_swallow = woke;
    if (woke) {
        wake_fade();
        lv_indev_wait_release(lv_indev_get_act());
    }
}

/* Full-screen password entry: network name, one text line, Cancel and Connect, keyboard below. */
static void wifi_connect_cb(lv_event_t *e)
{
    (void)e;
    if (s_swallow || s_wifi_overlay || s_wifi_pick[0] == '\0') {
        return;
    }
    s_wifi_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wifi_overlay);
    lv_obj_set_size(s_wifi_overlay, SCREEN_W, 320);
    lv_obj_set_style_bg_opa(s_wifi_overlay, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_wifi_overlay, COLOR_BG, 0);
    lv_obj_add_flag(s_wifi_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_wifi_overlay, wifi_overlay_press_cb, LV_EVENT_PRESSED, NULL);

    lv_obj_t *name = lv_label_create(s_wifi_overlay);
    lv_obj_set_pos(name, MARGIN, 8);
    lv_obj_set_width(name, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_color(name, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_20, 0);
    lv_label_set_text_fmt(name, LV_SYMBOL_WIFI "  %s", s_wifi_pick);

    s_wifi_ta = lv_textarea_create(s_wifi_overlay);
    lv_obj_set_pos(s_wifi_ta, MARGIN, 40);
    lv_obj_set_size(s_wifi_ta, 290, 48);
    lv_textarea_set_one_line(s_wifi_ta, true);
    lv_textarea_set_password_mode(s_wifi_ta, true);
    lv_textarea_set_max_length(s_wifi_ta, SETTINGS_WIFI_PASS_MAX);
    lv_textarea_set_placeholder_text(s_wifi_ta, "Password");
    lv_obj_set_style_text_font(s_wifi_ta, &lv_font_montserrat_20, 0);
    lv_obj_set_style_bg_color(s_wifi_ta, COLOR_CARD, 0);
    lv_obj_set_style_text_color(s_wifi_ta, COLOR_TEXT, 0);
    lv_obj_set_style_border_color(s_wifi_ta, COLOR_BORDER, 0);

    static const struct { const char *text; int x; int w; lv_event_cb_t cb; } btns[] = {
        {"Cancel", 308, 78, wifi_overlay_cancel_cb},
        {"Connect", 392, 78, wifi_overlay_connect_cb},
    };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = lv_btn_create(s_wifi_overlay);
        lv_obj_remove_style_all(b);
        lv_obj_add_style(b, &s_st_card, 0);
        lv_obj_set_pos(b, btns[i].x, 40);
        lv_obj_set_size(b, btns[i].w, 48);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, btns[i].text);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, btns[i].cb, LV_EVENT_CLICKED, NULL);
    }

    lv_obj_t *kb = lv_keyboard_create(s_wifi_overlay);
    lv_obj_set_size(kb, SCREEN_W, 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_wifi_ta);
    lv_obj_add_event_cb(kb, wifi_overlay_connect_cb, LV_EVENT_READY, NULL);  /* the OK key */
    lv_obj_add_event_cb(kb, wifi_overlay_cancel_cb, LV_EVENT_CANCEL, NULL);  /* the close key */
    bubble_all(s_wifi_overlay); /* every touch must reach wifi_overlay_press_cb */
}

/* Settings > Wi-Fi: switch, status, network list, Connect. */
static lv_obj_t *create_wifi_page(lv_obj_t *menu)
{
    lv_obj_t *page = menu_subpage(menu);
    lv_obj_t *row = menu_row(page);
    lv_obj_t *l = menu_label(row, LV_SYMBOL_WIFI "  Wi-Fi debug", COLOR_TEXT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(l, 1);
    s_wifi_switch = lv_switch_create(row);
    lv_obj_set_style_bg_color(s_wifi_switch, COLOR_ACCENT, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (settings_get_wifi_enabled()) {
        lv_obj_add_state(s_wifi_switch, LV_STATE_CHECKED);
    }
    lv_obj_add_event_cb(s_wifi_switch, wifi_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

    row = menu_row(page);
    s_wifi_status = menu_label(row, "", COLOR_ACCENT);
    lv_obj_set_style_text_font(s_wifi_status, &lv_font_montserrat_16, 0);
    wifi_refresh_status();

    row = menu_row(page);
    s_wifi_dd = lv_dropdown_create(row);
    lv_obj_set_height(s_wifi_dd, 44);
    lv_obj_set_flex_grow(s_wifi_dd, 1);
    lv_obj_set_style_radius(s_wifi_dd, 14, 0);
    lv_obj_set_style_bg_color(s_wifi_dd, COLOR_CARD, 0);
    lv_obj_set_style_border_color(s_wifi_dd, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_wifi_dd, 1, 0);
    lv_obj_set_style_text_color(s_wifi_dd, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_wifi_dd, &lv_font_montserrat_20, 0);
    lv_obj_set_style_shadow_width(s_wifi_dd, 0, 0);
    lv_obj_set_style_pad_top(s_wifi_dd, 10, 0);
    char saved[SETTINGS_WIFI_SSID_MAX + 1];
    if (settings_get_wifi_ssid(saved, sizeof(saved))) {
        lv_dropdown_set_options(s_wifi_dd, saved);
        snprintf(s_wifi_pick, sizeof(s_wifi_pick), "%s", saved);
    } else {
        lv_dropdown_set_options(s_wifi_dd, "Tap Scan");
    }
    lv_obj_add_event_cb(s_wifi_dd, wifi_dd_cb, LV_EVENT_ALL, NULL);

    row = menu_row(page);
    static const struct { const char *text; lv_event_cb_t cb; } btns[] = {
        {LV_SYMBOL_REFRESH "  Scan", wifi_scan_cb},
        {LV_SYMBOL_OK "  Connect", wifi_connect_cb},
    };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *b = lv_btn_create(row);
        lv_obj_remove_style_all(b);
        lv_obj_add_style(b, &s_st_card, 0);
        lv_obj_set_height(b, 48);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_t *bl = lv_label_create(b);
        lv_label_set_text(bl, btns[i].text);
        lv_obj_set_style_text_font(bl, &lv_font_montserrat_20, 0);
        lv_obj_center(bl);
        lv_obj_add_event_cb(b, btns[i].cb, LV_EVENT_CLICKED, NULL);
    }

    s_wifi_timer = lv_timer_create(wifi_timer_cb, 1000, NULL);
    return page;
}

static void create_settings_page(lv_obj_t *tile)
{
    lv_obj_t *menu = lv_menu_create(tile);
    lv_obj_set_size(menu, SCREEN_W, DOTS_Y - 6);
    lv_obj_set_pos(menu, 0, 0);
    lv_obj_set_style_bg_opa(menu, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_color(menu, COLOR_TEXT, 0);

    /* Display: brightness */
    lv_obj_t *display_page = menu_subpage(menu);
    lv_obj_t *row = menu_row(display_page);
    lv_obj_t *title = menu_label(row, LV_SYMBOL_EYE_OPEN "  Brightness", COLOR_TEXT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(title, 1);
    s_brightness_label = menu_label(row, "", COLOR_ACCENT);
    lv_obj_set_style_text_font(s_brightness_label, &lv_font_montserrat_20, 0);

    row = menu_row(display_page);
    lv_obj_set_style_pad_hor(row, KNOB_PAD, 0);
    lv_obj_t *slider = lv_slider_create(row);
    lv_obj_set_flex_grow(slider, 1);
    lv_obj_set_height(slider, 18);
    style_slider(slider);
    lv_slider_set_range(slider, 5, 100); /* never 0: the screen must stay visible */
    lv_slider_set_value(slider, settings_get_brightness(), LV_ANIM_OFF);
    lv_label_set_text_fmt(s_brightness_label, "%d%%", settings_get_brightness());
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(slider, slider_lock_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(slider, slider_lock_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(slider, slider_lock_cb, LV_EVENT_PRESS_LOST, NULL);

    /* Screen off: a switch, and the idle time (10 min .. 8 h) */
    row = menu_row(display_page);
    title = menu_label(row, LV_SYMBOL_POWER "  Turn screen off", COLOR_TEXT);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(title, 1);
    lv_obj_t *off_sw = menu_switch(row, settings_get_screen_off_enabled(), screen_off_switch_cb);

    row = menu_row(display_page);
    lv_obj_t *off_title = menu_label(row, "Screen off after", COLOR_MUTED);
    lv_obj_set_flex_grow(off_title, 1);
    s_off_label = menu_label(row, "", COLOR_ACCENT);
    lv_obj_set_style_text_font(s_off_label, &lv_font_montserrat_20, 0);

    row = menu_row(display_page);
    lv_obj_set_style_pad_hor(row, KNOB_PAD, 0);
    s_off_slider = lv_slider_create(row);
    lv_obj_set_flex_grow(s_off_slider, 1);
    lv_obj_set_height(s_off_slider, 18);
    style_slider(s_off_slider);
    lv_obj_set_style_bg_color(s_off_slider, COLOR_BORDER, LV_PART_INDICATOR | LV_STATE_DISABLED);
    lv_slider_set_range(s_off_slider, 0, settings_screen_off_preset_count() - 1);
    int oi = settings_screen_off_index(settings_get_screen_off_min());
    lv_slider_set_value(s_off_slider, oi, LV_ANIM_OFF);
    char otext[24];
    format_off(settings_screen_off_preset(oi), otext, sizeof(otext));
    lv_label_set_text(s_off_label, otext);
    lv_obj_add_event_cb(s_off_slider, screen_off_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_off_slider, screen_off_slider_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_off_slider, screen_off_slider_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(s_off_slider, slider_lock_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_off_slider, slider_lock_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_off_slider, slider_lock_cb, LV_EVENT_PRESS_LOST, NULL);
    screen_off_show_enabled(lv_obj_has_state(off_sw, LV_STATE_CHECKED));
    (void)off_title;

    /* Screensaver: timeout (11 presets, 5 s .. 5 min) and video */
    lv_obj_t *saver_page = menu_subpage(menu);
    row = menu_row(saver_page);
    lv_obj_t *l = menu_label(row, LV_SYMBOL_BELL "  Screensaver after", COLOR_TEXT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(l, 1);
    s_timeout_label = menu_label(row, "", COLOR_ACCENT);
    lv_obj_set_style_text_font(s_timeout_label, &lv_font_montserrat_20, 0);

    row = menu_row(saver_page);
    lv_obj_set_style_pad_hor(row, KNOB_PAD, 0);
    lv_obj_t *tslider = lv_slider_create(row);
    lv_obj_set_flex_grow(tslider, 1);
    lv_obj_set_height(tslider, 18);
    style_slider(tslider);
    lv_slider_set_range(tslider, 0, settings_timeout_preset_count() - 1);
    int ti = settings_timeout_index(settings_get_timeout_s());
    lv_slider_set_value(tslider, ti, LV_ANIM_OFF);
    char text[24];
    format_timeout(settings_timeout_preset(ti), text, sizeof(text));
    lv_label_set_text(s_timeout_label, text);
    lv_obj_add_event_cb(tslider, timeout_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(tslider, timeout_slider_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(tslider, timeout_slider_cb, LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(tslider, slider_lock_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(tslider, slider_lock_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(tslider, slider_lock_cb, LV_EVENT_PRESS_LOST, NULL);

    row = menu_row(saver_page);
    l = menu_label(row, LV_SYMBOL_LOOP "  Play multiple videos", COLOR_TEXT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(l, 1);
    s_multi_sw = menu_switch(row, settings_get_saver_multi(), multi_switch_cb);

    row = menu_row(saver_page);
    menu_label(row, "Video", COLOR_MUTED);
    s_video_single_rows[0] = row;
    row = menu_row(saver_page);
    s_video_single_rows[1] = row;
    s_video_dd = lv_dropdown_create(row);
    lv_obj_set_height(s_video_dd, 44);
    lv_obj_set_flex_grow(s_video_dd, 1);
    lv_obj_set_style_radius(s_video_dd, 14, 0);
    lv_obj_set_style_bg_color(s_video_dd, COLOR_CARD, 0);
    lv_obj_set_style_border_color(s_video_dd, COLOR_BORDER, 0);
    lv_obj_set_style_border_width(s_video_dd, 1, 0);
    lv_obj_set_style_text_color(s_video_dd, COLOR_TEXT, 0);
    lv_obj_set_style_text_font(s_video_dd, &lv_font_montserrat_20, 0);
    lv_obj_set_style_shadow_width(s_video_dd, 0, 0);
    lv_obj_set_style_pad_top(s_video_dd, 10, 0);
    video_refresh();
    lv_obj_add_event_cb(s_video_dd, video_dd_cb, LV_EVENT_ALL, NULL);

    /* Several videos: the order, and which videos to play. One ends, the next fades in over 5 s. */
    row = menu_row(saver_page);
    s_video_multi_rows[0] = row;
    l = menu_label(row, LV_SYMBOL_SHUFFLE "  Random order", COLOR_TEXT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_set_flex_grow(l, 1);
    menu_switch(row, settings_get_saver_random(), random_switch_cb);

    row = menu_row(saver_page);
    s_video_multi_rows[1] = row;
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
    menu_label(row, "Videos to play", COLOR_MUTED);
    s_video_list = lv_obj_create(row);
    lv_obj_remove_style_all(s_video_list);
    lv_obj_set_width(s_video_list, LV_PCT(100));
    lv_obj_set_height(s_video_list, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_video_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_video_list, 10, 0);
    video_list_build();
    video_show_mode(settings_get_saver_multi());

    /* System: host OS */
    lv_obj_t *system_page = menu_subpage(menu);
    row = menu_row(system_page);
    menu_label(row, "Host OS", COLOR_MUTED);

    row = menu_row(system_page);
    static const char *map[] = {"Linux", "Mac", ""};
    lv_obj_t *os = lv_btnmatrix_create(row);
    lv_btnmatrix_set_map(os, map);
    lv_btnmatrix_set_btn_ctrl_all(os, LV_BTNMATRIX_CTRL_CHECKABLE);
    lv_btnmatrix_set_one_checked(os, true);
    lv_btnmatrix_set_btn_ctrl(os, keymap_get_os() == OS_MAC ? 1 : 0, LV_BTNMATRIX_CTRL_CHECKED);
    lv_obj_set_height(os, 44);
    lv_obj_set_flex_grow(os, 1);
    lv_obj_set_style_bg_opa(os, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(os, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(os, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_gap(os, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(os, 14, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(os, COLOR_CARD, LV_PART_ITEMS);
    lv_obj_set_style_border_width(os, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(os, COLOR_BORDER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(os, COLOR_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(os, &lv_font_montserrat_22, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(os, COLOR_ACCENT, LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_text_color(os, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_set_style_shadow_width(os, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(os, os_matrix_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Games: for each game its best score and a button that resets it after a confirmation */
    lv_obj_t *game_page = menu_subpage(menu);
    s_best_label = game_block(game_page, "LabEscape best", GAME_LABESCAPE);
    s_crossy_best_label = game_block(game_page, "Crossy Parkway best", GAME_CROSSY);
    show_best_score();

    lv_obj_t *wifi_page = create_wifi_page(menu);

    /* The side panel */
    lv_obj_t *root = lv_menu_page_create(menu, NULL);
    lv_obj_set_style_pad_all(root, MARGIN, 0);
    lv_obj_set_style_pad_row(root, 6, 0);
    menu_entry(menu, root, "Display", display_page);
    menu_entry(menu, root, "Saver", saver_page);
    menu_entry(menu, root, "Games", game_page);
    menu_entry(menu, root, "Wi-Fi", wifi_page);
    menu_entry(menu, root, "System", system_page);
    lv_menu_set_sidebar_page(menu, root);
    lv_event_send(lv_obj_get_child(root, 0), LV_EVENT_CLICKED, NULL); /* open the first entry */

    bubble_all(menu);
}

static void fade_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa(obj, (lv_opa_t)v, 0);
}

static void wake_fade(void)
{
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_fade);
    lv_anim_set_exec_cb(&a, fade_anim_cb);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_time(&a, WAKE_FADE_MS);
    lv_anim_start(&a);
}

/* PRESSED from any widget bubbles up to the screen. Wake first; a waking touch sends no key. */
static void screen_pressed_cb(lv_event_t *e)
{
    (void)e;
    bool woke = power_touch();
    s_swallow = woke || s_label_woke;
    s_label_woke = false;
    if (woke) {
        wake_fade();
    }
}

/* A touch that wakes the screen must do nothing else (no key, no highlight, no swipe, no game start). The gate hides
 * it from LVGL until the finger has been up for a moment; see touch_gate.h. */
static touch_gate_t s_touch_gate;
static void (*s_touch_read_orig)(lv_indev_drv_t *drv, lv_indev_data_t *data);

static bool touch_gate_wake(void)
{
    if (!power_touch()) {
        return false;
    }
    wake_fade();
    /* LVGL resets its inactivity time when it sees a press. It never sees this one, so do it here, or the idle timer
     * (once a second) would start the screensaver again. */
    lv_disp_trig_activity(NULL);
    return true;
}

static void touch_gate_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    s_touch_read_orig(drv, data);
    if (!touch_gate_pressed(&s_touch_gate, data->state == LV_INDEV_STATE_PRESSED, lv_tick_get(), touch_gate_wake)) {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void touch_gate_install(void)
{
    for (lv_indev_t *i = lv_indev_get_next(NULL); i; i = lv_indev_get_next(i)) {
        if (i->driver->type == LV_INDEV_TYPE_POINTER && i->driver->read_cb && !s_touch_read_orig) {
            s_touch_read_orig = i->driver->read_cb;
            i->driver->read_cb = touch_gate_read;
        }
    }
}

void ui_create(void)
{
    styles_init();

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(scr, screen_pressed_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    s_tileview = lv_tileview_create(scr);
    lv_obj_remove_style_all(s_tileview);
    lv_obj_set_size(s_tileview, SCREEN_W, 320);
    lv_obj_set_scrollbar_mode(s_tileview, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_flag(s_tileview, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_event_cb(s_tileview, page_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_tileview, tile_scroll_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_add_event_cb(s_tileview, tile_scroll_cb, LV_EVENT_SCROLL_END, NULL);

    lv_obj_t *media = tile_prepare(lv_tileview_add_tile(s_tileview, 0, 0, LV_DIR_RIGHT));
    lv_obj_t *apps = tile_prepare(lv_tileview_add_tile(s_tileview, 1, 0, LV_DIR_HOR));
    lv_obj_t *ai = tile_prepare(lv_tileview_add_tile(s_tileview, 2, 0, LV_DIR_HOR));
    lv_obj_t *game = tile_prepare(lv_tileview_add_tile(s_tileview, GAME_PAGE, 0, LV_DIR_HOR));
    lv_obj_t *crossy = tile_prepare(lv_tileview_add_tile(s_tileview, CROSSY_PAGE, 0, LV_DIR_HOR));
    lv_obj_t *settings = tile_prepare(lv_tileview_add_tile(s_tileview, PAGES - 1, 0, LV_DIR_LEFT));
    create_grid_page(media, s_media_buttons, sizeof(s_media_buttons) / sizeof(s_media_buttons[0]));
    create_grid_page(apps, s_app_buttons, sizeof(s_app_buttons) / sizeof(s_app_buttons[0]));
    create_ai_page(ai);
    game_create(game);
    crossy_create(crossy);
    create_settings_page(settings);

    /* Page dots. Drawn on the screen, above the tiles, so they stay put while swiping. */
    int dots_w = PAGES * DOT_SIZE + (PAGES - 1) * DOT_GAP;
    for (int i = 0; i < PAGES; i++) {
        lv_obj_t *d = lv_obj_create(scr);
        lv_obj_remove_style_all(d);
        lv_obj_set_size(d, DOT_SIZE, DOT_SIZE);
        lv_obj_set_pos(d, (SCREEN_W - dots_w) / 2 + i * (DOT_SIZE + DOT_GAP), DOTS_Y);
        lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(d, lv_color_white(), 0);
        lv_obj_set_style_bg_opa(d, i == 0 ? LV_OPA_COVER : LV_OPA_30, 0);
        lv_obj_clear_flag(d, LV_OBJ_FLAG_CLICKABLE);
        s_page_dots[i] = d;
    }
    create_status(scr);
    lv_timer_create(status_timer_cb, 500, NULL);

    s_flash_bar = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_flash_bar);
    lv_obj_set_pos(s_flash_bar, 0, 0);
    lv_obj_set_size(s_flash_bar, 0, FLASH_BAR_H);
    lv_obj_set_style_bg_color(s_flash_bar, COLOR_ACCENT, 0);
    lv_obj_set_style_bg_opa(s_flash_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_flash_bar, LV_OBJ_FLAG_CLICKABLE);

    s_fade = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_fade);
    lv_obj_set_size(s_fade, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_fade, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_fade, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(s_fade, LV_OBJ_FLAG_CLICKABLE);

    touch_gate_install();
}
