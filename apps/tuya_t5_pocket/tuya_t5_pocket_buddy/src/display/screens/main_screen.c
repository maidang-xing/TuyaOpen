/**
 * @file main_screen.c
 * @brief Claude Desktop Buddy main screen (M7-UI / UI-3.0).
 *
 * Layout (384x168, with bottom nav bar):
 *   Header   20px  black bg white text: claude buddy / clock / W B batt%
 *   Body    142px  left-right split:
 *     Left 144px:
 *       - persona animation (144x110 canvas, y=20~130)
 *       - name+level area (y=130~162, 32px)
 *     Divider  2px  black separator
 *     Right 238px: project+session list (scrollable)
 *   NavBar    6px  bottom nav bar: St|Hm|Ch|Pi (Hm highlighted)
 *
 * Navigation: LEFT -> status_screen, RIGHT -> chart_screen,
 *             ENTER (selected session) -> session_screen,
 *             UP/DOWN scroll session list, JOYCON cycle persona.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "main_screen.h"
#include "approval_screen.h"
#include "status_screen.h"
#include "chart_screen.h"
#include "session_screen.h"
#include "buddy_protocol.h"
#include "buddy_transport.h"
#include "buddy_types.h"
#include "led_indicator.h"
// buddy_gif_stub not used in WS version
#include "ascii_persona.h"
#include "persona_registry.h"
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
#define FONT_L   (&buddy_font_l)
#define FONT_M   (&buddy_font_m)
#define FONT_S   (&buddy_font_s)

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
#define NAV_H       6                       /* bottom nav bar height */
#define NAV_Y       (SCR_H - NAV_H)         /* 162 */
#define BODY_H      (NAV_Y - HEADER_H)     /* 142 */

/* Left column: persona canvas + name/level strip */
#define PERSONA_W        144               /* = ASCII_CANVAS_W */
#define PERSONA_CANVAS_H 110               /* = ASCII_CANVAS_H */
#define PERSONA_STRIP_Y  (BODY_TOP + PERSONA_CANVAS_H)    /* 130 */
#define PERSONA_STRIP_H  (NAV_Y - PERSONA_STRIP_Y)        /* 32  */

/* Vertical divider */
#define DIV_X   PERSONA_W                  /* 144 */
#define DIV_W   2

/* Right info panel */
#define INFO_X      (PERSONA_W + DIV_W)    /* 146 */
#define INFO_Y      BODY_TOP               /* 20  */
#define INFO_W      (SCR_W - INFO_X)       /* 238 */
#define INFO_H      BODY_H                 /* 142 */
#define INFO_PAD    3

/* Font heights */
#define H_L  18
#define H_M  16
#define H_S  14

/* Row gap (px between rows) */
#define ROW_GAP  2

/* Row height for session list — raised to 18 so that project headers
 * (FONT_L, dev_4_27 UI update) fit without overlap. Cost: visible
 * row count drops from 8 to 7 (8*18=144 > BODY 142px). */
#define ROW_H   (H_L)            /* 18 */

/* Level calculation */
#define TOKENS_PER_LEVEL  50000U
#define LEVEL_BAR_BLOCKS  8

/* Session list */
#define LIST_VISIBLE_MAX  7     /* max visible rows in session panel (7*18=126 ≤ 136 inner h) */
#define FLAT_MAX          20    /* max flat items (proj headers + sessions, up to 12+8 projects) */

/* Running dot */
#define DOT_SIZE  7             /* 7×7 filled circle at the rightmost edge of session row */

/* Blinking dot interval (persona_tick × BLINK_TICKS × 100ms) */
#define BLINK_TICKS  5          /* 500ms blink period */

/* Timers */
#define PERSONA_TICK_MS    100U
#define CELEBRATE_HOLD_MS  3000U
#define HEART_HOLD_MS      2000U
#define KV_KEY_PERSONA_ID  "buddy.pid"

/* Nav bar tab indices */
#define NAV_ST  0
#define NAV_HM  1
#define NAV_CH  2
#define NAV_PI  3
#define NAV_SECT_W  (SCR_W / 4)   /* 96px */

