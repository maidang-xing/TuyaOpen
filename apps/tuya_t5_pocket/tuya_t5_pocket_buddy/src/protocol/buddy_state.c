/**
 * @file buddy_state.c
 * @brief Mutex-protected shared state for Claude Buddy.
 *
 * Transport layer writes state under mutex lock.
 * Display layer reads atomic snapshots.
 *
 * @copyright Copyright (c) 2024-2026 Tuya Inc. All Rights Reserved.
 */

#include "buddy_protocol.h"
#include "tal_mutex.h"
#include "tal_system.h"
#include "tal_log.h"
#include "cJSON.h"
#include <string.h>

static buddy_tama_state_t s_state;
static MUTEX_HANDLE        s_mutex = NULL;

OPERATE_RET buddy_state_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    if (!s_mutex) {
        return tal_mutex_create_init(&s_mutex);
    }
    return OPRT_OK;
}

void buddy_state_snapshot(buddy_tama_state_t *out)
{
    if (!out || !s_mutex) {
        return;
    }
    tal_mutex_lock(s_mutex);
    memcpy(out, &s_state, sizeof(buddy_tama_state_t));
    tal_mutex_unlock(s_mutex);
}

void buddy_state_set_connected(bool connected)
{
    if (!s_mutex) return;
    tal_mutex_lock(s_mutex);
    s_state.ws_connected = connected;
    tal_mutex_unlock(s_mutex);
}

void buddy_state_set_owner(const char *name)
{
    if (!s_mutex || !name) return;
    tal_mutex_lock(s_mutex);
    strncpy(s_state.owner_name, name, sizeof(s_state.owner_name) - 1);
    s_state.owner_name[sizeof(s_state.owner_name) - 1] = '\0';
    tal_mutex_unlock(s_mutex);
}

void buddy_state_update_time(int64_t epoch_s, int16_t tz_min)
{
    if (!s_mutex) return;
    tal_mutex_lock(s_mutex);
    s_state.wall_epoch_s      = epoch_s;
    s_state.wall_tz_min       = tz_min;
    s_state.wall_local_ms_at_rx = tal_system_get_millisecond();
    tal_mutex_unlock(s_mutex);
}

