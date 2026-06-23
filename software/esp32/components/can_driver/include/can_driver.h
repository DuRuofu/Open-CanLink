/*
 * can_driver.h - TWAI driver wrapper for esp-can-link
 *
 * Provides a simplified API for CAN bus operations using the
 * ESP-IDF TWAI (Two-Wire Automotive Interface) controller.
 * Handles initialization, transmit, receive callbacks, status,
 * and automatic bus-off recovery.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Supported CAN bitrates */
typedef enum {
    CAN_BITRATE_125K  = 125000,
    CAN_BITRATE_250K  = 250000,
    CAN_BITRATE_500K  = 500000,
    CAN_BITRATE_1000K = 1000000,
} can_bitrate_t;

/** CAN bus state */
typedef enum {
    CAN_STATE_STOPPED,
    CAN_STATE_RUNNING,
    CAN_STATE_BUS_OFF,
} can_state_t;

/** CAN driver status information */
typedef struct {
    can_state_t state;
    uint16_t tx_errors;
    uint16_t rx_errors;
    bool bus_off;
    uint32_t tx_queue_remaining;
} can_status_t;

/**
 * @brief CAN RX callback type.
 * @param id        CAN message ID
 * @param ext       true for extended frame (29-bit ID), false for standard (11-bit)
 * @param dlc       Data length code (0-8)
 * @param data      Pointer to data bytes (valid for dlc bytes)
 * @param timestamp_us Timestamp in microseconds (since node enabled)
 * @param user_ctx  User context pointer passed to can_driver_set_rx_callback
 */
typedef void (*can_rx_callback_t)(uint32_t id, bool ext, uint8_t dlc,
                                   const uint8_t *data, uint32_t timestamp_us,
                                   void *user_ctx);

/**
 * @brief Initialize the CAN driver.
 *
 * Configures the TWAI controller but does NOT start it.
 * Call can_driver_start() to begin bus participation.
 *
 * @param tx_pin       GPIO for CAN TX (connected to TJA1051 TXD)
 * @param rx_pin       GPIO for CAN RX (connected to TJA1051 RXD)
 * @param bitrate      Bitrate in bps (use can_bitrate_t values)
 * @param standby_pin  GPIO for TJA1051 S pin (LOW=normal, HIGH=standby).
 *                     Pass GPIO_NUM_NC if not used.
 * @return ESP_OK on success
 */
esp_err_t can_driver_init(gpio_num_t tx_pin, gpio_num_t rx_pin, uint32_t bitrate,
                           gpio_num_t standby_pin);

/**
 * @brief Start the CAN driver (enter normal operating mode).
 * @return ESP_OK on success
 */
esp_err_t can_driver_start(void);

/**
 * @brief Stop the CAN driver (disables the node but keeps configuration).
 * @return ESP_OK on success
 */
esp_err_t can_driver_stop(void);

/**
 * @brief Reconfigure the bitrate.
 *
 * Stops the driver, reconfigures, and restarts if was running.
 *
 * @param bitrate  New bitrate in bps
 * @return ESP_OK on success
 */
esp_err_t can_driver_set_bitrate(uint32_t bitrate);

/**
 * @brief Transmit a CAN frame.
 *
 * Thread-safe. Blocks until frame is queued for transmission
 * (does NOT wait for actual transmission completion).
 *
 * @param id    CAN message ID
 * @param ext   true for extended frame
 * @param dlc   Data length (0-8)
 * @param data  Pointer to dlc data bytes (can be NULL if dlc=0)
 * @return ESP_OK on success
 */
esp_err_t can_driver_transmit(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data);

/**
 * @brief Get current CAN driver status.
 * @param[out] status  Pointer to status struct to fill
 * @return ESP_OK on success
 */
esp_err_t can_driver_get_status(can_status_t *status);

/**
 * @brief Register a callback for received CAN frames.
 *
 * The callback is invoked from a FreeRTOS task context (NOT from ISR).
 *
 * @param cb        Callback function (NULL to unregister)
 * @param user_ctx  User context passed to callback
 */
void can_driver_set_rx_callback(can_rx_callback_t cb, void *user_ctx);

/**
 * @brief Configure acceptance filter (mask-based).
 *
 * @param id    Base ID to match
 * @param mask  Mask (1=match, 0=don't care)
 * @param ext   true for extended IDs, false for standard
 * @return ESP_OK on success
 */
esp_err_t can_driver_set_filter(uint32_t id, uint32_t mask, bool ext);

/**
 * @brief Trigger bus-off recovery (auto-called internally, but exposed for manual use).
 * @return ESP_OK on success
 */
esp_err_t can_driver_recover(void);

/**
 * @brief Start the internal RX processing task. Call from app_main.
 */
void can_driver_start_rx_task(void);

#ifdef __cplusplus
}
#endif
