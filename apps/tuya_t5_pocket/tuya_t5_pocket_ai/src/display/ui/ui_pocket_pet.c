/**
 * @file ui_pocket_pet.c
 * @brief Implementation of the GUI for the AI Pocket Pet interface
 *
 * This source file provides the implementation for initializing and managing
 * the GUI components of an AI Pocket Pet interface. It includes functions
 * to initialize the display, create the pet's environment, handle user
 * interactions, and manage various display states.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_cloud_types.h"

#if defined(ENABLE_GUI_POCKET) && (ENABLE_GUI_POCKET == 1)

#include "ui_display.h"
#include "ai-pocket-pet/lv_demo_ai_pocket_pet.h"
#include "ai-pocket-pet/lv_keyboard_widget.h"

#include "font_awesome_symbols.h"
#include "lvgl.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
// Theme color structure
typedef struct {
    lv_color_t background;
    lv_color_t text;
    lv_color_t chat_background;
    lv_color_t user_bubble;
    lv_color_t assistant_bubble;
    lv_color_t system_bubble;
    lv_color_t system_text;
    lv_color_t border;
    lv_color_t low_battery;
} APP_THEME_COLORS_T;

typedef struct {
    lv_obj_t *container;
    lv_obj_t *status_bar;
    lv_obj_t *content;
    lv_obj_t *emotion_label;
    lv_obj_t *chat_message_label;
    lv_obj_t *status_label;
    lv_obj_t *network_label;
    lv_obj_t *notification_label;
    lv_obj_t *mute_label;
    lv_obj_t *chat_mode_label;
} APP_UI_T;

typedef struct {
    APP_UI_T ui;
    APP_THEME_COLORS_T theme;

    UI_FONT_T font;

    lv_timer_t *notification_tm;
} APP_POCKET_PET_UI_T;

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static APP_POCKET_PET_UI_T sg_ui = {0};

/***********************************************************
***********************function define**********************
***********************************************************/
static void __ui_light_theme_init(APP_THEME_COLORS_T *theme)
{
    if (theme == NULL) {
        return;
    }

    theme->background = lv_color_white();
    theme->text = lv_color_black();
    theme->chat_background = lv_color_hex(1);
    theme->user_bubble = lv_color_hex(0);
    theme->assistant_bubble = lv_color_white();
    theme->system_bubble = lv_color_hex(0);
    theme->system_text = lv_color_hex(1);
    theme->border = lv_color_hex(0);
    theme->low_battery = lv_color_black();
}

static __attribute__((unused)) void __ui_dark_theme_init(APP_THEME_COLORS_T *theme)
{
    if (theme == NULL) {
        return;
    }

    theme->background = lv_color_hex(1);
    theme->text = lv_color_white();
    theme->chat_background = lv_color_hex(1);
    theme->user_bubble = lv_color_hex(0);
    theme->assistant_bubble = lv_color_hex(1);
    theme->system_bubble = lv_color_hex(1);
    theme->system_text = lv_color_hex(0);
    theme->border = lv_color_hex(0);
    theme->low_battery = lv_color_hex(0);
}

int __ui_font_init(UI_FONT_T *ui_font)
{
    if (ui_font == NULL) {
        return -1;
    }

    sg_ui.font.text = ui_font->text;
    sg_ui.font.icon = ui_font->icon;
    sg_ui.font.emoji = ui_font->emoji;
    sg_ui.font.emoji_list = ui_font->emoji_list;

    return 0;
}

static void __ui_notification_timeout_cb(lv_timer_t *timer)
{
    lv_timer_del(sg_ui.notification_tm);
    sg_ui.notification_tm = NULL;

    lv_obj_add_flag(sg_ui.ui.notification_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(sg_ui.ui.status_label, LV_OBJ_FLAG_HIDDEN);
}

int ui_init(UI_FONT_T *ui_font)
{
    // Theme init
    __ui_light_theme_init(&sg_ui.theme);

    // Font init
    __ui_font_init(ui_font);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, sg_ui.font.text, 0);
    lv_obj_set_style_text_color(screen, sg_ui.theme.text, 0);
    lv_obj_set_style_bg_color(screen, sg_ui.theme.background, 0);

    // Initialize the AI Pocket Pet demo
    printf("Initializing AI Pocket Pet UI...\n");
    lv_demo_ai_pocket_pet();

    return 0;
}

void ui_set_user_msg(const char *text)
{
    // This function can be adapted to control the pet's behavior based on user messages
}

void ui_set_assistant_msg(const char *text)
{
    // This function can be adapted to control the pet's behavior based on assistant messages
}

void ui_set_system_msg(const char *text)
{
    // This function can be adapted to display system messages in the pet's UI
}

void ui_set_emotion(const char *emotion)
{
    // This function can be adapted to change the pet's emotional state
}

void ui_set_status(const char *status)
{
    // This function can be adapted to display status updates in the pet's UI
}

void ui_set_notification(const char *notification)
{
    if (sg_ui.ui.notification_label == NULL) {
        return;
    }

    lv_label_set_text(sg_ui.ui.notification_label, notification);
    lv_obj_add_flag(sg_ui.ui.status_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(sg_ui.ui.notification_label, LV_OBJ_FLAG_HIDDEN);
    if (NULL == sg_ui.notification_tm) {
        sg_ui.notification_tm = lv_timer_create(__ui_notification_timeout_cb, 3000, NULL);
    } else {
        lv_timer_reset(sg_ui.notification_tm);
    }
}

void ui_set_network(char *wifi_icon)
{
    // This function can be adapted to display network status in the pet's UI
}

void ui_set_chat_mode(const char *chat_mode)
{
    // This function can be adapted to switch between different interaction modes
}

void ui_set_status_bar_pad(int32_t value)
{
    if (sg_ui.ui.status_bar == NULL) {
        return;
    }

    lv_obj_set_style_pad_left(sg_ui.ui.status_bar, value, 0);
    lv_obj_set_style_pad_right(sg_ui.ui.status_bar, value, 0);
}

#endif