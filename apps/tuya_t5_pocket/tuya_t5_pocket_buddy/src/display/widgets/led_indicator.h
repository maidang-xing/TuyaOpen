/**
 * @file led_indicator.h
 * @brief Maps buddy_led_state_e to tdl_led_manage actions.
 *
 * This module is driven by buddy_main_screen: each time persona_state
 * transitions, buddy_led_set() is called; the module deduplicates and
 * dispatches the corresponding tdl_led command. All state transitions
 * are logged via PR_DEBUG for serial verification.
 *
 * Hardware: T5AI-Pocket board GPIO LED (registered as LED_NAME in
 * boards/T5AI/TUYA_T5AI_POCKET/tuya_t5ai_pocket.c). If LED_NAME is
 * undefined at compile time or tdl_led_find_dev() fails, the module
 * degrades to "log-only" without affecting other UI.
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef LED_INDICATOR_H
#define LED_INDICATOR_H

#include "buddy_types.h"
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Initialize LED handle; finds and opens the LED_NAME device.
 * @return OPRT_OK on success; other error codes indicate device missing or
 *         open failure (module still usable but degrades to log-only,
 *         caller can ignore return value)
 * @note Thread-safe, idempotent (can be called multiple times).
 */
OPERATE_RET buddy_led_init(VOID_T);

/**
 * @brief Release LED handle.
 * @return OPRT_OK
 */
OPERATE_RET buddy_led_deinit(VOID_T);

/**
 * @brief Set LED visual state; repeated calls with same state are filtered.
 * @param[in] state target state
 * @return OPRT_OK if dispatched or already in that state; other error codes
 *         transparently from tdl_led_*
 */
OPERATE_RET buddy_led_set(buddy_led_state_e state);

/**
 * @brief Read the current module state (last effective value).
 * @return current buddy_led_state_e
 */
buddy_led_state_e buddy_led_current(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* LED_INDICATOR_H */
