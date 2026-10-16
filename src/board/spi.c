#include "spi.h"

/*
 * The data register must be accessed as 8 bits. A 16-bit access would pack
 * two frames into the FIFO.
 */
#define SPI_DR8(spi) (*(volatile uint8_t *)&(spi)->DR)

void spi_init(SPI_TypeDef *spi, spi_prescaler_t presc)
{
    spi->CR1 = 0u;
    /* 8-bit frames; RXNE fires on every byte (FRXTH = 1/4 FIFO). */
    spi->CR2 = (7u << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
    /* Master, CPOL = 0, CPHA = 0, software NSS held high internally. */
    spi->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | ((uint32_t)presc << SPI_CR1_BR_Pos);
    spi->CR1 |= SPI_CR1_SPE;
}

void spi_set_prescaler(SPI_TypeDef *spi, spi_prescaler_t presc)
{
    while ((spi->SR & SPI_SR_BSY) != 0u) {
    }
    spi->CR1 &= ~SPI_CR1_SPE;
    spi->CR1 = (spi->CR1 & ~SPI_CR1_BR_Msk) | ((uint32_t)presc << SPI_CR1_BR_Pos);
    spi->CR1 |= SPI_CR1_SPE;
}

uint8_t spi_transfer(SPI_TypeDef *spi, uint8_t tx)
{
    while ((spi->SR & SPI_SR_TXE) == 0u) {
    }
    SPI_DR8(spi) = tx;
    while ((spi->SR & SPI_SR_RXNE) == 0u) {
    }
    return SPI_DR8(spi);
}

void spi_write(SPI_TypeDef *spi, const uint8_t *data, size_t len)
{
    /* Waiting for RXNE after each byte keeps the RX FIFO drained (no
     * overrun) and guarantees the last bit is out before we return, so
     * the caller can release chip select straight away. */
    for (size_t i = 0; i < len; i++) {
        (void)spi_transfer(spi, data[i]);
    }
}

void spi_read(SPI_TypeDef *spi, uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        data[i] = spi_transfer(spi, 0x00u);
    }
}
