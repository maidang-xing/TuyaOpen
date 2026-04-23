/**
 * @file buddy_main_screen.c
 * @brief Claude Desktop Buddy 主屏（M5-UI，黑白单色风格）。
 *
 * 风格参考 main_screen.c / standby_screen.c：
 *   - 纯黑白，无彩色
 *   - 选中态：黑底白字（反色）
 *   - 普通态：白底黑字
 *   - 边框/分割线：黑色
 *
 * 布局：Header 20px（黑底）+ Body 148px（白底）。
 * 左侧 184px：persona 动画。
 * 右侧 192px：3 页信息，LEFT/RIGHT 翻页（页面指示在 header 右侧）：
 *   Page 0 (St) — Status：摘要 + 模型 + 最近条目
 *   Page 1 (Se) — Sessions：会话列表（名称 / 模型 / token）
 *   Page 2 (Lo) — Log：完整条目（UP/DOWN 滚动）
 *
 * 有 prompt 时弹出居中大窗口（340×138），遮盖 body。
 * UP/DOWN 移动光标（选中项反色），ENTER 确认，ESC 拒绝。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "buddy_main_screen.h"
#include "buddy_ble.h"
#include "buddy_data.h"
#include "buddy_led.h"
#include "buddy_gif_stub.h"
#include "ascii_persona.h"
#include "persona_registry.h"
#include "screen_manager.h"
#include "lv_vendor.h"
#include "tal_api.h"
#include "tuya_cloud_types.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Fonts — same as rest of project
 * --------------------------------------------------------------------------- */
#define FONT_L   &lv_font_terminusTTF_Bold_18
#define FONT_M   &lv_font_terminusTTF_Bold_16
#define FONT_S   &lv_font_terminusTTF_Bold_14

/* ---------------------------------------------------------------------------
 * Colors — pure B&W, matching main_screen.c style
 * --------------------------------------------------------------------------- */
#define C_BG      lv_color_white()           /* default background */
#define C_FG      lv_color_black()           /* default text */
#define C_INV_BG  lv_color_black()           /* inverted: selected background */
#define C_INV_FG  lv_color_white()           /* inverted: selected text */

/* ---------------------------------------------------------------------------
 * Layout
 * --------------------------------------------------------------------------- */
#define SCR_W       AI_PET_SCREEN_WIDTH     /* 384 */
#define SCR_H       AI_PET_SCREEN_HEIGHT    /* 168 */
#define HEADER_H    20
#define BODY_TOP    HEADER_H
#define BODY_H      (SCR_H - HEADER_H)     /* 148 */

#define PERSONA_X   0
#define PERSONA_Y   BODY_TOP
#define PERSONA_W   184
#define PERSONA_H   BODY_H

/* Right info panel — 2px gap from persona, 2px right margin */
#define INFO_X      (PERSONA_W + 2)         /* 186 */
#define INFO_Y      BODY_TOP
#define INFO_W      (SCR_W - INFO_X - 2)   /* 196 */
#define INFO_H      BODY_H                  /* 148 */
#define INFO_PAD    2

/* Divider between persona and info panel */
#define DIV_X       PERSONA_W               /* 184 */
#define DIV_W       2

/* Pages */
#define PAGE_COUNT    3
#define PAGE_STATUS   0
#define PAGE_SESSIONS 1
#define PAGE_LOG      2

#define STATUS_ENTRIES   3
#define LOG_VISIBLE      7
#define SESS_ROW_H       22

/* Permission popup — centered in body */
#define POPUP_W         340
#define POPUP_H         138
#define POPUP_X         ((SCR_W - POPUP_W) / 2)
#define POPUP_Y         (BODY_TOP + (BODY_H - POPUP_H) / 2)
#define POPUP_PAD       6
#define PERM_OPT_COUNT  3

/* Timer constants */
#define PERSONA_TICK_MS    100U
#define CELEBRATE_HOLD_MS  3000U
#define HEART_HOLD_MS      2000U
#define KV_KEY_PERSONA_ID  "buddy.pid"

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_buddy_main_screen = NULL;

/* header */
STATIC lv_obj_t *lbl_title;
STATIC lv_obj_t *lbl_clock;
STATIC lv_obj_t *lbl_ble;
STATIC lv_obj_t *lbl_page_ind;  /* "St" / "Se" / "Lo" */

/* Page 0 — Status */
STATIC lv_obj_t *pg0;
STATIC lv_obj_t *pg0_msg;
STATIC lv_obj_t *pg0_sessions;
STATIC lv_obj_t *pg0_tokens;
STATIC lv_obj_t *pg0_model;
STATIC lv_obj_t *pg0_owner;
STATIC lv_obj_t *pg0_entries[STATUS_ENTRIES];

/* Page 1 — Sessions */
STATIC lv_obj_t *pg1;
STATIC lv_obj_t *pg1_hdr;
STATIC lv_obj_t *pg1_rows[BUDDY_SESSIONS_MAX][2];
STATIC lv_obj_t *pg1_empty;

