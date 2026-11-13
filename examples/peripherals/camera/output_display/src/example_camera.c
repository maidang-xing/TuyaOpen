/**
 * @file example_camera.c
 * @brief Camera preview on the board display.
 *
 * Opens the camera in YUV422, converts each frame to the panel's RGB565
 * framebuffer and flushes it. On Jieli the vendor delivers frames from its own
 * raw_flow_task; the conversion and flush are handed to a worker thread so the
 * vendor task only copies the frame.
 *
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_cloud_types.h"
#include <string.h>
#include <stdio.h>

#include "tal_api.h"
#include "tkl_output.h"
#include "tkl_memory.h"

#include "board_com_api.h"

#include "tdl_display_manage.h"
#include "tdl_camera_manage.h"

/***********************************************************
*************************micro define***********************
***********************************************************/
#define DISPLAY_FRAME_BUFF_NUM 2

/* Bring-up bisect switch: 1 fills the frame buffer with a solid colour and
 * skips the YUV422 conversion, so a crash here isolates the display/EMI flush
 * path from the camera frame read. 0 is the normal example. */
#define EXAMPLE_SOLID_COLOR_TEST 0

/* Display-only bring-up: fill and flush one frame buffer from user_main before
 * the camera is opened. This separates the panel/EMI path from the camera
 * callback thread. */
#define EXAMPLE_DISPLAY_ONLY 0

/* The vendor delivers frames on its own raw_flow_task (8 KB stack). Doing the
 * YUV422->RGB565 conversion and the panel flush there hangs/faults on this
 * board, while the identical calls work on a Tuya thread. Hand each frame to a
 * worker instead: the callback only copies, the worker converts and flushes. */
#define EXAMPLE_DISPLAY_WORKER 1

/* Single-frame bring-up test, entirely on the Tuya main thread: the camera
 * callback only copies one real frame (after cache maintenance), then
 * user_main converts and flushes it. No worker thread, no conversion on the
 * vendor raw_flow_task. */
#define EXAMPLE_CAMTEST 0

#if defined(EXAMPLE_CAMERA_IMG_ROTATION_0) && (EXAMPLE_CAMERA_IMG_ROTATION_0 == 1)
#define EXAMPLE_CAMERA_IMG_ROTATION TUYA_DISPLAY_ROTATION_0
#elif defined(EXAMPLE_CAMERA_IMG_ROTATION_90) && (EXAMPLE_CAMERA_IMG_ROTATION_90 == 1)
#define EXAMPLE_CAMERA_IMG_ROTATION TUYA_DISPLAY_ROTATION_90
#elif defined(EXAMPLE_CAMERA_IMG_ROTATION_180) && (EXAMPLE_CAMERA_IMG_ROTATION_180 == 1)
#define EXAMPLE_CAMERA_IMG_ROTATION TUYA_DISPLAY_ROTATION_180
#elif defined(EXAMPLE_CAMERA_IMG_ROTATION_270) && (EXAMPLE_CAMERA_IMG_ROTATION_270 == 1)
#define EXAMPLE_CAMERA_IMG_ROTATION TUYA_DISPLAY_ROTATION_270
#else
#define EXAMPLE_CAMERA_IMG_ROTATION TUYA_DISPLAY_ROTATION_0
#endif

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/
static TDL_DISP_HANDLE_T      sg_tdl_disp_hdl = NULL;
static TDL_DISP_DEV_INFO_T    sg_display_info;
static TDL_FB_MANAGE_HANDLE_T sg_fb_manage = NULL;

static TDL_CAMERA_HANDLE_T sg_tdl_camera_hdl = NULL;
static TUYA_YUV422_ORDER_E sg_yuv_order      = TUYA_YUV422_UYVY;

#if EXAMPLE_DISPLAY_WORKER
static SEM_HANDLE       sg_frame_sem     = NULL;
static THREAD_HANDLE    sg_worker_hdl    = NULL;
static uint8_t         *sg_yuv_pending   = NULL;
static uint32_t         sg_yuv_size      = 0;
static volatile uint8_t sg_pending_ready = 0;
static volatile uint8_t sg_worker_busy   = 0;
#endif

/***********************************************************
***********************function define**********************
***********************************************************/
#if EXAMPLE_DISPLAY_WORKER
#if defined(JIELI_SELECTED_CHIP_WL82)
/* Written by the vendor ISP DMA; drop stale lines before the CPU copies it. */
extern void flushinv_dcache(void *ptr, int len);
#endif

