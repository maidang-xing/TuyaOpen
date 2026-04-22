/**
 * @file buddy_main_screen.c
 * @brief Claude Desktop Buddy minimal UI - text-only live mirror of host data.
 *
 * The only information shown on this screen comes from the Claude desktop
 * via BLE (Nordic UART Service).  There are no animations, demo modes,
 * pagination or stats pages; the single responsibility of this file is
 * to render buddy_tama_state_t and to translate button presses into
 * permission-decision JSON frames through buddy_ble.
 *
 * See doc/UI_INTERACTION.md for the full layout / button specification.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "buddy_main_screen.h"
#include "buddy_ble.h"
#include "buddy_data.h"
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
#define BUDDY_FONT_TITLE   &lv_font_terminusTTF_Bold_18
#define BUDDY_FONT_CONTENT &lv_font_terminusTTF_Bold_16
#define BUDDY_FONT_HINT    &lv_font_terminusTTF_Bold_14

/* ---------------------------------------------------------------------------
 * Layout constants
 * --------------------------------------------------------------------------- */
#define SCR_W        AI_PET_SCREEN_WIDTH   /* 384 */
#define SCR_H        AI_PET_SCREEN_HEIGHT  /* 168 */

#define HEADER_H     20
#define FOOTER_H     24
#define BODY_TOP     HEADER_H
#define BODY_H       (SCR_H - HEADER_H - FOOTER_H)  /* 124 */

#define HOT_COLOR    lv_color_make(0xFA, 0x20, 0x20)
#define OK_COLOR     lv_color_make(0x20, 0xA0, 0x20)
#define ACCENT_COLOR lv_color_make(0x10, 0x60, 0xC0)

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_buddy_main_screen = NULL;

/* header */
STATIC lv_obj_t *lbl_title;
STATIC lv_obj_t *lbl_ble;
STATIC lv_obj_t *lbl_clock;
STATIC lv_obj_t *lbl_device;

/* body - "no prompt" view */
STATIC lv_obj_t *body_status_msg;
STATIC lv_obj_t *body_sessions;
STATIC lv_obj_t *body_tokens;
STATIC lv_obj_t *body_owner;

/* body - "pending prompt" card */
STATIC lv_obj_t *card_prompt;
STATIC lv_obj_t *card_tool;
STATIC lv_obj_t *card_hint;
STATIC lv_obj_t *card_id;

/* body - entries transcript panel (4 visible rows) */
#define BUDDY_ENTRIES_VISIBLE 4
STATIC lv_obj_t *entries_lines[BUDDY_ENTRIES_VISIBLE];

/* footer */
STATIC lv_obj_t *lbl_footer;

/* latest snapshot we've rendered (initial value is the BLE bridge's
 * empty "waiting for Claude" baseline). */
STATIC buddy_tama_state_t s_staged_state = {0};

/* Entries scroll offset: 0 = newest row is at the top of the panel.
 * Incrementing rolls the window toward older entries. Clamped so the
 * panel always has at least one populated slot visible. */
STATIC uint8_t s_entries_scroll = 0;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __main_screen_init(VOID_T);
STATIC VOID_T __main_screen_deinit(VOID_T);
STATIC VOID_T __keyboard_event_cb(lv_event_t *e);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_body(lv_obj_t *parent);
STATIC VOID_T __build_footer(lv_obj_t *parent);
STATIC VOID_T __refresh_locked(VOID_T);
STATIC VOID_T __refresh_footer_locked(VOID_T);
STATIC VOID_T __refresh_entries_locked(VOID_T);
STATIC VOID_T __send_decision(const char *decision);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __format_entry_line(const buddy_entry_t *e, uint64_t abs_epoch_s, char *out, size_t n);

