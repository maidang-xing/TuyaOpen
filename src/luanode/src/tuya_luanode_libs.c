/**
 * @file tuya_luanode_libs.c
 * @brief Tuya helper libraries for Lua node
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_luanode_internal.h"

#include "lauxlib.h"
#include "tal_log.h"

#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define LUANODE_TUYA_LOG_MAX_LEN 256U

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC INT_T __luanode_tuya_log(lua_State *lua);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Log a Lua string through Tuya log system
 * @param[in] lua Lua state
 * @return Lua return value count
 */
STATIC INT_T __luanode_tuya_log(lua_State *lua)
{
    size_t        len = 0;
    CONST CHAR_T *msg = NULL;
    CHAR_T        log_buf[LUANODE_TUYA_LOG_MAX_LEN + 1U];
    size_t        copy_len = 0;

    msg      = luaL_checklstring(lua, 1, &len);
    copy_len = (len > LUANODE_TUYA_LOG_MAX_LEN) ? LUANODE_TUYA_LOG_MAX_LEN : len;
    memset(log_buf, 0, sizeof(log_buf));
    if (copy_len > 0) {
        memcpy(log_buf, msg, copy_len);
    }
    log_buf[LUANODE_TUYA_LOG_MAX_LEN] = '\0';
    TAL_PR_INFO("lua: %s", log_buf);
    return 0;
}

/**
 * @brief Register Tuya helper libraries into Lua runtime
 * @param[in] handle Lua node handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_register_tuya_libs(TUYA_LUANODE_HANDLE_T handle)
{
    __LUANODE_CTX_T *ctx         = __luanode_get_ctx(handle);
    luaL_Reg         tuya_libs[] = {
                {"log", __luanode_tuya_log},
                {NULL, NULL},
    };

    if ((ctx == NULL) || (ctx->lua == NULL)) {
        return OPRT_INVALID_PARM;
    }
    lua_newtable(ctx->lua);
    luaL_setfuncs(ctx->lua, tuya_libs, 0);
    lua_setglobal(ctx->lua, "tuya");
    return OPRT_OK;
}
