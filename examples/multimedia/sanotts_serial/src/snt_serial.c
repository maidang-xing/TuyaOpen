/**
 * @file snt_serial.c
 * @brief sanoTTS Serial Line Protocol and State Machine Implementation
 */

#include "snt_serial.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>

static bool parse_uint32(const char *str, uint32_t *val)
{
    if (!str || !*str) {
        return false;
    }
    while (*str == ' ') {
        str++;
    }
    if (*str == '-' || *str == '+') {
        return false;
    }
    char *endptr = NULL;
    unsigned long v = strtoul(str, &endptr, 10);
    if (endptr == str) {
        return false;
    }
    while (*endptr) {
        if (*endptr != ' ' && *endptr != '\r' && *endptr != '\n') {
            return false;
        }
        endptr++;
    }
    if (v > UINT32_MAX) {
        return false;
    }
    *val = (uint32_t)v;
    return true;
}

static bool is_duplicate_id(snt_serial_t *snt, uint32_t id)
{
    for (size_t i = 0; i < snt->id_history_count; i++) {
        if (snt->id_history[i] == id) {
            return true;
        }
    }
    return false;
}

static void record_id(snt_serial_t *snt, uint32_t id)
{
    if (snt->id_history_count < SNT_SERIAL_ID_HISTORY_CAP) {
        snt->id_history[snt->id_history_count++] = id;
    } else {
        /* Shift left to keep recent history */
        memmove(&snt->id_history[0], &snt->id_history[1],
                (SNT_SERIAL_ID_HISTORY_CAP - 1) * sizeof(uint32_t));
        snt->id_history[SNT_SERIAL_ID_HISTORY_CAP - 1] = id;
    }
}

void snt_serial_send_json(snt_serial_t *snt, const char *json_fmt, ...)
{
    char buf[1280];
    int prefix_len = snprintf(buf, sizeof(buf), "@SNT\t");
    if (prefix_len < 0 || prefix_len >= (int)sizeof(buf)) {
        return;
    }

    va_list args;
    va_start(args, json_fmt);
    int body_len = vsnprintf(buf + prefix_len, sizeof(buf) - (size_t)prefix_len - 2, json_fmt, args);
    va_end(args);

    if (body_len < 0) {
        return;
    }
    size_t total = (size_t)(prefix_len + body_len);
    buf[total++] = '\n';
    buf[total] = '\0';

    if (snt->tx_fn) {
        snt->tx_fn(buf, total, snt->tx_user);
    }
}

size_t snt_serial_size(void)
{
    return sizeof(snt_serial_t);
}

void snt_serial_init(snt_serial_t *snt,
                     snt_serial_tx_fn tx_fn, void *tx_user,
                     snt_serial_exec_fn exec_fn, void *exec_user,
                     snt_serial_cancel_fn cancel_fn, void *cancel_user)
{
    memset(snt, 0, sizeof(snt_serial_t));
    snt->tx_fn = tx_fn;
    snt->tx_user = tx_user;
    snt->exec_fn = exec_fn;
    snt->exec_user = exec_user;
    snt->cancel_fn = cancel_fn;
    snt->cancel_user = cancel_user;
    snt->voice_name = "en-nano";
}

void snt_serial_set_voice(snt_serial_t *snt, const char *voice_name)
{
    snt->voice_name = voice_name ? voice_name : "en-nano";
}

