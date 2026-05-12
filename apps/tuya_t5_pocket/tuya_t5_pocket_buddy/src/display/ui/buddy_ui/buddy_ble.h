/**
 * @file buddy_ble.h
 * @brief Claude Desktop Buddy BLE (Nordic UART Service) bridge.
 * @version 5.0
 * @date 2026-04-21
 * @copyright Copyright (c) Tuya Inc.
 *
 * Co-registers the Nordic UART Service (NUS) used by the Claude Desktop
 * Buddy protocol alongside the Tuya BLE provisioning service at stack
 * initialization time (see ENABLE_CLAUDE_DESKTOP_BUDDY_BLE in tal_bluetooth).
 * The BLE stack is never torn down at runtime; this module only:
 *   1. Registers a "sniffer" callback into TAL to see NUS GATT traffic.
 *   2. On MQTT connect, disables Tuya's pair-timeout monitor and publishes
 *      a Claude-branded advertising payload ("Claude_XXXX" + NUS UUID).
 *   3. Reassembles incoming NUS RX writes into newline-delimited UTF-8 JSON
 *      and dispatches commands (heartbeat, status, permission, ...).
 *   4. Fragments outgoing replies to the negotiated MTU and sends them over
 *      the NUS TX characteristic as GATT notifications.
 *
 * The module updates a shared buddy_tama_state_t so the UI layer can refresh
 * its visuals without any direct coupling to the BLE stack.
 */
#ifndef __BUDDY_BLE_H__
#define __BUDDY_BLE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include "buddy_data.h"

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
/* ASR text is capped before JSON escaping to keep transient buffers bounded. */
#define BUDDY_BLE_ASR_TEXT_MAX_BYTES 256U

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize the internal state of the Claude Buddy BLE bridge.
 *
 * Allocates the state / rx mutexes, sets the initial "waiting" snapshot and
 * registers the TAL sniffer callback so the module sees NUS GATT traffic.
 * Safe to call multiple times; subsequent calls return OPRT_OK without side
 * effects. Does NOT touch the BLE radio: call buddy_ble_start() after the
 * Tuya cloud stack is up to actually flip advertising to Claude mode.
 *
 * @return OPRT_OK on success
 */
OPERATE_RET buddy_ble_init(VOID_T);

/**
 * @brief Take ownership of BLE advertising for the Claude Buddy protocol.
 *
 * Disables Tuya's pair-timeout monitor and periodic advertising monitor,
 * then publishes our own advertising payload (flags + NUS UUID + scan
 * response with local name "Claude_XXXX"). Idempotent: subsequent calls
 * refresh the payload and restart advertising.
 *
 * @return OPRT_OK on success
 * @note Must be called AFTER the Tuya cloud MQTT has connected, so Tuya's
 *       own provisioning advertising is already expected to stop.
 */
OPERATE_RET buddy_ble_start(VOID_T);

/**
 * @brief Check whether a Claude Buddy host is currently connected.
 * @return TRUE if a GATT link is up
 */
BOOL_T buddy_ble_is_connected(VOID_T);

/**
 * @brief Check whether buddy_ble_start() has taken ownership of advertising.
 * @return TRUE if Claude-mode advertising is active
 */
BOOL_T buddy_ble_is_started(VOID_T);

/**
 * @brief Send a permission decision back to the Claude Buddy host.
 * @param[in] prompt_id unique id echoed from the original request
 * @param[in] decision "once" / "always" / "deny"
 * @return OPRT_OK on success
 */
OPERATE_RET buddy_ble_send_permission(const char *prompt_id, const char *decision);

/**
 * @brief Send a raw `{"cmd":"..."}` frame to the Claude desktop.
 *
 * Only a whitelisted set of plain-command names should be used here
 * (e.g. "status", "ping"); do NOT pass user-supplied text, the value is
 * embedded in JSON without further escaping.
 *
 * @param[in] cmd ASCII command token (must not contain " or \ or newline)
 * @return OPRT_OK on success, OPRT_INVALID_PARM / OPRT_COM_ERROR otherwise
 */
OPERATE_RET buddy_ble_send_cmd(const char *cmd);

/**
 * @brief Send an ASR (speech-recognition) text frame to the Claude desktop.
 *
 * Emits a single newline-terminated JSON frame over NUS TX with the form
 * `{"asr":"<utf-8 text>","sid":"<11-char short id>"}\n`. The text is JSON-
 * escaped (control chars + `"` + `\\`) by this function, so callers may pass
 * raw user-visible UTF-8. `sid` must already be the 11-char short session id
 * (typically obtained via buddy_main_screen_get_selected_sid()); when empty
 * the field is still emitted so the host can route it to the active project.
 *
 * Direction is device → host only; no reply is expected. Safe to call from
 * the AI/ASR task; internally it serializes onto the NUS TX path.
 *
 * @param[in] text non-NULL UTF-8 ASR transcript (may be empty → returns OPRT_INVALID_PARM)
 * @param[in] sid  short session id (NUL-terminated, length 0..11); NULL allowed → emitted as ""
 * @return OPRT_OK on success, OPRT_INVALID_PARM / OPRT_COM_ERROR otherwise
 * @note If no NUS link is up, returns an error and drops the frame; the caller
 *       must not block waiting for connectivity.
 */
OPERATE_RET buddy_ble_send_asr(const char *text, const char *sid);

/**
 * @brief Send a heartbeat-request (device-pull) frame to the Claude daemon.
 *
 * Emits a single newline-terminated JSON frame over NUS TX with the form
 * `{"cmd":"hb_req","page":"<name>"}\n`. The Tuya daemon answers by pushing
 * the next heartbeat snapshot immediately, eliminating the up-to-10 s wait
 * for the next keepalive when the user switches screens.
 *
 * Backward-compatible with REFERENCE.md: the official Claude Desktop apps
 * silently ignore unknown ``cmd`` frames originating from the device.
 *
 * @param[in] page optional short page tag (NUL-terminated, ≤ 16 bytes;
 *                  e.g. "main", "session", "chart", "pie", "status").
 *                  NULL or empty → omits the field.
 * @return OPRT_OK on success, OPRT_INVALID_PARM / OPRT_COM_ERROR otherwise
 * @note Safe from any task; serialised internally onto the NUS TX queue.
 *       Drops the frame (returns error) when the BLE link is down — the
 *       caller must not assume the request was delivered.
 */
OPERATE_RET buddy_ble_send_hb_req(const char *page);

/**
 * @brief Read a snapshot of the latest tama state decoded from BLE traffic.
 * @param[out] out target struct (must not be NULL)
 * @return none
 */
VOID_T buddy_ble_snapshot(buddy_tama_state_t *out);

/**
 * @brief Check whether the device is connected to the Tuya cloud (implies WiFi up).
 *
 * Uses the TuyaOS IoT client activation state as a proxy for WiFi connectivity.
 * Returns TRUE if the device has been activated (devid populated) and the IoT
 * client is running, which requires WiFi to have been connected at some point.
 *
 * @return TRUE if cloud/WiFi is active
 */
BOOL_T buddy_ble_cloud_is_connected(VOID_T);

#ifdef __cplusplus
}
#endif
#endif /* __BUDDY_BLE_H__ */
