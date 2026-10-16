#include "wifi_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mdns.h"

#include "log_ring.h"
#include "settings.h"
#include "wifi_http.h"

static const char *TAG = "wifi_debug";

#define RING_SIZE 4096
#define HOSTNAME "macropad"
#define SCAN_MAX 16

/* ---- log hook ---------------------------------------------------------------------------------------------- */

static char s_ring_store[RING_SIZE];
static log_ring_t s_ring;
static portMUX_TYPE s_ring_lock = portMUX_INITIALIZER_UNLOCKED;
static vprintf_like_t s_old_vprintf;

static int hook_vprintf(const char *fmt, va_list args)
{
    char line[128]; /* long lines are cut in the ring; the console copy stays whole */
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);
    if (n > 0) {
        size_t len = (size_t)n < sizeof(line) ? (size_t)n : sizeof(line) - 1;
        if ((size_t)n >= sizeof(line)) {
            line[len - 1] = '\n'; /* a cut line must not merge with the next one in /log */
        }
        portENTER_CRITICAL(&s_ring_lock);
        log_ring_write(&s_ring, line, len);
        portEXIT_CRITICAL(&s_ring_lock);
    }
    return s_old_vprintf ? s_old_vprintf(fmt, args) : vprintf(fmt, args);
}

size_t wifi_debug_log_read(uint64_t *pos, char *out, size_t max, size_t *skipped)
{
    portENTER_CRITICAL(&s_ring_lock);
    size_t n = log_ring_read(&s_ring, pos, out, max, skipped);
    portEXIT_CRITICAL(&s_ring_lock);
    return n;
}

/* ---- status ------------------------------------------------------------------------------------------------ */

static wifi_debug_status_t s_status;
static portMUX_TYPE s_status_lock = portMUX_INITIALIZER_UNLOCKED;

static void set_status(wifi_debug_state_t state, const char *ip, const char *reason)
{
    portENTER_CRITICAL(&s_status_lock);
    s_status.state = state;
    snprintf(s_status.ip, sizeof(s_status.ip), "%s", ip ? ip : "");
    snprintf(s_status.reason, sizeof(s_status.reason), "%s", reason ? reason : "");
    portEXIT_CRITICAL(&s_status_lock);
}

void wifi_debug_get_status(wifi_debug_status_t *out)
{
    portENTER_CRITICAL(&s_status_lock);
    *out = s_status;
    portEXIT_CRITICAL(&s_status_lock);
}

static void reason_text(int reason, char *out, size_t len)
{
    switch (reason) {
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        snprintf(out, len, "wrong password");
        break;
    case WIFI_REASON_NO_AP_FOUND:
        snprintf(out, len, "network not found");
        break;
    default:
        snprintf(out, len, "error %d", reason);
        break;
    }
}

/* ---- station ----------------------------------------------------------------------------------------------- */

static volatile bool s_running; /* changed by the wifi_ctl worker only */
static esp_netif_t *s_netif;
static esp_timer_handle_t s_retry_timer;
static int s_retry;
static volatile bool s_scan_running;
static wifi_debug_ap_t s_scan[SCAN_MAX];
static int s_scan_count;

static bool has_network(void)
{
    char ssid[SETTINGS_WIFI_SSID_MAX + 1];
    return settings_get_wifi_ssid(ssid, sizeof(ssid));
}

static void apply_config(void)
{
    wifi_config_t cfg = {0};
    char ssid[SETTINGS_WIFI_SSID_MAX + 1];
    char pass[SETTINGS_WIFI_PASS_MAX + 1];
    settings_get_wifi_ssid(ssid, sizeof(ssid));
    settings_get_wifi_pass(pass, sizeof(pass));
    strncpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid) - 1);
    strncpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password) - 1);
    cfg.sta.threshold.authmode = pass[0] ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static void retry_cb(void *arg)
{
    (void)arg;
    if (s_running && has_network()) {
        set_status(WIFI_DEBUG_CONNECTING, NULL, NULL);
        esp_wifi_connect(); /* an error here only means a connect is already running */
    }
}

static void schedule_retry(void)
{
    uint64_t delay_ms = 1000u << (s_retry < 5 ? s_retry : 5); /* 1 s .. 32 s */
    s_retry++;
    esp_timer_stop(s_retry_timer);
    esp_timer_start_once(s_retry_timer, delay_ms * 1000u);
}

