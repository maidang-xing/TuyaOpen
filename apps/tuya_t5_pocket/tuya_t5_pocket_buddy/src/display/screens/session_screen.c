/**
 * @file session_screen.c
 * @brief Claude Desktop Buddy session detail screen.
 *
 * Layout (384x168, no footer):
 *   Header   0-20    black bg, white text: "claude buddy" / clock / status
 *   Body    20-168   left panel (148px) | 2px divider | right panel (232px)
 *
 * Left panel (x=0..148, y=20..168):
 *   y=20-36  "Name" inverted header row
 *   y=38     session name (truncated to 16 chars)
 *   y=54     "ID: " + first 8 chars of sid
 *   y=70     "Model: " + model (abbreviated)
 *   y=86     "Out: " + formatted token count
 *   y=102    "Running: " + yes/no
 *   y=118    "Project: " + project name
 *
 * Right panel (x=152..384, y=20..168):
 *   y=20-36  "Session Log" inverted title row
 *   y=38..   local_entries[], 14px per row, up to 4 entries
 *            "No entries yet" if empty
 *
 * State passing: caller sets buddy_session_screen.state_data to a
 * const buddy_session_t * before screen_load().
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "session_screen.h"
#include "buddy_protocol.h"
#include "buddy_transport.h"
#include "buddy_types.h"
#include "buddy_cjk_font.h"
#include "status_bar.h"
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
#define SCR_W       AI_PET_SCREEN_WIDTH    /* 384 */
#define SCR_H       AI_PET_SCREEN_HEIGHT   /* 168 */
#define HEADER_H    20
#define BODY_TOP    HEADER_H               /* 20  */
#define BODY_H      (SCR_H - HEADER_H)    /* 148 */

/* Left panel */
#define LEFT_W      148
#define LEFT_PAD    2

/* Divider */
#define DIV_X       LEFT_W                 /* 148 */
#define DIV_W       2

/* Right panel */
#define RIGHT_X     (LEFT_W + DIV_W)       /* 150 */
#define RIGHT_W     (SCR_W - RIGHT_X)      /* 234 */
#define RIGHT_PAD   3

/* Row heights (must match font metrics) */
#define H_M         16
#define H_S         14

/* Inverted header row sits at BODY_TOP and is 16px tall */
#define INV_HDR_H   16

/* First content row starts immediately after the inverted header */
#define LEFT_CONTENT_Y  (BODY_TOP + INV_HDR_H + 2)  /* 38 */
#define LEFT_ROW_H      16   /* row stride: 14px glyph + 2px gap */

/* Right panel log entries */
#define LOG_ENTRY_Y0   (BODY_TOP + INV_HDR_H + 2)   /* 38 */
#define LOG_ENTRY_H    14                             /* FONT_S height */
#define LOG_VISIBLE    4                              /* BUDDY_SESSION_LOCAL_ENTRIES */

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_screen = NULL;

/* Header */
STATIC lv_obj_t *lbl_clock;
STATIC lv_obj_t *lbl_status;

/* Left panel content */
STATIC lv_obj_t *lbl_name_hdr;     /* inverted "Name" header */
STATIC lv_obj_t *lbl_sess_name;
STATIC lv_obj_t *lbl_sid;
STATIC lv_obj_t *lbl_model;
STATIC lv_obj_t *lbl_tokens;
STATIC lv_obj_t *lbl_running;
STATIC lv_obj_t *lbl_project;
STATIC lv_obj_t *lbl_ctx;          /* "Ctx: used/total" — global state */

/* Right panel content */
STATIC lv_obj_t *lbl_log_hdr;      /* inverted "Session Log" header */
STATIC lv_obj_t *lbl_log_empty;
STATIC lv_obj_t *lbl_log[LOG_VISIBLE];

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
STATIC buddy_tama_state_t s_state   = {0};
STATIC uint8_t            s_sess_idx = 0;  /* index into s_state.sessions[] */
STATIC uint8_t            s_log_scroll = 0;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __init(VOID_T);
STATIC VOID_T __deinit(VOID_T);
STATIC VOID_T __key_cb(lv_event_t *e);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_left_panel(lv_obj_t *parent);
STATIC VOID_T __build_right_panel(lv_obj_t *parent);
STATIC VOID_T __refresh_header(VOID_T);
STATIC VOID_T __refresh_left(VOID_T);
STATIC VOID_T __refresh_right(VOID_T);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n);
STATIC lv_obj_t *__lbl(lv_obj_t *parent, int32_t x, int32_t y,
                        int32_t w, const lv_font_t *font);
