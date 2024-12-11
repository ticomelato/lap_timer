idf_component_register(SRCS "lap_timer.c" "wifi.c"
                       INCLUDE_DIRS "."
                       REQUIRES cJSON esp_wifi nvs_flash esp_http_server esp_timer driver)