static void scan_done(void)
{
    static wifi_ap_record_t recs[SCAN_MAX]; /* static: only the event task runs this, and it has a small stack */
    uint16_t n = SCAN_MAX;
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        n = 0;
    }
    int count = 0;
    for (int i = 0; i < n; i++) {
        bool dup = false;
        for (int j = 0; j < count; j++) {
            dup |= strcmp(s_scan[j].ssid, (const char *)recs[i].ssid) == 0;
        }
        if (dup || recs[i].ssid[0] == '\0') {
            continue; /* the list is sorted by signal, so the first entry of a name is the strongest */
        }
        snprintf(s_scan[count].ssid, sizeof(s_scan[count].ssid), "%s", (const char *)recs[i].ssid);
        s_scan[count].rssi = recs[i].rssi;
        count++;
    }
    s_scan_count = count;
    s_scan_running = false;
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (has_network()) {
                set_status(WIFI_DEBUG_CONNECTING, NULL, NULL);
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *d = data;
            if (d->reason == WIFI_REASON_ASSOC_LEAVE) {
                break; /* our own esp_wifi_disconnect(); the connect timer reconnects */
            }
            char why[24];
            reason_text(d->reason, why, sizeof(why));
            ESP_LOGW(TAG, "disconnected: %s (%d)", why, d->reason);
            if (s_running && has_network()) {
                set_status(WIFI_DEBUG_FAILED, NULL, why);
                schedule_retry();
            }
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            scan_done();
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = data;
        char ip[16];
        snprintf(ip, sizeof(ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_retry = 0;
        set_status(WIFI_DEBUG_CONNECTED, ip, NULL);
        ESP_LOGI(TAG, "connected, ip %s, host %s.local", ip, HOSTNAME);
        ESP_LOGI(TAG, "free internal %u, min internal %u, free PSRAM %u",
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
}

/* Undo the steps of start() that succeeded, in reverse order. */
static void undo_start(bool wifi_inited)
{
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event);
    if (wifi_inited) {
        esp_wifi_deinit();
    }
    esp_netif_destroy_default_wifi(s_netif);
    s_netif = NULL;
    esp_log_set_vprintf(s_old_vprintf ? s_old_vprintf : vprintf);
}

static void start_failed(const char *what, esp_err_t err, bool wifi_inited)
{
    ESP_LOGE(TAG, "%s failed: %s", what, esp_err_to_name(err));
    undo_start(wifi_inited);
    char reason[24];
    snprintf(reason, sizeof(reason), "error %d", (int)err);
    set_status(WIFI_DEBUG_FAILED, NULL, reason);
}

static void start(void)
{
    if (s_running) {
        return;
    }
    log_ring_init(&s_ring, s_ring_store, sizeof(s_ring_store));
    s_old_vprintf = esp_log_set_vprintf(hook_vprintf);

    esp_netif_init();
    esp_event_loop_create_default(); /* returns ESP_ERR_INVALID_STATE if it exists; fine */
    s_netif = esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init);
    if (err != ESP_OK) {
        start_failed("esp_wifi_init", err, false);
        return;
    }
    esp_wifi_set_storage(WIFI_STORAGE_RAM); /* credentials live in our NVS keys only */
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);
    esp_wifi_set_mode(WIFI_MODE_STA);

    if (!s_retry_timer) {
        const esp_timer_create_args_t args = {.callback = retry_cb, .name = "wifi_retry"};
        err = esp_timer_create(&args, &s_retry_timer);
        if (err != ESP_OK) {
            start_failed("esp_timer_create", err, true);
            return;
        }
    }
    s_retry = 0;
    s_scan_running = false;
    s_scan_count = 0;
    set_status(WIFI_DEBUG_OFF, NULL, has_network() ? NULL : "no network set");
    if (has_network()) {
        apply_config();
    }
    s_running = true; /* the STA_START event may arrive as soon as esp_wifi_start() runs */
    err = esp_wifi_start();
    if (err != ESP_OK) {
        s_running = false;
        start_failed("esp_wifi_start", err, true);
        return;
    }
    esp_wifi_set_ps(WIFI_PS_NONE); /* modem sleep delays every TCP ack; /shot sends 460 KB */

    err = mdns_init();
    if (err != ESP_OK) {
        s_running = false;
        esp_wifi_stop();
        start_failed("mdns_init", err, true);
        return;
    }
    mdns_hostname_set(HOSTNAME);
    mdns_instance_name_set("Macropad");
    wifi_http_start();
    ESP_LOGI(TAG, "started");
}

