/**
 * @file tuya_luanode_cli.c
 * @brief Lua node CLI commands
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tuya_luanode.h"

#include "tal_cli.h"
#include "tal_log.h"

#include <stdio.h>
#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define LUANODE_CLI_SCRIPT_MAX_LEN 512U

#ifndef LUANODE_HEAP_SIZE
#define LUANODE_HEAP_SIZE 64
#endif

#ifndef LUANODE_STACK_SIZE
#define LUANODE_STACK_SIZE 4096
#endif

/* ---------------------------------------------------------------------------
 * Forward declarations
 * --------------------------------------------------------------------------- */
STATIC VOID_T __luanode_cli_cmd(INT_T argc, CHAR_T *argv[]);
STATIC BOOL_T __luanode_cli_join_args(INT_T argc, CHAR_T *argv[], INT_T start, CHAR_T *out, size_t out_size);

/* ---------------------------------------------------------------------------
 * File scope variables
 * --------------------------------------------------------------------------- */
STATIC cli_cmd_t s_luanode_cli_cmd[] = {
    {"lua", "lua run <script-path> | lua eval <script-text>", __luanode_cli_cmd},
};

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Join CLI arguments into a bounded string
 * @param[in] argc argument count
 * @param[in] argv argument vector
 * @param[in] start first argument index
 * @param[out] out output buffer
 * @param[in] out_size output buffer size
 * @return TRUE on success, FALSE on failure
 */
STATIC BOOL_T __luanode_cli_join_args(INT_T argc, CHAR_T *argv[], INT_T start, CHAR_T *out, size_t out_size)
{
    size_t offset = 0;

    if ((argv == NULL) || (out == NULL) || (out_size == 0) || (argc <= start)) {
        return FALSE;
    }
    out[0] = '\0';

    for (INT_T i = start; i < argc; i++) {
        INT_T written = snprintf(out + offset, out_size - offset, "%s%s", (i == start) ? "" : " ", argv[i]);
        if ((written < 0) || ((size_t)written >= (out_size - offset))) {
            out[out_size - 1U] = '\0';
            return FALSE;
        }
        offset += (size_t)written;
    }
    return TRUE;
}

/**
 * @brief Echo Lua CLI usage
 * @return none
 */
STATIC VOID_T __luanode_cli_usage(VOID_T)
{
    tal_cli_echo("Usage: lua run <script-path> | lua eval <script-text>");
}

/**
 * @brief Execute Lua CLI command
 * @param[in] argc argument count
 * @param[in] argv argument vector
 * @return none
 */
STATIC VOID_T __luanode_cli_cmd(INT_T argc, CHAR_T *argv[])
{
    TUYA_LUANODE_HANDLE_T handle = NULL;
    TUYA_LUANODE_CFG_T    cfg    = {
              .heap_size       = LUANODE_HEAP_SIZE,
              .stack_size      = LUANODE_STACK_SIZE,
              .enable_std_libs = TRUE,
    };
    CHAR_T      script[LUANODE_CLI_SCRIPT_MAX_LEN + 1U];
    OPERATE_RET ret = OPRT_OK;

    if ((argc < 3) || (argv == NULL) || (argv[1] == NULL)) {
        __luanode_cli_usage();
        return;
    }

    ret = tuya_luanode_create(&cfg, &handle);
    if (ret != OPRT_OK) {
        tal_cli_echo("lua: runtime create failed");
        return;
    }

#if defined(LUANODE_ENABLE_TUYA_LIBS) && (LUANODE_ENABLE_TUYA_LIBS == 1)
    (VOID_T) tuya_luanode_register_tuya_libs(handle);
#endif

    if (strcmp(argv[1], "run") == 0) {
        ret = tuya_luanode_dofile(handle, argv[2]);
    } else if (strcmp(argv[1], "eval") == 0) {
        memset(script, 0, sizeof(script));
        if (__luanode_cli_join_args(argc, argv, 2, script, sizeof(script)) == FALSE) {
            ret = OPRT_INVALID_PARM;
        } else {
            ret = tuya_luanode_dostring(handle, script);
        }
    } else {
        __luanode_cli_usage();
        ret = OPRT_INVALID_PARM;
    }

    (VOID_T) tuya_luanode_destroy(handle);
    if (ret != OPRT_OK) {
        TAL_PR_ERR("lua cli command failed: %d", ret);
        tal_cli_echo("lua: command failed");
    }
}

/**
 * @brief Register Lua node CLI commands
 * @return OPRT_OK on success, error code on failure
 * @note CLI commands are registered only when LUANODE_ENABLE_CLI is enabled.
 */
OPERATE_RET tuya_luanode_cli_register(VOID_T)
{
#if defined(LUANODE_ENABLE_CLI) && (LUANODE_ENABLE_CLI == 1)
    return tal_cli_cmd_register(s_luanode_cli_cmd, (UINT8_T)(sizeof(s_luanode_cli_cmd) / sizeof(s_luanode_cli_cmd[0])));
#else
    return OPRT_NOT_SUPPORTED;
#endif
}
