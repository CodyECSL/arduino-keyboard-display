#include "settings.h"

#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define NVS_NS "macropad"
#define KEY_TIMEOUT "saver_s"
#define KEY_FILE "saver_file"
#define KEY_OFF_EN "off_en"
#define KEY_OFF_MIN "off_min"
#define KEY_MULTI "multi"
#define KEY_RANDOM "random"
#define KEY_SET "saver_set"
#define KEY_BRIGHT "bright"
#define KEY_OS "host_os"
#define KEY_HISCORE "hi_score"
#define KEY_CR_BEST "cr_best"
#define KEY_CR_COINS "cr_coins"
#define KEY_WIFI_EN "wifi_en"
#define KEY_WIFI_SSID "wifi_ssid"
#define KEY_WIFI_PASS "wifi_pass"
#define FILE_MAX 64

static const uint16_t s_presets[] = {5, 10, 15, 30, 45, 60, 90, 120, 180, 240, 300};
#define PRESET_COUNT ((int)(sizeof(s_presets) / sizeof(s_presets[0])))

static const uint16_t s_off_presets[] = {10, 15, 20, 30, 45, 60, 90, 120, 180, 240, 360, 480};
#define OFF_PRESET_COUNT ((int)(sizeof(s_off_presets) / sizeof(s_off_presets[0])))

static bool s_off_en = true;
static uint32_t s_off_min = SETTINGS_SCREEN_OFF_DEFAULT_MIN;
static bool s_multi, s_random;
static char s_set[SETTINGS_SAVER_SET_MAX];
static uint32_t s_timeout_s = SETTINGS_TIMEOUT_DEFAULT_S;
static char s_file[FILE_MAX];
static int s_brightness = 100;
static int s_host_os; /* 0 Linux, 1 Mac */
static uint32_t s_high_score;
static uint32_t s_cr_best;
static uint32_t s_cr_coins;
static bool s_wifi_en;
static char s_wifi_ssid[SETTINGS_WIFI_SSID_MAX + 1];
static char s_wifi_pass[SETTINGS_WIFI_PASS_MAX + 1];
static bool s_ok; /* NVS is usable; otherwise settings only live until the next reboot */

int settings_timeout_preset_count(void) { return PRESET_COUNT; }

uint32_t settings_timeout_preset(int index)
{
    if (index < 0) {
        index = 0;
    }
    if (index >= PRESET_COUNT) {
        index = PRESET_COUNT - 1;
    }
    return s_presets[index];
}

int settings_timeout_index(uint32_t seconds)
{
    int best = 0;
    for (int i = 1; i < PRESET_COUNT; i++) {
        uint32_t d = seconds > s_presets[i] ? seconds - s_presets[i] : s_presets[i] - seconds;
        uint32_t b = seconds > s_presets[best] ? seconds - s_presets[best] : s_presets[best] - seconds;
        if (d < b) {
            best = i;
        }
    }
    return best;
}

int settings_screen_off_preset_count(void) { return OFF_PRESET_COUNT; }

uint32_t settings_screen_off_preset(int index)
{
    if (index < 0) {
        index = 0;
    }
    if (index >= OFF_PRESET_COUNT) {
        index = OFF_PRESET_COUNT - 1;
    }
    return s_off_presets[index];
}