static void stop(void)
{
    if (!s_running) {
        return;
    }
    s_running = false;
    s_scan_running = false;
    s_scan_count = 0;
    wifi_http_stop();
    esp_timer_stop(s_retry_timer);
    mdns_free();
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event);
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event);
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_netif_destroy_default_wifi(s_netif);
    s_netif = NULL;
    esp_log_set_vprintf(s_old_vprintf ? s_old_vprintf : vprintf);
    set_status(WIFI_DEBUG_OFF, NULL, NULL);
    ESP_LOGI(TAG, "stopped, free internal %u", (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

/* ---- worker ------------------------------------------------------------------------------------------------ */

/* start(), stop() and connect run in one task: the callers are the LVGL task (small stack, display lock held)
 * and the main task, and stopping the http server can block for seconds. */
typedef enum { CMD_START, CMD_STOP, CMD_CONNECT, CMD_RESTART } cmd_t;
static volatile bool s_boot_pending; /* the boot START has not finished yet */

static QueueHandle_t s_cmd_queue;

static void do_connect(void)
{
    if (!s_running) {
        return;
    }
    s_retry = 0;
    apply_config();
    set_status(WIFI_DEBUG_CONNECTING, NULL, NULL);
    esp_timer_stop(s_retry_timer);
    esp_wifi_disconnect();
    /* The disconnect event for this call (reason ASSOC_LEAVE) is ignored. The 300 ms timer does the connect. */
    esp_timer_start_once(s_retry_timer, 300 * 1000);
}

static void do_stop(void)
{
    if (!s_running) {
        set_status(WIFI_DEBUG_OFF, NULL, NULL); /* a failed start() leaves FAILED behind */
        return;
    }
    stop();
}

static void ctl_task(void *arg)
{
    (void)arg;
    cmd_t cmd;
    for (;;) {
        if (xQueueReceive(s_cmd_queue, &cmd, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        switch (cmd) {
        case CMD_START:
            start();
            s_boot_pending = false;
            break;
        case CMD_STOP:
            do_stop();
            break;
        case CMD_CONNECT:
            do_connect();
            break;
        case CMD_RESTART:
            /* The Wi-Fi driver needs about 80 KB of internal RAM in one go. After boot the screensaver and the
             * UI hold it, so Wi-Fi can only start early in the boot. The switch is saved already. */
            set_status(WIFI_DEBUG_OFF, NULL, "Restarting...");
            ESP_LOGW(TAG, "restarting to start Wi-Fi at boot");
            vTaskDelay(pdMS_TO_TICKS(500)); /* let the screen show the text */
            esp_restart();
            break;
        }
    }
}

/* Called from the LVGL task and the main task, never at the same time as itself on another core in practice;
 * the lazy create is guarded by a critical section anyway. */
static void post(cmd_t cmd)
{
    static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
    if (!s_cmd_queue) {
        QueueHandle_t q = xQueueCreate(4, sizeof(cmd_t));
        portENTER_CRITICAL(&lock);
        if (!s_cmd_queue) {
            s_cmd_queue = q;
            q = NULL;
        }
        portEXIT_CRITICAL(&lock);
        if (q) {
            vQueueDelete(q);
        }
        if (!s_cmd_queue || xTaskCreate(ctl_task, "wifi_ctl", 6144, NULL, 4, NULL) != pdPASS) {
            ESP_LOGE(TAG, "cannot create the control task");
            return;
        }
    }
    if (xQueueSend(s_cmd_queue, &cmd, 0) != pdTRUE) {
        ESP_LOGW(TAG, "command queue full, command %d dropped", (int)cmd);
    }
}

void wifi_debug_boot(void)
{
    if (settings_get_wifi_enabled()) {
        s_boot_pending = true;
        post(CMD_START);
    }
}

void wifi_debug_boot_wait(uint32_t timeout_ms)
{
    for (uint32_t waited = 0; s_boot_pending && waited < timeout_ms; waited += 10) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void wifi_debug_set_enabled(bool on)
{
    settings_set_wifi_enabled(on);
    if (on && !s_running) {
        post(CMD_RESTART);
    } else if (!on) {
        post(CMD_STOP);
    }
}

void wifi_debug_connect(const char *ssid, const char *pass)
{
    settings_set_wifi_credentials(ssid, pass);
    post(CMD_CONNECT);
}

bool wifi_debug_scan_start(void)
{
    if (!s_running || s_scan_running) {
        return false;
    }
    s_scan_running = true;
    s_scan_count = 0;
    wifi_scan_config_t cfg = {0};
    if (esp_wifi_scan_start(&cfg, false) != ESP_OK) {
        s_scan_running = false;
        return false;
    }
    return true;
}

int wifi_debug_scan_result(wifi_debug_ap_t *out, int max, bool *done)
{
    *done = !s_scan_running;
    int n = s_scan_count < max ? s_scan_count : max;
    if (*done) {
        memcpy(out, s_scan, (size_t)n * sizeof(s_scan[0]));
    } else {
        n = 0;
    }
    return n;
}
