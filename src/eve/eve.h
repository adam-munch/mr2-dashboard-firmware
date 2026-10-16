/*
 * Driver for FT81x EVE graphics controllers (tested target: FT812 on the
 * Newhaven NHD-7.0-800480FT-CSXP).
 *
 * Typical frame:
 *
 *     eve_cmd_dlstart();
 *     eve_cmd(CLEAR_COLOR_RGB(0, 0, 0));
 *     eve_cmd(CLEAR(1, 1, 1));
 *     eve_cmd_text(400, 240, 31, OPT_CENTER, "Hello");
 *     eve_cmd(DISPLAY());
 *     eve_cmd_swap();
 *     eve_cmd_flush();
 *
 * Commands are collected in a RAM buffer and sent to the co-processor FIFO
 * by eve_cmd_flush() (or automatically when the buffer fills up).
 */
#ifndef EVE_H
#define EVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ft81x.h"

typedef enum {
    EVE_OK = 0,
    EVE_ERR_NO_RESPONSE,   /* REG_ID never read back 0x7C: wiring, power, /PD */
    EVE_ERR_RESET_TIMEOUT, /* engines did not come out of reset               */
    EVE_ERR_COPRO_FAULT,   /* co-processor faulted (bad command)              */
    EVE_ERR_TIMEOUT,       /* co-processor did not finish in time             */
} eve_status_t;

/* Panel timing and module options. The register names match the FT81x guide. */
typedef struct {
    uint16_t hsize;   /* REG_HSIZE   */
    uint16_t hcycle;  /* REG_HCYCLE  */
    uint16_t hoffset; /* REG_HOFFSET */
    uint16_t hsync0;  /* REG_HSYNC0  */
    uint16_t hsync1;  /* REG_HSYNC1  */
    uint16_t vsize;   /* REG_VSIZE   */
    uint16_t vcycle;  /* REG_VCYCLE  */
    uint16_t voffset; /* REG_VOFFSET */
    uint16_t vsync0;  /* REG_VSYNC0  */
    uint16_t vsync1;  /* REG_VSYNC1  */
    uint8_t pclk;     /* REG_PCLK: pixel clock = 60 MHz / pclk */
    uint8_t pclk_pol; /* REG_PCLK_POL */
    uint8_t swizzle;  /* REG_SWIZZLE  */
    uint8_t cspread;  /* REG_CSPREAD  */
    uint8_t dither;   /* REG_DITHER   */
    bool ext_clock;   /* crystal fitted: use HCMD_CLKEXT instead of the internal oscillator */
    bool has_touch;   /* false: resistive touch engine is switched off */
    uint16_t backlight_pwm_hz; /* REG_PWM_HZ */
} eve_panel_t;

/* ---------------------------------------------------------------------------
 * Bring-up
 * ------------------------------------------------------------------------ */

/*
 * Power-cycle the EVE via /PD, start its clock, program the panel timing
 * and start the display with a black screen. The backlight is left off:
 * turn it on with eve_backlight_set() once something is drawn.
 */
eve_status_t eve_init(const eve_panel_t *panel);

/* Chip ID read during eve_init(), e.g. 0x00011208 for an FT812. */
uint32_t eve_chip_id(void);

/* 0 = off ... 128 = full brightness. */
void eve_backlight_set(uint8_t duty);

const char *eve_status_str(eve_status_t status);

/* ---------------------------------------------------------------------------
 * Memory and register access
 * ------------------------------------------------------------------------ */

void eve_host_cmd(uint8_t cmd, uint8_t param);

uint8_t eve_rd8(uint32_t addr);
uint16_t eve_rd16(uint32_t addr);
uint32_t eve_rd32(uint32_t addr);
void eve_rd_mem(uint32_t addr, uint8_t *dst, size_t len);

void eve_wr8(uint32_t addr, uint8_t value);
void eve_wr16(uint32_t addr, uint16_t value);
void eve_wr32(uint32_t addr, uint32_t value);
void eve_wr_mem(uint32_t addr, const uint8_t *src, size_t len);

/* ---------------------------------------------------------------------------
 * Co-processor command FIFO
 * ------------------------------------------------------------------------ */

/* Append one 32-bit word: a display list command or co-processor command/argument. */
void eve_cmd(uint32_t word);

/* Append a NUL-terminated string, zero-padded to a 4-byte boundary. */
void eve_cmd_str(const char *s);

/* Send the buffered commands. Blocks only while the FIFO is full. */
eve_status_t eve_cmd_flush(void);

/* Flush, then wait for the co-processor to execute everything. */
eve_status_t eve_cmd_wait_idle(uint32_t timeout_ms);

/* True when the co-processor has executed everything sent so far. */
bool eve_cmd_is_idle(void);

/* Reset the co-processor after EVE_ERR_COPRO_FAULT. Buffered commands are dropped. */
void eve_cmd_recover(void);

/* ---------------------------------------------------------------------------
 * Co-processor widgets
 * ------------------------------------------------------------------------ */

void eve_cmd_dlstart(void);
void eve_cmd_swap(void);
void eve_cmd_fgcolor(uint32_t rgb);
void eve_cmd_bgcolor(uint32_t rgb);
void eve_cmd_gradcolor(uint32_t rgb);
void eve_cmd_gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1);
void eve_cmd_text(int16_t x, int16_t y, int16_t font, uint16_t options, const char *s);
void eve_cmd_number(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n);
void eve_cmd_progress(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t options, uint16_t val,
                      uint16_t range);
void eve_cmd_gauge(int16_t x, int16_t y, int16_t r, uint16_t options, uint16_t major, uint16_t minor,
                   uint16_t val, uint16_t range);
void eve_cmd_button(int16_t x, int16_t y, int16_t w, int16_t h, int16_t font, uint16_t options,
                    const char *s);

/*
 * Attach ROM font `romslot` (16..34) to bitmap handle `font` (0..14). This
 * emits display list commands, so call it in every display list that
 * uses the font. Fonts 32..34 (the large ones) are only reachable this way.
 */
void eve_cmd_romfont(uint32_t font, uint32_t romslot);

/* Convenience: COLOR_RGB from 0xRRGGBB. */
static inline void eve_color(uint32_t rgb)
{
    eve_cmd(COLOR_RGB((rgb >> 16) & 0xFFu, (rgb >> 8) & 0xFFu, rgb & 0xFFu));
}

#endif /* EVE_H */
