/**
 * @file buddy_status_bar.h
 * @brief Shared header status-bar formatter (wifi/ble icons + battery %)
 * @version 1.0
 * @date 2026-04-27
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __BUDDY_STATUS_BAR_H__
#define __BUDDY_STATUS_BAR_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define BUDDY_BAT_PCT_UNKNOWN  (-1)

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Format the right-side connectivity / battery status string for the
 *        20px buddy header bar.
 *
 *        Layout: "<wifi> <bt> <bat>". On = LVGL symbol; off = single space
 *        of equal visual width. `bat_pct < 0` (or `BUDDY_BAT_PCT_UNKNOWN`)
 *        renders as "--".
 *
 * @param[out] buf       caller-owned buffer
 * @param[in]  cap       byte capacity of `buf`; recommended ≥ 24
 * @param[in]  wifi_on   TRUE to render WiFi icon, FALSE for blank
 * @param[in]  ble_on    TRUE to render Bluetooth icon, FALSE for blank
 * @param[in]  bat_pct   battery percentage 0~100, or
 *                       BUDDY_BAT_PCT_UNKNOWN for "--"
 * @return none
 */
VOID_T buddy_status_bar_format(char *buf, size_t cap,
                               BOOL_T wifi_on, BOOL_T ble_on, int bat_pct);

#ifdef __cplusplus
}
#endif
#endif /* __BUDDY_STATUS_BAR_H__ */
