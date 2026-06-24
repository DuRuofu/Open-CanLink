/*
 * protocol.c - JSON command protocol implementation
 *
 * Implements the line-delimited JSON protocol defined in docs/protocol.md.
 * Uses minimal, stack-based JSON parsing (no heap allocation, no cJSON dependency).
 */

#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "protocol.h"

/* ---- Line buffer ---- */

#define LINE_BUF_SIZE 512

static char s_line_buf[LINE_BUF_SIZE];
static int s_line_pos = 0;
static bool s_skip_lf = false;  /* handle \r\n line endings */

void protocol_feed_byte(char c)
{
    /* Ignore \n immediately following \r (Windows \r\n) */
    if (s_skip_lf) {
        s_skip_lf = false;
        if (c == '\n') {
            return;  /* skip the LF after CR */
        }
    }

    if (c == '\r') {
        /* End of line via CR - mark to skip following LF */
        s_skip_lf = true;
        if (s_line_pos > 0) {
            s_line_buf[s_line_pos] = '\0';
            s_line_pos++;  /* signal that a line is ready (pos > 0 until consumed) */
        }
        return;
    }

    if (c == '\n') {
        /* Standalone LF - end of line */
        if (s_line_pos > 0) {
            s_line_buf[s_line_pos] = '\0';
            s_line_pos++;
        }
        return;
    }

    /* Append to buffer (ignore overflow) */
    if (s_line_pos < LINE_BUF_SIZE - 1) {
        s_line_buf[s_line_pos++] = c;
    }
}

int protocol_get_line(char *line, size_t max_len)
{
    if (s_line_pos == 0) {
        return 0;
    }

    /* Check if line is complete (null-terminated) */
    if (s_line_buf[s_line_pos - 1] != '\0') {
        return 0;  /* still accumulating */
    }

    /* Line is complete */
    size_t copy_len = s_line_pos;
    if (copy_len > max_len - 1) {
        copy_len = max_len - 1;
    }
    memcpy(line, s_line_buf, copy_len);
    line[copy_len] = '\0';

    int result = (int)copy_len;
    s_line_pos = 0;  /* consumed */
    return result;
}

void protocol_reset(void)
{
    s_line_pos = 0;
    s_skip_lf = false;
}

/* ---- Minimal JSON parser ---- */

/* Skip whitespace, return pointer to first non-space char */
static const char *skip_space(const char *p)
{
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

/* Extract a quoted string value. p points to the opening quote. */
static const char *parse_string(const char *p, char *out, size_t max)
{
    if (*p != '"') return NULL;
    p++;
    size_t i = 0;
    while (*p && *p != '"' && i < max - 1) {
        if (*p == '\\' && *(p + 1)) {
            p++;
            switch (*p) {
            case '"':  out[i++] = '"'; break;
            case '\\': out[i++] = '\\'; break;
            case 'n':  out[i++] = '\n'; break;
            case 't':  out[i++] = '\t'; break;
            default:   out[i++] = *p; break;
            }
        } else {
            out[i++] = *p;
        }
        p++;
    }
    out[i] = '\0';
    if (*p == '"') p++;
    return p;
}

/* Parse an integer value */
static const char *parse_int(const char *p, int *val)
{
    bool neg = false;
    if (*p == '-') { neg = true; p++; }
    if (!isdigit((unsigned char)*p)) return NULL;
    *val = 0;
    while (isdigit((unsigned char)*p)) {
        *val = *val * 10 + (*p - '0');
        p++;
    }
    if (neg) *val = -(*val);
    return p;
}

/* Parse an unsigned 32-bit integer */
static const char *parse_uint32(const char *p, uint32_t *val)
{
    if (!isdigit((unsigned char)*p)) return NULL;
    *val = 0;
    while (isdigit((unsigned char)*p)) {
        *val = *val * 10 + (*p - '0');
        p++;
    }
    return p;
}

/* Find a key in a JSON object and return pointer to its value */
static const char *find_key(const char *p, const char *key)
{
    p = skip_space(p);
    if (*p != '{') return NULL;
    p++;

    while (*p) {
        p = skip_space(p);
        if (*p == '}') break;
        if (*p == ',') { p++; continue; }

        /* Parse key string */
        char kbuf[64];
        p = parse_string(p, kbuf, sizeof(kbuf));
        if (!p) return NULL;

        p = skip_space(p);
        if (*p != ':') return NULL;
        p++;  /* skip colon */

        if (strcmp(kbuf, key) == 0) {
            return p;  /* return pointer to value */
        }

        /* Skip value */
        p = skip_space(p);
        if (*p == '"') {
            char dummy[256];
            p = parse_string(p, dummy, sizeof(dummy));
        } else if (*p == '{') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '{') depth++;
                if (*p == '}') depth--;
                p++;
            }
        } else if (*p == '[') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '[') depth++;
                if (*p == ']') depth--;
                p++;
            }
        } else {
            while (*p && *p != ',' && *p != '}') p++;
        }
    }
    return NULL;  /* key not found */
}

