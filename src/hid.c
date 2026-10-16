#include "hid.h"

#include <stdint.h>

#include "class/hid/hid_device.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tinyusb_default_config.h"

static const char *TAG = "hid";

#define KEY_QUEUE_LEN 8
#define PRESS_MS 20
#define READY_POLL_MS 5
#define READY_POLL_TRIES 20
#define RELEASE_TRIES 5

#define REPORT_ID_KEYBOARD 1
#define REPORT_ID_CONSUMER 2
#define TYPE_GAP_MS 10

typedef struct {
    const hid_macro_t *macro; /* NULL: send a single consumer key */
    uint16_t usage;
} job_t;

static QueueHandle_t s_queue;
static volatile bool s_macro_busy;

/* Boot-protocol-compatible keyboard (report ID 1) and a 16-bit consumer-control usage (report ID 2). */
static const uint8_t s_report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_ID_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_ID_CONSUMER)),
};

static const uint8_t s_ascii_to_hid[128][2] = {HID_ASCII_TO_KEYCODE};

static const char *s_strings[] = {
    (char[]){0x09, 0x04}, /* 0: language = English (0x0409) */
    "macropadv2",         /* 1: manufacturer */
    "Macropad",           /* 2: product */
    "000001",             /* 3: serial */
    "Macropad Keys",      /* 4: HID interface */
};

#define CFG_DESC_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
static const uint8_t s_cfg_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CFG_DESC_LEN, 0, 100),
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(s_report_desc), 0x81, 16, 10),
};

/* TinyUSB callbacks required by the HID class. */
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    (void)instance;
    return s_report_desc;
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                               uint8_t *buffer, uint16_t reqlen)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)reqlen;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                           uint8_t const *buffer, uint16_t bufsize)
{
    (void)instance; (void)report_id; (void)report_type; (void)buffer; (void)bufsize;
}

static bool wait_ready(void)
{
    for (int i = 0; i < READY_POLL_TRIES; i++) {
        if (tud_hid_ready()) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(READY_POLL_MS));
    }
    return false;
}

static bool send_consumer(uint16_t usage)
{
    return wait_ready() && tud_hid_report(REPORT_ID_CONSUMER, &usage, sizeof(usage));
}

static bool send_keys(uint8_t mods, uint8_t key)
{
    uint8_t keys[6] = {key};
    return wait_ready() && tud_hid_keyboard_report(REPORT_ID_KEYBOARD, mods, keys);
}

/* A key must never stay down: retry the release. */
static void release_all(bool keyboard)
{
    for (int i = 0; i < RELEASE_TRIES; i++) {
        if (keyboard ? send_keys(0, 0) : send_consumer(0)) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGE(TAG, "release failed");
}

static void tap_consumer(uint16_t usage)
{
    if (!send_consumer(usage)) {
        ESP_LOGW(TAG, "press 0x%04x not sent", usage);
    }
    vTaskDelay(pdMS_TO_TICKS(PRESS_MS));
    release_all(false);
}

static void tap_combo(uint8_t mods, uint8_t key)
{
    if (!send_keys(mods, key)) {
        ESP_LOGW(TAG, "combo mods=0x%02x key=0x%02x not sent", mods, key);
    }
    vTaskDelay(pdMS_TO_TICKS(PRESS_MS));
    release_all(true);
    vTaskDelay(pdMS_TO_TICKS(TYPE_GAP_MS));
}

static void type_text(const char *text)
{
    for (const char *p = text; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c >= 128 || s_ascii_to_hid[c][1] == 0) {
            ESP_LOGW(TAG, "cannot type 0x%02x", c);
            continue;
        }
        tap_combo(s_ascii_to_hid[c][0] ? HID_MOD_SHIFT : 0, s_ascii_to_hid[c][1]);
    }
}

static void run_macro(const hid_macro_t *macro)
{
    for (size_t i = 0; i < macro->count; i++) {
        const hid_action_t *a = &macro->actions[i];
        if (!tud_mounted()) {
            ESP_LOGW(TAG, "host gone, macro stopped");
            return;
        }
        switch (a->type) {
        case HID_ACT_CONSUMER: tap_consumer(a->value); break;
        case HID_ACT_COMBO:    tap_combo(a->mods, a->key); break;
        case HID_ACT_TYPE:     type_text(a->text); break;
        case HID_ACT_DELAY:    vTaskDelay(pdMS_TO_TICKS(a->value)); break;
        }
    }
}

/* TinyUSB HID calls are made from this single task by design. */
static void hid_task(void *arg)
{
    (void)arg;
    job_t job;
    for (;;) {
        if (xQueueReceive(s_queue, &job, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!tud_mounted()) {
            ESP_LOGW(TAG, "host not connected, dropped job");
        } else if (job.macro) {
            run_macro(job.macro);
        } else {
            tap_consumer(job.usage);
        }
        if (job.macro) {
            s_macro_busy = false;
        }
    }
}

void hid_init(void)
{
    s_queue = xQueueCreate(KEY_QUEUE_LEN, sizeof(job_t));
    configASSERT(s_queue);

    tinyusb_config_t cfg = TINYUSB_DEFAULT_CONFIG();
    cfg.descriptor.string = s_strings;
    cfg.descriptor.string_count = sizeof(s_strings) / sizeof(s_strings[0]);
    cfg.descriptor.full_speed_config = s_cfg_desc;
    ESP_ERROR_CHECK(tinyusb_driver_install(&cfg));

    /* The host may still hold the old USB-Serial-JTAG device. Drop the D+ pull-up
     * so the host sees a disconnect, then reconnect to force re-enumeration.
     * tinyusb_driver_install() returns after the TinyUSB stack is started. */
    tud_disconnect();
    vTaskDelay(pdMS_TO_TICKS(300));
    tud_connect();

    xTaskCreate(hid_task, "hid", 4096, NULL, 5, NULL);
    ESP_LOGI(TAG, "HID started");
}

bool hid_send(media_key_t key)
{
    if (s_queue == NULL) {
        return false;
    }
    job_t job = {.macro = NULL, .usage = (uint16_t)key};
    return xQueueSend(s_queue, &job, 0) == pdTRUE;
}

bool hid_send_macro(const hid_macro_t *macro)
{
    if (s_queue == NULL || s_macro_busy) {
        return false;
    }
    s_macro_busy = true;
    job_t job = {.macro = macro, .usage = 0};
    if (xQueueSend(s_queue, &job, 0) != pdTRUE) {
        s_macro_busy = false;
        return false;
    }
    return true;
}

bool hid_is_mounted(void)
{
    return tud_mounted();
}

void hid_detach(void)
{
    tud_disconnect();
}
