/**
 * @file chart_screen.c
 * @brief Claude Desktop Buddy — Tab 3 "Chart" screen.
 *
 * Renders an ASCII bar chart of the last 7 / 14 / 28 days of output tokens
 * from s_state.daily_tokens[].  Index 0 = today, index 27 = 28 days ago.
 *
 * Layout (384×168, monochrome):
 *   Header    y=  0..20   (20px) — black bg, white text
 *   Title row y= 20..36   (16px) — period label + key hint
 *   Chart     y= 36..138  (102px) — filled-rect bar columns
 *   X-axis    y=138..154  (16px) — abbreviated date labels
 *   (gap)     y=154..162  ( 8px)
 *   Nav bar   y=162..168  ( 6px) — "St|Hm|Ch|Pi"
 *
 * Bar geometry:
 *   Chart container width = SCR_W - 2*CHART_PAD_X = 370px.
 *   Bar columns (lv_obj filled rectangles) are distributed evenly.
 *   Max bar height = CHART_H px; normalized to visible data max.
 *   Minimum visible bar height = 1px (so zero-data days stay at 1px stub).
 *
 * Navigation:
 *   UP / DOWN  — cycle period forward (7d→14d→28d→7d)
 *   RIGHT      — screen_load(&buddy_pie_screen)
 *   LEFT / ESC — screen_back()
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "chart_screen.h"
#include "pie_screen.h"
#include "buddy_protocol.h"
#include "buddy_transport.h"
#include "buddy_types.h"
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
#include "buddy_cjk_font.h"
#define FONT_M   (&buddy_font_m)
#define FONT_S   (&buddy_font_s)

/* ---------------------------------------------------------------------------
 * Colors
 * --------------------------------------------------------------------------- */
#define C_BG      lv_color_white()
#define C_FG      lv_color_black()
#define C_INV_BG  lv_color_black()
#define C_INV_FG  lv_color_white()

/* ---------------------------------------------------------------------------
 * Layout constants
 * --------------------------------------------------------------------------- */
#define SCR_W       AI_PET_SCREEN_WIDTH   /* 384 */
#define SCR_H       AI_PET_SCREEN_HEIGHT  /* 168 */
#define HEADER_H    20

/* Title row */
#define TITLE_Y     20
#define TITLE_H     16

/* Chart area */
#define CHART_Y     36
#define CHART_H     102   /* 138 - 36 */
#define CHART_BOT   (CHART_Y + CHART_H)   /* 138 */

/* X-axis label row */
#define XLABEL_Y    138
#define XLABEL_H    16

/* Nav bar */
#define NAV_Y       162
#define NAV_H       6

/* Horizontal padding inside chart */
#define CHART_PAD_X  7
/* Y-axis label column on the right side */
#define YAXIS_W      36
/* Bar area excludes the Y-axis column: ~334px */
#define CHART_W      (SCR_W - 2 * CHART_PAD_X - YAXIS_W)

/* Font heights */
#define H_M  16
#define H_S  14

/* ---------------------------------------------------------------------------
 * Chart data
 * --------------------------------------------------------------------------- */
#define CHART_MAX_BARS  28
#define PERIOD_COUNT     3

/* Minimum stub height so a zero-token bar is still visible */
#define BAR_MIN_H   1

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_screen = NULL;

/* Header */
STATIC lv_obj_t *s_lbl_clock   = NULL;
STATIC lv_obj_t *s_lbl_left    = NULL;
STATIC lv_obj_t *s_lbl_right   = NULL;

/* Title */
STATIC lv_obj_t *s_title_lbl   = NULL;

/* Chart bars and x-axis labels */
STATIC lv_obj_t *s_bars[CHART_MAX_BARS];
STATIC lv_obj_t *s_xlabels[CHART_MAX_BARS];

/* Chart container (clip area) */
STATIC lv_obj_t *s_chart_cont  = NULL;

/* Nav bar segments — stored so __deinit can null them */
STATIC lv_obj_t *s_nav_lbl     = NULL;

/* Y-axis labels (right side) */
STATIC lv_obj_t *s_yaxis_max   = NULL;
STATIC lv_obj_t *s_yaxis_min   = NULL;

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
STATIC buddy_tama_state_t s_state  = {0};
STATIC uint8_t            s_period = 0;   /* 0=7d  1=14d  2=28d */

