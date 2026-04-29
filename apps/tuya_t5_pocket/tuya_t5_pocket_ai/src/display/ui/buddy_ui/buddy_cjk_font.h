/**
 * @file buddy_cjk_font.h
 * @brief CJK fallback font wrappers for buddy UI labels
 * @version 1.0
 * @date 2026-04-27
 * @copyright Copyright (c) Tuya Inc.
 */
#ifndef __BUDDY_CJK_FONT_H__
#define __BUDDY_CJK_FONT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/* ---------------------------------------------------------------------------
 * Public fonts
 *
 * These are RAM-backed copies of `lv_font_terminusTTF_Bold_14/16/18` with
 * `.fallback` chained to `ui_font_puhui_18_2` so any CJK glyph that the
 * Latin-only Terminus font cannot resolve is rendered through Puhui. ASCII
 * layout matches Terminus exactly (same glyph data, line height, base line).
 *
 * Always use these aliases in buddy UI code instead of the raw Terminus
 * fonts, otherwise CJK text from .claude logs (project / session / tool
 * names, approval params, etc.) will render as `?`.
 * --------------------------------------------------------------------------- */
extern lv_font_t buddy_font_s;
extern lv_font_t buddy_font_m;
extern lv_font_t buddy_font_l;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize buddy CJK fallback fonts
 * @return none
 * @note Call once at boot before any buddy_*_screen_init(). Safe to call
 *       multiple times.
 */
void buddy_cjk_font_init(void);

#ifdef __cplusplus
}
#endif
#endif /* __BUDDY_CJK_FONT_H__ */
