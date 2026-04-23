/**
 * @file buddy_approval_screen.c
 * @brief 权限审批独立屏（B&W，无重叠布局）。
 *
 * 所有标签 pad_all=0，显式设置单行高度，避免 LVGL 内边距引起的字符重叠。
 * 字段间距 = 字体高度 + 3px 间隙，保证每行独立可见。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "buddy_approval_screen.h"
#include "buddy_ble.h"
#include "buddy_data.h"
#include "buddy_led.h"
#include "lv_vendor.h"
#include "screen_manager.h"
#include "tal_api.h"
#include "tuya_cloud_types.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Constants
 * --------------------------------------------------------------------------- */
#define FONT_L   &lv_font_terminusTTF_Bold_18
#define FONT_M   &lv_font_terminusTTF_Bold_16
#define FONT_S   &lv_font_terminusTTF_Bold_14

/* Font heights in pixels (no extra padding) */
#define H_M      16
#define H_S      14

#define SCR_W    AI_PET_SCREEN_WIDTH   /* 384 */
#define SCR_H    AI_PET_SCREEN_HEIGHT  /* 168 */
#define PAD      8
#define INNER_W  (SCR_W - PAD * 2)    /* 368 */

/* Title bar */
#define TITLE_H  20

/* Y positions (all relative to screen root, with PAD=8 left margin).
 * Derived: title_bar 0-20, then content at y=TITLE_H+gap.
 * Each item y = prev_y + prev_h + gap(3).
 *   tool:  TITLE_H + 4  = 24, h=H_M=16, ends=40
 *   hint:  40 + 4       = 44, h=H_S=14, ends=58
 *   ctx:   58 + 4       = 62, h=H_S=14, ends=76
 *   sep:   76 + 3       = 79, h=1,      ends=80
 *   opt0:  80 + 4       = 84, h=H_M=16, ends=100
 *   opt1:  100 + 6      = 106,h=H_M=16, ends=122
 *   opt2:  122 + 6      = 128,h=H_M=16, ends=144  (< 168 ok)
 */
#define Y_TOOL   (TITLE_H + 4)    /* 24 */
#define Y_HINT   (Y_TOOL + H_M + 4) /* 44 */
#define Y_CTX    (Y_HINT + H_S + 4) /* 62 */
#define Y_SEP    (Y_CTX  + H_S + 3) /* 79 */
#define Y_OPT0   (Y_SEP  + 1  + 4)  /* 84 */
#define Y_OPT1   (Y_OPT0 + H_M + 6) /* 106 */
#define Y_OPT2   (Y_OPT1 + H_M + 6) /* 128 */
#define OPT_COUNT 3

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *s_screen   = NULL;
STATIC lv_obj_t *s_tool     = NULL;
STATIC lv_obj_t *s_hint     = NULL;
STATIC lv_obj_t *s_ctx      = NULL;
STATIC lv_obj_t *s_opts[OPT_COUNT];

STATIC buddy_tama_state_t s_snap   = {0};
STATIC uint8_t            s_cursor = 0;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __screen_init(VOID_T);
STATIC VOID_T __screen_deinit(VOID_T);
STATIC VOID_T __keyboard_event_cb(lv_event_t *e);
STATIC VOID_T __refresh_cursor(VOID_T);
STATIC VOID_T __send_and_back(const char *decision);
STATIC lv_obj_t *__info_lbl(lv_obj_t *parent, int32_t y, int32_t h,
                             const lv_font_t *font);

Screen_t buddy_approval_screen = {
    .init       = __screen_init,
    .deinit     = __screen_deinit,
    .screen_obj = &s_screen,
    .name       = "buddy_approval_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Label helper — single-line, zero padding, explicit height
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *__info_lbl(lv_obj_t *parent, int32_t y, int32_t h,
                             const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_size(l, INNER_W, h);          /* explicit width+height → single line */
    lv_obj_set_style_pad_all(l, 0, 0);       /* remove LVGL default padding */
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_black(), 0);
    lv_obj_set_pos(l, PAD, y);
    return l;
}

/* ---------------------------------------------------------------------------
 * Init
 * --------------------------------------------------------------------------- */
