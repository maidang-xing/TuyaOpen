/**
 * @file tdd_camera_dvp_gc2145_v1.c
 * @brief GC2145 camera TDD for the JIELI DVP path
 *
 * Self-contained driver: the sensor side (reset, register tables, PPI) and the
 * transport side (tkl_dvp open/start, the frame pull thread, TDL registration).
 * The GC0310 SPI module carries the same transport block - the two are the only
 * modules this board supports, and only one is compiled per build.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_dvp.h"
#include "tkl_gpio.h"
#include "tkl_system.h"
#include "tkl_thread.h"

#include "tdd_camera_gc2145_init_seq.h"
#include "tdd_camera_gc2145_v1.h"

/***********************************************************
************************macro define************************
***********************************************************/
#define UNACTIVE_LEVEL(x) 	    (((x) == TUYA_GPIO_LEVEL_LOW)? TUYA_GPIO_LEVEL_HIGH: TUYA_GPIO_LEVEL_LOW)

#define GC2145_WRITE_ADDRESS            (0x78)
#define GC2145_READ_ADDRESS             (0x79)

/***********************************************************
***********************typedef define***********************
***********************************************************/
typedef struct {
    TUYA_CAMERA_PPI_E ppi;
    uint8_t          *seq_tab;
    uint32_t          tab_size;
}CAMERA_PPI_INIT_TAB_T;

typedef struct {
    TUYA_CAMERA_PPI_E ppi;
    uint16_t          fps;
    uint8_t          *seq_tab;
    uint32_t          tab_size;
}CAMERA_FPS_INIT_TAB_T;

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

const CAMERA_PPI_INIT_TAB_T cPPI_INIT_SEQ_LIST[] = {
    {TUYA_CAMERA_PPI_240X240, (uint8_t *)cGC2145_240_240_TAB, CNTSOF(cGC2145_240_240_TAB)},
    {TUYA_CAMERA_PPI_480X480,  (uint8_t *)cGC2145_480_480_TAB,  CNTSOF(cGC2145_480_480_TAB)},
    {TUYA_CAMERA_PPI_640X480,  (uint8_t *)cGC2145_640_480_TAB,  CNTSOF(cGC2145_640_480_TAB)},
    {TUYA_CAMERA_PPI_800X480,  (uint8_t *)cGC2145_800_480_TAB,  CNTSOF(cGC2145_800_480_TAB)},
    {TUYA_CAMERA_PPI_864X480,  (uint8_t *)cGC2145_864_480_TAB,  CNTSOF(cGC2145_864_480_TAB)},
    {TUYA_CAMERA_PPI_1280X720, (uint8_t *)cGC2145_1280_720_TAB, CNTSOF(cGC2145_1280_720_TAB)},
};

const CAMERA_FPS_INIT_TAB_T cFPS_INIT_SEQ_LIST[] = {
    {TUYA_CAMERA_PPI_640X480,  30, (uint8_t *)cGC2145_640_480_30FPS_TAB,  CNTSOF(cGC2145_640_480_30FPS_TAB)},
    {TUYA_CAMERA_PPI_640X480,  25, (uint8_t *)cGC2145_640_480_25FPS_TAB,  CNTSOF(cGC2145_640_480_25FPS_TAB)},
    {TUYA_CAMERA_PPI_640X480,  20, (uint8_t *)cGC2145_640_480_20FPS_TAB,  CNTSOF(cGC2145_640_480_20FPS_TAB)},
    {TUYA_CAMERA_PPI_640X480,  15, (uint8_t *)cGC2145_640_480_15FPS_TAB,  CNTSOF(cGC2145_640_480_15FPS_TAB)},

    {TUYA_CAMERA_PPI_864X480,  25, (uint8_t *)cGC2145_864_480_25FPS_TAB,  CNTSOF(cGC2145_864_480_25FPS_TAB)},
    {TUYA_CAMERA_PPI_864X480,  20, (uint8_t *)cGC2145_864_480_20FPS_TAB,  CNTSOF(cGC2145_864_480_20FPS_TAB)},
    {TUYA_CAMERA_PPI_864X480,  15, (uint8_t *)cGC2145_864_480_15FPS_TAB,  CNTSOF(cGC2145_864_480_15FPS_TAB)},

    {TUYA_CAMERA_PPI_1280X720, 20, (uint8_t *)cGC2145_1280_720_20FPS_TAB, CNTSOF(cGC2145_1280_720_20FPS_TAB)},
    {TUYA_CAMERA_PPI_1280X720, 15, (uint8_t *)cGC2145_1280_720_15FPS_TAB, CNTSOF(cGC2145_1280_720_15FPS_TAB)},
    {TUYA_CAMERA_PPI_1280X720, 10, (uint8_t *)cGC2145_1280_720_10FPS_TAB, CNTSOF(cGC2145_1280_720_10FPS_TAB)},
};


