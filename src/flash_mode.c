#include "flash_mode.h"

#include <esp_log.h>
#include <esp_rom_sys.h>
#include <esp_system.h>
#include <driver/gpio.h>
#include <soc/rtc_cntl_reg.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "esp_bsp.h"
#include "hid.h"

static const char *TAG = "flash_mode";

void flash_mode_boot(void)
{
    ESP_LOGI(TAG, "boot: reset_reason=%d OPTION1=0x%08x", (int)esp_reset_reason(),
             (unsigned)REG_READ(RTC_CNTL_OPTION1_REG));
    /* The RTC domain keeps this bit across resets. If it stays set, every later reset
     * (esptool RTS reset, RST button) boots the ROM download mode again. */
    REG_WRITE(RTC_CNTL_OPTION1_REG, 0);
    gpio_hold_dis(EXAMPLE_PIN_NUM_QSPI_BL); /* flash_mode_enter() may have held it on */
}

void flash_mode_enter(void)
{
    ESP_LOGI(TAG, "rebooting into download mode (OPTION1 before=0x%08x)",
             (unsigned)REG_READ(RTC_CNTL_OPTION1_REG));
    REG_WRITE(RTC_CNTL_OPTION1_REG, RTC_CNTL_FORCE_DOWNLOAD_BOOT);
    ESP_LOGI(TAG, "OPTION1 after=0x%08x, calling esp_rom_software_reset_system()",
             (unsigned)REG_READ(RTC_CNTL_OPTION1_REG));
    /* Experiment: keep the backlight on through the reset so the last frame stays visible. */
    gpio_set_direction(EXAMPLE_PIN_NUM_QSPI_BL, GPIO_MODE_OUTPUT);
    gpio_set_level(EXAMPLE_PIN_NUM_QSPI_BL, 1);
    gpio_hold_en(EXAMPLE_PIN_NUM_QSPI_BL);
    hid_detach(); /* host sees a disconnect before the reset */
    vTaskDelay(pdMS_TO_TICKS(300));
    REG_WRITE(RTC_CNTL_USB_CONF_REG, 0); /* back to the default USB PHY routing */
    esp_rom_software_reset_system();
}
