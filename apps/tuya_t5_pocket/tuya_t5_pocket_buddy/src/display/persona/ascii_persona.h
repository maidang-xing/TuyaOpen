/**
 * @file ascii_persona.h
 * @brief ASCII 人格渲染框架。
 *
 * 每个人格（capybara / duck / goose / ...）提供 7 个状态函数
 * （sleep / idle / busy / attention / celebrate / dizzy / heart），对应
 * buddy_persona_state_e 的 7 个状态。状态函数调用本头暴露的绘制原语
 * （ascii_print_sprite / ascii_print_line / ascii_set_cursor / ascii_print）
 * 将帧写入 buddy_main_screen 的人格画布。
 *
 * 本实现为 Claude Desktop Buddy（https://github.com/anthropics/claude-desktop-buddy）
 * 的 TuyaOpen T5AI-Pocket 移植；原参考项目为彩色 TFT（M5StickC Plus），此处
 * 为单色 OLED 384x168。`uint16_t color` 参数保留签名但运行时忽略（transformer
 * 保持与原 C++ 代码的 1:1 形状）。
 *
 * 使用：
 *   ascii_persona_attach(parent, x, y);
 *   ...
 *   ascii_persona_tick(persona_id, state);   // 每 100 ms 调用
 *   ...
 *   ascii_persona_detach();
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef ASCII_PERSONA_H
#define ASCII_PERSONA_H

#include "screen_manager.h"
#include "buddy_data.h"
#include "lv_vendor.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Geometry（像素坐标，ascii_persona_attach 坐标空间内）
 * --------------------------------------------------------------------------- */
#define ASCII_CANVAS_W    144
#define ASCII_CANVAS_H    110
#define ASCII_CHAR_W      8    /* Terminus Bold 14 monospace 近似 char 宽        */
#define ASCII_CHAR_H      14   /* 行高                                           */
#define ASCII_X_CENTER    (ASCII_CANVAS_W / 2)  /* 72                            */
#define ASCII_Y_BASE      20   /* 主体 sprite 顶部 y（垂直居中：(110-70)/2=20）   */
#define ASCII_Y_OVERLAY   2    /* overlay 参考 y（sprite 上方 18px 余量）          */

/* 画笔兼容常量（transformer 保留原 cpp 使用，但本实现忽略颜色）。 */
#define BUDDY_X_CENTER    ASCII_X_CENTER
#define BUDDY_CANVAS_W    ASCII_CANVAS_W
#define BUDDY_Y_BASE      ASCII_Y_BASE
#define BUDDY_Y_OVERLAY   ASCII_Y_OVERLAY
#define BUDDY_CHAR_W      ASCII_CHAR_W
#define BUDDY_CHAR_H      ASCII_CHAR_H

/* 原参考项目使用 RGB565；此处全部视为前景色（单色）。保留编码方便 diff。 */
#define BUDDY_BG          0x0000u
#define BUDDY_HEART       0xF81Fu
#define BUDDY_DIM         0x7BEFu
#define BUDDY_YEL         0xFFE0u
#define BUDDY_WHITE       0xFFFFu
#define BUDDY_CYAN        0x07FFu
#define BUDDY_GREEN       0x07E0u
#define BUDDY_PURPLE      0xA01Fu
#define BUDDY_RED         0xF800u
#define BUDDY_BLUE        0x001Fu

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
/**
 * @brief 单个人格状态函数。
 * @param[in] t UI 侧递增的 tick 计数，用于控制动画节拍（不是墙钟）
 * @return none
 */
typedef void (*ascii_state_fn)(uint32_t t);

/**
 * @brief 单个人格的注册表条目（由 persona_registry.c 使用）。
 */
typedef struct {
    const char *name;   /* 稳定的 ASCII 名（"capybara", "duck", ...） */
    ascii_state_fn states[BUDDY_PERSONA_STATE_COUNT];
} ascii_persona_t;

/* ---------------------------------------------------------------------------
 * Drawing primitives（状态函数调用，只在 tick 上下文内有效）
 * --------------------------------------------------------------------------- */
/**
 * @brief 在 sprite 槽内绘制 N 行文本精灵。
 * @param[in] lines    指向 n_lines 个 NUL 终止字符串的数组
 * @param[in] n_lines  行数
 * @param[in] y_off    相对 ASCII_Y_BASE 的 y 偏移（px）
 * @param[in] color    保留兼容参数（实现忽略）
 * @param[in] x_off    相对 ASCII_X_CENTER 的 x 偏移（px），默认 0
 * @return none
 */
void ascii_print_sprite(const char *const *lines, uint8_t n_lines, int y_off, uint16_t color, int x_off);

/**
 * @brief 居中单行文本。
 * @param[in] line   NUL 终止字符串
 * @param[in] y_px   绝对 y（px）
 * @param[in] color  保留兼容参数（实现忽略）
 * @param[in] x_off  x 偏移（px），默认 0
 * @return none
 */
void ascii_print_line(const char *line, int y_px, uint16_t color, int x_off);

/**
 * @brief 移动虚拟光标，用于后续 ascii_print() 绘制 overlay 粒子。
 * @param[in] x 绝对 x（px）
 * @param[in] y 绝对 y（px）
 * @return none
 */
void ascii_set_cursor(int x, int y);

/**
 * @brief 设置当前前景色（实现忽略；transformer 保持调用以减少 diff）。
 * @param[in] color 颜色值
 * @return none
 */
void ascii_set_color(uint16_t color);

/**
 * @brief 在当前光标位置绘制字符串（overlay 粒子）。
 * @param[in] s NUL 终止字符串
 * @return none
 * @note 自动分配下一个 overlay 槽位；超出池上限后静默丢弃。
 */
void ascii_print(const char *s);

/* ---------------------------------------------------------------------------
 * Canvas 生命周期 & 每帧接口
 * --------------------------------------------------------------------------- */
/**
 * @brief 在 parent 的 (x,y) 处创建人格画布。
 * @param[in] parent LVGL 容器（如 body）
 * @param[in] x      画布左上角 x（相对 parent，px）
 * @param[in] y      画布左上角 y（相对 parent，px）
 * @return none
 * @note 调用方须持 LVGL 锁（lv_vendor_disp_lock）。重复调用会先 detach。
 */
void ascii_persona_attach(lv_obj_t *parent, int x, int y);

/**
 * @brief 释放画布及所有 overlay 对象。
 * @return none
 * @note 调用方须持 LVGL 锁。
 */
void ascii_persona_detach(void);

/**
 * @brief 推进一帧并重绘。
 * @param[in] persona_id 0..BUDDY_PERSONA_COUNT-1，越界时回落到 0
 * @param[in] state      当前人格状态
 * @return none
 * @note 调用方须持 LVGL 锁。内部维护一个单调 tick，持续递增。
 */
void ascii_persona_tick(uint8_t persona_id, buddy_persona_state_e state);

/**
 * @brief 显式设置 tick 计数（测试 / 重置用）。
 * @param[in] t 新的 tick 起点
 * @return none
 */
void ascii_persona_reset_tick(uint32_t t);

#ifdef __cplusplus
}
#endif

#endif /* ASCII_PERSONA_H */
