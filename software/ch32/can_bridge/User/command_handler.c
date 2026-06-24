/*
 * command_handler.c - JSON command processing for open-can-link CH32
 *
 * Ported from ESP32 firmware. Key differences:
 *   - No FreeRTOS — mutex removed (single-threaded main loop)
 *   - can_driver_send() returns bool instead of esp_err_t
 *   - get_info reports CH32V203 hardware
 */

#include "command_handler.h"
#include "can_driver.h"
#include "protocol.h"
#include <string.h>
#include <stdio.h>

/* ── Constants ── */

#define MAX_PERIODIC_FRAMES 4

/* ── Periodic frame state ── */

typedef struct {
    bool     active;
    uint32_t id;
    bool     ext;
    uint8_t  data[8];
    uint8_t  dlc;
    uint32_t period_ms;
    uint32_t last_send_ms;
} periodic_frame_t;

static periodic_frame_t s_periodic[MAX_PERIODIC_FRAMES];
static uint32_t         s_tick_ms;

/* ── Init ── */

void command_handler_init(void)
{
    s_tick_ms = 0;
    for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
        s_periodic[i].active = false;
    }
}

/* ── Process ── */

int command_handler_process(const parsed_cmd_t *cmd, char *out, size_t max_len)
{
    if (!cmd || !out || max_len == 0) return 0;

    bool        ok     = true;
    const char *errmsg = "";

    switch (cmd->cmd) {

    case CMD_CAN_START:
        can_driver_start();
        break;

    case CMD_CAN_STOP:
        can_driver_stop();
        break;

    case CMD_SET_BITRATE:
        if (cmd->set_bitrate.bitrate == 125000 ||
            cmd->set_bitrate.bitrate == 250000 ||
            cmd->set_bitrate.bitrate == 500000 ||
            cmd->set_bitrate.bitrate == 1000000) {
            can_driver_set_bitrate(cmd->set_bitrate.bitrate);
        } else {
            ok     = false;
            errmsg = "Unsupported bitrate (use 125000/250000/500000/1000000)";
        }
        break;

    case CMD_SET_FILTER: {
        can_filter_entry_t entry;
        entry.id   = cmd->set_filter.id;
        entry.mask = cmd->set_filter.mask;
        entry.ext  = cmd->set_filter.ext;
        can_driver_set_filter(&entry, 1);
        break;
    }

    case CMD_SEND:
        ok = can_driver_send(cmd->send.id, cmd->send.ext,
                              cmd->send.dlc, cmd->send.data);
        if (!ok) {
            errmsg = "TX failed — CAN not running?";
        }
        break;

    case CMD_PERIODIC_START: {
        /* Find free slot or reuse same ID */
        int slot = -1;
        for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
            if (s_periodic[i].active && s_periodic[i].id == cmd->periodic.id) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
                if (!s_periodic[i].active) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot < 0) {
            ok     = false;
            errmsg = "No free periodic slot (max 4)";
        } else {
            s_periodic[slot].active       = true;
            s_periodic[slot].id           = cmd->periodic.id;
            s_periodic[slot].ext          = cmd->periodic.ext;
            s_periodic[slot].dlc          = cmd->periodic.dlc;
            memcpy(s_periodic[slot].data, cmd->periodic.data, 8);
            s_periodic[slot].period_ms    = cmd->periodic.period_ms;
            s_periodic[slot].last_send_ms = s_tick_ms;
        }
        break;
    }

    case CMD_PERIODIC_STOP:
        for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
            if (s_periodic[i].active && s_periodic[i].id == cmd->periodic.id) {
                s_periodic[i].active = false;
            }
        }
        break;

    case CMD_GET_STATUS: {
        can_status_t st;
        can_driver_get_status(&st);
        const char *state_str = "stopped";
        if (st.state == CAN_STATE_RUNNING) state_str = "running";
        else if (st.state == CAN_STATE_BUS_OFF) state_str = "bus_off";
        return protocol_build_status(state_str, st.tx_errors, st.rx_errors,
                                      st.bus_off, out, max_len);
    }

    case CMD_GET_INFO:
        return protocol_build_info("open-can-link", "0.1.0",
                                    "CH32V203+TJA1051T", out, max_len);

    default:
        ok     = false;
        errmsg = "Unknown command";
        break;
    }

    /* ── Build standard response ── */

    const char *cmd_name = "unknown";
    switch (cmd->cmd) {
    case CMD_CAN_START:      cmd_name = "can_start";      break;
    case CMD_CAN_STOP:       cmd_name = "can_stop";       break;
    case CMD_SET_BITRATE:    cmd_name = "set_bitrate";    break;
    case CMD_SET_FILTER:     cmd_name = "set_filter";     break;
    case CMD_SEND:           cmd_name = "send";           break;
    case CMD_PERIODIC_START: cmd_name = "periodic_start"; break;
    case CMD_PERIODIC_STOP:  cmd_name = "periodic_stop";  break;
    default: break;
    }

    return protocol_build_response(cmd_name, ok, errmsg, out, max_len);
}

/* ── Periodic tick ── */

void command_handler_periodic_tick(void)
{
    s_tick_ms += 10;  /* assumes called every 10ms */

    for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
        if (!s_periodic[i].active) continue;

        periodic_frame_t *pf = &s_periodic[i];
        uint32_t elapsed = s_tick_ms - pf->last_send_ms;

        /* Handle timer wraparound */
        if (elapsed >= pf->period_ms) {
            can_driver_send(pf->id, pf->ext, pf->dlc, pf->data);
            pf->last_send_ms = s_tick_ms;
        }
    }
}
