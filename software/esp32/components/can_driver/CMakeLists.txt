# Component: can_driver - TWAI driver wrapper for esp-can-link
idf_component_register(
    SRCS "can_driver.c"
    REQUIRES esp_driver_twai esp_driver_gpio
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
)
