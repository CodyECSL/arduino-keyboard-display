#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Persistent settings in NVS. They survive power cycles and normal flashes
 * (a full chip erase wipes them). Call settings_init() once, first thing. */
void settings_init(void);

/* Screensaver timeout. Values are limited to the presets below (5 s .. 5 min). */
#define SETTINGS_TIMEOUT_DEFAULT_S 30
int settings_timeout_preset_count(void);
uint32_t settings_timeout_preset(int index);
/** Index of the preset equal to or closest to `seconds`. */
int settings_timeout_index(uint32_t seconds);
uint32_t settings_get_timeout_s(void);
void settings_set_timeout_s(uint32_t seconds);

/* Screen off: after this idle time the backlight goes off and the video stops. The switch is on by
 * default (60 min). Off means the screen stays on. The time is limited to the presets (10 min .. 8 h). */
#define SETTINGS_SCREEN_OFF_DEFAULT_MIN 60
bool settings_get_screen_off_enabled(void);
void settings_set_screen_off_enabled(bool on);
int settings_screen_off_preset_count(void);
uint32_t settings_screen_off_preset(int index); /* minutes */
int settings_screen_off_index(uint32_t minutes);
uint32_t settings_get_screen_off_min(void);
void settings_set_screen_off_min(uint32_t minutes);

/* Backlight level in percent, 5..100. Default 100. */
#define SETTINGS_BRIGHTNESS_MIN 5
int settings_get_brightness(void);
void settings_set_brightness(int percent);

/* Host OS for the Apps and AI Mode shortcuts: 0 = Linux (default), 1 = Mac. */
int settings_get_host_os(void);
void settings_set_host_os(int os);

/* Name of the screensaver video in /mjpeg/ on the SD card. Empty if never chosen. */
bool settings_get_saver_file(char *buf, size_t len);
void settings_set_saver_file(const char *name);

/* Play several screensaver videos one after the other. The chosen videos are one string: file names,
 * one per line. Order is the file name order, or random. Off by default. */
#define SETTINGS_SAVER_SET_MAX 1024
bool settings_get_saver_multi(void);
void settings_set_saver_multi(bool on);
bool settings_get_saver_random(void);
void settings_set_saver_random(bool on);
bool settings_get_saver_set(char *buf, size_t len);
void settings_set_saver_set(const char *names);

/* Best distance of the Game page, in meters. 0 if never played. */
uint32_t settings_get_high_score(void);
/** Stores the score only if it beats the current one, so flash is written once per new record. */
void settings_set_high_score(uint32_t meters);
/** Sets the best score back to 0 and stores it. */
void settings_clear_high_score(void);

/* Best distance of the Crossy page, in rows, and the coins collected over all runs. */
uint32_t settings_get_crossy_best(void);
/** Stores the value only if it beats the current one. */
void settings_set_crossy_best(uint32_t rows);
/* Set the best distance back to 0 and store it. The coin total stays. */
void settings_clear_crossy_best(void);
uint32_t settings_get_crossy_coins(void);
/** Adds to the total; writes flash only if n is above 0. */
void settings_add_crossy_coins(uint32_t n);

/* Wi-Fi debug link. The switch is off by default. The password is never logged. */
#define SETTINGS_WIFI_SSID_MAX 32
#define SETTINGS_WIFI_PASS_MAX 63
bool settings_get_wifi_enabled(void);
void settings_set_wifi_enabled(bool on);
/** Copy the stored value. Returns false (and an empty string) if none is stored. */
bool settings_get_wifi_ssid(char *buf, size_t len);
bool settings_get_wifi_pass(char *buf, size_t len);
/** Store both. Longer values are cut to the maximum above. */
void settings_set_wifi_credentials(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif
