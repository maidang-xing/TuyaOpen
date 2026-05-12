/**
 * @file main_screen.h
 * @brief Claude Desktop Buddy main screen UI (M1-UI version).
 *
 * Single-screen layout, 384x168 landscape:
 *   +------------------------------------------------------+
 *   | header 20px  Claude Buddy   HH:MM   WS: -  Claude_..|
 *   +-----------------------------+------------------------+
 *   | body 124px                  |  body right 124px       |
 *   |   ASCII persona 184x120     |  msg / sessions / toks  |
 *   |   (or GIF stub placeholder) |  owner / entries (4 r)  |
 *   |   [permission card overlays entire body if pending]   |
 *   +------------------------------------------------------+
 *   | footer 24px  key hints (context sensitive)            |
 *   +------------------------------------------------------+
 *
 * Key bindings:
 *   ENTER   approve "once"       (when prompt pending)
 *   LEFT    approve "deny"       (when prompt pending) / prev persona (no prompt)
 *   RIGHT   approve "always"     (when prompt pending) / next persona (no prompt)
 *   UP/DOWN scroll entries
 *   JOYCON  send {"cmd":"status"}
 *   ESC     go back to previous screen
 *
 * Keys other than UP/DOWN/JOYCON/ESC are no-op when there is no pending
 * prompt and no persona switch, to avoid sending unmatched decisions.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef MAIN_SCREEN_H
#define MAIN_SCREEN_H

#include "screen_manager.h"
#include "buddy_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Exported Screen_t
 * --------------------------------------------------------------------------- */
extern Screen_t buddy_main_screen;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Push the latest state snapshot to the main screen.
 *
 * Thread-safe: acquires the LVGL lock internally, so it can be called from
 * any task (including the WS RX task).  When the screen is not yet loaded
 * the snapshot is cached; the next init() will re-draw using the cache
 * plus buddy_state_snapshot().
 *
 * @param[in] state  State snapshot (non-NULL)
 * @return none
 */
void buddy_main_screen_update_state(const buddy_tama_state_t *state);

/**
 * @brief Get the 11-char sid copy of the currently selected session.
 *
 * Thread-safe: acquires the LVGL lock internally.  Used by ASR / link-layer
 * for async sampling without a UI thread switch.  Returns 0 when the
 * selected row is not a session row or the snapshot has no such session.
 *
 * @param[out] out_sid  Output buffer (at least 12 bytes: 11 chars + NUL)
 * @param[in]  cap      Capacity of out_sid, must be >= 12
 * @return Number of bytes written (excluding NUL), 0 = no valid selection
 */
size_t buddy_main_screen_get_selected_sid(char *out_sid, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* MAIN_SCREEN_H */
