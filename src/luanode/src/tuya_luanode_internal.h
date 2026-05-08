/**
 * @file tuya_luanode_internal.h
 * @brief Internal Lua node declarations
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TUYA_LUANODE_INTERNAL_H__
#define __TUYA_LUANODE_INTERNAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "tuya_luanode.h"
#include "tuya_luanode_port.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    lua_State          *lua;
    LUANODE_ALLOC_CTX_T alloc_ctx;
    UINT32_T            heap_size;
    UINT32_T            stack_size;
    BOOL_T              enable_std_libs;
} __LUANODE_CTX_T;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Convert public Lua node handle to internal context
 * @param[in] handle Lua node handle
 * @return internal context pointer, or NULL when handle is invalid
 */
__LUANODE_CTX_T *__luanode_get_ctx(TUYA_LUANODE_HANDLE_T handle);

#ifdef __cplusplus
}
#endif
#endif /* __TUYA_LUANODE_INTERNAL_H__ */
