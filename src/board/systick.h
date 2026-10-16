/* 1 ms system tick. */
#ifndef SYSTICK_H
#define SYSTICK_H

#include <stdint.h>

/* Start a 1 kHz SysTick from the current SystemCoreClock. */
void systick_init(void);

/* Milliseconds since systick_init(). Wraps after ~49 days. */
uint32_t millis(void);

/* Busy-wait for at least `ms` milliseconds. */
void delay_ms(uint32_t ms);

#endif /* SYSTICK_H */