/* Period table */
STATIC const uint8_t PERIOD_DAYS[PERIOD_COUNT] = {7, 14, 28};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __init(VOID_T);
STATIC VOID_T __deinit(VOID_T);
STATIC VOID_T __key_cb(lv_event_t *e);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_title(lv_obj_t *parent);
STATIC VOID_T __build_chart(lv_obj_t *parent);
STATIC VOID_T __build_nav(lv_obj_t *parent);
STATIC VOID_T __refresh_header(VOID_T);
STATIC VOID_T __redraw_chart(VOID_T);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n);
STATIC VOID_T __day_date(const buddy_tama_state_t *s, uint8_t offset,
                         char *out, size_t n);

/* ---------------------------------------------------------------------------
 * Exported screen descriptor
 * --------------------------------------------------------------------------- */
Screen_t buddy_chart_screen = {
    .init       = __init,
    .deinit     = __deinit,
    .screen_obj = &ui_screen,
    .name       = "buddy_chart_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Utilities
 * --------------------------------------------------------------------------- */
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
    (VOID_T)snprintf(out, n, "%02d:%02d",
                     (int)(sod / 3600), (int)((sod / 60) % 60));
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

    /* Left: "claude buddy" */
    s_lbl_left = lv_label_create(bar);
    lv_label_set_text(s_lbl_left, "claude buddy");
    lv_obj_set_style_text_font(s_lbl_left, FONT_S, 0);
    lv_obj_set_style_text_color(s_lbl_left, C_INV_FG, 0);
    lv_obj_align(s_lbl_left, LV_ALIGN_LEFT_MID, 4, 0);

    /* Center: clock */
    s_lbl_clock = lv_label_create(bar);
    lv_label_set_text(s_lbl_clock, "--:--");
    lv_obj_set_style_text_font(s_lbl_clock, FONT_S, 0);
    lv_obj_set_style_text_color(s_lbl_clock, C_INV_FG, 0);
    lv_obj_align(s_lbl_clock, LV_ALIGN_CENTER, 0, 0);

    /* Right: tab indicator */
    s_lbl_right = lv_label_create(bar);
    lv_label_set_text(s_lbl_right, "W B 87%");
    lv_obj_set_style_text_font(s_lbl_right, FONT_S, 0);
    lv_obj_set_style_text_color(s_lbl_right, C_INV_FG, 0);
    lv_obj_align(s_lbl_right, LV_ALIGN_RIGHT_MID, -4, 0);
}

