/**
 * @file buddy_chart_screen.h
 * @brief Claude Desktop Buddy — Tab 3 "Chart" screen (28-day token bar chart).
 *
 * Layout (384×168, monochrome):
 *   Header   0..20   Black bg, white text
 *   Title   20..36   Period label + key hint
 *   Chart   36..138  Bar chart area (102px usable)
 *   X-axis 138..154  Date/day abbreviation labels
 *   Nav    162..168  Tab bar  "St|Hm|Ch|Pi"
 *
 * Navigation:
 *   UP / DOWN  — cycle period (7d / 14d / 28d)
 *   RIGHT      — go to buddy_pie_screen (Tab 4)
 *   LEFT / ESC — screen_back()
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_CHART_SCREEN_H
#define BUDDY_CHART_SCREEN_H

#include "screen_manager.h"
#include "buddy_data.h"

#ifdef __cplusplus
extern "C" {
#endif

extern Screen_t buddy_chart_screen;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_CHART_SCREEN_H */
