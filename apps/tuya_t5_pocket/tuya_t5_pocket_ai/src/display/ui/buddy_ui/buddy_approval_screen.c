/**
 * @file buddy_approval_screen.c
 * @brief 全屏权限审批屏实现。
 *
 * 提供与 main_screen 内联 prompt card 等价的视觉与键位，但独立成页。
 * 数据来自 buddy_ble_snapshot()；按键直接调用 buddy_ble_send_permission()
 * 并 screen_back() 回到调用方。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "buddy_approval_screen.h"
#include "buddy_ble.h"
#include "buddy_data.h"
#include "lv_vendor.h"
#include "screen_manager.h"
#include "tal_api.h"
#include "tuya_cloud_types.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Fonts & constants
 * --------------------------------------------------------------------------- */
#define APPROVAL_FONT_TITLE   &lv_font_terminusTTF_Bold_18
#define APPROVAL_FONT_CONTENT &lv_font_terminusTTF_Bold_16
#define APPROVAL_FONT_HINT    &lv_font_terminusTTF_Bold_14

#define APPROVAL_SCR_W        AI_PET_SCREEN_WIDTH
#define APPROVAL_SCR_H        AI_PET_SCREEN_HEIGHT

#define APPROVAL_HOT_COLOR    lv_color_make(0xFA, 0x20, 0x20)
#define APPROVAL_ACCENT_COLOR lv_color_make(0x10, 0x60, 0xC0)

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *s_screen = NULL;
STATIC lv_obj_t *s_title  = NULL;
STATIC lv_obj_t *s_tool   = NULL;
STATIC lv_obj_t *s_hint   = NULL;
STATIC lv_obj_t *s_id     = NULL;
STATIC lv_obj_t *s_footer = NULL;

STATIC buddy_tama_state_t s_snapshot = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __screen_init(VOID_T);
STATIC VOID_T __screen_deinit(VOID_T);
STATIC VOID_T __keyboard_event_cb(lv_event_t *e);
STATIC VOID_T __refresh_locked(VOID_T);
STATIC VOID_T __send_and_back(const char *decision);

/* ---------------------------------------------------------------------------
 * Exported Screen_t
 * --------------------------------------------------------------------------- */
Screen_t buddy_approval_screen = {
    .init       = __screen_init,
    .deinit     = __screen_deinit,
    .screen_obj = &s_screen,
    .name       = "buddy_approval_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Internal impl
 * --------------------------------------------------------------------------- */
/**
 * @brief 构建屏幕结构。
 */
STATIC VOID_T __screen_init(VOID_T)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, APPROVAL_SCR_W, APPROVAL_SCR_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_make(0xFF, 0xF2, 0xE0), 0);
    lv_obj_set_style_pad_all(s_screen, 8, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    s_title = lv_label_create(s_screen);
    lv_label_set_text(s_title, "PERMISSION REQUEST");
    lv_obj_set_style_text_font(s_title, APPROVAL_FONT_TITLE, 0);
    lv_obj_set_style_text_color(s_title, APPROVAL_HOT_COLOR, 0);
    lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_tool = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_tool, APPROVAL_FONT_CONTENT, 0);
    lv_obj_set_style_text_color(s_tool, lv_color_black(), 0);
    lv_obj_align(s_tool, LV_ALIGN_TOP_LEFT, 0, 28);

    s_hint = lv_label_create(s_screen);
    lv_label_set_long_mode(s_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_hint, APPROVAL_SCR_W - 32);
    lv_obj_set_style_text_font(s_hint, APPROVAL_FONT_HINT, 0);
    lv_obj_set_style_text_color(s_hint, lv_color_black(), 0);
    lv_obj_align(s_hint, LV_ALIGN_TOP_LEFT, 0, 56);

    s_id = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_id, APPROVAL_FONT_HINT, 0);
    lv_obj_set_style_text_color(s_id, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_align(s_id, LV_ALIGN_BOTTOM_LEFT, 0, -24);

    s_footer = lv_label_create(s_screen);
    lv_obj_set_style_text_font(s_footer, APPROVAL_FONT_HINT, 0);
    lv_obj_set_style_text_color(s_footer, APPROVAL_ACCENT_COLOR, 0);
    lv_obj_align(s_footer, LV_ALIGN_BOTTOM_LEFT, 0, 0);

    buddy_ble_snapshot(&s_snapshot);
    __refresh_locked();

    lv_obj_add_event_cb(s_screen, __keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), s_screen);
    lv_group_focus_obj(s_screen);

    PR_NOTICE("[%s] init pending=%d id=%s",
              buddy_approval_screen.name,
              (int)s_snapshot.has_prompt,
              s_snapshot.prompt_id[0] ? s_snapshot.prompt_id : "-");
}

/**
 * @brief 销毁屏幕。
 */
STATIC VOID_T __screen_deinit(VOID_T)
{
    if (s_screen != NULL) {
        lv_obj_remove_event_cb(s_screen, __keyboard_event_cb);
        lv_group_remove_obj(s_screen);
    }
    s_title = s_tool = s_hint = s_id = s_footer = NULL;
}

/**
 * @brief 按 snapshot 渲染内容。
 */
STATIC VOID_T __refresh_locked(VOID_T)
{
    if (s_tool != NULL) {
        lv_label_set_text_fmt(s_tool, "Tool: %s",
                              s_snapshot.prompt_tool[0] ? s_snapshot.prompt_tool : "(unknown)");
    }
    if (s_hint != NULL) {
        lv_label_set_text_fmt(s_hint, "Info: %s",
                              s_snapshot.prompt_hint[0] ? s_snapshot.prompt_hint : "-");
    }
    if (s_id != NULL) {
        lv_label_set_text_fmt(s_id, "id: %s",
                              s_snapshot.prompt_id[0] ? s_snapshot.prompt_id : "-");
    }
    if (s_footer != NULL) {
        if (s_snapshot.has_prompt) {
            lv_label_set_text(s_footer,
                              "ENTER=OK   LEFT=deny   RIGHT=always   ESC=back");
        } else {
            lv_label_set_text(s_footer, "no pending prompt    ESC=back");
        }
    }
}

/**
 * @brief 下发决策并回到上一屏。
 */
STATIC VOID_T __send_and_back(const char *decision)
{
    if (decision == NULL) {
        return;
    }
    if (!s_snapshot.has_prompt || s_snapshot.prompt_id[0] == '\0') {
        PR_WARN("approval screen: no pending prompt, ignoring \"%s\"", decision);
        return;
    }
    (VOID_T)buddy_ble_send_permission(s_snapshot.prompt_id, decision);
    PR_NOTICE("approval decision=%s id=%s", decision, s_snapshot.prompt_id);
    screen_back();
}

/**
 * @brief 键事件回调。
 */
STATIC VOID_T __keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case KEY_ENTER:
        __send_and_back("once");
        break;
    case KEY_LEFT:
        __send_and_back("deny");
        break;
    case KEY_RIGHT:
        __send_and_back("always");
        break;
    case KEY_ESC:
        screen_back();
        break;
    default:
        break;
    }
}
