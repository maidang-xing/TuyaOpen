/**
 * @file buddy_main_screen.h
 * @brief Claude Desktop Buddy 主屏 UI（M1-UI 版本）。
 *
 * 一屏布局，384x168 横向：
 *   +------------------------------------------------------+
 *   | header 20px  Claude Buddy   HH:MM   BLE: -  Claude_..|
 *   +-----------------------------+------------------------+
 *   | body 124px                  |  body right 124px       |
 *   |   ASCII persona 184x120     |  msg / sessions / toks  |
 *   |   (or GIF stub placeholder) |  owner / entries (4 r)  |
 *   |   [permission card overlays entire body if pending]   |
 *   +------------------------------------------------------+
 *   | footer 24px  key hints (context sensitive)            |
 *   +------------------------------------------------------+
 *
 * 按键（见 doc/UI_INTERACTION_zh.md 完整规格）：
 *   ENTER   审批 "once"        （prompt 时）
 *   LEFT    审批 "deny"         （prompt 时） / 上一个 persona（无 prompt）
 *   RIGHT   审批 "always"       （prompt 时） / 下一个 persona（无 prompt）
 *   UP/DOWN entries 滚动
 *   JOYCON  发送 {"cmd":"status"}
 *   ESC     返回上一屏
 *
 * 除了 UP/DOWN/JOYCON/ESC 之外，其他按键在无 prompt 且无 persona 切换
 * 场景时 no-op，避免把未匹配 id 的决策送上链路。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_MAIN_SCREEN_H
#define BUDDY_MAIN_SCREEN_H

#include "screen_manager.h"
#include "buddy_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Exported Screen_t
 * --------------------------------------------------------------------------- */
extern Screen_t buddy_main_screen;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief 把最新 BLE 快照推送到主屏。
 *
 * 线程安全：内部自取 LVGL 锁，因此可从任意任务（含 BLE RX）调用。屏幕
 * 尚未加载时仅缓存，下次 init() 会用缓存 + buddy_ble_snapshot() 重新绘制。
 *
 * @param[in] state BLE 层推送的快照（非 NULL）
 * @return none
 */
void buddy_main_screen_update_state(const buddy_tama_state_t *state);

/**
 * @brief 取主屏当前“选中会话”的 11 字符 sid 拷贝。
 *
 * 线程安全：内部自取 LVGL 锁。供 ASR/链路侧异步采样使用，无需 UI 线程切换。
 * 选中行不是 session 行（如位于 header）或快照中无该 session 时返回 0。
 *
 * @param[out] out_sid 至少 12 字节的输出缓冲（11 字符 + NUL）
 * @param[in]  cap     out_sid 容量，必须 ≥ 12
 * @return 写入的字节数（不含 NUL），0 表示当前没有有效选中会话
 */
size_t buddy_main_screen_get_selected_sid(char *out_sid, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_MAIN_SCREEN_H */
