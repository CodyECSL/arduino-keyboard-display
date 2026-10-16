#include "screensaver.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <lvgl.h>
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdmmc_cmd.h"

#include "esp_jpeg_dec.h"
#include "esp_bsp.h"
#include "lv_port.h"
#include "saver_logic.h"
#include "settings.h"

#define SD_MOUNT "/sdcard"
#define VIDEO_DIR SD_MOUNT "/mjpeg"      /* raw back-to-back 480x320 JPEGs, like the stock files */
#define DEFAULT_VIDEO "SAVER.mjpeg"      /* used when no choice is stored or the stored file is gone */
#define SD_HOST SPI3_HOST                      /* SPI2 is the LCD */
#define SD_PIN_CS 10
#define SD_PIN_MOSI 11
#define SD_PIN_SCLK 12
#define SD_PIN_MISO 13
#define LW 480 /* video size, landscape */
#define LH 320
#define PW LH /* panel size, portrait: the frame buffer is stored rotated 270 degrees clockwise */
#define PH LW
#define MAX_FRAME (64 * 1024)
#define STREAM_BUF (96 * 1024)
#define READ_STEP (32 * 1024)
#define TARGET_FPS 30 /* a raw MJPEG file has no timing: encode at this rate (tools/make_screensaver.py) */
#define SLOTS 3       /* read-ahead frames */
#define FBS 3         /* decoded frames: one being decoded, one being sent, one just sent (kept for the fade) */
#define FADE_MS 5000  /* dissolve from the last frame of a video into the next video */
#define PL_MAX 16     /* videos in the play list */
#define REFR_PERIOD_PAUSED_MS 0x7FFFFFFF

static const char *TAG = "saver";

static uint16_t *s_fbs[FBS]; /* PW x PH each, PSRAM */
static uint16_t *s_still;    /* PSRAM: last frame of the old video while the fade runs */
static uint8_t *s_stream;    /* PSRAM: file bytes read ahead */
static uint8_t *s_slots[SLOTS]; /* PSRAM: compressed frames waiting for the decoder */
static uint8_t *s_jpg;       /* internal RAM: the frame being decoded */

typedef struct {
    uint8_t slot;
    uint32_t len;
    bool first; /* first frame of a new video: the sender starts the fade here */
} filled_t;
typedef struct {
    uint8_t fb;
    bool first;
} ready_t;
static QueueHandle_t s_q_slot_free, s_q_slot_filled, s_q_fb_free, s_q_fb_ready;

static size_t s_have, s_pos;
static FILE *s_file;
static SemaphoreHandle_t s_file_mx; /* guards s_file, s_have and s_pos against a file switch */
static char s_cur[SAVER_NAME_MAX];  /* name of the open video; empty when none */
static bool s_mounted;
static char s_err[96]; /* empty when ready */

/* Play list: the chosen videos that are on the card. Built in screensaver_start(); the reader task
 * moves on to the next one at the end of a file. Only used when it holds two or more videos. */
static char (*s_pl)[SAVER_NAME_MAX]; /* PSRAM, PL_MAX names */
static int s_pl_count, s_pl_cur;
static bool s_pl_on, s_pl_random;
static bool s_eof;   /* next_frame() hit the end of the file */
static bool s_first; /* the next frame cut is the first of a new video */

static volatile bool s_run;
static bool s_ready; /* the video opened; start() may run it */

/* Find the next frame (SOI ... EOI) in the read-ahead buffer, reading more of the file as needed.
 * A 0xFF in JPEG entropy data is always followed by 0x00 or an RSTn marker, so FF D9 is the end. */
static bool next_frame(size_t *start, size_t *len)
{
    for (;;) {
        size_t i = s_pos;
        while (i + 1 < s_have) {
            const uint8_t *p = (const uint8_t *)memchr(s_stream + i, 0xFF, s_have - i - 1);
            if (!p) {
                i = s_have;
                break;
            }
            i = p - s_stream;
            if (i > s_pos + 2 && p[1] == 0xD9) {
                *start = s_pos;
                *len = i + 2 - s_pos;
                s_pos = i + 2;
                return true;
            }
            i++;
        }
        /* Need more data: drop what was consumed, then read. */
        if (s_pos > 0) {
            memmove(s_stream, s_stream + s_pos, s_have - s_pos);
            s_have -= s_pos;
            s_pos = 0;
        }
        if (s_have + READ_STEP > STREAM_BUF) {
            s_have = 0; /* no frame end within the whole buffer: bad data, resync */
            continue;
        }
        size_t got = fread(s_stream + s_have, 1, READ_STEP, s_file);
        if (got == 0) {
            s_eof = true;
            fseek(s_file, 0, SEEK_SET); /* loop */
            s_have = s_pos = 0;
            return false;
        }
        s_have += got;
    }
}

