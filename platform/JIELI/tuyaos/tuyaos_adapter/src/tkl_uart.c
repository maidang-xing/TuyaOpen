#include "tkl_uart.h"
#include "tuya_error_code.h"

#ifdef bool
#undef bool
#endif
#include "device.h"
#include "asm/uart.h"

#include "jieli_tkl_uart.h"

static void *s_uart_handles[JIELI_TKL_UART_MAX];

static const char *__uart_device_name(TUYA_UART_NUM_E port_id)
{
    return port_id == TUYA_UART_NUM_0 ? "uart2" : "uart1";
}

OPERATE_RET tkl_uart_init(TUYA_UART_NUM_E port_id, TUYA_UART_BASE_CFG_T *cfg)
{
    if (port_id >= JIELI_TKL_UART_MAX || cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (s_uart_handles[port_id] != NULL) {
        return OPRT_OK;
    }

    s_uart_handles[port_id] = dev_open(__uart_device_name(port_id), NULL);
    if (s_uart_handles[port_id] == NULL) {
        return OPRT_COM_ERROR;
    }

    dev_ioctl(s_uart_handles[port_id], UART_SET_RECV_BLOCK, 1);
    dev_ioctl(s_uart_handles[port_id], UART_SET_BAUDRATE, cfg->baudrate);
    dev_ioctl(s_uart_handles[port_id], UART_START, 0);
    return OPRT_OK;
}

OPERATE_RET tkl_uart_deinit(TUYA_UART_NUM_E port_id)
{
    if (port_id >= JIELI_TKL_UART_MAX || s_uart_handles[port_id] == NULL) {
        return OPRT_INVALID_PARM;
    }
    dev_close(s_uart_handles[port_id]);
    s_uart_handles[port_id] = NULL;
    return OPRT_OK;
}

int tkl_uart_write(TUYA_UART_NUM_E port_id, void *buff, uint16_t len)
{
    if (port_id >= JIELI_TKL_UART_MAX || s_uart_handles[port_id] == NULL || buff == NULL) {
        return OPRT_INVALID_PARM;
    }
    return dev_write(s_uart_handles[port_id], buff, len);
}

void tkl_uart_rx_irq_cb_reg(TUYA_UART_NUM_E port_id, TUYA_UART_IRQ_CB rx_cb)
{
    (void)port_id;
    (void)rx_cb;
}

void tkl_uart_tx_irq_cb_reg(TUYA_UART_NUM_E port_id, TUYA_UART_IRQ_CB tx_cb)
{
    (void)port_id;
    (void)tx_cb;
}

int tkl_uart_read(TUYA_UART_NUM_E port_id, void *buff, uint16_t len)
{
    if (port_id >= JIELI_TKL_UART_MAX || s_uart_handles[port_id] == NULL || buff == NULL) {
        return OPRT_INVALID_PARM;
    }
    return dev_read(s_uart_handles[port_id], buff, len);
}

OPERATE_RET tkl_uart_set_tx_int(TUYA_UART_NUM_E port_id, BOOL_T enable)
{
    (void)port_id;
    (void)enable;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_uart_set_rx_flowctrl(TUYA_UART_NUM_E port_id, BOOL_T enable)
{
    (void)port_id;
    (void)enable;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_uart_wait_for_data(TUYA_UART_NUM_E port_id, int timeout_ms)
{
    (void)port_id;
    (void)timeout_ms;
    return OPRT_NOT_SUPPORTED;
}

OPERATE_RET tkl_uart_ioctl(TUYA_UART_NUM_E port_id, uint32_t cmd, void *arg)
{
    (void)port_id;
    (void)cmd;
    (void)arg;
    return OPRT_NOT_SUPPORTED;
}
