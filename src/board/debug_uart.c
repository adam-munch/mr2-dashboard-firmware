#include "debug_uart.h"

#include "board.h"
#include "gpio.h"

#if BOARD_HAS_DEBUG_UART

void debug_uart_init(void)
{
    DBG_UART_CLK_ENABLE();
    gpio_config(DBG_UART_TX_PORT, DBG_UART_TX_PIN, PIN_AF, PIN_PULL_UP, PIN_SPEED_LOW,
                DBG_UART_TX_AF);

    /* The USART kernel clock defaults to PCLK. 16x oversampling. */
    DBG_UART->CR1 = 0u;
    DBG_UART->BRR = (SystemCoreClock + DBG_UART_BAUD / 2u) / DBG_UART_BAUD;
    DBG_UART->CR1 = USART_CR1_TE | USART_CR1_UE;
}

static void put_byte(char c)
{
    while ((DBG_UART->ISR & USART_ISR_TXE_TXFNF) == 0u) {
    }
    DBG_UART->TDR = (uint8_t)c;
}

void debug_uart_write(const char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        if (buf[i] == '\n') {
            put_byte('\r');
        }
        put_byte(buf[i]);
    }
}

#else /* !BOARD_HAS_DEBUG_UART */

void debug_uart_init(void)
{
}

void debug_uart_write(const char *buf, size_t len)
{
    (void)buf;
    (void)len;
}

#endif
