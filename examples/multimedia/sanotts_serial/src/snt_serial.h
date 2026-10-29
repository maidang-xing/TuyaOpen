/**
 * @file snt_serial.h
 * @brief sanoTTS Serial Line Protocol and State Machine
 */

#ifndef __SNT_SERIAL_H__
#define __SNT_SERIAL_H__

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "snt_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SNT_SERIAL_MAX_LINE       1024
#define SNT_SERIAL_MAX_PHONEMES   512
#define SNT_SERIAL_ID_HISTORY_CAP 128

typedef enum {
    SNT_CMD_UNKNOWN = 0,
    SNT_CMD_HELLO,
    SNT_CMD_SAY,
    SNT_CMD_PHONEMES,
    SNT_CMD_GOLDEN,
    SNT_CMD_STOP,
    SNT_CMD_G2P
} snt_cmd_type_t;

typedef enum {
    SNT_REQ_STATE_IDLE = 0,
    SNT_REQ_STATE_ACCEPTED,
    SNT_REQ_STATE_SYNTHESIZING,
    SNT_REQ_STATE_PLAYING,
    SNT_REQ_STATE_DONE,
    SNT_REQ_STATE_CANCELLED,
    SNT_REQ_STATE_ERROR
} snt_req_state_t;

typedef struct {
    snt_cmd_type_t type;
    uint32_t id;
    snt_req_state_t state;
    char text[SNT_SERIAL_MAX_LINE];
    size_t text_len;
    int32_t phonemes[SNT_SERIAL_MAX_PHONEMES];
    size_t n_phonemes;
    uint64_t start_time_us;
} snt_request_t;

/* Callback when data needs to be transmitted to UART */
typedef void (*snt_serial_tx_fn)(const char *data, size_t len, void *user);

/* Callback when an active request should be executed by worker/engine */
typedef void (*snt_serial_exec_fn)(const snt_request_t *req, void *user);

/* Callback when cancellation is requested by STOP */
typedef void (*snt_serial_cancel_fn)(void *user);

typedef struct {
    snt_serial_tx_fn tx_fn;
    void *tx_user;
    snt_serial_exec_fn exec_fn;
    void *exec_user;
    snt_serial_cancel_fn cancel_fn;
    void *cancel_user;

    /* Line parsing buffer */
    char line_buf[SNT_SERIAL_MAX_LINE];
    size_t line_len;
    bool discarding_overflow;

    /* Request state */
    bool has_active;
    snt_request_t active_req;

    bool has_pending;
    snt_request_t pending_req;

    snt_request_t temp_req;

    /* ID history for duplicate checking */
    uint32_t id_history[SNT_SERIAL_ID_HISTORY_CAP];
    size_t id_history_count;

    /* Current active voice name */
    const char *voice_name;
} snt_serial_t;

size_t snt_serial_size(void);

void snt_serial_init(snt_serial_t *snt,
                     snt_serial_tx_fn tx_fn, void *tx_user,
                     snt_serial_exec_fn exec_fn, void *exec_user,
                     snt_serial_cancel_fn cancel_fn, void *cancel_user);

void snt_serial_set_voice(snt_serial_t *snt, const char *voice_name);

/* Feed incoming UART bytes into the parser */
void snt_serial_feed(snt_serial_t *snt, const uint8_t *data, size_t len);

/* Notify parser that active request has entered a new state */
void snt_serial_send_phonemes(snt_serial_t *snt, uint32_t id, const int32_t *phonemes, size_t n);
void snt_serial_notify_synthesizing(snt_serial_t *snt, uint32_t id);
void snt_serial_notify_playing(snt_serial_t *snt, uint32_t id);
void snt_serial_notify_done(snt_serial_t *snt, uint32_t id,
                            uint32_t samples, uint32_t source_rate,
                            uint64_t compute_us, uint64_t wall_us,
                            uint32_t underruns);
void snt_serial_notify_error(snt_serial_t *snt, uint32_t id,
                             int code, const char *msg);
void snt_serial_notify_cancelled(snt_serial_t *snt, uint32_t id);

/* Check if active request is cancelled */
bool snt_serial_is_active_cancelled(snt_serial_t *snt, uint32_t id);

/* Send formatted machine response (@SNT\t{...}\n) */
void snt_serial_send_json(snt_serial_t *snt, const char *json_fmt, ...);

#ifdef __cplusplus
}
#endif

#endif /* __SNT_SERIAL_H__ */
