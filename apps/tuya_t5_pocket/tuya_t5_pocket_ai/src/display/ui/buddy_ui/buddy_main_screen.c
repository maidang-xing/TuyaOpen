/**
 * @file buddy_main_screen.c
 * @brief Claude Desktop Buddy 主屏（M7-UI）。
 *
 * 布局（384×168，无 footer）：
 *   Header   20px  黑底白字：标题 / 时钟 / BLE / 页面指示
 *   Body    148px  左右分割：
 *     左侧 144px：
 *       ├─ persona 动画（144×110 canvas，y=20~130）
 *       └─ 名字+等级区（y=130~168，38px）：
 *             "capybara"
 *             "Lv.3  ▓▓▓░░░░░ 42k"
 *     竖线  2px  黑色分割线
 *     右侧 238px：4 页信息面板（LEFT/RIGHT 翻页）
 *       St — Status   : msg / 会话 / token / 模型 / owner / 最近条目
 *       Se — Sessions : 会话列表（运行中反色）
 *       Lo — Log      : 完整条目（UP/DOWN 滚动）
 *       Md — Models   : 各模型 output token 统计
 *
 * has_prompt FALSE→TRUE 时直接 screen_load 到 buddy_approval_screen，
 * 主屏不保留任何审批 UI。
 *
 * 按键：LEFT/RIGHT 翻页；UP/DOWN 在 Log 页滚动；JOYCON 循环 persona；
 *        ENTER 无操作（避免误触）；ESC 返回上一屏。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "buddy_main_screen.h"
#include "buddy_approval_screen.h"
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
 * Fonts
 * --------------------------------------------------------------------------- */
#define FONT_L   &lv_font_terminusTTF_Bold_18
#define FONT_M   &lv_font_terminusTTF_Bold_16
#define FONT_S   &lv_font_terminusTTF_Bold_14

/* ---------------------------------------------------------------------------
 * Colors — pure B&W
 * --------------------------------------------------------------------------- */
#define C_BG      lv_color_white()
#define C_FG      lv_color_black()
#define C_INV_BG  lv_color_black()
#define C_INV_FG  lv_color_white()

/* ---------------------------------------------------------------------------
 * Layout
 * --------------------------------------------------------------------------- */
#define SCR_W       AI_PET_SCREEN_WIDTH     /* 384 */
#define SCR_H       AI_PET_SCREEN_HEIGHT    /* 168 */
#define HEADER_H    20
#define BODY_TOP    HEADER_H                /* 20  */
#define BODY_H      (SCR_H - HEADER_H)     /* 148 */

/* Left column: persona canvas + name/level strip */
#define PERSONA_W        144               /* = ASCII_CANVAS_W */
#define PERSONA_CANVAS_H 110               /* = ASCII_CANVAS_H */
#define PERSONA_STRIP_Y  (BODY_TOP + PERSONA_CANVAS_H)   /* 130 */
#define PERSONA_STRIP_H  (SCR_H - PERSONA_STRIP_Y)       /* 38  */

/* Vertical divider */
#define DIV_X   PERSONA_W                  /* 144 */
#define DIV_W   2

/* Right info panel */
#define INFO_X      (PERSONA_W + DIV_W)    /* 146 */
#define INFO_Y      BODY_TOP               /* 20  */
#define INFO_W      (SCR_W - INFO_X)       /* 238 */
#define INFO_H      BODY_H                 /* 148 */
#define INFO_PAD    3

/* Font heights — must match actual font metrics */
#define H_M  16    /* FONT_M = terminusTTF_Bold_16 */
#define H_S  14    /* FONT_S = terminusTTF_Bold_14 */

/* Gap between consecutive rows (px) */
#define ROW_GAP  3

/* Level calculation */
#define TOKENS_PER_LEVEL  50000U
#define LEVEL_BAR_BLOCKS  8

/* Pages */
#define PAGE_COUNT    4
#define PAGE_STATUS   0
#define PAGE_SESSIONS 1
#define PAGE_LOG      2
#define PAGE_MODELS   3