/***********************************************************
***********************variable define**********************
***********************************************************/
static TDD_DVP_SR_CFG_T sg_dvp_sensor = {
	.max_fps = 30,
	.max_width = 1616,
	.max_height = 1232,
	.fmt = TUYA_FRAME_FMT_YUV422,
	/* tkl_dvp packs the ISP output as Y U Y V; the converter defaults to UYVY. */
	.yuv_order = TUYA_YUV422_YUYV,
	/* Both sync lines are active high; the H-V exchange is a line mapping. */
	.sync_polarity = TUYA_DVP_SYNC_MODE_0,
};

/* Last geometry requested, so a restart after the vendor has enabled the
 * sensor clock can re-apply the same tables. */
static TUYA_CAMERA_PPI_E sg_ppi = TUYA_CAMERA_PPI_640X480;
static uint16_t sg_fps = 20;


/***********************************************************
***********************function define**********************
***********************************************************/
/**
 * @brief Update GC2145 sensor registers via I2C
 * @param port I2C port number
 * @param sensor_reg_table Pointer to sensor register table (register address and value pairs)
 * @param size Number of register entries in the table
 */
static void __dvp_gc2145_update_reg(TUYA_I2C_NUM_E port, const uint8_t sensor_reg_table[][2], uint32_t size)
{
    OPERATE_RET rt = OPRT_OK;
    DVP_I2C_REG_CFG_T i2c_reg_cfg;
    uint16_t val_len = sizeof(sensor_reg_table[0][0]);

    i2c_reg_cfg.port = port;
    i2c_reg_cfg.addr = (GC2145_WRITE_ADDRESS >> 1);
    i2c_reg_cfg.is_16_reg = 0;

    for (uint16_t idx = 0; idx < size; idx++) {
        i2c_reg_cfg.reg = sensor_reg_table[idx][0];
        rt = tdd_dvp_i2c_write(&i2c_reg_cfg, val_len, (uint8_t *)&sensor_reg_table[idx][1]);
        if (rt != OPRT_OK) {
            PR_ERR("gc2145 i2c write err");
            return;
        }
    }
}

/**
 * @brief Log the sensor chip id, so a silent I2C fault is visible
 * @param port I2C port number
 * @param tag Short marker identifying the call site
 */
static void __dvp_gc2145_log_id(TUYA_I2C_NUM_E port, const char *tag)
{
    DVP_I2C_REG_CFG_T id_cfg = {
        .port      = port,
        .addr      = (GC2145_READ_ADDRESS >> 1),
        .is_16_reg = 0,
        .reg       = 0xF0,
    };
    uint8_t id[2] = {0};
    OPERATE_RET rt = tdd_dvp_i2c_read(&id_cfg, sizeof(id), id);

    PR_NOTICE("gc2145 id %s: rt=%d %02x%02x", tag, rt, id[0], id[1]);
}

/**
 * @brief Reset GC2145 camera sensor
 * @param rst_pin Pointer to reset pin control structure
 * @param arg User argument (unused)
 * @return OPRT_OK on success
 */
static OPERATE_RET __dvp_gc2145_reset(TUYA_CAMERA_IO_CTRL_T *rst_pin, void *arg)
{
    if(NULL == rst_pin) {
        return OPRT_OK;
    }

    (void)arg;

    if(rst_pin->pin < TUYA_GPIO_NUM_MAX) {
        TUYA_GPIO_BASE_CFG_T gpio_cfg = {.direct = TUYA_GPIO_OUTPUT};

        gpio_cfg.level = UNACTIVE_LEVEL(rst_pin->active_level);
		tkl_gpio_init(rst_pin->pin, &gpio_cfg);

        tkl_gpio_write(rst_pin->pin, UNACTIVE_LEVEL(rst_pin->active_level));
        tal_system_sleep(20);
        tkl_gpio_write(rst_pin->pin, rst_pin->active_level);
        tal_system_sleep(100);
        tkl_gpio_write(rst_pin->pin, UNACTIVE_LEVEL(rst_pin->active_level));
        tal_system_sleep(100);
        PR_NOTICE("gc2145 reset pin %d lv:%d", rst_pin->pin, rst_pin->active_level);
    }

    return OPRT_OK;
}

/**
 * @brief Initialize GC2145 camera sensor
 * @param i2c Pointer to I2C configuration structure
 * @param arg User argument (unused)
 * @return OPRT_OK on success, OPRT_INVALID_PARM if parameters are invalid
 */
static OPERATE_RET __dvp_gc2145_init(DVP_I2C_CFG_T *i2c, void *arg)
{
    if(NULL == i2c) {
        return OPRT_INVALID_PARM;
    }

    (void)arg;

    __dvp_gc2145_update_reg(i2c->port, cGC2145_INIT_TAB, CNTSOF(cGC2145_INIT_TAB));

    PR_NOTICE("__dvp_gc2145_init");
    __dvp_gc2145_log_id(i2c->port, "init");

    return OPRT_OK;
}

