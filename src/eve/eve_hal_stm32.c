/* EVE hardware interface for the STM32G0B1 board (pins in board.h). */
#include "eve_hal.h"

#include "board.h"
#include "gpio.h"
#include "spi.h"
#include "systick.h"

void eve_hal_init(void)
{
    /* Outputs get their idle level before they are switched to output mode. */
    gpio_clock_enable(EVE_CS_PORT);
    gpio_set(EVE_CS_PORT, EVE_CS_PIN);
    gpio_config(EVE_CS_PORT, EVE_CS_PIN, PIN_OUTPUT, PIN_PULL_NONE, PIN_SPEED_HIGH, 0u);

    gpio_clock_enable(EVE_PD_PORT);
    gpio_clear(EVE_PD_PORT, EVE_PD_PIN); /* held in power-down until eve_init() releases it */
    gpio_config(EVE_PD_PORT, EVE_PD_PIN, PIN_OUTPUT, PIN_PULL_NONE, PIN_SPEED_LOW, 0u);

    /* /INT is open-drain on the module; not used yet, but keep it defined. */
    gpio_config(EVE_INT_PORT, EVE_INT_PIN, PIN_INPUT, PIN_PULL_UP, PIN_SPEED_LOW, 0u);

    gpio_config(EVE_SCK_PORT, EVE_SCK_PIN, PIN_AF, PIN_PULL_NONE, PIN_SPEED_HIGH, EVE_SCK_AF);
    gpio_config(EVE_MISO_PORT, EVE_MISO_PIN, PIN_AF, PIN_PULL_UP, PIN_SPEED_HIGH, EVE_MISO_AF);
    gpio_config(EVE_MOSI_PORT, EVE_MOSI_PIN, PIN_AF, PIN_PULL_NONE, PIN_SPEED_HIGH, EVE_MOSI_AF);

    EVE_SPI_CLK_ENABLE();
    spi_init(EVE_SPI, EVE_SPI_PRESC_INIT);
}

void eve_hal_spi_fast(bool fast)
{
    spi_set_prescaler(EVE_SPI, fast ? EVE_SPI_PRESC_RUN : EVE_SPI_PRESC_INIT);
}

void eve_hal_cs_assert(void)
{
    gpio_clear(EVE_CS_PORT, EVE_CS_PIN);
}

void eve_hal_cs_release(void)
{
    gpio_set(EVE_CS_PORT, EVE_CS_PIN);
}

void eve_hal_power_down(bool asserted)
{
    gpio_write(EVE_PD_PORT, EVE_PD_PIN, !asserted);
}

void eve_hal_spi_write(const uint8_t *data, size_t len)
{
    spi_write(EVE_SPI, data, len);
}

void eve_hal_spi_read(uint8_t *data, size_t len)
{
    spi_read(EVE_SPI, data, len);
}

void eve_hal_delay_ms(uint32_t ms)
{
    delay_ms(ms);
}

uint32_t eve_hal_millis(void)
{
    return millis();
}
