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
#include "buddy_approval_screen.h"
#include "buddy_led.h"
#include "screen_manager.h"
#include "lv_vendor.h"

#include "tal_api.h"
#include "tal_bluetooth.h"
#include "tal_bluetooth_nus_ext.h"
#include "tkl_bluetooth.h"
#include "ble_mgr.h"
#include "tuya_iot.h"
#include "cJSON.h"
#include "tal_workqueue.h"
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

/* Line buffer for inbound NUS writes.  Each incoming message is a chunk
 * envelope (≤ CHUNK_ENV_BUF_CAP bytes) or a small control frame; the daemon
 * splits frames larger than CHUNK_THRESHOLD before sending, so no single
 * line should exceed ~640 B.  1024 B gives comfortable headroom. */
#define BUDDY_BLE_RX_BUF_CAP 1024

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

/* Application-level chunking: large JSON frames are split into smaller
 * self-contained chunk envelopes so each BLE transfer is short and
 * individually parseable. The receiver reassembles the pieces before
 * processing. */
#define CHUNK_THRESHOLD      480U   /* frames larger than this get split */
#define CHUNK_RAW_SIZE       400U   /* raw payload bytes per chunk piece */
#define CHUNK_MAX_COUNT      20U    /* max pieces per frame (~8 KB) */
/* CHUNK_RX_CAP removed: reassembly buffer is now allocated on first chunk
 * arrival and freed immediately after dispatch (see __rx_chunk_feed). */
#define CHUNK_TIMEOUT_MS     5000U  /* discard incomplete frames after this */
/* TX envelope budget: header ~30 B + 400 B payload worst-case JSON-escaped
 * (~440 B) + trailer 3 B + newline = ~473 B; 640 B leaves ample margin. */
#define CHUNK_ENV_BUF_CAP    640U   /* stack buffer for one chunk envelope */

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
STATIC WORKQUEUE_HANDLE s_rx_workq = NULL;

/* Chunk TX rolling frame ID (wraps 0x00..0xFF). */
STATIC uint8_t s_tx_frame_id = 0;

/* Chunk RX reassembly state — only one frame in flight at a time.
 * The reassembly buffer is allocated on first-chunk arrival and freed after
 * dispatch to avoid 8 KB BSS occupancy between messages. */
typedef struct {
    char     fid[4];       /* current frame "_f" value */
    uint8_t  total;        /* expected chunk count */
    uint8_t  next_n;       /* next expected sequence number (1-based) */
    char    *buf;          /* heap-allocated reassembly buffer, NULL when idle */
    uint32_t buf_cap;      /* allocated capacity of buf */
    uint32_t buf_len;      /* bytes accumulated so far */
    uint32_t last_ms;      /* tick of last received chunk */
} __chunk_rx_t;

STATIC __chunk_rx_t s_chunk_rx = {0};

/* Static scratch buffer for heartbeat parsing.  Avoids ~6 KB heap allocation
 * per heartbeat: the parser writes into s_parse_snap and commits to s_state
 * under mutex when done.  Never shared with __push_ui_state (s_ui_snap). */
STATIC buddy_tama_state_t s_parse_snap;

/* Static snapshot buffer for __push_ui_state (UI refresh copy). */
STATIC buddy_tama_state_t s_ui_snap;

/* Workqueue scheduling coalescing: prevents flooding the queue when
 * BLE packets arrive faster than the workqueue processes them. */