/**
 * @brief Set GC2145 camera sensor PPI (resolution) and FPS
 * @param i2c Pointer to I2C configuration structure
 * @param ppi Camera PPI enumeration (resolution)
 * @param fps Frame rate per second
 * @param arg User argument (unused)
 * @return OPRT_OK on success, OPRT_INVALID_PARM if parameters are invalid
 */
static OPERATE_RET __dvp_gc2145_set_ppi(DVP_I2C_CFG_T *i2c, TUYA_CAMERA_PPI_E ppi, uint16_t fps, void *arg)
{
    uint32_t i = 0;
    CAMERA_PPI_INIT_TAB_T *ppi_init_tab = NULL;
    CAMERA_FPS_INIT_TAB_T *fps_init_tab = NULL;

    if(NULL == i2c) {
        return OPRT_INVALID_PARM;
    }

    (void)arg;

    for(i=0; i<CNTSOF(cPPI_INIT_SEQ_LIST); i++) {
        if(ppi == cPPI_INIT_SEQ_LIST[i].ppi) {
            ppi_init_tab = (CAMERA_PPI_INIT_TAB_T *)&cPPI_INIT_SEQ_LIST[i];
            break;
        }
    }

    if((NULL == ppi_init_tab) && (ppi == TUYA_CAMERA_PPI_320X240)) {
        /* No native QVGA table: run VGA and let the TKL decimate. */
        for(i=0; i<CNTSOF(cPPI_INIT_SEQ_LIST); i++) {
            if(TUYA_CAMERA_PPI_640X480 == cPPI_INIT_SEQ_LIST[i].ppi) {
                ppi_init_tab = (CAMERA_PPI_INIT_TAB_T *)&cPPI_INIT_SEQ_LIST[i];
                ppi          = TUYA_CAMERA_PPI_640X480;
                break;
            }
        }
    }

    for(i=0; i<CNTSOF(cFPS_INIT_SEQ_LIST); i++) {
        if((ppi == cFPS_INIT_SEQ_LIST[i].ppi) && (fps == cFPS_INIT_SEQ_LIST[i].fps)) {
            fps_init_tab = (CAMERA_FPS_INIT_TAB_T *)&cFPS_INIT_SEQ_LIST[i];
            break;
        }
    }

    if(ppi_init_tab) {
        __dvp_gc2145_update_reg(i2c->port, (const uint8_t (*)[2])ppi_init_tab->seq_tab, ppi_init_tab->tab_size);
    }

    if(fps_init_tab) {
        __dvp_gc2145_update_reg(i2c->port, (const uint8_t (*)[2])fps_init_tab->seq_tab, fps_init_tab->tab_size);
    }

    sg_ppi = ppi;
    sg_fps = fps;

    PR_NOTICE("__dvp_gc2145_set_ppi ppi:%x, fps:%d", ppi, fps);

    return OPRT_OK;
}

/**
 * @brief Replay reset + tables once video0 has powered the sensor clock
 * @return OPRT_OK on success, OPRT_COM_ERROR if the module never registered
 */
OPERATE_RET tdd_camera_dvp_gc2145_restart(void)
{
    DVP_I2C_CFG_T *i2c = &sg_dvp_sensor.usr_cfg.i2c;

    if (i2c->port >= TUYA_I2C_NUM_MAX) {
        return OPRT_COM_ERROR;
    }

    (void)tdd_dvp_i2c_init(i2c);
    (void)__dvp_gc2145_reset(&sg_dvp_sensor.usr_cfg.rst, NULL);
    (void)__dvp_gc2145_init(i2c, NULL);
    (void)__dvp_gc2145_set_ppi(i2c, sg_ppi, sg_fps, NULL);

    __dvp_gc2145_log_id(i2c->port, "restart");

    return OPRT_OK;
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

/**
 * @brief Register GC2145 camera sensor device
 * @param name Camera device name
 * @param cfg Pointer to DVP sensor user configuration structure
 * @return OPRT_OK on success, OPRT_INVALID_PARM if parameters are invalid
 */
OPERATE_RET tdd_camera_dvp_gc2145_register(char *name, TDD_DVP_SR_USR_CFG_T *cfg)
{
    if(NULL == name || NULL == cfg) {
        return OPRT_INVALID_PARM;
    }

    memcpy(&sg_dvp_sensor.usr_cfg, cfg, sizeof(TDD_DVP_SR_USR_CFG_T));

    TDD_DVP_SR_INTFS_T sr_intfs = {
        .arg      = NULL,
        .rst      = __dvp_gc2145_reset,
        .init     = __dvp_gc2145_init,
        .set_ppi  = __dvp_gc2145_set_ppi,
    };

    return __tdd_dvp_device_register(name, &sg_dvp_sensor, &sr_intfs);
}
