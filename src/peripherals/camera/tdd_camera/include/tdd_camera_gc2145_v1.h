/**
 * @file tdd_camera_gc2145_v1.h
 * @brief GC2145 sensor driver header for the pull (generation-2) TKL
 *
 * Same entry point as tdd_camera_gc2145.h; the pull variant also exposes the
 * restart call the JIELI board uses after video0 powers the sensor clock.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#ifndef __TDD_CAMERA_GC2145_V1_H__
#define __TDD_CAMERA_GC2145_V1_H__

#include "tuya_cloud_types.h"
#include "tdd_camera_dvp_v1.h"

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
