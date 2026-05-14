/**
 * @file game_ai_chat.c
 * @version 0.1
 * @date 2025-03-25
 */
#include "tal_api.h"

#include "ai_chat_main.h"
#include "buddy_transport.h"
#include "buddy_protocol.h"
#include "screen_manager.h"
#include "main_screen.h"
#include "lv_vendor.h"
/***********************************************************
************************macro define************************
***********************************************************/

/***********************************************************
***********************typedef define***********************
***********************************************************/

/***********************************************************
***********************variable define**********************
***********************************************************/

/***********************************************************
***********************function define**********************
***********************************************************/
static void __ai_chat_handle_event(AI_NOTIFY_EVENT_T *event)
{

}

#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
static void __ai_video_display_flush(TDL_CAMERA_FRAME_T *frame)
{
#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    ai_ui_camera_flush(frame->data, frame->width, frame->height);
#endif
}
#endif

OPERATE_RET buddy_chat_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    // custom ui register
    buddy_state_init();

    lv_vendor_init(DISPLAY_NAME);
    ui_init();
    lv_vendor_start(5, 1024 * 8);

    extern void buddy_indev_init(void);
    buddy_indev_init();

    buddy_protocol_init();
    buddy_protocol_set_state_cb(buddy_main_screen_update_state);
    buddy_ws_init();
    // buddy_ws_start();
    tal_event_subscribe(EVENT_MQTT_CONNECTED, "acp_client_init", buddy_ws_start, SUBSCRIBE_TYPE_NORMAL);

    AI_CHAT_MODE_CFG_T ai_chat_cfg = {
        .default_mode = AI_CHAT_MODE_HOLD,
        .default_vol  = 70,
        .evt_cb       = __ai_chat_handle_event,
    };
    TUYA_CALL_ERR_RETURN(ai_chat_init(&ai_chat_cfg));

#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
    AI_VIDEO_CFG_T ai_video_cfg = {
        .disp_flush_cb = __ai_video_display_flush,
    };

    TUYA_CALL_ERR_LOG(ai_video_init(&ai_video_cfg));
#endif

#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
    TUYA_CALL_ERR_RETURN(ai_mcp_init());
#endif

    return OPRT_OK;
}