int settings_screen_off_index(uint32_t minutes)
{
    int best = 0;
    for (int i = 1; i < OFF_PRESET_COUNT; i++) {
        uint32_t d = minutes > s_off_presets[i] ? minutes - s_off_presets[i] : s_off_presets[i] - minutes;
        uint32_t b = minutes > s_off_presets[best] ? minutes - s_off_presets[best] : s_off_presets[best] - minutes;
        if (d < b) {
            best = i;
        }
    }
    return best;
}

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erasing (%s)", esp_err_to_name(err));
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS init failed: %s; settings will not persist", esp_err_to_name(err));
        return;
    }
    s_ok = true;

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) {
        return; /* first boot: nothing stored yet */
    }
    uint32_t t;
    if (nvs_get_u32(h, KEY_TIMEOUT, &t) == ESP_OK) {
        s_timeout_s = settings_timeout_preset(settings_timeout_index(t));
    }
    uint32_t v;
    if (nvs_get_u32(h, KEY_OFF_EN, &v) == ESP_OK && v <= 1) {
        s_off_en = v != 0;
    }
    if (nvs_get_u32(h, KEY_OFF_MIN, &v) == ESP_OK) {
        s_off_min = settings_screen_off_preset(settings_screen_off_index(v));
    }
    if (nvs_get_u32(h, KEY_MULTI, &v) == ESP_OK && v <= 1) {
        s_multi = v != 0;
    }
    if (nvs_get_u32(h, KEY_RANDOM, &v) == ESP_OK && v <= 1) {
        s_random = v != 0;
    }
    size_t slen = sizeof(s_set);
    if (nvs_get_str(h, KEY_SET, s_set, &slen) != ESP_OK) {
        s_set[0] = '\0';
    }
    if (nvs_get_u32(h, KEY_BRIGHT, &v) == ESP_OK && v >= SETTINGS_BRIGHTNESS_MIN && v <= 100) {
        s_brightness = (int)v;
    }
    if (nvs_get_u32(h, KEY_OS, &v) == ESP_OK && v <= 1) {
        s_host_os = (int)v;
    }
    if (nvs_get_u32(h, KEY_HISCORE, &v) != ESP_OK) {
        v = 0;
    }
    s_high_score = v;
    if (nvs_get_u32(h, KEY_CR_BEST, &v) != ESP_OK) {
        v = 0;
    }
    s_cr_best = v;
    if (nvs_get_u32(h, KEY_CR_COINS, &v) != ESP_OK) {
        v = 0;
    }
    s_cr_coins = v;
    if (nvs_get_u32(h, KEY_WIFI_EN, &v) == ESP_OK && v <= 1) {
        s_wifi_en = v != 0;
    }
    size_t wlen = sizeof(s_wifi_ssid);
    if (nvs_get_str(h, KEY_WIFI_SSID, s_wifi_ssid, &wlen) != ESP_OK) {
        s_wifi_ssid[0] = '\0';
    }
    wlen = sizeof(s_wifi_pass);
    if (nvs_get_str(h, KEY_WIFI_PASS, s_wifi_pass, &wlen) != ESP_OK) {
        s_wifi_pass[0] = '\0';
    }
    size_t len = sizeof(s_file);
    if (nvs_get_str(h, KEY_FILE, s_file, &len) != ESP_OK) {
        s_file[0] = '\0';
    }
    nvs_close(h);
    ESP_LOGI(TAG, "timeout %u s, video '%s', brightness %d, os %d, high score %u", (unsigned)s_timeout_s, s_file,
             s_brightness, s_host_os, (unsigned)s_high_score);
}

uint32_t settings_get_timeout_s(void) { return s_timeout_s; }

static void save(const char *key, uint32_t num, const char *str)
{
    if (!s_ok) {
        return;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = str ? nvs_set_str(h, key, str) : nvs_set_u32(h, key, num);
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "save %s failed: %s", key, esp_err_to_name(err));
    }
}

void settings_set_timeout_s(uint32_t seconds)
{
    s_timeout_s = settings_timeout_preset(settings_timeout_index(seconds));
    save(KEY_TIMEOUT, s_timeout_s, NULL);
}

bool settings_get_screen_off_enabled(void) { return s_off_en; }

void settings_set_screen_off_enabled(bool on)
{
    if (on != s_off_en) {
        s_off_en = on;
        save(KEY_OFF_EN, on ? 1 : 0, NULL);
    }
}

uint32_t settings_get_screen_off_min(void) { return s_off_min; }

void settings_set_screen_off_min(uint32_t minutes)
{
    uint32_t m = settings_screen_off_preset(settings_screen_off_index(minutes));
    if (m != s_off_min) {
        s_off_min = m;
        save(KEY_OFF_MIN, m, NULL);
    }
}

bool settings_get_saver_multi(void) { return s_multi; }

void settings_set_saver_multi(bool on)
{
    if (on != s_multi) {
        s_multi = on;
        save(KEY_MULTI, on ? 1 : 0, NULL);
    }
}

bool settings_get_saver_random(void) { return s_random; }

