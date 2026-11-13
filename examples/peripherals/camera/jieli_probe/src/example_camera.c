/**
 * @file example_camera.c
 * @brief Jieli camera bring-up probe: open, receive frames, report evidence.
 *
 * No display and no SD card: this only proves the sensor probes and the
 * capture path delivers complete frames. Every frame is checked for a
 * non-zero payload and the first frame's size/format/timestamp is printed.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include "tal_api.h"
#include "tkl_output.h"

#include "board_com_api.h"
#include "tdl_camera_manage.h"

/* Diagnostic build: scan the camera I2C bus (iic1, software IIC on PH0/PH1 for
 * the SPI path) before the vendor video1 sensor check runs. Enabled by the
 * board Kconfig option so the normal probe image keeps its old behaviour. */
#if defined(JIELI_CAMERA_I2C_SCAN) && (JIELI_CAMERA_I2C_SCAN == 1)
/* Use the vendor device ioctls without pulling in device/device.h: this
 * example's Tuya target does not carry the SDK's include_lib/driver root, and
 * adding it makes the vendor INIT_LIST_HEAD collide with Tuya's tuya_list.h.
 * The command values are the stable ABI from include_lib/driver/device/iic.h. */
#include "generic/ioctl.h"
#include "asm/gpio.h"
#include "asm/WL82.h"

#define IIC_IOCTL_TX_WITH_START_BIT _IOW('I', 1, unsigned char)
#define IIC_IOCTL_START             _IO('I', 10)
#define IIC_IOCTL_STOP              _IO('I', 11)

extern void *dev_open(const char *name, void *arg);
extern int   dev_ioctl(void *device, int cmd, unsigned int arg);
extern int   dev_close(void *device);

#define EXAMPLE_I2C_SCAN 1
#else
#define EXAMPLE_I2C_SCAN 0
#endif

#define PROBE_FMT TDL_CAMERA_FMT_YUV422

static TDL_CAMERA_HANDLE_T sg_camera;
static volatile uint32_t   sg_frames;
static volatile uint32_t   sg_bad_frames;
static uint32_t            sg_first_ms;
static uint32_t            sg_open_ms;
static uint32_t            sg_last_report_ms;
static uint32_t            sg_last_report_frames;
static volatile uint32_t   sg_frame_bytes[4];

static uint32_t __nonzero_bytes(const uint8_t *data, uint32_t len)
{
    uint32_t nonzero = 0;
    uint32_t step    = len / 4096U;

    if (step == 0) {
        step = 1;
    }
    for (uint32_t i = 0; i < len; i += step) {
        if (data[i] != 0) {
            nonzero++;
        }
    }
    return nonzero;
}

static OPERATE_RET __frame_cb(TDL_CAMERA_HANDLE_T hdl, TDL_CAMERA_FRAME_T *frame)
{
    uint32_t now = tal_system_get_millisecond();
    uint32_t nonzero;

    (void)hdl;

    if (frame == NULL || frame->data == NULL || frame->data_len == 0) {
        sg_bad_frames++;
        return OPRT_OK;
    }

    nonzero = __nonzero_bytes(frame->data, frame->data_len);
    if (sg_frames == 0) {
        sg_first_ms = now;
        PR_NOTICE("FIRST FRAME: %ux%u fmt=0x%x len=%u latency=%ums nonzero=%u first16=%02x %02x %02x %02x %02x "
                  "%02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
                  frame->width, frame->height, (unsigned)frame->fmt, frame->data_len, now - sg_open_ms, nonzero,
                  frame->data[0], frame->data[1], frame->data[2], frame->data[3], frame->data[4], frame->data[5],
                  frame->data[6], frame->data[7], frame->data[8], frame->data[9], frame->data[10], frame->data[11],
                  frame->data[12], frame->data[13], frame->data[14], frame->data[15]);
    }

    if (nonzero == 0) {
        sg_bad_frames++;
    }
    sg_frames++;
    sg_frame_bytes[sg_frames & 3U] = frame->data_len;

    if (now - sg_last_report_ms >= 1000U) {
        uint32_t delta   = sg_frames - sg_last_report_frames;
        uint32_t seconds = (now - sg_last_report_ms) / 1000U;

        if (seconds == 0) {
            seconds = 1;
        }
        PR_NOTICE("frames=%u fps=%u bad=%u last_len=%u t=%ums", sg_frames, delta / seconds, sg_bad_frames,
                  frame->data_len, now - sg_first_ms);
        sg_last_report_ms     = now;
        sg_last_report_frames = sg_frames;
    }
    return OPRT_OK;
}

#if EXAMPLE_I2C_SCAN
#define I2C_SCAN_FIRST 0x08
#define I2C_SCAN_LAST  0x77

static void __scan_port_line(const char *name, int bit)
{
    printf("[I2CSCAN] %s: DIR=%u DIE=%u PU=%u PD=%u OUT=%u IN=%u\n", name, (unsigned)((JL_PORTH->DIR >> bit) & 1u),
           (unsigned)((JL_PORTH->DIE >> bit) & 1u), (unsigned)((JL_PORTH->PU >> bit) & 1u),
           (unsigned)((JL_PORTH->PD >> bit) & 1u), (unsigned)((JL_PORTH->OUT >> bit) & 1u),
           (unsigned)((JL_PORTH->IN >> bit) & 1u));
}

/* Probe every 7-bit address directly on the vendor iic1 device. This does not
 * depend on the BYD3901/GC0310/GC6133 drivers, so an ACK here separates a bus
 * problem from a sensor-driver problem. */
