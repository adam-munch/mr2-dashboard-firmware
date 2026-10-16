#ifndef CLOCK_H
#define CLOCK_H

/* Switch SYSCLK to 64 MHz (HSI16 -> PLL) and update SystemCoreClock. */
void clock_init(void);

#endif /* CLOCK_H */
