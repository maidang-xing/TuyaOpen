/**
 * @file status_screen.c
 * @brief Claude Buddy status / stats screen (tab 1 of 4).
 *
 * Layout (384×168, monochrome):
 *
 *   Header  y=0..20   (20px) black bg, white text
 *                     Left:   "claude buddy"  (FONT_S)
 *                     Center: HH:MM from wall_epoch_s
 *                     Right:  "W B 100%"  (W=wifi, B/b=WS, batt placeholder)
 *
 *   Content y=20..162 (142px) white bg, black text — 8 stat rows (FONT_S,16px each)
 *     Row 0  y=24   "Claude Code  " + claude_version or "---"
 *     Row 1  y=40   "Model        " + model or "---"
 *     Row 2  y=56   "Owner        " + owner_name or "---"
 *     Row 3  y=72   "Sessions     N total N run N wait"
 *     Row 4  y=88   "Today        " out / in / $cost
 *     Row 5  y=104  "Total        " out / in / $cost
 *     Row 6  y=120  "Cache R/W    " cache_read / cache_write
 *     Row 7  y=136  "Context      " ctx_used / ctx_total (NN%)
 *
 *   All values are sourced from buddy_tama_state_t which the daemon
 *   populates from ~/.claude/{settings.json, projects, stats-cache.json}
 *   so unavailable fields render as "---" without device-side scanning.
 *
 *   Nav bar y=162..168 (6px) 4 equal tabs (96px each): St | Hm | Ch | Pi
 *                     "St" (leftmost) is highlighted (white bg, black text).
 *
 * Key handling:
 *   KEY_RIGHT → screen_back()  (return to main/home)
 *   KEY_ESC   → screen_back()
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "status_screen.h"
#include "buddy_protocol.h"
#include "buddy_transport.h"
#include "buddy_types.h"
#include "screen_manager.h"
#include "lv_vendor.h"
#include "tal_api.h"
#include "tuya_cloud_types.h"
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Fonts
 * --------------------------------------------------------------------------- */
#include "buddy_cjk_font.h"
#include "buddy_status_bar.h"
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
 * Layout constants
 * --------------------------------------------------------------------------- */
#define SCR_W     AI_PET_SCREEN_WIDTH    /* 384 */
#define SCR_H     AI_PET_SCREEN_HEIGHT   /* 168 */

#define HEADER_H  20
#define NAV_H     6
#define NAV_Y     162                    /* SCR_H - NAV_H */

#define CONTENT_Y HEADER_H              /* 20  */
#define CONTENT_H (NAV_Y - CONTENT_Y)  /* 142 */

/* Row geometry: FONT_S height=14, 2px gap → stride 16px */
#define H_S       14
#define H_M       16
#define ROW_STRIDE 16
#define CONTENT_PAD 3

/* Row Y positions (relative to screen top) */
#define ROW_Y(n)  (CONTENT_Y + CONTENT_PAD + (int32_t)(n) * ROW_STRIDE + 1)
/* Row 0: 20+3+0+1=24, Row 1: 40, Row 2: 56, Row 3: 72, Row 4: 88,
 * Row 5: 104, Row 6: 120, Row 7: 136  — all fit before y=162 */

#define ROW_COUNT  8
#define ROW_W      (SCR_W - CONTENT_PAD * 2)   /* 378 */

/* Nav bar: 4 equal sections */
#define NAV_TAB_W  (SCR_W / 4)    /* 96 */
#define NAV_TAB_COUNT 4

/* ---------------------------------------------------------------------------
 * Widgets
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *s_screen      = NULL;

/* Header labels */
STATIC lv_obj_t *s_hdr_title   = NULL;
STATIC lv_obj_t *s_hdr_clock   = NULL;
STATIC lv_obj_t *s_hdr_status  = NULL;

/* Content row labels */
STATIC lv_obj_t *s_rows[ROW_COUNT];

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
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __fmt_tok(uint32_t v, char *out, size_t n);
STATIC VOID_T __fmt_cost(uint32_t ucc, char *out, size_t n);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_nav_bar(lv_obj_t *parent);
STATIC VOID_T __build_content(lv_obj_t *parent);
STATIC VOID_T __refresh_header(VOID_T);
STATIC VOID_T __refresh_content(VOID_T);

/* ---------------------------------------------------------------------------
 * Screen_t export
 * --------------------------------------------------------------------------- */