STATIC volatile BOOL_T s_rx_work_pending = FALSE;

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
STATIC VOID_T __rx_process_work(void *data);
STATIC VOID_T __handle_heartbeat(cJSON *root);
STATIC VOID_T __handle_time(cJSON *time_arr);
STATIC VOID_T __reset_entries(buddy_tama_state_t *snap);
STATIC VOID_T __push_entry(buddy_tama_state_t *snap, int index, const char *text);
STATIC VOID_T __send_ack(const char *ack, BOOL_T ok, int n);
STATIC OPERATE_RET __send_raw(const char *payload, uint16_t length);
STATIC OPERATE_RET __send_chunked(const char *payload, uint16_t length);
STATIC VOID_T __rx_chunk_reset(VOID_T);
STATIC VOID_T __rx_chunk_feed(cJSON *root);
STATIC VOID_T __send_status(VOID_T);
STATIC size_t __utf8_safe_prefix_len(const char *data, size_t full_len, size_t max_len);

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
    buddy_ble_snapshot(&s_ui_snap);
    buddy_main_screen_update_state(&s_ui_snap);

    static BOOL_T s_prev_has_prompt = FALSE;
    BOOL_T now_has_prompt = s_ui_snap.has_prompt ? TRUE : FALSE;
    if (now_has_prompt && !s_prev_has_prompt) {
        Screen_t *cur = screen_get_now_screen();
        if (cur != NULL && cur != &buddy_approval_screen
            && cur != &buddy_main_screen) {
            (VOID_T)buddy_led_set(BUDDY_LED_STATE_BLINK_FAST);
            lv_vendor_disp_lock();
            screen_load(&buddy_approval_screen);
            lv_vendor_disp_unlock();
        }
    }
    s_prev_has_prompt = now_has_prompt;
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

/**
 * @brief Public: cloud connectivity check.
 *
 * Uses the TuyaOS IoT client activation state as a lightweight WiFi proxy.
 * An activated device (devid != "") means WiFi provisioning succeeded and
 * cloud was reachable at least once in the device's lifetime.
 *
 * @return TRUE if the IoT client is active and the device is registered
 */