STATIC lv_obj_t *__make_inv_hdr(lv_obj_t *parent, int32_t x, int32_t y,
                                 int32_t w, const char *text);

Screen_t buddy_session_screen = {
    .init       = __init,
    .deinit     = __deinit,
    .screen_obj = &ui_screen,
    .name       = "buddy_session_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Utilities
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *__lbl(lv_obj_t *parent, int32_t x, int32_t y,
                        int32_t w, const lv_font_t *font)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    int32_t h = (font == FONT_M) ? H_M : H_S;
    lv_obj_set_size(l, (w > 0) ? w : LV_SIZE_CONTENT, h);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, C_FG, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

STATIC lv_obj_t *__make_inv_hdr(lv_obj_t *parent, int32_t x, int32_t y,
                                 int32_t w, const char *text)
{
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_set_size(bg, w, INV_HDR_H);
    lv_obj_set_pos(bg, x, y);
    lv_obj_set_style_bg_color(bg, C_INV_BG, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(bg);
    lv_obj_set_style_text_font(lbl, FONT_S, 0);
    lv_obj_set_style_text_color(lbl, C_INV_FG, 0);
    lv_obj_set_style_pad_all(lbl, 0, 0);
    lv_obj_set_pos(lbl, 3, 1);
    lv_label_set_text(lbl, text);
    return lbl;
}

STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n)
{
    if (!out || n < 6) return;
    if (!s || !s->wall_epoch_s) {
        (VOID_T)snprintf(out, n, "--:--");
        return;
    }
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
                         (unsigned)(v / 1000),
                         (unsigned)((v % 1000) / 100));
    else
        (VOID_T)snprintf(out, n, "%u", (unsigned)v);
}