/* Page 2 — Log */
STATIC lv_obj_t *pg2;
STATIC lv_obj_t *pg2_lines[LOG_VISIBLE];

/* Permission popup */
STATIC lv_obj_t *perm_popup;
STATIC lv_obj_t *perm_tool;
STATIC lv_obj_t *perm_hint;
STATIC lv_obj_t *perm_ctx;
STATIC lv_obj_t *perm_opts[PERM_OPT_COUNT];

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
STATIC buddy_tama_state_t    s_state         = {0};
STATIC uint8_t               s_persona_id    = 0;
STATIC buddy_persona_state_e s_persona_state = BUDDY_PERSONA_STATE_SLEEP;
STATIC buddy_led_state_e     s_led_state     = BUDDY_LED_STATE_OFF;
STATIC uint8_t               s_page          = 0;
STATIC uint8_t               s_log_scroll    = 0;
STATIC uint8_t               s_cursor        = 0;

STATIC uint64_t s_celebrate_until_ms = 0;
STATIC uint64_t s_heart_until_ms     = 0;
STATIC BOOL_T   s_prev_prompt        = FALSE;
STATIC BOOL_T   s_prev_completed     = FALSE;

STATIC lv_timer_t *s_persona_timer = NULL;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __init(VOID_T);
STATIC VOID_T __deinit(VOID_T);
STATIC VOID_T __key_cb(lv_event_t *e);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_pages(lv_obj_t *parent);
STATIC VOID_T __build_page0(lv_obj_t *parent);
STATIC VOID_T __build_page1(lv_obj_t *parent);
STATIC VOID_T __build_page2(lv_obj_t *parent);
STATIC VOID_T __build_perm_popup(lv_obj_t *parent);
STATIC VOID_T __refresh(VOID_T);
STATIC VOID_T __refresh_header(VOID_T);
STATIC VOID_T __refresh_page_ind(VOID_T);
STATIC VOID_T __refresh_page0(VOID_T);
STATIC VOID_T __refresh_page1(VOID_T);
STATIC VOID_T __refresh_page2(VOID_T);
STATIC VOID_T __refresh_popup(VOID_T);
STATIC VOID_T __refresh_cursor(VOID_T);
STATIC VOID_T __switch_page(uint8_t p);
STATIC VOID_T __send_decision(const char *decision);
STATIC VOID_T __persona_tick_cb(lv_timer_t *t);
STATIC VOID_T __persona_cycle(int8_t delta);
STATIC uint8_t __load_persona_id(VOID_T);
STATIC VOID_T __persist_persona_id(uint8_t id);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n);
STATIC VOID_T __derive_persona(VOID_T);
STATIC buddy_led_state_e __led_from_persona(buddy_persona_state_e s);

/* Helper: make a label with common defaults */
STATIC lv_obj_t *__lbl(lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                        const lv_font_t *font);

Screen_t buddy_main_screen = {
    .init       = __init,
    .deinit     = __deinit,
    .screen_obj = &ui_buddy_main_screen,
    .name       = "buddy_main_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Public API
 * --------------------------------------------------------------------------- */
VOID_T buddy_main_screen_update_state(const buddy_tama_state_t *state)
{
    if (!state) return;
    lv_vendor_disp_lock();
    s_state = *state;
    if (ui_buddy_main_screen) __refresh();
    lv_vendor_disp_unlock();
}

/* ---------------------------------------------------------------------------
 * Utility helpers
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *__lbl(lv_obj_t *parent, int32_t x, int32_t y, int32_t w,
                        const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    if (w > 0) lv_obj_set_width(l, w);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, C_FG, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n)
{
    if (!out || n < 6) return;
    if (!s || !s->wall_epoch_s) { (VOID_T)snprintf(out, n, "--:--"); return; }
    uint64_t now_ms   = tal_system_get_millisecond();
    int64_t  delta_ms = (int64_t)(now_ms - s->wall_local_ms_at_rx);
    int64_t  epoch    = s->wall_epoch_s + delta_ms / 1000 +
                        (int64_t)s->wall_tz_min * 60;
    int64_t  sod = epoch % 86400;
    if (sod < 0) sod += 86400;
    (VOID_T)snprintf(out, n, "%02d:%02d", (int)(sod / 3600), (int)((sod / 60) % 60));
}

STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n)
{
    if (v >= 1000U)
        (VOID_T)snprintf(out, n, "%u.%uk", (unsigned)(v / 1000), (unsigned)((v % 1000) / 100));
    else
        (VOID_T)snprintf(out, n, "%u", (unsigned)v);
}

/* ---------------------------------------------------------------------------
 * Header — black bar, white text (matching main_screen.c)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_header(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, HEADER_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, C_INV_BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "Claude Buddy");
    lv_obj_set_style_text_font(lbl_title, FONT_M, 0);
    lv_obj_set_style_text_color(lbl_title, C_INV_FG, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 6, 0);

    lbl_clock = lv_label_create(bar);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_clock, C_INV_FG, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_CENTER, 0, 0);

    lbl_ble = lv_label_create(bar);
    lv_label_set_text(lbl_ble, "BLE:-");
    lv_obj_set_style_text_font(lbl_ble, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_ble, C_INV_FG, 0);
    lv_obj_align(lbl_ble, LV_ALIGN_LEFT_MID, 140, 0);

    /* Page indicator — right side of header, inverted style */
    lbl_page_ind = lv_label_create(bar);
    lv_label_set_text(lbl_page_ind, "St");
    lv_obj_set_style_text_font(lbl_page_ind, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_page_ind, C_INV_FG, 0);
    lv_obj_align(lbl_page_ind, LV_ALIGN_RIGHT_MID, -6, 0);
}

