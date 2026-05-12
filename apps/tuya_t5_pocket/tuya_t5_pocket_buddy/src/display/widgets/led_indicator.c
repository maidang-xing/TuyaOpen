/**
 * @file led_indicator.c
 * @brief LED indicator implementation: state deduplication + tdl_led calls + transition logging.
 *
 * Blink mode selection:
 *   OFF         -> tdl_led_set_status(OFF)
 *   ON_DIM      -> tdl_led_set_status(ON) (T5AI-Pocket GPIO LED has no PWM, ON=full bright)
 *   BLINK_SLOW  -> tdl_led_flash(500)              1 Hz
 *   BLINK_FAST  -> tdl_led_flash(125)              4 Hz
 *   FLASH_ONCE  -> tdl_led_blink(cnt=1, 200/200)   then auto-reverts to previous state
 *
 * FLASH_ONCE semantics: sends a single 200ms pulse then restores the previous
 * stable state; the upper layer (buddy_main_screen) is responsible for switching
 * led_state back to IDLE/BUSY/... on the next tick.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "led_indicator.h"
#include "tdl_led_manage.h"
#include "tal_api.h"
#include <stddef.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#ifndef LED_NAME
/* If LED_NAME is not defined in board config, degrade to empty string;
 * tdl_led_find_dev will return NULL and buddy_led will work in log-only mode. */
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
 * @brief Initialize LED handle.
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
 * @brief Release LED handle.
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
 * @brief Set LED state with deduplication + logging.
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
 * @brief Read current state.
 */
buddy_led_state_e buddy_led_current(VOID_T)
{
    return s_state;
}

/* ---------------------------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------------------------- */
/**
 * @brief State enum to string (for logging only).
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
 * @brief Dispatch state to tdl_led; logs only when LED handle is unavailable.
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
