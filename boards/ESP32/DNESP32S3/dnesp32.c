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
#include "tdd_xl9555_io.h"
#include "lcd_st7789.h"
#include "tdd_audio_codec_bus.h"
#include "tdd_audio_es8388_codec.h"
#include "i2s_common.h"
#include "driver/i2c_master.h"
#include "i2s_std.h"
#include "esp_log.h"
#include "esp_err.h"
#include "clk_tree_defs.h"
#include "i2s_types.h"

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
static TDD_AUDIO_I2S_TX_HANDLE i2s_rx_handle = NULL;
static TDD_AUDIO_I2S_TX_HANDLE i2s_tx_handle = NULL;
static i2c_master_bus_handle_t i2c_bus = NULL;


/***********************************************************
***********************function define**********************
***********************************************************/
OPERATE_RET app_audio_driver_init(const char *name)
{
    TDD_AUDIO_CODEC_BUS_CFG_T bus_cfg = {
        .i2c_id = I2C_NUM,
        .i2c_sda_io = I2C_SDA_IO,
        .i2c_scl_io = I2C_SCL_IO,
        .i2s_id = I2S_NUM,
        .i2s_mck_io = I2S_MCK_IO,
        .i2s_bck_io = I2S_BCK_IO,
        .i2s_ws_io = I2S_WS_IO,
        .i2s_do_io = I2S_DO_IO,
        .i2s_di_io = I2S_DI_IO,
        .dma_desc_num = AUDIO_CODEC_DMA_DESC_NUM,
        .dma_frame_num = AUDIO_CODEC_DMA_FRAME_NUM,
        .sample_rate = I2S_OUTPUT_SAMPLE_RATE,
    };
    
    tdd_audio_codec_bus_i2c_new(bus_cfg, &i2c_bus);
    tdd_audio_codec_bus_i2s_new(bus_cfg, &i2s_tx_handle, &i2s_rx_handle);
    
    /* P10, P11, P12, P13, and P14 are inputs, other pins are outputs --> 1111 0000 0000 0011 Note: 0 is output, 1 is input */
    tdd_xl9555_io_init(i2c_bus, 0xF003);
    /* Turn off buzzer */
    tdd_xl9555_io_set(BEEP_IO, 1);
    /* Turn on Speaker */
    tdd_xl9555_io_set(SPK_EN_IO, 0);
    /* Turn on LCD backlight*/
    tdd_xl9555_io_set(SLCD_PWR_IO, 1);

    TDD_AUDIO_ES8388_CODEC_T codec = {
        .i2c_id = I2C_NUM,
        .i2c_handle = i2c_bus,
        .i2s_id = I2S_NUM,
        .i2s_tx_handle = i2s_tx_handle,
        .i2s_rx_handle = i2s_rx_handle,
        .mic_sample_rate = I2S_INPUT_SAMPLE_RATE,
        .spk_sample_rate = I2S_OUTPUT_SAMPLE_RATE,
        .es8388_addr = AUDIO_CODEC_ES8388_ADDR,
        .pa_pin = -1, /* Speaker power is controled by XL9555 */
        .defaule_volume = 80,
    };

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