Screen_t buddy_main_screen = {
    .init = __main_screen_init,
    .deinit = __main_screen_deinit,
    .screen_obj = &ui_buddy_main_screen,
    .name = "buddy_main_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Public entry points
 * --------------------------------------------------------------------------- */
/**
 * @brief Stage a new snapshot and repaint if the screen is visible.
 * @param[in] state snapshot from the BLE bridge
 * @return none
 */
VOID_T buddy_main_screen_update_state(const buddy_tama_state_t *state)
{
    if (state == NULL) {
        return;
    }

    lv_vendor_disp_lock();
    s_staged_state = *state;
    if (ui_buddy_main_screen != NULL) {
        __refresh_locked();
    }
    lv_vendor_disp_unlock();
}

/* ---------------------------------------------------------------------------
 * UI construction
 * --------------------------------------------------------------------------- */
/**
 * @brief Build the top bar (dark background, title, BLE status, device name).
 * @param[in] parent root screen
 * @return none
 */
STATIC VOID_T __build_header(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, HEADER_H);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_title = lv_label_create(bar);
    lv_label_set_text(lbl_title, "Claude Buddy");
    lv_obj_set_style_text_font(lbl_title, BUDDY_FONT_CONTENT, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
    lv_obj_align(lbl_title, LV_ALIGN_LEFT_MID, 6, 0);

    lbl_clock = lv_label_create(bar);
    lv_label_set_text(lbl_clock, "--:--");
    lv_obj_set_style_text_font(lbl_clock, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(lbl_clock, lv_color_white(), 0);
    lv_obj_align(lbl_clock, LV_ALIGN_CENTER, 0, 0);

    lbl_ble = lv_label_create(bar);
    lv_label_set_text(lbl_ble, "BLE: -");
    lv_obj_set_style_text_font(lbl_ble, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(lbl_ble, lv_color_white(), 0);
    lv_obj_align(lbl_ble, LV_ALIGN_LEFT_MID, 140, 0);

    lbl_device = lv_label_create(bar);
    lv_label_set_text(lbl_device, "Claude");
    lv_obj_set_style_text_font(lbl_device, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(lbl_device, lv_color_white(), 0);
    lv_obj_align(lbl_device, LV_ALIGN_RIGHT_MID, -6, 0);
}

/**
 * @brief Build the body with both the idle-view labels and the hidden
 *        permission card.  Exactly one of them is shown on each refresh.
 * @param[in] parent root screen
 * @return none
 */
STATIC VOID_T __build_body(lv_obj_t *parent)
{
    lv_obj_t *body = lv_obj_create(parent);
    lv_obj_set_size(body, SCR_W, BODY_H);
    lv_obj_set_pos(body, 0, BODY_TOP);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_set_style_bg_color(body, lv_color_white(), 0);
    lv_obj_set_style_border_width(body, 0, 0);
    lv_obj_set_style_radius(body, 0, 0);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);

    /* idle view -- four stacked labels (compact to make room for entries) */
    body_status_msg = lv_label_create(body);
    lv_label_set_long_mode(body_status_msg, LV_LABEL_LONG_DOT);
    lv_obj_set_width(body_status_msg, SCR_W - 16);
    lv_obj_set_style_text_font(body_status_msg, BUDDY_FONT_CONTENT, 0);
    lv_obj_set_style_text_color(body_status_msg, lv_color_black(), 0);
    lv_obj_set_pos(body_status_msg, 8, 2);

    body_sessions = lv_label_create(body);
    lv_obj_set_style_text_font(body_sessions, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(body_sessions, lv_color_black(), 0);
    lv_obj_set_pos(body_sessions, 8, 22);

    body_tokens = lv_label_create(body);
    lv_obj_set_style_text_font(body_tokens, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(body_tokens, lv_color_black(), 0);
    lv_obj_set_pos(body_tokens, 8, 36);

    body_owner = lv_label_create(body);
    lv_obj_set_style_text_font(body_owner, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(body_owner, lv_color_black(), 0);
    lv_obj_set_pos(body_owner, 8, 50);

    /* entries panel: 4 visible rows of 14 px font, bottom of body, scrollable
     * via the up/down joystick. Always hidden until entries_count > 0. */
    for (uint32_t i = 0; i < BUDDY_ENTRIES_VISIBLE; i++) {
        entries_lines[i] = lv_label_create(body);
        lv_label_set_long_mode(entries_lines[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(entries_lines[i], SCR_W - 16);
        lv_obj_set_style_text_font(entries_lines[i], BUDDY_FONT_HINT, 0);
        lv_obj_set_style_text_color(entries_lines[i], lv_color_black(), 0);
        lv_obj_set_pos(entries_lines[i], 8, (int32_t)(68U + i * 14U));
        lv_obj_add_flag(entries_lines[i], LV_OBJ_FLAG_HIDDEN);
    }

    /* pending permission card (overlays the entire body) */
    card_prompt = lv_obj_create(body);
    lv_obj_set_size(card_prompt, SCR_W - 12, BODY_H - 12);
    lv_obj_set_pos(card_prompt, 6, 6);
    lv_obj_set_style_bg_color(card_prompt, lv_color_make(0xFF, 0xF2, 0xE0), 0);
    lv_obj_set_style_border_color(card_prompt, HOT_COLOR, 0);
    lv_obj_set_style_border_width(card_prompt, 2, 0);
    lv_obj_set_style_radius(card_prompt, 4, 0);
    lv_obj_set_style_pad_all(card_prompt, 6, 0);
    lv_obj_clear_flag(card_prompt, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(card_prompt, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *card_title = lv_label_create(card_prompt);
    lv_label_set_text(card_title, "PERMISSION REQUEST");
    lv_obj_set_style_text_font(card_title, BUDDY_FONT_CONTENT, 0);
    lv_obj_set_style_text_color(card_title, HOT_COLOR, 0);
    lv_obj_align(card_title, LV_ALIGN_TOP_LEFT, 0, 0);

    card_tool = lv_label_create(card_prompt);
    lv_obj_set_style_text_font(card_tool, BUDDY_FONT_CONTENT, 0);
    lv_obj_set_style_text_color(card_tool, lv_color_black(), 0);
    lv_obj_align(card_tool, LV_ALIGN_TOP_LEFT, 0, 24);

    card_hint = lv_label_create(card_prompt);
    lv_label_set_long_mode(card_hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(card_hint, SCR_W - 32);
    lv_obj_set_style_text_font(card_hint, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(card_hint, lv_color_black(), 0);
    lv_obj_align(card_hint, LV_ALIGN_TOP_LEFT, 0, 48);

    card_id = lv_label_create(card_prompt);
    lv_obj_set_style_text_font(card_id, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(card_id, lv_color_make(0x80, 0x80, 0x80), 0);
    lv_obj_align(card_id, LV_ALIGN_BOTTOM_LEFT, 0, 0);
}

/**
 * @brief Build the footer bar that renders the current key hints.
 * @param[in] parent root screen
 * @return none
 */
STATIC VOID_T __build_footer(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, SCR_W, FOOTER_H);
    lv_obj_set_pos(bar, 0, SCR_H - FOOTER_H);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_black(), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lbl_footer = lv_label_create(bar);
    lv_label_set_text(lbl_footer, "ESC back");
    lv_obj_set_style_text_font(lbl_footer, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(lbl_footer, lv_color_white(), 0);
    lv_obj_align(lbl_footer, LV_ALIGN_LEFT_MID, 6, 0);
}

/* ---------------------------------------------------------------------------
 * Refresh / rendering (caller holds LVGL lock)
 * --------------------------------------------------------------------------- */
/**
 * @brief Format the device's current local wall-clock time into "HH:MM".
 * @param[in]  s   staged state carrying the wall-clock triple
 * @param[out] out output buffer (must hold at least 6 bytes)
 * @param[in]  n   size of out in bytes
 * @return none
 * @note All intermediate math is done in int64_t to avoid UINT32_T wrap
 *       across long uptimes (TuyaOS C security · integer overflow rule).
 *       Writes "--:--" when no time sync has been received yet.
 *       Emits a DEBUG anchor on every call to help runtime verification.
 */
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n)
{
    if (out == NULL || n < 6U) {
        return;
    }
    if (s == NULL || s->wall_epoch_s == 0) {
        (VOID_T)snprintf(out, n, "--:--");
        return;
    }

    uint64_t now_ms = tal_system_get_millisecond();
    /* Both endpoints are uint64_t; subtract first (wraps safely in 64-bit
     * unsigned), then reinterpret as signed to tolerate clock skew. */
    int64_t delta_ms = (int64_t)(now_ms - s->wall_local_ms_at_rx);
    int64_t now_epoch = s->wall_epoch_s + (delta_ms / 1000) + ((int64_t)s->wall_tz_min * 60);
    int64_t sec_of_day = now_epoch % 86400;
    if (sec_of_day < 0) {
        sec_of_day += 86400;
    }
    int hh = (int)(sec_of_day / 3600);
    int mm = (int)((sec_of_day / 60) % 60);
    (VOID_T)snprintf(out, n, "%02d:%02d", hh, mm);
    PR_DEBUG("ui clock render HH=%02d MM=%02d", hh, mm);
}

/**
 * @brief Format a single entry line as "HH:MM  <text>".
 * @param[in]  e           entry node from the ring (must be non-NULL)
 * @param[in]  abs_epoch_s per-entry wall stamp; 0 falls back to the current
 *                         device clock derived from the wall-clock triple
 * @param[out] out         output buffer
 * @param[in]  n           size of out in bytes
 * @return none
 * @note No per-entry stamps exist in the v1.0 wire protocol, so callers pass
 *       0 for abs_epoch_s and the line gets the current HH:MM (or "--:--").
 */
STATIC VOID_T __format_entry_line(const buddy_entry_t *e, uint64_t abs_epoch_s, char *out, size_t n)
{
    if (e == NULL || out == NULL || n < 8U) {
        return;
    }

    char ts[6];
    if (abs_epoch_s == 0U) {
        __format_clock(&s_staged_state, ts, sizeof(ts));
    } else {
        int64_t with_tz = (int64_t)abs_epoch_s + ((int64_t)s_staged_state.wall_tz_min * 60);
        int64_t sec_of_day = with_tz % 86400;
        if (sec_of_day < 0) {
            sec_of_day += 86400;
        }
        int hh = (int)(sec_of_day / 3600);
        int mm = (int)((sec_of_day / 60) % 60);
        (VOID_T)snprintf(ts, sizeof(ts), "%02d:%02d", hh, mm);
    }
    (VOID_T)snprintf(out, n, "%s  %s", ts, e->text);
}

/**
 * @brief Update the footer hint text based on connection / prompt state.
 * @return none
 */
STATIC VOID_T __refresh_footer_locked(VOID_T)
{
    if (lbl_footer == NULL) {
        return;
    }
    if (s_staged_state.has_prompt) {
        lv_label_set_text(lbl_footer, "ENTER=OK LEFT=deny RIGHT=always UP/DOWN=scroll ESC=back");
    } else if (s_staged_state.ble_connected) {
        lv_label_set_text(lbl_footer, "UP/DOWN=scroll JOYCON=refresh ESC=back");
    } else {
        lv_label_set_text(lbl_footer, "Waiting for Claude desktop...   ESC=back");
    }
}

/**
 * @brief Repaint the entries transcript panel from the staged ring.
 * @return none
 * @note Shows the window `[s_entries_scroll, s_entries_scroll + 4)` with
 *       slot 0 being the newest visible entry (drawn bold). Hidden labels
 *       cover slots with no backing entry.
 */
STATIC VOID_T __refresh_entries_locked(VOID_T)
{
    uint8_t count = s_staged_state.entries_count;
    uint8_t head = s_staged_state.entries_head;

    /* Clamp the scroll so the window always starts on a populated slot. */
    if (count == 0U) {
        s_entries_scroll = 0U;
    } else if (s_entries_scroll >= count) {
        s_entries_scroll = (uint8_t)(count - 1U);
    }

    for (uint32_t i = 0; i < BUDDY_ENTRIES_VISIBLE; i++) {
        if (entries_lines[i] == NULL) {
            continue;
        }
        uint8_t rel = (uint8_t)(s_entries_scroll + i);
        if (rel >= count) {
            lv_obj_add_flag(entries_lines[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        uint8_t ring_idx =
            (uint8_t)((head + BUDDY_ENTRIES_RING - rel) % BUDDY_ENTRIES_RING);

        char line[96];
        __format_entry_line(&s_staged_state.entries[ring_idx], 0U, line, sizeof(line));
        lv_label_set_text(entries_lines[i], line);
        lv_obj_set_style_text_font(entries_lines[i],
                                   (i == 0U) ? BUDDY_FONT_CONTENT : BUDDY_FONT_HINT, 0);
        lv_obj_clear_flag(entries_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Repaint all body widgets from s_staged_state.
 * @return none
 */
STATIC VOID_T __refresh_locked(VOID_T)
{
    if (ui_buddy_main_screen == NULL) {
        return;
    }

    /* header */
    if (lbl_ble) {
        lv_label_set_text(lbl_ble, s_staged_state.ble_connected ? "BLE: linked" : "BLE: -");
    }
    if (lbl_device) {
        lv_label_set_text(lbl_device, s_staged_state.device_name[0] ? s_staged_state.device_name : "Claude");
    }
    if (lbl_clock) {
        char clock_buf[6];
        __format_clock(&s_staged_state, clock_buf, sizeof(clock_buf));
        lv_label_set_text(lbl_clock, clock_buf);
    }

    const BOOL_T pending = s_staged_state.has_prompt ? TRUE : FALSE;

    /* toggle between the idle view and the prompt card */
    lv_obj_t *idle_widgets[] = {body_status_msg, body_sessions, body_tokens, body_owner};
    for (uint32_t i = 0; i < sizeof(idle_widgets) / sizeof(idle_widgets[0]); i++) {
        if (idle_widgets[i] != NULL) {
            if (pending) {
                lv_obj_add_flag(idle_widgets[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_clear_flag(idle_widgets[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }
    if (card_prompt != NULL) {
        if (pending) {
            lv_obj_clear_flag(card_prompt, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(card_prompt, LV_OBJ_FLAG_HIDDEN);
        }
    }
    /* Entries panel: always hidden under the prompt card; otherwise the
     * ring contents drive the visible rows. */
    if (pending) {
        for (uint32_t i = 0; i < BUDDY_ENTRIES_VISIBLE; i++) {
            if (entries_lines[i] != NULL) {
                lv_obj_add_flag(entries_lines[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    } else {
        __refresh_entries_locked();
    }

    if (pending) {
        if (card_tool) {
            lv_label_set_text_fmt(card_tool, "Tool:  %s",
                                  s_staged_state.prompt_tool[0] ? s_staged_state.prompt_tool : "(unknown)");
        }
        if (card_hint) {
            lv_label_set_text_fmt(card_hint, "Info:  %s", s_staged_state.prompt_hint[0] ? s_staged_state.prompt_hint : "-");
        }
        if (card_id) {
            lv_label_set_text_fmt(card_id, "id: %s", s_staged_state.prompt_id[0] ? s_staged_state.prompt_id : "-");
        }
    } else {
        if (body_status_msg) {
            const char *msg = s_staged_state.msg[0]
                                  ? s_staged_state.msg
                                  : (s_staged_state.ble_connected ? "Ready." : "Waiting for Claude desktop...");
            lv_label_set_text(body_status_msg, msg);
        }
        if (body_sessions) {
            lv_label_set_text_fmt(body_sessions, "Sessions   total %u   running %u   waiting %u",
                                  (unsigned)s_staged_state.sessions_total, (unsigned)s_staged_state.sessions_running,
                                  (unsigned)s_staged_state.sessions_waiting);
        }
        if (body_tokens) {
            lv_label_set_text_fmt(body_tokens, "Tokens     today %lu   total %lu",
                                  (unsigned long)s_staged_state.tokens_today, (unsigned long)s_staged_state.tokens);
        }
        if (body_owner) {
            lv_label_set_text_fmt(body_owner, "Owner      %s",
                                  s_staged_state.owner_name[0] ? s_staged_state.owner_name : "(not set)");
        }
    }

    __refresh_footer_locked();
}

/* ---------------------------------------------------------------------------
 * Button handling
 * --------------------------------------------------------------------------- */
/**
 * @brief Send a permission decision for the currently staged prompt.
 * @param[in] decision "once" / "always" / "deny"
 * @return none
 */
STATIC VOID_T __send_decision(const char *decision)
{
    if (decision == NULL) {
        return;
    }
    if (!s_staged_state.has_prompt || s_staged_state.prompt_id[0] == '\0') {
        return;
    }
    (VOID_T)buddy_ble_send_permission(s_staged_state.prompt_id, decision);
    printf("[buddy_main] decision=%s id=%s\n", decision, s_staged_state.prompt_id);

    /* Optimistically clear the prompt so the card disappears immediately,
     * the next heartbeat will confirm. */
    s_staged_state.has_prompt = FALSE;
    s_staged_state.prompt_id[0] = '\0';
    s_staged_state.prompt_tool[0] = '\0';
    s_staged_state.prompt_hint[0] = '\0';
    __refresh_locked();
}

/**
 * @brief LVGL key event handler for the main screen.
 * @param[in] e key event
 * @return none
 * @note Modeless button map (see doc/UI_INTERACTION.md):
 *         ENTER  -> permission:"once"       (prompt only)
 *         LEFT   -> permission:"deny"       (prompt only)
 *         RIGHT  -> permission:"always"     (prompt only)
 *         UP     -> scroll entries window back (older)
 *         DOWN   -> scroll entries window forward (newer)
 *         JOYCON -> send {"cmd":"status"} to refresh
 *         ESC    -> return to the previous screen
 *       Scrolling is always available, including while a prompt is pending,
 *       because approve/deny/always and scroll live on disjoint inputs.
 */
STATIC VOID_T __keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    switch (key) {
    case KEY_ENTER:
        __send_decision("once");
        break;

    case KEY_LEFT:
        __send_decision("deny");
        break;

    case KEY_RIGHT:
        __send_decision("always");
        break;

    case KEY_UP:
        /* Event callbacks run on the LVGL thread with the display lock
         * already held; do not re-lock. */
        if ((uint32_t)s_entries_scroll + BUDDY_ENTRIES_VISIBLE < (uint32_t)s_staged_state.entries_count) {
            s_entries_scroll = (uint8_t)(s_entries_scroll + 1U);
            PR_DEBUG("ui scroll idx=%d count=%d", (int)s_entries_scroll, (int)s_staged_state.entries_count);
            __refresh_locked();
        }
        break;

    case KEY_DOWN:
        if (s_entries_scroll > 0U) {
            s_entries_scroll = (uint8_t)(s_entries_scroll - 1U);
            PR_DEBUG("ui scroll idx=%d count=%d", (int)s_entries_scroll, (int)s_staged_state.entries_count);
            __refresh_locked();
        }
        break;

    case KEY_JOYCON:
        if (s_staged_state.ble_connected) {
            /* Nudge Claude into resending its current snapshot - the
             * desktop replies with {"ack":"status"} + next heartbeat. */
            (VOID_T)buddy_ble_send_cmd("status");
        }
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
/**
 * @brief Screen manager init hook; builds widgets and registers key cb.
 * @return none
 */
STATIC VOID_T __main_screen_init(VOID_T)
{
    ui_buddy_main_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_buddy_main_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(ui_buddy_main_screen, lv_color_white(), 0);
    lv_obj_set_style_pad_all(ui_buddy_main_screen, 0, 0);
    lv_obj_clear_flag(ui_buddy_main_screen, LV_OBJ_FLAG_SCROLLABLE);

    __build_header(ui_buddy_main_screen);
    __build_body(ui_buddy_main_screen);
    __build_footer(ui_buddy_main_screen);

    /* Pull the latest snapshot from the BLE bridge so we paint fresh. */
    buddy_tama_state_t snap;
    buddy_ble_snapshot(&snap);
    s_staged_state = snap;
    __refresh_locked();

    lv_obj_add_event_cb(ui_buddy_main_screen, __keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_buddy_main_screen);
    lv_group_focus_obj(ui_buddy_main_screen);

    printf("[%s] init done\n", buddy_main_screen.name);
}

/**
 * @brief Screen manager deinit hook; clears cached widget pointers.
 * @return none
 */
STATIC VOID_T __main_screen_deinit(VOID_T)
{
    if (ui_buddy_main_screen != NULL) {
        lv_obj_remove_event_cb(ui_buddy_main_screen, __keyboard_event_cb);
        lv_group_remove_obj(ui_buddy_main_screen);
    }
    lbl_title = lbl_ble = lbl_clock = lbl_device = NULL;
    body_status_msg = body_sessions = body_tokens = body_owner = NULL;
    card_prompt = card_tool = card_hint = card_id = NULL;
    for (uint32_t i = 0; i < BUDDY_ENTRIES_VISIBLE; i++) {
        entries_lines[i] = NULL;
    }
    s_entries_scroll = 0;
    lbl_footer = NULL;
}