static void __display_worker(void *arg)
{
    (void)arg;

    printf("[DISP_WORKER] thread running\n");
    while (1) {
        if (tal_semaphore_wait(sg_frame_sem, SEM_WAIT_FOREVER) != OPRT_OK) {
            continue;
        }
        sg_pending_ready = 0;
        sg_worker_busy   = 1;

        TDL_DISP_FRAME_BUFF_T *fb = tdl_disp_get_free_fb(sg_fb_manage);
        if (fb != NULL) {
            printf("[DISP_WORKER] convert %ux%u\n", EXAMPLE_CAMERA_WIDTH, EXAMPLE_CAMERA_HEIGHT);
            tdl_disp_convert_yuv422_to_fb(sg_yuv_pending, EXAMPLE_CAMERA_WIDTH, EXAMPLE_CAMERA_HEIGHT, fb,
                                          sg_display_info.is_swap, EXAMPLE_CAMERA_IMG_ROTATION, sg_yuv_order);
            printf("[DISP_WORKER] convert done, flush\n");
            tdl_disp_dev_flush(sg_tdl_disp_hdl, fb);
            printf("[DISP_WORKER] flush done\n");
        }

        sg_worker_busy = 0;
    }
}
#endif
static OPERATE_RET __get_camera_raw_frame_cb(TDL_CAMERA_HANDLE_T hdl, TDL_CAMERA_FRAME_T *frame)
{
    OPERATE_RET rt = OPRT_OK;
#if !EXAMPLE_DISPLAY_WORKER
    TDL_DISP_FRAME_BUFF_T *convert_fb = NULL;
#endif

#if EXAMPLE_DISPLAY_WORKER
    /* This runs on the vendor raw_flow_task: keep it to a copy and a post. */
    if (sg_yuv_pending == NULL || sg_worker_busy || sg_pending_ready || frame->data == NULL ||
        frame->data_len > sg_yuv_size) {
        return OPRT_OK;
    }
    {
        static uint32_t cb_cnt;
        if (cb_cnt < 3) {
            printf("[DISP_WORKER] cb data=%p len=%u\n", frame->data, frame->data_len);
            cb_cnt++;
        }
    }
#if defined(JIELI_SELECTED_CHIP_WL82)
    flushinv_dcache(frame->data, (int)frame->data_len);
#endif
    memcpy(sg_yuv_pending, frame->data, frame->data_len);
    sg_pending_ready = 1;
    tal_semaphore_post(sg_frame_sem);
    return OPRT_OK;
#else
    convert_fb = tdl_disp_get_free_fb(sg_fb_manage);
    TUYA_CHECK_NULL_RETURN(convert_fb, OPRT_COM_ERROR);

    {
        static uint32_t dbg_cnt;
        if (dbg_cnt < 4) {
            printf("[disp-dbg] frame data=%p wh=%ux%u len=%u fb=%p fbwh=%ux%u fblen=%u in_order=%d\n", frame->data,
                   frame->width, frame->height, frame->data_len, convert_fb->frame, convert_fb->width,
                   convert_fb->height, convert_fb->len, sg_yuv_order);
            dbg_cnt++;
        }
    }

#if EXAMPLE_SOLID_COLOR_TEST
    /* RGB565 red, byte order swapped so both halves show the colour. */
    memset(convert_fb->frame, 0xF8, convert_fb->len);
    (void)frame;
    printf("[disp-dbg] solid fill done\n");
#else
    TUYA_CALL_ERR_LOG(tdl_disp_convert_yuv422_to_fb(frame->data, frame->width, frame->height, convert_fb,
                                                    sg_display_info.is_swap, EXAMPLE_CAMERA_IMG_ROTATION,
                                                    sg_yuv_order));
    printf("[disp-dbg] convert done\n");
#endif

    tdl_disp_dev_flush(sg_tdl_disp_hdl, convert_fb);
    printf("[disp-dbg] flush returned\n");

    return rt;
#endif
}

#if defined(JIELI_SELECTED_CHIP_WL82)
/* The vendor ISP writes the overlay buffer; drop stale CPU cache lines before
 * reading it. Declared here because the example does not pull vendor headers. */
extern void flushinv_dcache(void *ptr, int len);
#endif

