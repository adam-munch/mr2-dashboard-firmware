#include "clock.h"

#include "board.h"

/*
 * PLL: VCO 128 MHz, R = 2 -> SYSCLK 64 MHz. AHB and APB prescalers stay at
 * their reset value of 1, so HCLK = PCLK = 64 MHz.
 *
 *   HSI16:        M = 1 (16 MHz in), N = 8
 *   HSE crystal:  M = HSE / 4 MHz (4 MHz in), N = 32
 *
 * HSI16 is +/-1 %: fine for SPI and UART, marginal for CAN (see board.h).
 */

#if BOARD_HSE_HZ != 0
#if (BOARD_HSE_HZ % 4000000u) != 0 || BOARD_HSE_HZ < 4000000u || BOARD_HSE_HZ > 32000000u
#error "BOARD_HSE_HZ must be a multiple of 4 MHz from 4 to 32 MHz"
#endif
#endif

/* Crystals start in a few ms. This is ~100 ms at the 16 MHz reset clock. */
#define HSE_STARTUP_LOOPS 200000u

static bool s_using_hse;

static bool hse_start(void)
{
#if BOARD_HSE_HZ != 0
#if BOARD_HSE_BYPASS
    RCC->CR |= RCC_CR_HSEBYP; /* must be set before HSEON */
#endif
    RCC->CR |= RCC_CR_HSEON;
    for (uint32_t i = 0u; i < HSE_STARTUP_LOOPS; i++) {
        if ((RCC->CR & RCC_CR_HSERDY) != 0u) {
            return true;
        }
    }
    RCC->CR &= ~(RCC_CR_HSEON | RCC_CR_HSEBYP);
#endif
    return false;
}

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

    s_using_hse = hse_start();

    uint32_t pllcfgr;
    if (s_using_hse) {
        pllcfgr = RCC_PLLCFGR_PLLSRC_HSE |
                  ((BOARD_HSE_HZ / 4000000u - 1u) << RCC_PLLCFGR_PLLM_Pos) |
                  (32u << RCC_PLLCFGR_PLLN_Pos);
    } else {
        pllcfgr = RCC_PLLCFGR_PLLSRC_HSI |
                  (0u << RCC_PLLCFGR_PLLM_Pos) | /* M = 1 */
                  (8u << RCC_PLLCFGR_PLLN_Pos);  /* N = 8 */
    }
    RCC->PLLCFGR = pllcfgr | (1u << RCC_PLLCFGR_PLLR_Pos) | /* R = 2 */
                   RCC_PLLCFGR_PLLREN;

    RCC->CR |= RCC_CR_PLLON;
    while ((RCC->CR & RCC_CR_PLLRDY) == 0u) {
    }

    RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_Msk) | RCC_CFGR_SW_1; /* SYSCLK = PLLRCLK */
    while ((RCC->CFGR & RCC_CFGR_SWS_Msk) != RCC_CFGR_SWS_1) {
    }

    /* Not SystemCoreClockUpdate(): system_stm32g0xx.c assumes an 8 MHz HSE
     * (HSE_VALUE). Both PLL setups above give exactly this. */
    SystemCoreClock = BOARD_SYSCLK_HZ;
}

bool clock_using_hse(void)
{
    return s_using_hse;
}
