/**
 * @file buddy_status_bar.c
 * @brief Shared header status-bar formatter (wifi/ble icons + battery %)
 * @version 1.0
 * @date 2026-04-27
 * @copyright Copyright (c) Tuya Inc.
 */
#include "buddy_status_bar.h"
#include "lvgl.h"
#include <stdio.h>

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Format the header-bar status string
 * @return none
 */
VOID_T buddy_status_bar_format(char *buf, size_t cap,
                               BOOL_T wifi_on, BOOL_T ble_on, int bat_pct)
{
    if (buf == NULL || cap == 0) {
        return;
    }

    const char *w = wifi_on ? LV_SYMBOL_WIFI      : " ";
    const char *b = ble_on  ? LV_SYMBOL_BLUETOOTH : " ";

    if (bat_pct < 0 || bat_pct > 100) {
        (VOID_T)snprintf(buf, cap, "%s %s --", w, b);
    } else {
        (VOID_T)snprintf(buf, cap, "%s %s %d%%", w, b, bat_pct);
    }
}
