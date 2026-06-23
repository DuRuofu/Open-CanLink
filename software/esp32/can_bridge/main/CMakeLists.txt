idf_component_register(
    SRCS "app_main.c" "command_handler.c"
    REQUIRES esp_driver_twai esp_driver_gpio can_driver usb_cdc protocol nvs_flash
    INCLUDE_DIRS "."
)
