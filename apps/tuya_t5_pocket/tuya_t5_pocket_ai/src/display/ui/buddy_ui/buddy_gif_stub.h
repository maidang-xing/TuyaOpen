/**
 * @file buddy_gif_stub.h
 * @brief 自定义 GIF 角色占位接口（M1-UI 仅占位，M4-Tools 才落地解码器）。
 *
 * 目标目录协议：`/custom_gifs/<name>/frame_%02d.gif`。当用户选择"自定义"
 * 角色时，buddy_main_screen 会调用 buddy_gif_stub_render() 在人格画布上
 * 显示占位帧（文案 + 边框），并在串口打印一次 INFO 日志提示 M4-Tools
 * 尚未实现 GIF 解码。不阻塞其他 UI。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef BUDDY_GIF_STUB_H
#define BUDDY_GIF_STUB_H

#include "lv_vendor.h"
#include "tuya_cloud_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief 在 parent 的 (x,y) 创建占位视图。
 * @param[in] parent LVGL 容器
 * @param[in] x      左上角 x（相对 parent）
 * @param[in] y      左上角 y（相对 parent）
 * @return none
 * @note 调用方须持 LVGL 锁；重复调用会先 detach。
 */
VOID_T buddy_gif_stub_attach(lv_obj_t *parent, int x, int y);

/**
 * @brief 释放占位视图。
 * @return none
 */
VOID_T buddy_gif_stub_detach(VOID_T);

/**
 * @brief 刷新占位文本，用于展示所选的自定义角色名。
 * @param[in] name 用户所选自定义角色名；NULL 显示 "(gif)"
 * @return none
 */
VOID_T buddy_gif_stub_set_name(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* BUDDY_GIF_STUB_H */
