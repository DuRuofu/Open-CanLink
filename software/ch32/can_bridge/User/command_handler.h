/*
 * command_handler.h - JSON command processing for open-can-link
 *
 * Ported from ESP32 firmware. Replaces FreeRTOS mutex with
 * bare-metal design (single-threaded main loop, no locking needed).
 */

#ifndef COMMAND_HANDLER_H
#define COMMAND_HANDLER_H

#include <stdint.h>
#include <stddef.h>
#include "protocol.h"

/* Call once at startup */
void command_handler_init(void);

/* Process a parsed JSON command. Writes JSON response into out[]. */
int command_handler_process(const parsed_cmd_t *cmd, char *out, size_t max_len);

/* Call every 10ms from main loop to service periodic CAN frames */
void command_handler_periodic_tick(void);

#endif
