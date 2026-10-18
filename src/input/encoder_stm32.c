/* Rotary encoder on a timer in encoder mode, push switch on a GPIO (pins in board.h). */
#include "encoder.h"

#include "board.h"
#include "gpio.h"

void encoder_hw_init(void)
{
#if BOARD_HAS_ENCODER
    gpio_config(ENC_A_PORT, ENC_A_PIN, PIN_AF, PIN_PULL_UP, PIN_SPEED_LOW, ENC_A_AF);
    gpio_config(ENC_B_PORT, ENC_B_PIN, PIN_AF, PIN_PULL_UP, PIN_SPEED_LOW, ENC_B_AF);
    gpio_config(ENC_SW_PORT, ENC_SW_PIN, PIN_INPUT, PIN_PULL_UP, PIN_SPEED_LOW, 0u);

    ENC_TIM_CLK_ENABLE();
    TIM_TypeDef *const tim = ENC_TIM;
    tim->CR1 = TIM_CR1_CKD_1; /* filter clock: tDTS = 4 timer clocks (16 MHz) */

    /* Encoder mode 3: count on every edge of both TI1 and TI2, up or down
     * depending on the level of the other one. Contact bounce on one channel
     * moves the count back and forth and settles where the contacts do. */
    tim->SMCR = TIM_SMCR_SMS_0 | TIM_SMCR_SMS_1;

    /* CC1 and CC2 as inputs on TI1 and TI2, through the longest digital
     * filter: sampled at fDTS / 32 = 500 kHz and 8 samples must agree, so
     * spikes shorter than 16 us (ignition noise) are never counted. */
    tim->CCMR1 = TIM_CCMR1_CC1S_0 | (0xFu << TIM_CCMR1_IC1F_Pos) | TIM_CCMR1_CC2S_0 |
                 (0xFu << TIM_CCMR1_IC2F_Pos);

    /* Inverting TI1 reverses the counting direction. */
    tim->CCER = ENC_REVERSE ? TIM_CCER_CC1P : 0u;

    tim->ARR = 0xFFFFu;
    tim->CNT = 0u;
    tim->CR1 |= TIM_CR1_CEN;
#endif
}

uint16_t encoder_hw_count(void)
{
#if BOARD_HAS_ENCODER
    return (uint16_t)ENC_TIM->CNT;
#else
    return 0u;
#endif
}

bool encoder_hw_switch(void)
{
#if BOARD_HAS_ENCODER
    return !gpio_read(ENC_SW_PORT, ENC_SW_PIN); /* pressed pulls the pin to GND */
#else
    return false;
#endif
}
