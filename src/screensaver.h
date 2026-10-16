#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SAVER_NAME_MAX 64 /* longest video file name, with the terminating zero */

/* Call once, after the display is up and ui_create() has run (LVGL lock held). */
void screensaver_init(void);
/* File names (*.mjpeg) in /mjpeg/ on the SD card, sorted. Returns how many were stored. */
int screensaver_list_files(char names[][SAVER_NAME_MAX], int max);
/* Name of the video in use, or "" when there is none. */
const char *screensaver_current_file(void);
/* Switch to another video and remember the choice. Only while the screensaver is not running
 * (LVGL context). Returns false and keeps the old video if the file cannot be opened. */
bool screensaver_select_file(const char *name);
/* Start the video (LVGL context). Returns false when there is no video: the caller then blanks the screen. */
bool screensaver_start(void);
/* Stop the video and repaint the UI (LVGL context). */
void screensaver_stop(void);

#ifdef __cplusplus
}
#endif
