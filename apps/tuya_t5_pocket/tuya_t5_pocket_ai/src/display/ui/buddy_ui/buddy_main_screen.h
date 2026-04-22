/**
 * @file buddy_main_screen.h
 * @brief Claude Desktop Buddy single-screen UI.
 *
 * A minimal, text-only 384x168 landscape screen that renders live data
 * pushed by the Claude desktop over BLE (NUS).  There is no on-device
 * persona, stats, pagination or demo mode: everything visible comes from
 * the last JSON frame received from the host.
 *
 * Layout (see doc/UI_INTERACTION.md for the full spec):
 *   +-----------------------------------------------+
 *   | header 20px  Claude Buddy   BLE: -  Claude_.. |
 *   +-----------------------------------------------+
 *   | body 124px   msg / sessions / tokens / owner  |
 *   |              [permission card if pending]     |
 *   +-----------------------------------------------+
 *   | footer 24px  key hints (context sensitive)    |
 *   +-----------------------------------------------+
 *
 * Keys:
 *   ENTER  : approve pending permission  ("decision":"once")
 *   RIGHT  : deny pending permission     ("decision":"deny")
 *   UP     : always allow                ("decision":"always")
 *   DOWN   : request a status refresh from Claude  ({"cmd":"status"})
 *   ESC    : return to the previous screen
 *
 * All keys are no-ops when no permission request is pending, except
 * DOWN (status poke) and ESC (navigation), so the UI can never send a
 * decision without a matching prompt id.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_MAIN_SCREEN_H
#define BUDDY_MAIN_SCREEN_H

#include "screen_manager.h"
#include "buddy_data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_main_screen;

/**
 * @brief Push the latest BLE-sourced snapshot onto the main screen.
 *
 * Thread-safe: acquires the LVGL display lock internally, so it may be
 * called from any task (e.g. directly from the BLE RX path).  Does
 * nothing if the screen has not been loaded yet; the next init() will
 * paint with whatever snapshot is staged via buddy_ble_snapshot().
 *
 * @param[in] state snapshot from buddy_ble (must not be NULL)
 * @return none
 */
void buddy_main_screen_update_state(const buddy_tama_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_MAIN_SCREEN_H */
