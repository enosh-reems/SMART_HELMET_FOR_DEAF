idf_component_register(
    SRCS "main.c" "siren_detector.cpp" "pixel_led.c" "ble_nav.c" "espnow_button.c"
    INCLUDE_DIRS "."
    EMBED_FILES "models/light_siren_realworld_v2_s3_w8a8.espdl"
    REQUIRES esp-sr esp-dl esp_driver_i2s esp_driver_ledc esp_driver_gpio esp_driver_rmt esp_driver_usb_serial_jtag esp_driver_uart bt esp_wifi nvs_flash
)

