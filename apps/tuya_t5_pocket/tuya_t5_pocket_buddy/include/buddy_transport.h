/**
 * @file buddy_transport.h
 * @brief Transport layer interface for WebSocket communication.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef BUDDY_TRANSPORT_H
#define BUDDY_TRANSPORT_H

#include "buddy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

OPERATE_RET buddy_ws_init(void);
OPERATE_RET buddy_ws_start(void);
OPERATE_RET buddy_ws_stop(void);
bool        buddy_ws_is_connected(void);

OPERATE_RET buddy_ws_send_permission(const char *id, const char *decision);
OPERATE_RET buddy_ws_send_asr(const char *text, const char *sid);
OPERATE_RET buddy_ws_send_hb_req(const char *page);
OPERATE_RET buddy_ws_send_ack(const char *cmd);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_TRANSPORT_H */
