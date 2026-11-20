/**
 * @file tdd_camera_gc0310_spi.c
 * @brief GC0310 camera TDD for the JIELI SPI (J4) path
 *
 * The vendor stack configures GC0310 from its video1 sensor driver. This
 * driver does the same register work through the TDD I2C helper so the SPI
 * transport can run without opening video1 at all.
 *
 * The file also carries the transport side (tkl_dvp open/start, the frame pull
 * thread, TDL registration), the same block as tdd_camera_dvp_gc2145_v1.c.
 * Only one of the two modules is compiled per build.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_dvp.h"
#include "tkl_gpio.h"
#include "tkl_system.h"
#include "tkl_thread.h"

#include "tdd_camera_dvp_i2c.h"
#include "tdd_camera_gc0310_init_seq.h"
#include "tdd_camera_gc0310_spi.h"

/***********************************************************
************************macro define************************
***********************************************************/
/* SCCB slave address 0x42 in 8-bit form; tkl_i2c takes the 7-bit address. */
#define GC0310_I2C_ADDR    (0x42 >> 1)
#define GC0310_PAGE_REG    0xfe
#define GC0310_PAGE_ID     0xf0
#define GC0310_CHIP_ID     0xa310
#define GC0310_UNACTIVE(x) (((x) == TUYA_GPIO_LEVEL_LOW) ? TUYA_GPIO_LEVEL_HIGH : TUYA_GPIO_LEVEL_LOW)

/***********************************************************
***********************typedef define***********************
***********************************************************/
/* Transport state, one per registered device. */
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
static TUYA_I2C_NUM_E        sg_i2c_port = TUYA_I2C_NUM_MAX;
static TUYA_CAMERA_IO_CTRL_T sg_rst_pin  = {.pin = TUYA_GPIO_NUM_MAX, .active_level = TUYA_GPIO_LEVEL_LOW};

/***********************************************************
***********************function define**********************
***********************************************************/
static OPERATE_RET __gc0310_write(uint8_t reg, uint8_t val)
{
    DVP_I2C_REG_CFG_T cfg = {
        .port      = sg_i2c_port,
        .addr      = GC0310_I2C_ADDR,
        .reg       = reg,
        .is_16_reg = 0,
    };

    return tdd_dvp_i2c_write(&cfg, 1, &val);
}

static OPERATE_RET __gc0310_read(uint8_t reg, uint8_t *buf, uint16_t len)
{
    DVP_I2C_REG_CFG_T cfg = {
        .port      = sg_i2c_port,
        .addr      = GC0310_I2C_ADDR,
        .reg       = reg,
        .is_16_reg = 0,
    };

    return tdd_dvp_i2c_read(&cfg, len, buf);
}

static OPERATE_RET __gc0310_write_table(void)
{
    for (uint32_t i = 0; i < GC0310_INIT_SEQ_LEN; i++) {
        OPERATE_RET rt = __gc0310_write(gc0310_init_seq[i].reg, gc0310_init_seq[i].val);

        if (rt != OPRT_OK) {
            PR_ERR("gc0310 i2c write failed: reg=0x%02x val=0x%02x rt=%d idx=%u/%u", gc0310_init_seq[i].reg,
                   gc0310_init_seq[i].val, rt, (unsigned)i, (unsigned)GC0310_INIT_SEQ_LEN);
            return rt;
        }
    }

    return OPRT_OK;
}

static OPERATE_RET __gc0310_check_id(void)
{
    uint8_t  id[2] = {0};
    uint16_t value;

    /* This module powers up on page 0, where 0xf0/0xf1 read 0x00. */
    __gc0310_write(GC0310_PAGE_REG, GC0310_PAGE_ID);
    tal_system_sleep(2);

    if (__gc0310_read(GC0310_PAGE_ID, id, 2) != OPRT_OK) {
        PR_ERR("gc0310 id read failed");
        return OPRT_COM_ERROR;
    }

    value = (uint16_t)((id[0] << 8) | id[1]);
    PR_NOTICE("gc0310 chip id: 0x%04x (expected 0x%04x)", value, GC0310_CHIP_ID);

    return (value == GC0310_CHIP_ID) ? OPRT_OK : OPRT_COM_ERROR;
}

static void __gc0310_pulse_reset(void)
{
    if (sg_rst_pin.pin >= TUYA_GPIO_NUM_MAX) {
        return;
    }

    tkl_gpio_write(sg_rst_pin.pin, sg_rst_pin.active_level);
    tal_system_sleep(10);
    tkl_gpio_write(sg_rst_pin.pin, GC0310_UNACTIVE(sg_rst_pin.active_level));
    /* The sensor NACKs its address for a few ms after reset is released. */
    tal_system_sleep(5);
}

