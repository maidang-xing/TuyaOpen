#include "tkl_queue.h"

#include "tal_system_port.h"
#include "tuya_error_code.h"

OPERATE_RET tkl_queue_create_init(TKL_QUEUE_HANDLE *queue, int msgsize, int msgcount)
{
    if (queue == NULL || msgsize <= 0 || msgcount <= 0) {
        return OPRT_INVALID_PARM;
    }
    *queue = tal_system_port_queue_create((uint32_t)msgsize, (uint32_t)msgcount);
    return *queue == NULL ? OPRT_OS_ADAPTER_QUEUE_CREAT_FAILED : OPRT_OK;
}

OPERATE_RET tkl_queue_post(const TKL_QUEUE_HANDLE queue, void *data, uint32_t timeout)
{
    if (queue == NULL || data == NULL) {
        return OPRT_INVALID_PARM;
    }
    return tal_system_port_queue_post(queue, data, timeout) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_QUEUE_SEND_FAIL;
}

OPERATE_RET tkl_queue_fetch(const TKL_QUEUE_HANDLE queue, void *msg, uint32_t timeout)
{
    if (queue == NULL || msg == NULL) {
        return OPRT_INVALID_PARM;
    }
    return tal_system_port_queue_fetch(queue, msg, timeout) == 0 ? OPRT_OK : OPRT_OS_ADAPTER_QUEUE_RECV_FAIL;
}

void tkl_queue_free(const TKL_QUEUE_HANDLE queue)
{
    if (queue != NULL) {
        (void)tal_system_port_queue_release(queue);
    }
}