/* Parse a JSON array of unsigned integers. Returns count of parsed values. */
static int parse_uint_array(const char *p, uint8_t *out, int max_count)
{
    p = skip_space(p);
    if (*p != '[') return -1;
    p++;

    int count = 0;
    while (*p && count < max_count) {
        p = skip_space(p);
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }

        int val;
        p = parse_int(p, &val);
        if (!p) return -1;
        if (val < 0 || val > 255) return -1;
        out[count++] = (uint8_t)val;
    }
    return count;
}

bool protocol_parse(const char *line, parsed_cmd_t *cmd)
{
    if (!line || !cmd) return false;
    memset(cmd, 0, sizeof(*cmd));

    /* Look for "cmd" key */
    const char *val = find_key(line, "cmd");
    if (!val) return false;

    val = skip_space(val);

    char cmd_str[32];
    if (*val == '"') {
        val = parse_string(val, cmd_str, sizeof(cmd_str));
        if (!val) return false;
    } else {
        return false;
    }

    /* Dispatch by command name */
    if (strcmp(cmd_str, "can_start") == 0) {
        cmd->cmd = CMD_CAN_START;
    } else if (strcmp(cmd_str, "can_stop") == 0) {
        cmd->cmd = CMD_CAN_STOP;
    } else if (strcmp(cmd_str, "set_bitrate") == 0) {
        cmd->cmd = CMD_SET_BITRATE;
        val = find_key(line, "bitrate");
        if (val) parse_uint32(skip_space(val), &cmd->set_bitrate.bitrate);
    } else if (strcmp(cmd_str, "set_filter") == 0) {
        cmd->cmd = CMD_SET_FILTER;
        /* Simplified: parse first filter entry */
        val = find_key(line, "filter");
        if (val) {
            val = skip_space(val);
            if (*val == '[') {
                val++;
                val = skip_space(val);
                if (*val == '{') {
                    const char *id_val = find_key(val, "id");
                    const char *mask_val = find_key(val, "mask");
                    const char *ext_val = find_key(val, "ext");
                    if (id_val) parse_uint32(skip_space(id_val), &cmd->set_filter.id);
                    if (mask_val) parse_uint32(skip_space(mask_val), &cmd->set_filter.mask);
                    if (ext_val) {
                        ext_val = skip_space(ext_val);
                        cmd->set_filter.ext = (strncmp(ext_val, "true", 4) == 0);
                    }
                }
            }
        }
    } else if (strcmp(cmd_str, "send") == 0) {
        cmd->cmd = CMD_SEND;
        val = find_key(line, "id");
        if (!val) { cmd->cmd = CMD_UNKNOWN; return true; }
        parse_uint32(skip_space(val), &cmd->send.id);
        val = find_key(line, "ext");
        if (val) {
            val = skip_space(val);
            cmd->send.ext = (strncmp(val, "true", 4) == 0);
        }
        val = find_key(line, "data");
        if (val) {
            int count = parse_uint_array(skip_space(val), cmd->send.data, 8);
            cmd->send.dlc = (count >= 0 && count <= 8) ? (uint8_t)count : 0;
        }
    } else if (strcmp(cmd_str, "periodic_start") == 0) {
        cmd->cmd = CMD_PERIODIC_START;
        val = find_key(line, "id");
        if (!val) { cmd->cmd = CMD_UNKNOWN; return true; }
        parse_uint32(skip_space(val), &cmd->periodic.id);
        val = find_key(line, "ext");
        if (val) {
            val = skip_space(val);
            cmd->periodic.ext = (strncmp(val, "true", 4) == 0);
        }
        val = find_key(line, "data");
        if (val) {
            int count = parse_uint_array(skip_space(val), cmd->periodic.data, 8);
            cmd->periodic.dlc = (count >= 0 && count <= 8) ? (uint8_t)count : 0;
        }
        val = find_key(line, "period_ms");
        if (!val) { cmd->cmd = CMD_UNKNOWN; return true; }
        {
            uint32_t period;
            parse_uint32(skip_space(val), &period);
            cmd->periodic.period_ms = (period > 0) ? period : 100;
        }
    } else if (strcmp(cmd_str, "periodic_stop") == 0) {
        cmd->cmd = CMD_PERIODIC_STOP;
        val = find_key(line, "id");
        if (val) parse_uint32(skip_space(val), &cmd->periodic.id);
    } else if (strcmp(cmd_str, "get_status") == 0) {
        cmd->cmd = CMD_GET_STATUS;
    } else if (strcmp(cmd_str, "get_info") == 0) {
        cmd->cmd = CMD_GET_INFO;
    } else {
        cmd->cmd = CMD_UNKNOWN;
    }

    return true;
}

