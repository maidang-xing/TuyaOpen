/**
 * @file buddy_ble.c
 * @brief Claude Desktop Buddy BLE (Nordic UART Service) bridge.
 * @version 5.0
 * @date 2026-04-21
 * @copyright Copyright (c) Tuya Inc.
 *
 * This module cooperates with the TAL BLE layer (which co-registers the NUS
 * service alongside Tuya's provisioning service at boot when
 * ENABLE_CLAUDE_DESKTOP_BUDDY_BLE=1). No runtime stack teardown is done:
 *   - We register a "sniffer" TAL callback to receive NUS GATT events.
 *   - On MQTT up, we disable Tuya's pair/monitor timers and publish
 *     advertising data that identifies the device as "Claude_XXXX" + NUS UUID.
 *   - Incoming JSON lines on NUS RX are dispatched.
 *   - Outgoing JSON lines are fragmented and notified on NUS TX.
 *
 * Protocol reference: https://github.com/anthropics/claude-desktop-buddy
 * See apps/tuya_t5_pocket/claude-desktop-buddy/REFERENCE.md
 */
#include "buddy_ble.h"
#include "buddy_main_screen.h"

#include "tal_api.h"
#include "tal_bluetooth.h"
#include "tkl_bluetooth.h"
#include "ble_mgr.h"
#include "tuya_iot.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define BUDDY_BLE_TAG "buddy_ble"

/* Advertising interval: 30-60 ms (units of 0.625 ms). */
#define BUDDY_BLE_ADV_INTERVAL_MIN 0x30
#define BUDDY_BLE_ADV_INTERVAL_MAX 0x60

/* Line buffer for inbound NUS writes: Claude can send ~4 KB per turn event;
 * give ourselves some headroom. */
#define BUDDY_BLE_RX_BUF_CAP 5120

/* Conservative notify payload defaults until we learn the negotiated MTU. */
#define BUDDY_BLE_DEFAULT_MTU      23
#define BUDDY_BLE_MAX_NOTIFY_CHUNK 180

/* ADV (31 B) + scan response (31 B) budgets. */
#define BUDDY_BLE_ADV_PAYLOAD_MAX 31
#define BUDDY_BLE_RSP_PAYLOAD_MAX 31

/* "Claude_XXXX" + NUL, plus headroom if the peer renames us. */
#define BUDDY_BLE_NAME_MAX 20

/* Nordic UART Service UUID, little-endian ordering used by the NimBLE adapter. */
#define BUDDY_NUS_UUID128_SVC                                                                                          \
    {                                                                                                                  \
        0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0, 0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E                 \
    }

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC BOOL_T s_initialized = FALSE;
STATIC BOOL_T s_started = FALSE;
STATIC MUTEX_HANDLE s_state_mutex = NULL;
STATIC buddy_tama_state_t s_state = {0};

STATIC uint16_t s_conn_handle = TKL_BLE_GATT_INVALID_HANDLE;
STATIC uint16_t s_mtu = BUDDY_BLE_DEFAULT_MTU;
STATIC uint16_t s_rx_char_handle = 0;
STATIC uint16_t s_tx_char_handle = 0;

STATIC uint8_t s_adv_buf[BUDDY_BLE_ADV_PAYLOAD_MAX];
STATIC uint8_t s_rsp_buf[BUDDY_BLE_RSP_PAYLOAD_MAX];
STATIC uint8_t s_adv_len = 0;
STATIC uint8_t s_rsp_len = 0;

STATIC char s_device_name[BUDDY_BLE_NAME_MAX] = "Claude";
STATIC char s_owner_name[24] = {0};

STATIC uint8_t *s_rx_buf = NULL;
STATIC uint16_t s_rx_len = 0;
STATIC MUTEX_HANDLE s_rx_mutex = NULL;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __push_ui_state(VOID_T);
STATIC VOID_T __reset_state(BOOL_T connected);
STATIC VOID_T __derive_name(VOID_T);
STATIC VOID_T __build_adv_payload(VOID_T);
STATIC OPERATE_RET __publish_adv(VOID_T);
STATIC VOID_T __refresh_nus_handles(VOID_T);
STATIC VOID_T __sniffer_cb(TAL_BLE_EVT_PARAMS_T *p_event);
STATIC VOID_T __rx_accumulate(const uint8_t *data, uint16_t len);
STATIC VOID_T __rx_dispatch_lines(VOID_T);
STATIC VOID_T __handle_line(char *line);
STATIC VOID_T __handle_heartbeat(cJSON *root);
STATIC VOID_T __handle_time(cJSON *time_arr);
STATIC VOID_T __reset_entries(buddy_tama_state_t *snap);
STATIC VOID_T __push_entry(buddy_tama_state_t *snap, int index, const char *text);
STATIC VOID_T __send_ack(const char *ack, BOOL_T ok, int n);
STATIC OPERATE_RET __send_raw(const char *payload, uint16_t length);
STATIC VOID_T __send_status(VOID_T);

