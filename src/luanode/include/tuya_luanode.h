/**
 * @file tuya_luanode.h
 * @brief Lua runtime wrapper for TuyaOpen
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#ifndef __TUYA_LUANODE_H__
#define __TUYA_LUANODE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "tuya_cloud_types.h"

/* ---------------------------------------------------------------------------
 * Type definitions
 * --------------------------------------------------------------------------- */
typedef struct {
    UINT32_T heap_size;
    UINT32_T stack_size;
    BOOL_T   enable_std_libs;
} TUYA_LUANODE_CFG_T;

typedef VOID_T *TUYA_LUANODE_HANDLE_T;

/* ---------------------------------------------------------------------------
 * Function declarations
 * --------------------------------------------------------------------------- */
/**
 * @brief Create Lua node runtime
 * @param[in] cfg runtime configuration
 * @param[out] handle output Lua node handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_create(CONST TUYA_LUANODE_CFG_T *cfg, TUYA_LUANODE_HANDLE_T *handle);

/**
 * @brief Destroy Lua node runtime
 * @param[in] handle Lua node handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_destroy(TUYA_LUANODE_HANDLE_T handle);

/**
 * @brief Execute Lua script string
 * @param[in] handle Lua node handle
 * @param[in] script Lua script string
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_dostring(TUYA_LUANODE_HANDLE_T handle, CONST CHAR_T *script);

/**
 * @brief Execute Lua script file
 * @param[in] handle Lua node handle
 * @param[in] path Lua script file path
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_dofile(TUYA_LUANODE_HANDLE_T handle, CONST CHAR_T *path);

/**
 * @brief Register Tuya helper libraries into Lua runtime
 * @param[in] handle Lua node handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_register_tuya_libs(TUYA_LUANODE_HANDLE_T handle);

/**
 * @brief Register Lua node CLI commands
 * @return OPRT_OK on success, error code on failure
 * @note CLI commands are registered only when LUANODE_ENABLE_CLI is enabled.
 */
OPERATE_RET tuya_luanode_cli_register(VOID_T);

#ifdef __cplusplus
}
#endif
#endif /* __TUYA_LUANODE_H__ */
