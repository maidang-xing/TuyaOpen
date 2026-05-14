/**
 * @file config_screen.c
 * @brief Claude Buddy config screen — IP address input + WS connect.
 *
 * Layout (384×168, monochrome):
 *   Header   y=  0..20   black bg, white text: "claude buddy" (typewriter) + status bar
 *   Body     y= 20..105  white bg — setup instructions + title
 *   Divider  y=105       1px black line
 *   IP area  y=106..168  white bg — 12-digit IP input + hint text
 *
 * Navigation:
 *   LEFT/RIGHT  move cursor (wraps 0↔11)
 *   UP          digit +1 mod 10
 *   DOWN        digit -1 mod 10
 *   ENTER       assemble IP → connect
 *   ESC         no-op (config_screen is the base screen)
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "config_screen.h"
#include "main_screen.h"
#include "buddy_transport.h"
#include "buddy_anim.h"
#include "buddy_cjk_font.h"
#include "lv_vendor.h"
#include "status_bar.h"
#include "tal_api.h"
#include "tuya_cloud_types.h"
#include "tal_kv.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Fonts
 * --------------------------------------------------------------------------- */
#define FONT_M  (&buddy_font_m)
#define FONT_S  (&buddy_font_s)

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
#define SCR_W       AI_PET_SCREEN_WIDTH     /* 384 */
#define SCR_H       AI_PET_SCREEN_HEIGHT    /* 168 */
#define HEADER_H    20

#define BODY_TOP    HEADER_H                /* 20  */
#define DIV_Y       105
#define IP_Y        106

#define DIGIT_COUNT 12
#define CELL_W      22
#define CELL_H      20
#define CELL_GAP    2
#define PAD         6

#define DIGIT_ROW_Y (IP_Y + 18)
#define HINT_Y      (DIGIT_ROW_Y + CELL_H + 4)

/* KV key — must match buddy_ws.c internal KV key */
#define KV_KEY_WS_HOST  "buddy_ws_host"

/* Connect poll timing */
#define POLL_INTERVAL_MS  500
#define POLL_MAX          10  /* 5 s total */

/* ---------------------------------------------------------------------------
 * Cell X position helper (groups 3+3+3+3 with '.' separators)
 * --------------------------------------------------------------------------- */
static int32_t __cell_x(int i)
{
    int group = i / 3;
    int pos   = i % 3;
    return (int32_t)(PAD + group * (3 * (CELL_W + CELL_GAP) + 10) + pos * (CELL_W + CELL_GAP));
}

/* ---------------------------------------------------------------------------
 * Static state
 * --------------------------------------------------------------------------- */
static lv_obj_t   *s_screen      = NULL;

/* Header widgets */
static lv_obj_t   *s_hdr_left    = NULL;   /* "claude buddy" — typewriter */
static lv_obj_t   *s_hdr_right   = NULL;   /* status bar */

/* Body widgets */
static lv_obj_t   *s_body_lbl    = NULL;   /* instructions */

/* IP digit cells */
static lv_obj_t   *s_cells[DIGIT_COUNT];
static lv_obj_t   *s_cell_lbls[DIGIT_COUNT];

/* Dot separator labels */
static lv_obj_t   *s_dots[3];

/* Hint label */
static lv_obj_t   *s_hint_lbl   = NULL;

/* Digit values [0..9] */
static uint8_t     s_digits[DIGIT_COUNT];
static int         s_cursor_pos = 0;

/* Connect poll timer */
static lv_timer_t *s_connect_poll = NULL;
static int         s_poll_count   = 0;

/* Hint restore timer (one-shot after failed connect) */
static lv_timer_t *s_hint_restore_timer = NULL;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
static void __load_ip(void);
static void __assemble_ip(char *buf, size_t cap);
static void __refresh_cell(int i);
static void __refresh_all_cells(void);
static void __set_cursor(int pos);
static void __kbd_cb(lv_event_t *e);
static void __connect(void);
static void __poll_cb(lv_timer_t *t);
static void __hint_restore_cb(lv_timer_t *t);
static void __build_header(lv_obj_t *parent);
static void __build_body(lv_obj_t *parent);
static void __build_ip_area(lv_obj_t *parent);

/* ---------------------------------------------------------------------------
 * KV IP load — parse "192.168.0.1" style stored string into s_digits[]
 * Default: 192.168.001.001
 * --------------------------------------------------------------------------- */
