/**
 * @file tuya_luanode_memory.c
 * @brief Lua node allocator port for TuyaOpen
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_luanode_port.h"

#include "tal_memory.h"

#include <stdint.h>

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Reset Lua node allocator context
 * @param[out] ctx allocator context
 * @param[in] heap_limit heap limit in bytes
 * @return none
 */
VOID_T tuya_luanode_allocator_reset(LUANODE_ALLOC_CTX_T *ctx, UINT32_T heap_limit)
{
    if (ctx == NULL) {
        return;
    }
    ctx->heap_limit = heap_limit;
    ctx->heap_used  = 0;
}

/**
 * @brief Allocate, resize, or free Lua runtime memory
 * @param[in,out] ud allocator context
 * @param[in] ptr old memory pointer
 * @param[in] osize old allocation size
 * @param[in] nsize new allocation size
 * @return memory pointer on success, NULL on failure or free
 */
STATIC VOID_T *__luanode_alloc(VOID_T *ud, VOID_T *ptr, size_t osize, size_t nsize)
{
    LUANODE_ALLOC_CTX_T *ctx     = (LUANODE_ALLOC_CTX_T *)ud;
    VOID_T              *new_ptr = NULL;

    if ((ctx == NULL) || (osize > UINT32_MAX) || (nsize > UINT32_MAX)) {
        return NULL;
    }

    if (nsize == 0) {
        if (ptr != NULL) {
            if (ctx->heap_used >= (UINT32_T)osize) {
                ctx->heap_used -= (UINT32_T)osize;
            } else {
                ctx->heap_used = 0;
            }
            tal_free(ptr);
        }
        return NULL;
    }

    if (nsize > osize) {
        UINT32_T grow_size = (UINT32_T)(nsize - osize);
        if (ctx->heap_used >= ctx->heap_limit) {
            return NULL;
        }
        if (grow_size > (ctx->heap_limit - ctx->heap_used)) {
            return NULL;
        }
    }

    new_ptr = tal_realloc(ptr, nsize);
    if (new_ptr == NULL) {
        return NULL;
    }

    if (nsize > osize) {
        ctx->heap_used += (UINT32_T)(nsize - osize);
    } else {
        ctx->heap_used -= (UINT32_T)(osize - nsize);
    }
    return new_ptr;
}

/**
 * @brief Get Lua node allocator function
 * @return Lua allocator function
 */
lua_Alloc tuya_luanode_get_allocator(VOID_T)
{
    return __luanode_alloc;
}
