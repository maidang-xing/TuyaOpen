#include "tkl_uart.h"
#include "tuya_error_code.h"

#include "tkl_uart_port.h"
#include "tal_uart_port.h"

static void *s_uart_handles[TKL_UART_PORT_MAX];

OPERATE_RET tkl_uart_init(TUYA_UART_NUM_E port_id, TUYA_UART_BASE_CFG_T *cfg)
{
    if (port_id >= TKL_UART_PORT_MAX || cfg == NULL) {
        return OPRT_INVALID_PARM;
    }
    if (s_uart_handles[port_id] != NULL) {
        return OPRT_OK;
    }

    s_uart_handles[port_id] = tal_uart_port_open(port_id, cfg->baudrate);
    if (s_uart_handles[port_id] == NULL) {
        return OPRT_OS_ADAPTER_UART_INIT_FAILED;
    }
    return OPRT_OK;
}

OPERATE_RET tkl_uart_deinit(TUYA_UART_NUM_E port_id)
{
    if (port_id >= TKL_UART_PORT_MAX || s_uart_handles[port_id] == NULL) {
        return OPRT_INVALID_PARM;
    }
    tal_uart_port_close(s_uart_handles[port_id]);
    s_uart_handles[port_id] = NULL;
    return OPRT_OK;
}

int tkl_uart_write(TUYA_UART_NUM_E port_id, void *buff, uint16_t len)
{
    if (port_id >= TKL_UART_PORT_MAX || s_uart_handles[port_id] == NULL || buff == NULL) {
        return OPRT_INVALID_PARM;
    }
    int ret = tal_uart_port_write(s_uart_handles[port_id], buff, len);
    return ret < 0 ? OPRT_OS_ADAPTER_UART_SEND_FAILED : ret;
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
    if (port_id >= TKL_UART_PORT_MAX || s_uart_handles[port_id] == NULL || buff == NULL) {
        return OPRT_INVALID_PARM;
    }
    int ret = tal_uart_port_read(s_uart_handles[port_id], buff, len);
    return ret < 0 ? OPRT_OS_ADAPTER_UART_READ_FAILED : ret;
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
