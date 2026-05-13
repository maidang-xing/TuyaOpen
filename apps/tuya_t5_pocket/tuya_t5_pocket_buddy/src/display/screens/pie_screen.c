/**
 * @file pie_screen.c
 * @brief Claude Desktop Buddy — Tab 4 "Model Usage" pie chart screen.
 *
 * dev_4_27 UI update — render an actual pie chart (not horizontal bars).
 * Data comes from s_state.mstats[] (up to BUDDY_MSTATS_MAX = 4 models).
 *
 * Layout (384x168, monochrome):
 *   Header   y=  0..20   black bg, white text
 *   Title    y= 20..36   "Model Token Usage"
 *   Body     y= 36..162  pie disc on the left + legend on the right
 *   Nav bar  y=162..168  4 cells, "Pi" highlighted
 *
 * Pie disc:
 *   - Diameter D=100, centered at (70, 99). Up to 4 wedges, each rendered
 *     as a separate lv_arc with line_width = R so the indicator fills the
 *     whole wedge from center to perimeter.
 *   - Adjacent wedges are separated by ~2 degree white gaps so they are
 *     visually distinguishable in pure black-and-white.
 *   - A small numeric label "1".."4" is drawn at 0.7*R from center inside
 *     each wedge so users can map a wedge to its legend row.
 *   - Single-model edge case: full circle, no gaps, label at center.
 *
 * Legend (right of pie):
 *   - 4 rows. Each row: numbered black square (1..4) on the left,
 *     "<model>  <tok>  NN%" on the right (FONT_S).
 *   - Sorted by tokens descending so the largest wedge maps to the top
 *     legend row.
 *
 * Navigation:
 *   LEFT / ESC — screen_back()
 *   (No RIGHT — last tab)
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

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

#define TITLE_Y     20
#define TITLE_H     16

/* Pie disc */
#define PIE_CX      70
#define PIE_CY      99
#define PIE_R       50
#define PIE_D       (PIE_R * 2)
#define PIE_X       (PIE_CX - PIE_R)    /* 20 */
#define PIE_Y       (PIE_CY - PIE_R)    /* 49 */

/* Angular gap (degrees) inserted between adjacent wedges so they remain
 * visually distinct on a pure B&W display. Total reserved = N*GAP. */
#define WEDGE_GAP_DEG   2

/* Numeric label radius (70% of R) — places "1".."4" inside the wedge */
#define LABEL_R_NUM     7
#define LABEL_R_DEN     10

/* Legend layout (right of pie) */
#define LEG_X           130
#define LEG_Y           44
#define LEG_ROW_H       24
#define LEG_MARK_W      14
#define LEG_MARK_H      14
#define LEG_TXT_X       (LEG_X + LEG_MARK_W + 6)
#define LEG_TXT_W       (SCR_W - LEG_TXT_X - 4)

/* Nav bar */
#define NAV_Y           162
#define NAV_H           6

/* Font heights */
#define H_M  16
#define H_S  14

#define PIE_MAX_ROWS    BUDDY_MSTATS_MAX   /* 4 */

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_screen      = NULL;

/* Header */
STATIC lv_obj_t *s_lbl_left     = NULL;
STATIC lv_obj_t *s_lbl_clock    = NULL;
STATIC lv_obj_t *s_lbl_right    = NULL;

/* Title */
STATIC lv_obj_t *s_title_lbl    = NULL;

/* Pie wedges + in-wedge numeric labels */
STATIC lv_obj_t *s_arc[PIE_MAX_ROWS];
STATIC lv_obj_t *s_wedge_num[PIE_MAX_ROWS];

/* Legend rows */
STATIC lv_obj_t *s_leg_mark[PIE_MAX_ROWS];   /* black square w/ number "1".."4" */
STATIC lv_obj_t *s_leg_mark_lbl[PIE_MAX_ROWS]; /* white "1".."4" inside square */
STATIC lv_obj_t *s_leg_txt[PIE_MAX_ROWS];    /* "<model>  <tok>  NN%" */

