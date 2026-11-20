/**
 * @file tdd_camera_gc2145_v1.h
 * @brief GC2145 sensor driver for the JIELI DVP module
 *
 * The transport is the shared TDD core (tdd_camera_dvp.c); this driver only
 * owns the sensor side and the restart hook the JIELI board calls once video0
 * has powered the sensor clock.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TDD_CAMERA_GC2145_V1_H__
#define __TDD_CAMERA_GC2145_V1_H__

#include "tuya_cloud_types.h"
#include "tdd_camera_dvp.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register GC2145 camera sensor device
 * @param name Camera device name
 * @param cfg Power / reset / I2C / clock the board wired for this module
 * @return OPRT_OK on success, error code otherwise
 */
OPERATE_RET tdd_camera_dvp_gc2145_register(char *name, TDD_DVP_SR_USR_CFG_T *cfg);

/**
 * @brief Replay reset + tables after video0 powers the sensor clock
 * @return OPRT_OK on success, OPRT_COM_ERROR if the sensor was never registered
 */
OPERATE_RET tdd_camera_dvp_gc2145_restart(void);

#ifdef __cplusplus
}
#endif

#endif /* __TDD_CAMERA_GC2145_V1_H__ */
