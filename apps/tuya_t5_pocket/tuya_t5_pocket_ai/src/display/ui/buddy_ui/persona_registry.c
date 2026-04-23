/**
 * @file persona_registry.c
 * @brief 18 个 ASCII 人格注册表实现。
 *
 * 每个人格的 ascii_persona_t 常量由 persona_<name>.c 导出；本文件只负责
 * 把它们按稳定顺序排进 s_registry[]。增加/移除/重排人格时同步更新
 * persona_registry.h 的注释并保持 BUDDY_PERSONA_COUNT 一致。
 *
 * @copyright Copyright (c) 2024-2026 TuyaOpen Project
 */

#include "persona_registry.h"
#include "tuya_cloud_types.h"
#include <stddef.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * External persona symbols
 * --------------------------------------------------------------------------- */
extern const ascii_persona_t PERSONA_CAPYBARA;
extern const ascii_persona_t PERSONA_DUCK;
extern const ascii_persona_t PERSONA_GOOSE;
extern const ascii_persona_t PERSONA_BLOB;
extern const ascii_persona_t PERSONA_CAT;
extern const ascii_persona_t PERSONA_DRAGON;
extern const ascii_persona_t PERSONA_OCTOPUS;
extern const ascii_persona_t PERSONA_OWL;
extern const ascii_persona_t PERSONA_PENGUIN;
extern const ascii_persona_t PERSONA_TURTLE;
extern const ascii_persona_t PERSONA_SNAIL;
extern const ascii_persona_t PERSONA_GHOST;
extern const ascii_persona_t PERSONA_AXOLOTL;
extern const ascii_persona_t PERSONA_CACTUS;
extern const ascii_persona_t PERSONA_ROBOT;
extern const ascii_persona_t PERSONA_RABBIT;
extern const ascii_persona_t PERSONA_MUSHROOM;
extern const ascii_persona_t PERSONA_CHONK;

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC const persona_entry_t s_registry[BUDDY_PERSONA_COUNT] = {
    {  0, &PERSONA_CAPYBARA },
    {  1, &PERSONA_DUCK     },
    {  2, &PERSONA_GOOSE    },
    {  3, &PERSONA_BLOB     },
    {  4, &PERSONA_CAT      },
    {  5, &PERSONA_DRAGON   },
    {  6, &PERSONA_OCTOPUS  },
    {  7, &PERSONA_OWL      },
    {  8, &PERSONA_PENGUIN  },
    {  9, &PERSONA_TURTLE   },
    { 10, &PERSONA_SNAIL    },
    { 11, &PERSONA_GHOST    },
    { 12, &PERSONA_AXOLOTL  },
    { 13, &PERSONA_CACTUS   },
    { 14, &PERSONA_ROBOT    },
    { 15, &PERSONA_RABBIT   },
    { 16, &PERSONA_MUSHROOM },
    { 17, &PERSONA_CHONK    },
};

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief 按 ID 查找人格。
 */
const persona_entry_t *persona_registry_get_by_id(uint8_t id)
{
    if (id >= BUDDY_PERSONA_COUNT) {
        return NULL;
    }
    return &s_registry[id];
}

/**
 * @brief 按名字查找人格（大小写敏感）。
 */
const persona_entry_t *persona_registry_get_by_name(const char *name)
{
    if (name == NULL || name[0] == '\0') {
        return NULL;
    }
    for (uint8_t i = 0; i < BUDDY_PERSONA_COUNT; i++) {
        const ascii_persona_t *p = s_registry[i].persona;
        if (p != NULL && p->name != NULL && strcmp(p->name, name) == 0) {
            return &s_registry[i];
        }
    }
    return NULL;
}

/**
 * @brief 注册表长度。
 */
uint8_t persona_registry_count(void)
{
    return BUDDY_PERSONA_COUNT;
}

/**
 * @brief 下一个 ID（环绕）。
 */
uint8_t persona_registry_next_id(uint8_t current)
{
    if (current >= BUDDY_PERSONA_COUNT) {
        return 0;
    }
    uint8_t next = (uint8_t)(current + 1U);
    if (next >= BUDDY_PERSONA_COUNT) {
        next = 0;
    }
    return next;
}

/**
 * @brief 上一个 ID（环绕）。
 */
uint8_t persona_registry_prev_id(uint8_t current)
{
    if (current == 0 || current >= BUDDY_PERSONA_COUNT) {
        return (uint8_t)(BUDDY_PERSONA_COUNT - 1U);
    }
    return (uint8_t)(current - 1U);
}
