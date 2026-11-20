/**
 * @file tdd_camera_dvp_gc2145_v1.c
 * @brief GC2145 sensor driver for the JIELI DVP module
 *
 * Sensor side only: reset, register tables, PPI and the restart hook. The
 * transport lives in the shared TDD core (tdd_camera_dvp.c), the same one the
 * T5AI boards use.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 *
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_gpio.h"

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

    return tdl_camera_dvp_device_register(name, &sg_dvp_sensor, &sr_intfs);
}
