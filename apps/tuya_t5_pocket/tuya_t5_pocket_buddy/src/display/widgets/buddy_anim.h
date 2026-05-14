/**
 * @file buddy_anim.h
 * @brief Terminal-style animation helpers for Claude Buddy UI.
 */
#ifndef BUDDY_ANIM_H
#define BUDDY_ANIM_H

#include "lvgl.h"
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Reveal text one UTF-8 character at a time via lv_timer.
 *        Cancels any in-progress typewriter on the same label first.
 *        The label's text is replaced (not appended) incrementally.
 * @param label       target lv_obj_t label
 * @param text        full text to reveal (copied internally)
 * @param interval_ms milliseconds between each character reveal
 */
void buddy_anim_typewriter(lv_obj_t *label, const char *text, uint32_t interval_ms);

/**
 * @brief Stop and clean up any typewriter animation on label.
 *        Safe to call even if no animation is running.
 */
void buddy_anim_typewriter_stop(lv_obj_t *label);

/**
 * @brief Periodically toggle obj bg color between color_a and color_b.
 *        Cancels any prior blink on the same obj first.
 * @param obj        target lv_obj_t
 * @param color_a    first color (shown first)
 * @param color_b    second color
 * @param period_ms  ms between each color toggle
 */
void buddy_anim_blink(lv_obj_t *obj, lv_color_t color_a, lv_color_t color_b, uint32_t period_ms);

/**
 * @brief Stop blink animation on obj, restore color_a.
 *        Safe to call even if no blink is running.
 */
void buddy_anim_blink_stop(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_ANIM_H */