void buddy_state_update_from_heartbeat(void *cjson_root)
{
    cJSON *root = (cJSON *)cjson_root;
    if (!s_mutex || !root) return;

    tal_mutex_lock(s_mutex);

    cJSON *item;

    #define GET_U8(field, key) \
        if ((item = cJSON_GetObjectItem(root, key)) && cJSON_IsNumber(item)) \
            s_state.field = (uint8_t)item->valueint;
    #define GET_U32(field, key) \
        if ((item = cJSON_GetObjectItem(root, key)) && cJSON_IsNumber(item)) \
            s_state.field = (uint32_t)item->valuedouble;
    #define GET_STR(field, key, maxlen) \
        if ((item = cJSON_GetObjectItem(root, key)) && cJSON_IsString(item)) { \
            strncpy(s_state.field, item->valuestring, maxlen); \
            s_state.field[maxlen] = '\0'; \
        }

    GET_U8(sessions_total, "total");
    GET_U8(sessions_running, "running");
    GET_U8(sessions_waiting, "waiting");
    GET_U32(tokens, "tokens");
    GET_U32(tokens_today, "tokens_today");
    GET_U32(tokens_in, "tokens_in");
    GET_U32(tokens_in_today, "tokens_in_today");
    GET_U32(cache_read, "cache_read");
    GET_U32(cache_write, "cache_write");
    GET_U32(ctx_used, "ctx_used");
    GET_U32(ctx_total, "ctx_total");
    GET_U32(cost_today_ucc, "cost_td");
    GET_U32(cost_total_ucc, "cost_all");
    GET_STR(model, "model", BUDDY_MODEL_LEN);
    GET_STR(claude_version, "ver", sizeof(s_state.claude_version) - 1);

    /* entries[] */
    cJSON *entries = cJSON_GetObjectItem(root, "entries");
    if (entries && cJSON_IsArray(entries)) {
        int count = cJSON_GetArraySize(entries);
        if (count > BUDDY_ENTRIES_RING) count = BUDDY_ENTRIES_RING;
        s_state.entries_count = (uint8_t)count;
        s_state.entries_head  = 0;
        for (int i = 0; i < count; i++) {
            cJSON *e = cJSON_GetArrayItem(entries, i);
            cJSON *t = cJSON_GetObjectItem(e, "t");
            cJSON *n = cJSON_GetObjectItem(e, "n");
            cJSON *h = cJSON_GetObjectItem(e, "h");
            s_state.entries[i].type = (t && cJSON_IsString(t) && t->valuestring[0]) ? t->valuestring[0] : '?';
            if (n && cJSON_IsString(n)) {
                strncpy(s_state.entries[i].name, n->valuestring, BUDDY_ENTRY_NAME_LEN);
                s_state.entries[i].name[BUDDY_ENTRY_NAME_LEN] = '\0';
            }
            if (h && cJSON_IsString(h)) {
                strncpy(s_state.entries[i].hint, h->valuestring, BUDDY_ENTRY_HINT_LEN);
                s_state.entries[i].hint[BUDDY_ENTRY_HINT_LEN] = '\0';
            }
        }
    }

    /* sessions[] */
    cJSON *sessions = cJSON_GetObjectItem(root, "sessions");
    if (sessions && cJSON_IsArray(sessions)) {
        int count = cJSON_GetArraySize(sessions);
        if (count > BUDDY_SESSIONS_MAX) count = BUDDY_SESSIONS_MAX;
        s_state.sessions_count = (uint8_t)count;
        for (int i = 0; i < count; i++) {
            cJSON *s = cJSON_GetArrayItem(sessions, i);
            buddy_session_t *ss = &s_state.sessions[i];
            memset(ss, 0, sizeof(*ss));

            cJSON *sid_item = cJSON_GetObjectItem(s, "sid");
            if (sid_item && cJSON_IsString(sid_item)) {
                strncpy(ss->sid, sid_item->valuestring, sizeof(ss->sid) - 1);
            }
            cJSON *name_item = cJSON_GetObjectItem(s, "name");
            if (name_item && cJSON_IsString(name_item)) {
                strncpy(ss->name, name_item->valuestring, BUDDY_SESSION_NAME_LEN);
            }
            cJSON *model_item = cJSON_GetObjectItem(s, "model");
            if (model_item && cJSON_IsString(model_item)) {
                strncpy(ss->model, model_item->valuestring, BUDDY_MODEL_LEN);
            }
            cJSON *proj_item = cJSON_GetObjectItem(s, "proj");
            if (proj_item && cJSON_IsString(proj_item)) {
                strncpy(ss->project, proj_item->valuestring, sizeof(ss->project) - 1);
            }

            cJSON *tok = cJSON_GetObjectItem(s, "tok");
            if (tok && cJSON_IsNumber(tok)) ss->tokens = (uint32_t)tok->valuedouble;

            cJSON *run = cJSON_GetObjectItem(s, "run");
            if (run) ss->is_running = cJSON_IsTrue(run);

            cJSON *ent = cJSON_GetObjectItem(s, "ent");
            if (ent && cJSON_IsArray(ent)) {
                int ec = cJSON_GetArraySize(ent);
                if (ec > BUDDY_SESSION_ENTRIES) ec = BUDDY_SESSION_ENTRIES;
                ss->local_entries_count = (uint8_t)ec;
                for (int j = 0; j < ec; j++) {
                    cJSON *ei = cJSON_GetArrayItem(ent, j);
                    if (ei && cJSON_IsString(ei)) {
                        strncpy(ss->local_entries[j], ei->valuestring, BUDDY_ENTRY_NAME_LEN);
                        ss->local_entries[j][BUDDY_ENTRY_NAME_LEN] = '\0';
                    }
                }
            }
        }
    }

    /* mstats[] */
    cJSON *mstats = cJSON_GetObjectItem(root, "mstats");
    if (mstats && cJSON_IsArray(mstats)) {
        int count = cJSON_GetArraySize(mstats);
        if (count > BUDDY_MSTATS_MAX) count = BUDDY_MSTATS_MAX;
        s_state.mstats_count = (uint8_t)count;
        for (int i = 0; i < count; i++) {
            cJSON *ms = cJSON_GetArrayItem(mstats, i);
            cJSON *m  = cJSON_GetObjectItem(ms, "m");
            cJSON *tk = cJSON_GetObjectItem(ms, "tok");
            if (m && cJSON_IsString(m)) {
                strncpy(s_state.mstats[i].model, m->valuestring, BUDDY_MODEL_LEN);
                s_state.mstats[i].model[BUDDY_MODEL_LEN] = '\0';
            }
            if (tk && cJSON_IsNumber(tk)) {
                s_state.mstats[i].tokens = (uint32_t)tk->valuedouble;
            }
        }
    }

    /* daily[] */
    cJSON *daily = cJSON_GetObjectItem(root, "daily");
    if (daily && cJSON_IsArray(daily)) {
        int count = cJSON_GetArraySize(daily);
        if (count > BUDDY_DAILY_HISTORY) count = BUDDY_DAILY_HISTORY;
        for (int i = 0; i < count; i++) {
            cJSON *d = cJSON_GetArrayItem(daily, i);
            if (d && cJSON_IsNumber(d)) {
                s_state.daily_tokens[i] = (uint32_t)d->valuedouble;
            }
        }
    }

    /* prompt */
    cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
    if (prompt && cJSON_IsObject(prompt)) {
        s_state.has_prompt = true;
        cJSON *pid = cJSON_GetObjectItem(prompt, "id");
        if (pid && cJSON_IsString(pid)) {
            strncpy(s_state.prompt_id, pid->valuestring, sizeof(s_state.prompt_id) - 1);
        }
        cJSON *ptool = cJSON_GetObjectItem(prompt, "tool");
        if (ptool && cJSON_IsString(ptool)) {
            strncpy(s_state.prompt_tool, ptool->valuestring, sizeof(s_state.prompt_tool) - 1);
        }
        cJSON *phint = cJSON_GetObjectItem(prompt, "hint");
        if (phint && cJSON_IsString(phint)) {
            strncpy(s_state.prompt_hint, phint->valuestring, sizeof(s_state.prompt_hint) - 1);
        }
    } else {
        s_state.has_prompt = false;
        s_state.prompt_id[0]   = '\0';
        s_state.prompt_tool[0] = '\0';
        s_state.prompt_hint[0] = '\0';
    }

    /* time */
    cJSON *time_arr = cJSON_GetObjectItem(root, "time");
    if (time_arr && cJSON_IsArray(time_arr) && cJSON_GetArraySize(time_arr) >= 2) {
        s_state.wall_epoch_s      = (int64_t)cJSON_GetArrayItem(time_arr, 0)->valuedouble;
        s_state.wall_tz_min       = (int16_t)cJSON_GetArrayItem(time_arr, 1)->valueint;
        s_state.wall_local_ms_at_rx = tal_system_get_millisecond();
    }

    #undef GET_U8
    #undef GET_U32
    #undef GET_STR

    PR_DEBUG("state_update: sess=%d run=%d tok=%u tdtok=%u model=%s prompt=%d",
             s_state.sessions_count, s_state.sessions_running,
             s_state.tokens, s_state.tokens_today,
             s_state.model, (int)s_state.has_prompt);

    tal_mutex_unlock(s_mutex);
}