#if EXAMPLE_CAMTEST
static SEM_HANDLE       sg_camtest_sem = NULL;
static uint8_t         *sg_camtest_buf = NULL;
static uint32_t         sg_camtest_len = 0;
static volatile uint8_t sg_camtest_got = 0;

static OPERATE_RET __camtest_frame_cb(TDL_CAMERA_HANDLE_T hdl, TDL_CAMERA_FRAME_T *frame)
{
    (void)hdl;

    if (sg_camtest_got || frame->data == NULL || frame->data_len == 0 || sg_camtest_buf == NULL) {
        return OPRT_OK;
    }

    printf("[CAMTEST] cb frame data=%p len=%u\n", frame->data, frame->data_len);
#if defined(JIELI_SELECTED_CHIP_WL82)
    flushinv_dcache(frame->data, (int)frame->data_len);
    printf("[CAMTEST] cb cache invalidated\n");
#endif
    uint32_t len = frame->data_len;
    if (len > (uint32_t)EXAMPLE_CAMERA_WIDTH * EXAMPLE_CAMERA_HEIGHT * 2u) {
        len = (uint32_t)EXAMPLE_CAMERA_WIDTH * EXAMPLE_CAMERA_HEIGHT * 2u;
    }
    memcpy(sg_camtest_buf, frame->data, len);
    sg_camtest_len = len;
    sg_camtest_got = 1;
    printf("[CAMTEST] cb copied %u bytes\n", len);
    tal_semaphore_post(sg_camtest_sem);

    return OPRT_OK;
}
#endif

static OPERATE_RET __display_init(void)
{
    OPERATE_RET rt     = OPRT_OK;
    uint16_t    width  = EXAMPLE_CAMERA_WIDTH;
    uint16_t    height = EXAMPLE_CAMERA_HEIGHT;

    memset(&sg_display_info, 0, sizeof(TDL_DISP_DEV_INFO_T));

    sg_tdl_disp_hdl = tdl_disp_find_dev(DISPLAY_NAME);
    if (NULL == sg_tdl_disp_hdl) {
        PR_ERR("display dev %s not found", DISPLAY_NAME);
        return OPRT_NOT_FOUND;
    }

    TUYA_CALL_ERR_RETURN(tdl_disp_dev_get_info(sg_tdl_disp_hdl, &sg_display_info));
    printf("[disp-dbg] info %ux%u fmt=%d swap=%d\n", sg_display_info.width, sg_display_info.height, sg_display_info.fmt,
           sg_display_info.is_swap);

    if (sg_display_info.fmt != TUYA_PIXEL_FMT_RGB565 && sg_display_info.fmt != TUYA_PIXEL_FMT_MONOCHROME) {
        PR_ERR("display pixel format %d not supported", sg_display_info.fmt);
        return OPRT_NOT_SUPPORTED;
    }

    TUYA_CALL_ERR_RETURN(tdl_disp_dev_open(sg_tdl_disp_hdl));

    tdl_disp_set_brightness(sg_tdl_disp_hdl, 100); // Set brightness to 100%

    TUYA_CALL_ERR_RETURN(tdl_disp_fb_manage_init(&sg_fb_manage));

    /*create frame buffer*/
    width  = sg_display_info.width;
    height = sg_display_info.height;

    for (uint8_t i = 0; i < DISPLAY_FRAME_BUFF_NUM; i++) {
        TUYA_CALL_ERR_LOG(tdl_disp_fb_manage_add(sg_fb_manage, sg_display_info.fmt, width, height));
    }
    printf("[disp-dbg] fb add %ux%u fmt=%d num=%d\n", width, height, sg_display_info.fmt, DISPLAY_FRAME_BUFF_NUM);

#if EXAMPLE_DISPLAY_ONLY
    {
        TDL_DISP_FRAME_BUFF_T *fb = tdl_disp_get_free_fb(sg_fb_manage);
        if (fb == NULL) {
            printf("[disp-dbg] selftest: no free fb\n");
            return OPRT_COM_ERROR;
        }
        printf("[disp-dbg] selftest fb=%p len=%u\n", fb->frame, fb->len);
        fb->frame[0] = 0xF8;
        printf("[disp-dbg] selftest first byte written\n");
        memset(fb->frame + 1, 0xF8, fb->len - 1);
        printf("[disp-dbg] selftest fill done\n");
        tdl_disp_dev_flush(sg_tdl_disp_hdl, fb);
        printf("[disp-dbg] selftest flush returned\n");

        /* Exercise the conversion itself on this thread (4 KB stack) with a
         * synthetic 640x480 YUV422 frame, so the camera callback thread is not
         * part of the experiment. */
        uint8_t *yuv = tkl_system_malloc(640 * 480 * 2);
        if (yuv != NULL) {
            for (uint32_t i = 0; i < 640u * 480u * 2u; i++) {
                yuv[i] = (uint8_t)(i & 0xff);
            }
            fb = tdl_disp_get_free_fb(sg_fb_manage);
            if (fb != NULL) {
                printf("[disp-dbg] selftest convert start fb=%p len=%u\n", fb->frame, fb->len);
                tdl_disp_convert_yuv422_to_fb(yuv, 640, 480, fb, false, TUYA_DISPLAY_ROTATION_0, TUYA_YUV422_UYVY);
                printf("[disp-dbg] selftest convert done\n");
                tdl_disp_dev_flush(sg_tdl_disp_hdl, fb);
                printf("[disp-dbg] selftest convert flush returned\n");
            }
            tkl_system_free(yuv);
        } else {
            printf("[disp-dbg] selftest no yuv buffer\n");
        }
    }
#endif

    return OPRT_OK;
}

