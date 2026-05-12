/**
 * @file buddy_indev.c
 * @brief Input device initialization for Claude Buddy T5AI Pocket.
 *
 * Manages hardware buttons and joystick, mapping physical inputs to
 * LVGL key codes for the buddy UI navigation system.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tuya_iot.h"

#include "tdl_button_manage.h"
#include "tdl_joystick_manage.h"
#include "lv_vendor.h"
#include "app_display.h"

#define MENU_BUTTON_NAME "btn_menu"

typedef struct {
    char *name;
    TDL_BUTTON_TOUCH_EVENT_E event;
    uint32_t  key_tp;
} button_code_map_t;

button_code_map_t disp_btn_code_map[] = {
    {"btn_enter", TDL_BUTTON_PRESS_DOWN, KEY_ENTER},
    {"btn_esc",   TDL_BUTTON_PRESS_DOWN, KEY_ESC},
};

typedef struct {
    TDL_JOYSTICK_TOUCH_EVENT_E event;
    uint32_t key_tp;
} joystick_code_map_t;

joystick_code_map_t disp_joystick_code_map[] = {
    { TDL_JOYSTICK_UP, KEY_UP},
    { TDL_JOYSTICK_DOWN, KEY_DOWN},
    { TDL_JOYSTICK_LEFT, KEY_LEFT},
    { TDL_JOYSTICK_RIGHT, KEY_RIGHT},
    { TDL_JOYSTICK_BUTTON_PRESS_DOWN, KEY_JOYCON},
};

static uint32_t sg_cur_key = 0;

static void menu_button_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E event, void *arg)
{
    (void)arg;

    // if (TDL_BUTTON_PRESS_REPEAT == event) {
    //     PR_DEBUG("Reset ctrl data");
    //     tuya_iot_reset(tuya_iot_client_get());
    // } else if (TDL_BUTTON_LONG_PRESS_START == event) {
    //     game_pet_reset();
    // }
}

static void disp_button_cb(char *name, TDL_BUTTON_TOUCH_EVENT_E event, void *arg)
{
    uint32_t i;
    (void)arg;

    for (i = 0; i < CNTSOF(disp_btn_code_map); i++) {
        if (strcmp(name, disp_btn_code_map[i].name) == 0 && event == disp_btn_code_map[i].event) {
            PR_DEBUG("Button pressed: %s, event: %d, key type: %d", name, event, disp_btn_code_map[i].key_tp);
            sg_cur_key = disp_btn_code_map[i].key_tp;
            break;
        }
    }
}

static void disp_joystick_cb(char *name, TDL_JOYSTICK_TOUCH_EVENT_E event, void *arg)
{
    uint32_t i;
    (void)arg;

    for (i = 0; i < CNTSOF(disp_joystick_code_map); i++) {
        if (event == disp_joystick_code_map[i].event) {
            PR_DEBUG("Joystick event: %d, key type: %d", event, disp_joystick_code_map[i].key_tp);
            sg_cur_key = disp_joystick_code_map[i].key_tp;
            break;
        }
    }
}

static void keypad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    data->key = sg_cur_key;

    if (sg_cur_key != 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        sg_cur_key = 0;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void buddy_indev_init(void)
{
    TDL_BUTTON_CFG_T button_cfg = {.long_start_valid_time = 3000,
                                   .long_keep_timer = 1000,
                                   .button_debounce_time = 50,
                                   .button_repeat_valid_count = 3,
                                   .button_repeat_valid_time = 500};
    TDL_BUTTON_HANDLE button_hdl = NULL;

    tdl_button_create(MENU_BUTTON_NAME, &button_cfg, &button_hdl);
    tdl_button_event_register(button_hdl, TDL_BUTTON_PRESS_REPEAT, menu_button_cb);
    tdl_button_event_register(button_hdl, TDL_BUTTON_LONG_PRESS_START, menu_button_cb);

    for (uint32_t i = 0; i < CNTSOF(disp_btn_code_map); i++) {
        tdl_button_create(disp_btn_code_map[i].name, &button_cfg, &button_hdl);
        tdl_button_event_register(button_hdl, disp_btn_code_map[i].event, disp_button_cb);
    }

    TDL_JOYSTICK_CFG_T joystick_cfg = {
        .button_cfg = {.long_start_valid_time = 3000,
                       .long_keep_timer = 1000,
                       .button_debounce_time = 50,
                       .button_repeat_valid_count = 2,
                       .button_repeat_valid_time = 500},
        .adc_cfg =
            {
                .adc_max_val = 8192,
                .adc_min_val = 0,
                .normalized_range = 10,
                .sensitivity = 2,
            },
    };

    TDL_JOYSTICK_HANDLE sg_joystick_hdl = NULL;

    tdl_joystick_create(JOYSTICK_NAME, &joystick_cfg, &sg_joystick_hdl);

    for (uint32_t i = 0; i < CNTSOF(disp_joystick_code_map); i++) {
        tdl_joystick_event_register(sg_joystick_hdl, disp_joystick_code_map[i].event, disp_joystick_cb);
    }

    lv_indev_t *indev_keypad = NULL;
    indev_keypad = lv_indev_create();
    lv_indev_set_type(indev_keypad, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev_keypad, keypad_read);
    lv_group_t *group = lv_group_get_default();
    if (group == NULL) {
        group = lv_group_create();
        lv_group_set_default(group);
    }
    lv_indev_set_group(indev_keypad, group);
    lv_group_set_default(group);
}