/* ---------------------------------------------------------------------------
 * Flat item for session list
 * --------------------------------------------------------------------------- */
typedef struct {
    BOOL_T  is_header;
    uint8_t sess_idx;
    char    proj_name[BUDDY_SESSION_PROJECT_LEN + 1]; /* valid for headers */
} flat_item_t;

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_buddy_main_screen = NULL;

/* Header widgets */
STATIC lv_obj_t *lbl_title;        /* "claude buddy" */
STATIC lv_obj_t *lbl_clock;        /* HH:MM */
STATIC lv_obj_t *lbl_conn;         /* "W B 87%" */

/* Persona name + level strip */
STATIC lv_obj_t *lbl_persona_name;
STATIC lv_obj_t *lbl_persona_level;

/* Session list rows (right panel) */
STATIC lv_obj_t *s_list_rows[LIST_VISIBLE_MAX];

/* Nav bar */
STATIC lv_obj_t *s_nav_sects[4];

/* Running dot indicators — one per visible row */
STATIC lv_obj_t *s_dot_objs[LIST_VISIBLE_MAX];

/* "No sessions" placeholder */
STATIC lv_obj_t *s_lbl_empty;

/* ---------------------------------------------------------------------------
 * State
 * --------------------------------------------------------------------------- */
STATIC buddy_tama_state_t    s_state         = {0};
STATIC uint8_t               s_persona_id    = 0;
STATIC buddy_persona_state_e s_persona_state = BUDDY_PERSONA_STATE_SLEEP;
STATIC buddy_led_state_e     s_led_state     = BUDDY_LED_STATE_OFF;

STATIC uint64_t s_celebrate_until_ms = 0;
STATIC uint64_t s_heart_until_ms     = 0;
STATIC BOOL_T   s_prev_prompt        = FALSE;
STATIC BOOL_T   s_prev_completed     = FALSE;

STATIC lv_timer_t *s_persona_timer = NULL;

/* Session list state */
STATIC flat_item_t s_flat[FLAT_MAX];
STATIC uint8_t     s_flat_count  = 0;
STATIC uint8_t     s_scroll_top  = 0;   /* first visible flat item */
STATIC uint8_t     s_sel_flat    = 0;   /* selected flat item (session only) */
STATIC uint8_t     s_blink_tick  = 0;
STATIC BOOL_T      s_dot_visible = TRUE;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __init(VOID_T);
STATIC VOID_T __deinit(VOID_T);
STATIC VOID_T __key_cb(lv_event_t *e);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_persona_strip(lv_obj_t *parent);
STATIC VOID_T __build_sessions_panel(lv_obj_t *parent);
STATIC VOID_T __build_nav_bar(lv_obj_t *parent);
STATIC VOID_T __build_flat_list(VOID_T);
STATIC VOID_T __refresh(VOID_T);
STATIC VOID_T __refresh_header(VOID_T);
STATIC VOID_T __refresh_persona_strip(VOID_T);
STATIC VOID_T __refresh_sessions(VOID_T);
STATIC VOID_T __refresh_nav_bar(VOID_T);
STATIC VOID_T __persona_tick_cb(lv_timer_t *t);
STATIC VOID_T __persona_cycle(int8_t delta);
STATIC uint8_t __load_persona_id(VOID_T);
STATIC VOID_T __persist_persona_id(uint8_t id);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
// STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n);
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
    PR_DEBUG("[main_screen] update_state: ws=%d sess=%d run=%d tok=%u prompt=%d scr=%p",
             (int)state->ws_connected, (int)state->sessions_count,
             (int)state->sessions_running, (unsigned)state->tokens,
             (int)state->has_prompt, (void *)ui_buddy_main_screen);
    lv_vendor_disp_lock();
    const BOOL_T was_prompt = s_state.has_prompt ? TRUE : FALSE;
    s_state = *state;
    if (ui_buddy_main_screen) {
        if (s_state.has_prompt && !was_prompt) {
            s_led_state = BUDDY_LED_STATE_BLINK_FAST;
            (VOID_T)buddy_led_set(BUDDY_LED_STATE_BLINK_FAST);
            lv_vendor_disp_unlock();
            screen_load(&buddy_approval_screen);
            return;
        }
        if (s_state.has_prompt) {
            lv_vendor_disp_unlock();
            return;
        }
        __refresh();
    }
    lv_vendor_disp_unlock();
}