static void parse_and_process_line(snt_serial_t *snt, char *line)
{
    /* Strip trailing \r if present */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\r') {
        line[len - 1] = '\0';
        len--;
    }

    if (len == 0) {
        return; /* Ignore empty lines */
    }

    /* First field: Command Verb up to first \t */
    char *tab1 = strchr(line, '\t');
    if (!tab1) {
        snt_serial_send_json(snt, "{\"id\":0,\"event\":\"error\",\"code\":%d,\"msg\":\"missing tab delimiter\"}",
                             SNT_EINVAL);
        return;
    }
    *tab1 = '\0';
    const char *verb = line;
    char *rest = tab1 + 1;

    /* Second field: Request ID up to next \t (if any) */
    char *tab2 = strchr(rest, '\t');
    if (tab2) {
        *tab2 = '\0';
    }
    const char *id_str = rest;
    char *payload = tab2 ? (tab2 + 1) : NULL;

    uint32_t id = 0;
    if (!parse_uint32(id_str, &id)) {
        snt_serial_send_json(snt, "{\"id\":0,\"event\":\"error\",\"code\":%d,\"msg\":\"invalid id format\"}",
                             SNT_EINVAL);
        return;
    }

    /* Check duplicate ID */
    if (is_duplicate_id(snt, id)) {
        snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"duplicate id\"}",
                             id, SNT_EINVAL);
        return;
    }

    /* Determine command type */
    snt_cmd_type_t cmd_type = SNT_CMD_UNKNOWN;
    if (strcmp(verb, "HELLO") == 0) {
        cmd_type = SNT_CMD_HELLO;
    } else if (strcmp(verb, "STOP") == 0) {
        cmd_type = SNT_CMD_STOP;
    } else if (strcmp(verb, "SAY") == 0) {
        cmd_type = SNT_CMD_SAY;
    } else if (strcmp(verb, "PHONEMES") == 0) {
        cmd_type = SNT_CMD_PHONEMES;
    } else if (strcmp(verb, "GOLDEN") == 0) {
        cmd_type = SNT_CMD_GOLDEN;
    } else if (strcmp(verb, "G2P") == 0) {
        cmd_type = SNT_CMD_G2P;
    } else {
        snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"unknown command verb\"}",
                             id, SNT_EINVAL);
        return;
    }

    /* Record valid ID in history */
    record_id(snt, id);

    /* Handle HELLO (control path, non-blocking) */
    if (cmd_type == SNT_CMD_HELLO) {
        if (payload && *payload != '\0') {
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"extra parameter in HELLO\"}",
                                 id, SNT_EINVAL);
            return;
        }
        snt_serial_send_json(snt,
            "{\"id\":%u,\"event\":\"hello\",\"version\":\"1.0.0\",\"voice\":\"%s\","
            "\"commands\":[\"HELLO\",\"SAY\",\"PHONEMES\",\"GOLDEN\",\"STOP\",\"G2P\"],"
            "\"limits\":{\"max_line\":%d,\"max_phonemes\":%d}}",
            id, snt->voice_name ? snt->voice_name : "en-nano",
            SNT_SERIAL_MAX_LINE, SNT_SERIAL_MAX_PHONEMES);
        return;
    }

    /* Handle STOP (control path, highest priority cancel) */
    if (cmd_type == SNT_CMD_STOP) {
        if (payload && *payload != '\0') {
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"extra parameter in STOP\"}",
                                 id, SNT_EINVAL);
            return;
        }

        /* Signal cancel to engine/audio immediately */
        if (snt->cancel_fn) {
            snt->cancel_fn(snt->cancel_user);
        }

        /* Cancel active request if present */
        if (snt->has_active) {
            uint32_t active_id = snt->active_req.id;
            snt->active_req.state = SNT_REQ_STATE_CANCELLED;
            snt->has_active = false;
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"cancelled\"}", active_id);
        }

        /* Cancel pending request if present */
        if (snt->has_pending) {
            uint32_t pending_id = snt->pending_req.id;
            snt->pending_req.state = SNT_REQ_STATE_CANCELLED;
            snt->has_pending = false;
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"cancelled\"}", pending_id);
        }

        /* Acknowledge STOP itself */
        snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"accepted\"}", id);
        snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"done\"}", id);
        return;
    }

    /* Prepare request object */
    snt_request_t *req = &snt->temp_req;
    memset(req, 0, sizeof(*req));
    req->type = cmd_type;
    req->id = id;
    req->state = SNT_REQ_STATE_ACCEPTED;

    if (cmd_type == SNT_CMD_SAY || cmd_type == SNT_CMD_G2P) {
        if (!payload || *payload == '\0') {
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"empty SAY text\"}",
                                 id, SNT_EINVAL);
            return;
        }
        size_t p_len = strlen(payload);
        if (p_len >= sizeof(req->text)) {
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"text too long\"}",
                                 id, SNT_E2BIG);
            return;
        }
        memcpy(req->text, payload, p_len + 1);
        req->text_len = p_len;
    } else if (cmd_type == SNT_CMD_PHONEMES) {
        if (!payload || *payload == '\0') {
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"empty PHONEMES list\"}",
                                 id, SNT_EINVAL);
            return;
        }
        char *token = strtok(payload, ",");
        size_t count = 0;
        bool parse_err = false;
        while (token) {
            while (*token == ' ') token++;
            if (*token == '\0' || *token == '-' || !isdigit((unsigned char)*token)) {
                parse_err = true;
                break;
            }
            char *endptr = NULL;
            long val = strtol(token, &endptr, 10);
            while (endptr && *endptr == ' ') endptr++;
            if (endptr && *endptr != '\0') {
                parse_err = true;
                break;
            }
            if (val < 0) {
                parse_err = true;
                break;
            }
            if (count >= SNT_SERIAL_MAX_PHONEMES) {
                snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"too many phonemes\"}",
                                     id, SNT_E2BIG);
                return;
            }
            req->phonemes[count++] = (int32_t)val;
            token = strtok(NULL, ",");
        }
        if (parse_err || count == 0) {
            snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"invalid phonemes CSV\"}",
                                 id, SNT_EINVAL);
            return;
        }
        req->n_phonemes = count;
    } else if (cmd_type == SNT_CMD_GOLDEN) {
        if (payload && *payload != '\0') {
            size_t p_len = strlen(payload);
            if (p_len < sizeof(req->text)) {
                memcpy(req->text, payload, p_len + 1);
                req->text_len = p_len;
            }
        }
    }

    /* Queue check: at most 1 active and 1 pending */
    if (snt->has_active && snt->has_pending) {
        snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"busy\"}",
                             id, SNT_EBUSY);
        return;
    }

    /* Request accepted */
    snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"accepted\",\"voice\":\"%s\"}",
                         id, snt->voice_name ? snt->voice_name : "en-nano");

    if (!snt->has_active) {
        snt->active_req = *req;
        snt->has_active = true;
        if (snt->exec_fn) {
            snt->exec_fn(&snt->active_req, snt->exec_user);
        }
    } else {
        snt->pending_req = *req;
        snt->has_pending = true;
    }
}