#define STATUS_ENTRIES  5   /* entries visible on status page */
#define LOG_VISIBLE     7
#define SESS_ROW_H      23

/* Timers */
#define PERSONA_TICK_MS    100U
#define CELEBRATE_HOLD_MS  3000U
#define HEART_HOLD_MS      2000U
#define KV_KEY_PERSONA_ID  "buddy.pid"

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_buddy_main_screen = NULL;

/* header */
STATIC lv_obj_t *lbl_model;
STATIC lv_obj_t *lbl_clock;
STATIC lv_obj_t *lbl_ble;
STATIC lv_obj_t *lbl_stat_line;
STATIC lv_obj_t *lbl_page_ind;

/* persona name + level strip */
STATIC lv_obj_t *lbl_persona_name;
STATIC lv_obj_t *lbl_persona_level;

/* Page 0 — Status */
STATIC lv_obj_t *pg0;
STATIC lv_obj_t *pg0_msg;
STATIC lv_obj_t *pg0_sessions_line;
STATIC lv_obj_t *pg0_ctx_bar;      /* context window ASCII bar + pct */
STATIC lv_obj_t *pg0_ctx_detail;   /* ctx_used/ctx_total + cache stats */
STATIC lv_obj_t *pg0_model_line;
STATIC lv_obj_t *pg0_owner_line;
STATIC lv_obj_t *pg0_entries[STATUS_ENTRIES];

/* Page 1 — Sessions */
STATIC lv_obj_t *pg1;
STATIC lv_obj_t *pg1_hdr;
STATIC lv_obj_t *pg1_rows[BUDDY_SESSIONS_MAX][2];
STATIC lv_obj_t *pg1_empty;

/* Page 2 — Log */
STATIC lv_obj_t *pg2;
STATIC lv_obj_t *pg2_lines[LOG_VISIBLE];

/* Page 3 — Models */
STATIC lv_obj_t *pg3;
STATIC lv_obj_t *pg3_hdr;
STATIC lv_obj_t *pg3_rows[BUDDY_MSTATS_MAX][2];
STATIC lv_obj_t *pg3_empty;

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
STATIC buddy_tama_state_t    s_state         = {0};
STATIC uint8_t               s_persona_id    = 0;
STATIC buddy_persona_state_e s_persona_state = BUDDY_PERSONA_STATE_SLEEP;
STATIC buddy_led_state_e     s_led_state     = BUDDY_LED_STATE_OFF;
STATIC uint8_t               s_page          = 0;
STATIC uint8_t               s_log_scroll    = 0;

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
STATIC VOID_T __build_persona_strip(lv_obj_t *parent);
STATIC VOID_T __build_pages(lv_obj_t *parent);
STATIC VOID_T __build_page0(lv_obj_t *parent);
STATIC VOID_T __build_page1(lv_obj_t *parent);
STATIC VOID_T __build_page2(lv_obj_t *parent);
STATIC VOID_T __build_page3(lv_obj_t *parent);
STATIC VOID_T __refresh(VOID_T);
STATIC VOID_T __refresh_header(VOID_T);
STATIC VOID_T __refresh_persona_strip(VOID_T);
STATIC VOID_T __refresh_page_ind(VOID_T);
STATIC VOID_T __refresh_page0(VOID_T);
STATIC VOID_T __refresh_page1(VOID_T);
STATIC VOID_T __refresh_page2(VOID_T);
STATIC VOID_T __refresh_page3(VOID_T);
STATIC VOID_T __switch_page(uint8_t p);
STATIC VOID_T __persona_tick_cb(lv_timer_t *t);
STATIC VOID_T __persona_cycle(int8_t delta);
STATIC uint8_t __load_persona_id(VOID_T);
STATIC VOID_T __persist_persona_id(uint8_t id);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n);
STATIC VOID_T __derive_persona(VOID_T);
STATIC buddy_led_state_e __led_from_persona(buddy_persona_state_e s);
STATIC lv_obj_t *__lbl(lv_obj_t *parent, int32_t x, int32_t y,
                        int32_t w, const lv_font_t *font);

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
    const BOOL_T was_prompt = s_state.has_prompt ? TRUE : FALSE;
    s_state = *state;
    if (ui_buddy_main_screen) {
        if (s_state.has_prompt && !was_prompt) {
            /* Set LED now — __derive_persona() is never reached when we
             * switch screens, so we must drive the LED explicitly here. */
            s_led_state = BUDDY_LED_STATE_BLINK_FAST;
            (VOID_T)buddy_led_set(BUDDY_LED_STATE_BLINK_FAST);
            lv_vendor_disp_unlock();
            screen_load(&buddy_approval_screen);
            return;
        }
        __refresh();
    }
    lv_vendor_disp_unlock();
}

