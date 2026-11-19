/**
 * @file tdd_camera_dvp_pull.c
 * @brief DVP camera core for platforms whose TKL exposes the pull API
 *
 * This file implements the DVP camera driver, including frame buffer management,
 * DVP hardware initialization, camera device registration, and frame processing
 * callbacks. It provides the core functionality for DVP camera sensor integration.
 *
 * @copyright Copyright (c) 2021-2025 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"

#include "tkl_dvp.h"
#include "tkl_gpio.h"
#include "tkl_system.h"
#include "tkl_thread.h"

#include "tdd_camera_dvp.h"


/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    TDD_DVP_SR_CFG_T        sensor;
    TDD_DVP_SR_INTFS_T      intfs;
    TUYA_DVP_BASE_CFG_T     dvp_cfg;
    TUYA_MEDIA_OUTPUT_CFG_T out_cfg;
    TKL_THREAD_HANDLE       pull_thread;
    volatile int            pull_running;
    volatile int            pull_exited;
    uint32_t                frame_seq;
    bool                    is_inited;
} CAMERA_DVP_DEV_T;

/***********************************************************
***********************variable define**********************
***********************************************************/
static CAMERA_DVP_DEV_T *sg_dvp_dev = NULL;

/***********************************************************
***********************function define**********************
***********************************************************/
/* The v2 TKL reports lifecycle events asynchronously; only log them. */
static void __tdd_dvp_event_cb(TUYA_CAMERA_EVENT_E event, void *arg)
{
    (void)arg;
    switch (event) {
    case TUYA_CAMERA_EVENT_INITIALIZED:
        PR_DEBUG("dvp initialized");
        break;
    case TUYA_CAMERA_EVENT_STARTED:
        PR_DEBUG("dvp started");
        break;
    case TUYA_CAMERA_EVENT_STOPPED:
        PR_DEBUG("dvp stopped");
        break;
    case TUYA_CAMERA_EVENT_DEINITIALIZED:
        PR_DEBUG("dvp deinitialized");
        break;
    default:
        break;
    }
}

/* Pulls channel 1 and posts each frame to the TDL pool. */
static void __tdd_dvp_pull_thread(void *arg)
{
    CAMERA_DVP_DEV_T *dev = (CAMERA_DVP_DEV_T *)arg;

    while (dev->pull_running) {
        TUYA_MEDIA_BUF_T *buf = NULL;
        OPERATE_RET       rt  = tkl_dvp_get_frame(TUYA_CAMERA_FRAME_CHANNEL_1, &buf, 100);

        if (rt != OPRT_OK) {
            if (!dev->pull_running) {
                break;
            }
            if (rt != OPRT_TIMEOUT) {
                tkl_system_sleep(10);
            }
            continue;
        }

        if (buf != NULL && buf->buffer != NULL && buf->length > 0) {
            TDD_CAMERA_FRAME_T *tdd_frame = tdl_camera_create_tdd_frame((TDD_CAMERA_DEV_HANDLE_T)dev, buf->format);

            if (tdd_frame != NULL) {
                uint32_t len = buf->length;

                if (len > tdd_frame->frame.data_len) {
                    len = tdd_frame->frame.data_len;
                }
                memcpy(tdd_frame->frame.data, buf->buffer, len);
                tdd_frame->frame.id              = ++dev->frame_seq;
                tdd_frame->frame.is_i_frame      = 0;
                tdd_frame->frame.is_complete     = 1;
                tdd_frame->frame.fmt             = buf->format;
                tdd_frame->frame.width           = buf->width;
                tdd_frame->frame.height          = buf->height;
                tdd_frame->frame.data_len        = len;
                tdd_frame->frame.total_frame_len = len;

                if (tdl_camera_post_tdd_frame((TDD_CAMERA_DEV_HANDLE_T)dev, tdd_frame) != OPRT_OK) {
                    tdl_camera_release_tdd_frame((TDD_CAMERA_DEV_HANDLE_T)dev, tdd_frame);
                }
            }
        }

        if (buf != NULL && buf->free_cb != NULL) {
            buf->free_cb(buf);
        }
    }
    dev->pull_exited = 1;
}

