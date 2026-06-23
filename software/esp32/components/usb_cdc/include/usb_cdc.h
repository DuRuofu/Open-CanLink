/*
 * usb_cdc.h - USB CDC ACM wrapper for esp-can-link
 *
 * Provides a simplified, thread-safe API for USB CDC virtual serial port
 * communication using the TinyUSB stack.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB CDC RX callback type.
 * @param data      Pointer to received bytes
 * @param len       Number of bytes received
 * @param user_ctx  User context pointer
 */
typedef void (*usb_cdc_rx_callback_t)(const uint8_t *data, size_t len, void *user_ctx);

/**
 * @brief Initialize USB CDC ACM.
 *
 * Installs TinyUSB driver and configures a single CDC ACM port.
 * After this call, the device will appear as a USB serial port on the host.
 *
 * @return ESP_OK on success
 */
esp_err_t usb_cdc_init(void);

/**
 * @brief Register a callback for received USB data.
 *
 * The callback is invoked from a FreeRTOS task context (NOT from ISR).
 *
 * @param cb        Callback function (NULL to unregister)
 * @param user_ctx  User context passed to callback
 */
void usb_cdc_set_rx_callback(usb_cdc_rx_callback_t cb, void *user_ctx);

/**
 * @brief Write raw data to USB CDC.
 *
 * Thread-safe. Blocks until data is queued for transmission.
 *
 * @param data  Pointer to bytes to send
 * @param len   Number of bytes to send
 * @return ESP_OK on success
 */
esp_err_t usb_cdc_write(const uint8_t *data, size_t len);

/**
 * @brief Write a null-terminated string to USB CDC.
 *
 * Convenience wrapper around usb_cdc_write().
 *
 * @param str  Null-terminated string to send
 * @return ESP_OK on success
 */
esp_err_t usb_cdc_write_str(const char *str);

#ifdef __cplusplus
}
#endif
