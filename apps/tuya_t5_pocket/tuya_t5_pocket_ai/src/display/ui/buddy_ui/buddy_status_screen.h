/**
 * @file buddy_status_screen.h
 * @brief Claude Buddy status/stats screen (tab 1 of 4).
 *
 * Full-screen statistics view for the 384×168 monochrome display.
 * Shows Claude version, daily/total token counts, costs, model, and owner.
 *
 * Layout:
 *   Header  (y=0..20)    black bg, white text: title / clock / status
 *   Content (y=20..162)  white bg, black text: 8 stat rows
 *   Nav bar (y=162..168) 4 tabs, "St" tab highlighted
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_STATUS_SCREEN_H
#define BUDDY_STATUS_SCREEN_H

#include "screen_manager.h"
#include "buddy_data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_status_screen;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_STATUS_SCREEN_H */
