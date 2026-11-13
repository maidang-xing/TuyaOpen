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

#define PROBE_FMT TDL_CAMERA_FMT_YUV422

static TDL_CAMERA_HANDLE_T sg_camera;
static volatile uint32_t   sg_frames;
static volatile uint32_t   sg_bad_frames;
static uint32_t            sg_first_ms;
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
        PR_NOTICE("FIRST FRAME: %ux%u fmt=0x%x len=%u nonzero=%u first16=%02x %02x %02x %02x %02x %02x %02x %02x %02x "
                  "%02x %02x %02x %02x %02x %02x %02x",
                  frame->width, frame->height, (unsigned)frame->fmt, frame->data_len, nonzero, frame->data[0],
                  frame->data[1], frame->data[2], frame->data[3], frame->data[4], frame->data[5], frame->data[6],
                  frame->data[7], frame->data[8], frame->data[9], frame->data[10], frame->data[11], frame->data[12],
                  frame->data[13], frame->data[14], frame->data[15]);
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

void user_main(void)
{
    OPERATE_RET           rt;
    TDL_CAMERA_CFG_T      cfg;
    TDL_CAMERA_DEV_INFO_T info;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 4096, (TAL_LOG_OUTPUT_CB)tkl_log_output);

    rt = board_register_hardware();
    PR_NOTICE("board_register_hardware: %d", rt);

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
    rt                = tdl_camera_dev_open(sg_camera, &cfg);
    PR_NOTICE("tdl_camera_dev_open(%ux%u@%u): %d", cfg.width, cfg.height, cfg.fps, rt);
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
    thrd_param.stackDepth   = 1024 * 4;
    thrd_param.priority     = THREAD_PRIO_1;
    thrd_param.thrdname     = "tuya_app_main";
    tal_thread_create_and_start(&ty_app_thread, NULL, NULL, tuya_app_thread, NULL, &thrd_param);
}
#endif