static void __i2c_scan(void)
{
    void *iic;
    int   ack_count = 0;

    printf("[I2CSCAN] begin\n");
    printf("[I2CSCAN] expected iic1 = software IIC, SCL=PH0, SDA=PH1, sw_iic_delay=50\n");
    printf("[I2CSCAN] PORT H: OUT=0x%08x IN=0x%08x DIR=0x%08x DIE=0x%08x PU=0x%08x PD=0x%08x\n",
           (unsigned)JL_PORTH->OUT, (unsigned)JL_PORTH->IN, (unsigned)JL_PORTH->DIR, (unsigned)JL_PORTH->DIE,
           (unsigned)JL_PORTH->PU, (unsigned)JL_PORTH->PD);
    __scan_port_line("PH0/SCL", 0);
    __scan_port_line("PH1/SDA", 1);
    printf("[I2CSCAN] IOMAP: CON0=0x%08x CON1=0x%08x CON2=0x%08x CON3=0x%08x\n", (unsigned)JL_IOMAP->CON0,
           (unsigned)JL_IOMAP->CON1, (unsigned)JL_IOMAP->CON2, (unsigned)JL_IOMAP->CON3);

    iic = dev_open("iic1", NULL);
    if (iic == NULL) {
        printf("[I2CSCAN] dev_open(iic1) failed\n");
        return;
    }

    for (int addr = I2C_SCAN_FIRST; addr <= I2C_SCAN_LAST; addr++) {
        int ret;

        dev_ioctl(iic, IIC_IOCTL_START, 0);
        ret = dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, (unsigned int)(addr << 1));
        dev_ioctl(iic, IIC_IOCTL_STOP, 0);

        if (ret == 0) {
            ack_count++;
            printf("[I2CSCAN] ACK 0x%02x (write byte 0x%02x)\n", addr, (addr << 1) & 0xff);
        }
    }

    printf("[I2CSCAN] scan 0x%02x-0x%02x done, %d ACK\n", I2C_SCAN_FIRST, I2C_SCAN_LAST, ack_count);
    if (ack_count == 0) {
        printf("[I2CSCAN] no ACK: bus idle/no device -> J4 power, module contact or SCL/SDA wiring\n");
    } else {
        printf("[I2CSCAN] expected: BYD3901 0x6e, GC0310 0x21, GC6133 0x40\n");
    }

    dev_close(iic);
}
#endif

void user_main(void)
{
    OPERATE_RET           rt;
    TDL_CAMERA_CFG_T      cfg;
    TDL_CAMERA_DEV_INFO_T info;

    /* First line of the application: proves the image reached user_main even
     * if the camera bring-up later hangs. printf() is the vendor console and
     * is already up from setup_arch(). */
#if defined(JIELI_CAMERA_PATH_SPI) && (JIELI_CAMERA_PATH_SPI == 1)
    printf("[SPI_PROBE] boot, path=SPI\n");
#elif defined(JIELI_CAMERA_PATH_DVP) && (JIELI_CAMERA_PATH_DVP == 1)
    printf("[SPI_PROBE] boot, path=DVP\n");
#else
    printf("[SPI_PROBE] boot, path=none\n");
#endif

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    rt = board_register_hardware();
    PR_NOTICE("board_register_hardware: %d", rt);

#if EXAMPLE_I2C_SCAN
    __i2c_scan();
    printf("[I2CSCAN] vendor check follows: BYD3901 -> GC0310 -> GC6133 (video1 probe order)\n");
#endif

    sg_camera = tdl_camera_find_dev((char *)CAMERA_NAME);
    if (sg_camera == NULL) {
        PR_ERR("camera device '%s' not found", CAMERA_NAME);
        return;
    }

    memset(&info, 0, sizeof(info));
    if (tdl_camera_dev_get_info(sg_camera, &info) == OPRT_OK) {
        PR_NOTICE("camera info: max %ux%u@%ufps fmts=0x%x", info.max_width, info.max_height, info.max_fps,
                  info.supported_fmts);
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.fps          = EXAMPLE_CAMERA_FPS;
    cfg.width        = EXAMPLE_CAMERA_WIDTH;
    cfg.height       = EXAMPLE_CAMERA_HEIGHT;
    cfg.out_fmt      = PROBE_FMT;
    cfg.get_frame_cb = __frame_cb;

    sg_last_report_ms = tal_system_get_millisecond();
    sg_open_ms        = sg_last_report_ms;
    rt                = tdl_camera_dev_open(sg_camera, &cfg);
    PR_NOTICE("tdl_camera_dev_open(%ux%u@%u): %d", cfg.width, cfg.height, cfg.fps, rt);
#if EXAMPLE_I2C_SCAN
    printf("[I2CSCAN] vendor check result rt=%d (0 = a sensor check passed)\n", rt);
#endif
    if (rt != OPRT_OK) {
        return;
    }

    while (1) {
        tal_system_sleep(1000);
    }
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;
    user_main();
}
#else
static THREAD_HANDLE ty_app_thread = NULL;

static void tuya_app_thread(void *arg)
{
    (void)arg;
    user_main();
    tal_thread_delete(ty_app_thread);
    ty_app_thread = NULL;
}

void tuya_app_main(void)
{
    THREAD_CFG_T thrd_param = {0};
    thrd_param.stackDepth = 1024 * 4;
    thrd_param.priority = THREAD_PRIO_1;
    thrd_param.thrdname = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
