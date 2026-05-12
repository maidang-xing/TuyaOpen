/**
 * @file ascii_persona.c
 * @brief ASCII 人格渲染框架实现。
 *
 * 单 sprite label + 预分配 overlay label 池，每 tick 先隐藏所有 overlay，
 * 然后调用当前人格状态函数，状态函数通过 ascii_* 原语填充 sprite 文本与
 * overlay 位置。帧率由 buddy_main_screen 的 lv_timer 决定（默认 100 ms）。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "ascii_persona.h"
#include "persona_registry.h"
#include "tal_api.h"
#include "tuya_cloud_types.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
/* overlay label 池大小；粒子最多的人格动画（capybara celebrate 6 + sparkle）
 * 大约需要 12 个槽位，留余量到 24。超出后静默丢弃。 */
#define ASCII_OVERLAY_POOL 24

/* sprite 最大行数（原参考项目均为 5 行，预留到 6）与单行最大列数。 */
#define ASCII_SPRITE_ROWS      6
#define ASCII_SPRITE_COLS      16
#define ASCII_SPRITE_BUF_SZ    ((ASCII_SPRITE_COLS + 1) * ASCII_SPRITE_ROWS + 1)

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *s_canvas = NULL;
STATIC lv_obj_t *s_sprite = NULL;
STATIC lv_obj_t *s_overlay[ASCII_OVERLAY_POOL];
STATIC uint8_t   s_overlay_used = 0;

STATIC int  s_cursor_x = 0;
STATIC int  s_cursor_y = 0;
STATIC uint32_t s_tick  = 0;

STATIC uint8_t                s_last_persona  = 0xFF;
STATIC buddy_persona_state_e  s_last_state    = BUDDY_PERSONA_STATE_COUNT;

STATIC char s_sprite_buf[ASCII_SPRITE_BUF_SZ];

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC void __reset_frame(void);
STATIC void __ensure_overlay(uint8_t idx);

/* ---------------------------------------------------------------------------
 * Drawing primitives
 * --------------------------------------------------------------------------- */
/**
 * @brief 居中写入多行 sprite。
 * @param[in] lines   字符串数组
 * @param[in] n_lines 行数，≤ ASCII_SPRITE_ROWS
 * @param[in] y_off   相对 ASCII_Y_BASE 的 y 偏移
 * @param[in] color   忽略
 * @param[in] x_off   相对 ASCII_X_CENTER 的 x 偏移
 * @return none
 */
void ascii_print_sprite(const char *const *lines, uint8_t n_lines, int y_off, uint16_t color, int x_off)
{
    (void)color;
    if (s_sprite == NULL || lines == NULL || n_lines == 0) {
        return;
    }
    if (n_lines > ASCII_SPRITE_ROWS) {
        n_lines = ASCII_SPRITE_ROWS;
    }

    s_sprite_buf[0] = '\0';
    size_t off = 0;
    for (uint8_t i = 0; i < n_lines; i++) {
        const char *ln = (lines[i] != NULL) ? lines[i] : "";
        size_t ln_len = strlen(ln);
        if (ln_len > ASCII_SPRITE_COLS) {
            ln_len = ASCII_SPRITE_COLS;
        }
        if (off + ln_len + 2U >= sizeof(s_sprite_buf)) {
            break;
        }
        memcpy(&s_sprite_buf[off], ln, ln_len);
        off += ln_len;
        if (i + 1U < n_lines) {
            s_sprite_buf[off++] = '\n';
        }
    }
    s_sprite_buf[off] = '\0';

    lv_label_set_text(s_sprite, s_sprite_buf);
    int sprite_w = ASCII_SPRITE_COLS * ASCII_CHAR_W;
    int x = (ASCII_CANVAS_W - sprite_w) / 2 + x_off;
    int y = ASCII_Y_BASE + y_off;
    lv_obj_set_pos(s_sprite, x, y);
}

/**
 * @brief 居中单行文本。
 */
void ascii_print_line(const char *line, int y_px, uint16_t color, int x_off)
{
    (void)color;
    if (line == NULL || line[0] == '\0') {
        return;
    }
    __ensure_overlay(s_overlay_used);
    lv_obj_t *lbl = s_overlay[s_overlay_used];
    if (lbl == NULL) {
        return;
    }
    lv_label_set_text(lbl, line);
    size_t len = strlen(line);
    int w = (int)len * ASCII_CHAR_W;
    int x = (ASCII_CANVAS_W - w) / 2 + x_off;
    lv_obj_set_pos(lbl, x, y_px);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    s_overlay_used++;
}

/**
 * @brief 移动绘制光标。
 */
void ascii_set_cursor(int x, int y)
{
    s_cursor_x = x;
    s_cursor_y = y;
}

/**
 * @brief 设置前景色（忽略，保持签名兼容）。
 */
void ascii_set_color(uint16_t color)
{
    (void)color;
}

/**
 * @brief 在当前光标位置绘制 overlay 字符串。
 */