static void __load_ip(void)
{
    /* defaults */
    /* 192.168.001.001 */
    s_digits[0]  = 1; s_digits[1]  = 9; s_digits[2]  = 2;
    s_digits[3]  = 1; s_digits[4]  = 6; s_digits[5]  = 8;
    s_digits[6]  = 0; s_digits[7]  = 0; s_digits[8]  = 1;
    s_digits[9]  = 0; s_digits[10] = 0; s_digits[11] = 1;

    uint8_t *buf = NULL;
    size_t   len = 0;
    if (tal_kv_get(KV_KEY_WS_HOST, &buf, &len) != OPRT_OK || !buf) return;

    /* buf is a string like "192.168.1.1" */
    char host[64];
    size_t cp = (len < sizeof(host) - 1) ? len : sizeof(host) - 1;
    memcpy(host, buf, cp);
    host[cp] = '\0';
    tal_kv_free(buf);

    /* Parse octets */
    int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(host, "%d.%d.%d.%d", &a, &b, &c, &d) != 4) return;
    /* Clamp to 0-255 */
    if (a < 0 || a > 255 || b < 0 || b > 255 ||
        c < 0 || c > 255 || d < 0 || d > 255) return;

    s_digits[0]  = (uint8_t)((a / 100) % 10);
    s_digits[1]  = (uint8_t)((a / 10)  % 10);
    s_digits[2]  = (uint8_t)(a         % 10);
    s_digits[3]  = (uint8_t)((b / 100) % 10);
    s_digits[4]  = (uint8_t)((b / 10)  % 10);
    s_digits[5]  = (uint8_t)(b         % 10);
    s_digits[6]  = (uint8_t)((c / 100) % 10);
    s_digits[7]  = (uint8_t)((c / 10)  % 10);
    s_digits[8]  = (uint8_t)(c         % 10);
    s_digits[9]  = (uint8_t)((d / 100) % 10);
    s_digits[10] = (uint8_t)((d / 10)  % 10);
    s_digits[11] = (uint8_t)(d         % 10);
}

/* ---------------------------------------------------------------------------
 * Assemble IP string from digit array
 * --------------------------------------------------------------------------- */
static void __assemble_ip(char *buf, size_t cap)
{
    int a = s_digits[0] * 100 + s_digits[1] * 10 + s_digits[2];
    int b = s_digits[3] * 100 + s_digits[4] * 10 + s_digits[5];
    int c = s_digits[6] * 100 + s_digits[7] * 10 + s_digits[8];
    int d = s_digits[9] * 100 + s_digits[10] * 10 + s_digits[11];
    (void)snprintf(buf, cap, "%d.%d.%d.%d", a, b, c, d);
}

/* ---------------------------------------------------------------------------
 * Refresh a single digit cell appearance
 * --------------------------------------------------------------------------- */
