/**
 * @file example_luanode.c
 * @brief Lua node get-started example
 * @version 1.0
 * @date 2026-05-07
 * @copyright Copyright (c) 2021-2026 Tuya Inc. All Rights Reserved.
 */

#include "tal_log.h"
#include "tkl_output.h"
#include "tuya_iot_config.h"
#include "tuya_luanode.h"

#if !defined(OPERATING_SYSTEM) || (OPERATING_SYSTEM != SYSTEM_LINUX)
#include "tal_thread.h"
#endif

#include <string.h>

/* ---------------------------------------------------------------------------
 * Macros
 * --------------------------------------------------------------------------- */
#define LUANODE_EXAMPLE_SCRIPT_PATH "src/scripts/hello.lua"

#ifndef LUANODE_HEAP_SIZE
#define LUANODE_HEAP_SIZE 64
#endif

#ifndef LUANODE_STACK_SIZE
#define LUANODE_STACK_SIZE 4096
#endif

/* ---------------------------------------------------------------------------
 * Function implementations
 * --------------------------------------------------------------------------- */
/**
 * @brief Run Lua node example
 * @return OPRT_OK on success, error code on failure
 */
STATIC OPERATE_RET __example_luanode_run(VOID_T)
{
    TUYA_LUANODE_HANDLE_T handle = NULL;
    TUYA_LUANODE_CFG_T    cfg    = {
              .heap_size       = LUANODE_HEAP_SIZE,
              .stack_size      = LUANODE_STACK_SIZE,
              .enable_std_libs = TRUE,
    };
    OPERATE_RET ret = OPRT_OK;

    ret = tuya_luanode_create(&cfg, &handle);
    if (ret != OPRT_OK) {
        PR_ERR("create luanode failed: %d", ret);
        return ret;
    }

#if defined(LUANODE_ENABLE_TUYA_LIBS) && (LUANODE_ENABLE_TUYA_LIBS == 1)
    ret = tuya_luanode_register_tuya_libs(handle);
    if (ret != OPRT_OK) {
        PR_ERR("register luanode libs failed: %d", ret);
        (VOID_T) tuya_luanode_destroy(handle);
        return ret;
    }
#endif

    ret = tuya_luanode_dostring(handle, "print(\"hello from dostring\")");
    if (ret != OPRT_OK) {
        PR_ERR("run lua string failed: %d", ret);
        (VOID_T) tuya_luanode_destroy(handle);
        return ret;
    }

    ret = tuya_luanode_dofile(handle, LUANODE_EXAMPLE_SCRIPT_PATH);
    if (ret != OPRT_OK) {
        PR_ERR("run lua file failed: %d", ret);
    }

    (VOID_T) tuya_luanode_destroy(handle);
    return ret;
}

/**
 * @brief User main entry
 * @return OPRT_OK on success, error code on failure
 */
INT_T user_main(VOID_T)
{
    OPERATE_RET ret = OPRT_OK;

    tal_log_init(TAL_LOG_LEVEL_DEBUG, 1024, (TAL_LOG_OUTPUT_CB)tkl_log_output);
    PR_NOTICE("Luanode example start");

    ret = __example_luanode_run();
    if (ret != OPRT_OK) {
        PR_ERR("Luanode example failed: %d", ret);
    } else {
        PR_NOTICE("Luanode example finished");
    }
    return ret;
}

#if OPERATING_SYSTEM == SYSTEM_LINUX
/**
 * @brief Linux main entry
 * @param[in] argc argument count
 * @param[in] argv argument vector
 * @return OPRT_OK on success, error code on failure
 */
INT_T main(INT_T argc, CHAR_T *argv[])
{
    (VOID_T) argc;
    (VOID_T) argv;
    return user_main();
}
#else
/* Tuya thread handle */
STATIC THREAD_HANDLE s_luanode_thread = NULL;

/**
 * @brief TuyaOpen application thread
 * @param[in] arg thread argument
 * @return none
 */
STATIC VOID_T __example_luanode_thread(VOID_T *arg)
{
    (VOID_T) arg;
    (VOID_T) user_main();
    tal_thread_delete(s_luanode_thread);
    s_luanode_thread = NULL;
}

/**
 * @brief TuyaOpen application entry
 * @return none
 */
VOID_T tuya_app_main(VOID_T)
{
    THREAD_CFG_T thrd_param;

    memset(&thrd_param, 0, sizeof(THREAD_CFG_T));
    thrd_param.stackDepth = LUANODE_STACK_SIZE;
    thrd_param.priority   = THREAD_PRIO_1;
    thrd_param.thrdname   = "luanode_example";
    (VOID_T) tal_thread_create_and_start(&s_luanode_thread, NULL, NULL, __example_luanode_thread, NULL, &thrd_param);
}
#endif
