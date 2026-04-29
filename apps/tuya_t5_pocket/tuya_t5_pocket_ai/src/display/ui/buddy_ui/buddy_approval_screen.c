/**
 * @file buddy_approval_screen.c
 * @brief 权限审批独立屏（B&W，4 行信息）。
 *
 * 信息区（4 行，无重叠）：
 *   Tool    — 工具名称（FONT_M，突出）
 *   Info    — 参数/路径（FONT_S）
 *   Session — 会话名称（第一条用户 prompt，FONT_S）
 *   Owner   — 所有者 + 模型名（FONT_S）
 *
 * LED 行为：
 *   - 审批到达时由 buddy_main_screen 设置快闪（通知用户）
 *   - __screen_init() 末尾关闭 LED（用户已看到屏幕，无需持续闪烁）
 *   - __send_and_back() 决策后也确保 LED 关闭
 *
 * 布局（384×168，无 footer）：
 *   title_bar  0-20   黑底白字 "PERMISSION REQUEST"
 *   tool       24     FONT_M h=16
 *   info       43     FONT_S h=14
 *   session    60     FONT_S h=14
 *   owner      77     FONT_S h=14
 *   separator  94     1px 黑线
 *   opt0       99     FONT_M h=16  "Approve (once)"
 *   opt1       117    FONT_M h=16  "Approve (always)"
 *   opt2       135    FONT_M h=16  "Deny"
 *              ends=151 < 168 ✓
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
#include "buddy_cjk_font.h"
#define FONT_M   (&buddy_font_m)
#define FONT_S   (&buddy_font_s)

#define H_M      16
#define H_S      14

#define SCR_W    AI_PET_SCREEN_WIDTH   /* 384 */
#define SCR_H    AI_PET_SCREEN_HEIGHT  /* 168 */
#define PAD      8
#define INNER_W  (SCR_W - PAD * 2)    /* 368 */

#define TITLE_H  20

/* Y positions — each row = prev_y + prev_h + 3px gap */
#define Y_TOOL   (TITLE_H + 4)           /* 24 */
#define Y_INFO   (Y_TOOL + H_M + 3)      /* 43 */
#define Y_SESS   (Y_INFO + H_S + 3)      /* 60 */
#define Y_OWNER  (Y_SESS + H_S + 3)      /* 77 */
#define Y_SEP    (Y_OWNER + H_S + 3)     /* 94 */
#define Y_OPT0   (Y_SEP  + 1  + 4)      /* 99 */
#define Y_OPT1   (Y_OPT0 + H_M + 2)     /* 117 */
#define Y_OPT2   (Y_OPT1 + H_M + 2)     /* 135 */
#define OPT_COUNT 3

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *s_screen        = NULL;
STATIC lv_obj_t *s_tool          = NULL;
STATIC lv_obj_t *s_info          = NULL;
STATIC lv_obj_t *s_session       = NULL;
STATIC lv_obj_t *s_owner         = NULL;
STATIC lv_obj_t *s_opts[OPT_COUNT];

STATIC buddy_tama_state_t s_snap   = {0};
STATIC uint8_t            s_cursor = 0;