static OPERATE_RET __dvp_gc0310_spi_reset(TUYA_CAMERA_IO_CTRL_T *rst_pin, void *arg)
{
    (void)arg;

    if (NULL == rst_pin) {
        return OPRT_OK;
    }

    sg_rst_pin = *rst_pin;

    if (rst_pin->pin < TUYA_GPIO_NUM_MAX) {
        TUYA_GPIO_BASE_CFG_T gpio_cfg = {
            .direct = TUYA_GPIO_OUTPUT,
            .level  = GC0310_UNACTIVE(rst_pin->active_level),
        };

        tkl_gpio_init(rst_pin->pin, &gpio_cfg);
        tkl_gpio_write(rst_pin->pin, GC0310_UNACTIVE(rst_pin->active_level));
        tal_system_sleep(5);
        __gc0310_pulse_reset();
        PR_NOTICE("gc0310 reset pin %d lv:%d", rst_pin->pin, rst_pin->active_level);
    }

    return OPRT_OK;
}

static OPERATE_RET __dvp_gc0310_spi_init(DVP_I2C_CFG_T *i2c, void *arg)
{
    (void)arg;

    if (NULL == i2c) {
        return OPRT_INVALID_PARM;
    }

    sg_i2c_port = i2c->port;

    /* The board's reset callback already released the sensor; check the ID
     * before the long table so a wrong module fails quickly. */
    if (__gc0310_check_id() != OPRT_OK) {
        return OPRT_COM_ERROR;
    }

    OPERATE_RET rt = __gc0310_write_table();
    if (rt != OPRT_OK) {
        return rt;
    }

    PR_NOTICE("gc0310 spi init done");

    return OPRT_OK;
}

static OPERATE_RET __dvp_gc0310_spi_set_ppi(DVP_I2C_CFG_T *i2c, TUYA_CAMERA_PPI_E ppi, uint16_t fps, void *arg)
{
    (void)i2c;
    (void)arg;

    /* The SPI stream is fixed at the sensor's native VGA raster; the preview
     * size is decimated by tkl_dvp. A different PPI here cannot change the
     * 1288-byte row stride (measured on AC791). */
    PR_NOTICE("gc0310 spi fixed VGA (requested ppi 0x%x fps %u)", ppi, fps);

    return OPRT_OK;
}

OPERATE_RET tdd_camera_gc0310_spi_restart(void)
{
    if (sg_i2c_port >= TUYA_I2C_NUM_MAX) {
        return OPRT_COM_ERROR;
    }

    __gc0310_pulse_reset();

    return __gc0310_write_table();
}

/* ------------------------------------------------------------------------- */
/* Transport: tkl_dvp lifecycle and the frame pull thread.                    */
/* ------------------------------------------------------------------------- */

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

static OPERATE_RET __tdd_camera_dvp_open(TDD_CAMERA_DEV_HANDLE_T device, TDD_CAMERA_OPEN_CFG_T *cfg)
{
    OPERATE_RET           rt        = OPRT_OK;
    CAMERA_DVP_DEV_T     *dvp_dev   = (CAMERA_DVP_DEV_T *)device;
    TUYA_GPIO_BASE_CFG_T  gpio_cfg  = {.direct = TUYA_GPIO_OUTPUT};
    TDD_DVP_SR_USR_CFG_T *p_usr_cfg = NULL;

    if (NULL == device || NULL == cfg) {
        return OPRT_INVALID_PARM;
    }

    p_usr_cfg = &(dvp_dev->sensor.usr_cfg);

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

static OPERATE_RET __tdd_camera_dvp_close(TDD_CAMERA_DEV_HANDLE_T device)
{
    if (NULL == device) {
        return OPRT_INVALID_PARM;
    }
    /* Stop, not teardown - tkl_dvp_deinit belongs to shutting the board down. */
    __tdd_dvp_pull_stop((CAMERA_DVP_DEV_T *)device);
    return tkl_dvp_stop();
}

static OPERATE_RET __tdd_dvp_device_register(char *name, TDD_DVP_SR_CFG_T *sr_cfg, TDD_DVP_SR_INTFS_T *sr_intfs)
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

OPERATE_RET tdd_camera_gc0310_spi_register(char *name, TDD_DVP_SR_USR_CFG_T *cfg)
{
    TDD_DVP_SR_CFG_T sensor = {
        .max_fps    = 15,
        .max_width  = 640,
        .max_height = 480,
        .fmt        = TUYA_FRAME_FMT_YUV422,
        .yuv_order  = TUYA_YUV422_YUYV,
#if defined(TKL_DVP_HAS_PULL_API)
        /* No sync lines on this module; tkl_dvp still validates the value. */
        .sync_polarity = TUYA_DVP_SYNC_MODE_0,
#endif
    };

    if (NULL == name || NULL == cfg) {
        return OPRT_INVALID_PARM;
    }

    memcpy(&sensor.usr_cfg, cfg, sizeof(TDD_DVP_SR_USR_CFG_T));

    TDD_DVP_SR_INTFS_T intfs = {
        .arg     = NULL,
        .rst     = __dvp_gc0310_spi_reset,
        .init    = __dvp_gc0310_spi_init,
        .set_ppi = __dvp_gc0310_spi_set_ppi,
    };

    return __tdd_dvp_device_register(name, &sensor, &intfs);
}