/* ---------------------------------------------------------------------------
 * Build helpers
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

    lv_obj_t *lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "claude buddy");
    lv_obj_set_style_text_font(lbl_title, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_title, C_INV_FG, 0);
    lv_obj_set_style_pad_all(lbl_title, 0, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 4, 0);

    lbl_clock = lv_label_create(bar);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_clock, C_INV_FG, 0);
    lv_obj_set_style_pad_all(lbl_clock, 0, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_CENTER, 0, 0);

    lbl_status = lv_label_create(bar);
    lv_label_set_text(lbl_status, "W B --");
    lv_obj_set_style_text_font(lbl_status, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_status, C_INV_FG, 0);
    lv_obj_set_style_pad_all(lbl_status, 0, 0);
    lv_obj_align(lbl_status, LV_ALIGN_RIGHT_MID, -4, 0);
}

STATIC VOID_T __build_left_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, LEFT_W, BODY_H);
    lv_obj_set_pos(panel, 0, BODY_TOP);
    lv_obj_set_style_bg_color(panel, C_BG, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lbl_name_hdr = __make_inv_hdr(panel, 0, 0, LEFT_W, "Name");

    int32_t y = INV_HDR_H + 2;
    int32_t cw = LEFT_W - LEFT_PAD * 2;

    lbl_sess_name = __lbl(panel, LEFT_PAD, y, cw, FONT_S);  y += LEFT_ROW_H;
    lbl_sid       = __lbl(panel, LEFT_PAD, y, cw, FONT_S);  y += LEFT_ROW_H;
    lbl_model     = __lbl(panel, LEFT_PAD, y, cw, FONT_S);  y += LEFT_ROW_H;
    lbl_tokens    = __lbl(panel, LEFT_PAD, y, cw, FONT_S);  y += LEFT_ROW_H;
    lbl_running   = __lbl(panel, LEFT_PAD, y, cw, FONT_S);  y += LEFT_ROW_H;
    lbl_project   = __lbl(panel, LEFT_PAD, y, cw, FONT_S);  y += LEFT_ROW_H;
    lbl_ctx       = __lbl(panel, LEFT_PAD, y, cw, FONT_S);
}

STATIC VOID_T __build_right_panel(lv_obj_t *parent)
{
    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, RIGHT_W, BODY_H);
    lv_obj_set_pos(panel, RIGHT_X, BODY_TOP);
    lv_obj_set_style_bg_color(panel, C_BG, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lbl_log_hdr = __make_inv_hdr(panel, 0, 0, RIGHT_W, "Session Log");

    int32_t cw = RIGHT_W - RIGHT_PAD * 2;
    for (uint32_t i = 0; i < LOG_VISIBLE; i++) {
        int32_t ey = (int32_t)(INV_HDR_H + 2 + i * (LOG_ENTRY_H + 1));
        lbl_log[i] = __lbl(panel, RIGHT_PAD, ey, cw, FONT_S);
        lv_obj_add_flag(lbl_log[i], LV_OBJ_FLAG_HIDDEN);
    }

    lbl_log_empty = __lbl(panel, RIGHT_PAD,
                          INV_HDR_H + 2, cw, FONT_S);
    lv_label_set_text(lbl_log_empty, "No entries yet");
    lv_obj_add_flag(lbl_log_empty, LV_OBJ_FLAG_HIDDEN);
}

/* ---------------------------------------------------------------------------
 * Refresh helpers
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_header(VOID_T)
{
    if (lbl_clock) {
        char buf[6];
        __format_clock(&s_state, buf, sizeof(buf));
        lv_label_set_text(lbl_clock, buf);
    }
    if (lbl_status) {
        char buf[24];
        buddy_status_bar_format(buf, sizeof(buf),
                                buddy_ws_is_connected(),
                                s_state.ws_connected,
                                BUDDY_BAT_PCT_UNKNOWN);
        lv_label_set_text(lbl_status, buf);
    }
}

STATIC VOID_T __refresh_left(VOID_T)
{
    if (s_sess_idx >= s_state.sessions_count) return;
    const buddy_session_t *sess = &s_state.sessions[s_sess_idx];

    if (lbl_sess_name) {
        char name_buf[20];
        if (strlen(sess->name) > 16) {
            (VOID_T)snprintf(name_buf, sizeof(name_buf), "%.13s...", sess->name);
        } else {
            (VOID_T)snprintf(name_buf, sizeof(name_buf), "%s",
                             sess->name[0] ? sess->name : "(unnamed)");
        }
        lv_label_set_text(lbl_sess_name, name_buf);
    }

    if (lbl_sid) {
        char id_buf[20];
        (VOID_T)snprintf(id_buf, sizeof(id_buf), "ID: %.8s", sess->sid);
        lv_label_set_text(lbl_sid, id_buf);
    }

    if (lbl_model) {
        char mod_buf[24];
        (VOID_T)snprintf(mod_buf, sizeof(mod_buf), "Model: %.11s",
                         sess->model[0] ? sess->model : "?");
        lv_label_set_text(lbl_model, mod_buf);
    }

    if (lbl_tokens) {
        char tok[10];
        char out_buf[20];
        __fmt_tok(sess->tokens, tok, sizeof(tok));
        (VOID_T)snprintf(out_buf, sizeof(out_buf), "Out: %s", tok);
        lv_label_set_text(lbl_tokens, out_buf);
    }

    if (lbl_running) {
        char run_buf[16];
        (VOID_T)snprintf(run_buf, sizeof(run_buf),
                         "Running: %s", sess->is_running ? "yes" : "no");
        lv_label_set_text(lbl_running, run_buf);
    }

    if (lbl_project) {
        char proj_buf[36];
        (VOID_T)snprintf(proj_buf, sizeof(proj_buf), "Project: %.18s",
                         sess->project[0] ? sess->project : "-");
        lv_label_set_text(lbl_project, proj_buf);
    }

    if (lbl_ctx) {
        char ctx_buf[28];
        char used_s[10];
        char tot_s[10];
        __fmt_tok(s_state.ctx_used,  used_s, sizeof(used_s));
        __fmt_tok(s_state.ctx_total, tot_s,  sizeof(tot_s));
        if (s_state.ctx_total > 0) {
            (VOID_T)snprintf(ctx_buf, sizeof(ctx_buf),
                             "Ctx: %s/%s", used_s, tot_s);
        } else {
            (VOID_T)snprintf(ctx_buf, sizeof(ctx_buf), "Ctx: -");
        }
        lv_label_set_text(lbl_ctx, ctx_buf);
    }
}

STATIC VOID_T __refresh_right(VOID_T)
{
    if (s_sess_idx >= s_state.sessions_count) {
        for (uint32_t i = 0; i < LOG_VISIBLE; i++) {
            if (lbl_log[i]) lv_obj_add_flag(lbl_log[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (lbl_log_empty) lv_obj_clear_flag(lbl_log_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    const buddy_session_t *sess = &s_state.sessions[s_sess_idx];
    uint8_t count = sess->local_entries_count;

    if (count == 0) {
        for (uint32_t i = 0; i < LOG_VISIBLE; i++) {
            if (lbl_log[i]) lv_obj_add_flag(lbl_log[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (lbl_log_empty) lv_obj_clear_flag(lbl_log_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (lbl_log_empty) lv_obj_add_flag(lbl_log_empty, LV_OBJ_FLAG_HIDDEN);

    if (s_log_scroll >= count) s_log_scroll = (uint8_t)(count - 1U);

    for (uint32_t i = 0; i < LOG_VISIBLE; i++) {
        if (!lbl_log[i]) continue;
        uint8_t entry_idx = (uint8_t)(s_log_scroll + i);
        if (entry_idx >= count) {
            lv_obj_add_flag(lbl_log[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        lv_label_set_text(lbl_log[i],
                          sess->local_entries[entry_idx]);
        lv_obj_clear_flag(lbl_log[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------------------------------------------------------------------
 * Key handler
 * --------------------------------------------------------------------------- */
