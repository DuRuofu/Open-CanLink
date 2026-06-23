/*
 * command_handler.c - JSON command processing implementation
 */

#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "can_driver.h"
#include "protocol.h"
#include "command_handler.h"

static const char *TAG = "cmd_handler";

/* Maximum periodic frames */
#define MAX_PERIODIC_FRAMES 4

/* Periodic frame state */
typedef struct {
    bool active;
    uint32_t id;
    bool ext;
    uint8_t data[8];
    uint8_t dlc;
    uint32_t period_ms;
    uint32_t last_send_ms;
} periodic_frame_t;

static periodic_frame_t s_periodic_frames[MAX_PERIODIC_FRAMES];
static uint32_t s_tick_ms = 0;

int command_handler_process(const parsed_cmd_t *cmd, char *response, size_t max_len)
{
    if (!cmd || !response) return 0;

    esp_err_t ret = ESP_OK;
    const char *errmsg = "";
    bool ok = true;

    switch (cmd->cmd) {
    case CMD_CAN_START:
        ret = can_driver_start();
        if (ret != ESP_OK) {
            ok = false;
            errmsg = "Failed to start CAN";
        }
        break;

    case CMD_CAN_STOP:
        ret = can_driver_stop();
        if (ret != ESP_OK) {
            ok = false;
            errmsg = "Failed to stop CAN";
        }
        break;

    case CMD_SET_BITRATE:
        if (cmd->set_bitrate.bitrate == 125000 ||
            cmd->set_bitrate.bitrate == 250000 ||
            cmd->set_bitrate.bitrate == 500000 ||
            cmd->set_bitrate.bitrate == 1000000) {
            ret = can_driver_set_bitrate(cmd->set_bitrate.bitrate);
            if (ret != ESP_OK) {
                ok = false;
                errmsg = "Failed to set bitrate";
            }
        } else {
            ok = false;
            errmsg = "Unsupported bitrate (use 125000/250000/500000/1000000)";
        }
        break;

    case CMD_SET_FILTER:
        ret = can_driver_set_filter(cmd->set_filter.id,
                                     cmd->set_filter.mask,
                                     cmd->set_filter.ext);
        if (ret != ESP_OK) {
            ok = false;
            errmsg = "Failed to set filter";
        }
        break;

    case CMD_SEND:
        ret = can_driver_transmit(cmd->send.id, cmd->send.ext,
                                   cmd->send.dlc, cmd->send.data);
        if (ret != ESP_OK) {
            ok = false;
            errmsg = "TX failed";
        }
        break;

    case CMD_PERIODIC_START: {
        /* Find free slot or reuse same ID */
        int slot = -1;
        for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
            if (s_periodic_frames[i].active &&
                s_periodic_frames[i].id == cmd->periodic.id) {
                slot = i;
                break;
            }
        }
        if (slot < 0) {
            for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
                if (!s_periodic_frames[i].active) {
                    slot = i;
                    break;
                }
            }
        }
        if (slot < 0) {
            ok = false;
            errmsg = "No free periodic slot";
        } else {
            s_periodic_frames[slot].active = true;
            s_periodic_frames[slot].id = cmd->periodic.id;
            s_periodic_frames[slot].ext = cmd->periodic.ext;
            s_periodic_frames[slot].dlc = cmd->periodic.dlc;
            memcpy(s_periodic_frames[slot].data, cmd->periodic.data, 8);
            s_periodic_frames[slot].period_ms = cmd->periodic.period_ms;
            s_periodic_frames[slot].last_send_ms = 0;
            ESP_LOGI(TAG, "Periodic frame started: ID=0x%lX, period=%lums, slot=%d",
                     cmd->periodic.id, cmd->periodic.period_ms, slot);
        }
        break;
    }

    case CMD_PERIODIC_STOP:
        for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
            if (s_periodic_frames[i].active &&
                s_periodic_frames[i].id == cmd->periodic.id) {
                s_periodic_frames[i].active = false;
                ESP_LOGI(TAG, "Periodic frame stopped: ID=0x%lX", cmd->periodic.id);
            }
        }
        break;

    case CMD_GET_STATUS:
        return command_handler_get_status_json(response, max_len);

    case CMD_GET_INFO:
        return command_handler_get_info_json(response, max_len);

    default:
        ok = false;
        errmsg = "Unknown command";
        break;
    }

    /* Build response for commands that need one */
    if (cmd->cmd == CMD_GET_STATUS || cmd->cmd == CMD_GET_INFO) {
        return 0;  /* already handled above */
    }

    const char *cmd_names[] = {
        [CMD_CAN_START] = "can_start",
        [CMD_CAN_STOP] = "can_stop",
        [CMD_SET_BITRATE] = "set_bitrate",
        [CMD_SET_FILTER] = "set_filter",
        [CMD_SEND] = "send",
        [CMD_PERIODIC_START] = "periodic_start",
        [CMD_PERIODIC_STOP] = "periodic_stop",
    };

    const char *cmd_name = (cmd->cmd < sizeof(cmd_names)/sizeof(cmd_names[0]))
                           ? cmd_names[cmd->cmd] : "unknown";

    return protocol_build_response(cmd_name, ok, errmsg, response, max_len);
}

int command_handler_get_status_json(char *out, size_t max_len)
{
    can_status_t status;
    esp_err_t ret = can_driver_get_status(&status);

    const char *state;
    if (ret != ESP_OK) {
        state = "unknown";
    } else {
        switch (status.state) {
        case CAN_STATE_RUNNING: state = "running"; break;
        case CAN_STATE_BUS_OFF: state = "bus_off"; break;
        default:                state = "stopped"; break;
        }
    }

    return protocol_build_status(state, status.tx_errors, status.rx_errors,
                                  status.bus_off, out, max_len);
}

int command_handler_get_info_json(char *out, size_t max_len)
{
    return protocol_build_info("esp-can-link", "0.1.0",
                                "ESP32-S3+TJA1051", out, max_len);
}

void command_handler_periodic_tick(void)
{
    s_tick_ms += 10;  /* assumes called every 10ms */

    for (int i = 0; i < MAX_PERIODIC_FRAMES; i++) {
        if (!s_periodic_frames[i].active) continue;

        periodic_frame_t *pf = &s_periodic_frames[i];
        if (s_tick_ms - pf->last_send_ms >= pf->period_ms) {
            /* Send the periodic frame */
            esp_err_t ret = can_driver_transmit(pf->id, pf->ext, pf->dlc, pf->data);
            if (ret == ESP_OK) {
                pf->last_send_ms = s_tick_ms;
            } else {
                ESP_LOGW(TAG, "Periodic TX failed for ID=0x%lX: %s",
                         pf->id, esp_err_to_name(ret));
            }
        }
    }
}