BOOL_T buddy_ble_cloud_is_connected(VOID_T)
{
    tuya_iot_client_t *iot = tuya_iot_client_get();
    if (iot == NULL) return FALSE;
    return (iot->activate.devid[0] != '\0') ? TRUE : FALSE;
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
    if (tal_ble_nus_ext_handles_get(&rx, &tx) == OPRT_OK) {
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
        /* Cancel Tuya's 30 s pair-timeout immediately on connect so it cannot
         * force-disconnect a Claude peer, even if buddy_ble_start() hasn't run yet. */
        (VOID_T)tuya_ble_pair_monitor_disable(TRUE);
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
        if (s_rx_workq != NULL) {
            if (!s_rx_work_pending) {
                OPERATE_RET rt = tal_workqueue_schedule(s_rx_workq, __rx_process_work, NULL);
                if (rt == OPRT_OK) {
                    s_rx_work_pending = TRUE;
                }
            }
        } else {
            __rx_dispatch_lines();
        }
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
    if (len >= BUDDY_BLE_RX_BUF_CAP) {
        PR_WARN("%s rx frame too large, drop (len=%u cap=%u)", BUDDY_BLE_TAG, (unsigned)len,
                (unsigned)BUDDY_BLE_RX_BUF_CAP);
        s_rx_len = 0;
        if (s_rx_mutex) {
            tal_mutex_unlock(s_rx_mutex);
        }
        return;
    }
    if ((uint32_t)s_rx_len + len >= BUDDY_BLE_RX_BUF_CAP) {
        /* Protocol is newline-delimited; on overflow start fresh rather than
         * corrupt the next JSON frame. */
        PR_WARN("%s rx overflow, reset (len=%u+%u cap=%u)", BUDDY_BLE_TAG, (unsigned)s_rx_len, (unsigned)len,
                (unsigned)BUDDY_BLE_RX_BUF_CAP);
        s_rx_len = 0;
    }
    memcpy(s_rx_buf + s_rx_len, data, len);
    s_rx_len = (uint16_t)(s_rx_len + len);
    if (s_rx_mutex) {
        tal_mutex_unlock(s_rx_mutex);
    }
}

STATIC VOID_T __rx_process_work(void *data)
{
    (void)data;
    s_rx_work_pending = FALSE;
    __rx_dispatch_lines();
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

/* ---------------------------------------------------------------------------
 * Application-level chunk TX / RX
 * --------------------------------------------------------------------------- */
/**
 * @brief Ensure split position does not break a multi-byte UTF-8 sequence.
 * @param[in] data   buffer
 * @param[in] desired target split position
 * @param[in] start  start of the current chunk (lower bound for back-up)
 * @return adjusted position (always >= start)
 */
STATIC uint16_t __utf8_safe_end(const char *data, uint16_t desired, uint16_t start)
{
    uint16_t pos = desired;
    while (pos > start && ((uint8_t)data[pos] & 0xC0) == 0x80) {
        pos--;
    }
    return pos;
}

/**
 * @brief Clamp a UTF-8 string byte length without splitting a sequence.
 * @param[in] data input UTF-8 bytes
 * @param[in] full_len total available byte length
 * @param[in] max_len requested maximum byte length
 * @return safe byte length no greater than max_len
 */
STATIC size_t __utf8_safe_prefix_len(const char *data, size_t full_len, size_t max_len)
{
    size_t len = full_len;

    if (data == NULL) {
        return 0;
    }
    if (len > max_len) {
        len = max_len;
        while (len > 0 && len < full_len && (((uint8_t)data[len] & 0xC0U) == 0x80U)) {
            len--;
        }
    }
    return len;
}

/**
 * @brief Send a JSON line with application-level chunking for large payloads.
 *
 * If the payload is shorter than CHUNK_THRESHOLD the frame is sent as-is
 * via __send_raw.  Otherwise the payload (minus trailing newline) is split
 * into chunks whose _d values are JSON-escaped substrings.  Each chunk
 * envelope is a complete newline-terminated JSON object that the receiver
 * can parse independently.
 *
 * @param[in] payload  NUL-terminated JSON line (may include trailing \\n)
 * @param[in] length   byte count (including \\n if present)
 * @return OPRT_OK on success
 */
STATIC OPERATE_RET __send_chunked(const char *payload, uint16_t length)
{
    if (length <= (uint16_t)CHUNK_THRESHOLD) {
        return __send_raw(payload, length);
    }

    /* Strip trailing newline for splitting. */
    uint16_t data_len = length;
    while (data_len > 0 && payload[data_len - 1] == '\n') {
        data_len--;
    }
    if (data_len == 0) {
        return OPRT_OK;
    }

    /* Pre-count chunks (respecting UTF-8 boundaries). */
    uint16_t n_chunks = 0;
    uint16_t pos = 0;
    while (pos < data_len) {
        uint16_t end = (uint16_t)(pos + CHUNK_RAW_SIZE);
        if (end >= data_len) {
            end = data_len;
        } else {
            end = __utf8_safe_end(payload, end, pos);
            if (end <= pos) {
                end = (uint16_t)(pos + 1);
            }
        }
        n_chunks++;
        pos = end;
    }
    if (n_chunks > (uint16_t)CHUNK_MAX_COUNT) {
        PR_WARN("%s frame too large for app chunking (%u B, %u chunks)",
                BUDDY_BLE_TAG, (unsigned)length, (unsigned)n_chunks);
        return OPRT_COM_ERROR;
    }

    char fid[4];
    (VOID_T)snprintf(fid, sizeof(fid), "%02x", (unsigned)(s_tx_frame_id & 0xFF));
    s_tx_frame_id++;

    char env[CHUNK_ENV_BUF_CAP];

    pos = 0;
    uint16_t seq = 1;
    OPERATE_RET rt = OPRT_OK;

    while (pos < data_len) {
        uint16_t end = (uint16_t)(pos + CHUNK_RAW_SIZE);
        if (end >= data_len) {
            end = data_len;
        } else {
            end = __utf8_safe_end(payload, end, pos);
            if (end <= pos) {
                end = (uint16_t)(pos + 1);
            }
        }

        /* Build envelope:  {"_f":"XX","_n":N,"_t":T,"_d":"..."}\n
         * The _d value is JSON-escaped inline (only " and \ need escaping
         * since the payload is valid JSON text without raw control chars). */
        size_t off = 0;
        int w = snprintf(env, CHUNK_ENV_BUF_CAP,
                         "{\"_f\":\"%s\",\"_n\":%u,\"_t\":%u,\"_d\":\"",
                         fid, (unsigned)seq, (unsigned)n_chunks);
        if (w <= 0 || (size_t)w >= CHUNK_ENV_BUF_CAP) {
            rt = OPRT_COM_ERROR;
            break;
        }
        off = (size_t)w;

        uint16_t i = pos;
        for (; i < end; i++) {
            if (off + 8 >= CHUNK_ENV_BUF_CAP) {
                rt = OPRT_COM_ERROR;
                break;
            }
            unsigned char c = (unsigned char)payload[i];
            if (c == '"') {
                env[off++] = '\\';
                env[off++] = '"';
            } else if (c == '\\') {
                env[off++] = '\\';
                env[off++] = '\\';
            } else if (c < 0x20) {
                int m = snprintf(env + off, CHUNK_ENV_BUF_CAP - off,
                                 "\\u%04x", (unsigned)c);
                if (m <= 0 || (size_t)m >= CHUNK_ENV_BUF_CAP - off) {
                    rt = OPRT_COM_ERROR;
                    break;
                }
                off += (size_t)m;
            } else {
                env[off++] = (char)c;
            }
        }
        if (rt != OPRT_OK) {
            PR_WARN("%s chunk envelope overflow", BUDDY_BLE_TAG);
            break;
        }
        if (i < end) {
            PR_WARN("%s chunk payload truncated", BUDDY_BLE_TAG);
            rt = OPRT_COM_ERROR;
            break;
        }

        w = snprintf(env + off, CHUNK_ENV_BUF_CAP - off, "\"}\n");
        if (w <= 0) {
            rt = OPRT_COM_ERROR;
            break;
        }
        off += (size_t)w;

        rt = __send_raw(env, (uint16_t)off);
        if (rt != OPRT_OK) {
            break;
        }

        pos = end;
        seq++;
    }

    return rt;
}

/**
 * @brief Discard any partial chunk reassembly state.
 * @return none
 */
STATIC VOID_T __rx_chunk_reset(VOID_T)
{
    if (s_chunk_rx.buf != NULL) {
        tal_free(s_chunk_rx.buf);
    }
    s_chunk_rx.buf = NULL;
    s_chunk_rx.buf_cap = 0;
    s_chunk_rx.buf_len = 0;
    s_chunk_rx.total = 0;
    s_chunk_rx.next_n = 0;
    s_chunk_rx.last_ms = 0;
    memset(s_chunk_rx.fid, 0, sizeof(s_chunk_rx.fid));
}

/**
 * @brief Buffer an incoming chunk envelope and reassemble when complete.
 *
 * When all chunks of a frame arrive in order, the concatenated _d payloads
 * are forwarded to __handle_line for normal JSON dispatch.  Out-of-order
 * or timed-out chunks cause the whole frame to be discarded.
 *
 * @param[in] root  parsed cJSON object that has a "_f" key
 * @return none
 */
STATIC VOID_T __rx_chunk_feed(cJSON *root)
{
    cJSON *jf = cJSON_GetObjectItem(root, "_f");
    cJSON *jn = cJSON_GetObjectItem(root, "_n");
    cJSON *jt = cJSON_GetObjectItem(root, "_t");
    cJSON *jd = cJSON_GetObjectItem(root, "_d");

    if (!cJSON_IsString(jf) || !cJSON_IsNumber(jn) ||
        !cJSON_IsNumber(jt) || !cJSON_IsString(jd)) {
        return;
    }

    const char *fid = jf->valuestring;
    int n = jn->valueint;
    int t = jt->valueint;
    const char *d = jd->valuestring;

    if (fid == NULL || d == NULL) {
        return;
    }
    if (n < 1 || t < 1 || (uint32_t)t > CHUNK_MAX_COUNT || n > t) {
        return;
    }

    uint32_t now_ms = (uint32_t)(tal_system_get_millisecond() & 0xFFFFFFFFU);

    /* Timeout: discard stale partial frames. */
    if (s_chunk_rx.buf != NULL &&
        (now_ms - s_chunk_rx.last_ms) > CHUNK_TIMEOUT_MS) {
        PR_DEBUG("%s chunk timeout, reset", BUDDY_BLE_TAG);
        __rx_chunk_reset();
    }

    /* First chunk of a (possibly new) frame? */
    if (n == 1 || s_chunk_rx.buf == NULL ||
        strncmp(s_chunk_rx.fid, fid, sizeof(s_chunk_rx.fid) - 1) != 0) {
        __rx_chunk_reset();
        strncpy(s_chunk_rx.fid, fid, sizeof(s_chunk_rx.fid) - 1);
        s_chunk_rx.fid[sizeof(s_chunk_rx.fid) - 1] = '\0';
        s_chunk_rx.total = (uint8_t)t;
        s_chunk_rx.next_n = 1;

        /* Allocate reassembly buffer sized for the whole frame + NUL.
         * Guard against integer overflow before multiplying. */
        uint32_t alloc_cap = (uint32_t)t * CHUNK_RAW_SIZE + 1U;
        if (alloc_cap < (uint32_t)t) { /* overflow */
            PR_WARN("%s chunk alloc overflow t=%d", BUDDY_BLE_TAG, t);
            return;
        }
        s_chunk_rx.buf = (char *)tal_malloc(alloc_cap);
        if (s_chunk_rx.buf == NULL) {
            PR_ERR("%s chunk buf oom (%u B)", BUDDY_BLE_TAG, (unsigned)alloc_cap);
            return;
        }
        s_chunk_rx.buf_cap = alloc_cap;
        s_chunk_rx.buf_len = 0;
    }

    /* Enforce in-order delivery. */
    if ((uint8_t)n != s_chunk_rx.next_n) {
        PR_WARN("%s chunk seq mismatch got=%d want=%u",
                BUDDY_BLE_TAG, n, (unsigned)s_chunk_rx.next_n);
        __rx_chunk_reset();
        return;
    }

    /* Append _d data. */
    size_t d_len = strlen(d);
    if (s_chunk_rx.buf == NULL || s_chunk_rx.buf_len + (uint32_t)d_len >= s_chunk_rx.buf_cap) {
        PR_WARN("%s chunk reassembly overflow", BUDDY_BLE_TAG);
        __rx_chunk_reset();
        return;
    }
    memcpy(s_chunk_rx.buf + s_chunk_rx.buf_len, d, d_len);
    s_chunk_rx.buf_len += (uint32_t)d_len;
    s_chunk_rx.next_n++;
    s_chunk_rx.last_ms = now_ms;

    PR_DEBUG("%s chunk %s %d/%d (+%u B)", BUDDY_BLE_TAG,
             fid, n, t, (unsigned)d_len);

    /* All chunks received? */
    if ((uint8_t)n == s_chunk_rx.total) {
        s_chunk_rx.buf[s_chunk_rx.buf_len] = '\0';

        PR_DEBUG("%s chunk reassembled %u B", BUDDY_BLE_TAG,
                 (unsigned)s_chunk_rx.buf_len);
        __handle_line(s_chunk_rx.buf);
        __rx_chunk_reset();
    }
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

    size_t line_len = strlen(line);
    const size_t log_limit = 160;
    if (line_len > log_limit) {
        PR_DEBUG("%s RX %.160s...(len=%u)", BUDDY_BLE_TAG, line, (unsigned)line_len);
    } else {
        PR_DEBUG("%s RX %s", BUDDY_BLE_TAG, line);
    }

    cJSON *root = cJSON_Parse(line);
    if (root == NULL) {
        PR_WARN("%s bad json", BUDDY_BLE_TAG);
        return;
    }

    /* Application-level chunk envelope? Reassemble before dispatching. */
    cJSON *chunk_f = cJSON_GetObjectItem(root, "_f");
    if (cJSON_IsString(chunk_f)) {
        __rx_chunk_feed(root);
        cJSON_Delete(root);
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

    buddy_tama_state_t *snap = &s_parse_snap;
    buddy_ble_snapshot(snap);
    snap->ble_connected = TRUE;

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
        snap->sessions_total = (uint8_t)total->valueint;
    }
    if (cJSON_IsNumber(running)) {
        snap->sessions_running = (uint8_t)running->valueint;
    }
    if (cJSON_IsNumber(waiting)) {
        snap->sessions_waiting = (uint8_t)waiting->valueint;
    }
    if (cJSON_IsNumber(tokens)) {
        snap->tokens = (uint32_t)tokens->valuedouble;
    }
    if (cJSON_IsNumber(tokens_today)) {
        snap->tokens_today = (uint32_t)tokens_today->valuedouble;
    }
    if (cJSON_IsNumber(tokens_in)) {
        snap->tokens_in = (uint32_t)tokens_in->valuedouble;
    }
    if (cJSON_IsNumber(tokens_in_today)) {
        snap->tokens_in_today = (uint32_t)tokens_in_today->valuedouble;
    }
    if (cJSON_IsNumber(cache_read)) {
        snap->cache_read = (uint32_t)cache_read->valuedouble;
    }
    if (cJSON_IsNumber(cache_write)) {
        snap->cache_write = (uint32_t)cache_write->valuedouble;
    }
    if (cJSON_IsNumber(ctx_used)) {
        snap->ctx_used = (uint32_t)ctx_used->valuedouble;
    }
    if (cJSON_IsNumber(ctx_total)) {
        snap->ctx_total = (uint32_t)ctx_total->valuedouble;
    }
    __copy_str(snap->msg, sizeof(snap->msg), msg);

    cJSON *prompt = cJSON_GetObjectItem(root, "prompt");
    if (cJSON_IsObject(prompt)) {
        __copy_str(snap->prompt_id, sizeof(snap->prompt_id), cJSON_GetObjectItem(prompt, "id"));
        __copy_str(snap->prompt_tool, sizeof(snap->prompt_tool), cJSON_GetObjectItem(prompt, "tool"));
        __copy_str(snap->prompt_hint, sizeof(snap->prompt_hint), cJSON_GetObjectItem(prompt, "hint"));
        snap->has_prompt = (snap->prompt_id[0] != '\0');
    } else {
        snap->has_prompt = FALSE;
        snap->prompt_id[0] = '\0';
    }

    cJSON *entries = cJSON_GetObjectItem(root, "entries");
    if (cJSON_IsArray(entries)) {
        int n = cJSON_GetArraySize(entries);
        int first = (n > BUDDY_ENTRIES_RING) ? (n - BUDDY_ENTRIES_RING) : 0;
        __reset_entries(snap);
        for (int i = first; i < n; i++) {
            cJSON *e = cJSON_GetArrayItem(entries, i);
            if (cJSON_IsString(e) && e->valuestring != NULL) {
                __push_entry(snap, i, e->valuestring);
            }
        }
    }

    /* model name */
    __copy_str(snap->model, sizeof(snap->model), cJSON_GetObjectItem(root, "model"));

    /* sessions array: [{"id":..,"n":..,"m":..,"to":uint,"r":bool,"p":str,"e":[]}] */
    snap->sessions_count = 0;
    cJSON *jsessions = cJSON_GetObjectItem(root, "sessions");
    if (cJSON_IsArray(jsessions)) {
        int n = cJSON_GetArraySize(jsessions);
        if (n > BUDDY_SESSIONS_MAX) {
            n = BUDDY_SESSIONS_MAX;
        }
        for (int i = 0; i < n; i++) {
            cJSON *js = cJSON_GetArrayItem(jsessions, i);
            if (!cJSON_IsObject(js)) {
                continue;
            }
            buddy_session_t *sess = &snap->sessions[snap->sessions_count];
            memset(sess, 0, sizeof(*sess));
            __copy_str(sess->sid,     sizeof(sess->sid),     cJSON_GetObjectItem(js, "id"));
            __copy_str(sess->name,    sizeof(sess->name),    cJSON_GetObjectItem(js, "n"));
            __copy_str(sess->model,   sizeof(sess->model),   cJSON_GetObjectItem(js, "m"));
            __copy_str(sess->project, sizeof(sess->project), cJSON_GetObjectItem(js, "p"));
            cJSON *jto = cJSON_GetObjectItem(js, "to");
            cJSON *jr  = cJSON_GetObjectItem(js, "r");
            if (cJSON_IsNumber(jto)) {
                sess->tokens_out = (uint32_t)jto->valuedouble;
            }
            sess->is_running = cJSON_IsTrue(jr) ? TRUE : FALSE;
            cJSON *je = cJSON_GetObjectItem(js, "e");
            sess->local_entry_count = 0;
            if (cJSON_IsArray(je)) {
                int ne = cJSON_GetArraySize(je);
                if (ne > BUDDY_SESSION_LOCAL_ENTRIES) {
                    ne = BUDDY_SESSION_LOCAL_ENTRIES;
                }
                for (int ei = 0; ei < ne; ei++) {
                    cJSON *eitem = cJSON_GetArrayItem(je, ei);
                    if (cJSON_IsString(eitem) && eitem->valuestring) {
                        (VOID_T)snprintf(sess->local_entries[sess->local_entry_count].text,
                                         sizeof(sess->local_entries[0].text),
                                         "%.79s", eitem->valuestring);
                        sess->local_entry_count++;
                    }
                }
            }
            snap->sessions_count++;
        }
    }

    /* mstats array: [{"m":..,"to":uint}] */
    snap->mstats_count = 0;
    cJSON *jmstats = cJSON_GetObjectItem(root, "mstats");
    if (cJSON_IsArray(jmstats)) {
        int n = cJSON_GetArraySize(jmstats);
        if (n > BUDDY_MSTATS_MAX) {
            n = BUDDY_MSTATS_MAX;
        }
        for (int i = 0; i < n; i++) {
            cJSON *jm = cJSON_GetArrayItem(jmstats, i);
            if (!cJSON_IsObject(jm)) {
                continue;
            }
            buddy_mstat_t *ms = &snap->mstats[snap->mstats_count];
            memset(ms, 0, sizeof(*ms));
            __copy_str(ms->model, sizeof(ms->model), cJSON_GetObjectItem(jm, "m"));
            cJSON *jto = cJSON_GetObjectItem(jm, "to");
            if (cJSON_IsNumber(jto)) {
                ms->tokens_out = (uint32_t)jto->valuedouble;
            }
            snap->mstats_count++;
        }
    }

    /* M4: Claude version, cost (micro-USD), daily token history */
    __copy_str(snap->claude_version, sizeof(snap->claude_version), cJSON_GetObjectItem(root, "ver"));
    cJSON *jcost_td  = cJSON_GetObjectItem(root, "cost_td");
    cJSON *jcost_all = cJSON_GetObjectItem(root, "cost_all");
    if (cJSON_IsNumber(jcost_td)) {
        snap->cost_today_ucc = (uint32_t)jcost_td->valuedouble;
    }
    if (cJSON_IsNumber(jcost_all)) {
        snap->cost_total_ucc = (uint32_t)jcost_all->valuedouble;
    }
    cJSON *jdaily = cJSON_GetObjectItem(root, "daily");
    if (cJSON_IsArray(jdaily)) {
        int nd = cJSON_GetArraySize(jdaily);
        if (nd > BUDDY_DAILY_HISTORY_DAYS) {
            nd = BUDDY_DAILY_HISTORY_DAYS;
        }
        for (int di = 0; di < nd; di++) {
            cJSON *dv = cJSON_GetArrayItem(jdaily, di);
            snap->daily_tokens[di] = cJSON_IsNumber(dv) ? (uint32_t)dv->valuedouble : 0U;
        }
    }

    if (s_state_mutex) {
        tal_mutex_lock(s_state_mutex);
    }
    s_state = *snap;
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
        (VOID_T)__send_chunked(buf, (uint16_t)wrote);
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
                (VOID_T)__send_chunked(buf, (uint16_t)(len + 1));
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
    return __send_chunked(buf, (uint16_t)n);
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
    return __send_chunked(buf, (uint16_t)n);
}

/**
 * @brief Public: send an ASR transcript frame to the host.
 * @param[in] text non-NULL UTF-8 transcript
 * @param[in] sid  short session id (NUL-terminated, ≤ 11 chars); NULL → ""
 * @return OPRT_OK on success
 * @note JSON-escapes control characters, '"' and '\\' in `text`. Drops the
 *       frame (returns OPRT_COM_ERROR) if no NUS link is up. Never blocks.
 */
OPERATE_RET buddy_ble_send_asr(const char *text, const char *sid)
{
    if (text == NULL) {
        return OPRT_INVALID_PARM;
    }
    size_t tlen = strlen(text);
    if (tlen == 0) {
        return OPRT_INVALID_PARM;
    }
    /* Hard cap: limits worst-case buffer size and keeps frames below the
     * chunk threshold for typical ASR results. */
    tlen = __utf8_safe_prefix_len(text, tlen, BUDDY_BLE_ASR_TEXT_MAX_BYTES);
    char sid_buf[12] = {0};
    if (sid != NULL) {
        size_t sl = strnlen(sid, sizeof(sid_buf) - 1);
        for (size_t i = 0; i < sl; i++) {
            unsigned char c = (unsigned char)sid[i];
            if (c == '"' || c == '\\' || c < 0x20) {
                return OPRT_INVALID_PARM;
            }
            sid_buf[i] = (char)c;
        }
        sid_buf[sl] = '\0';
    }
    if (s_conn_handle == TKL_BLE_GATT_INVALID_HANDLE) {
        return OPRT_COM_ERROR;
    }
    /* Stack buffer sized for: prefix(8) + worst-case escape(256*6=1536) +
     * sid suffix(~32) + NUL = 1577 B.  Safe on the 10 KB RX workqueue stack. */
    char buf[1600];
    size_t cap = sizeof(buf);
    size_t off = 0;
    int n = snprintf(buf + off, cap - off, "{\"asr\":\"");
    if (n <= 0 || (size_t)n >= cap - off) {
        return OPRT_COM_ERROR;
    }
    off += (size_t)n;
    for (size_t i = 0; i < tlen && off + 8 < cap; i++) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') {
            buf[off++] = '\\';
            buf[off++] = (char)c;
        } else if (c == '\n') {
            buf[off++] = '\\'; buf[off++] = 'n';
        } else if (c == '\r') {
            buf[off++] = '\\'; buf[off++] = 'r';
        } else if (c == '\t') {
            buf[off++] = '\\'; buf[off++] = 't';
        } else if (c < 0x20) {
            int m = snprintf(buf + off, cap - off, "\\u%04x", c);
            if (m <= 0 || (size_t)m >= cap - off) { break; }
            off += (size_t)m;
        } else {
            buf[off++] = (char)c;
        }
    }
    n = snprintf(buf + off, cap - off, "\",\"sid\":\"%s\"}\n", sid_buf);
    if (n <= 0 || (size_t)n >= cap - off) {
        return OPRT_COM_ERROR;
    }
    off += (size_t)n;
    return __send_chunked(buf, (uint16_t)off);
}