void settings_set_saver_random(bool on)
{
    if (on != s_random) {
        s_random = on;
        save(KEY_RANDOM, on ? 1 : 0, NULL);
    }
}

bool settings_get_saver_set(char *buf, size_t len)
{
    if (len == 0) {
        return false;
    }
    strncpy(buf, s_set, len - 1);
    buf[len - 1] = '\0';
    return s_set[0] != '\0';
}

void settings_set_saver_set(const char *names)
{
    strncpy(s_set, names, sizeof(s_set) - 1);
    s_set[sizeof(s_set) - 1] = '\0';
    save(KEY_SET, 0, s_set);
}

int settings_get_brightness(void) { return s_brightness; }

void settings_set_brightness(int percent)
{
    if (percent < SETTINGS_BRIGHTNESS_MIN) {
        percent = SETTINGS_BRIGHTNESS_MIN;
    }
    if (percent > 100) {
        percent = 100;
    }
    if (percent != s_brightness) {
        s_brightness = percent;
        save(KEY_BRIGHT, (uint32_t)percent, NULL);
    }
}

int settings_get_host_os(void) { return s_host_os; }

void settings_set_host_os(int os)
{
    os = os ? 1 : 0;
    if (os != s_host_os) {
        s_host_os = os;
        save(KEY_OS, (uint32_t)os, NULL);
    }
}

uint32_t settings_get_high_score(void) { return s_high_score; }

void settings_set_high_score(uint32_t meters)
{
    if (meters > s_high_score) {
        s_high_score = meters;
        save(KEY_HISCORE, meters, NULL);
    }
}

void settings_clear_high_score(void)
{
    s_high_score = 0;
    save(KEY_HISCORE, 0, NULL);
}

uint32_t settings_get_crossy_best(void) { return s_cr_best; }

void settings_set_crossy_best(uint32_t rows)
{
    if (rows > s_cr_best) {
        s_cr_best = rows;
        save(KEY_CR_BEST, rows, NULL);
    }
}

void settings_clear_crossy_best(void)
{
    s_cr_best = 0;
    save(KEY_CR_BEST, 0, NULL);
}

uint32_t settings_get_crossy_coins(void) { return s_cr_coins; }

void settings_add_crossy_coins(uint32_t n)
{
    if (n > 0) {
        s_cr_coins += n;
        save(KEY_CR_COINS, s_cr_coins, NULL);
    }
}

bool settings_get_saver_file(char *buf, size_t len)
{
    if (s_file[0] == '\0' || len == 0) {
        return false;
    }
    strncpy(buf, s_file, len - 1);
    buf[len - 1] = '\0';
    return true;
}

void settings_set_saver_file(const char *name)
{
    strncpy(s_file, name, sizeof(s_file) - 1);
    s_file[sizeof(s_file) - 1] = '\0';
    save(KEY_FILE, 0, s_file);
}

bool settings_get_wifi_enabled(void) { return s_wifi_en; }

void settings_set_wifi_enabled(bool on)
{
    if (on != s_wifi_en) {
        s_wifi_en = on;
        save(KEY_WIFI_EN, on ? 1 : 0, NULL);
    }
}

static bool copy_out(const char *src, char *buf, size_t len)
{
    if (len == 0) {
        return false;
    }
    strncpy(buf, src, len - 1);
    buf[len - 1] = '\0';
    return src[0] != '\0';
}

bool settings_get_wifi_ssid(char *buf, size_t len) { return copy_out(s_wifi_ssid, buf, len); }

bool settings_get_wifi_pass(char *buf, size_t len) { return copy_out(s_wifi_pass, buf, len); }

void settings_set_wifi_credentials(const char *ssid, const char *pass)
{
    strncpy(s_wifi_ssid, ssid, sizeof(s_wifi_ssid) - 1);
    s_wifi_ssid[sizeof(s_wifi_ssid) - 1] = '\0';
    strncpy(s_wifi_pass, pass, sizeof(s_wifi_pass) - 1);
    s_wifi_pass[sizeof(s_wifi_pass) - 1] = '\0';
    save(KEY_WIFI_SSID, 0, s_wifi_ssid);
    save(KEY_WIFI_PASS, 0, s_wifi_pass);
}
