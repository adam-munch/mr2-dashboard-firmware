#include "board.h"

#include "clock.h"
#include "debug_uart.h"
#include "gpio.h"
#include "systick.h"

void board_init(void)
{
    clock_init();
    systick_init();

    /*
     * The STM32G0B1 powers up with USB-PD "dead battery" pull-downs on the
     * UCPD CC pins (PA8, PB15, PD0, PD2). Strobing these bits releases them
     * so those pins behave as normal GPIOs.
     */
    RCC->APBENR2 |= RCC_APBENR2_SYSCFGEN;
    SYSCFG->CFGR1 |= SYSCFG_CFGR1_UCPD1_STROBE | SYSCFG_CFGR1_UCPD2_STROBE;

#if BOARD_HAS_STATUS_LED
    gpio_config(LED_PORT, LED_PIN, PIN_OUTPUT, PIN_PULL_NONE, PIN_SPEED_LOW, 0u);
    board_led_set(false);
#endif

    debug_uart_init();
}

void board_led_set(bool on)
{
#if BOARD_HAS_STATUS_LED
    gpio_write(LED_PORT, LED_PIN, on == (LED_ACTIVE_HIGH != 0));
#else
    (void)on;
#endif
}

void board_led_toggle(void)
{
#if BOARD_HAS_STATUS_LED
    LED_PORT->ODR ^= 1u << LED_PIN;
#endif
}