/**
 * @brief Public: send a heartbeat-request (device-pull) frame to the host.
 * @param[in] page optional short page tag (≤ 16 ASCII chars); NULL/"" omits
 * @return OPRT_OK on success, OPRT_INVALID_PARM / OPRT_COM_ERROR otherwise
 * @note Tuya extension over REFERENCE.md; non-Tuya peers ignore the frame.
 */
OPERATE_RET buddy_ble_send_hb_req(const char *page)
{
    if (s_conn_handle == TKL_BLE_GATT_INVALID_HANDLE) {
        return OPRT_COM_ERROR;
    }
    char page_buf[17] = {0};
    if (page != NULL && page[0] != '\0') {
        size_t pl = strnlen(page, sizeof(page_buf) - 1);
        for (size_t i = 0; i < pl; i++) {
            unsigned char c = (unsigned char)page[i];
            if (c == '"' || c == '\\' || c < 0x20 || c > 0x7E) {
                return OPRT_INVALID_PARM;
            }
            page_buf[i] = (char)c;
        }
        page_buf[pl] = '\0';
    }
    char buf[64];
    int n;
    if (page_buf[0] != '\0') {
        n = snprintf(buf, sizeof(buf),
                     "{\"cmd\":\"hb_req\",\"page\":\"%s\"}\n", page_buf);
    } else {
        n = snprintf(buf, sizeof(buf), "{\"cmd\":\"hb_req\"}\n");
    }
    if (n <= 0 || n >= (int)sizeof(buf)) {
        return OPRT_COM_ERROR;
    }
    return __send_chunked(buf, (uint16_t)n);
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

    THREAD_CFG_T rx_workq_cfg = {
        .stackDepth = 1024 * 6,
        .priority = THREAD_PRIO_2,
        .thrdname = "buddy_ble_rx",
    };
    rt = tal_workqueue_create(6, &rx_workq_cfg, &s_rx_workq);
    if (rt != OPRT_OK) {
        PR_ERR("%s rx workq rt=%d", BUDDY_BLE_TAG, rt);
        return rt;
    }

    s_rx_buf = (uint8_t *)tal_malloc(BUDDY_BLE_RX_BUF_CAP);
    if (s_rx_buf == NULL) {
        PR_ERR("%s rx buf oom", BUDDY_BLE_TAG);
        return OPRT_MALLOC_FAILED;
    }
    memset(s_rx_buf, 0, BUDDY_BLE_RX_BUF_CAP);
    s_rx_len = 0;

    (VOID_T)tal_ble_nus_ext_sniffer_register(__sniffer_cb);
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
