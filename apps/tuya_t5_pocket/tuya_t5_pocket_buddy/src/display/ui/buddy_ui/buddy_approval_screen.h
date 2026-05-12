/**
 * @file buddy_approval_screen.h
 * @brief Claude 权限审批屏（候选全屏版本）。
 *
 * 本屏作为 main_screen 内联卡片的可选替代：当 UI 层判断需要把审批独立
 * 成页（例如从其他场景跳转进来审批），可以 push 这个 Screen_t 进栈。
 *
 * 按键映射（与 UI_INTERACTION 一致）：
 *   ENTER  → permission "once"
 *   LEFT   → permission "deny"
 *   RIGHT  → permission "always"
 *   ESC    → screen_back()（回主屏不下发任何决策）
 *
 * 与 main_screen 的 prompt card 共享数据源（buddy_ble_snapshot），不
 * 增加额外的状态缓存。prompt_id 不存在时屏幕仍可打开但禁用所有决策键。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_APPROVAL_SCREEN_H
#define BUDDY_APPROVAL_SCREEN_H

#include "screen_manager.h"
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Exported Screen_t
 * --------------------------------------------------------------------------- */
extern Screen_t buddy_approval_screen;

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_APPROVAL_SCREEN_H */
