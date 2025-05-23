/**
 * @file bread_compact_wifi.c
 * @brief bread_compact_wifi module is used to
 * @version 0.1
 * @date 2025-04-23
 */

#include "tuya_cloud_types.h"

#include "app_board_api.h"

#include "board_config.h"

#include "tkl_memory.h"
#include "tal_log.h"
#include "lcd_st7789.h"
#include "tdd_xl9555_io.h"
#include "tdd_audio_es8388_codec.h"


/***********************************************************
************************macro define************************
***********************************************************/
#define TAG "AUDIO_CODEC_ES8388"

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
    TDD_AUDIO_ES8388_CODEC_T codec = {
        .i2c_id = I2C_NUM,
        .i2c_sda_io = I2C_SDA_IO,                
        .i2c_scl_io = I2C_SCL_IO,                
        .i2s_id = I2S_NUM,
        .i2s_mck_io = I2S_MCK_IO,
        .i2s_bck_io = I2S_BCK_IO,
        .i2s_ws_io = I2S_WS_IO,
        .i2s_do_io = I2S_DO_IO,
        .i2s_di_io = I2S_DI_IO,
        .gpio_output_pa = -1,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .mic_sample_rate = I2S_INPUT_SAMPLE_RATE,
        .spk_sample_rate = I2S_OUTPUT_SAMPLE_RATE,
        .es8388_addr = AUDIO_CODEC_ES8388_ADDR,
        .defaule_volume = 80,
    };

    /* Turn on Speaker */
    tdd_xl9555_io_set(SPK_EN_IO, 0);

    return tdd_audio_es8388_codec_register(name, codec);
}

int board_display_init(void)
{
    return lcd_st7789_init();
}

void *board_display_get_panel_io_handle(void)
{
    return lcd_st7789_get_panel_io_handle();
}

void *board_display_get_panel_handle(void)
{
    return lcd_st7789_get_panel_handle();
}
