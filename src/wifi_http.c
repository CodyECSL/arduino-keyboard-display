#include "wifi_http.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

#include <lvgl.h>

#include "bmp.h"
#include "esp_bsp.h"
#include "wifi_debug.h"

static const char *TAG = "wifi_http";

static httpd_handle_t s_server;
static volatile bool s_log_busy;
static volatile bool s_stopping; /* set by wifi_http_stop so log_task ends before httpd_stop */
static volatile int s_log_fd = -1;       /* socket of the current log client; -1 when none */
static volatile bool s_log_gone;         /* httpd closed that socket; the fd number may be reused */

/* Runs when httpd deletes a session. With a close_fn set, httpd no longer closes the socket itself. */
static void on_session_close(httpd_handle_t hd, int sockfd)
{
    (void)hd;
    if (sockfd == s_log_fd) {
        s_log_gone = true;
    }
    close(sockfd);
}

static void send_status(httpd_req_t *req, const char *status, const char *text)
{
    httpd_resp_set_status(req, status);
    httpd_resp_sendstr(req, text);
}

/* True when the peer has closed the connection. Looks at the socket without taking data. */
static bool client_gone(httpd_req_t *req)
{
    char c;
    if (s_log_gone) {
        return true;
    }
    int r = recv(httpd_req_to_sockfd(req), &c, 1, MSG_PEEK | MSG_DONTWAIT);
    return s_log_gone || r == 0 || (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
}

/* One task per log client. The httpd task must stay free so /shot still answers. */
static void log_task(void *arg)
{
    httpd_req_t *req = arg;
    char chunk[256];
    uint64_t pos = 0; /* from the oldest text still in the ring */
    esp_err_t err = ESP_OK;
    httpd_resp_set_type(req, "text/plain");
    while (err == ESP_OK && !s_stopping && !s_log_gone) {
        size_t skipped = 0;
        size_t n = wifi_debug_log_read(&pos, chunk, sizeof(chunk), &skipped);
        if (s_log_gone) {
            break;
        }
        if (skipped) {
            char note[48];
            int len = snprintf(note, sizeof(note), "[%u bytes of log lost]\n", (unsigned)skipped);
            err = httpd_resp_send_chunk(req, note, len);
        }
        if (n > 0 && err == ESP_OK && !s_log_gone) {
            err = httpd_resp_send_chunk(req, chunk, (ssize_t)n);
        } else if (n == 0) {
            if (client_gone(req)) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    if (!s_log_gone) {
        httpd_resp_send_chunk(req, NULL, 0);
    }
    httpd_req_async_handler_complete(req);
    s_log_fd = -1;
    s_log_busy = false;
    vTaskDelete(NULL);
}

static esp_err_t log_get(httpd_req_t *req)
{
    if (s_log_busy) {
        send_status(req, "503 Service Unavailable", "one log client at a time\n");
        return ESP_OK;
    }
    httpd_req_t *async;
    if (httpd_req_async_handler_begin(req, &async) != ESP_OK) {
        send_status(req, HTTPD_500, "no async handler\n");
        return ESP_OK;
    }
    s_log_gone = false;
    s_log_fd = httpd_req_to_sockfd(async);
    s_log_busy = true;
    if (xTaskCreate(log_task, "log_http", 4096, async, 5, NULL) != pdPASS) {
        s_log_busy = false;
        s_log_fd = -1;
        send_status(async, HTTPD_500, "no task\n");
        httpd_req_async_handler_complete(async);
    }
    return ESP_OK;
}

#define SHOT_ROWS_PER_CHUNK 8

static esp_err_t shot_get(httpd_req_t *req)
{
    /* Own buffer: lv_snapshot_take would spin in LV_ASSERT_MALLOC (lock held) when memory is short. */
    if (!bsp_display_lock(2000)) {
        send_status(req, "503 Service Unavailable", "display busy\n");
        return ESP_OK;
    }
    uint32_t size = lv_snapshot_buf_size_needed(lv_scr_act(), LV_IMG_CF_TRUE_COLOR);
    bsp_display_unlock();
    void *buf = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        send_status(req, "503 Service Unavailable", "no memory for the snapshot\n");
        return ESP_OK;
    }
    lv_img_dsc_t dsc;
    if (!bsp_display_lock(2000)) {
        free(buf);
        send_status(req, "503 Service Unavailable", "display busy\n");
        return ESP_OK;
    }
    lv_res_t res = lv_snapshot_take_to_buf(lv_scr_act(), LV_IMG_CF_TRUE_COLOR, &dsc, buf, size);
    bsp_display_unlock();
    if (res != LV_RES_OK) {
        free(buf);
        send_status(req, "503 Service Unavailable", "snapshot failed\n");
        return ESP_OK;
    }

    int w = dsc.header.w;
    int h = dsc.header.h;
    size_t row_bytes = bmp_row_bytes(w);
    uint8_t *out = malloc(row_bytes * SHOT_ROWS_PER_CHUNK);
    if (!out) {
        free(buf);
        send_status(req, "503 Service Unavailable", "no memory\n");
        return ESP_OK;
    }
    uint8_t head[BMP_HEADER_SIZE];
    bmp_header(head, w, h);
    httpd_resp_set_type(req, "image/bmp");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    esp_err_t err = httpd_resp_send_chunk(req, (const char *)head, BMP_HEADER_SIZE);
    for (int y = 0; y < h && err == ESP_OK; y += SHOT_ROWS_PER_CHUNK) {
        int rows = h - y < SHOT_ROWS_PER_CHUNK ? h - y : SHOT_ROWS_PER_CHUNK;
        for (int r = 0; r < rows; r++) {
            bmp_row_from_rgb565(dsc.data + (size_t)(y + r) * w * 2, w, LV_COLOR_16_SWAP, out + r * row_bytes);
        }
        err = httpd_resp_send_chunk(req, (const char *)out, (ssize_t)(row_bytes * rows));
    }
    if (err == ESP_OK) {
        err = httpd_resp_send_chunk(req, NULL, 0);
    }
    free(out);
    free(buf);
    return err == ESP_OK ? ESP_OK : ESP_FAIL; /* a failed send mid-stream must close the connection */
}

void wifi_http_start(void)
{
    if (s_server) {
        /* Left running by a stop that timed out: re-arm it and re-add the mDNS service. */
        s_stopping = false;
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        return;
    }
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 4;
    cfg.lru_purge_enable = true;
    cfg.stack_size = 6144;
    cfg.send_wait_timeout = 1;
    cfg.close_fn = on_session_close;
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        s_server = NULL;
        return;
    }
    s_stopping = false;
    static const httpd_uri_t log_uri = {.uri = "/log", .method = HTTP_GET, .handler = log_get};
    httpd_register_uri_handler(s_server, &log_uri);
    static const httpd_uri_t shot_uri = {.uri = "/shot", .method = HTTP_GET, .handler = shot_get};
    httpd_register_uri_handler(s_server, &shot_uri);
    mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    ESP_LOGI(TAG, "http server started");
}

void wifi_http_stop(void)
{
    if (!s_server) {
        return;
    }
    /* Let a running log_task finish its request first, so it never touches a closed server. */
    s_stopping = true;
    for (int i = 0; i < 125 && s_log_busy; i++) {
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_log_busy) {
        ESP_LOGE(TAG, "log client still active, leaving the http server running");
        return;
    }
    httpd_stop(s_server);
    s_server = NULL;
}