/* End of the video: open the next one of the play list. The frames of the old video that are still in
 * the pipeline play to the end. Called with s_file_mx held. If the file cannot be opened, the old one loops. */
static void open_next_video(void)
{
    int next = saver_next_index(s_pl_count, s_pl_cur, s_pl_random, esp_random());
    if (next < 0 || next == s_pl_cur) {
        return;
    }
    char path[sizeof(VIDEO_DIR) + SAVER_NAME_MAX + 1];
    snprintf(path, sizeof(path), VIDEO_DIR "/%s", s_pl[next]);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return;
    }
    fclose(s_file);
    s_file = f;
    s_have = s_pos = 0;
    s_pl_cur = next;
    strcpy(s_cur, s_pl[next]);
    s_first = true;
    ESP_LOGI(TAG, "next video: %s", s_cur);
}

/* Stage 1: read the file ahead and cut it into frames. */
static void reader_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint8_t slot;
        if (!s_run || xQueueReceive(s_q_slot_free, &slot, pdMS_TO_TICKS(50)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(s_run ? 1 : 50));
            continue;
        }
        size_t start = 0, len = 0;
        xSemaphoreTake(s_file_mx, portMAX_DELAY);
        s_eof = false;
        bool got_frame = s_file && next_frame(&start, &len) && len <= MAX_FRAME;
        bool first = false;
        if (got_frame) {
            memcpy(s_slots[slot], s_stream + start, len);
            first = s_first;
            s_first = false;
        } else if (s_eof && s_pl_on) {
            open_next_video();
        }
        xSemaphoreGive(s_file_mx);
        if (got_frame) {
            filled_t f = {slot, (uint32_t)len, first};
            xQueueSend(s_q_slot_filled, &f, portMAX_DELAY);
        } else {
            xQueueSend(s_q_slot_free, &slot, 0); /* looped or bad frame: give the slot back */
        }
    }
}

/* Stage 2: decode a frame into the next free frame buffer, rotated for the panel. */
static void decoder_task(void *arg)
{
    (void)arg;
    jpeg_dec_config_t cfg = DEFAULT_JPEG_DEC_CONFIG();
    cfg.output_type = JPEG_PIXEL_FORMAT_RGB565_BE; /* what the panel wants; same as the LVGL path */
    cfg.rotate = JPEG_ROTATE_270D;
    jpeg_dec_handle_t dec = NULL;
    if (jpeg_dec_open(&cfg, &dec) != JPEG_ERR_OK) {
        ESP_LOGE(TAG, "jpeg decoder open failed");
        vTaskDelete(NULL);
    }
    for (;;) {
        filled_t f;
        if (!s_run || xQueueReceive(s_q_slot_filled, &f, pdMS_TO_TICKS(50)) != pdTRUE) {
            vTaskDelay(pdMS_TO_TICKS(s_run ? 1 : 50));
            continue;
        }
        uint8_t fb;
        while (xQueueReceive(s_q_fb_free, &fb, pdMS_TO_TICKS(50)) != pdTRUE) {
            if (!s_run) {
                break;
            }
        }
        if (!s_run) {
            xQueueSend(s_q_slot_free, &f.slot, 0);
            continue;
        }
        memcpy(s_jpg, s_slots[f.slot], f.len);
        xQueueSend(s_q_slot_free, &f.slot, 0);

        /* The decoder rotates 270 degrees clockwise: landscape (x, y) -> frame (y, LW-1-x). */
        jpeg_dec_io_t io = {};
        io.inbuf = s_jpg;
        io.inbuf_len = (int)f.len;
        io.outbuf = (uint8_t *)s_fbs[fb];
        jpeg_dec_header_info_t info;
        jpeg_error_t err = jpeg_dec_parse_header(dec, &io, &info);
        /* The size is reported before or after the rotation; accept both. */
        bool ok = err == JPEG_ERR_OK &&
                  ((info.width == LW && info.height == LH) || (info.width == LH && info.height == LW));
        if (ok) {
            err = jpeg_dec_process(dec, &io);
            ok = err == JPEG_ERR_OK;
        }
        if (ok) {
            ready_t r = {fb, f.first};
            xQueueSend(s_q_fb_ready, &r, portMAX_DELAY);
        } else {
            ESP_LOGE(TAG, "frame decode failed, jpeg error %d", (int)err);
            xQueueSend(s_q_fb_free, &fb, 0);
        }
    }
}