/* ---------------------------------------------------------------------------
 * UI / state helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief Reset the buddy runtime state to a sensible default and push to UI.
 * @param[in] connected whether the peer is currently connected
 * @return none
 */
STATIC VOID_T __reset_state(BOOL_T connected)
{
    buddy_tama_state_t fresh = {0};
    fresh.ble_connected = connected;
    snprintf(fresh.msg, sizeof(fresh.msg), connected ? "waiting for data" : "waiting for claude");
    if (s_state_mutex) {
        tal_mutex_lock(s_state_mutex);
    }
    s_state = fresh;
    if (s_state_mutex) {
        tal_mutex_unlock(s_state_mutex);
    }
    __push_ui_state();
}

/**
 * @brief Copy the current state into the UI.
 * @return none
 */
STATIC VOID_T __push_ui_state(VOID_T)
{
    buddy_tama_state_t snap;
    buddy_ble_snapshot(&snap);
    buddy_main_screen_update_state(&snap);
}

/**
 * @brief Public: copy the current state snapshot.
 * @param[out] out target
 * @return none
 */
VOID_T buddy_ble_snapshot(buddy_tama_state_t *out)
{
    if (out == NULL) {
        return;
    }
    if (s_state_mutex) {
        tal_mutex_lock(s_state_mutex);
    }
    memcpy(out, &s_state, sizeof(*out));
    /* device / owner names live outside s_state to keep them stable across
     * heartbeat merges; copy them in so the UI sees a consistent view. */
    strncpy(out->device_name, s_device_name, sizeof(out->device_name) - 1);
    out->device_name[sizeof(out->device_name) - 1] = '\0';
    strncpy(out->owner_name, s_owner_name, sizeof(out->owner_name) - 1);
    out->owner_name[sizeof(out->owner_name) - 1] = '\0';
    if (s_state_mutex) {
        tal_mutex_unlock(s_state_mutex);
    }
}

/**
 * @brief Public: connected flag.
 * @return TRUE if the desktop host is currently connected
 */
BOOL_T buddy_ble_is_connected(VOID_T)
{
    return (s_conn_handle != TKL_BLE_GATT_INVALID_HANDLE);
}

/**
 * @brief Public: started flag.
 * @return TRUE if Claude-mode advertising has been taken over
 */
BOOL_T buddy_ble_is_started(VOID_T)
{
    return s_started;
}

/* ---------------------------------------------------------------------------
 * Naming / advertising
 * --------------------------------------------------------------------------- */
/**
 * @brief Derive a device name "Claude_XXXX" from the current BLE address.
 * @return none
 */
STATIC VOID_T __derive_name(VOID_T)
{
    /* 1) BLE MAC (may be zero on platforms where the TKL stub doesn't
     *    populate it). */
    TAL_BLE_ADDR_T addr = {0};
    if (tal_ble_address_get(&addr) == OPRT_OK) {
        uint8_t hi = addr.addr[1];
        uint8_t lo = addr.addr[0];
        if ((hi | lo) != 0) {
            snprintf(s_device_name, sizeof(s_device_name), "Claude_%02X%02X", hi, lo);
            s_device_name[BUDDY_BLE_NAME_MAX - 1] = '\0';
            return;
        }
    }

    /* 2) Fall back to the last 4 chars of the Tuya device UUID / devid
     *    once the device is activated. This keeps the name stable and
     *    distinguishable across units. */
    tuya_iot_client_t *iot = tuya_iot_client_get();
    if (iot != NULL) {
        const char *src = (iot->activate.devid[0] != '\0') ? iot->activate.devid : iot->config.uuid;
        if (src != NULL && src[0] != '\0') {
            size_t len = strlen(src);
            const char *suffix = (len >= 4) ? (src + len - 4) : src;
            snprintf(s_device_name, sizeof(s_device_name), "Claude_%s", suffix);
            s_device_name[BUDDY_BLE_NAME_MAX - 1] = '\0';
            return;
        }
    }

    /* 3) Last resort: generic label. */
    snprintf(s_device_name, sizeof(s_device_name), "Claude");
    s_device_name[BUDDY_BLE_NAME_MAX - 1] = '\0';
}

/**
 * @brief Compose advertisement bytes (flags + 128-bit UUID) and scan response
 *        bytes (complete local name).
 * @return none
 */
STATIC VOID_T __build_adv_payload(VOID_T)
{
    CONST uint8_t nus_uuid[16] = BUDDY_NUS_UUID128_SVC;

    uint8_t *p = s_adv_buf;
    uint8_t *end = s_adv_buf + sizeof(s_adv_buf);

    if ((p + 3) <= end) {
        *p++ = 0x02;
        *p++ = 0x01;
        *p++ = 0x06;
    }
    if ((p + 2 + 16) <= end) {
        *p++ = 0x11;
        *p++ = 0x07;
        memcpy(p, nus_uuid, 16);
        p += 16;
    }
    s_adv_len = (uint8_t)(p - s_adv_buf);

    uint8_t *r = s_rsp_buf;
    uint8_t *rend = s_rsp_buf + sizeof(s_rsp_buf);
    size_t name_len = strnlen(s_device_name, BUDDY_BLE_NAME_MAX - 1);
    if ((1u + 1u + name_len) <= (size_t)(rend - r)) {
        *r++ = (uint8_t)(1u + name_len);
        *r++ = 0x09;
        memcpy(r, s_device_name, name_len);
        r += name_len;
    }
    s_rsp_len = (uint8_t)(r - s_rsp_buf);
}