#if EXAMPLE_CAMTEST
static OPERATE_RET __camtest_run(void)
{
    OPERATE_RET rt = OPRT_OK;

    TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&sg_camtest_sem, 0, 1));

    sg_camtest_buf = tkl_system_malloc((uint32_t)EXAMPLE_CAMERA_WIDTH * EXAMPLE_CAMERA_HEIGHT * 2u);
    if (sg_camtest_buf == NULL) {
        printf("[CAMTEST] no memory for the frame copy\n");
        return OPRT_MALLOC_FAILED;
    }

    sg_tdl_camera_hdl = tdl_camera_find_dev(CAMERA_NAME);
    if (sg_tdl_camera_hdl == NULL) {
        printf("[CAMTEST] camera dev %s not found\n", CAMERA_NAME);
        return OPRT_NOT_FOUND;
    }

    TDL_CAMERA_DEV_INFO_T cam_info = {0};
    tdl_camera_dev_get_info(sg_tdl_camera_hdl, &cam_info);
    sg_yuv_order = TUYA_YUV422_YUYV;

    TDL_CAMERA_CFG_T cfg;
    memset(&cfg, 0, sizeof(TDL_CAMERA_CFG_T));
    cfg.fps          = EXAMPLE_CAMERA_FPS;
    cfg.width        = EXAMPLE_CAMERA_WIDTH;
    cfg.height       = EXAMPLE_CAMERA_HEIGHT;
    cfg.out_fmt      = TDL_CAMERA_FMT_YUV422;
    cfg.get_frame_cb = __camtest_frame_cb;

    printf("[CAMTEST] opening camera %ux%u@%u yuv_order=%d\n", EXAMPLE_CAMERA_WIDTH, EXAMPLE_CAMERA_HEIGHT,
           EXAMPLE_CAMERA_FPS, sg_yuv_order);
    TUYA_CALL_ERR_RETURN(tdl_camera_dev_open(sg_tdl_camera_hdl, &cfg));
    printf("[CAMTEST] camera opened, waiting for one frame\n");

    rt = tal_semaphore_wait(sg_camtest_sem, 5000);
    printf("[CAMTEST] wait rt=%d got=%u len=%u\n", rt, sg_camtest_got, sg_camtest_len);

    tdl_camera_dev_close(sg_tdl_camera_hdl);
    printf("[CAMTEST] camera closed\n");

    if (!sg_camtest_got) {
        printf("[CAMTEST] no frame within 5 s\n");
        return OPRT_TIMEOUT;
    }

    TDL_DISP_FRAME_BUFF_T *fb = tdl_disp_get_free_fb(sg_fb_manage);
    if (fb == NULL) {
        printf("[CAMTEST] no free display fb\n");
        return OPRT_COM_ERROR;
    }

    printf("[CAMTEST] convert begin fb=%p len=%u\n", fb->frame, fb->len);
    tdl_disp_convert_yuv422_to_fb(sg_camtest_buf, EXAMPLE_CAMERA_WIDTH, EXAMPLE_CAMERA_HEIGHT, fb,
                                  sg_display_info.is_swap, EXAMPLE_CAMERA_IMG_ROTATION, sg_yuv_order);
    printf("[CAMTEST] convert done, flush\n");
    tdl_disp_dev_flush(sg_tdl_disp_hdl, fb);
    printf("[CAMTEST] flush done\n");

    return OPRT_OK;
}
#endif

