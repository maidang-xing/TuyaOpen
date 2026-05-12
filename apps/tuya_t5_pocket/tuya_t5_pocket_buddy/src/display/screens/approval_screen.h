/**
 * @file approval_screen.h
 * @brief Claude permission approval screen (full-screen variant).
 *
 * This screen is an optional alternative to the inline card in main_screen:
 * when the UI decides to show approval as a standalone page (e.g. navigating
 * from another context), it can push this Screen_t onto the stack.
 *
 * Key bindings (consistent with UI_INTERACTION spec):
 *   ENTER  - permission "once"
 *   LEFT   - permission "deny"
 *   RIGHT  - permission "always"
 *   ESC    - screen_back() (return to main without sending any decision)
 *
 * Shares data source with main_screen prompt card (buddy_state_snapshot);
 * no extra state cache. When prompt_id is absent the screen can still open
 * but all decision keys are disabled.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef APPROVAL_SCREEN_H
#define APPROVAL_SCREEN_H

#include "screen_manager.h"
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Exported Screen_t
 * --------------------------------------------------------------------------- */
extern Screen_t buddy_approval_screen;

#ifdef __cplusplus
}
#endif

#endif /* APPROVAL_SCREEN_H */
