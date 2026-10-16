/* Polled SPI master driver (8-bit frames, mode 0, MSB first, software NSS). */
#ifndef SPI_H
#define SPI_H

#include <stddef.h>
#include <stdint.h>

#include "stm32g0xx.h"

/* SPI clock = PCLK / 2^(n+1). */
typedef enum {
    SPI_PRESC_DIV2   = 0u,
    SPI_PRESC_DIV4   = 1u,
    SPI_PRESC_DIV8   = 2u,
    SPI_PRESC_DIV16  = 3u,
    SPI_PRESC_DIV32  = 4u,
    SPI_PRESC_DIV64  = 5u,
    SPI_PRESC_DIV128 = 6u,
    SPI_PRESC_DIV256 = 7u,
} spi_prescaler_t;

/* The peripheral clock must already be enabled and the pins configured. */
void spi_init(SPI_TypeDef *spi, spi_prescaler_t presc);

/* Change the bit rate. Waits for any transfer in progress to finish. */
void spi_set_prescaler(SPI_TypeDef *spi, spi_prescaler_t presc);

uint8_t spi_transfer(SPI_TypeDef *spi, uint8_t tx);

/* Send `len` bytes; received bytes are discarded. */
void spi_write(SPI_TypeDef *spi, const uint8_t *data, size_t len);

/* Receive `len` bytes while sending 0x00. */
void spi_read(SPI_TypeDef *spi, uint8_t *data, size_t len);

#endif /* SPI_H */
