#include "power.h"

#include <lvgl.h>

#include "display.h"
#include "screensaver.h"
#include "settings.h"

#define CHECK_PERIOD_MS 1000

static int s_level = 100; /* set from the stored setting in power_init() */
static bool s_asleep;
static bool s_off; /* the backlight is off and the video is stopped */
static bool s_dark; /* the backlight is off because the screensaver could not start */

static void idle_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    if (!s_asleep && lv_disp_get_inactive_time(NULL) > settings_get_timeout_s() * 1000) {
        s_asleep = true;
        if (!screensaver_start()) {
            s_dark = true;
            bsp_display_brightness_set(0);
        }
    }
    if (s_asleep && !s_off && settings_get_screen_off_enabled() &&
        lv_disp_get_inactive_time(NULL) > settings_get_screen_off_min() * 60u * 1000u) {
        s_off = true;
        bsp_display_brightness_set(0);
        if (!s_dark) {
            screensaver_stop(); /* stop the video; the UI repaint is not visible */
        }
    }
}

void power_init(void)
{
    s_level = settings_get_brightness();
    bsp_display_brightness_set(s_level);
    lv_timer_create(idle_timer_cb, CHECK_PERIOD_MS, NULL);
}

void power_set_level(int percent)
{
    s_level = percent;
    if (!s_dark && !s_off) {
        bsp_display_brightness_set(s_level);
    }
}

bool power_touch(void)
{
    if (!s_asleep) {
        return false;
    }
    s_asleep = false;
    if (s_off) {
        s_off = false;
        bsp_display_brightness_set(s_level);
        s_dark = false;
        return true;
    }
    if (s_dark) {
        s_dark = false;
        bsp_display_brightness_set(s_level);
    }
    screensaver_stop();
    return true;
}
