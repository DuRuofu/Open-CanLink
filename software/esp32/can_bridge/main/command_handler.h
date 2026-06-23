/*
 * command_handler.h - JSON command processing for esp-can-link bridge
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process a parsed command and produce a JSON response.
 *
 * Handles all 9 protocol commands: can_start, can_stop, set_bitrate,
 * set_filter, send, periodic_start, periodic_stop, get_status, get_info.
 *
 * @param[in]  cmd      Parsed command
 * @param[out] response Output buffer for JSON response
 * @param      max_len  Size of response buffer
 * @return Number of bytes written (excluding null terminator), 0 if no response needed
 */
int command_handler_process(const parsed_cmd_t *cmd, char *response, size_t max_len);

/**
 * @brief Get the current CAN status as a JSON string.
 */
int command_handler_get_status_json(char *out, size_t max_len);

/**
 * @brief Get device info as a JSON string.
 */
int command_handler_get_info_json(char *out, size_t max_len);

/**
 * @brief Periodic task: sends registered periodic frames.
 *
 * Call this from a FreeRTOS task loop with appropriate delays.
 */
void command_handler_periodic_tick(void);

#ifdef __cplusplus
}
#endif
