/**
 * @file tuya_luanode_port.h
 * @brief Lua node platform port declarations
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TUYA_LUANODE_PORT_H__
#define __TUYA_LUANODE_PORT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "tuya_cloud_types.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    UINT32_T heap_limit;
    UINT32_T heap_used;
} LUANODE_ALLOC_CTX_T;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Reset Lua node allocator context
 * @param[out] ctx allocator context
 * @param[in] heap_limit heap limit in bytes
 * @return none
 */
VOID_T tuya_luanode_allocator_reset(LUANODE_ALLOC_CTX_T *ctx, UINT32_T heap_limit);

/**
 * @brief Get Lua node allocator function
 * @return Lua allocator function
 */
lua_Alloc tuya_luanode_get_allocator(VOID_T);

#ifdef __cplusplus
}
#endif
#endif /* __TUYA_LUANODE_PORT_H__ */
