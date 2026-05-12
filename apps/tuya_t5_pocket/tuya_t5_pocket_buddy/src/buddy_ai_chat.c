/**
 * @file buddy_ai_chat.c
 * @brief AI chat integration for Claude Buddy.
 *
 * Initializes the AI chat system (ASR, text streaming, optional video/MCP)
 * and bridges recognized speech to the Claude Desktop Buddy over BLE NUS.
 *
 * @copyright Copyright (c) 2025 Tuya Inc. All Rights Reserved.
 */

#include "tal_api.h"

#include "app_display.h"
#include "lv_vendor.h"
#include "ai_chat_main.h"
#include "buddy_ble.h"
#include "buddy_main_screen.h"

static volatile BOOL_T sg_text_stream_active = FALSE;

/**
 * @brief Clamp a UTF-8 byte length without splitting a multi-byte sequence.
 * @param[in] data input UTF-8 bytes
 * @param[in] full_len total available byte length
 * @param[in] max_len requested maximum byte length
 * @return safe byte length no greater than max_len
 */
static size_t __utf8_safe_prefix_len(const uint8_t *data, size_t full_len, size_t max_len)
{
    size_t len = full_len;

    if (data == NULL) {
        return 0;
    }
    if (len > max_len) {
        len = max_len;
        while (len > 0 && len < full_len && ((data[len] & 0xC0U) == 0x80U)) {
            len--;
        }
    }
    return len;
}

static void ai_chat_event_handler(AI_NOTIFY_EVENT_T *event)
{
    AI_NOTIFY_TEXT_T *text = NULL;

    switch (event->type) {
    case AI_USER_EVT_ASR_OK: {
        text = (AI_NOTIFY_TEXT_T *)event->data;
        /* Forward the recognized transcript to the Claude desktop buddy
         * over BLE NUS, tagged with the currently selected session id so
         * the host can route it to the right Claude Code session. Drops
         * silently if no buddy is connected — this path must not block. */
        if (text != NULL && text->data != NULL && text->datalen > 0) {
            size_t tlen = __utf8_safe_prefix_len((const uint8_t *)text->data, (size_t)text->datalen,
                                                 BUDDY_BLE_ASR_TEXT_MAX_BYTES);
            char *utf8 = (char *)tal_malloc(tlen + 1);
            if (utf8 != NULL) {
                memcpy(utf8, text->data, tlen);
                utf8[tlen] = '\0';
                char sid[12] = {0};
                (VOID_T)buddy_main_screen_get_selected_sid(sid, sizeof(sid));
                (VOID_T)buddy_ble_send_asr(utf8, sid);
                tal_free(utf8);
            }
        }
    } break;

    case AI_USER_EVT_TEXT_STREAM_START: {
    } break;
    case AI_USER_EVT_TEXT_STREAM_DATA: {
    } break;
    case AI_USER_EVT_TEXT_STREAM_STOP: {
        sg_text_stream_active = FALSE;
    } break;
    default:
        break;
    }
}

#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
static void ai_video_display_flush(TDL_CAMERA_FRAME_T *frame)
{
#if defined(ENABLE_COMP_AI_DISPLAY) && (ENABLE_COMP_AI_DISPLAY == 1)
    ai_ui_camera_flush(frame->data, frame->width, frame->height);
#endif
}
#endif

static void __buddy_display_init(void)
{
    lv_vendor_init(DISPLAY_NAME);

    ui_init();

    lv_vendor_start(5, 1024*8);
}

OPERATE_RET buddy_ai_chat_init(void)
{
    OPERATE_RET rt = OPRT_OK;

    __buddy_display_init();

    extern void buddy_indev_init(void);
    buddy_indev_init();

    TUYA_CALL_ERR_RETURN(ai_ui_chat_register());

    AI_CHAT_MODE_CFG_T ai_chat_cfg = {
        .default_mode = AI_CHAT_MODE_HOLD,
        .default_vol  = 70,
        .evt_cb       = ai_chat_event_handler,
    };
    TUYA_CALL_ERR_RETURN(ai_chat_init(&ai_chat_cfg));

#if defined(ENABLE_COMP_AI_VIDEO) && (ENABLE_COMP_AI_VIDEO == 1)
    AI_VIDEO_CFG_T ai_video_cfg = {
        .disp_flush_cb = ai_video_display_flush,
    };

    TUYA_CALL_ERR_LOG(ai_video_init(&ai_video_cfg));
#endif

#if defined(ENABLE_COMP_AI_MCP) && (ENABLE_COMP_AI_MCP == 1)
    TUYA_CALL_ERR_RETURN(ai_mcp_init());
#endif

    return OPRT_OK;
}

/**
 * @brief Get text stream status.
 * @return TRUE if text stream is active, FALSE otherwise.
 */
BOOL_T app_get_text_stream_status(void)
{
    return sg_text_stream_active;
}