static OPERATE_RET __tdd_dvp_pull_start(CAMERA_DVP_DEV_T *dev)
{
    if (dev->pull_thread != NULL) {
        return OPRT_OK;
    }
    dev->pull_running = 1;
    dev->pull_exited  = 0;
    if (tkl_thread_create(&dev->pull_thread, "dvp_pull", 4096, 4, __tdd_dvp_pull_thread, dev) != OPRT_OK) {
        dev->pull_thread  = NULL;
        dev->pull_running = 0;
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

static void __tdd_dvp_pull_stop(CAMERA_DVP_DEV_T *dev)
{
    int i;

    if (dev->pull_thread == NULL) {
        return;
    }
    dev->pull_running = 0;
    for (i = 0; i < 100 && !dev->pull_exited; i++) {
        tkl_system_sleep(5);
    }
    if (!dev->pull_exited) {
        /* Still inside tkl_dvp_get_frame: keep the handle. */
        PR_WARN("dvp pull thread did not exit in 500 ms");
        return;
    }
    tkl_thread_release(dev->pull_thread);
    dev->pull_thread = NULL;
}

/**
 * @brief Initialize DVP camera device
 * @param dev Pointer to DVP camera device structure
 * @param cfg Pointer to camera open configuration structure
 * @return OPRT_OK on success, OPRT_INVALID_PARM if parameters are invalid
 */
static OPERATE_RET __tdd_camera_dvp_init(CAMERA_DVP_DEV_T *dev, TDD_CAMERA_OPEN_CFG_T *cfg)
{
    OPERATE_RET rt;

    if (NULL == dev || NULL == cfg) {
        return OPRT_INVALID_PARM;
    }

    /* Channel 1 only. */
    if (cfg->out_fmt != TDL_CAMERA_FMT_YUV422) {
        PR_ERR("dvp delivers YUV422 only, got out_fmt %d", cfg->out_fmt);
        return OPRT_NOT_SUPPORTED;
    }

    memset(&dev->dvp_cfg, 0, sizeof(dev->dvp_cfg));
    memset(&dev->out_cfg, 0, sizeof(dev->out_cfg));

    dev->dvp_cfg.dvp_cfg.width  = cfg->width;
    dev->dvp_cfg.dvp_cfg.height = cfg->height;
    dev->dvp_cfg.dvp_cfg.fps    = cfg->fps;
    dev->dvp_cfg.dvp_cfg.format = TUYA_FRAME_FMT_YUV422;
    dev->dvp_cfg.sync_polarity  = dev->sensor.sync_polarity;

    dev->out_cfg.channel = TUYA_CAMERA_FRAME_CHANNEL_1;
    dev->out_cfg.rotate  = TUYA_FRAME_ROTATE_0;
    dev->out_cfg.width   = cfg->width;
    dev->out_cfg.height  = cfg->height;
    dev->out_cfg.format  = TUYA_FRAME_FMT_YUV422;
    dev->out_cfg.cb      = NULL; /* channel 1 is pulled, not pushed */

    dev->dvp_cfg.output_num = 1;
    dev->dvp_cfg.outputs    = &dev->out_cfg;
    dev->dvp_cfg.event_cb   = __tdd_dvp_event_cb;
    dev->dvp_cfg.event_arg  = dev;

    rt = tkl_dvp_init(&dev->dvp_cfg);
    if (rt != OPRT_OK) {
        return rt;
    }
    tkl_dvp_stop(); /* output is open()'s to enable */

    return OPRT_OK;
}

/**
 * @brief Open DVP camera device
 * @param device DVP camera device handle
 * @param cfg Pointer to camera open configuration structure
 * @return OPRT_OK on success, error code otherwise
 */
static OPERATE_RET __tdd_camera_dvp_open(TDD_CAMERA_DEV_HANDLE_T device, TDD_CAMERA_OPEN_CFG_T *cfg)
{
    OPERATE_RET           rt        = OPRT_OK;
    CAMERA_DVP_DEV_T     *dvp_dev   = (CAMERA_DVP_DEV_T *)device;
    TUYA_GPIO_BASE_CFG_T  gpio_cfg  = {.direct = TUYA_GPIO_OUTPUT};
    TDD_DVP_SR_USR_CFG_T *p_usr_cfg = NULL;

    if (NULL == device || NULL == cfg) {
        return OPRT_INVALID_PARM;
    }

    sg_dvp_dev = dvp_dev;
    p_usr_cfg  = &(dvp_dev->sensor.usr_cfg);

    /* Board state: bring up once, open/close is start/stop. */
    if (dvp_dev->is_inited) {
        rt = tkl_dvp_start();
        if (rt != OPRT_OK) {
            return rt;
        }
        return __tdd_dvp_pull_start(dvp_dev);
    }

    if (p_usr_cfg->i2c.port < TUYA_I2C_NUM_MAX) {
        TUYA_CALL_ERR_RETURN(tdd_dvp_i2c_init(&p_usr_cfg->i2c));
    }

    if (p_usr_cfg->pwr.pin < TUYA_GPIO_NUM_MAX) {
        gpio_cfg.level = p_usr_cfg->pwr.active_level;
        tkl_gpio_init(p_usr_cfg->pwr.pin, &gpio_cfg);
        PR_NOTICE("dvp pwr on:%d", p_usr_cfg->pwr.pin);
    }

    if (dvp_dev->intfs.rst) {
        TUYA_CALL_ERR_RETURN(dvp_dev->intfs.rst(&p_usr_cfg->rst, dvp_dev->intfs.arg));
    }

    TUYA_CALL_ERR_RETURN(__tdd_camera_dvp_init(dvp_dev, cfg));

    if (dvp_dev->intfs.init) {
        rt = dvp_dev->intfs.init(&p_usr_cfg->i2c, dvp_dev->intfs.arg);
        if (rt != OPRT_OK) {
            goto __ERR_DEINIT;
        }
    }

    if (dvp_dev->intfs.set_ppi) {
        TUYA_CAMERA_PPI_E ppi = (TUYA_CAMERA_PPI_E)((cfg->width << 16) | cfg->height);
        rt                    = dvp_dev->intfs.set_ppi(&p_usr_cfg->i2c, ppi, cfg->fps, dvp_dev->intfs.arg);
        if (rt != OPRT_OK) {
            goto __ERR_DEINIT;
        }
    }

    dvp_dev->is_inited = true;

    rt = tkl_dvp_start();
    if (rt != OPRT_OK) {
        goto __ERR_DEINIT;
    }
    rt = __tdd_dvp_pull_start(dvp_dev);
    if (rt != OPRT_OK) {
        tkl_dvp_stop();
        goto __ERR_DEINIT;
    }
    return OPRT_OK;

__ERR_DEINIT:
    /* tkl_dvp_init() already ran: release its buffers. */
    (void)tkl_dvp_deinit();
    return rt;
}

/**
 * @brief Close DVP camera device
 * @param device DVP camera device handle
 * @return OPRT_NOT_SUPPORTED (function not implemented)
 */
static OPERATE_RET __tdd_camera_dvp_close(TDD_CAMERA_DEV_HANDLE_T device)
{
    if (NULL == device) {
        return OPRT_INVALID_PARM;
    }
    /* Stop, not teardown - tkl_dvp_deinit belongs to shutting the board down. */
    __tdd_dvp_pull_stop((CAMERA_DVP_DEV_T *)device);
    return tkl_dvp_stop();
}

/**
 * @brief Register a DVP camera device
 * @param name Camera device name
 * @param sr_cfg Pointer to DVP sensor configuration structure
 * @param sr_intfs Pointer to DVP sensor interface functions structure
 * @return OPRT_OK on success, OPRT_INVALID_PARM if parameters are invalid,
 *         OPRT_MALLOC_FAILED on memory allocation failure
 */
OPERATE_RET tdl_camera_dvp_device_register(char *name, TDD_DVP_SR_CFG_T *sr_cfg, TDD_DVP_SR_INTFS_T *sr_intfs)
{
    CAMERA_DVP_DEV_T     *dvp_dev = NULL;
    TDD_CAMERA_DEV_INFO_T dev_info;

    if (NULL == name || NULL == sr_cfg) {
        return OPRT_INVALID_PARM;
    }

    dvp_dev = (CAMERA_DVP_DEV_T *)tal_malloc(sizeof(CAMERA_DVP_DEV_T));
    if (NULL == dvp_dev) {
        return OPRT_MALLOC_FAILED;
    }
    memset(dvp_dev, 0, sizeof(CAMERA_DVP_DEV_T));

    memcpy(&dvp_dev->sensor, sr_cfg, sizeof(TDD_DVP_SR_CFG_T));
    memcpy(&dvp_dev->intfs, sr_intfs, sizeof(TDD_DVP_SR_INTFS_T));

    dev_info.type       = TDL_CAMERA_DVP;
    dev_info.max_fps    = sr_cfg->max_fps;
    dev_info.max_width  = sr_cfg->max_width;
    dev_info.max_height = sr_cfg->max_height;
    /* YUV process channel only. */
    dev_info.supported_fmts = TDL_CAMERA_FMT_YUV422;
    dev_info.yuv_order      = sr_cfg->yuv_order;

    TDD_CAMERA_INTFS_T camera_intfs = {
        .open  = __tdd_camera_dvp_open,
        .close = __tdd_camera_dvp_close,
    };

    return tdl_camera_device_register(name, (TDD_CAMERA_DEV_HANDLE_T)dvp_dev, &camera_intfs, &dev_info);
}