/* Stage 3: send decoded frames to the panel at the frame cap. A sent frame stays in `held` until the next
 * one is here, so that the last frame of a video is still there when the next video starts. Then the
 * sender dissolves from that frame into the live video over FADE_MS (the mix is written over the new frame). */
static void sender_task(void *arg)
{
    (void)arg;
    const int NONE = -1;
    int held = NONE;
    bool was_running = false;
    int64_t t0 = 0, fade_t0 = 0;
    bool fading = false;
    uint32_t shown = 0;

    for (;;) {
        if (!s_run) {
            if (held != NONE) {
                uint8_t h = (uint8_t)held;
                xQueueSend(s_q_fb_free, &h, 0);
                held = NONE;
            }
            was_running = false;
            fading = false;
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        ready_t r;
        if (xQueueReceive(s_q_fb_ready, &r, pdMS_TO_TICKS(50)) != pdTRUE) {
            continue;
        }
        if (!was_running) {
            was_running = true;
            shown = 0;
            t0 = esp_timer_get_time();
        }

        if (r.first && held != NONE && s_still) {
            memcpy(s_still, s_fbs[held], PW * PH * sizeof(uint16_t));
            fading = true;
            fade_t0 = esp_timer_get_time();
        }
        if (fading) {
            int64_t ms = (esp_timer_get_time() - fade_t0) / 1000;
            if (ms >= FADE_MS) {
                fading = false;
            } else {
                saver_blend565be(s_fbs[r.fb], s_still, PW * PH, (int)(ms * SAVER_BLEND_MAX / FADE_MS));
            }
        }

        if (bsp_display_lock(200)) {
            if (s_run) {
                lvgl_port_blit(lv_disp_get_default(), s_fbs[r.fb], PW, PH, 1);
            }
            bsp_display_unlock();
        }
        if (held != NONE) {
            uint8_t h = (uint8_t)held;
            xQueueSend(s_q_fb_free, &h, 0);
        }
        held = r.fb;
        shown++;

        int64_t due = t0 + (int64_t)shown * 1000000 / TARGET_FPS;
        int64_t now = esp_timer_get_time();
        if (due > now) {
            vTaskDelay(pdMS_TO_TICKS((due - now) / 1000));
        } else if (fading && (shown % 30) == 0) {
            ESP_LOGW(TAG, "fade is late by %d ms", (int)((now - due) / 1000)); /* the blend costs more than a frame */
        }
    }
}

/* Mount the SD card and allocate the buffers. The video file is opened separately. */
static bool mount_and_alloc(void)
{
    spi_bus_config_t bus = {};
    bus.mosi_io_num = SD_PIN_MOSI;
    bus.miso_io_num = SD_PIN_MISO;
    bus.sclk_io_num = SD_PIN_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = 16 * 1024;
    esp_err_t err = spi_bus_initialize(SD_HOST, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        snprintf(s_err, sizeof(s_err), "SD spi bus: %s", esp_err_to_name(err));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_HOST;
    sdspi_device_config_t slot = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot.gpio_cs = (gpio_num_t)SD_PIN_CS;
    slot.host_id = SD_HOST;
    esp_vfs_fat_sdmmc_mount_config_t mc = {};
    mc.format_if_mount_failed = false;
    mc.max_files = 3;
    mc.allocation_unit_size = 16 * 1024;
    sdmmc_card_t *card = NULL;
    err = esp_vfs_fat_sdspi_mount(SD_MOUNT, &host, &slot, &mc, &card);
    if (err != ESP_OK) {
        snprintf(s_err, sizeof(s_err), "SD mount: %s", esp_err_to_name(err));
        return false;
    }
    s_mounted = true;

    s_stream = (uint8_t *)heap_caps_malloc(STREAM_BUF, MALLOC_CAP_SPIRAM);
    s_jpg = (uint8_t *)heap_caps_malloc(MAX_FRAME, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!s_jpg) {
        s_jpg = (uint8_t *)heap_caps_malloc(MAX_FRAME, MALLOC_CAP_SPIRAM); /* internal RAM taken (Wi-Fi debug): slower, but works */
    }
    bool ok = s_stream && s_jpg;
    for (int i = 0; i < FBS; i++) {
        s_fbs[i] = (uint16_t *)heap_caps_aligned_alloc(16, PW * PH * sizeof(uint16_t), MALLOC_CAP_SPIRAM); /* 16-byte aligned for the decoder */
        ok = ok && s_fbs[i];
    }
    for (int i = 0; i < SLOTS; i++) {
        s_slots[i] = (uint8_t *)heap_caps_malloc(MAX_FRAME, MALLOC_CAP_SPIRAM);
        ok = ok && s_slots[i];
    }
    s_still = (uint16_t *)heap_caps_malloc(PW * PH * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    s_pl = (char(*)[SAVER_NAME_MAX])heap_caps_malloc(PL_MAX * SAVER_NAME_MAX, MALLOC_CAP_SPIRAM);
    ok = ok && s_still && s_pl;
    if (!ok) {
        snprintf(s_err, sizeof(s_err), "out of memory");
        return false;
    }
    for (int i = 0; i < FBS; i++) {
        memset(s_fbs[i], 0, PW * PH * sizeof(uint16_t));
    }
    ESP_LOGI(TAG, "SD ready, jpg buffer in %s", esp_ptr_internal(s_jpg) ? "internal RAM" : "PSRAM");
    return true;
}

static bool is_video_name(const char *name)
{
    size_t n = strlen(name);
    return name[0] != '.' /* skips macOS "._" files */ && n >= 7 && n < SAVER_NAME_MAX &&
           strcasecmp(name + n - 6, ".mjpeg") == 0;
}

static int name_cmp(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

int screensaver_list_files(char names[][SAVER_NAME_MAX], int max)
{
    if (!s_mounted) {
        return 0;
    }
    DIR *dir = opendir(VIDEO_DIR);
    if (!dir) {
        return 0;
    }
    int count = 0;
    struct dirent *e;
    while (count < max && (e = readdir(dir)) != NULL) {
        if (e->d_type != DT_DIR && is_video_name(e->d_name)) {
            strcpy(names[count++], e->d_name);
        }
    }
    closedir(dir);
    qsort(names, count, SAVER_NAME_MAX, name_cmp);
    return count;
}

const char *screensaver_current_file(void)
{
    return s_cur;
}

/* Open `name` and make it the video. The caller keeps the old one if this fails. */
static bool switch_file(const char *name)
{
    if (!s_mounted || !is_video_name(name)) {
        return false;
    }
    char path[sizeof(VIDEO_DIR) + SAVER_NAME_MAX + 1];
    snprintf(path, sizeof(path), VIDEO_DIR "/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        return false;
    }
    xSemaphoreTake(s_file_mx, portMAX_DELAY);
    FILE *old = s_file;
    s_file = f;
    s_have = s_pos = 0;
    s_first = false;
    strcpy(s_cur, name);
    xSemaphoreGive(s_file_mx);
    if (old) {
        fclose(old);
    }
    /* Return frames of the old video to their free queues. */
    filled_t fl;
    while (xQueueReceive(s_q_slot_filled, &fl, 0) == pdTRUE) {
        xQueueSend(s_q_slot_free, &fl.slot, 0);
    }
    ready_t r;
    while (xQueueReceive(s_q_fb_ready, &r, 0) == pdTRUE) {
        xQueueSend(s_q_fb_free, &r.fb, 0);
    }
    s_ready = true;
    ESP_LOGI(TAG, "video: %s", name);
    return true;
}

bool screensaver_select_file(const char *name)
{
    if (s_run || s_q_slot_free == NULL || !switch_file(name)) {
        return false;
    }
    settings_set_saver_file(name);
    return true;
}

void screensaver_init(void)
{
    s_file_mx = xSemaphoreCreateMutex();
    configASSERT(s_file_mx);
    if (!mount_and_alloc()) {
        ESP_LOGW(TAG, "screensaver off: %s", s_err); /* the idle screen goes dark instead */
        return;
    }
    s_q_slot_free = xQueueCreate(SLOTS, sizeof(uint8_t));
    s_q_slot_filled = xQueueCreate(SLOTS, sizeof(filled_t));
    s_q_fb_free = xQueueCreate(FBS, sizeof(uint8_t));
    s_q_fb_ready = xQueueCreate(FBS, sizeof(ready_t));
    for (uint8_t i = 0; i < SLOTS; i++) {
        xQueueSend(s_q_slot_free, &i, 0);
    }
    for (uint8_t i = 0; i < FBS; i++) {
        xQueueSend(s_q_fb_free, &i, 0);
    }
    /* Decoder alone on core 1; the file reader and the panel sender share core 0. */
    xTaskCreatePinnedToCore(reader_task, "saver_rd", 4096, NULL, 2, NULL, 0);
    xTaskCreatePinnedToCore(decoder_task, "saver_dec", 8192, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(sender_task, "saver_tx", 4096, NULL, 3, NULL, 0);

    /* Stored choice first, then the default name, then any video on the card. */
    char name[SAVER_NAME_MAX];
    static char all[16][SAVER_NAME_MAX];
    if (!(settings_get_saver_file(name, sizeof(name)) && switch_file(name)) && !switch_file(DEFAULT_VIDEO)) {
        int n = screensaver_list_files(all, 16);
        if (n == 0 || !switch_file(all[0])) {
            ESP_LOGW(TAG, "no video in " VIDEO_DIR "; the idle screen goes dark instead");
        }
    }
}

/* Build the play list from the chosen videos that are on the card and pick where to begin.
 * One chosen video plays alone (looping); none chosen leaves the single-video choice as it is. */
static void build_playlist(void)
{
    s_pl_on = false;
    s_pl_count = 0;
    if (!settings_get_saver_multi() || !s_pl) {
        return;
    }
    char *set = (char *)heap_caps_malloc(SETTINGS_SAVER_SET_MAX, MALLOC_CAP_SPIRAM);
    if (!set) {
        return;
    }
    settings_get_saver_set(set, SETTINGS_SAVER_SET_MAX);
    DIR *dir = s_mounted ? opendir(VIDEO_DIR) : NULL;
    struct dirent *e;
    while (dir && s_pl_count < PL_MAX && (e = readdir(dir)) != NULL) {
        if (e->d_type != DT_DIR && is_video_name(e->d_name) && saver_set_has(set, e->d_name)) {
            strcpy(s_pl[s_pl_count++], e->d_name);
        }
    }
    if (dir) {
        closedir(dir);
    }
    heap_caps_free(set);
    qsort(s_pl, s_pl_count, SAVER_NAME_MAX, name_cmp);

    s_pl_random = settings_get_saver_random();
    s_pl_cur = -1;
    for (int i = 0; i < s_pl_count; i++) {
        if (strcmp(s_pl[i], s_cur) == 0) {
            s_pl_cur = i;
        }
    }
    if (s_pl_count > 0 && s_pl_cur < 0) { /* the video in use is not chosen: begin with a chosen one */
        int first = saver_next_index(s_pl_count, -1, s_pl_random, esp_random());
        if (switch_file(s_pl[first])) {
            s_pl_cur = first;
        }
    }
    s_pl_on = s_pl_count >= 2 && s_pl_cur >= 0;
    ESP_LOGI(TAG, "play list: %d video(s), %s", s_pl_count, s_pl_random ? "random" : "in order");
}

bool screensaver_start(void)
{
    if (!s_ready) {
        return false;
    }
    build_playlist();
    /* LVGL must not draw over the video. LVGL resumes its refresh timer on every invalidation, so
     * pausing is not enough: drop the flushes, and stretch the period so nothing is rendered either. */
    lvgl_port_set_flush_suppressed(true);
    lv_timer_set_period(lv_disp_get_default()->refr_timer, REFR_PERIOD_PAUSED_MS);
    s_run = true;
    return true;
}

void screensaver_stop(void)
{
    if (!s_ready) {
        return;
    }
    s_run = false;
    lv_disp_t *disp = lv_disp_get_default();
    lvgl_port_set_flush_suppressed(false);
    lv_timer_set_period(disp->refr_timer, LV_DISP_DEF_REFR_PERIOD);
    lv_timer_resume(disp->refr_timer);
    lv_obj_invalidate(lv_scr_act()); /* repaint the UI over the last video frame */
    lv_obj_invalidate(lv_layer_top());
}
