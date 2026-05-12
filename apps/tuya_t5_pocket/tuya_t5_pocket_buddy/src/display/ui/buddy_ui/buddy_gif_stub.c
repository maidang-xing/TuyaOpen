/**
 * @file buddy_gif_stub.c
 * @brief 自定义 GIF 角色占位实现。
 *
 * 只绘制一个带虚线边框的矩形 + 居中的两行文字："<name>" + "(gif stub)"。
 * M4-Tools 落地 GIF 解码器后，本模块会整体替换为 lv_gif 或等价实现。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "buddy_gif_stub.h"
#include "ascii_persona.h"
#include "tal_api.h"
#include "tuya_cloud_types.h"
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *s_box  = NULL;
STATIC lv_obj_t *s_name = NULL;
STATIC lv_obj_t *s_hint = NULL;
STATIC char      s_current_name[24] = "(gif)";

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief 创建占位视图。
 */
VOID_T buddy_gif_stub_attach(lv_obj_t *parent, int x, int y)
{
    if (parent == NULL) {
        return;
    }
    if (s_box != NULL) {
        buddy_gif_stub_detach();
    }

    s_box = lv_obj_create(parent);
    lv_obj_set_size(s_box, ASCII_CANVAS_W, ASCII_CANVAS_H);
    lv_obj_set_pos(s_box, x, y);
    lv_obj_set_style_pad_all(s_box, 4, 0);
    lv_obj_set_style_bg_color(s_box, lv_color_white(), 0);
    lv_obj_set_style_border_color(s_box, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_box, 1, 0);
    lv_obj_set_style_border_side(s_box, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_radius(s_box, 4, 0);
    lv_obj_clear_flag(s_box, LV_OBJ_FLAG_SCROLLABLE);

    s_name = lv_label_create(s_box);
    lv_label_set_text(s_name, s_current_name);
    lv_obj_set_style_text_font(s_name, &lv_font_terminusTTF_Bold_16, 0);
    lv_obj_set_style_text_color(s_name, lv_color_black(), 0);
    lv_obj_align(s_name, LV_ALIGN_CENTER, 0, -12);

    s_hint = lv_label_create(s_box);
    lv_label_set_text(s_hint, "(gif stub - M4)");
    lv_obj_set_style_text_font(s_hint, &lv_font_terminusTTF_Bold_14, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_make(0x60, 0x60, 0x60), 0);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 10);

    PR_INFO("gif_stub attached (name=\"%s\")", s_current_name);
}

/**
 * @brief 释放占位视图。
 */
VOID_T buddy_gif_stub_detach(VOID_T)
{
    if (s_box != NULL) {
        lv_obj_del(s_box);
        s_box  = NULL;
        s_name = NULL;
        s_hint = NULL;
    }
}

/**
 * @brief 刷新文本（用户所选自定义角色名）。
 */
VOID_T buddy_gif_stub_set_name(const char *name)
{
    const char *n = (name != NULL && name[0] != '\0') ? name : "(gif)";
    (VOID_T)snprintf(s_current_name, sizeof(s_current_name), "%s", n);
    if (s_name != NULL) {
        lv_label_set_text(s_name, s_current_name);
    }
}
