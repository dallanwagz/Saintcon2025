file(GLOB_RECURSE embedded_images images/*.c)
file(GLOB_RECURSE embedded_fonts fonts/*.c)
file(GLOB_RECURSE screen_sources screens/*.c)
file(GLOB_RECURSE widget_sources widgets/*.c)

idf_component_register(SRCS "loadanim.c" "nav.c" "theme.c" "ui_events.c" "ui.c" "screenshot.c" ${embedded_images} ${embedded_fonts} ${screen_sources} ${widget_sources}
                       INCLUDE_DIRS "include" "widgets"
                       REQUIRES "badge" "battery" "display" "hid" "input" "json" "led" "led_patterns" "lvgl" "nfc" "wifi_manager" "wled_mirror")

include_directories(${CMAKE_BINARY_DIR}/include)
target_include_directories(${COMPONENT_LIB} PRIVATE ".")