STATIC VOID_T __screen_init(VOID_T)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_screen, lv_color_white(), 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Title bar */
    lv_obj_t *title_bar = lv_obj_create(s_screen);
    lv_obj_set_size(title_bar, SCR_W, TITLE_H);
    lv_obj_set_pos(title_bar, 0, 0);
    lv_obj_set_style_bg_color(title_bar, lv_color_black(), 0);
    lv_obj_set_style_border_width(title_bar, 0, 0);
    lv_obj_set_style_radius(title_bar, 0, 0);
    lv_obj_set_style_pad_all(title_bar, 0, 0);
    lv_obj_clear_flag(title_bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(title_bar);
    lv_label_set_text(title_lbl, "PERMISSION REQUEST");
    lv_obj_set_style_text_font(title_lbl, FONT_M, 0);
    lv_obj_set_style_text_color(title_lbl, lv_color_white(), 0);
    lv_obj_set_style_pad_all(title_lbl, 0, 0);
    lv_obj_align(title_lbl, LV_ALIGN_LEFT_MID, PAD, 0);

    /* Info rows — explicit single-line height, zero padding */
    s_tool = __info_lbl(s_screen, Y_TOOL, H_M, FONT_M);
    s_hint = __info_lbl(s_screen, Y_HINT, H_S, FONT_S);
    s_ctx  = __info_lbl(s_screen, Y_CTX,  H_S, FONT_S);

    /* Separator */
    lv_obj_t *sep = lv_obj_create(s_screen);
    lv_obj_set_size(sep, INNER_W, 1);
    lv_obj_set_pos(sep, PAD, Y_SEP);
    lv_obj_set_style_bg_color(sep, lv_color_black(), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);

    /* Option labels */
    static const int32_t OPT_Y[OPT_COUNT] = {Y_OPT0, Y_OPT1, Y_OPT2};
    static const char *const OPT_TEXT[OPT_COUNT] = {
        "Approve (once)", "Approve (always)", "Deny"
    };
    for (uint32_t i = 0; i < OPT_COUNT; i++) {
        s_opts[i] = lv_label_create(s_screen);
        lv_label_set_long_mode(s_opts[i], LV_LABEL_LONG_DOT);
        lv_obj_set_size(s_opts[i], INNER_W, H_M);
        lv_obj_set_style_pad_all(s_opts[i], 0, 0);
        lv_obj_set_style_text_font(s_opts[i], FONT_M, 0);
        lv_obj_set_style_text_color(s_opts[i], lv_color_black(), 0);
        lv_obj_set_pos(s_opts[i], PAD, OPT_Y[i]);
        char buf[32];
        (VOID_T)snprintf(buf, sizeof(buf), "  %s", OPT_TEXT[i]);
        lv_label_set_text(s_opts[i], buf);
    }

    /* Load snapshot */
    buddy_ble_snapshot(&s_snap);
    s_cursor = 0;

    if (s_tool)
        lv_label_set_text_fmt(s_tool, "Tool:  %s",
            s_snap.prompt_tool[0] ? s_snap.prompt_tool : "?");
    if (s_hint)
        lv_label_set_text_fmt(s_hint, "Info:  %s",
            s_snap.prompt_hint[0] ? s_snap.prompt_hint : "-");
    if (s_ctx)
        lv_label_set_text_fmt(s_ctx, "Owner: %s   S:%u  R:%u",
            s_snap.owner_name[0] ? s_snap.owner_name : "-",
            (unsigned)s_snap.sessions_total,
            (unsigned)s_snap.sessions_running);

    __refresh_cursor();

    lv_obj_add_event_cb(s_screen, __keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), s_screen);
    lv_group_focus_obj(s_screen);

    PR_NOTICE("[%s] init id=%s",
              buddy_approval_screen.name,
              s_snap.prompt_id[0] ? s_snap.prompt_id : "-");
}

STATIC VOID_T __screen_deinit(VOID_T)
{
    if (s_screen) {
        lv_obj_remove_event_cb(s_screen, __keyboard_event_cb);
        lv_group_remove_obj(s_screen);
    }
    s_tool = s_hint = s_ctx = NULL;
    for (uint32_t i = 0; i < OPT_COUNT; i++) s_opts[i] = NULL;
    s_cursor = 0;
}

/* ---------------------------------------------------------------------------
 * Cursor — selected row inverted
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_cursor(VOID_T)
{
    static const char *const OPT_TEXT[OPT_COUNT] = {
        "Approve (once)", "Approve (always)", "Deny"
    };
    for (uint32_t i = 0; i < OPT_COUNT; i++) {
        if (!s_opts[i]) continue;
        char buf[32];
        if (i == (uint32_t)s_cursor) {
            (VOID_T)snprintf(buf, sizeof(buf), "> %s", OPT_TEXT[i]);
            lv_obj_set_style_text_color(s_opts[i], lv_color_white(), 0);
            lv_obj_set_style_bg_color(s_opts[i],   lv_color_black(), 0);
            lv_obj_set_style_bg_opa(s_opts[i],     LV_OPA_COVER, 0);
        } else {
            (VOID_T)snprintf(buf, sizeof(buf), "  %s", OPT_TEXT[i]);
            lv_obj_set_style_text_color(s_opts[i], lv_color_black(), 0);
            lv_obj_set_style_bg_opa(s_opts[i],     LV_OPA_TRANSP, 0);
        }
        lv_label_set_text(s_opts[i], buf);
    }
}

/* ---------------------------------------------------------------------------
 * Decision + key handler
 * --------------------------------------------------------------------------- */
STATIC VOID_T __send_and_back(const char *decision)
{
    /* Turn off the LED regardless of outcome */
    (VOID_T)buddy_led_set(BUDDY_LED_STATE_OFF);

    if (!decision || !s_snap.has_prompt || !s_snap.prompt_id[0]) {
        screen_back();
        return;
    }
    (VOID_T)buddy_ble_send_permission(s_snap.prompt_id, decision);
    PR_NOTICE("[%s] decision=%s id=%s",
              buddy_approval_screen.name, decision, s_snap.prompt_id);
    screen_back();
}

STATIC VOID_T __keyboard_event_cb(lv_event_t *e)
{
    static const char *const DECISIONS[OPT_COUNT] = {
        "once", "always", "deny"
    };
    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case KEY_UP:
        s_cursor = (s_cursor == 0) ? (uint8_t)(OPT_COUNT - 1)
                                   : (uint8_t)(s_cursor - 1);
        lv_vendor_disp_lock();
        __refresh_cursor();
        lv_vendor_disp_unlock();
        break;
    case KEY_DOWN:
        s_cursor = (uint8_t)((s_cursor + 1) % OPT_COUNT);
        lv_vendor_disp_lock();
        __refresh_cursor();
        lv_vendor_disp_unlock();
        break;
    case KEY_ENTER:
        __send_and_back(DECISIONS[s_cursor]);
        break;
    case KEY_ESC:
        __send_and_back("deny");
        break;
    default:
        break;
    }
}
