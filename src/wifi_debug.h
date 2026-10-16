#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WIFI_DEBUG_OFF, WIFI_DEBUG_CONNECTING, WIFI_DEBUG_CONNECTED, WIFI_DEBUG_FAILED } wifi_debug_state_t;
typedef struct {
    wifi_debug_state_t state;
    char ip[16];      /* "192.168.1.20" when connected, else "" */
    char reason[24];  /* when FAILED: "wrong password", "network not found", "error 205" */
} wifi_debug_status_t;
typedef struct { char ssid[33]; int8_t rssi; } wifi_debug_ap_t;

void wifi_debug_boot_wait(uint32_t timeout_ms);       /* after wifi_debug_boot(): wait until the boot START has finished (or the timeout). Call it before other code takes the internal RAM Wi-Fi needs. */
void wifi_debug_boot(void);                         /* call once after settings_init(); starts Wi-Fi only if the saved switch is on */
void wifi_debug_set_enabled(bool on);               /* saves the switch, starts or stops */
void wifi_debug_connect(const char *ssid, const char *pass); /* saves the credentials; (re)connects if running */
void wifi_debug_get_status(wifi_debug_status_t *out);
bool wifi_debug_scan_start(void);                   /* false if Wi-Fi is off or a scan is running */
/* Copies up to `max` results. *done is true when the scan has finished. Returns the count copied. */
int wifi_debug_scan_result(wifi_debug_ap_t *out, int max, bool *done);
/* For wifi_http: copy log text from absolute position *pos. Same contract as log_ring_read. */
size_t wifi_debug_log_read(uint64_t *pos, char *out, size_t max, size_t *skipped);

#ifdef __cplusplus
}
#endif
