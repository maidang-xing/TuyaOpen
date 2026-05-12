/**
 * @file startup_screen.c
 * @brief Buddy project startup screen - shows splash then transitions to buddy main screen.
 *
 * @copyright Copyright (c) 2025 Tuya Inc.
 */

#include "startup_screen.h"
#include "buddy_main_screen.h"
#include <stdio.h>

#define SCREEN_TITLE_FONT   &lv_font_montserrat_24
#define SCREEN_CONTENT_FONT &lv_font_montserrat_14

static lv_obj_t *ui_startup_screen;
static lv_timer_t *timer;

Screen_t startup_screen = {
    .init = startup_screen_init,
    .deinit = startup_screen_deinit,
    .screen_obj = &ui_startup_screen,
    .name = "Startup",
};

static void startup_timer_cb(lv_timer_t *timer)
{
    screen_load(&buddy_main_screen);
}

static void keyboard_event_cb(lv_event_t *e)
{
    uint32_t key = lv_event_get_key(e);
    printf("[%s] Keyboard event received: key = %d\n", startup_screen.name, key);
}

void startup_screen_init(void)
{
    ui_startup_screen = lv_obj_create(NULL);
    lv_obj_set_size(ui_startup_screen, 384, 168);
    lv_obj_set_style_bg_color(ui_startup_screen, lv_color_white(), 0);

    lv_obj_t *title = lv_label_create(ui_startup_screen);
    lv_label_set_text(title, "Claude Buddy");
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_text_font(title, SCREEN_TITLE_FONT, 0);

    lv_obj_t *subtitle = lv_label_create(ui_startup_screen);
    lv_label_set_text(subtitle, "T5AI Pocket");
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 20);
    lv_obj_set_style_text_font(subtitle, SCREEN_CONTENT_FONT, 0);

    timer = lv_timer_create(startup_timer_cb, 1000, NULL);
    lv_obj_add_event_cb(ui_startup_screen, keyboard_event_cb, LV_EVENT_KEY, NULL);
    lv_group_add_obj(lv_group_get_default(), ui_startup_screen);
}

void startup_screen_deinit(void)
{
    if (ui_startup_screen) {
        printf("deinit startup screen\n");
        lv_obj_remove_event_cb(ui_startup_screen, keyboard_event_cb);
        lv_group_remove_obj(ui_startup_screen);
    }
    if (timer) {
        lv_timer_del(timer);
        timer = NULL;
    }
}