static void __refresh_cell(int i)
{
    if (i < 0 || i >= DIGIT_COUNT) return;
    if (!s_cells[i] || !s_cell_lbls[i]) return;

    char txt[2] = { (char)('0' + s_digits[i]), '\0' };
    lv_label_set_text(s_cell_lbls[i], txt);

    if (i == s_cursor_pos) {
        /* Cursor cell: black bg, white text */
        lv_obj_set_style_bg_color(s_cells[i], C_INV_BG, 0);
        lv_obj_set_style_bg_opa(s_cells[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_cell_lbls[i], C_INV_FG, 0);
        /* Start blink animation on cell bg */
        buddy_anim_blink(s_cells[i], C_INV_BG, C_BG, 500);
    } else {
        /* Normal cell: white bg, black text, no blink */
        buddy_anim_blink_stop(s_cells[i]);
        lv_obj_set_style_bg_color(s_cells[i], C_BG, 0);
        lv_obj_set_style_bg_opa(s_cells[i], LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(s_cell_lbls[i], C_FG, 0);
    }
}

static void __refresh_all_cells(void)
{
    for (int i = 0; i < DIGIT_COUNT; i++) {
        __refresh_cell(i);
    }
}

/* ---------------------------------------------------------------------------
 * Move cursor, refresh old and new cell
 * --------------------------------------------------------------------------- */
static void __set_cursor(int pos)
{
    int old = s_cursor_pos;
    s_cursor_pos = pos;
    __refresh_cell(old);
    __refresh_cell(pos);
}

/* ---------------------------------------------------------------------------
 * Hint restore timer callback (one-shot after failed connect)
 * --------------------------------------------------------------------------- */
static void __hint_restore_cb(lv_timer_t *t)
{
    (void)t;
    if (s_hint_lbl) {
        lv_label_set_text(s_hint_lbl, "◄► 选位  ▲▼ 调值  ENTER 确认");
    }
    s_hint_restore_timer = NULL;
}

/* ---------------------------------------------------------------------------
 * Poll timer callback — check connection status
 * --------------------------------------------------------------------------- */
static void __poll_cb(lv_timer_t *t)
{
    (void)t;
    s_poll_count++;

#if !defined(CONFIG_LVGL_PC_SIMULATOR) || !CONFIG_LVGL_PC_SIMULATOR
    if (buddy_ws_is_connected()) {
        /* Connected — stop poll, go to main */
        if (s_connect_poll) {
            lv_timer_del(s_connect_poll);
            s_connect_poll = NULL;
        }
        screen_load(&buddy_main_screen);
        return;
    }
    if (s_poll_count >= POLL_MAX) {
        /* Timed out — stop poll, show error */
        lv_timer_del(s_connect_poll);
        s_connect_poll = NULL;
        if (s_hint_lbl) {
            lv_label_set_text(s_hint_lbl, "连接失败，请重试");
        }
        /* One-shot timer to restore hint after 3s */
        if (s_hint_restore_timer) {
            lv_timer_del(s_hint_restore_timer);
        }
        s_hint_restore_timer = lv_timer_create(__hint_restore_cb, 3000, NULL);
        lv_timer_set_repeat_count(s_hint_restore_timer, 1);
    }
#endif /* !CONFIG_LVGL_PC_SIMULATOR */
}

/* ---------------------------------------------------------------------------
 * Connect flow
 * --------------------------------------------------------------------------- */
static void __connect(void)
{
#ifdef CONFIG_LVGL_PC_SIMULATOR
    screen_load(&buddy_main_screen);
#else
    char ip[32];
    __assemble_ip(ip, sizeof(ip));

    buddy_ws_set_host(ip);
    tal_kv_set(KV_KEY_WS_HOST, (const uint8_t *)ip, strlen(ip) + 1);
    buddy_ws_stop();
    buddy_ws_start(NULL);

    if (s_hint_lbl) {
        lv_label_set_text(s_hint_lbl, "连接中...");
    }

    /* Clean up any existing poll */
    if (s_connect_poll) {
        lv_timer_del(s_connect_poll);
        s_connect_poll = NULL;
    }
    if (s_hint_restore_timer) {
        lv_timer_del(s_hint_restore_timer);
        s_hint_restore_timer = NULL;
    }
    s_poll_count = 0;
    s_connect_poll = lv_timer_create(__poll_cb, POLL_INTERVAL_MS, NULL);
#endif /* CONFIG_LVGL_PC_SIMULATOR */
}

/* ---------------------------------------------------------------------------
 * Key event callback
 * --------------------------------------------------------------------------- */
static void __kbd_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    switch (key) {
    case KEY_LEFT: {
        int next = s_cursor_pos - 1;
        if (next < 0) next = DIGIT_COUNT - 1;
        __set_cursor(next);
        break;
    }
    case KEY_RIGHT: {
        int next = (s_cursor_pos + 1) % DIGIT_COUNT;
        __set_cursor(next);
        break;
    }
    case KEY_UP: {
        s_digits[s_cursor_pos] = (uint8_t)((s_digits[s_cursor_pos] + 1) % 10);
        __refresh_cell(s_cursor_pos);
        break;
    }
    case KEY_DOWN: {
        s_digits[s_cursor_pos] = (uint8_t)((s_digits[s_cursor_pos] + 9) % 10);
        __refresh_cell(s_cursor_pos);
        break;
    }
    case KEY_ENTER:
        __connect();
        break;
    case KEY_ESC:
        /* config_screen is the base — no-op */
        break;
    default:
        break;
    }
}

/* ---------------------------------------------------------------------------
 * Build helpers
 * --------------------------------------------------------------------------- */
static void __build_header(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, HEADER_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, C_INV_BG, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Left: "claude buddy" with typewriter anim */
    s_hdr_left = lv_label_create(bar);
    lv_label_set_text(s_hdr_left, "");
    lv_obj_set_style_text_font(s_hdr_left, FONT_S, 0);
    lv_obj_set_style_text_color(s_hdr_left, C_INV_FG, 0);
    lv_obj_align(s_hdr_left, LV_ALIGN_LEFT_MID, 4, 0);
    buddy_anim_typewriter(s_hdr_left, "claude buddy", 40);

    /* Right: status bar */
    s_hdr_right = lv_label_create(bar);
    lv_label_set_text(s_hdr_right, "W b --");
    lv_obj_set_style_text_font(s_hdr_right, FONT_S, 0);
    lv_obj_set_style_text_color(s_hdr_right, C_INV_FG, 0);
    lv_obj_align(s_hdr_right, LV_ALIGN_RIGHT_MID, -4, 0);

    /* Update status bar */
    char sbuf[24];
#if !defined(CONFIG_LVGL_PC_SIMULATOR) || !CONFIG_LVGL_PC_SIMULATOR
    buddy_status_bar_format(sbuf, sizeof(sbuf),
                            FALSE, buddy_ws_is_connected(),
                            BUDDY_BAT_PCT_UNKNOWN);
#else
    buddy_status_bar_format(sbuf, sizeof(sbuf), FALSE, FALSE, BUDDY_BAT_PCT_UNKNOWN);
#endif /* !CONFIG_LVGL_PC_SIMULATOR */
    lv_label_set_text(s_hdr_right, sbuf);
}

static void __build_body(lv_obj_t *parent)
{
    /* Body container: y=20..105 */
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_size(body, SCR_W, DIV_Y - BODY_TOP);
    lv_obj_set_pos(body, 0, BODY_TOP);
    lv_obj_set_style_bg_color(body, C_BG, 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_set_style_pad_all(body, 4, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* Title */
    lv_obj_t *title = lv_label_create(body);
    lv_obj_set_style_text_font(title, FONT_M, 0);
    lv_obj_set_style_text_color(title, C_FG, 0);
    lv_obj_set_pos(title, 0, 0);
    buddy_anim_typewriter(title, "Setup", 60);

    /* Instructions — 4 numbered steps */
    static const char *const STEPS[] = {
        "1. 打开涂鸦 App 扫码配网",
        "2. 安装插件: claude mcp add buddy",
        "3. 填写下方服务器 IP 地址",
        "4. 按 ENTER 连接",
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *lbl = lv_label_create(body);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(lbl, SCR_W - PAD * 2);
        lv_obj_set_style_text_font(lbl, FONT_S, 0);
        lv_obj_set_style_text_color(lbl, C_FG, 0);
        lv_obj_set_pos(lbl, 0, 18 + i * 16);
        lv_label_set_text(lbl, STEPS[i]);
    }

    /* Divider */
    lv_obj_t *div = lv_obj_create(parent);
    lv_obj_set_size(div, SCR_W, 1);
    lv_obj_set_pos(div, 0, DIV_Y);
    lv_obj_set_style_bg_color(div, C_FG, 0);
    lv_obj_set_style_border_width(div, 0, 0);
    lv_obj_set_style_radius(div, 0, 0);
    lv_obj_set_style_pad_all(div, 0, 0);
}

static void __build_ip_area(lv_obj_t *parent)
{
    /* "Server IP:" label */
    lv_obj_t *ip_lbl = lv_label_create(s_screen);
    lv_obj_set_style_text_font(ip_lbl, FONT_S, 0);
    lv_obj_set_style_text_color(ip_lbl, C_FG, 0);
    lv_obj_set_pos(ip_lbl, PAD, IP_Y + 2);
    lv_label_set_text(ip_lbl, "Server IP:");

    /* Build 12 digit cells */
    for (int i = 0; i < DIGIT_COUNT; i++) {
        int32_t cx = __cell_x(i);
        int32_t cy = DIGIT_ROW_Y;

        /* Cell background box */
        s_cells[i] = lv_obj_create(parent);
        lv_obj_set_size(s_cells[i], CELL_W, CELL_H);
        lv_obj_set_pos(s_cells[i], cx, cy);
        lv_obj_set_style_bg_color(s_cells[i], C_BG, 0);
        lv_obj_set_style_bg_opa(s_cells[i], LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_cells[i], C_FG, 0);
        lv_obj_set_style_border_width(s_cells[i], 1, 0);
        lv_obj_set_style_radius(s_cells[i], 0, 0);
        lv_obj_set_style_pad_all(s_cells[i], 0, 0);
        lv_obj_clear_flag(s_cells[i], LV_OBJ_FLAG_SCROLLABLE);

        /* Digit label inside cell */
        s_cell_lbls[i] = lv_label_create(s_cells[i]);
        lv_obj_set_style_text_font(s_cell_lbls[i], FONT_S, 0);
        lv_obj_set_style_text_color(s_cell_lbls[i], C_FG, 0);
        lv_obj_align(s_cell_lbls[i], LV_ALIGN_CENTER, 0, 0);

        char txt[2] = { (char)('0' + s_digits[i]), '\0' };
        lv_label_set_text(s_cell_lbls[i], txt);
    }

    /* Dot separators after groups 0, 1, 2 */
    for (int g = 0; g < 3; g++) {
        /* Dot appears after the last cell of group g (cell index 2, 5, 8) */
        int last_in_group = (g + 1) * 3 - 1;
        int32_t dot_x = __cell_x(last_in_group) + CELL_W + 1;
        int32_t dot_y = DIGIT_ROW_Y;

        s_dots[g] = lv_label_create(parent);
        lv_label_set_text(s_dots[g], ".");
        lv_obj_set_style_text_font(s_dots[g], FONT_M, 0);
        lv_obj_set_style_text_color(s_dots[g], C_FG, 0);
        lv_obj_set_pos(s_dots[g], dot_x, dot_y + 2);
    }

    /* Hint label */
    s_hint_lbl = lv_label_create(parent);
    lv_label_set_long_mode(s_hint_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s_hint_lbl, SCR_W - PAD * 2, 16);
    lv_obj_set_pos(s_hint_lbl, PAD, HINT_Y);
    lv_obj_set_style_text_font(s_hint_lbl, FONT_S, 0);
    lv_obj_set_style_text_color(s_hint_lbl, C_FG, 0);
    lv_label_set_text(s_hint_lbl, "◄► 选位  ▲▼ 调值  ENTER 确认");
}

/* ---------------------------------------------------------------------------
 * Screen init / deinit
 * --------------------------------------------------------------------------- */
void config_screen_init(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(s_screen, C_BG, 0);
    lv_obj_set_style_pad_all(s_screen, 0, 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    /* Load saved IP */
    __load_ip();
    s_cursor_pos = 0;

    __build_header(s_screen);
    __build_body(s_screen);
    __build_ip_area(s_screen);

    /* Apply cursor styling (blink on cell 0) */
    __refresh_all_cells();

    /* Input group */
    lv_group_add_obj(lv_group_get_default(), s_screen);
    lv_obj_add_event_cb(s_screen, __kbd_cb, LV_EVENT_KEY, NULL);
    lv_group_focus_obj(s_screen);

    PR_NOTICE("[%s] init", buddy_config_screen.name);
}

void config_screen_deinit(void)
{
    /* 1. Delete connect poll timer */
    if (s_connect_poll) {
        lv_timer_del(s_connect_poll);
        s_connect_poll = NULL;
    }
    /* Delete hint restore timer */
    if (s_hint_restore_timer) {
        lv_timer_del(s_hint_restore_timer);
        s_hint_restore_timer = NULL;
    }

    /* 2. Stop blink on all digit cells */
    for (int i = 0; i < DIGIT_COUNT; i++) {
        if (s_cells[i]) {
            buddy_anim_blink_stop(s_cells[i]);
        }
    }

    /* 3. Stop typewriter on header */
    if (s_hdr_left) {
        buddy_anim_typewriter_stop(s_hdr_left);
    }

    /* 4. Remove event cb and group membership */
    if (s_screen) {
        lv_obj_remove_event_cb(s_screen, __kbd_cb);
        lv_group_remove_obj(s_screen);
    }

    /* 5. NULL all static pointers */
    s_screen     = NULL;
    s_hdr_left   = NULL;
    s_hdr_right  = NULL;
    s_body_lbl   = NULL;
    s_hint_lbl   = NULL;
    for (int i = 0; i < DIGIT_COUNT; i++) {
        s_cells[i]    = NULL;
        s_cell_lbls[i] = NULL;
    }
    for (int g = 0; g < 3; g++) {
        s_dots[g] = NULL;
    }
    s_poll_count = 0;
}

/* ---------------------------------------------------------------------------
 * Screen descriptor
 * --------------------------------------------------------------------------- */
Screen_t buddy_config_screen = {
    .init       = config_screen_init,
    .deinit     = config_screen_deinit,
    .screen_obj = &s_screen,
    .name       = "buddy_config_screen",
};
