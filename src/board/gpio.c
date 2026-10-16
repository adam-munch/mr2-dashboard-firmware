#include "gpio.h"

void gpio_clock_enable(GPIO_TypeDef *port)
{
    /* GPIO ports sit 0x400 apart starting at GPIOA, in the same order as
     * their enable bits in RCC_IOPENR. */
    uint32_t index = ((uint32_t)port - GPIOA_BASE) / 0x400u;
    RCC->IOPENR |= 1u << index;
    (void)RCC->IOPENR; /* read back: the clock is running before first access */
}

void gpio_config(GPIO_TypeDef *port, uint32_t pin, pin_mode_t mode, pin_pull_t pull,
                 pin_speed_t speed, uint32_t af)
{
    gpio_clock_enable(port);

    const uint32_t shift2 = pin * 2u;

    port->OTYPER &= ~(1u << pin);
    port->OSPEEDR = (port->OSPEEDR & ~(3u << shift2)) | ((uint32_t)speed << shift2);
    port->PUPDR = (port->PUPDR & ~(3u << shift2)) | ((uint32_t)pull << shift2);

    if (mode == PIN_AF) {
        volatile uint32_t *afr = &port->AFR[pin >> 3];
        const uint32_t shift4 = (pin & 7u) * 4u;
        *afr = (*afr & ~(0xFu << shift4)) | ((af & 0xFu) << shift4);
    }

    port->MODER = (port->MODER & ~(3u << shift2)) | ((uint32_t)mode << shift2);
}
