/**
 * @file buddy_led.h
 * @brief 将 buddy_led_state_e 映射到 tdl_led_manage 动作的薄封装。
 *
 * 本模块由 buddy_main_screen 驱动：每次 persona_state 发生迁移时调用
 * buddy_led_set()，模块内部判重并下发对应 tdl_led 命令。所有状态迁移
 * 会通过 PR_DEBUG 输出日志，便于串口侧验收。
 *
 * 硬件：T5AI-Pocket 板上的 GPIO LED（boards/T5AI/TUYA_T5AI_POCKET/
 * tuya_t5ai_pocket.c 中以 LED_NAME 注册）。若编译时 LED_NAME 未定义
 * 或 tdl_led_find_dev() 失败，则本模块降级为"仅日志"，不影响其他 UI。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_LED_H
#define BUDDY_LED_H

#include "buddy_data.h"
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief 初始化 LED 句柄；查找 LED_NAME 指定设备并打开。
 * @return OPRT_OK 成功；其他错误码表示设备缺失或打开失败（模块仍可用但
 *         会降级为仅日志，调用方可忽略返回值）
 * @note 线程安全，可重复调用（幂等）。
 */
OPERATE_RET buddy_led_init(VOID_T);

/**
 * @brief 释放 LED 句柄。
 * @return OPRT_OK
 */
OPERATE_RET buddy_led_deinit(VOID_T);

/**
 * @brief 设置 LED 视觉状态；相同状态重复调用会被过滤。
 * @param[in] state 目标状态
 * @return OPRT_OK 下发成功或已处于该状态；其他错误码透传 tdl_led_*
 */
OPERATE_RET buddy_led_set(buddy_led_state_e state);

/**
 * @brief 读取模块当前状态（最近一次生效值）。
 * @return 当前 buddy_led_state_e
 */
buddy_led_state_e buddy_led_current(VOID_T);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_LED_H */