Screen_t buddy_status_screen = {
    .init       = __init,
    .deinit     = __deinit,
    .screen_obj = &s_screen,
    .name       = "buddy_status_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Utility: clock formatter
 *   Applies live wall-clock delta so the time advances even without new WS
 *   frames, identical to buddy_main_screen.c.
 * --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
 * Utility: token formatter — "1.2k" for >=1000, else plain integer
 * --------------------------------------------------------------------------- */
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
 * Utility: micro-USD cost formatter → "$1.23"
 *   ucc is micro-USD (÷1 000 000 = USD).
 *   We compute whole cents from milli-USD to avoid floating point.
 * --------------------------------------------------------------------------- */
STATIC VOID_T __fmt_cost(uint32_t ucc, char *out, size_t n)
{
    /* Convert micro-USD to milli-USD (÷1000), then to cents×10 (÷100) */
    uint32_t milliusd  = ucc / 1000U;          /* milli-USD              */
    uint32_t dollars   = milliusd / 1000U;      /* whole dollars          */
    uint32_t cents100  = (milliusd % 1000U);    /* remainder in milli-USD */
    uint32_t cents     = cents100 / 10U;        /* hundredths of a dollar */
    (VOID_T)snprintf(out, n, "%u.%02u", (unsigned)dollars, (unsigned)cents);
}

/* ---------------------------------------------------------------------------
 * Header builder
 *   Black bar (384×20). Three labels:
 *     Left  x=4:   "claude buddy"
 *     Center:      HH:MM clock
 *     Right x=-4:  "W B 100%"
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

    /* Left: screen title */
    s_hdr_title = lv_label_create(bar);
    lv_label_set_text(s_hdr_title, "claude buddy");
    lv_obj_set_style_text_font(s_hdr_title, FONT_S, 0);
    lv_obj_set_style_text_color(s_hdr_title, C_INV_FG, 0);
    lv_obj_set_style_pad_all(s_hdr_title, 0, 0);
    lv_obj_align(s_hdr_title, LV_ALIGN_LEFT_MID, 4, 0);

    /* Center: wall clock */
    s_hdr_clock = lv_label_create(bar);
    lv_label_set_text(s_hdr_clock, "--:--");
    lv_obj_set_style_text_font(s_hdr_clock, FONT_S, 0);
    lv_obj_set_style_text_color(s_hdr_clock, C_INV_FG, 0);
    lv_obj_set_style_pad_all(s_hdr_clock, 0, 0);
    lv_obj_align(s_hdr_clock, LV_ALIGN_CENTER, 0, 0);

    /* Right: connectivity + battery */
    s_hdr_status = lv_label_create(bar);
    lv_label_set_text(s_hdr_status, "W B 100%");
    lv_obj_set_style_text_font(s_hdr_status, FONT_S, 0);
    lv_obj_set_style_text_color(s_hdr_status, C_INV_FG, 0);
    lv_obj_set_style_pad_all(s_hdr_status, 0, 0);
    lv_obj_align(s_hdr_status, LV_ALIGN_RIGHT_MID, -4, 0);
}

/* ---------------------------------------------------------------------------
 * Nav bar builder
 *   Thin black bar (384×6) at y=162, split into 4 tabs of 96px.
 *   Tab 0 ("St") is highlighted: white fill.
 *   Others: black fill.  No text labels (too tall for 6px bar).
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_nav_bar(lv_obj_t *parent)
{
    /* Background bar */
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, NAV_H);
    lv_obj_set_pos(bar, 0, NAV_Y);
    lv_obj_set_style_bg_color(bar, C_INV_BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < NAV_TAB_COUNT; i++) {
        BOOL_T active = (i == 0) ? TRUE : FALSE;

        lv_obj_t *cell = lv_obj_create(bar);
        lv_obj_set_size(cell, NAV_TAB_W, NAV_H);
        lv_obj_set_pos(cell, (int32_t)(i * (uint32_t)NAV_TAB_W), 0);
        lv_obj_set_style_bg_color(cell, active ? C_BG : C_INV_BG, 0);
        lv_obj_set_style_border_width(cell, 0, 0);
        lv_obj_set_style_radius(cell, active ? (NAV_H / 2) : 0, 0);
        lv_obj_set_style_pad_all(cell, 0, 0);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    }
}

/* ---------------------------------------------------------------------------
 * Content builder
 *   White area (384×142) from y=20 to y=162.
 *   8 FONT_S label rows, each 16px tall, x=CONTENT_PAD, w=ROW_W.
 * --------------------------------------------------------------------------- */
