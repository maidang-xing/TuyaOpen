/**
 * @file tuya_luanode.c
 * @brief Lua runtime wrapper for TuyaOpen
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_luanode_internal.h"

#include "lauxlib.h"
#include "lualib.h"
#include "tal_fs.h"
#include "tal_log.h"
#include "tal_memory.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define LUANODE_KB_SIZE              1024U
#define LUANODE_MIN_HEAP_SIZE_KB     16U
#define LUANODE_LOG_MAX_LEN          128U
#define LUANODE_PATH_MAX_LEN         256U
#define LUANODE_FILE_READ_MODE       "rb"
#define LUANODE_PATH_SEPARATOR       '/'
#define LUANODE_ALT_PATH_SEPARATOR   '\\'
#define LUANODE_PATH_TRAVERSAL_TOKEN ".."

#ifndef LUANODE_MAX_SCRIPT_SIZE
#define LUANODE_MAX_SCRIPT_SIZE 65536
#endif

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC OPERATE_RET __luanode_validate_cfg(CONST TUYA_LUANODE_CFG_T *cfg);
STATIC BOOL_T      __luanode_path_is_valid(CONST CHAR_T *path);
STATIC OPERATE_RET __luanode_read_file_linux(CONST CHAR_T *path, CHAR_T **script);
STATIC OPERATE_RET __luanode_read_file_tal(CONST CHAR_T *path, CHAR_T **script);
STATIC OPERATE_RET __luanode_read_file(CONST CHAR_T *path, CHAR_T **script);
STATIC VOID_T      __luanode_log_error(lua_State *lua);

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Validate Lua node runtime configuration
 * @param[in] cfg runtime configuration
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __luanode_validate_cfg(CONST TUYA_LUANODE_CFG_T *cfg)
{
    if (cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (cfg->heap_size < LUANODE_MIN_HEAP_SIZE_KB) {
        return OPRT_INVALID_PARM;
    }
    if (cfg->heap_size > (UINT32_MAX / LUANODE_KB_SIZE)) {
        return OPRT_INVALID_PARM;
    }
    return OPRT_OK;
}

/**
 * @brief Check whether Lua script path is acceptable
 * @param[in] path Lua script file path
 * @return TRUE when path is acceptable, FALSE otherwise
 */
STATIC BOOL_T __luanode_path_is_valid(CONST CHAR_T *path)
{
    size_t path_len = 0;

    if (path == NULL) {
        return FALSE;
    }
    path_len = strlen(path);
    if ((path_len == 0) || (path_len >= LUANODE_PATH_MAX_LEN)) {
        return FALSE;
    }
    if ((path[0] == LUANODE_PATH_SEPARATOR) || (path[0] == LUANODE_ALT_PATH_SEPARATOR)) {
        return FALSE;
    }
    if (strstr(path, LUANODE_PATH_TRAVERSAL_TOKEN) != NULL) {
        return FALSE;
    }
    return TRUE;
}