void ascii_print(const char *s)
{
    if (s == NULL || s[0] == '\0') {
        return;
    }
    if (s_overlay_used >= ASCII_OVERLAY_POOL) {
        return;
    }
    __ensure_overlay(s_overlay_used);
    lv_obj_t *lbl = s_overlay[s_overlay_used];
    if (lbl == NULL) {
        return;
    }
    lv_label_set_text(lbl, s);
    lv_obj_set_pos(lbl, s_cursor_x, s_cursor_y);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    s_overlay_used++;
}

/* ---------------------------------------------------------------------------
 * Canvas lifecycle
 * --------------------------------------------------------------------------- */
/**
 * @brief 在 parent 内创建人格画布。
 */
void ascii_persona_attach(lv_obj_t *parent, int x, int y)
{
    if (parent == NULL) {
        return;
    }
    if (s_canvas != NULL) {
        ascii_persona_detach();
    }

    s_canvas = lv_obj_create(parent);
    lv_obj_set_size(s_canvas, ASCII_CANVAS_W, ASCII_CANVAS_H);
    lv_obj_set_pos(s_canvas, x, y);
    lv_obj_set_style_pad_all(s_canvas, 0, 0);
    lv_obj_set_style_border_width(s_canvas, 0, 0);
    lv_obj_set_style_radius(s_canvas, 0, 0);
    lv_obj_set_style_bg_color(s_canvas, lv_color_white(), 0);
    lv_obj_clear_flag(s_canvas, LV_OBJ_FLAG_SCROLLABLE);

    s_sprite = lv_label_create(s_canvas);
    lv_label_set_text(s_sprite, "");
    lv_obj_set_style_text_font(s_sprite, &lv_font_terminusTTF_Bold_14, 0);
    lv_obj_set_style_text_color(s_sprite, lv_color_black(), 0);
    lv_obj_set_style_text_line_space(s_sprite, 0, 0);
    lv_obj_set_pos(s_sprite, 0, ASCII_Y_BASE);

    for (uint8_t i = 0; i < ASCII_OVERLAY_POOL; i++) {
        s_overlay[i] = NULL;
    }
    s_overlay_used = 0;
    s_tick = 0;
    s_last_persona = 0xFF;
    s_last_state   = BUDDY_PERSONA_STATE_COUNT;
}

/**
 * @brief 释放画布。
 */
void ascii_persona_detach(void)
{
    if (s_canvas != NULL) {
        lv_obj_del(s_canvas);
        s_canvas = NULL;
    }
    s_sprite = NULL;
    for (uint8_t i = 0; i < ASCII_OVERLAY_POOL; i++) {
        s_overlay[i] = NULL;
    }
    s_overlay_used = 0;
}

/**
 * @brief 推进一 tick 并重绘。
 */
void ascii_persona_tick(uint8_t persona_id, buddy_persona_state_e state)
{
    if (s_canvas == NULL) {
        return;
    }
    if (state >= BUDDY_PERSONA_STATE_COUNT) {
        state = BUDDY_PERSONA_STATE_IDLE;
    }

    const persona_entry_t *entry = persona_registry_get_by_id(persona_id);
    if (entry == NULL || entry->persona == NULL) {
        return;
    }
    ascii_state_fn fn = entry->persona->states[state];
    if (fn == NULL) {
        return;
    }

    if (persona_id != s_last_persona || state != s_last_state) {
        s_tick = 0;
        s_last_persona = persona_id;
        s_last_state   = state;
        PR_DEBUG("persona_tick transition persona=%s state=%d",
                 entry->persona->name ? entry->persona->name : "?", (int)state);
    }

    __reset_frame();
    fn(s_tick);
    s_tick++;

    /* 隐藏本帧未用到的 overlay 槽位。 */
    for (uint8_t i = s_overlay_used; i < ASCII_OVERLAY_POOL; i++) {
        if (s_overlay[i] != NULL) {
            lv_obj_add_flag(s_overlay[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 重置 tick 计数。
 */
void ascii_persona_reset_tick(uint32_t t)
{
    s_tick = t;
}

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief 清空本帧状态：隐藏 overlay，清空 sprite，复位光标。
 */
STATIC void __reset_frame(void)
{
    s_overlay_used = 0;
    s_cursor_x = 0;
    s_cursor_y = 0;
    for (uint8_t i = 0; i < ASCII_OVERLAY_POOL; i++) {
        if (s_overlay[i] != NULL) {
            lv_obj_add_flag(s_overlay[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

/**
 * @brief 确保 overlay[idx] 已创建（惰性分配）。
 */
STATIC void __ensure_overlay(uint8_t idx)
{
    if (idx >= ASCII_OVERLAY_POOL || s_canvas == NULL) {
        return;
    }
    if (s_overlay[idx] != NULL) {
        return;
    }
    lv_obj_t *lbl = lv_label_create(s_canvas);
    if (lbl == NULL) {
        return;
    }
    lv_obj_set_style_text_font(lbl, &lv_font_terminusTTF_Bold_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_black(), 0);
    lv_obj_set_style_text_line_space(lbl, 0, 0);
    lv_label_set_text(lbl, "");
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
    s_overlay[idx] = lbl;
}