/* ---------------------------------------------------------------------------
 * Utilities
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *__lbl(lv_obj_t *parent, int32_t x, int32_t y,
                        int32_t w, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    /* Constrain to a single line: set both width and height explicitly.
     * This prevents multi-line expansion when content contains '\n'. */
    int32_t h = (font == FONT_M) ? H_M : H_S;
    lv_obj_set_size(l, (w > 0) ? w : LV_SIZE_CONTENT, h);
    lv_obj_set_style_pad_all(l, 0, 0);  /* remove LVGL default padding */
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
        (VOID_T)snprintf(out, n, "%u.%uk",
                         (unsigned)(v / 1000), (unsigned)((v % 1000) / 100));
    else
        (VOID_T)snprintf(out, n, "%u", (unsigned)v);
}

/* ---------------------------------------------------------------------------
 * Header
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

    /* Left: BLE status */
    lbl_ble = lv_label_create(bar);
    lv_label_set_text(lbl_ble, "BLE: --");
    lv_obj_set_style_text_font(lbl_ble, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_ble, C_INV_FG, 0);
    lv_obj_align(lbl_ble, LV_ALIGN_LEFT_MID, 4, 0);

    /* Left+52: model name (up to 14 chars, truncated) */
    lbl_model = lv_label_create(bar);
    lv_label_set_long_mode(lbl_model, LV_LABEL_LONG_DOT);
    lv_obj_set_size(lbl_model, 100, H_S);
    lv_label_set_text(lbl_model, "?");
    lv_obj_set_style_text_font(lbl_model, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_model, C_INV_FG, 0);
    lv_obj_align(lbl_model, LV_ALIGN_LEFT_MID, 52, 0);

    /* Center: clock */
    lbl_clock = lv_label_create(bar);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_clock, C_INV_FG, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_CENTER, 0, 0);

    /* Right-second: sessions + total tokens summary, e.g. "2s 1.2k" */
    lbl_stat_line = lv_label_create(bar);
    lv_label_set_long_mode(lbl_stat_line, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(lbl_stat_line, 72, H_S);
    lv_label_set_text(lbl_stat_line, "0s 0");
    lv_obj_set_style_text_font(lbl_stat_line, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_stat_line, C_INV_FG, 0);
    lv_obj_align(lbl_stat_line, LV_ALIGN_RIGHT_MID, -42, 0);

    /* Right: page indicator (4 chars + brackets = 6 chars) */
    lbl_page_ind = lv_label_create(bar);
    lv_label_set_text(lbl_page_ind, "[St]");
    lv_obj_set_style_text_font(lbl_page_ind, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_page_ind, C_INV_FG, 0);
    lv_obj_align(lbl_page_ind, LV_ALIGN_RIGHT_MID, -2, 0);
}

