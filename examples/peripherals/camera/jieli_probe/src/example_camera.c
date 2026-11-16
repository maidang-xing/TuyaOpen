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
#define IIC_IOCTL_TX                _IO('I', 3)
#define IIC_IOCTL_TX_WITH_STOP_BIT  _IOW('I', 4, unsigned char)
#define IIC_IOCTL_RX_WITH_STOP_BIT  _IOR('I', 6, unsigned char)
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

/* Write one register over the vendor iic1 device. GalaxyCore parts need the
 * page register 0xFE set before the ID registers answer. */
static int __i2c_write_reg(void *iic, int addr, uint8_t reg, uint8_t val)
{
    int ret = -1;

    dev_ioctl(iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, (unsigned int)(addr << 1))) {
        goto out;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX, reg)) {
        goto out;
    }
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_STOP_BIT, val)) {
        goto out;
    }
    ret = 0;

out:
    dev_ioctl(iic, IIC_IOCTL_STOP, 0);
    return ret;
}

/* Read one register over the vendor iic1 device, so an ACK'd address can be
 * identified instead of guessed. Mirrors the GC0310 driver's read sequence. */
static int __i2c_read_reg(void *iic, int addr, uint8_t reg, uint8_t *val)
{
    int ret = -1;

    dev_ioctl(iic, IIC_IOCTL_START, 0);
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, (unsigned int)(addr << 1))) {
        goto out;
    }
    tal_system_sleep(2);
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_STOP_BIT, reg)) {
        goto out;
    }
    tal_system_sleep(2);
    if (dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, (unsigned int)((addr << 1) | 1))) {
        goto out;
    }
    tal_system_sleep(2);
    dev_ioctl(iic, IIC_IOCTL_RX_WITH_STOP_BIT, (unsigned int)val);
    ret = 0;

out:
    dev_ioctl(iic, IIC_IOCTL_STOP, 0);
    return ret;
}

