/**
 * @file buddy_protocol.c
 * @brief JSON frame dispatcher for Claude Buddy protocol.
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#include "buddy_protocol.h"
#include "cJSON.h"
#include "tal_log.h"

#define TAG "buddy_proto"

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
    }

    cJSON_Delete(root);
}