/* ---------------------------------------------------------------------------
 * Persona name + level strip (below the canvas, y=130~168)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_persona_strip(lv_obj_t *parent)
{
    /* Black background strip below persona canvas */
    lv_obj_t *strip = lv_obj_create(parent);
    lv_obj_set_size(strip, PERSONA_W, PERSONA_STRIP_H);
    lv_obj_set_pos(strip, 0, PERSONA_STRIP_Y);
    lv_obj_set_style_bg_color(strip, C_INV_BG, 0);
    lv_obj_set_style_border_width(strip, 0, 0);
    lv_obj_set_style_radius(strip, 0, 0);
    lv_obj_set_style_pad_all(strip, 1, 0);
    lv_obj_clear_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

    lbl_persona_name = lv_label_create(strip);
    lv_label_set_long_mode(lbl_persona_name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_persona_name, PERSONA_W - 4);
    lv_obj_set_style_text_font(lbl_persona_name, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_persona_name, C_INV_FG, 0);
    lv_obj_align(lbl_persona_name, LV_ALIGN_TOP_MID, 0, 1);

    lbl_persona_level = lv_label_create(strip);
    lv_label_set_long_mode(lbl_persona_level, LV_LABEL_LONG_DOT);
    lv_obj_set_width(lbl_persona_level, PERSONA_W - 4);
    lv_obj_set_style_text_font(lbl_persona_level, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_persona_level, C_INV_FG, 0);
    lv_obj_align(lbl_persona_level, LV_ALIGN_TOP_MID, 0, 17);
}

/* ---------------------------------------------------------------------------
 * Pages
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *__make_page(lv_obj_t *parent, BOOL_T hidden)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, INFO_W, INFO_H);
    lv_obj_set_pos(p, 0, 0);
    lv_obj_set_style_bg_color(p, C_BG, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 0, 0);
    lv_obj_set_style_pad_all(p, INFO_PAD, 0);
    lv_obj_clear_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    if (hidden) lv_obj_add_flag(p, LV_OBJ_FLAG_HIDDEN);
    return p;
}

STATIC lv_obj_t *__make_inv_hdr(lv_obj_t *parent, lv_obj_t **out_lbl)
{
    const int32_t W = INFO_W - INFO_PAD * 2;
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_set_size(bg, W, 16);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, C_INV_BG, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(bg);
    lv_obj_set_style_text_font(lbl, FONT_S, 0);
    lv_obj_set_style_text_color(lbl, C_INV_FG, 0);
    lv_obj_set_pos(lbl, 3, 1);
    if (out_lbl) *out_lbl = lbl;
    return bg;
}

STATIC lv_obj_t *__make_sep(lv_obj_t *parent, int32_t y)
{
    const int32_t W = INFO_W - INFO_PAD * 2;
    lv_obj_t *s = lv_obj_create(parent);
    lv_obj_set_size(s, W, 1);
    lv_obj_set_pos(s, 0, y);
    lv_obj_set_style_bg_color(s, C_FG, 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_set_style_radius(s, 0, 0);
    lv_obj_set_style_pad_all(s, 0, 0);
    return s;
}

/* --- Page 0: Status ---
 *
 * Layout in 238×148px (INFO_PAD=3):
 *   y=  0  msg          (FONT_M)
 *   y= 18  sep
 *   y= 21  sessions     "3s  1r  0w"
 *   y= 36  ctx_bar      "[#######.....] 35%  244k/1M"
 *   y= 50  ctx_detail   "Out:1.2k  R:244k  W:55"
 *   y= 64  model        "Model: sonnet[1m]"
 *   y= 78  owner        "Owner: alice"
 *   y= 94  sep
 *   y= 97  entry[0]     (FONT_M)
 *   y=115  entry[1]     (FONT_S)
 *   y=130  entry[2]     (FONT_S)
 */