/* ---------------------------------------------------------------------------
 * Title row
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_title(lv_obj_t *parent)
{
    s_title_lbl = lv_label_create(parent);
    lv_label_set_long_mode(s_title_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(s_title_lbl, SCR_W - 8, H_M);
    lv_obj_set_style_pad_all(s_title_lbl, 0, 0);
    lv_obj_set_style_text_font(s_title_lbl, FONT_M, 0);
    lv_obj_set_style_text_color(s_title_lbl, C_FG, 0);
    lv_obj_set_pos(s_title_lbl, 4, TITLE_Y);
    lv_label_set_text(s_title_lbl, "Last 7d  (UP/DOWN change)");
}

/* ---------------------------------------------------------------------------
 * Chart area — allocate widget pool
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_chart(lv_obj_t *parent)
{
    /* Clip container for the bar columns */
    s_chart_cont = lv_obj_create(parent);
    lv_obj_set_size(s_chart_cont, SCR_W, CHART_H + XLABEL_H);
    lv_obj_set_pos(s_chart_cont, 0, CHART_Y);
    lv_obj_set_style_bg_color(s_chart_cont, C_BG, 0);
    lv_obj_set_style_border_width(s_chart_cont, 0, 0);
    lv_obj_set_style_radius(s_chart_cont, 0, 0);
    lv_obj_set_style_pad_all(s_chart_cont, 0, 0);
    lv_obj_clear_flag(s_chart_cont, LV_OBJ_FLAG_SCROLLABLE);

    /* Pre-allocate all bar objects and x-axis labels.
     * They are positioned and sized by __redraw_chart(). */
    for (uint32_t i = 0; i < CHART_MAX_BARS; i++) {
        /* Bar rectangle */
        s_bars[i] = lv_obj_create(s_chart_cont);
        lv_obj_set_style_bg_color(s_bars[i], C_FG, 0);
        lv_obj_set_style_border_width(s_bars[i], 0, 0);
        lv_obj_set_style_radius(s_bars[i], 0, 0);
        lv_obj_set_style_pad_all(s_bars[i], 0, 0);
        lv_obj_set_size(s_bars[i], 2, 1);       /* placeholder size */
        lv_obj_set_pos(s_bars[i], 0, CHART_H);  /* out of view until redrawn */
        lv_obj_add_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);

        /* X-axis label — 36px wide for "MM/DD" format */
        s_xlabels[i] = lv_label_create(s_chart_cont);
        lv_label_set_long_mode(s_xlabels[i], LV_LABEL_LONG_CLIP);
        lv_obj_set_size(s_xlabels[i], 36, H_S);
        lv_obj_set_style_pad_all(s_xlabels[i], 0, 0);
        lv_obj_set_style_text_font(s_xlabels[i], FONT_S, 0);
        lv_obj_set_style_text_color(s_xlabels[i], C_FG, 0);
        lv_obj_set_pos(s_xlabels[i], 0, CHART_H);  /* below chart area */
        lv_label_set_text(s_xlabels[i], "");
        lv_obj_add_flag(s_xlabels[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Y-axis max/min labels (right side column) */
    int32_t yaxis_x = CHART_PAD_X + CHART_W + 2;   /* right of bar area */

    s_yaxis_max = lv_label_create(s_chart_cont);
    lv_label_set_long_mode(s_yaxis_max, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(s_yaxis_max, YAXIS_W - 2, H_S);
    lv_obj_set_style_pad_all(s_yaxis_max, 0, 0);
    lv_obj_set_style_text_font(s_yaxis_max, FONT_S, 0);
    lv_obj_set_style_text_color(s_yaxis_max, C_FG, 0);
    lv_obj_set_pos(s_yaxis_max, yaxis_x, 0);
    lv_label_set_text(s_yaxis_max, "");

    s_yaxis_min = lv_label_create(s_chart_cont);
    lv_label_set_long_mode(s_yaxis_min, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(s_yaxis_min, YAXIS_W - 2, H_S);
    lv_obj_set_style_pad_all(s_yaxis_min, 0, 0);
    lv_obj_set_style_text_font(s_yaxis_min, FONT_S, 0);
    lv_obj_set_style_text_color(s_yaxis_min, C_FG, 0);
    lv_obj_set_pos(s_yaxis_min, yaxis_x, CHART_H - H_S);
    lv_label_set_text(s_yaxis_min, "0");
}

/* ---------------------------------------------------------------------------
 * Nav bar — 4 pure colored segments (no text); Tab 3 = Ch highlighted
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_nav(lv_obj_t *parent)
{
    /* Black background strip */
    lv_obj_t *nav_bg = lv_obj_create(parent);
    lv_obj_set_size(nav_bg, SCR_W, NAV_H);
    lv_obj_set_pos(nav_bg, 0, NAV_Y);
    lv_obj_set_style_bg_color(nav_bg, C_INV_BG, 0);
    lv_obj_set_style_border_width(nav_bg, 0, 0);
    lv_obj_set_style_radius(nav_bg, 0, 0);
    lv_obj_set_style_pad_all(nav_bg, 0, 0);
    lv_obj_clear_flag(nav_bg, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < 4; i++) {
        BOOL_T active = (i == 2) ? TRUE : FALSE;   /* Tab 2 = Ch */
        lv_obj_t *cell = lv_obj_create(nav_bg);
        lv_obj_set_size(cell, SCR_W / 4, NAV_H);
        lv_obj_set_pos(cell, (int32_t)(i * (uint32_t)(SCR_W / 4)), 0);
        lv_obj_set_style_bg_color(cell, active ? C_BG : C_INV_BG, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_radius(cell, active ? (NAV_H / 2) : 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    }
}

/* ---------------------------------------------------------------------------
 * Refresh header labels
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_header(VOID_T)
{
    if (s_lbl_clock) {
        char buf[8];
        __format_clock(&s_state, buf, sizeof(buf));
        lv_label_set_text(s_lbl_clock, buf);
    }
    if (s_lbl_right) {
        /* Show wifi/WS icons and total tokens */
        const char *wifi = buddy_ws_is_connected() ? LV_SYMBOL_WIFI      : " ";
        const char *bt   = s_state.ws_connected          ? LV_SYMBOL_BLUETOOTH : " ";
        char tok[12];
        __fmt_tok(s_state.tokens, tok, sizeof(tok));
        char buf[32];
        (VOID_T)snprintf(buf, sizeof(buf), "%s %s %s", wifi, bt, tok);
        lv_label_set_text(s_lbl_right, buf);
    }
}

/* ---------------------------------------------------------------------------
 * Convert day offset to "MM/DD" string using Julian Day Number algorithm.
 * offset 0 = today, 1 = yesterday, etc.
 * Falls back to "-N" format when wall clock is unavailable.
 * --------------------------------------------------------------------------- */
STATIC VOID_T __day_date(const buddy_tama_state_t *s, uint8_t offset,
                         char *out, size_t n)
{
    if (s && s->wall_epoch_s) {
        uint64_t now_ms   = tal_system_get_millisecond();
        int64_t  delta_ms = (int64_t)(now_ms - s->wall_local_ms_at_rx);
        int64_t  epoch    = s->wall_epoch_s + delta_ms / 1000
                            + (int64_t)s->wall_tz_min * 60;
        /* Unix day number for the target date */
        int64_t  epoch_day = (epoch / 86400) - (int64_t)offset;
        /* Julian Day Number (1970-01-01 = JD 2440588) */
        int32_t JD = (int32_t)(epoch_day + 2440588);
        int32_t a  = JD + 32044;
        int32_t b  = (4 * a + 3) / 146097;
        int32_t c  = a - (146097 * b) / 4;
        int32_t d  = (4 * c + 3) / 1461;
        int32_t e  = c - (1461 * d) / 4;
        int32_t m  = (5 * e + 2) / 153;
        int32_t day   = e - (153 * m + 2) / 5 + 1;
        int32_t month = m + 3 - 12 * (m / 10);
        (VOID_T)snprintf(out, n, "%02d/%02d", (int)month, (int)day);
    } else {
        (VOID_T)snprintf(out, n, "-%u", (unsigned)offset);
    }
}

/* ---------------------------------------------------------------------------
 * Animation setter: grow bar upward (bottom-anchored).
 * --------------------------------------------------------------------------- */
static void __bar_h_set(void *obj, int32_t v)
{
    lv_obj_t *bar  = (lv_obj_t *)obj;
    int32_t   w    = lv_obj_get_width(bar);
    int32_t   full = lv_obj_get_style_height(bar, 0);
    /* bar is bottom-anchored: keep bottom edge fixed, grow upward */
    int32_t   orig_bottom = lv_obj_get_y(bar) + lv_obj_get_height(bar);
    lv_obj_set_size(bar, w, v);
    lv_obj_set_y(bar, orig_bottom - v);
    (void)full;
}

/* ---------------------------------------------------------------------------
 * Redraw chart for current period.
 * All widget geometry is recalculated from s_state.daily_tokens[].
 * --------------------------------------------------------------------------- */
STATIC VOID_T __redraw_chart(VOID_T)
{
    uint8_t  n_bars   = PERIOD_DAYS[s_period];   /* 7, 14, or 28 */

    /* --- Update title label --- */
    if (s_title_lbl) {
        static const char *const PERIOD_LABEL[PERIOD_COUNT] = {
            "Last 7d", "Last 14d", "Last 28d"
        };
        char buf[48];
        (VOID_T)snprintf(buf, sizeof(buf), "%s  (UP/DOWN to change)",
                         PERIOD_LABEL[s_period]);
        lv_label_set_text(s_title_lbl, buf);
    }

    /* --- Find max token count in visible range --- */
    uint32_t max_tok = 1U;   /* avoid divide-by-zero */
    for (uint8_t i = 0; i < n_bars; i++) {
        if (s_state.daily_tokens[i] > max_tok)
            max_tok = s_state.daily_tokens[i];
    }

    /* --- Update Y-axis labels --- */
    if (s_yaxis_max) {
        char buf[12];
        __fmt_tok(max_tok, buf, sizeof(buf));
        lv_label_set_text(s_yaxis_max, buf);
    }
    if (s_yaxis_min) lv_label_set_text(s_yaxis_min, "0");

    /* --- Bar geometry --- */
    /* Usable horizontal width for all bars */
    int32_t usable_w  = CHART_W;
    /* Bar pitch (center-to-center) */
    int32_t pitch     = usable_w / (int32_t)n_bars;
    /* Bar column drawn width: leave a 1px gap on right side when possible */
    int32_t bar_w     = (pitch > 2) ? (pitch - 1) : 1;
    /* Maximum bar height in pixels */
    int32_t max_bar_h = CHART_H;

    /* Hide all bars first, then show only the active ones */
    for (uint32_t i = 0; i < CHART_MAX_BARS; i++) {
        if (s_bars[i])   lv_obj_add_flag(s_bars[i],   LV_OBJ_FLAG_HIDDEN);
        if (s_xlabels[i]) lv_obj_add_flag(s_xlabels[i], LV_OBJ_FLAG_HIDDEN);
    }

    for (uint8_t i = 0; i < n_bars; i++) {
        /* Bars are displayed left=oldest, right=newest.
         * daily_tokens[0] = today (rightmost), daily_tokens[n-1] = oldest (leftmost). */
        uint8_t data_idx = (uint8_t)(n_bars - 1 - i);   /* 0=oldest on left */
        uint32_t tok = s_state.daily_tokens[data_idx];

        /* Normalise height */
        int32_t bar_h;
        if (tok == 0) {
            bar_h = BAR_MIN_H;
        } else {
            bar_h = (int32_t)(((uint64_t)tok * (uint32_t)max_bar_h) / max_tok);
            if (bar_h < BAR_MIN_H) bar_h = BAR_MIN_H;
            if (bar_h > max_bar_h) bar_h = max_bar_h;
        }

        /* Bar x: left edge of this column */
        int32_t bar_x = CHART_PAD_X + (int32_t)i * pitch;
        /* Bar y: grows upward from chart bottom */
        int32_t bar_y = CHART_H - bar_h;   /* relative to s_chart_cont top */

        if (s_bars[i]) {
            /* Start bar at bottom (height=1), animate grow to bar_h */
            lv_obj_set_pos(s_bars[i], bar_x, bar_y + bar_h - 1);
            lv_obj_set_size(s_bars[i], bar_w, 1);
            lv_obj_clear_flag(s_bars[i], LV_OBJ_FLAG_HIDDEN);

            lv_anim_t a;
            lv_anim_init(&a);
            lv_anim_set_var(&a, s_bars[i]);
            lv_anim_set_exec_cb(&a, __bar_h_set);
            lv_anim_set_values(&a, 1, bar_h);
            lv_anim_set_time(&a, 200);
            lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
            lv_anim_set_delay(&a, (uint32_t)i * 8);
            lv_anim_start(&a);
        }

        /* X-axis label: centered under bar column */
        if (s_xlabels[i]) {
            char date_str[8];
            uint8_t day_offset = data_idx;   /* offset 0=today, growing */
            __day_date(&s_state, day_offset, date_str, sizeof(date_str));

            /* Only draw labels when there's enough horizontal space.
             * Show every label for 7d; every 2nd for 14d; every 4th for 28d. */
            uint8_t label_step = (n_bars <= 7) ? 1U :
                                 (n_bars <= 14) ? 2U : 4U;
            if ((i % label_step) == 0) {
                lv_label_set_text(s_xlabels[i], date_str);
                /* Centre the label over the bar */
                int32_t lbl_w = (int32_t)lv_obj_get_width(s_xlabels[i]);
                int32_t lbl_x = bar_x + bar_w / 2 - lbl_w / 2;
                lv_obj_set_pos(s_xlabels[i], lbl_x, CHART_H);
                lv_obj_clear_flag(s_xlabels[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Key handler
 * --------------------------------------------------------------------------- */
STATIC VOID_T __key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case KEY_UP:
    case KEY_DOWN:
        /* Both directions cycle forward: 7d→14d→28d→7d */
        s_period = (uint8_t)((s_period + 1) % PERIOD_COUNT);
        lv_vendor_disp_lock();
        __redraw_chart();
        lv_vendor_disp_unlock();
        break;

    case KEY_LEFT:
    case KEY_ESC:
        screen_back();
        break;

    case KEY_RIGHT:
        screen_load(&buddy_pie_screen);
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
    ui_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(ui_screen, C_BG, 0);
    lv_obj_set_style_pad_all(ui_screen, 0, 0);
    lv_obj_clear_flag(ui_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Snapshot latest state + pull fresh heartbeat from the daemon so the
     * 7/14/28d daily-tokens array is up-to-date when this screen opens. */
    buddy_state_snapshot(&s_state);
    (VOID_T)buddy_ws_send_hb_req("chart");

    /* Build UI */
    __build_header(ui_screen);
    __build_title(ui_screen);
    __build_chart(ui_screen);
    __build_nav(ui_screen);

    /* Initial render */
    __refresh_header();
    __redraw_chart();

    /* Input */
    lv_obj_add_event_cb(ui_screen, __key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_screen);
    lv_group_focus_obj(ui_screen);

    PR_NOTICE("[%s] init period=%ud", buddy_chart_screen.name,
              (unsigned)PERIOD_DAYS[s_period]);
}

STATIC VOID_T __deinit(VOID_T)
{
    if (ui_screen) {
        lv_obj_remove_event_cb(ui_screen, __key_cb);
        lv_group_remove_obj(ui_screen);
    }

    s_lbl_clock = NULL;
    s_lbl_left  = NULL;
    s_lbl_right = NULL;
    s_title_lbl = NULL;
    s_chart_cont = NULL;
    s_nav_lbl   = NULL;
    s_yaxis_max = NULL;
    s_yaxis_min = NULL;

    for (uint32_t i = 0; i < CHART_MAX_BARS; i++) {
        s_bars[i]    = NULL;
        s_xlabels[i] = NULL;
    }
}
