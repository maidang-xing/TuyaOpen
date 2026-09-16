#ifndef TAL_PORT_UART_H
#define TAL_PORT_UART_H

#include <stdint.h>

typedef void *TAL_PORT_UART_HANDLE;

TAL_PORT_UART_HANDLE tal_uart_port_open(uint32_t port, uint32_t baudrate);
int tal_uart_port_close(TAL_PORT_UART_HANDLE handle);
int tal_uart_port_read(TAL_PORT_UART_HANDLE handle, void *buffer, uint16_t length);
int tal_uart_port_write(TAL_PORT_UART_HANDLE handle, const void *buffer, uint16_t length);
int tal_uart_port_ioctl(TAL_PORT_UART_HANDLE handle, uint32_t command, uint32_t argument);

#endif
