/**
 * @file tkl_dvp.h
 * @brief Tuya DVP (camera parallel interface) adaptation layer -- porting template
 *
 * Copy this file to the platform's adapter include directory and implement the
 * API in tkl_dvp.c. Define TKL_DVP_HAS_PULL_API once the pull interface below
 * is implemented: the shared camera driver in src/peripherals/camera/tdd_camera
 * then compiles its pull implementation instead of the legacy callback one.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TKL_DVP_H__
#define __TKL_DVP_H__

/* Uncomment after tkl_dvp_get_frame() and the frame pool are implemented. */
/* #define TKL_DVP_HAS_PULL_API 1 */

#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* hsync / vsync polarity. The values are the shared TUYA_DVP_SYNC_MODE enum in
 * tuya_cloud_types.h; this header only gives them the spelling this interface
 * uses, so a platform and the upper layers agree on one set of enumerators. */
typedef TUYA_DVP_SYNC_MODE TUYA_DVP_SYNC_MODE_E;

typedef struct {
    /* input frame config (unified shape: width / height / fps / format) */
    TUYA_CAMERA_FRAME_CFG_T dvp_cfg;
    /* polarity the sensor drives; the caller fills it from the sensor */
    TUYA_DVP_SYNC_MODE_E    sync_polarity;

    /* output channel array. Channels 1 (YUV process data) is pulled through
     * tkl_dvp_get_frame(); channels 2 (rgb) and 3 (h264) are pushed through
     * their node's cb. A node present in the array counts as enabled. */
    uint8_t                 output_num;
    TUYA_MEDIA_OUTPUT_CFG_T *outputs;

    /* lifecycle events only: INITIALIZED / STARTED / STOPPED / DEINITIALIZED */
    TUYA_CAMERA_EVENT_CB    event_cb;
    void                   *event_arg;
} TUYA_DVP_BASE_CFG_T;

/**
 * @brief Initialize the DVP input and every requested output channel
 * @param[in] cfg config and output array (deep-copied; the APP keeps ownership
 *                of the objects the nodes point to)
 * @return OPRT_OK; OPRT_INVALID_PARM on a bad config (unsupported format,
 *         illegal or duplicate channel, sync_polarity out of range);
 *         OPRT_NOT_SUPPORTED for a capability this platform lacks;
 *         OPRT_INIT_MORE_THAN_ONCE without a deinit; OPRT_COM_ERROR otherwise
 * @note Unsupported combinations must be rejected, never silently ignored.
 */
OPERATE_RET tkl_dvp_init(const TUYA_DVP_BASE_CFG_T *cfg);

/**
 * @brief Start every enabled output channel
 * @return OPRT_OK / OPRT_COM_ERROR (not initialized)
 */
OPERATE_RET tkl_dvp_start(void);

/**
 * @brief Stop streaming, keep the initialized resources; idempotent
 * @return OPRT_OK / OPRT_COM_ERROR (not initialized)
 */
OPERATE_RET tkl_dvp_stop(void);

/**
 * @brief Stop and release every camera resource
 * @return OPRT_OK / OPRT_COM_ERROR (not initialized)
 * @note Memory must stay level across repeated init/deinit cycles.
 */
OPERATE_RET tkl_dvp_deinit(void);

/**
 * @brief Pull one frame from a process-data channel (channel 1)
 * @param[in]  channel   TUYA_CAMERA_FRAME_CHANNEL_1
 * @param[out] out       frame descriptor owned by the TKL; release it through
 *                       its free_cb when the bytes are no longer needed
 * @param[in]  timeout_ms wait budget, 0 = return immediately
 * @return OPRT_OK / OPRT_TIMEOUT (no frame in time) / OPRT_INVALID_PARM /
 *         OPRT_COM_ERROR (not initialized) / OPRT_NOT_SUPPORTED (not a
 *         process-data channel)
 * @note A delivered frame is never reclaimed by the TKL.
 */
OPERATE_RET tkl_dvp_get_frame(TUYA_CAMERA_FRAME_CHANNEL_E channel, TUYA_MEDIA_BUF_T **out, uint32_t timeout_ms);

/**
 * @brief Ask the next H264 frame to be an IDR
 * @return OPRT_OK / OPRT_COM_ERROR / OPRT_NOT_SUPPORTED (no H264 channel)
 */
OPERATE_RET tkl_dvp_request_idr(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __TKL_DVP_H__ */