/**
 * @brief Push adv/scan response data to TAL and (re)start advertising.
 * @return OPRT_OK on success, or the underlying TAL error code
 */
STATIC OPERATE_RET __publish_adv(VOID_T)
{
    TAL_BLE_DATA_T adv_data = {.len = s_adv_len, .p_data = s_adv_buf};
    TAL_BLE_DATA_T rsp_data = {.len = s_rsp_len, .p_data = s_rsp_buf};

    (VOID_T)tal_ble_advertising_stop();

    OPERATE_RET rt = tal_ble_advertising_data_set(&adv_data, &rsp_data);
    if (rt != OPRT_OK) {
        PR_WARN("%s adv_data_set rt=%d", BUDDY_BLE_TAG, rt);
    }

    TAL_BLE_ADV_PARAMS_T params = {
        .adv_type = TAL_BLE_ADV_TYPE_CS_UNDIR,
        .adv_interval_min = BUDDY_BLE_ADV_INTERVAL_MIN,
        .adv_interval_max = BUDDY_BLE_ADV_INTERVAL_MAX,
    };
    rt = tal_ble_advertising_start(&params);
    if (rt != OPRT_OK) {
        PR_WARN("%s adv_start rt=%d", BUDDY_BLE_TAG, rt);
    }
    return rt;
}

/**
 * @brief Refresh cached NUS RX/TX handles from TAL. Handles are assigned when
 *        the GATT stack initializes, so this may fail silently before boot
 *        is complete; re-call on connect.
 * @return none
 */
STATIC VOID_T __refresh_nus_handles(VOID_T)
{
    uint16_t rx = 0;
    uint16_t tx = 0;
    if (tal_ble_claude_handles_get(&rx, &tx) == OPRT_OK) {
        s_rx_char_handle = rx;
        s_tx_char_handle = tx;
    }
}

/* ---------------------------------------------------------------------------
 * TAL sniffer callback
 * --------------------------------------------------------------------------- */
/**
 * @brief Auxiliary TAL BLE event callback. Observes connect/disconnect/MTU
 *        and filters GATT writes to the NUS RX characteristic for us.
 * @param[in] p_event event payload (shared with Tuya's ble_mgr, must not mutate)
 * @return none
 */
