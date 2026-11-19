/**
 * @file tdd_camera_dvp_v1.h
 * @brief DVP camera interface for the pull (generation-2) TKL
 *
 * Same types and entry point as tdd_camera_dvp.h, plus the two fields the pull
 * core forwards to tkl_dvp and TDL. Only platforms that define
 * TKL_DVP_HAS_PULL_API use this header.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TDD_CAMERA_DVP_V1_H__
#define __TDD_CAMERA_DVP_V1_H__

#include "tuya_cloud_types.h"
#include "tkl_dvp.h"
#include "tdl_camera_driver.h"
#include "tdd_camera_dvp_i2c.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    TUYA_CAMERA_IO_CTRL_T pwr;
    TUYA_CAMERA_IO_CTRL_T rst;
    DVP_I2C_CFG_T         i2c;
    uint32_t              clk;
}TDD_DVP_SR_USR_CFG_T;

typedef struct {
    TDD_DVP_SR_USR_CFG_T  usr_cfg;
    uint16_t              max_fps;
    uint32_t              max_width;
    uint32_t              max_height;
    TUYA_FRAME_FMT_E      fmt;
    /* Byte order the transport delivers; forwarded to TDL as yuv_order. */
    TUYA_YUV422_ORDER_E   yuv_order;
#if defined(TKL_DVP_HAS_PULL_API)
    /* HSYNC/VSYNC polarity the sensor drives; the pull TKL requires it. */
    TUYA_DVP_SYNC_MODE_E  sync_polarity;
#endif
}TDD_DVP_SR_CFG_T;

typedef struct {
    void *arg;
    OPERATE_RET (*rst     )(TUYA_CAMERA_IO_CTRL_T *rst_pin, void *arg);
    OPERATE_RET (*init    )(DVP_I2C_CFG_T *i2c, void *arg);
    OPERATE_RET (*set_ppi )(DVP_I2C_CFG_T *i2c, TUYA_CAMERA_PPI_E ppi, uint16_t fps, void *arg);
}TDD_DVP_SR_INTFS_T;

/**
 * @brief Register a DVP camera device with the pull core
 * @param name Camera device name
 * @param sr_cfg Sensor capabilities and the transport settings above
 * @param sr_intfs Reset / init / set_ppi callbacks, NULL entries are optional
 * @return OPRT_OK on success, error code otherwise
 */
OPERATE_RET tdl_camera_dvp_device_register(char *name, TDD_DVP_SR_CFG_T *sr_cfg, TDD_DVP_SR_INTFS_T *sr_intfs);

#ifdef __cplusplus
}
#endif

#endif /* __TDD_CAMERA_DVP_V1_H__ */
