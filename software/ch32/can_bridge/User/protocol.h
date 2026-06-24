/*
 * protocol.h - JSON command protocol for open-can-link
 *
 * Implements the line-delimited JSON protocol defined in docs/protocol.md.
 * Provides line buffering, command parsing, and response/event formatting.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Command types ---- */

typedef enum {
    CMD_UNKNOWN,
    CMD_CAN_START,
    CMD_CAN_STOP,
    CMD_SET_BITRATE,
    CMD_SET_FILTER,
    CMD_SEND,
    CMD_PERIODIC_START,
    CMD_PERIODIC_STOP,
    CMD_GET_STATUS,
    CMD_GET_INFO,
} cmd_type_t;

/* ---- Parsed command ---- */

typedef struct {
    cmd_type_t cmd;
    union {
        struct {
            uint32_t id;
            bool ext;
            uint8_t data[8];
            uint8_t dlc;
        } send;
        struct {
            uint32_t id;
            bool ext;
            uint8_t data[8];
            uint8_t dlc;
            uint32_t period_ms;
        } periodic;
        struct {
            uint32_t bitrate;
        } set_bitrate;
        struct {
            uint32_t id;
            uint32_t mask;
            bool ext;
        } set_filter;
    };
} parsed_cmd_t;

/* ---- Filter entry ---- */

#define MAX_FILTER_ENTRIES 4

typedef struct {
    uint32_t id;
    uint32_t mask;
    bool ext;
} filter_entry_t;

/* ---- Line buffer ---- */

/**
 * @brief Feed a single byte into the line buffer.
 *
 * Call this for each byte received. When a complete line (terminated by \n)
 * is accumulated, subsequent calls to protocol_get_line() will return it.
 *
 * @param c  Byte to feed
 */
void protocol_feed_byte(char c);

/**
 * @brief Get the next available complete line.
 *
 * @param[out] line     Buffer to receive the null-terminated line
 * @param      max_len  Size of the output buffer
 * @return Number of bytes written (including null terminator), or 0 if no line available
 */
int protocol_get_line(char *line, size_t max_len);

/**
 * @brief Parse a JSON command line into a structured command.
 *
 * @param[in]  line  Null-terminated JSON line from protocol_get_line()
 * @param[out] cmd   Parsed command struct (only valid if function returns true)
 * @return true if parsing succeeded, false otherwise
 */
bool protocol_parse(const char *line, parsed_cmd_t *cmd);

/* ---- JSON builders (output buffers should be at least 256 bytes) ---- */

/**
 * @brief Build a command response JSON.
 *
 * @param      cmd_name  Original command name (e.g., "send")
 * @param      ok        true for success, false for error
 * @param      message   Error description (or "" for success)
 * @param[out] out       Output buffer
 * @param      max_len   Size of output buffer
 * @return Number of bytes written (excluding null terminator)
 */
int protocol_build_response(const char *cmd_name, bool ok, const char *message,
                             char *out, size_t max_len);

/**
 * @brief Build a received CAN frame JSON (type: rx).
 */
int protocol_build_rx_frame(uint32_t id, bool ext, uint8_t dlc,
                             const uint8_t *data, uint32_t timestamp_ms,
                             char *out, size_t max_len);

/**
 * @brief Build a status update JSON (type: status).
 */
int protocol_build_status(const char *state, int tx_errors, int rx_errors,
                           bool bus_off, char *out, size_t max_len);

/**
 * @brief Build a device info JSON (type: info).
 */
int protocol_build_info(const char *firmware, const char *version,
                         const char *hw, char *out, size_t max_len);

/**
 * @brief Reset the line buffer (e.g., on connection loss).
 */
void protocol_reset(void);

#ifdef __cplusplus
}
#endif