/* ---------------------------------------------------------------------------
 * Page container + pages
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_page0(lv_obj_t *parent)
{
    pg0 = lv_obj_create(parent);
    lv_obj_set_size(pg0, INFO_W, INFO_H);
    lv_obj_set_pos(pg0, 0, 0);
    lv_obj_set_style_bg_color(pg0, C_BG, 0);
    lv_obj_set_style_border_width(pg0, 0, 0);
    lv_obj_set_style_radius(pg0, 0, 0);
    lv_obj_set_style_pad_all(pg0, INFO_PAD, 0);
    lv_obj_clear_flag(pg0, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t W = INFO_W - INFO_PAD * 2;
    pg0_msg      = __lbl(pg0, 0,  0, W, FONT_M);
    pg0_sessions = __lbl(pg0, 0, 20, W, FONT_S);
    pg0_tokens   = __lbl(pg0, 0, 35, W, FONT_S);
    pg0_model    = __lbl(pg0, 0, 50, W, FONT_S);
    pg0_owner    = __lbl(pg0, 0, 65, W, FONT_S);

    /* Divider line before entries */
    lv_obj_t *div = lv_obj_create(pg0);
    lv_obj_set_size(div, W, 1);
    lv_obj_set_pos(div, 0, 80);
    lv_obj_set_style_bg_color(div, C_FG, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);

    for (uint32_t i = 0; i < STATUS_ENTRIES; i++) {
        pg0_entries[i] = __lbl(pg0, 0, (int32_t)(84 + i * 22), W,
                               (i == 0) ? FONT_M : FONT_S);
        lv_obj_add_flag(pg0_entries[i], LV_OBJ_FLAG_HIDDEN);
    }
}