size_t buddy_main_screen_get_selected_sid(char *out_sid, size_t cap)
{
    if (out_sid == NULL || cap < 12) {
        return 0;
    }
    out_sid[0] = '\0';
    size_t n = 0;
    lv_vendor_disp_lock();
    if (s_flat_count > 0 && s_sel_flat < s_flat_count &&
        !s_flat[s_sel_flat].is_header) {
        uint8_t si = s_flat[s_sel_flat].sess_idx;
        if (si < s_state.sessions_count) {
            const char *sid = s_state.sessions[si].sid;
            (VOID_T)snprintf(out_sid, cap, "%s", sid);
            n = strlen(out_sid);
        }
    }
    lv_vendor_disp_unlock();
    return n;
}

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

// STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n)
// {
//     if (v >= 1000U)
//         (VOID_T)snprintf(out, n, "%u.%uk",
//                          (unsigned)(v / 1000), (unsigned)((v % 1000) / 100));
//     else
//         (VOID_T)snprintf(out, n, "%u", (unsigned)v);
// }

/* ---------------------------------------------------------------------------
 * Header (claude buddy | time | W B bat%)
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
    lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "claude buddy");
    lv_obj_set_style_text_font(lbl_title, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_title, C_INV_FG, 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 4, 0);

    /* Center: clock */
    lbl_clock = lv_label_create(bar);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_clock, C_INV_FG, 0);
    lv_obj_align(lbl_clock, LV_ALIGN_CENTER, 0, 0);

    /* Right: W B bat% */
    lbl_conn = lv_label_create(bar);
    lv_label_set_text(lbl_conn, "W b --");
    lv_obj_set_style_text_font(lbl_conn, FONT_S, 0);
    lv_obj_set_style_text_color(lbl_conn, C_INV_FG, 0);
    lv_obj_align(lbl_conn, LV_ALIGN_RIGHT_MID, -4, 0);
}