STATIC VOID_T __key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    const buddy_session_t *sess = (s_sess_idx < s_state.sessions_count)
                                  ? &s_state.sessions[s_sess_idx] : NULL;

    switch (key) {
    case KEY_ESC:
    case KEY_LEFT:
        screen_back();
        break;

    case KEY_UP:
        if (sess) {
            uint8_t count = sess->local_entries_count;
            if (count > 0 &&
                (uint32_t)s_log_scroll + LOG_VISIBLE < (uint32_t)count) {
                s_log_scroll++;
                lv_vendor_disp_lock();
                __refresh_right();
                lv_vendor_disp_unlock();
            }
        }
        break;

    case KEY_DOWN:
        if (s_log_scroll > 0) {
            s_log_scroll--;
            lv_vendor_disp_lock();
            __refresh_right();
            lv_vendor_disp_unlock();
        }
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
    const buddy_session_t *requested_sess =
        (const buddy_session_t *)buddy_session_screen.state_data;

    buddy_state_snapshot(&s_state);
    (VOID_T)buddy_ws_send_hb_req("session");

    s_sess_idx = 0;
    s_log_scroll = 0;
    if (requested_sess && requested_sess->sid[0]) {
        for (uint8_t i = 0; i < s_state.sessions_count; i++) {
            if (strncmp(s_state.sessions[i].sid,
                        requested_sess->sid,
                        sizeof(s_state.sessions[i].sid) - 1U) == 0) {
                s_sess_idx = i;
                break;
            }
        }
    }

    ui_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(ui_screen, C_BG, 0);
    lv_obj_set_style_pad_all(ui_screen, 0, 0);
    lv_obj_clear_flag(ui_screen, LV_OBJ_FLAG_SCROLLABLE);

    __build_header(ui_screen);
    __build_left_panel(ui_screen);

    lv_obj_t *div = lv_obj_create(ui_screen);
    lv_obj_set_size(div, DIV_W, BODY_H);
    lv_obj_set_pos(div, DIV_X, BODY_TOP);
    lv_obj_set_style_bg_color(div, C_FG, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);

    __build_right_panel(ui_screen);

    __refresh_header();
    __refresh_left();
    __refresh_right();

    lv_obj_add_event_cb(ui_screen, __key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_screen);
    lv_group_focus_obj(ui_screen);

    PR_NOTICE("[%s] init sess_idx=%u sid=%.8s",
              buddy_session_screen.name,
              (unsigned)s_sess_idx,
              (s_sess_idx < s_state.sessions_count)
                  ? s_state.sessions[s_sess_idx].sid : "?");
}

STATIC VOID_T __deinit(VOID_T)
{
    if (ui_screen) {
        lv_obj_remove_event_cb(ui_screen, __key_cb);
        lv_group_remove_obj(ui_screen);
    }

    lbl_clock = NULL;
    lbl_status = NULL;
    lbl_name_hdr = NULL;
    lbl_sess_name = NULL;
    lbl_sid = NULL;
    lbl_model = NULL;
    lbl_tokens = NULL;
    lbl_running = NULL;
    lbl_project = NULL;
    lbl_ctx = NULL;
    lbl_log_hdr = NULL;
    lbl_log_empty = NULL;
    for (uint32_t i = 0; i < LOG_VISIBLE; i++) lbl_log[i] = NULL;

    s_sess_idx = 0;
    s_log_scroll = 0;
}
