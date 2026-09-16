#include "tkl_thread.h"

#include "tal_system_port.h"
#include "tuya_error_code.h"

OPERATE_RET tkl_thread_create(TKL_THREAD_HANDLE *thread, const char *name, uint32_t stack_size,
                              uint32_t priority, const THREAD_FUNC_T func, void *const arg)
{
    TAL_PORT_THREAD_HANDLE handle;
    if (thread == NULL || func == NULL) {
        return OPRT_INVALID_PARM;
    }
    handle = tal_system_port_thread_create(name, stack_size, priority, func, arg);
    if (handle == NULL) {
        return OPRT_OS_ADAPTER_THRD_CREAT_FAILED;
    }
    *thread = handle;
    return OPRT_OK;
}

OPERATE_RET tkl_thread_release(const TKL_THREAD_HANDLE thread)
{
    return thread == NULL ? OPRT_INVALID_PARM :
           (tal_system_port_thread_release(thread) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_THRD_RELEASE_FAILED);
}

OPERATE_RET tkl_thread_get_watermark(const TKL_THREAD_HANDLE thread, uint32_t *watermark)
{
    (void)thread;
    if (watermark == NULL) {
        return OPRT_INVALID_PARM;
    }
    *watermark = 0;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_thread_get_id(TKL_THREAD_HANDLE *thread)
{
    if (thread == NULL) {
        return OPRT_INVALID_PARM;
    }
    *thread = tal_system_port_thread_current();
    return *thread == NULL ? OPRT_OS_ADAPTER_THRD_JUDGE_SELF_FAILED : OPRT_OK;
}

OPERATE_RET tkl_thread_set_self_name(const char *name)
{
    (void)name;
    return name == NULL ? OPRT_INVALID_PARM : OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_thread_is_self(TKL_THREAD_HANDLE thread, BOOL_T *is_self)
{
    if (thread == NULL || is_self == NULL) {
        return OPRT_INVALID_PARM;
    }
    *is_self = tal_system_port_thread_is_current(thread) ? TRUE : FALSE;
    return OPRT_OK;
}

OPERATE_RET tkl_thread_get_priority(TKL_THREAD_HANDLE thread, int *priority)
{
    (void)thread;
    (void)priority;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_thread_set_priority(TKL_THREAD_HANDLE thread, int priority)
{
    (void)thread;
    (void)priority;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_thread_diagnose(TKL_THREAD_HANDLE thread)
{
    (void)thread;
    return OPRT_NOT_SUPPORTED;
}