STATIC VOID_T __build_content(lv_obj_t *parent)
{
    /* White content background */
    lv_obj_t *bg = lv_obj_create(parent);
    lv_obj_set_size(bg, SCR_W, CONTENT_H);
    lv_obj_set_pos(bg, 0, CONTENT_Y);
    lv_obj_set_style_bg_color(bg, C_BG, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_pad_all(bg, 0, 0);
    lv_obj_clear_flag(bg, LV_OBJ_FLAG_SCROLLABLE);

    for (uint32_t i = 0; i < ROW_COUNT; i++) {
        lv_obj_t *l = lv_label_create(bg);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
        lv_obj_set_size(l, ROW_W, H_S);
        lv_obj_set_style_pad_all(l, 0, 0);
        lv_obj_set_style_text_font(l, FONT_S, 0);
        lv_obj_set_style_text_color(l, C_FG, 0);
        /* y is relative to the bg container, so subtract CONTENT_Y */
        lv_obj_set_pos(l, CONTENT_PAD, ROW_Y(i) - CONTENT_Y);
        lv_label_set_text(l, "");
        s_rows[i] = l;
    }
}

/* ---------------------------------------------------------------------------
 * Header refresh — updates clock and WS status indicator
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_header(VOID_T)
{
    /* Clock */
    if (s_hdr_clock) {
        char clk[6];
        __format_clock(&s_state, clk, sizeof(clk));
        lv_label_set_text(s_hdr_clock, clk);
    }

    /* Connectivity + battery: icons via shared formatter */
    if (s_hdr_status) {
        char buf[24];
        buddy_status_bar_format(buf, sizeof(buf),
                                buddy_ws_cloud_is_connected(),
                                s_state.ws_connected,
                                BUDDY_BAT_PCT_UNKNOWN);
        lv_label_set_text(s_hdr_status, buf);
    }
}

/* ---------------------------------------------------------------------------
 * Content refresh — populates the 8 stat rows from s_state
 * --------------------------------------------------------------------------- */
STATIC VOID_T __refresh_content(VOID_T)
{
    char line[80];
    char vout[16];
    char vin[16];
    char vcost[16];

    /* Row 0: Claude Code version (from ~/.claude/settings.json) */
    if (s_rows[0]) {
        if (s_state.claude_version[0]) {
            (VOID_T)snprintf(line, sizeof(line),
                             "Claude Code   %s", s_state.claude_version);
        } else {
            (VOID_T)snprintf(line, sizeof(line), "Claude Code   ---");
        }
        lv_label_set_text(s_rows[0], line);
    }

    /* Row 1: Current model (from settings.json or env) */
    if (s_rows[1]) {
        if (s_state.model[0]) {
            (VOID_T)snprintf(line, sizeof(line),
                             "Model         %s", s_state.model);
        } else {
            (VOID_T)snprintf(line, sizeof(line), "Model         ---");
        }
        lv_label_set_text(s_rows[1], line);
    }

    /* Row 2: Owner (from {"cmd":"owner"} during pairing) */
    if (s_rows[2]) {
        if (s_state.owner_name[0]) {
            (VOID_T)snprintf(line, sizeof(line),
                             "Owner         %s", s_state.owner_name);
        } else {
            (VOID_T)snprintf(line, sizeof(line), "Owner         ---");
        }
        lv_label_set_text(s_rows[2], line);
    }

    /* Row 3: Live session counters */
    if (s_rows[3]) {
        (VOID_T)snprintf(line, sizeof(line),
                         "Sessions      %u total %u run %u wait",
                         (unsigned)s_state.sessions_total,
                         (unsigned)s_state.sessions_running,
                         (unsigned)s_state.sessions_waiting);
        lv_label_set_text(s_rows[3], line);
    }

    /* Row 4: Today usage — combine output / input / cost in one row */
    if (s_rows[4]) {
        __fmt_tok(s_state.tokens_today,    vout, sizeof(vout));
        __fmt_tok(s_state.tokens_in_today, vin,  sizeof(vin));
        __fmt_cost(s_state.cost_today_ucc, vcost, sizeof(vcost));
        (VOID_T)snprintf(line, sizeof(line),
                         "Today         %s out / %s in / $%s",
                         vout, vin, vcost);
        lv_label_set_text(s_rows[4], line);
    }

    /* Row 5: Total usage */
    if (s_rows[5]) {
        __fmt_tok(s_state.tokens,    vout, sizeof(vout));
        __fmt_tok(s_state.tokens_in, vin,  sizeof(vin));
        __fmt_cost(s_state.cost_total_ucc, vcost, sizeof(vcost));
        (VOID_T)snprintf(line, sizeof(line),
                         "Total         %s out / %s in / $%s",
                         vout, vin, vcost);
        lv_label_set_text(s_rows[5], line);
    }

    /* Row 6: Cache read / write tokens (Anthropic prompt cache).
     * Render "---" when both are zero — likely no API turn observed yet. */
    if (s_rows[6]) {
        if (s_state.cache_read == 0U && s_state.cache_write == 0U) {
            (VOID_T)snprintf(line, sizeof(line), "Cache R/W     ---");
        } else {
            __fmt_tok(s_state.cache_read,  vout, sizeof(vout));
            __fmt_tok(s_state.cache_write, vin,  sizeof(vin));
            (VOID_T)snprintf(line, sizeof(line),
                             "Cache R/W     %s / %s", vout, vin);
        }
        lv_label_set_text(s_rows[6], line);
    }

    /* Row 7: Context window utilization. ctx_total can be 0 before the
     * first heartbeat carrying a context size — show "---" in that case. */
    if (s_rows[7]) {
        if (s_state.ctx_total == 0U) {
            (VOID_T)snprintf(line, sizeof(line), "Context       ---");
        } else {
            __fmt_tok(s_state.ctx_used,  vout, sizeof(vout));
            __fmt_tok(s_state.ctx_total, vin,  sizeof(vin));
            unsigned pct = (unsigned)(((uint64_t)s_state.ctx_used * 100U)
                                       / s_state.ctx_total);
            if (pct > 100U) pct = 100U;
            (VOID_T)snprintf(line, sizeof(line),
                             "Context       %s/%s (%u%%)",
                             vout, vin, pct);
        }
        lv_label_set_text(s_rows[7], line);
    }
}