void snt_serial_feed(snt_serial_t *snt, const uint8_t *data, size_t len)
{
    if (!snt || !data || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char c = (char)data[i];
        if (snt->discarding_overflow) {
            if (c == '\n') {
                snt->discarding_overflow = false;
                snt->line_len = 0;
            }
            continue;
        }

        if (c == '\n') {
            snt->line_buf[snt->line_len] = '\0';
            parse_and_process_line(snt, snt->line_buf);
            snt->line_len = 0;
        } else {
            if (snt->line_len + 1 >= SNT_SERIAL_MAX_LINE) {
                /* Exceeded max line length (including LF) */
                snt->discarding_overflow = true;
                snt->line_len = 0;
                snt_serial_send_json(snt, "{\"id\":0,\"event\":\"error\",\"code\":%d,\"msg\":\"line too long\"}",
                                     SNT_E2BIG);
            } else {
                snt->line_buf[snt->line_len++] = c;
            }
        }
    }
}

void snt_serial_send_phonemes(snt_serial_t *snt, uint32_t id, const int32_t *phonemes, size_t n)
{
    char buf[2560];
    int len = snprintf(buf, sizeof(buf), "{\"id\":%u,\"event\":\"phonemes\",\"n_phonemes\":%u,\"phonemes\":[",
                       (unsigned)id, (unsigned)n);
    for (size_t i = 0; i < n && len < (int)sizeof(buf) - 16; i++) {
        len += snprintf(buf + len, sizeof(buf) - len, "%s%d", (i > 0) ? "," : "", (int)phonemes[i]);
    }
    snprintf(buf + len, sizeof(buf) - len, "]}");
    snt_serial_send_json(snt, "%s", buf);
}

void snt_serial_notify_synthesizing(snt_serial_t *snt, uint32_t id)
{
    snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"synthesizing\"}", id);
}

void snt_serial_notify_playing(snt_serial_t *snt, uint32_t id)
{
    snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"playing\"}", id);
}

void snt_serial_notify_done(snt_serial_t *snt, uint32_t id,
                            uint32_t samples, uint32_t source_rate,
                            uint64_t compute_us, uint64_t wall_us,
                            uint32_t underruns)
{
    snt_serial_send_json(snt,
        "{\"id\":%u,\"event\":\"done\",\"samples\":%u,\"source_rate\":%u,"
        "\"compute_us\":%llu,\"wall_us\":%llu,\"underruns\":%u}",
        id, samples, source_rate,
        (unsigned long long)compute_us, (unsigned long long)wall_us,
        underruns);

    if (snt->has_active && snt->active_req.id == id) {
        snt->has_active = false;
        if (snt->has_pending) {
            snt->active_req = snt->pending_req;
            snt->has_active = true;
            snt->has_pending = false;
            if (snt->exec_fn) {
                snt->exec_fn(&snt->active_req, snt->exec_user);
            }
        }
    }
}

void snt_serial_notify_error(snt_serial_t *snt, uint32_t id,
                             int code, const char *msg)
{
    snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"error\",\"code\":%d,\"msg\":\"%s\"}",
                         id, code, msg ? msg : "unknown error");

    if (snt->has_active && snt->active_req.id == id) {
        snt->has_active = false;
        if (snt->has_pending) {
            snt->active_req = snt->pending_req;
            snt->has_active = true;
            snt->has_pending = false;
            if (snt->exec_fn) {
                snt->exec_fn(&snt->active_req, snt->exec_user);
            }
        }
    }
}

void snt_serial_notify_cancelled(snt_serial_t *snt, uint32_t id)
{
    snt_serial_send_json(snt, "{\"id\":%u,\"event\":\"cancelled\"}", id);

    if (snt->has_active && snt->active_req.id == id) {
        snt->has_active = false;
        if (snt->has_pending) {
            snt->active_req = snt->pending_req;
            snt->has_active = true;
            snt->has_pending = false;
            if (snt->exec_fn) {
                snt->exec_fn(&snt->active_req, snt->exec_user);
            }
        }
    }
}

bool snt_serial_is_active_cancelled(snt_serial_t *snt, uint32_t id)
{
    if (snt->has_active && snt->active_req.id == id) {
        return (snt->active_req.state == SNT_REQ_STATE_CANCELLED);
    }
    return false;
}