/* ---------------------------------------------------------------------------
 * Prototypes
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
 * Label helper — single line, zero padding, explicit height
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *__info_lbl(lv_obj_t *parent, int32_t y, int32_t h,
                             const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_size(l, INNER_W, h);
    lv_obj_set_style_pad_all(l, 0, 0);
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

    /* Title bar — black bg, white text */
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

    /* Four info rows */
    s_tool    = __info_lbl(s_screen, Y_TOOL,  H_M, FONT_M);
    s_info    = __info_lbl(s_screen, Y_INFO,  H_S, FONT_S);
    s_session = __info_lbl(s_screen, Y_SESS,  H_S, FONT_S);
    s_owner   = __info_lbl(s_screen, Y_OWNER, H_S, FONT_S);

    /* Separator */
    lv_obj_t *sep = lv_obj_create(s_screen);
    lv_obj_set_size(sep, INNER_W, 1);
    lv_obj_set_pos(sep, PAD, Y_SEP);
    lv_obj_set_style_bg_color(sep, lv_color_black(), 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

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

    /* Populate info from BLE snapshot */
    buddy_ble_snapshot(&s_snap);
    s_cursor = 0;

    /* Row 1: Tool name (bold) */
    if (s_tool)
        lv_label_set_text_fmt(s_tool, "Tool:  %s",
            s_snap.prompt_tool[0] ? s_snap.prompt_tool : "?");

    /* Row 2: Parameter / path hint */
    if (s_info)
        lv_label_set_text_fmt(s_info, "Info:  %s",
            s_snap.prompt_hint[0] ? s_snap.prompt_hint : "-");

    /* Row 3: Session name — first running session's name, else msg */
    if (s_session) {
        const char *sess_name = NULL;
        for (uint8_t i = 0; i < s_snap.sessions_count; i++) {
            if (s_snap.sessions[i].is_running && s_snap.sessions[i].name[0]) {
                sess_name = s_snap.sessions[i].name;
                break;
            }
        }
        if (!sess_name) {
            /* Fall back to msg as a proxy for what's happening */
            sess_name = s_snap.msg[0] ? s_snap.msg : "-";
        }
        lv_label_set_text_fmt(s_session, "Session: %s", sess_name);
    }

    /* Row 4: Owner + model */
    if (s_owner) {
        if (s_snap.model[0]) {
            lv_label_set_text_fmt(s_owner, "Owner: %s   Model: %s",
                s_snap.owner_name[0] ? s_snap.owner_name : "-",
                s_snap.model);
        } else {
            lv_label_set_text_fmt(s_owner, "Owner: %s   Sessions: %u active",
                s_snap.owner_name[0] ? s_snap.owner_name : "-",
                (unsigned)s_snap.sessions_running);
        }
    }

    __refresh_cursor();

    lv_obj_add_event_cb(s_screen, __keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), s_screen);
    lv_group_focus_obj(s_screen);

    /* Keep LED fast-blinking while the approval screen is visible so the
     * user has a physical cue that a decision is pending.  The LED is turned
     * off in __send_and_back() when the user actually makes a decision. */
    (VOID_T)buddy_led_set(BUDDY_LED_STATE_BLINK_FAST);

    PR_NOTICE("[%s] init tool=%s id=%s",
              buddy_approval_screen.name,
              s_snap.prompt_tool[0] ? s_snap.prompt_tool : "?",
              s_snap.prompt_id[0] ? s_snap.prompt_id : "-");
}

STATIC VOID_T __screen_deinit(VOID_T)
{
    if (s_screen) {
        lv_obj_remove_event_cb(s_screen, __keyboard_event_cb);
        lv_group_remove_obj(s_screen);
    }
    s_tool = s_info = s_session = s_owner = NULL;
    for (uint32_t i = 0; i < OPT_COUNT; i++) s_opts[i] = NULL;
    s_cursor = 0;
}

/* ---------------------------------------------------------------------------
 * Cursor refresh — selected row inverted (black bg, white text)
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
 * Decision
 * --------------------------------------------------------------------------- */
STATIC VOID_T __send_and_back(const char *decision)
{
    (VOID_T)buddy_led_set(BUDDY_LED_STATE_OFF);  /* ensure LED off on exit */

    if (!decision || !s_snap.has_prompt || !s_snap.prompt_id[0]) {
        screen_back();
        return;
    }
    (VOID_T)buddy_ble_send_permission(s_snap.prompt_id, decision);
    PR_NOTICE("[%s] decision=%s id=%s",
              buddy_approval_screen.name, decision, s_snap.prompt_id);
    screen_back();
}

/* ---------------------------------------------------------------------------
 * Key handler
 * --------------------------------------------------------------------------- */
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
