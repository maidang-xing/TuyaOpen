/**
 * @file buddy_main_screen.c
 * @brief Claude Desktop Buddy 主屏（M1-UI 版本）。
 *
 * 左半 persona 画布（ascii_persona 或 buddy_gif_stub）+ 右半文字面板
 * （msg / sessions / tokens / owner / entries）。收到 prompt 时右半 +
 * 左半被一张覆盖全 body 的 PERMISSION REQUEST 卡片替换。persona 状态
 * 由 BLE 快照派生，并通过 buddy_led 同步 LED 视觉状态。
 *
 * persona_id 经 tal_kv 持久化（key: "buddy.pid"），复位后恢复。
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

/* 左侧 persona 画布占位；高度对齐 body。 */
#define PERSONA_X    0
#define PERSONA_Y    BODY_TOP
#define PERSONA_W    184
#define PERSONA_H    BODY_H

/* 右侧文本面板。 */
#define TEXT_X       (PERSONA_W + 4)
#define TEXT_Y       BODY_TOP
#define TEXT_W       (SCR_W - TEXT_X - 4)
#define TEXT_H       BODY_H

#define HOT_COLOR    lv_color_make(0xFA, 0x20, 0x20)
#define OK_COLOR     lv_color_make(0x20, 0xA0, 0x20)
#define ACCENT_COLOR lv_color_make(0x10, 0x60, 0xC0)

/* Persona tick 周期（ms）。参考项目在 ~30Hz，本机 OLED 刷新 10Hz 足够。 */
#define PERSONA_TICK_MS  100U

/* KV key name（持久化 persona_id）。 */
#define KV_KEY_PERSONA_ID "buddy.pid"

/* 瞬态状态保持时长（ms）。 */
#define CELEBRATE_HOLD_MS  3000U
#define HEART_HOLD_MS      2000U

/* 条目显示行数。 */
#define BUDDY_ENTRIES_VISIBLE 4

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC lv_obj_t *ui_buddy_main_screen = NULL;

/* header */
STATIC lv_obj_t *lbl_title;
STATIC lv_obj_t *lbl_ble;
STATIC lv_obj_t *lbl_clock;
STATIC lv_obj_t *lbl_device;

/* body 右侧：idle view */
STATIC lv_obj_t *body_right;            /* 容器 */
STATIC lv_obj_t *body_status_msg;
STATIC lv_obj_t *body_sessions;
STATIC lv_obj_t *body_tokens;
STATIC lv_obj_t *body_owner;
STATIC lv_obj_t *entries_lines[BUDDY_ENTRIES_VISIBLE];

/* body 覆盖层：permission card */
STATIC lv_obj_t *card_prompt;
STATIC lv_obj_t *card_tool;
STATIC lv_obj_t *card_hint;
STATIC lv_obj_t *card_id;

/* footer */
STATIC lv_obj_t *lbl_footer;

/* 最近一次渲染的状态快照。 */
STATIC buddy_tama_state_t s_staged_state = {0};

/* 由 state 派生的 UI 本地字段（不回写 snapshot，避免抖动）。 */
STATIC uint8_t                s_persona_id    = 0;
STATIC buddy_persona_state_e  s_persona_state = BUDDY_PERSONA_STATE_SLEEP;
STATIC buddy_led_state_e      s_led_state     = BUDDY_LED_STATE_OFF;

/* Entries 窗口偏移：0 = 最新在顶。 */
STATIC uint8_t s_entries_scroll = 0;

/* 瞬态状态持续计时（ms，tal_system_get_millisecond）。 */
STATIC uint64_t s_celebrate_until_ms = 0;
STATIC uint64_t s_heart_until_ms     = 0;
STATIC BOOL_T   s_prev_has_prompt    = FALSE;
STATIC BOOL_T   s_prev_completed    = FALSE;