/**
 * @brief Read Linux host Lua script file into memory
 * @param[in] path Lua script file path
 * @param[out] script output script buffer
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __luanode_read_file_linux(CONST CHAR_T *path, CHAR_T **script)
{
    FILE   *file      = NULL;
    long    file_size = 0;
    size_t  read_size = 0;
    CHAR_T *buffer    = NULL;

    if ((path == NULL) || (script == NULL)) {
        return OPRT_INVALID_PARM;
    }
    *script = NULL;

    file = fopen(path, LUANODE_FILE_READ_MODE);
    if (file == NULL) {
        return OPRT_COM_ERROR;
    }
    if (fseek(file, 0L, SEEK_END) != 0) {
        fclose(file);
        return OPRT_COM_ERROR;
    }
    file_size = ftell(file);
    if ((file_size <= 0) || (file_size > LUANODE_MAX_SCRIPT_SIZE)) {
        fclose(file);
        return OPRT_INVALID_PARM;
    }
    if (fseek(file, 0L, SEEK_SET) != 0) {
        fclose(file);
        return OPRT_COM_ERROR;
    }

    buffer = (CHAR_T *)tal_malloc((size_t)file_size + 1U);
    if (buffer == NULL) {
        fclose(file);
        return OPRT_MALLOC_FAILED;
    }
    memset(buffer, 0, (size_t)file_size + 1U);

    read_size = fread(buffer, 1U, (size_t)file_size, file);
    fclose(file);
    if (read_size != (size_t)file_size) {
        memset(buffer, 0, (size_t)file_size + 1U);
        tal_free(buffer);
        return OPRT_COM_ERROR;
    }

    buffer[file_size] = '\0';
    *script           = buffer;
    return OPRT_OK;
}

/**
 * @brief Read Tuya filesystem Lua script file into memory
 * @param[in] path Lua script file path
 * @param[out] script output script buffer
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __luanode_read_file_tal(CONST CHAR_T *path, CHAR_T **script)
{
    INT_T     file_size = 0;
    INT_T     read_size = 0;
    TUYA_FILE file      = NULL;
    CHAR_T   *buffer    = NULL;

    if ((path == NULL) || (script == NULL)) {
        return OPRT_INVALID_PARM;
    }
    *script = NULL;

    file_size = tal_fgetsize(path);
    if ((file_size <= 0) || (file_size > LUANODE_MAX_SCRIPT_SIZE)) {
        return OPRT_INVALID_PARM;
    }

    buffer = (CHAR_T *)tal_malloc((size_t)file_size + 1U);
    if (buffer == NULL) {
        return OPRT_MALLOC_FAILED;
    }
    memset(buffer, 0, (size_t)file_size + 1U);

    file = tal_fopen(path, LUANODE_FILE_READ_MODE);
    if (file == NULL) {
        tal_free(buffer);
        return OPRT_COM_ERROR;
    }

    read_size = tal_fread(buffer, file_size, file);
    tal_fclose(file);
    if (read_size != file_size) {
        memset(buffer, 0, (size_t)file_size + 1U);
        tal_free(buffer);
        return OPRT_COM_ERROR;
    }

    buffer[file_size] = '\0';
    *script           = buffer;
    return OPRT_OK;
}

/**
 * @brief Read Lua script file into memory
 * @param[in] path Lua script file path
 * @param[out] script output script buffer
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __luanode_read_file(CONST CHAR_T *path, CHAR_T **script)
{
#if defined(OPERATING_SYSTEM) && (OPERATING_SYSTEM == SYSTEM_LINUX)
    return __luanode_read_file_linux(path, script);
#else
    return __luanode_read_file_tal(path, script);
#endif
}

/**
 * @brief Log Lua error message with length limit
 * @param[in] lua Lua state
 * @return none
 */
STATIC VOID_T __luanode_log_error(lua_State *lua)
{
    CONST CHAR_T *msg = NULL;
    CHAR_T        log_buf[LUANODE_LOG_MAX_LEN + 1U];

    if (lua == NULL) {
        return;
    }
    msg = lua_tostring(lua, -1);
    if (msg == NULL) {
        msg = "unknown lua error";
    }
    (VOID_T) snprintf(log_buf, sizeof(log_buf), "%s", msg);
    log_buf[LUANODE_LOG_MAX_LEN] = '\0';
    TAL_PR_ERR("luanode error: %s", log_buf);
    lua_pop(lua, 1);
}

