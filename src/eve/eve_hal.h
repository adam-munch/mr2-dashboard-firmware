/*
 * Hardware interface used by the EVE driver. eve_hal_stm32.c implements it
 * for this board; the host tests swap in a mock.
 */
#ifndef EVE_HAL_H
#define EVE_HAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Configure CS, PD, INT and the SPI bus (slow clock, CS released). */
void eve_hal_init(void);

/* Slow (<= 11 MHz) until the EVE clock is running, fast (<= 30 MHz) after. */
void eve_hal_spi_fast(bool fast);

void eve_hal_cs_assert(void);
void eve_hal_cs_release(void);

/* Drive /PD: true = power down (pin low), false = run (pin high). */
void eve_hal_power_down(bool asserted);

void eve_hal_spi_write(const uint8_t *data, size_t len);
void eve_hal_spi_read(uint8_t *data, size_t len);

void eve_hal_delay_ms(uint32_t ms);
uint32_t eve_hal_millis(void);

#endif /* EVE_HAL_H */
