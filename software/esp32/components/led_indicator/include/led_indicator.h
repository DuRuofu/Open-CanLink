/*
 * led_indicator.h - WS2812 status LED for esp-can-link
 *
 * Uses a single WS2812 LED on a configurable GPIO to show device state:
 *   Off    — not initialized / booting
 *   Green  — CAN running, USB active
 *   Blue   — CAN stopped (ready)
 *   Red    — CAN bus-off / error
 */

#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    LED_STATE_OFF,
    LED_STATE_READY,     /* CAN stopped, device idle (blue) */
    LED_STATE_RUNNING,   /* CAN running normally (green) */
    LED_STATE_ERROR,     /* CAN bus-off or error (red) */
} led_state_t;

/**
 * @brief Initialize the status LED.
 * @param gpio_num  GPIO for WS2812 data line
 * @return ESP_OK on success
 */
esp_err_t led_indicator_init(uint8_t gpio_num);

/**
 * @brief Set the LED state (color).
 * @param state  One of LED_STATE_OFF / READY / RUNNING / ERROR
 * @return ESP_OK on success
 */
esp_err_t led_indicator_set_state(led_state_t state);

/**
 * @brief Quick activity blink: off briefly then restore last state.
 * Non-blocking — call from ISR callback or task context.
 */
void led_indicator_blip(void);

/**
 * @brief Turn off the LED and release resources.
 * @return ESP_OK on success
 */
esp_err_t led_indicator_deinit(void);

#ifdef __cplusplus
}
#endif
