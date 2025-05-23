/**
 * @file tdd_audio_es8388_codec.h
 * @brief es8388 codec module
 * @version 0.1
 * @date 2025-04-08
 */

#ifndef __TDD_AUDIO_ES8388_CODEC_H__
#define __TDD_AUDIO_ES8388_CODEC_H__

#include "tuya_cloud_types.h"

#include "tdl_audio_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t i2c_id;
    uint8_t es8388_addr;
    TUYA_GPIO_NUM_E i2c_sda_io;                
    TUYA_GPIO_NUM_E i2c_scl_io;                
    uint8_t i2s_id;
    TUYA_GPIO_NUM_E i2s_mck_io;
    TUYA_GPIO_NUM_E i2s_bck_io;
    TUYA_GPIO_NUM_E i2s_ws_io;
    TUYA_GPIO_NUM_E i2s_do_io;
    TUYA_GPIO_NUM_E i2s_di_io;
    TUYA_GPIO_NUM_E gpio_output_pa;
    uint32_t dma_desc_num;
    uint32_t dma_frame_num;
    uint32_t mic_sample_rate;
    uint32_t spk_sample_rate;
    int defaule_volume;
} TDD_AUDIO_ES8388_CODEC_T;

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
********************function declaration********************
***********************************************************/

OPERATE_RET tdd_audio_es8388_codec_register(char *name, TDD_AUDIO_ES8388_CODEC_T cfg);

#ifdef __cplusplus
}
#endif

#endif /* __TDD_AUDIO_ES8388_CODEC_H__ */
