/*
 * can_driver.h - CAN1 driver for CH32V203 (STM32-style API)
 *
 * Wraps WCH's ch32v20x_can.h peripheral library.
 * Hardware: CAN1 Remap1 — PB8=RX, PB9=TX, PB15=STB
 */

#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "ch32v20x.h"

/* ── Types ── */

typedef enum {
    CAN_STATE_STOPPED = 0,
    CAN_STATE_RUNNING,
    CAN_STATE_BUS_OFF
} can_state_t;

typedef struct {
    can_state_t state;
    uint8_t     tx_errors;
    uint8_t     rx_errors;
    bool        bus_off;
} can_status_t;

typedef struct {
    uint32_t id;
    uint32_t mask;
    bool     ext;
} can_filter_entry_t;

/* ── API ── */

/* Initialize CAN1 with given pins and bitrate. Starts in stopped state. */
void can_driver_init(uint16_t tx_pin, uint16_t rx_pin, uint16_t stb_pin,
                     uint32_t bitrate);

/* Start CAN (exit init mode, STB low) */
void can_driver_start(void);

/* Stop CAN (enter init mode, STB high) */
void can_driver_stop(void);

/* Send one CAN frame. Returns false if CAN not running or TX failed. */
bool can_driver_send(uint32_t id, bool ext, uint8_t dlc, const uint8_t *data);

/* Set bitrate. CAN must be stopped first. */
void can_driver_set_bitrate(uint32_t bitrate);

/* Configure hardware acceptance filters. Pass count=0 to clear. */
void can_driver_set_filter(const can_filter_entry_t *filters, uint8_t count);

/* Read bus status + error counters */
void can_driver_get_status(can_status_t *status);

/* Poll CAN1 FIFO0 for a received frame. Returns true if a frame was read. */
bool can_driver_poll_receive(uint32_t *id, bool *ext, uint8_t *dlc,
                              uint8_t *data, uint32_t *timestamp_us);

#endif