/* ---- JSON builders ---- */

int protocol_build_response(const char *cmd_name, bool ok, const char *message,
                             char *out, size_t max_len)
{
    return snprintf(out, max_len,
        "{\"type\":\"response\",\"cmd\":\"%s\",\"status\":\"%s\",\"message\":\"%s\"}\n",
        cmd_name ? cmd_name : "unknown",
        ok ? "ok" : "error",
        message ? message : "");
}

int protocol_build_rx_frame(uint32_t id, bool ext, uint8_t dlc,
                             const uint8_t *data, uint32_t timestamp_ms,
                             char *out, size_t max_len)
{
    int pos = snprintf(out, max_len,
        "{\"type\":\"rx\",\"id\":%lu,\"ext\":%s,\"dlc\":%u,\"data\":[",
        (unsigned long)id, ext ? "true" : "false", dlc);

    /* Guard against initial snprintf overflow */
    if (pos < 0 || (size_t)pos >= max_len) {
        out[max_len > 0 ? max_len - 1 : 0] = '\0';
        return (int)(max_len - 1);
    }

    for (uint8_t i = 0; i < dlc; i++) {
        int remaining = (int)max_len - pos;
        if (remaining <= 1) break;  /* no room */
        int added = snprintf(out + pos, (size_t)remaining, "%s%u",
                             i > 0 ? "," : "", data ? data[i] : 0);
        if (added < 0 || added >= remaining) break;
        pos += added;
    }

    int remaining = (int)max_len - pos;
    if (remaining > 1) {
        int added = snprintf(out + pos, (size_t)remaining,
            "],\"timestamp_ms\":%lu}\n", (unsigned long)timestamp_ms);
        if (added > 0 && added < remaining) pos += added;
    }

    return pos;
}

int protocol_build_status(const char *state, int tx_errors, int rx_errors,
                           bool bus_off, char *out, size_t max_len)
{
    return snprintf(out, max_len,
        "{\"type\":\"status\",\"state\":\"%s\",\"tx_errors\":%d,"
        "\"rx_errors\":%d,\"bus_off\":%s}\n",
        state ? state : "unknown", tx_errors, rx_errors,
        bus_off ? "true" : "false");
}

int protocol_build_info(const char *firmware, const char *version,
                         const char *hw, char *out, size_t max_len)
{
    return snprintf(out, max_len,
        "{\"type\":\"info\",\"firmware\":\"%s\",\"version\":\"%s\","
        "\"hw\":\"%s\"}\n",
        firmware ? firmware : "unknown",
        version ? version : "0.0.0",
        hw ? hw : "unknown");
}
