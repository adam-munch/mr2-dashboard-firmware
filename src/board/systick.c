#include "systick.h"

#include "stm32g0xx.h"

static volatile uint32_t s_ticks;

void SysTick_Handler(void)
{
    s_ticks++;
}

void systick_init(void)
{
    (void)SysTick_Config(SystemCoreClock / 1000u);
}

uint32_t millis(void)
{
    return s_ticks;
}

void delay_ms(uint32_t ms)
{
    const uint32_t start = s_ticks;
    /* `<=` so a partial first tick never shortens the delay. */
    while ((uint32_t)(s_ticks - start) <= ms) {
    }
}