STATIC VOID_T __build_page0(lv_obj_t *parent)
{
    pg0 = __make_page(parent, FALSE);
    const int32_t W = INFO_W - INFO_PAD * 2;

    pg0_msg          = __lbl(pg0, 0,  0, W, FONT_M);
    __make_sep(pg0, 18);
    pg0_sessions_line= __lbl(pg0, 0, 21, W, FONT_S);
    pg0_ctx_bar      = __lbl(pg0, 0, 36, W, FONT_S);
    pg0_ctx_detail   = __lbl(pg0, 0, 50, W, FONT_S);
    pg0_model_line   = __lbl(pg0, 0, 64, W, FONT_S);
    pg0_owner_line   = __lbl(pg0, 0, 78, W, FONT_S);
    __make_sep(pg0, 94);

    static const int32_t ENTRY_Y[5] = {97, 115, 130, 130, 130};
    for (uint32_t i = 0; i < STATUS_ENTRIES; i++) {
        const lv_font_t *f = (i == 0) ? FONT_M : FONT_S;
        pg0_entries[i] = __lbl(pg0, 0, ENTRY_Y[i], W, f);
        lv_obj_add_flag(pg0_entries[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* --- Page 1: Sessions --- */
STATIC VOID_T __build_page1(lv_obj_t *parent)
{
    pg1 = __make_page(parent, TRUE);
    const int32_t W = INFO_W - INFO_PAD * 2;

    __make_inv_hdr(pg1, &pg1_hdr);

    for (uint32_t i = 0; i < BUDDY_SESSIONS_MAX; i++) {
        /* name row + detail row: name(14) + 1px gap + detail(14) + 3px gap = 32px/session
         * 4 sessions: 18 + 4×32 = 146 → just fits (142 usable, trim 4px at bottom ok) */
        int32_t y  = (int32_t)(18 + i * 32);
        int32_t y2 = y + H_S + 1;  /* detail: 1px below name */
        pg1_rows[i][0] = __lbl(pg1, 0, y,  W,     FONT_S);
        pg1_rows[i][1] = __lbl(pg1, 4, y2, W - 4, FONT_S);
        lv_obj_add_flag(pg1_rows[i][0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pg1_rows[i][1], LV_OBJ_FLAG_HIDDEN);
    }
    pg1_empty = __lbl(pg1, 0, 20, W, FONT_S);
    lv_label_set_text(pg1_empty, "No sessions");
}

/* --- Page 2: Log --- */
STATIC VOID_T __build_page2(lv_obj_t *parent)
{
    pg2 = __make_page(parent, TRUE);
    const int32_t W = INFO_W - INFO_PAD * 2;
    /* Line spacing: h + 1px gap.
     * Row 0: FONT_M h=16, y=0,  ends=16  next=17
     * Row 1: FONT_S h=14, y=17, ends=31  next=32
     * Row 2: FONT_S h=14, y=32, ends=46  next=47
     * Row 3: FONT_S h=14, y=47, ends=61  next=62
     * Row 4: FONT_S h=14, y=62, ends=76  next=77
     * Row 5: FONT_S h=14, y=77, ends=91  next=92
     * Row 6: FONT_S h=14, y=92, ends=106 < 142 ✓
     */
    static const int32_t LOG_Y[LOG_VISIBLE] = {0, 17, 32, 47, 62, 77, 92};
    for (uint32_t i = 0; i < LOG_VISIBLE; i++) {
        pg2_lines[i] = __lbl(pg2, 0, LOG_Y[i], W,
                             (i == 0) ? FONT_M : FONT_S);
        lv_obj_add_flag(pg2_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* --- Page 3: Models --- */
STATIC VOID_T __build_page3(lv_obj_t *parent)
{
    pg3 = __make_page(parent, TRUE);
    const int32_t W = INFO_W - INFO_PAD * 2;

    __make_inv_hdr(pg3, &pg3_hdr);

    for (uint32_t i = 0; i < BUDDY_MSTATS_MAX; i++) {
        int32_t y = (int32_t)(20 + i * 30);
        pg3_rows[i][0] = __lbl(pg3, 0, y,          W, FONT_M); /* model name */
        pg3_rows[i][1] = __lbl(pg3, 4, (int32_t)(y + 16), W - 4, FONT_S); /* tokens */
        lv_obj_add_flag(pg3_rows[i][0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pg3_rows[i][1], LV_OBJ_FLAG_HIDDEN);
    }
    pg3_empty = __lbl(pg3, 0, 24, W, FONT_S);
    lv_label_set_text(pg3_empty, "No token data yet");
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
    __build_page3(container);

    /* Vertical divider */
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_set_size(div, DIV_W, BODY_H);
    lv_obj_set_pos(div, DIV_X, BODY_TOP);
    lv_obj_set_style_bg_color(div, C_FG, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);
}

/* ---------------------------------------------------------------------------
 * Page switching
 * --------------------------------------------------------------------------- */
STATIC VOID_T __switch_page(uint8_t p)
{
    s_page = p % PAGE_COUNT;
    s_log_scroll = 0;
    lv_obj_t *pages[PAGE_COUNT] = {pg0, pg1, pg2, pg3};
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
    case PAGE_MODELS:   __refresh_page3(); break;
    default: break;
    }
}

/* ---------------------------------------------------------------------------
 * Persona derivation
 * --------------------------------------------------------------------------- */
STATIC buddy_led_state_e __led_from_persona(buddy_persona_state_e s)
{
    /* LED only blinks fast during approval; off at all other times. */
    return (s == BUDDY_PERSONA_STATE_ATTENTION) ? BUDDY_LED_STATE_BLINK_FAST
                                                : BUDDY_LED_STATE_OFF;
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
            s_state.ble_connected ? "BLE: linked" : "BLE: --");
    if (lbl_model)
        lv_label_set_text(lbl_model,
            s_state.model[0] ? s_state.model : "?");
    if (lbl_clock) {
        char buf[6];
        __format_clock(&s_state, buf, sizeof(buf));
        lv_label_set_text(lbl_clock, buf);
    }
    if (lbl_stat_line) {
        char tok[8];
        __fmt_tok(s_state.tokens, tok, sizeof(tok));
        char buf[16];
        (VOID_T)snprintf(buf, sizeof(buf), "%u sessions",
                         (unsigned)s_state.sessions_total);
        lv_label_set_text(lbl_stat_line, buf);
    }
}

STATIC VOID_T __refresh_persona_strip(VOID_T)
{
    const persona_entry_t *p = persona_registry_get_by_id(s_persona_id);
    const char *pname = (p && p->persona && p->persona->name)
                        ? p->persona->name : "?";

    if (lbl_persona_name)
        lv_label_set_text(lbl_persona_name, pname);

    if (lbl_persona_level) {
        uint32_t tok = s_state.tokens;
        uint32_t level = tok / TOKENS_PER_LEVEL + 1U;
        uint32_t progress = (tok % TOKENS_PER_LEVEL) * LEVEL_BAR_BLOCKS / TOKENS_PER_LEVEL;
        char bar[16];
        uint32_t b;
        for (b = 0; b < LEVEL_BAR_BLOCKS; b++) {
            bar[b] = (b < progress) ? '#' : '.';
        }
        bar[LEVEL_BAR_BLOCKS] = '\0';
        char lvbuf[24];
        (VOID_T)snprintf(lvbuf, sizeof(lvbuf), "Level %u [%s]", (unsigned)level, bar);
        lv_label_set_text(lbl_persona_level, lvbuf);
    }
}

STATIC VOID_T __refresh_page_ind(VOID_T)
{
    if (!lbl_page_ind) return;
    static const char *const NAMES[PAGE_COUNT] = {"1/4", "2/4", "3/4", "4/4"};
    lv_label_set_text(lbl_page_ind, NAMES[s_page]);
}

STATIC VOID_T __refresh_page0(VOID_T)
{
    if (!pg0_msg) return;
    const int32_t W = INFO_W - INFO_PAD * 2;

    /* --- msg --- */
    lv_label_set_text(pg0_msg,
        s_state.msg[0] ? s_state.msg
        : (s_state.ble_connected ? "Ready." : "Waiting for Claude..."));

    /* --- sessions --- */
    if (pg0_sessions_line) {
        if (!s_state.ble_connected) {
            lv_label_set_text(pg0_sessions_line, "Not connected");
        } else {
            lv_label_set_text_fmt(pg0_sessions_line,
                "%u open  %u active  %u waiting",
                (unsigned)s_state.sessions_total,
                (unsigned)s_state.sessions_running,
                (unsigned)s_state.sessions_waiting);
        }
    }

    /* --- context window bar: "Context [########] 24%  244k/1M" --- */
    if (pg0_ctx_bar) {
        uint32_t used  = s_state.ctx_used;
        uint32_t total = s_state.ctx_total ? s_state.ctx_total : 200000U;
        uint32_t pct   = (uint32_t)(((uint64_t)used * 100U) / total);
        if (pct > 100U) pct = 100U;

        /* 8-char bar: "Context " (8 chars) + "[########]" = fits in 232px */
        char bar[10];
        uint32_t filled = pct * 8U / 100U;
        for (uint32_t b = 0; b < 8U; b++) {
            bar[b] = (b < filled) ? '#' : '.';
        }
        bar[8] = '\0';

        char used_s[10], tot_s[8];
        __fmt_tok(used, used_s, sizeof(used_s));
        if (total >= 1000000U)
            (VOID_T)snprintf(tot_s, sizeof(tot_s), "%uM",
                             (unsigned)(total / 1000000U));
        else
            (VOID_T)snprintf(tot_s, sizeof(tot_s), "%uk",
                             (unsigned)(total / 1000U));

        if (used) {
            lv_label_set_text_fmt(pg0_ctx_bar,
                "Context [%s] %u%%  %s/%s", bar, (unsigned)pct, used_s, tot_s);
        } else {
            lv_label_set_text_fmt(pg0_ctx_bar,
                "Context [--------] --  --/%s", tot_s);
        }
    }

    /* --- token detail: "Output: 1.2k  Read cache: 244k" --- */
    if (pg0_ctx_detail) {
        char out_s[10], cr_s[10];
        __fmt_tok(s_state.tokens,     out_s, sizeof(out_s));
        __fmt_tok(s_state.cache_read, cr_s,  sizeof(cr_s));
        if (s_state.tokens || s_state.cache_read) {
            lv_label_set_text_fmt(pg0_ctx_detail,
                "Output: %s  Read cache: %s", out_s, cr_s);
        } else {
            lv_label_set_text(pg0_ctx_detail, "Output: --  Read cache: --");
        }
    }

    /* --- model / owner --- */
    if (pg0_model_line)
        lv_label_set_text_fmt(pg0_model_line, "Model: %s",
            s_state.model[0] ? s_state.model : "unknown");
    if (pg0_owner_line)
        lv_label_set_text_fmt(pg0_owner_line, "Owner: %s",
            s_state.owner_name[0] ? s_state.owner_name : "-");

    /* --- recent entries (up to 3) --- */
    uint8_t count = s_state.entries_count;
    uint8_t head  = s_state.entries_head;
    static const int32_t ENTRY_Y[5] = {97, 115, 130, 130, 130};
    const uint32_t VISIBLE = 3;
    for (uint32_t i = 0; i < STATUS_ENTRIES; i++) {
        if (!pg0_entries[i]) continue;
        if (i >= VISIBLE || i >= (uint32_t)count) {
            lv_obj_add_flag(pg0_entries[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_obj_set_pos(pg0_entries[i], 0, ENTRY_Y[i]);
        lv_obj_set_width(pg0_entries[i], W);
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

        char name_buf[36];
        (VOID_T)snprintf(name_buf, sizeof(name_buf), "%s %s",
                         sess->is_running ? ">" : " ",
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

        char tok[10];
        __fmt_tok(sess->tokens_out, tok, sizeof(tok));
        char detail[40];
        (VOID_T)snprintf(detail, sizeof(detail), "  %s  out: %s",
                         sess->model[0] ? sess->model : "?", tok);
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

STATIC VOID_T __refresh_page3(VOID_T)
{
    uint8_t cnt = s_state.mstats_count;
    if (pg3_hdr)
        lv_label_set_text_fmt(pg3_hdr, "Model Usage (%u)", (unsigned)cnt);

    if (cnt == 0) {
        if (pg3_empty) lv_obj_clear_flag(pg3_empty, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t i = 0; i < BUDDY_MSTATS_MAX; i++) {
            if (pg3_rows[i][0]) lv_obj_add_flag(pg3_rows[i][0], LV_OBJ_FLAG_HIDDEN);
            if (pg3_rows[i][1]) lv_obj_add_flag(pg3_rows[i][1], LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (pg3_empty) lv_obj_add_flag(pg3_empty, LV_OBJ_FLAG_HIDDEN);

    for (uint32_t i = 0; i < BUDDY_MSTATS_MAX; i++) {
        if (!pg3_rows[i][0] || !pg3_rows[i][1]) continue;
        if (i >= (uint32_t)cnt) {
            lv_obj_add_flag(pg3_rows[i][0], LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(pg3_rows[i][1], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const buddy_mstat_t *ms = &s_state.mstats[i];
        lv_label_set_text(pg3_rows[i][0],
            ms->model[0] ? ms->model : "unknown");
        char tok[12];
        __fmt_tok(ms->tokens_out, tok, sizeof(tok));
        char detail[32];
        (VOID_T)snprintf(detail, sizeof(detail), "  Output: %s tokens", tok);
        lv_label_set_text(pg3_rows[i][1], detail);
        lv_obj_clear_flag(pg3_rows[i][0], LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pg3_rows[i][1], LV_OBJ_FLAG_HIDDEN);
    }
}

STATIC VOID_T __refresh(VOID_T)
{
    if (!ui_buddy_main_screen) return;
    __refresh_header();
    __refresh_persona_strip();
    __derive_persona();

    switch (s_page) {
    case PAGE_STATUS:   __refresh_page0(); break;
    case PAGE_SESSIONS: __refresh_page1(); break;
    case PAGE_LOG:      __refresh_page2(); break;
    case PAGE_MODELS:   __refresh_page3(); break;
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
    __refresh_persona_strip();
    __refresh();
}

STATIC uint8_t __load_persona_id(VOID_T)
{
    uint8_t v = 0; uint8_t *buf = NULL; size_t len = 0;
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
 * Key handler — ENTER does NOT cycle persona (JOYCON only)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
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
        /* Only the joystick center press cycles the persona */
        __persona_cycle(+1);
        break;
    case KEY_ENTER:
        /* Intentionally no-op in main screen to avoid mis-triggers */
        break;
    case KEY_ESC:
        screen_back();
        break;
    default:
        break;
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
    /* Persona canvas first (z-bottom); name/level strip after */
    ascii_persona_attach(ui_buddy_main_screen, 0, BODY_TOP);
    __build_persona_strip(ui_buddy_main_screen);
    __build_pages(ui_buddy_main_screen);

    s_page = 0; s_log_scroll = 0;
    s_persona_id = __load_persona_id();

    buddy_tama_state_t snap;
    buddy_ble_snapshot(&snap);
    s_state = snap;
    s_prev_prompt = snap.has_prompt ? TRUE : FALSE;

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

    lbl_model = lbl_clock = lbl_ble = lbl_stat_line = lbl_page_ind = NULL;
    lbl_persona_name = lbl_persona_level = NULL;
    pg0 = pg0_msg = pg0_sessions_line = pg0_ctx_bar = pg0_ctx_detail = NULL;
    pg0_model_line = pg0_owner_line = NULL;
    for (uint32_t i = 0; i < STATUS_ENTRIES; i++) pg0_entries[i] = NULL;
    pg1 = pg1_hdr = pg1_empty = NULL;
    for (uint32_t i = 0; i < BUDDY_SESSIONS_MAX; i++)
        pg1_rows[i][0] = pg1_rows[i][1] = NULL;
    pg2 = NULL;
    for (uint32_t i = 0; i < LOG_VISIBLE; i++) pg2_lines[i] = NULL;
    pg3 = pg3_hdr = pg3_empty = NULL;
    for (uint32_t i = 0; i < BUDDY_MSTATS_MAX; i++)
        pg3_rows[i][0] = pg3_rows[i][1] = NULL;
    s_log_scroll = 0; s_page = 0;
}
