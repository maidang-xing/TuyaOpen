/**
 * @file bread_compact_wifi.c
 * @brief bread_compact_wifi module is used to
 * @version 0.1
 * @date 2025-04-23
 */

#include "tuya_cloud_types.h"

#include "app_board_api.h"

#include "board_config.h"

#include "tdd_audio_no_codec.h"

#include "tkl_memory.h"

#include "oled_ssd1306.h"

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/

/***********************************************************
***********************function define**********************
***********************************************************/

OPERATE_RET app_audio_driver_init(const char *name)
{
    TDD_AUDIO_NO_CODEC_T cfg = {0};
    cfg.i2s_id = 0;
    cfg.mic_sample_rate = 16000;
    cfg.spk_sample_rate = 16000;

    return tdd_audio_no_codec_register(name, cfg);
}

int board_display_init(void)
{
    return oled_ssd1306_init();
}

void *board_display_get_panel_io_handle(void)
{
    return oled_ssd1306_get_panel_io_handle();
}

void *board_display_get_panel_handle(void)
{
    return oled_ssd1306_get_panel_handle();
}
