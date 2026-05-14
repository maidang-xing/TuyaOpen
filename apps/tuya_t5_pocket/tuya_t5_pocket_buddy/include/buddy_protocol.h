/**
 * @file buddy_protocol.h
 * @brief Protocol layer interface for JSON frame parsing and state management.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef BUDDY_PROTOCOL_H
#define BUDDY_PROTOCOL_H

#include "buddy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*buddy_state_cb_t)(const buddy_tama_state_t *state);

OPERATE_RET buddy_protocol_init(void);
void        buddy_protocol_on_recv(const char *json_str);
void        buddy_protocol_set_state_cb(buddy_state_cb_t cb);

OPERATE_RET buddy_state_init(void);
void        buddy_state_snapshot(buddy_tama_state_t *out);
void        buddy_state_set_connected(bool connected);
void        buddy_state_update_from_heartbeat(void *cjson_root);
void        buddy_state_update_time(int64_t epoch_s, int16_t tz_min);
void        buddy_state_set_owner(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_PROTOCOL_H */
