/**
 * @file buddy_pie_screen.h
 * @brief Claude Desktop Buddy — Tab 4 "Model Usage" screen.
 *
 * Shows per-model output token usage as horizontal percentage bars.
 *
 * Layout (384×168, monochrome):
 *   Header   0..20   Black bg, white text
 *   Title   20..36   "Model Token Usage"
 *   Rows    40..155  Up to 4 model rows (20px each)
 *   Nav    162..168  Tab bar  "St|Hm|Ch|Pi"
 *
 * Navigation:
 *   LEFT / ESC — screen_back()
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_PIE_SCREEN_H
#define BUDDY_PIE_SCREEN_H

#include "screen_manager.h"
#include "buddy_data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_pie_screen;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_PIE_SCREEN_H */
