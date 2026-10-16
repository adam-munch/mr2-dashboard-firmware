#include "clock.h"

#include "board.h"

/*
 * HSI16 (16 MHz) -> PLL: M = 1, N = 8 -> VCO 128 MHz, R = 2 -> 64 MHz.
 * AHB and APB prescalers stay at their reset value of 1, so HCLK = PCLK = 64 MHz.
 *
 * HSI16 is +/-1 %, fine for SPI and UART. If you add CAN (FDCAN) later,
 * fit a crystal and run the PLL from HSE: CAN needs a tighter clock.
 */
void clock_init(void)
{
    /* 64 MHz in voltage range 1 (the reset default) needs 2 flash wait states.
     * Raise the latency before raising the clock. */
    FLASH->ACR = (FLASH->ACR & ~FLASH_ACR_LATENCY_Msk) | FLASH_ACR_LATENCY_1 | FLASH_ACR_PRFTEN |
                 FLASH_ACR_ICEN;
    while ((FLASH->ACR & FLASH_ACR_LATENCY_Msk) != FLASH_ACR_LATENCY_1) {
    }

    RCC->CR |= RCC_CR_HSION;
    while ((RCC->CR & RCC_CR_HSIRDY) == 0u) {
    }

    /* The PLL must be off while it is reconfigured. */
    RCC->CR &= ~RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) != 0u) {
    }

    RCC->PLLCFGR = RCC_PLLCFGR_PLLSRC_HSI |
                   (0u << RCC_PLLCFGR_PLLM_Pos) | /* M = 1 */
                   (8u << RCC_PLLCFGR_PLLN_Pos) | /* N = 8 */
                   (1u << RCC_PLLCFGR_PLLR_Pos) | /* R = 2 */
                   RCC_PLLCFGR_PLLREN;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
    }

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_1; /* SYSCLK = PLLRCLK */
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_1) {
    }

    SystemCoreClockUpdate();
}