/* ---------------------------------------------------------------------------
 * Key handler
 *   RIGHT or ESC → screen_back() (return to caller, typically buddy_main)
 *   All other keys are ignored.
 * --------------------------------------------------------------------------- */
STATIC VOID_T __key_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case KEY_RIGHT:
        screen_back();
        break;
    case KEY_ESC:
        screen_back();
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Public update hook (called by external state pusher if needed)
 *   Kept lightweight: just refreshes the content from the new state.
 * --------------------------------------------------------------------------- */
VOID_T buddy_status_screen_update_state(const buddy_tama_state_t *state)
{
    if (!state) return;
    lv_vendor_disp_lock();
    s_state = *state;
    if (s_screen) {
        __refresh_header();
        __refresh_content();
    }
    lv_vendor_disp_unlock();
}

/* ---------------------------------------------------------------------------
 * Init
 * --------------------------------------------------------------------------- */
STATIC VOID_T __init(VOID_T)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_screen, C_BG, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Snapshot WS state first so builders can query it; then ask the
     * daemon for a fresh heartbeat so version/cost/token totals are
     * current the moment this screen comes up. */
    buddy_state_snapshot(&s_state);
    (VOID_T)buddy_ws_send_hb_req("status");

    /* Build UI layers */
    __build_header(s_screen);
    __build_content(s_screen);
    __build_nav_bar(s_screen);  /* drawn last so it sits above content */

    /* Populate with live data */
    __refresh_header();
    __refresh_content();

    /* Register key handler */
    lv_obj_add_event_cb(s_screen, __key_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), s_screen);
    lv_group_focus_obj(s_screen);

    PR_NOTICE("[%s] init ver=%s model=%s",
              buddy_status_screen.name,
              s_state.claude_version[0] ? s_state.claude_version : "?",
              s_state.model[0]          ? s_state.model          : "?");
}

/* ---------------------------------------------------------------------------
 * Deinit
 * --------------------------------------------------------------------------- */
STATIC VOID_T __deinit(VOID_T)
{
    if (s_screen) {
        lv_obj_remove_event_cb(s_screen, __key_cb);
        lv_group_remove_obj(s_screen);
    }

    s_hdr_title  = NULL;
    s_hdr_clock  = NULL;
    s_hdr_status = NULL;

    for (uint32_t i = 0; i < ROW_COUNT; i++) {
        s_rows[i] = NULL;
    }
}