/**
 * @brief Create Lua node runtime
 * @param[in] cfg runtime configuration
 * @param[out] handle output Lua node handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_create(CONST TUYA_LUANODE_CFG_T *cfg, TUYA_LUANODE_HANDLE_T *handle)
{
    __LUANODE_CTX_T *ctx        = NULL;
    OPERATE_RET      ret        = OPRT_OK;
    UINT32_T         heap_bytes = 0;

    if ((cfg == NULL) || (handle == NULL)) {
        return OPRT_INVALID_PARM;
    }
    *handle = NULL;

    ret = __luanode_validate_cfg(cfg);
    if (ret != OPRT_OK) {
        return ret;
    }

    ctx = (__LUANODE_CTX_T *)tal_calloc(1U, sizeof(__LUANODE_CTX_T));
    if (ctx == NULL) {
        return OPRT_MALLOC_FAILED;
    }

    heap_bytes = cfg->heap_size * LUANODE_KB_SIZE;
    tuya_luanode_allocator_reset(&ctx->alloc_ctx, heap_bytes);
    ctx->heap_size       = heap_bytes;
    ctx->stack_size      = cfg->stack_size;
    ctx->enable_std_libs = cfg->enable_std_libs;
    ctx->lua             = lua_newstate(tuya_luanode_get_allocator(), &ctx->alloc_ctx, luaL_makeseed(NULL));
    if (ctx->lua == NULL) {
        tal_free(ctx);
        return OPRT_MALLOC_FAILED;
    }

    if (ctx->enable_std_libs == TRUE) {
        luaL_openlibs(ctx->lua);
    }

    *handle = (TUYA_LUANODE_HANDLE_T)ctx;
    return OPRT_OK;
}

/**
 * @brief Destroy Lua node runtime
 * @param[in] handle Lua node handle
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_destroy(TUYA_LUANODE_HANDLE_T handle)
{
    __LUANODE_CTX_T *ctx = __luanode_get_ctx(handle);

    if (ctx == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (ctx->lua != NULL) {
        lua_close(ctx->lua);
        ctx->lua = NULL;
    }
    memset(ctx, 0, sizeof(__LUANODE_CTX_T));
    tal_free(ctx);
    return OPRT_OK;
}

/**
 * @brief Execute Lua script string
 * @param[in] handle Lua node handle
 * @param[in] script Lua script string
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_dostring(TUYA_LUANODE_HANDLE_T handle, CONST CHAR_T *script)
{
    __LUANODE_CTX_T *ctx        = __luanode_get_ctx(handle);
    size_t           script_len = 0;
    INT_T            lua_ret    = 0;

    if ((ctx == NULL) || (script == NULL)) {
        return OPRT_INVALID_PARM;
    }
    script_len = strlen(script);
    if ((script_len == 0) || (script_len > LUANODE_MAX_SCRIPT_SIZE)) {
        return OPRT_INVALID_PARM;
    }

    lua_ret = luaL_loadbuffer(ctx->lua, script, script_len, "luanode-script");
    if (lua_ret != LUA_OK) {
        __luanode_log_error(ctx->lua);
        return OPRT_COM_ERROR;
    }

    lua_ret = lua_pcall(ctx->lua, 0, LUA_MULTRET, 0);
    if (lua_ret != LUA_OK) {
        __luanode_log_error(ctx->lua);
        return OPRT_COM_ERROR;
    }
    return OPRT_OK;
}

/**
 * @brief Execute Lua script file
 * @param[in] handle Lua node handle
 * @param[in] path Lua script file path
 * @return OPRT_OK on success, error code on failure
 */
OPERATE_RET tuya_luanode_dofile(TUYA_LUANODE_HANDLE_T handle, CONST CHAR_T *path)
{
    __LUANODE_CTX_T *ctx    = __luanode_get_ctx(handle);
    CHAR_T          *script = NULL;
    OPERATE_RET      ret    = OPRT_OK;

    if ((ctx == NULL) || (path == NULL)) {
        return OPRT_INVALID_PARM;
    }
    if (__luanode_path_is_valid(path) == FALSE) {
        return OPRT_INVALID_PARM;
    }

    ret = __luanode_read_file(path, &script);
    if (ret != OPRT_OK) {
        return ret;
    }

    ret = tuya_luanode_dostring(handle, script);
    memset(script, 0, strlen(script));
    tal_free(script);
    return ret;
}

/**
 * @brief Convert public Lua node handle to internal context
 * @param[in] handle Lua node handle
 * @return internal context pointer, or NULL when handle is invalid
 */
__LUANODE_CTX_T *__luanode_get_ctx(TUYA_LUANODE_HANDLE_T handle)
{
    if (handle == NULL) {
        return NULL;
    }
    return (__LUANODE_CTX_T *)handle;
}
