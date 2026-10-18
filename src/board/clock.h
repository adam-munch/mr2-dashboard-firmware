#ifndef CLOCK_H
#define CLOCK_H

#include <stdbool.h>

/* Switch SYSCLK to 64 MHz via the PLL and update SystemCoreClock. The PLL
 * runs from the crystal if board.h configures one, otherwise from HSI16. */
void clock_init(void);

/* True if the PLL runs from the crystal, false if from HSI16 (no crystal
 * configured, or it failed to start). */
bool clock_using_hse(void);

#endif /* CLOCK_H */