/* persona 动画 tick 定时器。 */
STATIC lv_timer_t *s_persona_timer = NULL;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __main_screen_init(VOID_T);
STATIC VOID_T __main_screen_deinit(VOID_T);
STATIC VOID_T __keyboard_event_cb(lv_event_t *e);
STATIC VOID_T __build_header(lv_obj_t *parent);
STATIC VOID_T __build_body_right(lv_obj_t *parent);
STATIC VOID_T __build_footer(lv_obj_t *parent);
STATIC VOID_T __build_prompt_card(lv_obj_t *parent);
STATIC VOID_T __refresh_locked(VOID_T);
STATIC VOID_T __refresh_footer_locked(VOID_T);
STATIC VOID_T __refresh_entries_locked(VOID_T);
STATIC VOID_T __send_decision(const char *decision);
STATIC VOID_T __format_clock(const buddy_tama_state_t *s, char *out, size_t n);
STATIC VOID_T __format_entry_line(const buddy_entry_t *e, uint64_t abs_epoch_s, char *out, size_t n);
STATIC VOID_T __derive_persona_state(VOID_T);
STATIC VOID_T __persona_timer_cb(lv_timer_t *t);
STATIC VOID_T __persona_cycle(int8_t delta);
STATIC VOID_T __persist_persona_id(uint8_t id);
STATIC uint8_t __load_persona_id(VOID_T);
STATIC buddy_led_state_e __led_state_from_persona(buddy_persona_state_e s);

Screen_t buddy_main_screen = {
    .init       = __main_screen_init,
    .deinit     = __main_screen_deinit,
    .screen_obj = &ui_buddy_main_screen,
    .name       = "buddy_main_screen",
    .state_data = NULL,
};

/* ---------------------------------------------------------------------------
 * Public entry points
 * --------------------------------------------------------------------------- */
/**
 * @brief 暂存新的快照并在屏幕已加载时重绘。
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
 * @brief 顶栏（深色背景：标题 / 时钟 / BLE 状态 / 设备名）。
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
 * @brief 右侧文字面板（msg / sessions / tokens / owner / entries）。
 */
