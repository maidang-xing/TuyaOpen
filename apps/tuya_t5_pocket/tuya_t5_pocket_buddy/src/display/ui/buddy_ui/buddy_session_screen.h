/**
 * @file buddy_session_screen.h
 * @brief Claude Desktop Buddy session detail screen.
 *
 * Entered from the main screen when the user selects a session on the
 * Sessions page. The caller sets buddy_session_screen.state_data to a
 * const buddy_session_t * before calling screen_load().
 *
 * Layout (384x168):
 *   Header  0-20   black bg, white text: title / clock / status
 *   Body   20-168  left panel (148px) | 2px divider | right panel (232px)
 *     Left  - session metadata (name, id, model, tokens, running, project)
 *     Right - session log entries, scrollable
 *
 * Keys:
 *   ESC / LEFT  -> screen_back()
 *   UP / DOWN   -> scroll log panel (future use)
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_SESSION_SCREEN_H
#define BUDDY_SESSION_SCREEN_H

#include "screen_manager.h"
#include "buddy_data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_session_screen;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_SESSION_SCREEN_H */
