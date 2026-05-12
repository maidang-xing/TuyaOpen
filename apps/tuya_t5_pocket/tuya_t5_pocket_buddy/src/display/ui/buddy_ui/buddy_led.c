/**
 * @file buddy_led.c
 * @brief buddy_led 实现：状态去重 + tdl_led 调用 + 状态迁移日志。
 *
 * 闪烁模式选择：
 *   OFF         → tdl_led_set_status(OFF)
 *   ON_DIM      → tdl_led_set_status(ON)（T5AI-Pocket GPIO LED 无 PWM，ON=满亮）
 *   BLINK_SLOW  → tdl_led_flash(500)              1 Hz
 *   BLINK_FAST  → tdl_led_flash(125)              4 Hz
 *   FLASH_ONCE  → tdl_led_blink(cnt=1, 200/200)   然后自动回到之前状态
 *
 * FLASH_ONCE 语义：发送一次 200ms 脉冲后恢复到上一个稳定状态；由上层
 * （buddy_main_screen）负责在下次 tick 将 led_state 切回 IDLE/BUSY/...。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "buddy_led.h"
#include "tdl_led_manage.h"
#include "tal_api.h"
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#ifndef LED_NAME
/* 未在 board 中定义 LED_NAME 时退化为空字符串；tdl_led_find_dev 会返回
 * NULL，buddy_led 将以"仅日志"模式工作。 */
#define LED_NAME ""
#endif

#define LED_FLASH_SLOW_HALF_MS  500u
#define LED_FLASH_FAST_HALF_MS  125u
#define LED_FLASH_ONCE_HALF_MS  200u

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC TDL_LED_HANDLE_T    s_led_hdl   = NULL;
STATIC BOOL_T              s_inited    = FALSE;
STATIC buddy_led_state_e   s_state     = BUDDY_LED_STATE_OFF;

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC const char *__state_name(buddy_led_state_e s);
STATIC OPERATE_RET __apply(buddy_led_state_e s);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief 初始化 LED 句柄。
 */
OPERATE_RET buddy_led_init(VOID_T)
{
    if (s_inited) {
        return OPRT_OK;
    }
    s_led_hdl = tdl_led_find_dev((char *)LED_NAME);
    if (s_led_hdl == NULL) {
        PR_WARN("buddy_led: LED_NAME=\"%s\" not registered, falling back to log-only",
                LED_NAME);
        s_inited = TRUE;
        return OPRT_COM_ERROR;
    }
    OPERATE_RET rt = tdl_led_open(s_led_hdl);
    if (rt != OPRT_OK) {
        PR_WARN("buddy_led: tdl_led_open failed %d, falling back to log-only", rt);
        s_led_hdl = NULL;
    }
    s_inited = TRUE;
    s_state  = BUDDY_LED_STATE_OFF;
    (VOID_T)__apply(s_state);
    PR_NOTICE("buddy_led init done (device=%s handle=%p)",
              LED_NAME, s_led_hdl);
    return OPRT_OK;
}

/**
 * @brief 释放 LED 句柄。
 */
OPERATE_RET buddy_led_deinit(VOID_T)
{
    if (!s_inited) {
        return OPRT_OK;
    }
    if (s_led_hdl != NULL) {
        (VOID_T)tdl_led_set_status(s_led_hdl, TDL_LED_OFF);
        (VOID_T)tdl_led_close(s_led_hdl);
        s_led_hdl = NULL;
    }
    s_inited = FALSE;
    s_state  = BUDDY_LED_STATE_OFF;
    return OPRT_OK;
}

/**
 * @brief 设置 LED 状态，去重 + 日志。
 */
OPERATE_RET buddy_led_set(buddy_led_state_e state)
{
    if (state >= BUDDY_LED_STATE_COUNT) {
        return OPRT_INVALID_PARM;
    }
    if (!s_inited) {
        (VOID_T)buddy_led_init();
    }
    if (state == s_state && state != BUDDY_LED_STATE_FLASH_ONCE) {
        return OPRT_OK;
    }
    PR_DEBUG("buddy_led %s -> %s", __state_name(s_state), __state_name(state));
    OPERATE_RET rt = __apply(state);
    if (rt == OPRT_OK) {
        s_state = state;
    }
    return rt;
}

/**
 * @brief 读取当前状态。
 */
buddy_led_state_e buddy_led_current(VOID_T)
{
    return s_state;
}

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief 状态枚举转字符串（仅用于日志）。
 */
STATIC const char *__state_name(buddy_led_state_e s)
{
    switch (s) {
    case BUDDY_LED_STATE_OFF:        return "OFF";
    case BUDDY_LED_STATE_ON_DIM:     return "ON_DIM";
    case BUDDY_LED_STATE_BLINK_SLOW: return "BLINK_SLOW";
    case BUDDY_LED_STATE_BLINK_FAST: return "BLINK_FAST";
    case BUDDY_LED_STATE_FLASH_ONCE: return "FLASH_ONCE";
    default:                         return "?";
    }
}

/**
 * @brief 下发状态到 tdl_led；LED 句柄不可用时只打日志。
 */
STATIC OPERATE_RET __apply(buddy_led_state_e state)
{
    if (s_led_hdl == NULL) {
        return OPRT_OK;
    }

    switch (state) {
    case BUDDY_LED_STATE_OFF:
        return tdl_led_set_status(s_led_hdl, TDL_LED_OFF);

    case BUDDY_LED_STATE_ON_DIM:
        return tdl_led_set_status(s_led_hdl, TDL_LED_ON);

    case BUDDY_LED_STATE_BLINK_SLOW:
        return tdl_led_flash(s_led_hdl, LED_FLASH_SLOW_HALF_MS);

    case BUDDY_LED_STATE_BLINK_FAST:
        return tdl_led_flash(s_led_hdl, LED_FLASH_FAST_HALF_MS);

    case BUDDY_LED_STATE_FLASH_ONCE: {
        TDL_LED_BLINK_CFG_T cfg = {
            .cnt                    = 1,
            .start_stat             = TDL_LED_ON,
            .end_stat               = TDL_LED_OFF,
            .first_half_cycle_time  = LED_FLASH_ONCE_HALF_MS,
            .latter_half_cycle_time = LED_FLASH_ONCE_HALF_MS,
        };
        return tdl_led_blink(s_led_hdl, &cfg);
    }

    default:
        return OPRT_INVALID_PARM;
    }
}
