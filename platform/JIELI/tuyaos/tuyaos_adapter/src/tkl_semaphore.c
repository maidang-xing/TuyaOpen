#include "tkl_semaphore.h"

#include "tal_system_port.h"
#include "tuya_error_code.h"

OPERATE_RET tkl_semaphore_create_init(TKL_SEM_HANDLE *handle, uint32_t sem_cnt, uint32_t sem_max)
{
    (void)sem_max;
    if (handle == NULL) {
        return OPRT_INVALID_PARM;
    }
    *handle = tal_system_port_sem_create(sem_cnt);
    return *handle == NULL ? OPRT_OS_ADAPTER_SEM_CREAT_FAILED : OPRT_OK;
}

OPERATE_RET tkl_semaphore_wait(const TKL_SEM_HANDLE handle, uint32_t timeout)
{
    int result;
    if (handle == NULL) {
        return OPRT_INVALID_PARM;
    }
    result = timeout == 0u ? tal_system_port_sem_wait(handle, 0u) : tal_system_port_sem_wait(handle, timeout);
    if (result == -2) {
        return OPRT_OS_ADAPTER_SEM_WAIT_TIMEOUT;
    }
    return result == 0 ? OPRT_OK : OPRT_OS_ADAPTER_SEM_WAIT_FAILED;
}

OPERATE_RET tkl_semaphore_post(const TKL_SEM_HANDLE handle)
{
    return handle == NULL ? OPRT_INVALID_PARM :
           (tal_system_port_sem_post(handle) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_SEM_POST_FAILED);
}

OPERATE_RET tkl_semaphore_release(const TKL_SEM_HANDLE handle)
{
    return handle == NULL ? OPRT_INVALID_PARM :
           (tal_system_port_sem_release(handle) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_SEM_RELEASE_FAILED);
}