STATIC VOID_T __sniffer_cb(TAL_BLE_EVT_PARAMS_T *p_event)
{
    if (p_event == NULL) {
        return;
    }

    switch (p_event->type) {
    case TAL_BLE_EVT_PERIPHERAL_CONNECT: {
        if (p_event->ble_event.connect.result != 0) {
            break;
        }
        s_conn_handle = p_event->ble_event.connect.peer.conn_handle;
        s_mtu = BUDDY_BLE_DEFAULT_MTU;
        __refresh_nus_handles();
        if (s_rx_mutex) {
            tal_mutex_lock(s_rx_mutex);
        }
        s_rx_len = 0;
        if (s_rx_mutex) {
            tal_mutex_unlock(s_rx_mutex);
        }
        PR_NOTICE("%s peer connected conn=%u (rx=%u tx=%u)", BUDDY_BLE_TAG, (unsigned)s_conn_handle,
                  (unsigned)s_rx_char_handle, (unsigned)s_tx_char_handle);
        __reset_state(TRUE);
    } break;

    case TAL_BLE_EVT_DISCONNECT: {
        PR_NOTICE("%s peer disconnected", BUDDY_BLE_TAG);
        s_conn_handle = TKL_BLE_GATT_INVALID_HANDLE;
        s_mtu = BUDDY_BLE_DEFAULT_MTU;
        __reset_state(FALSE);
        if (s_started) {
            (VOID_T)__publish_adv();
        }
    } break;

    case TAL_BLE_EVT_MTU_REQUEST: {
        s_mtu = p_event->ble_event.exchange_mtu.mtu;
        PR_INFO("%s mtu=%u", BUDDY_BLE_TAG, (unsigned)s_mtu);
    } break;

    case TAL_BLE_EVT_WRITE_REQ: {
        uint16_t char_handle = p_event->ble_event.write_report.peer.char_handle[0];
        if (s_rx_char_handle == 0) {
            __refresh_nus_handles();
        }
        if (char_handle != s_rx_char_handle) {
            /* Not our NUS RX write - leave it for Tuya's ble_mgr. */
            break;
        }
        __rx_accumulate(p_event->ble_event.write_report.report.p_data,
                        p_event->ble_event.write_report.report.len);
        __rx_dispatch_lines();
    } break;

    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * RX line buffer / JSON dispatcher
 * --------------------------------------------------------------------------- */
/**
 * @brief Append incoming NUS bytes to the line buffer, discarding on overflow.
 * @param[in] data  pointer to bytes
 * @param[in] len   byte count
 * @return none
 */
STATIC VOID_T __rx_accumulate(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0 || s_rx_buf == NULL) {
        return;
    }
    if (s_rx_mutex) {
        tal_mutex_lock(s_rx_mutex);
    }
    if ((uint32_t)s_rx_len + len >= BUDDY_BLE_RX_BUF_CAP) {
        /* Protocol is newline-delimited; on overflow start fresh rather than
         * corrupt the next JSON frame. */
        PR_WARN("%s rx overflow, reset (len=%u+%u)", BUDDY_BLE_TAG, (unsigned)s_rx_len, (unsigned)len);
        s_rx_len = 0;
    }
    memcpy(s_rx_buf + s_rx_len, data, len);
    s_rx_len = (uint16_t)(s_rx_len + len);
    if (s_rx_mutex) {
        tal_mutex_unlock(s_rx_mutex);
    }
}

/**
 * @brief Scan the buffer for newline-terminated frames, dispatch each, and
 *        keep any trailing partial bytes.
 * @return none
 */
STATIC VOID_T __rx_dispatch_lines(VOID_T)
{
    if (s_rx_buf == NULL) {
        return;
    }

    for (;;) {
        char *line = NULL;
        uint16_t consumed = 0;

        if (s_rx_mutex) {
            tal_mutex_lock(s_rx_mutex);
        }
        uint16_t i = 0;
        while (i < s_rx_len && s_rx_buf[i] != '\n') {
            i++;
        }
        if (i >= s_rx_len) {
            if (s_rx_mutex) {
                tal_mutex_unlock(s_rx_mutex);
            }
            break;
        }
        consumed = (uint16_t)(i + 1);
        line = (char *)tal_malloc(i + 1);
        if (line == NULL) {
            PR_ERR("%s oom on line %u", BUDDY_BLE_TAG, (unsigned)i);
            s_rx_len = 0;
            if (s_rx_mutex) {
                tal_mutex_unlock(s_rx_mutex);
            }
            return;
        }
        memcpy(line, s_rx_buf, i);
        line[i] = '\0';
        if (consumed < s_rx_len) {
            memmove(s_rx_buf, s_rx_buf + consumed, s_rx_len - consumed);
        }
        s_rx_len = (uint16_t)(s_rx_len - consumed);
        if (s_rx_mutex) {
            tal_mutex_unlock(s_rx_mutex);
        }

        __handle_line(line);
        tal_free(line);
    }
}

/**
 * @brief Copy a cJSON string (if present) into a fixed buffer with clamp.
 * @param[out] dst destination buffer
 * @param[in] dst_size buffer size in bytes
 * @param[in] src cJSON node
 * @return none
 */
STATIC VOID_T __copy_str(char *dst, size_t dst_size, const cJSON *src)
{
    if (dst == NULL || dst_size == 0) {
        return;
    }
    if (cJSON_IsString(src) && src->valuestring != NULL) {
        strncpy(dst, src->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    } else {
        dst[0] = '\0';
    }
}

/**
 * @brief Clear the entries ring on the given staging snapshot.
 * @param[in,out] snap staging snapshot owned by the heartbeat parser
 * @return none
 * @note Called whenever a heartbeat carries an `entries` field, even if it
 *       is empty, so stale transcript data cannot leak across peers.
 */
STATIC VOID_T __reset_entries(buddy_tama_state_t *snap)
{
    if (snap == NULL) {
        return;
    }
    snap->entries_count = 0;
    snap->entries_head = 0;
    for (uint8_t i = 0; i < BUDDY_ENTRIES_RING; i++) {
        snap->entries[i].text[0] = '\0';
    }
}

/**
 * @brief Append one entry to the ring with bounded copy.
 * @param[in,out] snap  staging snapshot owned by the heartbeat parser
 * @param[in]     index caller-side source index (for the DEBUG anchor)
 * @param[in]     text  UTF-8 string (NUL-terminated); truncated at
 *                      BUDDY_ENTRY_MAX_CHARS bytes
 * @return none
 * @note No unbounded string ops: uses snprintf so the destination is always
 *       NUL-terminated regardless of input length (TuyaOS C security rule).
 *       Input longer than the cap is silently truncated; callers must have
 *       already validated `text` is a cJSON string node.
 */
STATIC VOID_T __push_entry(buddy_tama_state_t *snap, int index, const char *text)
{
    if (snap == NULL || text == NULL) {
        return;
    }
    uint8_t slot;
    if (snap->entries_count == 0) {
        slot = 0;
        snap->entries_head = 0;
    } else {
        snap->entries_head = (uint8_t)((snap->entries_head + 1U) % BUDDY_ENTRIES_RING);
        slot = snap->entries_head;
    }
    (VOID_T)snprintf(snap->entries[slot].text, sizeof(snap->entries[slot].text), "%s", text);
    if (snap->entries_count < BUDDY_ENTRIES_RING) {
        snap->entries_count = (uint8_t)(snap->entries_count + 1U);
    }
    PR_DEBUG("%s entries: idx=%d text=%.80s", BUDDY_BLE_TAG, index, snap->entries[slot].text);
}

/**
 * @brief Parse a `{"time":[epoch, tz]}` frame and update the UI offset.
 * @param[in] time_arr cJSON array node (may be NULL / not an array)
 * @return none
 * @note Stores epoch seconds + signed tz minutes + local receive timestamp
 *       for later UI-side HH:MM rendering.  Does NOT touch the TuyaOS RTC.
 *       All math uses int64_t to avoid UINT32_T wrap on long deltas.
 */
STATIC VOID_T __handle_time(cJSON *time_arr)
{
    if (!cJSON_IsArray(time_arr) || cJSON_GetArraySize(time_arr) < 2) {
        return;
    }
    cJSON *epoch = cJSON_GetArrayItem(time_arr, 0);
    cJSON *tz = cJSON_GetArrayItem(time_arr, 1);
    if (!cJSON_IsNumber(epoch) || !cJSON_IsNumber(tz)) {
        return;
    }

    int64_t epoch_s = (int64_t)epoch->valuedouble;
    /* tz value is in seconds per REFERENCE.md; store as minutes internally */
    int16_t tz_min = (int16_t)(tz->valuedouble / 60.0);
    uint64_t now_ms = tal_system_get_millisecond();

    if (s_state_mutex) {
        tal_mutex_lock(s_state_mutex);
    }
    s_state.wall_epoch_s = epoch_s;
    s_state.wall_tz_min = tz_min;
    s_state.wall_local_ms_at_rx = now_ms;
    if (s_state_mutex) {
        tal_mutex_unlock(s_state_mutex);
    }
    PR_DEBUG("%s time sync ok epoch=%lld tz=%d", BUDDY_BLE_TAG, (long long)epoch_s, (int)tz_min);
}

/**
 * @brief Parse and dispatch a single JSON line from the desktop host.
 * @param[in] line NUL-terminated JSON text (no trailing newline)
 * @return none
 */
STATIC VOID_T __handle_line(char *line)
{
    if (line == NULL || line[0] == '\0') {
        return;
    }
    PR_DEBUG("%s RX %s", BUDDY_BLE_TAG, line);

    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        PR_WARN("%s bad json", BUDDY_BLE_TAG);
        return;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");
    if (cJSON_IsString(cmd) && cmd->valuestring != NULL) {
        if (strcmp(cmd->valuestring, "status") == 0) {
            __send_status();
        } else if (strcmp(cmd->valuestring, "name") == 0) {
            cJSON *n = cJSON_GetObjectItem(root, "name");
            if (cJSON_IsString(n) && n->valuestring != NULL) {
                strncpy(s_device_name, n->valuestring, sizeof(s_device_name) - 1);
                s_device_name[sizeof(s_device_name) - 1] = '\0';
            }
            __send_ack("name", TRUE, 0);
        } else if (strcmp(cmd->valuestring, "owner") == 0) {
            cJSON *n = cJSON_GetObjectItem(root, "name");
            if (cJSON_IsString(n) && n->valuestring != NULL) {
                strncpy(s_owner_name, n->valuestring, sizeof(s_owner_name) - 1);
                s_owner_name[sizeof(s_owner_name) - 1] = '\0';
            }
            __send_ack("owner", TRUE, 0);
        } else if (strcmp(cmd->valuestring, "unpair") == 0) {
            __send_ack("unpair", TRUE, 0);
        } else {
            /* char_begin/file/chunk/file_end/char_end: decline by not acking. */
            PR_DEBUG("%s unhandled cmd '%s'", BUDDY_BLE_TAG, cmd->valuestring);
        }
        cJSON_Delete(root);
        return;
    }

    __handle_time(cJSON_GetObjectItem(root, "time"));
    __handle_heartbeat(root);
    cJSON_Delete(root);
}

/**
 * @brief Apply a heartbeat snapshot to the UI state.
 * @param[in] root parsed JSON object
 * @return none
 */
STATIC VOID_T __handle_heartbeat(cJSON *root)
{
    if (root == NULL) {
        return;
    }

    buddy_tama_state_t snap;
    buddy_ble_snapshot(&snap);
    snap.ble_connected = TRUE;

    cJSON *total = cJSON_GetObjectItem(root, "total");
    cJSON *running = cJSON_GetObjectItem(root, "running");
    cJSON *waiting = cJSON_GetObjectItem(root, "waiting");
    cJSON *tokens = cJSON_GetObjectItem(root, "tokens");
    cJSON *tokens_today = cJSON_GetObjectItem(root, "tokens_today");
    cJSON *tokens_in = cJSON_GetObjectItem(root, "tokens_in");
    cJSON *tokens_in_today = cJSON_GetObjectItem(root, "tokens_in_today");
    cJSON *cache_read = cJSON_GetObjectItem(root, "cache_read");
    cJSON *cache_write = cJSON_GetObjectItem(root, "cache_write");
    cJSON *ctx_used = cJSON_GetObjectItem(root, "ctx_used");
    cJSON *ctx_total = cJSON_GetObjectItem(root, "ctx_total");
    cJSON *msg = cJSON_GetObjectItem(root, "msg");

    if (cJSON_IsNumber(total)) {
        snap.sessions_total = (uint8_t)total->valueint;
    }
    if (cJSON_IsNumber(running)) {
        snap.sessions_running = (uint8_t)running->valueint;
    }
    if (cJSON_IsNumber(waiting)) {
        snap.sessions_waiting = (uint8_t)waiting->valueint;
    }
    if (cJSON_IsNumber(tokens)) {
        snap.tokens = (uint32_t)tokens->valuedouble;
    }
    if (cJSON_IsNumber(tokens_today)) {
        snap.tokens_today = (uint32_t)tokens_today->valuedouble;
    }
    if (cJSON_IsNumber(tokens_in)) {
        snap.tokens_in = (uint32_t)tokens_in->valuedouble;
    }
    if (cJSON_IsNumber(tokens_in_today)) {
        snap.tokens_in_today = (uint32_t)tokens_in_today->valuedouble;
    }
    if (cJSON_IsNumber(cache_read)) {
        snap.cache_read = (uint32_t)cache_read->valuedouble;
    }
    if (cJSON_IsNumber(cache_write)) {
        snap.cache_write = (uint32_t)cache_write->valuedouble;
    }
    if (cJSON_IsNumber(ctx_used)) {
        snap.ctx_used = (uint32_t)ctx_used->valuedouble;
    }
    if (cJSON_IsNumber(ctx_total)) {
        snap.ctx_total = (uint32_t)ctx_total->valuedouble;
    }
    __copy_str(snap.msg, sizeof(snap.msg), msg);

    cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
    if (cJSON_IsObject(prompt)) {
        __copy_str(snap.prompt_id, sizeof(snap.prompt_id), cJSON_GetObjectItem(prompt, "id"));
        __copy_str(snap.prompt_tool, sizeof(snap.prompt_tool), cJSON_GetObjectItem(prompt, "tool"));
        __copy_str(snap.prompt_hint, sizeof(snap.prompt_hint), cJSON_GetObjectItem(prompt, "hint"));
        snap.has_prompt = (snap.prompt_id[0] != '\0');
    } else {
        snap.has_prompt = FALSE;
        snap.prompt_id[0] = '\0';
    }

    cJSON *entries = cJSON_GetObjectItem(root, "entries");
    if (cJSON_IsArray(entries)) {
        int n = cJSON_GetArraySize(entries);
        int first = (n > BUDDY_ENTRIES_RING) ? (n - BUDDY_ENTRIES_RING) : 0;
        __reset_entries(&snap);
        for (int i = first; i < n; i++) {
            cJSON *e = cJSON_GetArrayItem(entries, i);
            if (cJSON_IsString(e) && e->valuestring != NULL) {
                __push_entry(&snap, i, e->valuestring);
            }
        }
    }

    /* model name */
    __copy_str(snap.model, sizeof(snap.model), cJSON_GetObjectItem(root, "model"));

    /* sessions array: [{"id":..,"n":..,"m":..,"to":uint,"r":bool}] */
    snap.sessions_count = 0;
    cJSON *jsessions = cJSON_GetObjectItem(root, "sessions");
    if (cJSON_IsArray(jsessions)) {
        int n = cJSON_GetArraySize(jsessions);
        if (n > BUDDY_SESSIONS_MAX) n = BUDDY_SESSIONS_MAX;
        for (int i = 0; i < n; i++) {
            cJSON *js = cJSON_GetArrayItem(jsessions, i);
            if (!cJSON_IsObject(js)) continue;
            buddy_session_t *sess = &snap.sessions[snap.sessions_count];
            memset(sess, 0, sizeof(*sess));
            __copy_str(sess->sid,   sizeof(sess->sid),   cJSON_GetObjectItem(js, "id"));
            __copy_str(sess->name,  sizeof(sess->name),  cJSON_GetObjectItem(js, "n"));
            __copy_str(sess->model, sizeof(sess->model), cJSON_GetObjectItem(js, "m"));
            cJSON *jto = cJSON_GetObjectItem(js, "to");
            cJSON *jr  = cJSON_GetObjectItem(js, "r");
            if (cJSON_IsNumber(jto)) sess->tokens_out = (uint32_t)jto->valuedouble;
            sess->is_running = cJSON_IsTrue(jr) ? TRUE : FALSE;
            snap.sessions_count++;
        }
    }

    /* mstats array: [{"m":..,"to":uint}] */
    snap.mstats_count = 0;
    cJSON *jmstats = cJSON_GetObjectItem(root, "mstats");
    if (cJSON_IsArray(jmstats)) {
        int n = cJSON_GetArraySize(jmstats);
        if (n > BUDDY_MSTATS_MAX) n = BUDDY_MSTATS_MAX;
        for (int i = 0; i < n; i++) {
            cJSON *jm = cJSON_GetArrayItem(jmstats, i);
            if (!cJSON_IsObject(jm)) continue;
            buddy_mstat_t *ms = &snap.mstats[snap.mstats_count];
            memset(ms, 0, sizeof(*ms));
            __copy_str(ms->model, sizeof(ms->model), cJSON_GetObjectItem(jm, "m"));
            cJSON *jto = cJSON_GetObjectItem(jm, "to");
            if (cJSON_IsNumber(jto)) ms->tokens_out = (uint32_t)jto->valuedouble;
            snap.mstats_count++;
        }
    }

    if (s_state_mutex) {
        tal_mutex_lock(s_state_mutex);
    }
    s_state = snap;
    if (s_state_mutex) {
        tal_mutex_unlock(s_state_mutex);
    }
    __push_ui_state();
}

/* ---------------------------------------------------------------------------
 * TX helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief Send a raw payload over the TX characteristic, fragmenting to fit
 *        the negotiated MTU (notify payload = MTU - 3, capped for safety).
 * @param[in] payload pointer to bytes
 * @param[in] length  byte count
 * @return OPRT_OK on success, OPRT_COM_ERROR if no peer, or the TKL error
 */
STATIC OPERATE_RET __send_raw(const char *payload, uint16_t length)
{
    if (s_conn_handle == TKL_BLE_GATT_INVALID_HANDLE) {
        return OPRT_COM_ERROR;
    }
    if (payload == NULL || length == 0) {
        return OPRT_INVALID_PARM;
    }
    if (s_tx_char_handle == 0) {
        __refresh_nus_handles();
        if (s_tx_char_handle == 0) {
            return OPRT_COM_ERROR;
        }
    }

    uint16_t chunk = (s_mtu > 3) ? (uint16_t)(s_mtu - 3) : 20;
    if (chunk > BUDDY_BLE_MAX_NOTIFY_CHUNK) {
        chunk = BUDDY_BLE_MAX_NOTIFY_CHUNK;
    }

    uint16_t sent = 0;
    while (sent < length) {
        uint16_t n = (uint16_t)(length - sent);
        if (n > chunk) {
            n = chunk;
        }
        OPERATE_RET rt =
            tkl_ble_gatts_value_notify(s_conn_handle, s_tx_char_handle, (uint8_t *)(payload + sent), n);
        if (rt != OPRT_OK) {
            PR_WARN("%s notify rt=%d (sent=%u/%u)", BUDDY_BLE_TAG, rt, (unsigned)sent, (unsigned)length);
            return rt;
        }
        sent = (uint16_t)(sent + n);
        tal_system_sleep(6);
    }
    return OPRT_OK;
}

/**
 * @brief Send a trivial {"ack":"...","ok":...,"n":...} line.
 * @param[in] ack command name to echo
 * @param[in] ok  TRUE for success
 * @param[in] n   generic counter
 * @return none
 */
STATIC VOID_T __send_ack(const char *ack, BOOL_T ok, int n)
{
    char buf[96];
    int wrote = snprintf(buf, sizeof(buf), "{\"ack\":\"%s\",\"ok\":%s,\"n\":%d}\n", ack, ok ? "true" : "false", n);
    if (wrote > 0 && wrote < (int)sizeof(buf)) {
        (VOID_T)__send_raw(buf, (uint16_t)wrote);
    }
}

/**
 * @brief Send a status ack with the device's current metrics.
 * @return none
 */
STATIC VOID_T __send_status(VOID_T)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }
    cJSON_AddStringToObject(root, "ack", "status");
    cJSON_AddBoolToObject(root, "ok", TRUE);

    cJSON *data = cJSON_AddObjectToObject(root, "data");
    if (data != NULL) {
        cJSON_AddStringToObject(data, "name", s_device_name);
        cJSON_AddBoolToObject(data, "sec", FALSE);

        cJSON *sys = cJSON_AddObjectToObject(data, "sys");
        if (sys != NULL) {
            uint32_t up = (uint32_t)(tal_system_get_millisecond() / 1000);
            cJSON_AddNumberToObject(sys, "up", up);
        }
    }

    char *out = cJSON_PrintUnformatted(root);
    if (out != NULL) {
        size_t len = strlen(out);
        if (len + 2 < 512) {
            char *buf = (char *)tal_malloc(len + 2);
            if (buf != NULL) {
                memcpy(buf, out, len);
                buf[len] = '\n';
                buf[len + 1] = '\0';
                (VOID_T)__send_raw(buf, (uint16_t)(len + 1));
                tal_free(buf);
            }
        }
        cJSON_free(out);
    }
    cJSON_Delete(root);
}

