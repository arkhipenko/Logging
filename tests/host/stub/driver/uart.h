/* Host stub of the ESP-IDF UART API used by serial_esp32.c (tests only) */
#ifndef STUB_DRIVER_UART_H
#define STUB_DRIVER_UART_H
#include <stdio.h>
typedef int uart_port_t;
#define UART_NUM_0 0
static inline int uart_write_bytes(uart_port_t port, const void *data, size_t len) {
    (void)port;
    return (int)fwrite(data, 1, len, stdout);
}
#endif