/* "No data" empty-state label */
STATIC lv_obj_t *s_empty_lbl    = NULL;

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
STATIC buddy_tama_state_t s_state = {0};

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __init(VOID_T);
STATIC VOID_T __deinit(VOID_T);
STATIC VOID_T __key_cb(lv_event_t *e);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_title(lv_obj_t *parent);
STATIC VOID_T __build_pie(lv_obj_t *parent);
STATIC VOID_T __build_legend(lv_obj_t *parent);
STATIC VOID_T __build_nav(lv_obj_t *parent);
STATIC VOID_T __refresh_header(VOID_T);
STATIC VOID_T __refresh_pie(VOID_T);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n);

/* ---------------------------------------------------------------------------
 * Exported screen descriptor
 * --------------------------------------------------------------------------- */
Screen_t buddy_pie_screen = {
    .init       = __init,
    .deinit     = __deinit,
    .screen_obj = &ui_screen,
    .name       = "buddy_pie_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Utilities
 * --------------------------------------------------------------------------- */
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n)
{
    if (!out || n < 6) {
        return;
    }
    if (!s || !s->wall_epoch_s) {
        (VOID_T)snprintf(out, n, "--:--");
        return;
    }
    uint64_t now_ms   = tal_system_get_millisecond();
    int64_t  delta_ms = (int64_t)(now_ms - s->wall_local_ms_at_rx);
    int64_t  epoch    = s->wall_epoch_s + delta_ms / 1000 +
                        (int64_t)s->wall_tz_min * 60;
    int64_t  sod = epoch % 86400;
    if (sod < 0) {
        sod += 86400;
    }
    (VOID_T)snprintf(out, n, "%02d:%02d",
                     (int)(sod / 3600), (int)((sod / 60) % 60));
}

STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n)
{
    if (v >= 1000U) {
        (VOID_T)snprintf(out, n, "%u.%uk",
                         (unsigned)(v / 1000),
                         (unsigned)((v % 1000) / 100));
    } else {
        (VOID_T)snprintf(out, n, "%u", (unsigned)v);
    }
}

