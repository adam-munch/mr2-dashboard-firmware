/* Blocking TX-only debug UART. stdout (printf) is routed here. */
#ifndef DEBUG_UART_H
#define DEBUG_UART_H

#include <stddef.h>

void debug_uart_init(void);

/* Write raw bytes. "\n" is expanded to "\r\n". No-op if the UART is disabled. */
void debug_uart_write(const char *buf, size_t len);

#endif /* DEBUG_UART_H */