/* Count transitions on PA2/PA3/PA4 by polling the port input register. */
static uint32_t __sample_pa_edges(uint32_t n, uint32_t *pa2, uint32_t *pa3, uint32_t *pa4)
{
    uint32_t prev  = JL_PORTA->IN;
    uint32_t edges = 0;
    uint32_t a = 0, b = 0, c = 0;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t in = JL_PORTA->IN;
        uint32_t ch = in ^ prev;

        if (ch & 0x1cu) {
            edges++;
            if (ch & 0x04) {
                a++;
            }
            if (ch & 0x08) {
                b++;
            }
            if (ch & 0x10) {
                c++;
            }
        }
        prev = in;
    }
    *pa2 = a;
    *pa3 = b;
    *pa4 = c;
    return edges;
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

    /* J4 carries RST (PH3) and PWDN. The vendor camera1_data uses PH3 as its
     * reset line, so release reset before reading IDs: a sensor held in reset
     * still ACKs its address but returns 0x00/0xff from every register. The
     * MCLK (PH2, output channel 1) must run as well, otherwise the sensor's
     * digital core cannot answer register reads. */
    gpio_output_channle(IO_PORTH_02, CH1_PLL_24M);
    tal_system_sleep(20);
    gpio_direction_output(IO_PORTH_03, 1);
    tal_system_sleep(10);
    printf("[I2CSCAN] XCLK PH2 on (CH1_PLL_24M), released RST (PH3 high)\n");

    for (int addr = I2C_SCAN_FIRST; addr <= I2C_SCAN_LAST; addr++) {
        int ret;

        dev_ioctl(iic, IIC_IOCTL_START, 0);
        ret = dev_ioctl(iic, IIC_IOCTL_TX_WITH_START_BIT, (unsigned int)(addr << 1));
        dev_ioctl(iic, IIC_IOCTL_STOP, 0);

        if (ret == 0) {
            ack_count++;
            printf("[I2CSCAN] ACK 0x%02x (write byte 0x%02x)\n", addr, (addr << 1) & 0xff);
            {
                static const uint8_t regs[] = {0x00, 0x0a, 0x0b, 0xf0, 0xf1};
                printf("[I2CSCAN]   0x%02x regs:", addr);
                for (unsigned i = 0; i < sizeof(regs); i++) {
                    uint8_t v = 0xff;
                    if (__i2c_read_reg(iic, addr, regs[i], &v) == 0) {
                        printf(" %02x=%02x", regs[i], v);
                    } else {
                        printf(" %02x=RD-ERR", regs[i]);
                    }
                }
                printf("\n");
            }
        }
    }

    printf("[I2CSCAN] scan 0x%02x-0x%02x done, %d ACK\n", I2C_SCAN_FIRST, I2C_SCAN_LAST, ack_count);
    if (ack_count == 0) {
        printf("[I2CSCAN] no ACK: bus idle/no device -> J4 power, module contact or SCL/SDA wiring\n");
    } else {
        /* GalaxyCore sensors answer their ID only after the page register is
         * set, and need a settle delay. Try the standard 0xFE=0xF0 page on
         * every address that ACKed; GC0310 should report 0xA3/0x10 and GC6133
         * 0xBA at 0xF0. */
        static const int candidates[] = {0x21, 0x38, 0x3c};

        for (unsigned c = 0; c < sizeof(candidates) / sizeof(candidates[0]); c++) {
            int     addr = candidates[c];
            uint8_t id0  = 0;
            uint8_t id1  = 0;

            printf("[I2CSCAN] identify 0x%02x: page 0xfe=0xf0\n", addr);
            if (__i2c_read_reg(iic, addr, 0xf0, &id0) != 0) {
                printf("[I2CSCAN] 0x%02x f0 read failed\n", addr);
                continue;
            }
            (void)__i2c_write_reg(iic, addr, 0xfe, 0xf0);
            tal_system_sleep(10);
            if (__i2c_read_reg(iic, addr, 0xf0, &id0) == 0 && __i2c_read_reg(iic, addr, 0xf1, &id1) == 0) {
                printf("[I2CSCAN] 0x%02x id f0=%02x f1=%02x\n", addr, id0, id1);
                if (addr == 0x21) {
                    /* Reproduce the vendor's reset (1 -> 0 -> 1) and read again:
                     * the probe reports "iic write err" right after this, so
                     * this isolates reset timing from the bus itself. */
                    gpio_direction_output(IO_PORTH_03, 1);
                    gpio_direction_output(IO_PORTH_03, 0);
                    tal_system_sleep(1);
                    gpio_direction_output(IO_PORTH_03, 1);
                    tal_system_sleep(5);
                    uint8_t r0  = 0;
                    uint8_t r1  = 0;
                    int     ok0 = __i2c_read_reg(iic, addr, 0xf0, &r0);
                    int     ok1 = __i2c_read_reg(iic, addr, 0xf1, &r1);
                    printf("[I2CSCAN] 0x21 after reset: ok=%d/%d f0=%02x f1=%02x\n", ok0, ok1, r0, r1);
                }
            } else {
                printf("[I2CSCAN] 0x%02x id read failed\n", addr);
            }
        }
    }

    dev_close(iic);

    /* The vendor drivers open iic0 for ISP_DEV_0 and iic1 otherwise. Check the
     * same sensor through iic0 as well: if only one bus answers, the probe
     * failure is a bus routing issue, not the sensor. */
    {
        void   *iic0 = dev_open("iic0", NULL);
        uint8_t v    = 0;

        if (iic0 == NULL) {
            printf("[I2CSCAN] dev_open(iic0) failed\n");
        } else {
            (void)__i2c_write_reg(iic0, 0x21, 0xfe, 0xf0);
            tal_system_sleep(5);
            int ok = __i2c_read_reg(iic0, 0x21, 0xf0, &v);
            printf("[I2CSCAN] iic0 0x21 f0: ok=%d val=%02x\n", ok, v);
            dev_close(iic0);
        }
    }

    /* Which of PA2/PA3/PA4 carries the sensor clock? Open video1 so the sensor
     * starts streaming, then sample the three pins as inputs and count edges.
     *
     * The vendor table keeps its SPI-output block on page 0x03
     * (GC0310_config_SENSOR: 0xfe=0x03 then 0x02/0x03/0x51..0x67, with 0x64
     * "sck always" and 0x65 the head sync code). If those writes did not stick
     * the sensor stays mute on PCLK, so read the page back before sampling. */
    {
        void *vdev = dev_open("video1.0", NULL);

        if (vdev == NULL) {
            printf("[I2CPIN] video1.0 open failed\n");
        } else {
            tal_system_sleep(300);
            printf("[I2CPIN] after video1 open: IOMAP CON0=0x%08x CON1=0x%08x CON2=0x%08x\n", (unsigned)JL_IOMAP->CON0,
                   (unsigned)JL_IOMAP->CON1, (unsigned)JL_IOMAP->CON2);
            printf("[I2CPIN] PORT A: DIR=0x%08x DIE=0x%08x IN=0x%08x\n", (unsigned)JL_PORTA->DIR,
                   (unsigned)JL_PORTA->DIE, (unsigned)JL_PORTA->IN);

            {
                void         *ii      = dev_open("iic1", NULL);
                uint8_t       v       = 0;
                const uint8_t page3[] = {0x02, 0x03, 0x51, 0x53, 0x59, 0x5d, 0x60, 0x64, 0x65};
                int           k;

                if (ii != NULL) {
                    (void)__i2c_write_reg(ii, 0x21, 0xfe, 0x03);
                    tal_system_sleep(5);
                    for (k = 0; k < (int)sizeof(page3); k++) {
                        if (__i2c_read_reg(ii, 0x21, page3[k], &v) == 0) {
                            printf("[I2CPIN] page3 0x%02x=0x%02x\n", page3[k], v);
                        } else {
                            printf("[I2CPIN] page3 0x%02x read failed\n", page3[k]);
                        }
                    }
                    /* Leave the part on its default page, as the vendor table
                     * does at the end of channel setup. */
                    (void)__i2c_write_reg(ii, 0x21, 0xfe, 0x00);
                    dev_close(ii);
                }
            }

            gpio_direction_input(IO_PORTA_02);
            gpio_direction_input(IO_PORTA_03);
            gpio_direction_input(IO_PORTA_04);

            /* video1.0 open clears the PH2 XCLK output channel, so the module
             * is mute until it is re-armed after dev_open (measured: PA3 shows
             * no PCLK at all before the re-arm, thousands of edges after). */
            gpio_output_channle(IO_PORTH_02, CH2_PLL_24M);
            tal_system_sleep(150);

            {
                uint32_t cnt[3] = {0};
                uint32_t edges  = __sample_pa_edges(8000000, &cnt[0], &cnt[1], &cnt[2]);

                /* The sensor only clocks PCLK while a frame is on the wire (the
                 * vendor header: "有数据的时候才有时钟，空闲时候时钟无"), and one
                 * VGA frame is ~13 ms of activity in a ~66 ms period, so a
                 * short window usually lands in the idle gap. 8M iterations
                 * span several frames. */
                printf("[I2CPIN] samples=8000000 edges=%u PA2=%u PA3=%u PA4=%u last=0x%08x\n", (unsigned)edges,
                       (unsigned)cnt[0], (unsigned)cnt[1], (unsigned)cnt[2], (unsigned)JL_PORTA->IN);
            }
            dev_close(vdev);
        }
    }
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
