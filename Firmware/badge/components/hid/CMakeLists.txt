idf_component_register(SRCS "hid_ble.c" "hid_usb.c" "hid_profiles.c"
                       INCLUDE_DIRS "include"
                       REQUIRES "esp_hid" "bt" "esp_event" "joystick" "nvs_flash" "esp_driver_usb_serial_jtag")
