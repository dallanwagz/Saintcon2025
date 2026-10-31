idf_component_register(SRCS "badge.c" "config.c" "migrate.c" "version.c" "ops/nfc.c"
                       INCLUDE_DIRS "include" "include/badge"
                       REQUIRES "display" "esp_app_format" "led_patterns" "nfc" "nvs" "spiffs")