STATIC VOID_T __build_page1(lv_obj_t *parent)
{
    pg1 = lv_obj_create(parent);
    lv_obj_set_size(pg1, INFO_W, INFO_H);
    lv_obj_set_pos(pg1, 0, 0);
    lv_obj_set_style_bg_color(pg1, C_BG, 0);
    lv_obj_set_style_border_width(pg1, 0, 0);
    lv_obj_set_style_radius(pg1, 0, 0);
    lv_obj_set_style_pad_all(pg1, INFO_PAD, 0);
    lv_obj_clear_flag(pg1, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pg1, LV_OBJ_FLAG_HIDDEN);

    const int32_t W = INFO_W - INFO_PAD * 2;

    /* Header row — inverted (black bg, white text) */
    lv_obj_t *hdr_bg = lv_obj_create(pg1);
    lv_obj_set_size(hdr_bg, W, 16);
    lv_obj_set_pos(hdr_bg, 0, 0);
    lv_obj_set_style_bg_color(hdr_bg, C_INV_BG, 0);
    lv_obj_set_style_border_width(hdr_bg, 0, 0);
    lv_obj_set_style_radius(hdr_bg, 0, 0);
    lv_obj_set_style_pad_all(hdr_bg, 0, 0);
    lv_obj_clear_flag(hdr_bg, LV_OBJ_FLAG_SCROLLABLE);

    pg1_hdr = lv_label_create(hdr_bg);
    lv_label_set_text(pg1_hdr, "Sessions");
    lv_obj_set_style_text_font(pg1_hdr, FONT_S, 0);
    lv_obj_set_style_text_color(pg1_hdr, C_INV_FG, 0);
    lv_obj_set_pos(pg1_hdr, 2, 1);

    for (uint32_t i = 0; i < BUDDY_SESSIONS_MAX; i++) {
        int32_t y = (int32_t)(18 + i * SESS_ROW_H);
        /* name label (white bg, black text normally; inverted if running) */
        pg1_rows[i][0] = __lbl(pg1, 0, y, W, FONT_S);
        /* detail label (small, slightly indented) */
        pg1_rows[i][1] = __lbl(pg1, 4, (int32_t)(y + 10), W - 4, FONT_S);
        lv_obj_add_flag(pg1_rows[i][0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pg1_rows[i][1], LV_OBJ_FLAG_HIDDEN);
    }
    pg1_empty = __lbl(pg1, 0, 20, W, FONT_S);
    lv_label_set_text(pg1_empty, "No sessions yet");
}

STATIC VOID_T __build_page2(lv_obj_t *parent)
{
    pg2 = lv_obj_create(parent);
    lv_obj_set_size(pg2, INFO_W, INFO_H);
    lv_obj_set_pos(pg2, 0, 0);
    lv_obj_set_style_bg_color(pg2, C_BG, 0);
    lv_obj_set_style_border_width(pg2, 0, 0);
    lv_obj_set_style_radius(pg2, 0, 0);
    lv_obj_set_style_pad_all(pg2, INFO_PAD, 0);
    lv_obj_clear_flag(pg2, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(pg2, LV_OBJ_FLAG_HIDDEN);

    const int32_t W = INFO_W - INFO_PAD * 2;
    for (uint32_t i = 0; i < LOG_VISIBLE; i++) {
        pg2_lines[i] = __lbl(pg2, 0, (int32_t)(i * 21), W,
                             (i == 0) ? FONT_M : FONT_S);
        lv_obj_add_flag(pg2_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
}

STATIC VOID_T __build_pages(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_set_size(container, INFO_W, INFO_H);
    lv_obj_set_pos(container, INFO_X, INFO_Y);
    lv_obj_set_style_bg_color(container, C_BG, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_style_radius(container, 0, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);

    __build_page0(container);
    __build_page1(container);
    __build_page2(container);

    /* Vertical divider between persona and info panel */
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_set_size(div, DIV_W, BODY_H);
    lv_obj_set_pos(div, DIV_X, BODY_TOP);
    lv_obj_set_style_bg_color(div, C_FG, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);
}

/* ---------------------------------------------------------------------------
 * Permission popup — B&W style:
 *   - White background, black 2px border
 *   - Title in inverted bar (black bg, white text)
 *   - Selected option: inverted (black bg, white text)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_perm_popup(lv_obj_t *parent)
{
    perm_popup = lv_obj_create(parent);
    lv_obj_set_size(perm_popup, POPUP_W, POPUP_H);
    lv_obj_set_pos(perm_popup, POPUP_X, POPUP_Y);
    lv_obj_set_style_bg_color(perm_popup, C_BG, 0);
    lv_obj_set_style_border_color(perm_popup, C_FG, 0);
    lv_obj_set_style_border_width(perm_popup, 2, 0);
    lv_obj_set_style_radius(perm_popup, 3, 0);
    lv_obj_set_style_pad_all(perm_popup, POPUP_PAD, 0);
    lv_obj_clear_flag(perm_popup, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(perm_popup, LV_OBJ_FLAG_HIDDEN);

    /* Title bar — inverted */
    lv_obj_t *title_bg = lv_obj_create(perm_popup);
    lv_obj_set_size(title_bg, POPUP_W - POPUP_PAD * 2, 18);
    lv_obj_set_pos(title_bg, 0, 0);
    lv_obj_set_style_bg_color(title_bg, C_INV_BG, 0);
    lv_obj_set_style_border_width(title_bg, 0, 0);
    lv_obj_set_style_radius(title_bg, 0, 0);
    lv_obj_set_style_pad_all(title_bg, 0, 0);
    lv_obj_clear_flag(title_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title_lbl = lv_label_create(title_bg);
    lv_label_set_text(title_lbl, "PERMISSION REQUEST");
    lv_obj_set_style_text_font(title_lbl, FONT_M, 0);
    lv_obj_set_style_text_color(title_lbl, C_INV_FG, 0);
    lv_obj_set_pos(title_lbl, 2, 0);

    const int32_t W = POPUP_W - POPUP_PAD * 2;
    perm_tool = __lbl(perm_popup, 0, 22, W, FONT_M);
    perm_hint = __lbl(perm_popup, 0, 40, W, FONT_S);
    perm_ctx  = __lbl(perm_popup, 0, 56, W, FONT_S);

    /* Horizontal divider */
    lv_obj_t *sep = lv_obj_create(perm_popup);
    lv_obj_set_size(sep, W, 1);
    lv_obj_set_pos(sep, 0, 72);
    lv_obj_set_style_bg_color(sep, C_FG, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_set_style_pad_all(sep, 0, 0);

    static const char *const OPT[PERM_OPT_COUNT] = {
        "Approve (once)", "Approve (always)", "Deny"
    };
    for (uint32_t i = 0; i < PERM_OPT_COUNT; i++) {
        perm_opts[i] = lv_label_create(perm_popup);
        lv_label_set_long_mode(perm_opts[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(perm_opts[i], W);
        lv_obj_set_style_text_font(perm_opts[i], FONT_S, 0);
        lv_obj_set_style_text_color(perm_opts[i], C_FG, 0);
        lv_obj_set_pos(perm_opts[i], 0, (int32_t)(76 + i * 18));
        char buf[32];
        (VOID_T)snprintf(buf, sizeof(buf), "  %s", OPT[i]);
        lv_label_set_text(perm_opts[i], buf);
    }
}

/* ---------------------------------------------------------------------------
 * Page switching
 * --------------------------------------------------------------------------- */
STATIC VOID_T __switch_page(uint8_t p)
{
    s_page = p % PAGE_COUNT;
    s_log_scroll = 0;

    lv_obj_t *pages[PAGE_COUNT] = {pg0, pg1, pg2};
    for (uint32_t i = 0; i < PAGE_COUNT; i++) {
        if (!pages[i]) continue;
        if (i == (uint32_t)s_page) lv_obj_clear_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        else                       lv_obj_add_flag(pages[i],    LV_OBJ_FLAG_HIDDEN);
    }
    __refresh_page_ind();

    switch (s_page) {
    case PAGE_STATUS:   __refresh_page0(); break;
    case PAGE_SESSIONS: __refresh_page1(); break;
    case PAGE_LOG:      __refresh_page2(); break;
    default: break;
    }
}

/* ---------------------------------------------------------------------------
 * Persona derivation
 * --------------------------------------------------------------------------- */
STATIC buddy_led_state_e __led_from_persona(buddy_persona_state_e s)
{
    switch (s) {
    case BUDDY_PERSONA_STATE_SLEEP:     return BUDDY_LED_STATE_OFF;
    case BUDDY_PERSONA_STATE_IDLE:      return BUDDY_LED_STATE_ON_DIM;
    case BUDDY_PERSONA_STATE_BUSY:      return BUDDY_LED_STATE_BLINK_SLOW;
    case BUDDY_PERSONA_STATE_ATTENTION: return BUDDY_LED_STATE_BLINK_FAST;
    case BUDDY_PERSONA_STATE_DIZZY:     return BUDDY_LED_STATE_BLINK_FAST;
    default:                            return BUDDY_LED_STATE_FLASH_ONCE;
    }
}

STATIC VOID_T __derive_persona(VOID_T)
{
    uint64_t now = tal_system_get_millisecond();

    if (s_state.recently_completed && !s_prev_completed)
        s_celebrate_until_ms = now + CELEBRATE_HOLD_MS;
    s_prev_completed = s_state.recently_completed ? TRUE : FALSE;

    if (!s_state.has_prompt && s_prev_prompt)
        s_heart_until_ms = now + HEART_HOLD_MS;
    s_prev_prompt = s_state.has_prompt ? TRUE : FALSE;

    buddy_persona_state_e next;
    if (!s_state.ble_connected)           next = BUDDY_PERSONA_STATE_SLEEP;
    else if (s_state.has_prompt)          next = BUDDY_PERSONA_STATE_ATTENTION;
    else if (now < s_heart_until_ms)      next = BUDDY_PERSONA_STATE_HEART;
    else if (now < s_celebrate_until_ms)  next = BUDDY_PERSONA_STATE_CELEBRATE;
    else if (s_state.sessions_running)    next = BUDDY_PERSONA_STATE_BUSY;
    else                                  next = BUDDY_PERSONA_STATE_IDLE;

    if (next != s_persona_state) {
        PR_DEBUG("persona %d->%d", (int)s_persona_state, (int)next);
        s_persona_state = next;
    }
    s_state.persona_state = s_persona_state;
    s_state.persona_id    = s_persona_id;

    buddy_led_state_e led = __led_from_persona(s_persona_state);
    if (led != s_led_state) {
        s_led_state = led;
        (VOID_T)buddy_led_set(led);
    }
    s_state.led_state = s_led_state;
}

/* ---------------------------------------------------------------------------
 * Refresh helpers
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_header(VOID_T)
{
    if (lbl_ble)
        lv_label_set_text(lbl_ble,
            s_state.ble_connected ? "BLE:OK" : "BLE:-");
    if (lbl_clock) {
        char buf[6];
        __format_clock(&s_state, buf, sizeof(buf));
        lv_label_set_text(lbl_clock, buf);
    }
}

STATIC VOID_T __refresh_page_ind(VOID_T)
{
    if (!lbl_page_ind) return;
    static const char *const NAMES[PAGE_COUNT] = {"St", "Se", "Lo"};
    lv_label_set_text(lbl_page_ind, NAMES[s_page]);
}

STATIC VOID_T __refresh_page0(VOID_T)
{
    if (!pg0_msg) return;

    const char *msg = s_state.msg[0]
        ? s_state.msg
        : (s_state.ble_connected ? "Ready." : "Waiting for Claude...");
    lv_label_set_text(pg0_msg, msg);

    if (pg0_sessions)
        lv_label_set_text_fmt(pg0_sessions, "S:%u  R:%u  W:%u",
            (unsigned)s_state.sessions_total,
            (unsigned)s_state.sessions_running,
            (unsigned)s_state.sessions_waiting);

    if (pg0_tokens) {
        char ti[8], to[8];
        __fmt_tok(s_state.tokens_today, ti, sizeof(ti));
        __fmt_tok(s_state.tokens, to, sizeof(to));
        lv_label_set_text_fmt(pg0_tokens, "tok %s today / %s total", ti, to);
    }

    if (pg0_model)
        lv_label_set_text_fmt(pg0_model, "model: %s",
            s_state.model[0] ? s_state.model : "unknown");

    if (pg0_owner) {
        const persona_entry_t *p = persona_registry_get_by_id(s_persona_id);
        const char *pname = (p && p->persona && p->persona->name)
                            ? p->persona->name : "?";
        lv_label_set_text_fmt(pg0_owner, "%s  [%s]",
            s_state.owner_name[0] ? s_state.owner_name : "-", pname);
    }

    uint8_t count = s_state.entries_count;
    uint8_t head  = s_state.entries_head;
    for (uint32_t i = 0; i < STATUS_ENTRIES; i++) {
        if (!pg0_entries[i]) continue;
        if (i >= (uint32_t)count) {
            lv_obj_add_flag(pg0_entries[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        uint8_t idx = (uint8_t)((head + BUDDY_ENTRIES_RING - i) % BUDDY_ENTRIES_RING);
        lv_label_set_text(pg0_entries[i], s_state.entries[idx].text);
        lv_obj_clear_flag(pg0_entries[i], LV_OBJ_FLAG_HIDDEN);
    }
}

STATIC VOID_T __refresh_page1(VOID_T)
{
    uint8_t cnt = s_state.sessions_count;

    if (pg1_hdr)
        lv_label_set_text_fmt(pg1_hdr, "Sessions (%u)", (unsigned)cnt);

    if (cnt == 0) {
        if (pg1_empty) lv_obj_clear_flag(pg1_empty, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t i = 0; i < BUDDY_SESSIONS_MAX; i++) {
            if (pg1_rows[i][0]) lv_obj_add_flag(pg1_rows[i][0], LV_OBJ_FLAG_HIDDEN);
            if (pg1_rows[i][1]) lv_obj_add_flag(pg1_rows[i][1], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (pg1_empty) lv_obj_add_flag(pg1_empty, LV_OBJ_FLAG_HIDDEN);

    for (uint32_t i = 0; i < BUDDY_SESSIONS_MAX; i++) {
        if (!pg1_rows[i][0] || !pg1_rows[i][1]) continue;
        if (i >= (uint32_t)cnt) {
            lv_obj_add_flag(pg1_rows[i][0], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pg1_rows[i][1], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const buddy_session_t *sess = &s_state.sessions[i];

        /* name row: running = inverted, idle = normal */
        char name_buf[32];
        const char *prefix = sess->is_running ? "*" : " ";
        (VOID_T)snprintf(name_buf, sizeof(name_buf), "%s %s",
                         prefix,
                         sess->name[0] ? sess->name : "(unnamed)");
        lv_label_set_text(pg1_rows[i][0], name_buf);
        if (sess->is_running) {
            lv_obj_set_style_text_color(pg1_rows[i][0], C_INV_FG, 0);
            lv_obj_set_style_bg_color(pg1_rows[i][0],  C_INV_BG, 0);
            lv_obj_set_style_bg_opa(pg1_rows[i][0],    LV_OPA_COVER, 0);
        } else {
            lv_obj_set_style_text_color(pg1_rows[i][0], C_FG, 0);
            lv_obj_set_style_bg_opa(pg1_rows[i][0],    LV_OPA_TRANSP, 0);
        }

        /* detail row */
        char ti[8], to_s[8];
        __fmt_tok(sess->tokens_in,  ti,   sizeof(ti));
        __fmt_tok(sess->tokens_out, to_s, sizeof(to_s));
        char detail[40];
        (VOID_T)snprintf(detail, sizeof(detail), "  %s  in:%s  out:%s",
                         sess->model[0] ? sess->model : "?", ti, to_s);
        lv_label_set_text(pg1_rows[i][1], detail);
        lv_obj_set_style_text_color(pg1_rows[i][1], C_FG, 0);

        lv_obj_clear_flag(pg1_rows[i][0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pg1_rows[i][1], LV_OBJ_FLAG_HIDDEN);
    }
}

STATIC VOID_T __refresh_page2(VOID_T)
{
    uint8_t count = s_state.entries_count;
    uint8_t head  = s_state.entries_head;
    if (count == 0) s_log_scroll = 0;
    else if (s_log_scroll >= count) s_log_scroll = (uint8_t)(count - 1);

    for (uint32_t i = 0; i < LOG_VISIBLE; i++) {
        if (!pg2_lines[i]) continue;
        uint8_t rel = (uint8_t)(s_log_scroll + i);
        if (rel >= count) {
            lv_obj_add_flag(pg2_lines[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        uint8_t idx = (uint8_t)((head + BUDDY_ENTRIES_RING - rel) % BUDDY_ENTRIES_RING);
        lv_label_set_text(pg2_lines[i], s_state.entries[idx].text);
        lv_obj_clear_flag(pg2_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* Refresh permission popup option labels: selected = inverted */
STATIC VOID_T __refresh_cursor(VOID_T)
{
    static const char *const OPT[PERM_OPT_COUNT] = {
        "Approve (once)", "Approve (always)", "Deny"
    };
    for (uint32_t i = 0; i < PERM_OPT_COUNT; i++) {
        if (!perm_opts[i]) continue;
        char buf[32];
        if (i == (uint32_t)s_cursor) {
            (VOID_T)snprintf(buf, sizeof(buf), "> %s", OPT[i]);
            lv_obj_set_style_text_color(perm_opts[i], C_INV_FG, 0);
            lv_obj_set_style_bg_color(perm_opts[i],   C_INV_BG, 0);
            lv_obj_set_style_bg_opa(perm_opts[i],     LV_OPA_COVER, 0);
        } else {
            (VOID_T)snprintf(buf, sizeof(buf), "  %s", OPT[i]);
            lv_obj_set_style_text_color(perm_opts[i], C_FG, 0);
            lv_obj_set_style_bg_opa(perm_opts[i],     LV_OPA_TRANSP, 0);
        }
        lv_label_set_text(perm_opts[i], buf);
    }
}

STATIC VOID_T __refresh_popup(VOID_T)
{
    if (perm_tool)
        lv_label_set_text_fmt(perm_tool, "Tool:  %s",
            s_state.prompt_tool[0] ? s_state.prompt_tool : "?");
    if (perm_hint)
        lv_label_set_text_fmt(perm_hint, "Info:  %s",
            s_state.prompt_hint[0] ? s_state.prompt_hint : "-");
    if (perm_ctx)
        lv_label_set_text_fmt(perm_ctx, "Owner: %s  S:%u R:%u",
            s_state.owner_name[0] ? s_state.owner_name : "-",
            (unsigned)s_state.sessions_total,
            (unsigned)s_state.sessions_running);
    __refresh_cursor();
}

STATIC VOID_T __refresh(VOID_T)
{
    if (!ui_buddy_main_screen) return;
    __refresh_header();
    __derive_persona();

    const BOOL_T pending = s_state.has_prompt ? TRUE : FALSE;
    if (perm_popup) {
        if (pending) {
            if (!s_prev_prompt) s_cursor = 0;
            lv_obj_clear_flag(perm_popup, LV_OBJ_FLAG_HIDDEN);
            __refresh_popup();
        } else {
            lv_obj_add_flag(perm_popup, LV_OBJ_FLAG_HIDDEN);
        }
    }

    switch (s_page) {
    case PAGE_STATUS:   __refresh_page0(); break;
    case PAGE_SESSIONS: __refresh_page1(); break;
    case PAGE_LOG:      __refresh_page2(); break;
    default: break;
    }
}

/* ---------------------------------------------------------------------------
 * Persona timer
 * --------------------------------------------------------------------------- */
STATIC VOID_T __persona_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!ui_buddy_main_screen) return;
    ascii_persona_tick(s_persona_id, s_persona_state);
}

STATIC VOID_T __persona_cycle(int8_t delta)
{
    uint8_t next = (delta > 0)
        ? persona_registry_next_id(s_persona_id)
        : persona_registry_prev_id(s_persona_id);
    if (next == s_persona_id) return;
    s_persona_id = next;
    __persist_persona_id(next);
    ascii_persona_reset_tick(0);
    const persona_entry_t *p = persona_registry_get_by_id(next);
    PR_NOTICE("persona -> %u (%s)", (unsigned)next,
              (p && p->persona && p->persona->name) ? p->persona->name : "?");
    __refresh();
}

STATIC uint8_t __load_persona_id(VOID_T)
{
    uint8_t  v = 0; uint8_t *buf = NULL; size_t len = 0;
    if (tal_kv_get(KV_KEY_PERSONA_ID, &buf, &len) == OPRT_OK && buf) {
        if (len) v = buf[0];
        tal_kv_free(buf);
    }
    return (v < BUDDY_PERSONA_COUNT) ? v : 0;
}

STATIC VOID_T __persist_persona_id(uint8_t id)
{
    if (id < BUDDY_PERSONA_COUNT)
        (VOID_T)tal_kv_set(KV_KEY_PERSONA_ID, &id, sizeof(id));
}

/* ---------------------------------------------------------------------------
 * Permission decision
 * --------------------------------------------------------------------------- */
STATIC VOID_T __send_decision(const char *decision)
{
    if (!decision || !s_state.has_prompt || !s_state.prompt_id[0]) return;
    (VOID_T)buddy_ble_send_permission(s_state.prompt_id, decision);
    PR_NOTICE("decision=%s id=%s", decision, s_state.prompt_id);
    s_state.has_prompt = FALSE;
    s_state.prompt_id[0] = s_state.prompt_tool[0] = s_state.prompt_hint[0] = '\0';
    __refresh();
}

/* ---------------------------------------------------------------------------
 * Key handler
 * --------------------------------------------------------------------------- */
STATIC VOID_T __key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    if (s_state.has_prompt) {
        static const char *const DECISIONS[PERM_OPT_COUNT] = {
            "once", "always", "deny"
        };
        switch (key) {
        case KEY_UP:
            s_cursor = (s_cursor == 0) ? (uint8_t)(PERM_OPT_COUNT - 1)
                                       : (uint8_t)(s_cursor - 1);
            lv_vendor_disp_lock();
            __refresh_cursor();
            lv_vendor_disp_unlock();
            break;
        case KEY_DOWN:
            s_cursor = (uint8_t)((s_cursor + 1) % PERM_OPT_COUNT);
            lv_vendor_disp_lock();
            __refresh_cursor();
            lv_vendor_disp_unlock();
            break;
        case KEY_ENTER:
            __send_decision(DECISIONS[s_cursor]);
            break;
        case KEY_ESC:
            __send_decision("deny");
            screen_back();
            break;
        default: break;
        }
        return;
    }

    switch (key) {
    case KEY_LEFT:
        lv_vendor_disp_lock();
        __switch_page((s_page == 0) ? (uint8_t)(PAGE_COUNT - 1)
                                    : (uint8_t)(s_page - 1));
        lv_vendor_disp_unlock();
        break;
    case KEY_RIGHT:
        lv_vendor_disp_lock();
        __switch_page((uint8_t)((s_page + 1) % PAGE_COUNT));
        lv_vendor_disp_unlock();
        break;
    case KEY_UP:
        if (s_page == PAGE_LOG) {
            if ((uint32_t)s_log_scroll + LOG_VISIBLE < (uint32_t)s_state.entries_count) {
                s_log_scroll++;
                lv_vendor_disp_lock();
                __refresh_page2();
                lv_vendor_disp_unlock();
            }
        }
        break;
    case KEY_DOWN:
        if (s_page == PAGE_LOG && s_log_scroll > 0) {
            s_log_scroll--;
            lv_vendor_disp_lock();
            __refresh_page2();
            lv_vendor_disp_unlock();
        }
        break;
    case KEY_JOYCON:
    case KEY_ENTER:
        __persona_cycle(+1);
        break;
    case KEY_ESC:
        screen_back();
        break;
    default: break;
    }
}

/* ---------------------------------------------------------------------------
 * Init / deinit
 * --------------------------------------------------------------------------- */
STATIC VOID_T __init(VOID_T)
{
    ui_buddy_main_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_buddy_main_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(ui_buddy_main_screen, C_BG, 0);
    lv_obj_set_style_pad_all(ui_buddy_main_screen, 0, 0);
    lv_obj_clear_flag(ui_buddy_main_screen, LV_OBJ_FLAG_SCROLLABLE);

    __build_header(ui_buddy_main_screen);
    ascii_persona_attach(ui_buddy_main_screen, PERSONA_X, PERSONA_Y);
    __build_pages(ui_buddy_main_screen);
    __build_perm_popup(ui_buddy_main_screen);

    s_page = 0; s_log_scroll = 0; s_cursor = 0;
    s_persona_id = __load_persona_id();

    buddy_tama_state_t snap;
    buddy_ble_snapshot(&snap);
    s_state = snap;
    __refresh_page_ind();
    __refresh();

    (VOID_T)buddy_led_init();

    if (!s_persona_timer)
        s_persona_timer = lv_timer_create(__persona_tick_cb, PERSONA_TICK_MS, NULL);

    lv_obj_add_event_cb(ui_buddy_main_screen, __key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_buddy_main_screen);
    lv_group_focus_obj(ui_buddy_main_screen);

    PR_NOTICE("[%s] init persona=%u", buddy_main_screen.name, (unsigned)s_persona_id);
}

STATIC VOID_T __deinit(VOID_T)
{
    if (s_persona_timer) { lv_timer_del(s_persona_timer); s_persona_timer = NULL; }
    ascii_persona_detach();

    if (ui_buddy_main_screen) {
        lv_obj_remove_event_cb(ui_buddy_main_screen, __key_cb);
        lv_group_remove_obj(ui_buddy_main_screen);
    }

    lbl_title = lbl_clock = lbl_ble = lbl_page_ind = NULL;
    pg0 = pg0_msg = pg0_sessions = pg0_tokens = pg0_model = pg0_owner = NULL;
    for (uint32_t i = 0; i < STATUS_ENTRIES; i++) pg0_entries[i] = NULL;
    pg1 = pg1_hdr = pg1_empty = NULL;
    for (uint32_t i = 0; i < BUDDY_SESSIONS_MAX; i++)
        pg1_rows[i][0] = pg1_rows[i][1] = NULL;
    pg2 = NULL;
    for (uint32_t i = 0; i < LOG_VISIBLE; i++) pg2_lines[i] = NULL;
    perm_popup = perm_tool = perm_hint = perm_ctx = NULL;
    for (uint32_t i = 0; i < PERM_OPT_COUNT; i++) perm_opts[i] = NULL;
    s_log_scroll = s_cursor = s_page = 0;
}