/* ---------------------------------------------------------------------------
 * Header builder
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

    s_lbl_left = lv_label_create(bar);
    lv_label_set_text(s_lbl_left, "claude buddy");
    lv_obj_set_style_text_font(s_lbl_left, FONT_S, 0);
    lv_obj_set_style_text_color(s_lbl_left, C_INV_FG, 0);
    lv_obj_align(s_lbl_left, LV_ALIGN_LEFT_MID, 4, 0);

    s_lbl_clock = lv_label_create(bar);
    lv_label_set_text(s_lbl_clock, "--:--");
    lv_obj_set_style_text_font(s_lbl_clock, FONT_S, 0);
    lv_obj_set_style_text_color(s_lbl_clock, C_INV_FG, 0);
    lv_obj_align(s_lbl_clock, LV_ALIGN_CENTER, 0, 0);

    s_lbl_right = lv_label_create(bar);
    lv_label_set_text(s_lbl_right, "");
    lv_obj_set_style_text_font(s_lbl_right, FONT_S, 0);
    lv_obj_set_style_text_color(s_lbl_right, C_INV_FG, 0);
    lv_obj_align(s_lbl_right, LV_ALIGN_RIGHT_MID, -4, 0);
}

/* ---------------------------------------------------------------------------
 * Title builder
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
    lv_label_set_text(s_title_lbl, "Model Token Usage");
}

/* ---------------------------------------------------------------------------
 * Pie disc builder
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_pie(lv_obj_t *parent)
{
    for (uint32_t i = 0; i < PIE_MAX_ROWS; i++) {
        lv_obj_t *arc = lv_arc_create(parent);
        lv_obj_set_size(arc, PIE_D, PIE_D);
        lv_obj_set_pos(arc, PIE_X, PIE_Y);
        lv_arc_set_rotation(arc, 270);
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_angles(arc, 0, 0);

        lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(arc, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_set_style_arc_color(arc, C_BG, LV_PART_MAIN);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, PIE_R, LV_PART_MAIN);

        lv_obj_set_style_arc_color(arc, C_FG, LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
        lv_obj_set_style_arc_width(arc, PIE_R, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, false, LV_PART_MAIN);

        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_pad_all(arc, 0, LV_PART_KNOB);

        lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        s_arc[i] = arc;

        lv_obj_t *num = lv_label_create(parent);
        lv_obj_set_style_pad_all(num, 0, 0);
        lv_obj_set_style_text_font(num, FONT_S, 0);
        lv_obj_set_style_text_color(num, C_INV_FG, 0);
        lv_label_set_text(num, "");
        lv_obj_add_flag(num, LV_OBJ_FLAG_HIDDEN);
        s_wedge_num[i] = num;
    }

    s_empty_lbl = lv_label_create(parent);
    lv_label_set_long_mode(s_empty_lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(s_empty_lbl, SCR_W - 8, H_S);
    lv_obj_set_style_pad_all(s_empty_lbl, 0, 0);
    lv_obj_set_style_text_font(s_empty_lbl, FONT_S, 0);
    lv_obj_set_style_text_color(s_empty_lbl, C_FG, 0);
    lv_obj_align(s_empty_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(s_empty_lbl, "No model data yet");
    lv_obj_add_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------------------
 * Legend builder
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_legend(lv_obj_t *parent)
{
    for (uint32_t i = 0; i < PIE_MAX_ROWS; i++) {
        int32_t row_y = (int32_t)(LEG_Y + i * (uint32_t)LEG_ROW_H);

        lv_obj_t *m = lv_obj_create(parent);
        lv_obj_set_size(m, LEG_MARK_W, LEG_MARK_H);
        lv_obj_set_pos(m, LEG_X, row_y);
        lv_obj_set_style_bg_color(m, C_FG, 0);
        lv_obj_set_style_bg_opa(m, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(m, 0, 0);
        lv_obj_set_style_radius(m, 2, 0);
        lv_obj_set_style_pad_all(m, 0, 0);
        lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(m, LV_OBJ_FLAG_HIDDEN);
        s_leg_mark[i] = m;

        lv_obj_t *ml = lv_label_create(m);
        lv_obj_set_style_text_font(ml, FONT_S, 0);
        lv_obj_set_style_text_color(ml, C_INV_FG, 0);
        lv_obj_set_style_pad_all(ml, 0, 0);
        lv_label_set_text(ml, "");
        lv_obj_align(ml, LV_ALIGN_CENTER, 0, 0);
        s_leg_mark_lbl[i] = ml;

        lv_obj_t *t = lv_label_create(parent);
        lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
        lv_obj_set_size(t, LEG_TXT_W, H_S);
        lv_obj_set_pos(t, LEG_TXT_X, row_y + (LEG_ROW_H - H_S) / 2);
        lv_obj_set_style_pad_all(t, 0, 0);
        lv_obj_set_style_text_font(t, FONT_S, 0);
        lv_obj_set_style_text_color(t, C_FG, 0);
        lv_label_set_text(t, "");
        lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
        s_leg_txt[i] = t;
    }
}

/* ---------------------------------------------------------------------------
 * Nav bar — Tab 4 ("Pi") highlighted
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_nav(lv_obj_t *parent)
{
    lv_obj_t *nav_bg = lv_obj_create(parent);
    lv_obj_set_size(nav_bg, SCR_W, NAV_H);
    lv_obj_set_pos(nav_bg, 0, NAV_Y);
    lv_obj_set_style_bg_color(nav_bg, C_INV_BG, 0);
    lv_obj_set_style_border_width(nav_bg, 0, 0);
    lv_obj_set_style_radius(nav_bg, 0, 0);
    lv_obj_set_style_pad_all(nav_bg, 0, 0);
    lv_obj_clear_flag(nav_bg, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < 4; i++) {
        BOOL_T active = (i == 3) ? TRUE : FALSE;
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
 * Refresh header
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_header(VOID_T)
{
    if (s_lbl_clock) {
        char buf[8];
        __format_clock(&s_state, buf, sizeof(buf));
        lv_label_set_text(s_lbl_clock, buf);
    }
    if (s_lbl_right) {
        const char *wifi = buddy_ws_is_connected() ? LV_SYMBOL_WIFI      : " ";
        const char *bt   = s_state.ws_connected    ? LV_SYMBOL_BLUETOOTH : " ";
        char tok[12];
        __fmt_tok(s_state.tokens, tok, sizeof(tok));
        char buf[32];
        (VOID_T)snprintf(buf, sizeof(buf), "%s %s %s", wifi, bt, tok);
        lv_label_set_text(s_lbl_right, buf);
    }
}

STATIC VOID_T __hide_all_wedges(VOID_T)
{
    for (uint32_t i = 0; i < PIE_MAX_ROWS; i++) {
        if (s_arc[i])         lv_obj_add_flag(s_arc[i],         LV_OBJ_FLAG_HIDDEN);
        if (s_wedge_num[i])   lv_obj_add_flag(s_wedge_num[i],   LV_OBJ_FLAG_HIDDEN);
        if (s_leg_mark[i])    lv_obj_add_flag(s_leg_mark[i],    LV_OBJ_FLAG_HIDDEN);
        if (s_leg_txt[i])     lv_obj_add_flag(s_leg_txt[i],     LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------------------------------------------------------------------
 * Refresh pie + legend
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_pie(VOID_T)
{
    uint8_t idx[PIE_MAX_ROWS] = {0};
    uint8_t cnt = s_state.mstats_count;
    if (cnt > PIE_MAX_ROWS) {
        cnt = PIE_MAX_ROWS;
    }
    for (uint8_t i = 0; i < cnt; i++) {
        idx[i] = i;
    }
    for (uint8_t i = 0; i < cnt; i++) {
        for (uint8_t j = (uint8_t)(i + 1); j < cnt; j++) {
            if (s_state.mstats[idx[j]].tokens >
                s_state.mstats[idx[i]].tokens) {
                uint8_t tmp = idx[i];
                idx[i] = idx[j];
                idx[j] = tmp;
            }
        }
    }

    if (cnt == 0) {
        if (s_empty_lbl) {
            lv_obj_clear_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        }
        __hide_all_wedges();
        return;
    }
    if (s_empty_lbl) {
        lv_obj_add_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
    }

    uint32_t total_tok = 0U;
    for (uint8_t i = 0; i < cnt; i++) {
        total_tok += s_state.mstats[idx[i]].tokens;
    }
    if (total_tok == 0U) {
        if (s_empty_lbl) {
            lv_obj_clear_flag(s_empty_lbl, LV_OBJ_FLAG_HIDDEN);
        }
        __hide_all_wedges();
        return;
    }

    int32_t total_gap_deg = (cnt > 1) ? (int32_t)(WEDGE_GAP_DEG * cnt) : 0;
    int32_t avail_deg     = 360 - total_gap_deg;
    if (avail_deg < 0) {
        avail_deg = 0;
    }

    int32_t cursor_deg = 0;
    for (uint32_t row = 0; row < PIE_MAX_ROWS; row++) {
        if (row >= (uint32_t)cnt) {
            if (s_arc[row])       lv_obj_add_flag(s_arc[row],       LV_OBJ_FLAG_HIDDEN);
            if (s_wedge_num[row]) lv_obj_add_flag(s_wedge_num[row], LV_OBJ_FLAG_HIDDEN);
            if (s_leg_mark[row])  lv_obj_add_flag(s_leg_mark[row],  LV_OBJ_FLAG_HIDDEN);
            if (s_leg_txt[row])   lv_obj_add_flag(s_leg_txt[row],   LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const buddy_mstat_t *ms = &s_state.mstats[idx[row]];

        int32_t span_deg;
        if (row + 1U == (uint32_t)cnt) {
            span_deg = (avail_deg) - cursor_deg + (int32_t)(row * WEDGE_GAP_DEG);
            if (span_deg < 1) {
                span_deg = 1;
            }
        } else {
            span_deg = (int32_t)(((uint64_t)ms->tokens * (uint32_t)avail_deg)
                                 / total_tok);
            if (span_deg < 1) {
                span_deg = 1;
            }
        }

        int32_t start_deg = cursor_deg;
        int32_t end_deg   = cursor_deg + span_deg;
        if (end_deg > 360) {
            end_deg = 360;
        }

        if (s_arc[row]) {
            uint32_t s_a = (uint32_t)start_deg;
            uint32_t e_a = (uint32_t)((cnt == 1U) ? 359 : end_deg);
            lv_arc_set_angles(s_arc[row], s_a, e_a);
            lv_obj_clear_flag(s_arc[row], LV_OBJ_FLAG_HIDDEN);
        }

        int32_t mid_deg = (start_deg + end_deg) / 2;
        int32_t r_lbl  = (int32_t)PIE_R * (int32_t)LABEL_R_NUM
                         / (int32_t)LABEL_R_DEN;
        int32_t sin_v  = (int32_t)lv_trigo_sin((int16_t)mid_deg);
        int32_t cos_v  = (int32_t)lv_trigo_sin((int16_t)(mid_deg + 90));
        int32_t lx     = (int32_t)PIE_CX + (r_lbl * sin_v) / 32768;
        int32_t ly     = (int32_t)PIE_CY - (r_lbl * cos_v) / 32768;
        if (s_wedge_num[row]) {
            char nbuf[4];
            (VOID_T)snprintf(nbuf, sizeof(nbuf), "%u", (unsigned)(row + 1U));
            lv_label_set_text(s_wedge_num[row], nbuf);
            lv_obj_set_pos(s_wedge_num[row], lx - 3, ly - 7);
            lv_obj_clear_flag(s_wedge_num[row], LV_OBJ_FLAG_HIDDEN);
        }

        if (s_leg_mark[row]) {
            lv_obj_clear_flag(s_leg_mark[row], LV_OBJ_FLAG_HIDDEN);
        }
        if (s_leg_mark_lbl[row]) {
            char nbuf[4];
            (VOID_T)snprintf(nbuf, sizeof(nbuf), "%u", (unsigned)(row + 1U));
            lv_label_set_text(s_leg_mark_lbl[row], nbuf);
        }
        if (s_leg_txt[row]) {
            uint32_t pct = (uint32_t)(((uint64_t)ms->tokens * 100U)
                                       / total_tok);
            if (pct > 100U) {
                pct = 100U;
            }
            char tok[12];
            __fmt_tok(ms->tokens, tok, sizeof(tok));
            char buf[64];
            (VOID_T)snprintf(buf, sizeof(buf), "%-12.12s %s  %u%%",
                             ms->model[0] ? ms->model : "unknown",
                             tok, (unsigned)pct);
            lv_label_set_text(s_leg_txt[row], buf);
            lv_obj_clear_flag(s_leg_txt[row], LV_OBJ_FLAG_HIDDEN);
        }

        cursor_deg = end_deg;
        if (row + 1U < (uint32_t)cnt) {
            cursor_deg += WEDGE_GAP_DEG;
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
    case KEY_LEFT:
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
    ui_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(ui_screen, C_BG, 0);
    lv_obj_set_style_pad_all(ui_screen, 0, 0);
    lv_obj_clear_flag(ui_screen, LV_OBJ_FLAG_SCROLLABLE);

    buddy_state_snapshot(&s_state);
    (VOID_T)buddy_ws_send_hb_req("pie");

    __build_header(ui_screen);
    __build_title(ui_screen);
    __build_pie(ui_screen);
    __build_legend(ui_screen);
    __build_nav(ui_screen);

    __refresh_header();
    __refresh_pie();

    lv_obj_add_event_cb(ui_screen, __key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_screen);
    lv_group_focus_obj(ui_screen);

    PR_NOTICE("[%s] init mstats_count=%u", buddy_pie_screen.name,
              (unsigned)s_state.mstats_count);
}

STATIC VOID_T __deinit(VOID_T)
{
    if (ui_screen) {
        lv_obj_remove_event_cb(ui_screen, __key_cb);
        lv_group_remove_obj(ui_screen);
    }

    s_lbl_left   = NULL;
    s_lbl_clock  = NULL;
    s_lbl_right  = NULL;
    s_title_lbl  = NULL;
    s_empty_lbl  = NULL;

    for (uint32_t i = 0; i < PIE_MAX_ROWS; i++) {
        s_arc[i]          = NULL;
        s_wedge_num[i]    = NULL;
        s_leg_mark[i]     = NULL;
        s_leg_mark_lbl[i] = NULL;
        s_leg_txt[i]      = NULL;
    }
}
