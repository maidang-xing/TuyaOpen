/**
 * @file tdd_display_qspi.h
 * @brief tdd_display_qspi module is used to implement the QSPI interface for display devices.
 * @version 0.1
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 */

#ifndef __TDD_DISPLAY_QSPI_H__
#define __TDD_DISPLAY_QSPI_H__

#include "tuya_cloud_types.h"
#include "tdl_display_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    uint16_t width;
    uint16_t height;
    TUYA_DISPLAY_PIXEL_FMT_E pixel_fmt;
    TUYA_GPIO_NUM_E cs_pin;
    TUYA_GPIO_NUM_E dc_pin;
    TUYA_GPIO_NUM_E rst_pin;
    TUYA_QSPI_NUM_E port;
    uint32_t spi_clk;
    uint8_t cmd_caset;
    uint8_t cmd_raset;
    uint8_t cmd_ramwr;
} DISP_QSPI_BASE_CFG_T;

typedef struct {
    DISP_QSPI_BASE_CFG_T cfg;
    TUYA_DISPLAY_BL_CTRL_T bl;
    TUYA_DISPLAY_IO_CTRL_T power;
    TUYA_DISPLAY_ROTATION_E rotation;
    const uint8_t *init_seq; // Initialization commands for the display
} TDD_DISP_QSPI_CFG_T;

/***********************************************************
********************function declaration********************
***********************************************************/
/**
 * @brief Registers a QSPI display device with simulated SPI functionality.
 *
 * @param name Device name for identification.
 * @param spi Pointer to QSPI configuration structure.
 * @return OPERATE_RET Operation result code.
 */
OPERATE_RET tdd_disp_qspi_simulate_spi_device_register(char *name, TDD_DISP_QSPI_CFG_T *spi);

#ifdef __cplusplus
}
#endif

#endif /* __TDD_DISPLAY_QSPI_H__ */