/* ---------------------------------------------------------------------------
 * Persona name + level strip (y=130~162)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_persona_strip(lv_obj_t *parent)
{
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
 * Sessions panel (right side: INFO_X..SCR_W, y=INFO_Y..NAV_Y)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_sessions_panel(lv_obj_t *parent)
{
    /* Container */
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, INFO_W, INFO_H);
    lv_obj_set_pos(cont, INFO_X, INFO_Y);
    lv_obj_set_style_bg_color(cont, C_BG, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_radius(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, INFO_PAD, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    const int32_t W = INFO_W - INFO_PAD * 2;

    /* Pre-allocate row labels */
    for (uint32_t i = 0; i < LIST_VISIBLE_MAX; i++) {
        s_list_rows[i] = __lbl(cont, 0, (int32_t)(i * ROW_H), W, FONT_S);
        lv_label_set_text(s_list_rows[i], "");
        lv_obj_add_flag(s_list_rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* Pre-allocate running-dot objects (one per row) */
    for (uint32_t i = 0; i < LIST_VISIBLE_MAX; i++) {
        s_dot_objs[i] = lv_obj_create(cont);
        lv_obj_set_size(s_dot_objs[i], DOT_SIZE, DOT_SIZE);
        lv_obj_set_style_bg_color(s_dot_objs[i], C_FG, 0);
        lv_obj_set_style_border_width(s_dot_objs[i], 0, 0);
        lv_obj_set_style_radius(s_dot_objs[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_pad_all(s_dot_objs[i], 0, 0);
        lv_obj_set_pos(s_dot_objs[i], W - DOT_SIZE,
                       (int32_t)(i * ROW_H) + (ROW_H - DOT_SIZE) / 2);
        lv_obj_add_flag(s_dot_objs[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* "No sessions" placeholder */
    s_lbl_empty = __lbl(cont, 0, 4, W, FONT_S);
    lv_label_set_text(s_lbl_empty, "No sessions");

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
 * Bottom nav bar — 4 pure colored segments (no text labels)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_nav_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, NAV_H);
    lv_obj_set_pos(bar, 0, NAV_Y);
    lv_obj_set_style_bg_color(bar, C_INV_BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < 4; i++) {
        s_nav_sects[i] = lv_obj_create(bar);
        lv_obj_set_size(s_nav_sects[i], NAV_SECT_W, NAV_H);
        lv_obj_set_pos(s_nav_sects[i], (int32_t)(i * NAV_SECT_W), 0);
        lv_obj_set_style_border_width(s_nav_sects[i], 0, 0);
        lv_obj_set_style_radius(s_nav_sects[i], 0, 0);
        lv_obj_set_style_pad_all(s_nav_sects[i], 0, 0);
        lv_obj_clear_flag(s_nav_sects[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    __refresh_nav_bar();
}

/* ---------------------------------------------------------------------------
 * Flat list builder (groups sessions by project)
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_flat_list(VOID_T)
{
    s_flat_count = 0;

    /* Collect unique project names */
    char seen[BUDDY_SESSIONS_MAX][BUDDY_SESSION_PROJECT_LEN + 1];
    uint8_t np = 0;
    for (uint8_t i = 0; i < s_state.sessions_count && i < BUDDY_SESSIONS_MAX; i++) {
        const char *p = s_state.sessions[i].project[0]
                        ? s_state.sessions[i].project : "Other";
        BOOL_T found = FALSE;
        for (uint8_t j = 0; j < np; j++) {
            if (strncmp(seen[j], p, BUDDY_SESSION_PROJECT_LEN) == 0) {
                found = TRUE; break;
            }
        }
        if (!found && np < BUDDY_SESSIONS_MAX) {
            (VOID_T)snprintf(seen[np], sizeof(seen[0]), "%s", p);
            np++;
        }
    }

    /* Build flat list: project header followed by its sessions */
    for (uint8_t pi = 0; pi < np && s_flat_count < FLAT_MAX; pi++) {
        s_flat[s_flat_count].is_header = TRUE;
        (VOID_T)snprintf(s_flat[s_flat_count].proj_name,
                         sizeof(s_flat[0].proj_name), "%s", seen[pi]);
        s_flat[s_flat_count].sess_idx = 0;
        s_flat_count++;

        for (uint8_t si = 0; si < s_state.sessions_count && s_flat_count < FLAT_MAX; si++) {
            const char *sp = s_state.sessions[si].project[0]
                             ? s_state.sessions[si].project : "Other";
            if (strncmp(sp, seen[pi], BUDDY_SESSION_PROJECT_LEN) == 0) {
                s_flat[s_flat_count].is_header = FALSE;
                s_flat[s_flat_count].sess_idx  = si;
                s_flat[s_flat_count].proj_name[0] = '\0';
                s_flat_count++;
            }
        }
    }

    /* Clamp selection; then advance to first session item if pointing at a header */
    if (s_sel_flat >= s_flat_count) s_sel_flat = 0;
    if (s_scroll_top >= s_flat_count) s_scroll_top = 0;
    /* If selected item is a project header, advance to the first session below it */
    if (s_flat_count > 0 && s_flat[s_sel_flat].is_header) {
        for (uint8_t i = s_sel_flat + 1; i < s_flat_count; i++) {
            if (!s_flat[i].is_header) { s_sel_flat = i; break; }
        }
    }
}

/* ---------------------------------------------------------------------------
 * Nav bar refresh — active tab gets white fill, others black
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_nav_bar(VOID_T)
{
    for (uint32_t i = 0; i < 4; i++) {
        if (!s_nav_sects[i]) continue;
        if (i == NAV_HM) {
            lv_obj_set_style_bg_color(s_nav_sects[i], C_INV_FG, 0);
            lv_obj_set_style_bg_opa(s_nav_sects[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(s_nav_sects[i], NAV_H / 2, 0);
        } else {
            lv_obj_set_style_bg_color(s_nav_sects[i], C_INV_BG, 0);
            lv_obj_set_style_bg_opa(s_nav_sects[i], LV_OPA_COVER, 0);
            lv_obj_set_style_radius(s_nav_sects[i], 0, 0);
        }
    }
}

/* ---------------------------------------------------------------------------
 * Persona derivation
 * --------------------------------------------------------------------------- */
STATIC buddy_led_state_e __led_from_persona(buddy_persona_state_e s)
{
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
    if (!s_state.ws_connected)           next = BUDDY_PERSONA_STATE_SLEEP;
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
    if (lbl_clock) {
        char buf[6];
        __format_clock(&s_state, buf, sizeof(buf));
        lv_label_set_text(lbl_clock, buf);
    }
    if (lbl_conn) {
        char buf[24];
        buddy_status_bar_format(buf, sizeof(buf),
                                buddy_ws_is_connected(),
                                s_state.ws_connected,
                                BUDDY_BAT_PCT_UNKNOWN);
        lv_label_set_text(lbl_conn, buf);
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
        for (b = 0; b < LEVEL_BAR_BLOCKS; b++) bar[b] = (b < progress) ? '#' : '.';
        bar[LEVEL_BAR_BLOCKS] = '\0';
        char lvbuf[24];
        (VOID_T)snprintf(lvbuf, sizeof(lvbuf), "Lv.%u [%s]", (unsigned)level, bar);
        lv_label_set_text(lbl_persona_level, lvbuf);
    }
}

STATIC VOID_T __refresh_sessions(VOID_T)
{
    __build_flat_list();

    const int32_t W = INFO_W - INFO_PAD * 2;

    if (s_flat_count == 0) {
        /* Show placeholder, hide rows */
        if (s_lbl_empty) lv_obj_clear_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t i = 0; i < LIST_VISIBLE_MAX; i++) {
            if (s_list_rows[i]) lv_obj_add_flag(s_list_rows[i], LV_OBJ_FLAG_HIDDEN);
            if (s_dot_objs[i])  lv_obj_add_flag(s_dot_objs[i],  LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }
    if (s_lbl_empty) lv_obj_add_flag(s_lbl_empty, LV_OBJ_FLAG_HIDDEN);

    /* Ensure scroll_top keeps selected item visible */
    if (s_sel_flat < s_scroll_top) s_scroll_top = s_sel_flat;
    if (s_sel_flat >= s_scroll_top + LIST_VISIBLE_MAX)
        s_scroll_top = (uint8_t)(s_sel_flat - LIST_VISIBLE_MAX + 1);

    for (uint32_t row = 0; row < LIST_VISIBLE_MAX; row++) {
        if (!s_list_rows[row]) continue;
        uint8_t fi = (uint8_t)(s_scroll_top + row);
        if (fi >= s_flat_count) {
            lv_obj_add_flag(s_list_rows[row], LV_OBJ_FLAG_HIDDEN);
            if (s_dot_objs[row]) lv_obj_add_flag(s_dot_objs[row], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        const flat_item_t *item = &s_flat[fi];
        char text[40];

        if (item->is_header) {
            /* Project header: "[ProjectName]" — FONT_L (terminus 18px) for
             * stronger visual hierarchy (dev_4_27 UI update). The row
             * stride (ROW_H=18) is sized so the bigger glyphs fit. */
            (VOID_T)snprintf(text, sizeof(text), "[%s]", item->proj_name);
            lv_label_set_text(s_list_rows[row], text);
            lv_obj_set_size(s_list_rows[row], W, H_L);
            lv_obj_set_style_text_font(s_list_rows[row], FONT_L, 0);
            /* Inverted style for headers */
            lv_obj_set_style_text_color(s_list_rows[row], C_INV_FG, 0);
            lv_obj_set_style_bg_color(s_list_rows[row],  C_INV_BG, 0);
            lv_obj_set_style_bg_opa(s_list_rows[row],    LV_OPA_COVER, 0);
            /* No running dot for headers */
            if (s_dot_objs[row]) lv_obj_add_flag(s_dot_objs[row], LV_OBJ_FLAG_HIDDEN);
        } else {
            /* Session row: "> name             sid8" — restore FONT_S in case
             * the previous row left FONT_M on this widget (rows are reused). */
            lv_obj_set_style_text_font(s_list_rows[row], FONT_S, 0);
            const buddy_session_t *sess = &s_state.sessions[item->sess_idx];
            BOOL_T selected = (fi == s_sel_flat) ? TRUE : FALSE;
            BOOL_T running  = sess->is_running ? TRUE : FALSE;

            /* Session name: use first 8 chars of sid if no name available */
            char sname[16];
            if (sess->name[0]) {
                (VOID_T)snprintf(sname, sizeof(sname), "%.15s", sess->name);
            } else {
                (VOID_T)snprintf(sname, sizeof(sname), "%.8s", sess->sid);
            }

            /* ID: first 8 chars */
            char sid8[9];
            (VOID_T)snprintf(sid8, sizeof(sid8), "%.8s", sess->sid);

            (VOID_T)snprintf(text, sizeof(text), "%s%-15s %7s",
                             selected ? ">" : " ",
                             sname, sid8);

            lv_label_set_text(s_list_rows[row], text);
            lv_obj_set_size(s_list_rows[row], W - DOT_SIZE - 2, H_S);

            if (selected) {
                lv_obj_set_style_text_color(s_list_rows[row], C_INV_FG, 0);
                lv_obj_set_style_bg_color(s_list_rows[row],  C_INV_BG, 0);
                lv_obj_set_style_bg_opa(s_list_rows[row],    LV_OPA_COVER, 0);
            } else {
                lv_obj_set_style_text_color(s_list_rows[row], C_FG, 0);
                lv_obj_set_style_bg_opa(s_list_rows[row],    LV_OPA_TRANSP, 0);
            }

            /* Running dot: blinks via s_dot_visible; color inverted on selected row */
            if (s_dot_objs[row]) {
                if (running) {
                    lv_color_t dot_col = selected ? C_INV_FG : C_FG;
                    lv_obj_set_style_bg_color(s_dot_objs[row], dot_col, 0);
                    if (s_dot_visible) {
                        lv_obj_clear_flag(s_dot_objs[row], LV_OBJ_FLAG_HIDDEN);
                    } else {
                        lv_obj_add_flag(s_dot_objs[row], LV_OBJ_FLAG_HIDDEN);
                    }
                    /* Keep the dot above the (possibly inverted) row label so
                     * its color stays correct when the row is selected. */
                    lv_obj_move_foreground(s_dot_objs[row]);
                } else {
                    lv_obj_add_flag(s_dot_objs[row], LV_OBJ_FLAG_HIDDEN);
                }
            }
        }

        lv_obj_set_pos(s_list_rows[row], 0, (int32_t)(row * ROW_H));
        lv_obj_clear_flag(s_list_rows[row], LV_OBJ_FLAG_HIDDEN);
    }
}

STATIC VOID_T __refresh(VOID_T)
{
    if (!ui_buddy_main_screen) return;
    PR_DEBUG("[main_screen] refresh: ws=%d sess=%d run=%d tok=%u flat=%d",
             (int)s_state.ws_connected, (int)s_state.sessions_count,
             (int)s_state.sessions_running, (unsigned)s_state.tokens,
             (int)s_flat_count);
    __refresh_header();
    __refresh_persona_strip();
    __derive_persona();
    __refresh_sessions();
}

/* ---------------------------------------------------------------------------
 * Persona timer (100ms) — also drives blink
 * --------------------------------------------------------------------------- */
STATIC VOID_T __persona_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (!ui_buddy_main_screen) return;
    ascii_persona_tick(s_persona_id, s_persona_state);

    /* Blink dot every BLINK_TICKS ticks (500ms) */
    s_blink_tick++;
    if (s_blink_tick >= BLINK_TICKS) {
        s_blink_tick = 0;
        s_dot_visible = s_dot_visible ? FALSE : TRUE;
        /* Only redraw if any session is running */
        BOOL_T any_running = FALSE;
        for (uint8_t i = 0; i < s_state.sessions_count; i++) {
            if (s_state.sessions[i].is_running) { any_running = TRUE; break; }
        }
        if (any_running) {
            lv_vendor_disp_lock();
            __refresh_sessions();
            lv_vendor_disp_unlock();
        }
    }
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
 * Key handler
 * --------------------------------------------------------------------------- */
STATIC VOID_T __key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case KEY_LEFT:
        /* Tab left → Status screen. screen_load() keeps main in stack so
         * screen_back() from status returns here correctly. */
        screen_load(&buddy_status_screen);
        break;

    case KEY_RIGHT:
        /* Tab right → Chart screen. Same stack-push convention. */
        screen_load(&buddy_chart_screen);
        break;

    case KEY_UP: {
        /* Move selection up, skip project headers */
        if (s_flat_count == 0) break;
        uint8_t orig = s_sel_flat;
        uint8_t idx  = s_sel_flat;
        /* Find previous session item */
        uint8_t tries = s_flat_count;
        while (tries-- > 0) {
            if (idx > 0) idx--;
            else idx = (uint8_t)(s_flat_count - 1);
            if (!s_flat[idx].is_header) { s_sel_flat = idx; break; }
        }
        if (s_sel_flat != orig) {
            /* Force the running dot visible right after selection changes,
             * otherwise if the blink-tick happens to be in the OFF half the
             * dot stays hidden until the next 500ms tick — looking like the
             * dot disappears on selection (dev_4_27 step1-3). */
            s_dot_visible = TRUE;
            s_blink_tick  = 0;
            lv_vendor_disp_lock();
            __refresh_sessions();
            lv_vendor_disp_unlock();
        }
    } break;

    case KEY_DOWN: {
        /* Move selection down, skip project headers */
        if (s_flat_count == 0) break;
        uint8_t orig = s_sel_flat;
        uint8_t idx  = s_sel_flat;
        uint8_t tries = s_flat_count;
        while (tries-- > 0) {
            idx = (uint8_t)((idx + 1) % s_flat_count);
            if (!s_flat[idx].is_header) { s_sel_flat = idx; break; }
        }
        if (s_sel_flat != orig) {
            /* Same fix as KEY_UP — keep dot visible after selection moves. */
            s_dot_visible = TRUE;
            s_blink_tick  = 0;
            lv_vendor_disp_lock();
            __refresh_sessions();
            lv_vendor_disp_unlock();
        }
    } break;

    case KEY_ENTER: {
        /* Enter session detail for selected session */
        if (s_sel_flat < s_flat_count && !s_flat[s_sel_flat].is_header) {
            uint8_t si = s_flat[s_sel_flat].sess_idx;
            if (si < s_state.sessions_count) {
                buddy_session_screen.state_data =
                    (void *)&s_state.sessions[si];
                screen_load(&buddy_session_screen);
            }
        }
    } break;

    case KEY_JOYCON:
        __persona_cycle(+1);
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
    ascii_persona_attach(ui_buddy_main_screen, 0, BODY_TOP);
    __build_persona_strip(ui_buddy_main_screen);
    __build_sessions_panel(ui_buddy_main_screen);
    __build_nav_bar(ui_buddy_main_screen);

    s_persona_id = __load_persona_id();
    s_scroll_top = 0;
    s_sel_flat   = 0;
    s_dot_visible = TRUE;
    s_blink_tick  = 0;

    buddy_tama_state_t snap;
    buddy_state_snapshot(&snap);
    s_state = snap;
    s_prev_prompt = snap.has_prompt ? TRUE : FALSE;

    /* Pull fresh heartbeat from the daemon (best-effort, non-blocking).
     * Daemon answers within one WS round-trip; rx pump will refresh state. */
    (VOID_T)buddy_ws_send_hb_req("main");

    __refresh_nav_bar();
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

    lbl_title = lbl_clock = lbl_conn = NULL;
    lbl_persona_name = lbl_persona_level = NULL;
    for (uint32_t i = 0; i < LIST_VISIBLE_MAX; i++) {
        s_list_rows[i] = NULL;
        s_dot_objs[i]  = NULL;
    }
    for (uint32_t i = 0; i < 4; i++) s_nav_sects[i] = NULL;
    s_lbl_empty = NULL;
    s_flat_count = 0;
}