static OPERATE_RET __camera_init(void)
{
    OPERATE_RET      rt = OPRT_OK;
    TDL_CAMERA_CFG_T cfg;

    memset(&cfg, 0, sizeof(TDL_CAMERA_CFG_T));

    sg_tdl_camera_hdl = tdl_camera_find_dev(CAMERA_NAME);
    if (NULL == sg_tdl_camera_hdl) {
        PR_ERR("camera dev %s not found", CAMERA_NAME);
        return OPRT_NOT_FOUND;
    }

    /* Get YUV byte order from camera driver info */
    TDL_CAMERA_DEV_INFO_T cam_info = {0};
    tdl_camera_dev_get_info(sg_tdl_camera_hdl, &cam_info);
    /* The vendor logs GC2145 as YUYV while the TDD reports UYVY; the image on
     * the panel is colour-swapped with the TDD default, so pin YUYV here. */
    sg_yuv_order = TUYA_YUV422_YUYV;
    printf("[DISP_WORKER] cam_info.yuv_order=%d, using YUYV\n", cam_info.yuv_order);

    cfg.fps     = EXAMPLE_CAMERA_FPS;
    cfg.width   = EXAMPLE_CAMERA_WIDTH;
    cfg.height  = EXAMPLE_CAMERA_HEIGHT;
    cfg.out_fmt = TDL_CAMERA_FMT_YUV422;

    cfg.get_frame_cb = __get_camera_raw_frame_cb;

#if EXAMPLE_DISPLAY_WORKER
    sg_yuv_size    = (uint32_t)EXAMPLE_CAMERA_WIDTH * EXAMPLE_CAMERA_HEIGHT * 2u;
    sg_yuv_pending = tkl_system_malloc(sg_yuv_size);
    if (sg_yuv_pending == NULL) {
        PR_ERR("no memory for the %u-byte display frame", (unsigned)sg_yuv_size);
        return OPRT_MALLOC_FAILED;
    }
    TUYA_CALL_ERR_RETURN(tal_semaphore_create_init(&sg_frame_sem, 0, 1));

    THREAD_CFG_T worker_cfg = {0};
    worker_cfg.stackDepth   = 8192;
    worker_cfg.priority     = THREAD_PRIO_3;
    worker_cfg.thrdname     = "camera_disp";
    TUYA_CALL_ERR_RETURN(tal_thread_create_and_start(&sg_worker_hdl, NULL, NULL, __display_worker, NULL, &worker_cfg));
#endif

    TUYA_CALL_ERR_RETURN(tdl_camera_dev_open(sg_tdl_camera_hdl, &cfg));

    PR_NOTICE("camera init success");

    return OPRT_OK;
}

/**
 * @brief user_main
 *
 * @param[in] param:Task parameters
 * @return none
 */
void user_main(void)
{
    OPERATE_RET rt = OPRT_OK;

    printf("[CAMTEST] begin\n");
#if EXAMPLE_DISPLAY_WORKER
    printf("[DISP_WORKER] init\n");
#endif

    /* basic init */
    tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    /*hardware register*/
    board_register_hardware();

    TUYA_CALL_ERR_LOG(__display_init());

#if EXAMPLE_CAMTEST
    TUYA_CALL_ERR_LOG(__camtest_run());
#elif !EXAMPLE_DISPLAY_ONLY
    TUYA_CALL_ERR_LOG(__camera_init());
#else
    printf("[disp-dbg] display-only build, camera not opened\n");
#endif

    while (1) {
        tal_system_sleep(1000);
    }
}

/**
 * @brief main
 *
 * @param argc
 * @param argv
 * @return void
 */
#if OPERATING_SYSTEM == SYSTEM_LINUX
void main(int argc, char *argv[])
{
    user_main();

    while (1) {
        tal_system_sleep(500);
    }
}
#else

/* Tuya thread handle */
static THREAD_HANDLE ty_app_thread = NULL;

/**
 * @brief  task thread
 *
 * @param[in] arg:Parameters when creating a task
 * @return none
 */
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
