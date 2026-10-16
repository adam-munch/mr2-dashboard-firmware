/*
 * Board definition for the MR2 dashboard controller (STM32G0B1CBT6, LQFP48).
 *
 * Every pin and board option lives in this file. The defaults below are a
 * sensible starting point: CHECK THEM AGAINST YOUR SCHEMATIC before
 * flashing, and edit them here if your routing differs.
 *
 * Display: Newhaven NHD-7.0-800480FT-CSXP (FT812 EVE), on CN1 (2x10 IDC) or
 * CN2 (20-pin FFC). Pin numbers below refer to the module's connector.
 *
 *   Module pin        Signal        MCU pin (default)
 *   ---------------   -----------   ------------------------------
 *   1  VDD            3.3 V         3V3 rail (~100 mA)
 *   2  GND            GND           GND
 *   3  SCK            SPI clock     PA5  SPI1_SCK  (AF0)
 *   4  MISO/IO1       SPI MISO      PA6  SPI1_MISO (AF0)
 *   5  MOSI/IO0       SPI MOSI      PA7  SPI1_MOSI (AF0)
 *   6  /CS            chip select   PA4  GPIO output
 *   7  /INT           interrupt     PB1  GPIO input, pull-up
 *   8  /PD            power down    PB0  GPIO output
 *   9  AUDIO_L        audio PWM     (unused, needs filter + amp)
 *   11-14 GPIO0..3    EVE GPIOs     (unused)
 *   17,18 VBL         backlight     3.3 V or 5 V, 760 mA @ 3.3 V
 *   19,20 GND         GND           GND
 *
 * CAN bus to the ECU, through a 3.3 V CAN transceiver (TJA1051T/3,
 * SN65HVD230, MCP2562 with VIO = 3.3 V, ...):
 *
 *   Transceiver pin   MCU pin (default)
 *   ---------------   ------------------------------
 *   TXD               PB9  FDCAN1_TX (AF3)
 *   RXD               PB8  FDCAN1_RX (AF3)
 *   S / STBY / Rs     tie low, or see BOARD_HAS_CAN_STBY
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "spi.h"
#include "stm32g0xx.h"

/* ---------------------------------------------------------------------------
 * Clock
 * ------------------------------------------------------------------------ */

/* SYSCLK = HCLK = PCLK = 64 MHz from the PLL (see clock.c). */
#define BOARD_SYSCLK_HZ 64000000u

/*
 * External crystal on PF0-OSC_IN / PF1-OSC_OUT, in Hz: a multiple of 4 MHz
 * from 4 to 32 MHz (8 MHz is typical). 0 = no crystal, run from HSI16.
 *
 * CAN needs an accurate clock. HSI16 is only +/-1 %, which uses up most of
 * the CAN bit-timing tolerance, so a board on the bus should have a crystal.
 * If the crystal doesn't start, clock.c falls back to HSI16 and says so on
 * the debug UART.
 */
#define BOARD_HSE_HZ     0u
#define BOARD_HSE_BYPASS 0 /* 1: an oscillator drives OSC_IN instead of a crystal */

/* ---------------------------------------------------------------------------
 * EVE display (FT812) on SPI1
 * ------------------------------------------------------------------------ */

#define EVE_SPI             SPI1
#define EVE_SPI_CLK_ENABLE() (RCC->APBENR2 |= RCC_APBENR2_SPI1EN)

#define EVE_SCK_PORT   GPIOA
#define EVE_SCK_PIN    5u
#define EVE_SCK_AF     0u

#define EVE_MISO_PORT  GPIOA
#define EVE_MISO_PIN   6u
#define EVE_MISO_AF    0u

#define EVE_MOSI_PORT  GPIOA
#define EVE_MOSI_PIN   7u
#define EVE_MOSI_AF    0u

#define EVE_CS_PORT    GPIOA
#define EVE_CS_PIN     4u

#define EVE_PD_PORT    GPIOB
#define EVE_PD_PIN     0u

#define EVE_INT_PORT   GPIOB
#define EVE_INT_PIN    1u

/*
 * SPI clock = PCLK / prescaler. The FT81x accepts at most 11 MHz until its
 * PLL is running, and 30 MHz after that. 16 MHz leaves margin for the
 * ribbon cable; try SPI_PRESC_DIV2 (32 MHz) only on a short, clean link
 * and only if you lower PCLK accordingly, since 32 MHz exceeds the FT812's
 * 30 MHz limit.
 */
#define EVE_SPI_PRESC_INIT  SPI_PRESC_DIV8   /* 8 MHz  */
#define EVE_SPI_PRESC_RUN   SPI_PRESC_DIV4   /* 16 MHz */

/* ---------------------------------------------------------------------------
 * CAN bus to the ECU (FDCAN1). Other options on this package: PA11/PA12
 * (also USB) or PD0/PD1, all AF3. FDCAN2 works too: it is on PB0/PB1 (used
 * by the display here), PB5/PB6 or PB12/PB13, AF3.
 * ------------------------------------------------------------------------ */

#define CAN_FDCAN    FDCAN1

#define CAN_RX_PORT  GPIOB
#define CAN_RX_PIN   8u
#define CAN_RX_AF    3u

#define CAN_TX_PORT  GPIOB
#define CAN_TX_PIN   9u
#define CAN_TX_AF    3u

/*
 * Transceiver standby / silent pin (TJA1051 "S", MCP2562 "STBY",
 * SN65HVD230 "Rs"). All of these need it low to talk on the bus. Off by
 * default so an unknown pin is never driven: tie the pin low on the board,
 * or set this to 1 and fix the pin.
 */
#define BOARD_HAS_CAN_STBY 0

#define CAN_STBY_PORT GPIOB
#define CAN_STBY_PIN  7u

/* ---------------------------------------------------------------------------
 * Debug UART (TX only, 115200 8N1). printf() goes here.
 * ------------------------------------------------------------------------ */

#define BOARD_HAS_DEBUG_UART 1

#define DBG_UART              USART2
#define DBG_UART_CLK_ENABLE() (RCC->APBENR1 |= RCC_APBENR1_USART2EN)
#define DBG_UART_TX_PORT      GPIOA
#define DBG_UART_TX_PIN       2u
#define DBG_UART_TX_AF        1u
#define DBG_UART_BAUD         115200u

/* ---------------------------------------------------------------------------
 * Status LED (optional). Off by default so an unknown pin is never driven;
 * set to 1 and fix the pin if your board has one.
 * ------------------------------------------------------------------------ */

#define BOARD_HAS_STATUS_LED 0

#define LED_PORT        GPIOB
#define LED_PIN         2u
#define LED_ACTIVE_HIGH 1

/* ---------------------------------------------------------------------------
 * API
 * ------------------------------------------------------------------------ */

/* Clocks, SysTick, GPIO ports, LED and debug UART. Call first thing in main(). */
void board_init(void);

void board_led_set(bool on);
void board_led_toggle(void);

#endif /* BOARD_H */
