/**
 * @file buddy_types.h
 * @brief Shared data structures for Claude Buddy.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef BUDDY_TYPES_H
#define BUDDY_TYPES_H

#include "tuya_cloud_types.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BUDDY_ENTRIES_RING       8
#define BUDDY_ENTRY_NAME_LEN     24
#define BUDDY_ENTRY_HINT_LEN     48
#define BUDDY_SESSIONS_MAX       12
#define BUDDY_SESSION_NAME_LEN   32
#define BUDDY_SESSION_PROJECT_LEN 16
#define BUDDY_SESSION_ENTRIES    4
#define BUDDY_MODEL_LEN          15
#define BUDDY_MSTATS_MAX         4
#define BUDDY_DAILY_HISTORY      28
#define BUDDY_PERSONA_COUNT      18

#define BUDDY_WS_DEFAULT_PORT    7681
#define BUDDY_WS_PATH            "/buddy"
#define BUDDY_WS_HOST_LEN        64

typedef struct {
    char type;
    char name[BUDDY_ENTRY_NAME_LEN + 1];
    char hint[BUDDY_ENTRY_HINT_LEN + 1];
} buddy_entry_t;

typedef struct {
    char     sid[16];
    char     name[BUDDY_SESSION_NAME_LEN + 1];
    char     model[BUDDY_MODEL_LEN + 1];
    uint32_t tokens;
    char     project[16];
    bool     is_running;
    char     local_entries[BUDDY_SESSION_ENTRIES][BUDDY_ENTRY_NAME_LEN + 1];
    uint8_t  local_entries_count;
} buddy_session_t;

typedef struct {
    char     model[BUDDY_MODEL_LEN + 1];
    uint32_t tokens;
} buddy_mstat_t;

typedef enum {
    BUDDY_PERSONA_STATE_SLEEP = 0,
    BUDDY_PERSONA_STATE_IDLE,
    BUDDY_PERSONA_STATE_BUSY,
    BUDDY_PERSONA_STATE_ATTENTION,
    BUDDY_PERSONA_STATE_CELEBRATE,
    BUDDY_PERSONA_STATE_HEART,
    BUDDY_PERSONA_STATE_DIZZY,
    BUDDY_PERSONA_STATE_COUNT,
} buddy_persona_state_e;

typedef enum {
    BUDDY_LED_STATE_OFF = 0,
    BUDDY_LED_STATE_ON_DIM,
    BUDDY_LED_STATE_BLINK_SLOW,
    BUDDY_LED_STATE_BLINK_FAST,
    BUDDY_LED_STATE_FLASH_ONCE,
    BUDDY_LED_STATE_COUNT,
} buddy_led_state_e;

typedef struct {
    bool     ws_connected;

    uint8_t  sessions_total;
    uint8_t  sessions_running;
    uint8_t  sessions_waiting;

    uint32_t tokens;
    uint32_t tokens_today;
    uint32_t tokens_in;
    uint32_t tokens_in_today;
    uint32_t cache_read;
    uint32_t cache_write;
    uint32_t ctx_used;
    uint32_t ctx_total;

    char     msg[64];
    char     owner_name[32];
    char     model[BUDDY_MODEL_LEN + 1];
    char     claude_version[20];
    uint32_t cost_today_ucc;
    uint32_t cost_total_ucc;

    bool     has_prompt;
    bool     recently_completed;
    char     prompt_id[40];
    char     prompt_tool[32];
    char     prompt_hint[64];

    buddy_entry_t entries[BUDDY_ENTRIES_RING];
    uint8_t       entries_count;
    uint8_t       entries_head;

    buddy_session_t sessions[BUDDY_SESSIONS_MAX];
    uint8_t         sessions_count;

    buddy_mstat_t mstats[BUDDY_MSTATS_MAX];
    uint8_t       mstats_count;

    uint32_t daily_tokens[BUDDY_DAILY_HISTORY];

    int64_t  wall_epoch_s;
    int16_t  wall_tz_min;
    uint64_t wall_local_ms_at_rx;

    uint8_t               persona_id;
    buddy_persona_state_e persona_state;
    buddy_led_state_e     led_state;
} buddy_tama_state_t;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_TYPES_H */
