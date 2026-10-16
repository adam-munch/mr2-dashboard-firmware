/* Minimal register-level GPIO helpers. */
#ifndef GPIO_H
#define GPIO_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32g0xx.h"

typedef enum {
    PIN_INPUT  = 0u,
    PIN_OUTPUT = 1u,
    PIN_AF     = 2u,
    PIN_ANALOG = 3u,
} pin_mode_t;

typedef enum {
    PIN_PULL_NONE = 0u,
    PIN_PULL_UP   = 1u,
    PIN_PULL_DOWN = 2u,
} pin_pull_t;

typedef enum {
    PIN_SPEED_LOW       = 0u,
    PIN_SPEED_MEDIUM    = 1u,
    PIN_SPEED_HIGH      = 2u,
    PIN_SPEED_VERY_HIGH = 3u,
} pin_speed_t;

/* Enable the IOPORT clock for a GPIO port. */
void gpio_clock_enable(GPIO_TypeDef *port);

/*
 * Configure one pin (push-pull output type). `af` is used only in PIN_AF
 * mode. Enables the port clock if needed.
 */
void gpio_config(GPIO_TypeDef *port, uint32_t pin, pin_mode_t mode, pin_pull_t pull,
                 pin_speed_t speed, uint32_t af);

static inline void gpio_set(GPIO_TypeDef *port, uint32_t pin)
{
    port->BSRR = 1u << pin;
}

static inline void gpio_clear(GPIO_TypeDef *port, uint32_t pin)
{
    port->BRR = 1u << pin;
}

static inline void gpio_write(GPIO_TypeDef *port, uint32_t pin, bool high)
{
    if (high) {
        gpio_set(port, pin);
    } else {
        gpio_clear(port, pin);
    }
}

static inline bool gpio_read(const GPIO_TypeDef *port, uint32_t pin)
{
    return (port->IDR & (1u << pin)) != 0u;
}

#endif /* GPIO_H */
