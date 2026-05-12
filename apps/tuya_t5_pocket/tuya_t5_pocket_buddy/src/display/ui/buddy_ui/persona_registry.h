/**
 * @file persona_registry.h
 * @brief 18 个 ASCII 人格的注册表。
 *
 * 每个人格定义在自己的 persona_<name>.c 文件中（由 tools/port_buddies.py
 * 从 claude-desktop-buddy/src/buddies/<name>.cpp 转换得到），以
 * `const ascii_persona_t PERSONA_<NAME>` 符号导出。
 *
 * 注册表按稳定顺序排列：
 *   0 capybara   6 octopus   12 axolotl
 *   1 duck       7 owl       13 cactus
 *   2 goose      8 penguin   14 robot
 *   3 blob       9 turtle    15 rabbit
 *   4 cat       10 snail     16 mushroom
 *   5 dragon    11 ghost     17 chonk
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#ifndef PERSONA_REGISTRY_H
#define PERSONA_REGISTRY_H

#include "ascii_persona.h"
#include "buddy_data.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
/**
 * @brief 注册表条目。
 */
typedef struct {
    uint8_t                 id;        /* 0..BUDDY_PERSONA_COUNT-1 */
    const ascii_persona_t  *persona;   /* 指向 persona_<name>.c 导出的常量 */
} persona_entry_t;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief 按 ID 查找人格。
 * @param[in] id 0..BUDDY_PERSONA_COUNT-1，越界返回 NULL
 * @return 指向注册条目的常量指针；越界时 NULL
 */
const persona_entry_t *persona_registry_get_by_id(uint8_t id);

/**
 * @brief 按名字查找人格（大小写敏感）。
 * @param[in] name NUL 终止字符串；NULL / 未知名字返回 NULL
 * @return 指向注册条目的常量指针；NULL 表示未找到
 */
const persona_entry_t *persona_registry_get_by_name(const char *name);

/**
 * @brief 注册表长度。
 * @return BUDDY_PERSONA_COUNT
 */
uint8_t persona_registry_count(void);

/**
 * @brief 计算下一个人格 ID（环绕）。
 * @param[in] current 当前 ID
 * @return 下一个 ID；current 越界时返回 0
 */
uint8_t persona_registry_next_id(uint8_t current);

/**
 * @brief 计算上一个人格 ID（环绕）。
 * @param[in] current 当前 ID
 * @return 上一个 ID；current 越界时返回 BUDDY_PERSONA_COUNT-1
 */
uint8_t persona_registry_prev_id(uint8_t current);

#ifdef __cplusplus
}
#endif

#endif /* PERSONA_REGISTRY_H */
