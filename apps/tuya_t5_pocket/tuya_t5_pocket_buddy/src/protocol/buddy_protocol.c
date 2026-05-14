/**
 * @file buddy_protocol.c
 * @brief JSON frame dispatcher for Claude Buddy protocol.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#include "buddy_protocol.h"
#include "cJSON.h"
#include "tal_log.h"

#define TAG "buddy_proto"

static buddy_state_cb_t s_state_cb = NULL;

void buddy_protocol_set_state_cb(buddy_state_cb_t cb)
{
    s_state_cb = cb;
}

OPERATE_RET buddy_protocol_init(void)
{
    PR_INFO("buddy_protocol_init");
    return OPRT_OK;
}

void buddy_protocol_on_recv(const char *json_str)
{
    if (!json_str || !json_str[0]) return;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        PR_WARN("buddy_protocol: invalid JSON");
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cmd && cJSON_IsString(cmd)) {
        if (strcmp(cmd->valuestring, "owner") == 0) {
            cJSON *name = cJSON_GetObjectItem(root, "name");
            if (name && cJSON_IsString(name)) {
                buddy_state_set_owner(name->valuestring);
            }
        } else if (strcmp(cmd->valuestring, "status") == 0) {
            /* PC requesting device status -- handled by transport layer */
        }
    } else {
        /* No "cmd" field -- treat as heartbeat */
        buddy_state_update_from_heartbeat(root);

        /* Notify display layer so it can refresh immediately */
        if (s_state_cb) {
            buddy_tama_state_t snap;
            buddy_state_snapshot(&snap);
            PR_DEBUG("hb parsed: ws=%d sess=%d/%d tok=%u prompt=%d model=%s",
                     (int)snap.ws_connected, (int)snap.sessions_running,
                     (int)snap.sessions_count, (unsigned)snap.tokens,
                     (int)snap.has_prompt, snap.model);
            s_state_cb(&snap);
        } else {
            PR_WARN("hb: no state_cb registered!");
        }
    }

    cJSON_Delete(root);
}
