#include "tkl_mutex.h"

#include "tal_system_port.h"
#include "tuya_error_code.h"

OPERATE_RET tkl_mutex_create_init(TKL_MUTEX_HANDLE *handle)
{
    if (handle == NULL) {
        return OPRT_INVALID_PARM;
    }
    *handle = tal_system_port_mutex_create();
    return *handle == NULL ? OPRT_OS_ADAPTER_MUTEX_CREAT_FAILED : OPRT_OK;
}

OPERATE_RET tkl_mutex_lock(const TKL_MUTEX_HANDLE handle)
{
    return handle == NULL ? OPRT_INVALID_PARM :
           (tal_system_port_mutex_lock(handle, 0xFFFFFFFFu) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED);
}

OPERATE_RET tkl_mutex_trylock(const TKL_MUTEX_HANDLE handle)
{
    return handle == NULL ? OPRT_INVALID_PARM :
           (tal_system_port_mutex_trylock(handle) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_MUTEX_LOCK_FAILED);
}

OPERATE_RET tkl_mutex_unlock(const TKL_MUTEX_HANDLE handle)
{
    return handle == NULL ? OPRT_INVALID_PARM :
           (tal_system_port_mutex_unlock(handle) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_MUTEX_UNLOCK_FAILED);
}

OPERATE_RET tkl_mutex_release(const TKL_MUTEX_HANDLE handle)
{
    return handle == NULL ? OPRT_INVALID_PARM :
           (tal_system_port_mutex_release(handle) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_MUTEX_RELEASE_FAILED);
}