STATIC VOID_T __build_body_right(lv_obj_t *parent)
{
    body_right = lv_obj_create(parent);
    lv_obj_set_size(body_right, TEXT_W, TEXT_H);
    lv_obj_set_pos(body_right, TEXT_X, TEXT_Y);
    lv_obj_set_style_pad_all(body_right, 2, 0);
    lv_obj_set_style_bg_color(body_right, lv_color_white(), 0);
    lv_obj_set_style_border_width(body_right, 0, 0);
    lv_obj_set_style_radius(body_right, 0, 0);
    lv_obj_clear_flag(body_right, LV_OBJ_FLAG_SCROLLABLE);

    body_status_msg = lv_label_create(body_right);
    lv_label_set_long_mode(body_status_msg, LV_LABEL_LONG_DOT);
    lv_obj_set_width(body_status_msg, TEXT_W - 6);
    lv_obj_set_style_text_font(body_status_msg, BUDDY_FONT_CONTENT, 0);
    lv_obj_set_style_text_color(body_status_msg, lv_color_black(), 0);
    lv_obj_set_pos(body_status_msg, 2, 0);

    body_sessions = lv_label_create(body_right);
    lv_label_set_long_mode(body_sessions, LV_LABEL_LONG_DOT);
    lv_obj_set_width(body_sessions, TEXT_W - 6);
    lv_obj_set_style_text_font(body_sessions, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(body_sessions, lv_color_black(), 0);
    lv_obj_set_pos(body_sessions, 2, 20);

    body_tokens = lv_label_create(body_right);
    lv_label_set_long_mode(body_tokens, LV_LABEL_LONG_DOT);
    lv_obj_set_width(body_tokens, TEXT_W - 6);
    lv_obj_set_style_text_font(body_tokens, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(body_tokens, lv_color_black(), 0);
    lv_obj_set_pos(body_tokens, 2, 34);

    body_owner = lv_label_create(body_right);
    lv_label_set_long_mode(body_owner, LV_LABEL_LONG_DOT);
    lv_obj_set_width(body_owner, TEXT_W - 6);
    lv_obj_set_style_text_font(body_owner, BUDDY_FONT_HINT, 0);
    lv_obj_set_style_text_color(body_owner, lv_color_black(), 0);
    lv_obj_set_pos(body_owner, 2, 48);

    for (uint32_t i = 0; i < BUDDY_ENTRIES_VISIBLE; i++) {
        entries_lines[i] = lv_label_create(body_right);
        lv_label_set_long_mode(entries_lines[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(entries_lines[i], TEXT_W - 6);
        lv_obj_set_style_text_font(entries_lines[i], BUDDY_FONT_HINT, 0);
        lv_obj_set_style_text_color(entries_lines[i], lv_color_black(), 0);
        lv_obj_set_pos(entries_lines[i], 2, (int32_t)(68U + i * 14U));
        lv_obj_add_flag(entries_lines[i], LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief 权限卡（覆盖整个 body）。
 */
STATIC VOID_T __build_prompt_card(lv_obj_t *parent)
{
    card_prompt = lv_obj_create(parent);
    lv_obj_set_size(card_prompt, SCR_W - 12, BODY_H - 12);
    lv_obj_set_pos(card_prompt, 6, BODY_TOP + 6);
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
 * @brief 底栏（深色背景：键位提示）。
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
 * Refresh / rendering（调用方持 LVGL 锁）
 * --------------------------------------------------------------------------- */
/**
 * @brief 把设备本地墙钟格式化为 "HH:MM"。
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
    int64_t delta_ms = (int64_t)(now_ms - s->wall_local_ms_at_rx);
    int64_t now_epoch = s->wall_epoch_s + (delta_ms / 1000) + ((int64_t)s->wall_tz_min * 60);
    int64_t sec_of_day = now_epoch % 86400;
    if (sec_of_day < 0) {
        sec_of_day += 86400;
    }
    int hh = (int)(sec_of_day / 3600);
    int mm = (int)((sec_of_day / 60) % 60);
    (VOID_T)snprintf(out, n, "%02d:%02d", hh, mm);
}

/**
 * @brief 格式化 entry 行："HH:MM  <text>"。
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
 * @brief 刷新底栏提示。
 */
STATIC VOID_T __refresh_footer_locked(VOID_T)
{
    if (lbl_footer == NULL) {
        return;
    }
    if (s_staged_state.has_prompt) {
        lv_label_set_text(lbl_footer,
                          "ENTER=OK LEFT=deny RIGHT=always UP/DOWN=scroll ESC=back");
    } else if (s_staged_state.ble_connected) {
        lv_label_set_text(lbl_footer,
                          "LEFT/RIGHT=persona UP/DOWN=scroll JOYCON=status ESC=back");
    } else {
        lv_label_set_text(lbl_footer, "Waiting for Claude desktop...   ESC=back");
    }
}

/**
 * @brief 刷新 entries 面板。
 */
STATIC VOID_T __refresh_entries_locked(VOID_T)
{
    uint8_t count = s_staged_state.entries_count;
    uint8_t head = s_staged_state.entries_head;

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
 * @brief 根据 BLE 快照 + 瞬态计时派生 persona_state / led_state。
 */
STATIC VOID_T __derive_persona_state(VOID_T)
{
    uint64_t now_ms = tal_system_get_millisecond();

    /* 触发 CELEBRATE：recently_completed 从 FALSE 跳到 TRUE。 */
    if (s_staged_state.recently_completed && !s_prev_completed) {
        s_celebrate_until_ms = now_ms + CELEBRATE_HOLD_MS;
        PR_DEBUG("persona trigger CELEBRATE hold=%ums", (unsigned)CELEBRATE_HOLD_MS);
    }
    s_prev_completed = s_staged_state.recently_completed ? TRUE : FALSE;

    /* 触发 HEART：has_prompt 从 TRUE 跳到 FALSE（"审批完成"）。 */
    if (!s_staged_state.has_prompt && s_prev_has_prompt) {
        s_heart_until_ms = now_ms + HEART_HOLD_MS;
        PR_DEBUG("persona trigger HEART hold=%ums", (unsigned)HEART_HOLD_MS);
    }
    s_prev_has_prompt = s_staged_state.has_prompt ? TRUE : FALSE;

    buddy_persona_state_e next;
    if (!s_staged_state.ble_connected) {
        next = BUDDY_PERSONA_STATE_SLEEP;
    } else if (s_staged_state.has_prompt) {
        next = BUDDY_PERSONA_STATE_ATTENTION;
    } else if (now_ms < s_heart_until_ms) {
        next = BUDDY_PERSONA_STATE_HEART;
    } else if (now_ms < s_celebrate_until_ms) {
        next = BUDDY_PERSONA_STATE_CELEBRATE;
    } else if (s_staged_state.sessions_running > 0U) {
        next = BUDDY_PERSONA_STATE_BUSY;
    } else {
        next = BUDDY_PERSONA_STATE_IDLE;
    }

    if (next != s_persona_state) {
        PR_DEBUG("persona state %d -> %d (ble=%d prompt=%d run=%u)",
                 (int)s_persona_state, (int)next,
                 (int)s_staged_state.ble_connected,
                 (int)s_staged_state.has_prompt,
                 (unsigned)s_staged_state.sessions_running);
        s_persona_state = next;
    }
    s_staged_state.persona_state = s_persona_state;

    buddy_led_state_e led = __led_state_from_persona(s_persona_state);
    if (led != s_led_state) {
        s_led_state = led;
        (VOID_T)buddy_led_set(led);
    }
    s_staged_state.led_state  = s_led_state;
    s_staged_state.persona_id = s_persona_id;
}

/**
 * @brief persona -> LED 状态映射。
 */
STATIC buddy_led_state_e __led_state_from_persona(buddy_persona_state_e s)
{
    switch (s) {
    case BUDDY_PERSONA_STATE_SLEEP:     return BUDDY_LED_STATE_OFF;
    case BUDDY_PERSONA_STATE_IDLE:      return BUDDY_LED_STATE_ON_DIM;
    case BUDDY_PERSONA_STATE_BUSY:      return BUDDY_LED_STATE_BLINK_SLOW;
    case BUDDY_PERSONA_STATE_ATTENTION: return BUDDY_LED_STATE_BLINK_FAST;
    case BUDDY_PERSONA_STATE_DIZZY:     return BUDDY_LED_STATE_BLINK_FAST;
    case BUDDY_PERSONA_STATE_CELEBRATE: return BUDDY_LED_STATE_FLASH_ONCE;
    case BUDDY_PERSONA_STATE_HEART:     return BUDDY_LED_STATE_FLASH_ONCE;
    default:                            return BUDDY_LED_STATE_OFF;
    }
}

/**
 * @brief 根据 s_staged_state 重绘所有 body widget。
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
        lv_label_set_text(lbl_device,
                          s_staged_state.device_name[0] ? s_staged_state.device_name : "Claude");
    }
    if (lbl_clock) {
        char clock_buf[6];
        __format_clock(&s_staged_state, clock_buf, sizeof(clock_buf));
        lv_label_set_text(lbl_clock, clock_buf);
    }

    __derive_persona_state();

    const BOOL_T pending = s_staged_state.has_prompt ? TRUE : FALSE;

    /* 显隐切换：prompt 时隐藏 persona + body 右侧，显示 card。 */
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
    if (body_right != NULL) {
        if (pending) {
            lv_obj_add_flag(body_right, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(body_right, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (card_prompt != NULL) {
        if (pending) {
            lv_obj_clear_flag(card_prompt, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(card_prompt, LV_OBJ_FLAG_HIDDEN);
        }
    }

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
            lv_label_set_text_fmt(card_hint, "Info:  %s",
                                  s_staged_state.prompt_hint[0] ? s_staged_state.prompt_hint : "-");
        }
        if (card_id) {
            lv_label_set_text_fmt(card_id, "id: %s",
                                  s_staged_state.prompt_id[0] ? s_staged_state.prompt_id : "-");
        }
    } else {
        if (body_status_msg) {
            const char *msg = s_staged_state.msg[0]
                                  ? s_staged_state.msg
                                  : (s_staged_state.ble_connected ? "Ready." : "Waiting for Claude desktop...");
            lv_label_set_text(body_status_msg, msg);
        }
        if (body_sessions) {
            lv_label_set_text_fmt(body_sessions, "S:%u R:%u W:%u",
                                  (unsigned)s_staged_state.sessions_total,
                                  (unsigned)s_staged_state.sessions_running,
                                  (unsigned)s_staged_state.sessions_waiting);
        }
        if (body_tokens) {
            lv_label_set_text_fmt(body_tokens, "tok %lu/%lu",
                                  (unsigned long)s_staged_state.tokens_today,
                                  (unsigned long)s_staged_state.tokens);
        }
        if (body_owner) {
            const persona_entry_t *p = persona_registry_get_by_id(s_persona_id);
            const char *pname = (p && p->persona && p->persona->name) ? p->persona->name : "?";
            lv_label_set_text_fmt(body_owner, "%s / %s",
                                  s_staged_state.owner_name[0] ? s_staged_state.owner_name : "-",
                                  pname);
        }
    }

    __refresh_footer_locked();
}

/* ---------------------------------------------------------------------------
 * Persona timer & cycling
 * --------------------------------------------------------------------------- */
/**
 * @brief lv_timer 回调：推进 persona 动画一帧。
 */
STATIC VOID_T __persona_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (ui_buddy_main_screen == NULL) {
        return;
    }
    if (s_staged_state.has_prompt) {
        return;
    }
    ascii_persona_tick(s_persona_id, s_persona_state);
}

/**
 * @brief 循环切换 persona；delta=+1/-1。
 */
STATIC VOID_T __persona_cycle(int8_t delta)
{
    uint8_t next = s_persona_id;
    if (delta > 0) {
        next = persona_registry_next_id(s_persona_id);
    } else {
        next = persona_registry_prev_id(s_persona_id);
    }
    if (next == s_persona_id) {
        return;
    }
    s_persona_id = next;
    __persist_persona_id(s_persona_id);

    const persona_entry_t *p = persona_registry_get_by_id(s_persona_id);
    PR_NOTICE("persona cycled -> id=%u name=%s",
              (unsigned)s_persona_id,
              (p && p->persona && p->persona->name) ? p->persona->name : "?");
    ascii_persona_reset_tick(0);
    __refresh_locked();
}

/**
 * @brief 从 tal_kv 读取 persona_id；失败时返回 0。
 */
STATIC uint8_t __load_persona_id(VOID_T)
{
    uint8_t  value = 0;
    uint8_t *buf   = NULL;
    size_t   len   = 0;
    if (tal_kv_get(KV_KEY_PERSONA_ID, &buf, &len) == OPRT_OK && buf != NULL) {
        if (len >= sizeof(uint8_t)) {
            value = buf[0];
        }
        tal_kv_free(buf);
    }
    if (value >= BUDDY_PERSONA_COUNT) {
        value = 0;
    }
    return value;
}

/**
 * @brief 将 persona_id 写入 tal_kv。
 */
STATIC VOID_T __persist_persona_id(uint8_t id)
{
    if (id >= BUDDY_PERSONA_COUNT) {
        return;
    }
    OPERATE_RET rt = tal_kv_set(KV_KEY_PERSONA_ID, (const uint8_t *)&id, sizeof(id));
    if (rt != OPRT_OK) {
        PR_WARN("persona kv_set failed %d", rt);
    }
}

/* ---------------------------------------------------------------------------
 * Button handling
 * --------------------------------------------------------------------------- */
/**
 * @brief 发送权限决策。
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
    PR_NOTICE("decision=%s id=%s", decision, s_staged_state.prompt_id);

    /* 乐观清空，下一心跳会确认。 */
    s_staged_state.has_prompt = FALSE;
    s_staged_state.prompt_id[0] = '\0';
    s_staged_state.prompt_tool[0] = '\0';
    s_staged_state.prompt_hint[0] = '\0';
    __refresh_locked();
}

/**
 * @brief LVGL 按键事件回调。
 */
STATIC VOID_T __keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);

    switch (key) {
    case KEY_ENTER:
        if (s_staged_state.has_prompt) {
            __send_decision("once");
        }
        break;

    case KEY_LEFT:
        if (s_staged_state.has_prompt) {
            __send_decision("deny");
        } else {
            __persona_cycle(-1);
        }
        break;

    case KEY_RIGHT:
        if (s_staged_state.has_prompt) {
            __send_decision("always");
        } else {
            __persona_cycle(+1);
        }
        break;

    case KEY_UP:
        if ((uint32_t)s_entries_scroll + BUDDY_ENTRIES_VISIBLE <
            (uint32_t)s_staged_state.entries_count) {
            s_entries_scroll = (uint8_t)(s_entries_scroll + 1U);
            __refresh_locked();
        }
        break;

    case KEY_DOWN:
        if (s_entries_scroll > 0U) {
            s_entries_scroll = (uint8_t)(s_entries_scroll - 1U);
            __refresh_locked();
        }
        break;

    case KEY_JOYCON:
        if (s_staged_state.ble_connected) {
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
 * @brief Screen_manager init 钩子：构建 widget 并注册回调。
 */
STATIC VOID_T __main_screen_init(VOID_T)
{
    ui_buddy_main_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_buddy_main_screen, SCR_W, SCR_H);
    lv_obj_set_style_bg_color(ui_buddy_main_screen, lv_color_white(), 0);
    lv_obj_set_style_pad_all(ui_buddy_main_screen, 0, 0);
    lv_obj_clear_flag(ui_buddy_main_screen, LV_OBJ_FLAG_SCROLLABLE);

    __build_header(ui_buddy_main_screen);
    __build_body_right(ui_buddy_main_screen);
    __build_prompt_card(ui_buddy_main_screen);
    __build_footer(ui_buddy_main_screen);

    /* persona canvas 挂在屏幕根（而不是 body_right），以便 prompt card
     * 能覆盖整个 body（包括 persona）。 */
    ascii_persona_attach(ui_buddy_main_screen, PERSONA_X, PERSONA_Y);

    /* 从 KV 恢复 persona_id。 */
    s_persona_id = __load_persona_id();

    /* 从 BLE 拉最新快照。 */
    buddy_tama_state_t snap;
    buddy_ble_snapshot(&snap);
    s_staged_state = snap;
    __refresh_locked();

    /* 初始化 LED（幂等）。 */
    (VOID_T)buddy_led_init();

    /* persona tick 定时器。 */
    if (s_persona_timer == NULL) {
        s_persona_timer = lv_timer_create(__persona_timer_cb, PERSONA_TICK_MS, NULL);
    }

    lv_obj_add_event_cb(ui_buddy_main_screen, __keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_buddy_main_screen);
    lv_group_focus_obj(ui_buddy_main_screen);

    PR_NOTICE("[%s] init done persona_id=%u",
              buddy_main_screen.name, (unsigned)s_persona_id);
}

/**
 * @brief Screen_manager deinit 钩子。
 */
STATIC VOID_T __main_screen_deinit(VOID_T)
{
    if (s_persona_timer != NULL) {
        lv_timer_del(s_persona_timer);
        s_persona_timer = NULL;
    }
    ascii_persona_detach();

    if (ui_buddy_main_screen != NULL) {
        lv_obj_remove_event_cb(ui_buddy_main_screen, __keyboard_event_cb);
        lv_group_remove_obj(ui_buddy_main_screen);
    }
    lbl_title = lbl_ble = lbl_clock = lbl_device = NULL;
    body_right = NULL;
    body_status_msg = body_sessions = body_tokens = body_owner = NULL;
    card_prompt = card_tool = card_hint = card_id = NULL;
    for (uint32_t i = 0; i < BUDDY_ENTRIES_VISIBLE; i++) {
        entries_lines[i] = NULL;
    }
    s_entries_scroll = 0;
    lbl_footer = NULL;
}