/**
 * @brief Public: send a permission decision.
 * @param[in] prompt_id  echoed id from the original prompt
 * @param[in] decision   "once" / "always" / "deny"
 * @return OPRT_OK on success
 */
OPERATE_RET buddy_ble_send_permission(const char *prompt_id, const char *decision)
{
    if (prompt_id == NULL || decision == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (s_conn_handle == TKL_BLE_GATT_INVALID_HANDLE) {
        return OPRT_COM_ERROR;
    }
    char buf[192];
    int n = snprintf(buf, sizeof(buf), "{\"cmd\":\"permission\",\"id\":\"%s\",\"decision\":\"%s\"}\n", prompt_id,
                     decision);
    if (n <= 0 || n >= (int)sizeof(buf)) {
        return OPRT_COM_ERROR;
    }
    return __send_raw(buf, (uint16_t)n);
}

/**
 * @brief Public: send a minimal `{"cmd":"..."}` poke to the desktop.
 * @param[in] cmd ASCII command token
 * @return OPRT_OK on success
 * @note Only intended for short, hard-coded tokens like "status".  The
 *       value is not escaped: do not forward untrusted input here.
 */
OPERATE_RET buddy_ble_send_cmd(const char *cmd)
{
    if (cmd == NULL) {
        return OPRT_INVALID_PARM;
    }
    size_t cmd_len = strlen(cmd);
    if (cmd_len == 0 || cmd_len > 32) {
        return OPRT_INVALID_PARM;
    }
    /* Reject any byte that would produce an invalid JSON string. */
    for (size_t i = 0; i < cmd_len; i++) {
        unsigned char c = (unsigned char)cmd[i];
        if (c == '"' || c == '\\' || c < 0x20) {
            return OPRT_INVALID_PARM;
        }
    }
    if (s_conn_handle == TKL_BLE_GATT_INVALID_HANDLE) {
        return OPRT_COM_ERROR;
    }
    char buf[64];
    int n = snprintf(buf, sizeof(buf), "{\"cmd\":\"%s\"}\n", cmd);
    if (n <= 0 || n >= (int)sizeof(buf)) {
        return OPRT_COM_ERROR;
    }
    return __send_raw(buf, (uint16_t)n);
}

/* ---------------------------------------------------------------------------
 * Public entry points
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize mutexes, RX buffer, and register the TAL sniffer callback.
 * @return OPRT_OK on success
 */
OPERATE_RET buddy_ble_init(VOID_T)
{
    if (s_initialized) {
        return OPRT_OK;
    }

    OPERATE_RET rt = tal_mutex_create_init(&s_state_mutex);
    if (rt != OPRT_OK) {
        PR_ERR("%s state mutex rt=%d", BUDDY_BLE_TAG, rt);
        return rt;
    }
    rt = tal_mutex_create_init(&s_rx_mutex);
    if (rt != OPRT_OK) {
        PR_ERR("%s rx mutex rt=%d", BUDDY_BLE_TAG, rt);
        return rt;
    }

    s_rx_buf = (uint8_t *)tal_malloc(BUDDY_BLE_RX_BUF_CAP);
    if (s_rx_buf == NULL) {
        PR_ERR("%s rx buf oom", BUDDY_BLE_TAG);
        return OPRT_MALLOC_FAILED;
    }
    memset(s_rx_buf, 0, BUDDY_BLE_RX_BUF_CAP);
    s_rx_len = 0;

    (VOID_T)tal_ble_claude_sniffer_register(__sniffer_cb);
    __refresh_nus_handles();
    __derive_name();
    __reset_state(FALSE);

    s_initialized = TRUE;
    PR_NOTICE("%s init done name=%s (rx=%u tx=%u)", BUDDY_BLE_TAG, s_device_name, (unsigned)s_rx_char_handle,
              (unsigned)s_tx_char_handle);
    return OPRT_OK;
}

/**
 * @brief Take ownership of advertising for the Claude Buddy protocol.
 * @return OPRT_OK on success
 */
OPERATE_RET buddy_ble_start(VOID_T)
{
    if (!s_initialized) {
        OPERATE_RET rt = buddy_ble_init();
        if (rt != OPRT_OK) {
            return rt;
        }
    }

    /* Stop Tuya's pair-timeout (would force-disconnect any non-Tuya peer
     * after 30 s) and the periodic monitor timer (would otherwise stop our
     * advertising once the cloud is connected). */
    (VOID_T)tuya_ble_pair_monitor_disable(TRUE);

    __refresh_nus_handles();
    __derive_name();
    __build_adv_payload();

    OPERATE_RET rt = __publish_adv();
    if (rt == OPRT_OK) {
        s_started = TRUE;
        PR_NOTICE("%s advertising as %s (rx=%u tx=%u)", BUDDY_BLE_TAG, s_device_name, (unsigned)s_rx_char_handle,
                  (unsigned)s_tx_char_handle);
    } else {
        PR_ERR("%s start failed rt=%d", BUDDY_BLE_TAG, rt);
    }
    return rt;
}
