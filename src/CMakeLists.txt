FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(SRCS ${app_sources} ${CMAKE_SOURCE_DIR}/src/screensaver.cpp
                       INCLUDE_DIRS "."
                       REQUIRES esp_new_jpeg esp_tinyusb fatfs sdmmc driver esp_rom esp_timer nvs_flash
                                esp_wifi esp_netif esp_event esp_http_server mdns)
