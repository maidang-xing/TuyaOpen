/**
 * @file tal_bluetooth_nus_ext.h
 * @brief Internal extension declarations for an optional Nordic UART Service
 *        (NUS) co-registered alongside Tuya's provisioning GATT table.
 *
 * This header is NOT part of the public TAL BLE API surface.  It is included
 * only by application modules that need to hook into the auxiliary sniffer
 * callback and obtain NUS characteristic handles.
 *
 * Keeping these declarations separate from tal_bluetooth.h prevents
 * application-specific naming from leaking into the common SDK public interface.
 *
 * @copyright Copyright (c) 2021-2024 Tuya Inc. All Rights Reserved.
 */

#ifndef __TAL_BLUETOOTH_NUS_EXT_H__
#define __TAL_BLUETOOTH_NUS_EXT_H__

#include "tal_bluetooth_def.h"

#ifdef __cplusplus
extern "C" {
#endif

#if (defined(ENABLE_CLAUDE_DESKTOP_BUDDY_BLE) && (ENABLE_CLAUDE_DESKTOP_BUDDY_BLE == 1))

/**
 * @brief Register an auxiliary ("sniffer") TAL BLE event callback.
 *
 * The sniffer receives the same GAP/GATT events as the primary callback
 * registered via tal_ble_bt_init(). It is intended to let an optional NUS
 * application module observe GATT traffic without disturbing Tuya's ble_mgr
 * state machine. The sniffer must NOT mutate the event payload and must not
 * block.
 *
 * @param[in] cb sniffer callback, or NULL to unregister
 * @return OPRT_OK on success
 */
OPERATE_RET tal_ble_nus_ext_sniffer_register(TAL_BLE_EVT_FUNC_CB cb);

/**
 * @brief Get the GATT handles assigned to the NUS RX / TX characteristics.
 *
 * Handles are populated during tal_ble_bt_init() when the GATT table is
 * registered.  This call may return OPRT_COM_ERROR before init completes.
 *
 * @param[out] rx_handle optional output: handle of the NUS RX (write) char
 * @param[out] tx_handle optional output: handle of the NUS TX (notify) char
 * @return OPRT_OK on success, OPRT_COM_ERROR if handles not yet populated
 */
OPERATE_RET tal_ble_nus_ext_handles_get(uint16_t *rx_handle, uint16_t *tx_handle);

#endif /* ENABLE_CLAUDE_DESKTOP_BUDDY_BLE */

#ifdef __cplusplus
}
#endif

#endif /* __TAL_BLUETOOTH_NUS_EXT_H__ */
