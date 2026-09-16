#include "tal_uart_port.h"

#include "device.h"
#include "uart.h"

static const char *uart_device_name(uint32_t port)
{
    return port == 0u ? "uart2" : "uart1";
}

TAL_PORT_UART_HANDLE tal_uart_port_open(uint32_t port, uint32_t baudrate)
{
    void *handle = dev_open(uart_device_name(port), NULL);
    if (handle == NULL) {
        return NULL;
    }
    if (dev_ioctl(handle, UART_SET_RECV_BLOCK, 1u) != 0 ||
        dev_ioctl(handle, UART_SET_BAUDRATE, baudrate) != 0 ||
        dev_ioctl(handle, UART_START, 0u) != 0) {
        dev_close(handle);
        return NULL;
    }
    return (TAL_PORT_UART_HANDLE)handle;
}

int tal_uart_port_close(TAL_PORT_UART_HANDLE handle)
{
    return handle == NULL ? -1 : dev_close(handle);
}

int tal_uart_port_read(TAL_PORT_UART_HANDLE handle, void *buffer, uint16_t length)
{
    return handle == NULL || buffer == NULL ? -1 : dev_read(handle, buffer, length);
}

int tal_uart_port_write(TAL_PORT_UART_HANDLE handle, const void *buffer, uint16_t length)
{
    return handle == NULL || buffer == NULL ? -1 : dev_write(handle, (void *)buffer, length);
}

int tal_uart_port_ioctl(TAL_PORT_UART_HANDLE handle, uint32_t command, uint32_t argument)
{
    return handle == NULL ? -1 : dev_ioctl(handle, (int)command, argument);
}
