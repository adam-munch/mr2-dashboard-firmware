#include "eve.h"

#include "eve_hal.h"

/* Local command buffer. Must be a multiple of 4; 4 KB matches the FIFO. */
#define EVE_CMD_BUF_SIZE 4096u

/* REG_CMDB_SPACE reads this when the FIFO is empty (co-processor idle). */
#define EVE_CMDB_SPACE_EMPTY (RAM_CMD_SIZE - 4u)

/* Give up if the co-processor makes no progress for this long. */
#define EVE_FIFO_TIMEOUT_MS 200u

static uint8_t s_cmd_buf[EVE_CMD_BUF_SIZE];
static size_t s_cmd_len;
static eve_status_t s_deferred_error; /* from an automatic flush inside eve_cmd() */
static uint32_t s_chip_id;

/* ---------------------------------------------------------------------------
 * SPI framing
 *
 * Every transaction starts with a 22-bit address. Bits 23:22 of the first
 * three bytes are 10 for a write and 00 for a read; reads are followed by
 * one dummy byte before the data. Data is little-endian.
 * ------------------------------------------------------------------------ */

static void begin_write(uint32_t addr)
{
    const uint8_t hdr[3] = {
        (uint8_t)(0x80u | ((addr >> 16) & 0x3Fu)),
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
    };
    eve_hal_cs_assert();
    eve_hal_spi_write(hdr, sizeof hdr);
}

static void begin_read(uint32_t addr)
{
    const uint8_t hdr[4] = {
        (uint8_t)((addr >> 16) & 0x3Fu),
        (uint8_t)(addr >> 8),
        (uint8_t)addr,
        0x00u, /* dummy */
    };
    eve_hal_cs_assert();
    eve_hal_spi_write(hdr, sizeof hdr);
}

void eve_host_cmd(uint8_t cmd, uint8_t param)
{
    const uint8_t frame[3] = {cmd, param, 0x00u};
    eve_hal_cs_assert();
    eve_hal_spi_write(frame, sizeof frame);
    eve_hal_cs_release();
}

void eve_rd_mem(uint32_t addr, uint8_t *dst, size_t len)
{
    begin_read(addr);
    eve_hal_spi_read(dst, len);
    eve_hal_cs_release();
}

uint8_t eve_rd8(uint32_t addr)
{
    uint8_t b;
    eve_rd_mem(addr, &b, 1u);
    return b;
}

uint16_t eve_rd16(uint32_t addr)
{
    uint8_t b[2];
    eve_rd_mem(addr, b, sizeof b);
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

uint32_t eve_rd32(uint32_t addr)
{
    uint8_t b[4];
    eve_rd_mem(addr, b, sizeof b);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

void eve_wr_mem(uint32_t addr, const uint8_t *src, size_t len)
{
    begin_write(addr);
    eve_hal_spi_write(src, len);
    eve_hal_cs_release();
}

void eve_wr8(uint32_t addr, uint8_t value)
{
    eve_wr_mem(addr, &value, 1u);
}

void eve_wr16(uint32_t addr, uint16_t value)
{
    const uint8_t b[2] = {(uint8_t)value, (uint8_t)(value >> 8)};
    eve_wr_mem(addr, b, sizeof b);
}

void eve_wr32(uint32_t addr, uint32_t value)
{
    const uint8_t b[4] = {(uint8_t)value, (uint8_t)(value >> 8), (uint8_t)(value >> 16),
                          (uint8_t)(value >> 24)};
    eve_wr_mem(addr, b, sizeof b);
}

/* ---------------------------------------------------------------------------
 * Bring-up
 * ------------------------------------------------------------------------ */

static bool poll_rd8(uint32_t addr, uint8_t mask, uint8_t want, uint32_t timeout_ms)
{
    const uint32_t start = eve_hal_millis();
    for (;;) {
        if ((eve_rd8(addr) & mask) == want) {
            return true;
        }
        if ((uint32_t)(eve_hal_millis() - start) >= timeout_ms) {
            return false;
        }
        eve_hal_delay_ms(1u);
    }
}

eve_status_t eve_init(const eve_panel_t *panel)
{
    s_cmd_len = 0u;
    s_deferred_error = EVE_OK;
    s_chip_id = 0u;

    eve_hal_init();
    eve_hal_spi_fast(false); /* <= 11 MHz until the EVE's PLL is running */

    /* Hard reset: /PD low for >= 5 ms, then >= 20 ms before the first access. */
    eve_hal_power_down(true);
    eve_hal_delay_ms(6u);
    eve_hal_power_down(false);
    eve_hal_delay_ms(21u);

    eve_host_cmd(panel->ext_clock ? HCMD_CLKEXT : HCMD_CLKINT, 0u);
    eve_host_cmd(HCMD_ACTIVE, 0u);
    eve_hal_delay_ms(60u); /* stay off the bus while the chip boots */

    /* REG_ID reads 0x7C once the chip has booted (typically < 300 ms). */
    if (!poll_rd8(REG_ID, 0xFFu, EVE_REG_ID_VALUE, 600u)) {
        return EVE_ERR_NO_RESPONSE;
    }
    /* Wait for the co-processor, touch and audio engines to leave reset. */
    if (!poll_rd8(REG_CPURESET, 0x07u, 0x00u, 100u)) {
        return EVE_ERR_RESET_TIMEOUT;
    }

    /* Only valid now: the co-processor may later overwrite this RAM_G location. */
    s_chip_id = eve_rd32(ROM_CHIPID);

    eve_hal_spi_fast(true);

    eve_wr8(REG_PWM_DUTY, 0u); /* backlight off while we set up */

    eve_wr16(REG_HCYCLE, panel->hcycle);
    eve_wr16(REG_HOFFSET, panel->hoffset);
    eve_wr16(REG_HSYNC0, panel->hsync0);
    eve_wr16(REG_HSYNC1, panel->hsync1);
    eve_wr16(REG_VCYCLE, panel->vcycle);
    eve_wr16(REG_VOFFSET, panel->voffset);
    eve_wr16(REG_VSYNC0, panel->vsync0);
    eve_wr16(REG_VSYNC1, panel->vsync1);
    eve_wr8(REG_SWIZZLE, panel->swizzle);
    eve_wr8(REG_PCLK_POL, panel->pclk_pol);
    eve_wr8(REG_CSPREAD, panel->cspread);
    eve_wr8(REG_DITHER, panel->dither);
    eve_wr16(REG_HSIZE, panel->hsize);
    eve_wr16(REG_VSIZE, panel->vsize);

    if (!panel->has_touch) {
        eve_wr8(REG_TOUCH_MODE, TOUCHMODE_OFF);
    }

    /* Mute audio (reset default volume is 0xFF). */
    eve_wr8(REG_VOL_PB, 0u);
    eve_wr8(REG_VOL_SOUND, 0u);

    /* Black first frame. */
    eve_wr32(RAM_DL + 0u, CLEAR_COLOR_RGB(0, 0, 0));
    eve_wr32(RAM_DL + 4u, CLEAR(1, 1, 1));
    eve_wr32(RAM_DL + 8u, DISPLAY());
    eve_wr8(REG_DLSWAP, DLSWAP_FRAME);

    /* Enable the panel (DISP), then start the pixel clock last. */
    eve_wr8(REG_GPIO_DIR, (uint8_t)(eve_rd8(REG_GPIO_DIR) | EVE_GPIO_DISP));
    eve_wr8(REG_GPIO, (uint8_t)(eve_rd8(REG_GPIO) | EVE_GPIO_DISP));
    eve_wr8(REG_PCLK, panel->pclk);

    eve_wr16(REG_PWM_HZ, panel->backlight_pwm_hz);

    return eve_cmd_wait_idle(EVE_FIFO_TIMEOUT_MS);
}

uint32_t eve_chip_id(void)
{
    return s_chip_id;
}

void eve_backlight_set(uint8_t duty)
{
    eve_wr8(REG_PWM_DUTY, duty > EVE_PWM_DUTY_MAX ? EVE_PWM_DUTY_MAX : duty);
}

const char *eve_status_str(eve_status_t status)
{
    switch (status) {
    case EVE_OK:
        return "ok";
    case EVE_ERR_NO_RESPONSE:
        return "no response (REG_ID != 0x7C)";
    case EVE_ERR_RESET_TIMEOUT:
        return "engines stuck in reset";
    case EVE_ERR_COPRO_FAULT:
        return "co-processor fault";
    case EVE_ERR_TIMEOUT:
        return "co-processor timeout";
    }
    return "unknown";
}

/* ---------------------------------------------------------------------------
 * Co-processor command FIFO
 *
 * Commands are written through REG_CMDB_WRITE: the FT81x appends whatever
 * is written there to RAM_CMD and handles the ring-buffer wrap itself.
 * REG_CMDB_SPACE says how many bytes are free; its low two bits are set
 * after a co-processor fault.
 * ------------------------------------------------------------------------ */

static eve_status_t read_fifo_space(uint16_t *space)
{
    const uint16_t s = eve_rd16(REG_CMDB_SPACE) & 0x0FFFu;
    if ((s & 3u) != 0u) {
        return EVE_ERR_COPRO_FAULT;
    }
    *space = s;
    return EVE_OK;
}

static eve_status_t send_buffer(void)
{
    size_t sent = 0u;
    uint32_t last_progress = eve_hal_millis();

    while (sent < s_cmd_len) {
        uint16_t space;
        const eve_status_t st = read_fifo_space(&space);
        if (st != EVE_OK) {
            return st;
        }
        if (space == 0u) {
            if ((uint32_t)(eve_hal_millis() - last_progress) > EVE_FIFO_TIMEOUT_MS) {
                return EVE_ERR_TIMEOUT;
            }
            continue;
        }

        size_t chunk = s_cmd_len - sent;
        if (chunk > space) {
            chunk = space;
        }
        eve_wr_mem(REG_CMDB_WRITE, &s_cmd_buf[sent], chunk);
        sent += chunk;
        last_progress = eve_hal_millis();
    }
    return EVE_OK;
}

void eve_cmd(uint32_t word)
{
    if (s_cmd_len + 4u > EVE_CMD_BUF_SIZE) {
        const eve_status_t st = send_buffer();
        s_cmd_len = 0u;
        if (st != EVE_OK && s_deferred_error == EVE_OK) {
            s_deferred_error = st;
        }
    }
    s_cmd_buf[s_cmd_len + 0u] = (uint8_t)word;
    s_cmd_buf[s_cmd_len + 1u] = (uint8_t)(word >> 8);
    s_cmd_buf[s_cmd_len + 2u] = (uint8_t)(word >> 16);
    s_cmd_buf[s_cmd_len + 3u] = (uint8_t)(word >> 24);
    s_cmd_len += 4u;
}

void eve_cmd_str(const char *s)
{
    /* Pack 4 characters per word; the word holding the NUL ends the string. */
    for (;;) {
        uint32_t word = 0u;
        for (uint32_t i = 0u; i < 4u; i++) {
            const uint8_t c = (uint8_t)*s;
            if (c == 0u) {
                eve_cmd(word);
                return;
            }
            word |= (uint32_t)c << (8u * i);
            s++;
        }
        eve_cmd(word);
    }
}

eve_status_t eve_cmd_flush(void)
{
    eve_status_t st = s_deferred_error;
    s_deferred_error = EVE_OK;
    if (st == EVE_OK) {
        st = send_buffer();
    }
    s_cmd_len = 0u;
    return st;
}

eve_status_t eve_cmd_wait_idle(uint32_t timeout_ms)
{
    eve_status_t st = eve_cmd_flush();
    if (st != EVE_OK) {
        return st;
    }

    const uint32_t start = eve_hal_millis();
    for (;;) {
        uint16_t space;
        st = read_fifo_space(&space);
        if (st != EVE_OK) {
            return st;
        }
        if (space == EVE_CMDB_SPACE_EMPTY) {
            return EVE_OK;
        }
        if ((uint32_t)(eve_hal_millis() - start) > timeout_ms) {
            return EVE_ERR_TIMEOUT;
        }
    }
}

bool eve_cmd_is_idle(void)
{
    uint16_t space;
    return read_fifo_space(&space) == EVE_OK && space == EVE_CMDB_SPACE_EMPTY;
}

void eve_cmd_recover(void)
{
    s_cmd_len = 0u;
    s_deferred_error = EVE_OK;

    eve_wr8(REG_CPURESET, 1u); /* hold the co-processor in reset */
    eve_wr16(REG_CMD_READ, 0u);
    eve_wr16(REG_CMD_WRITE, 0u);
    eve_wr16(REG_CMD_DL, 0u);
    eve_wr8(REG_CPURESET, 0u);
    eve_hal_delay_ms(10u);
}

/* ---------------------------------------------------------------------------
 * Co-processor widgets
 * ------------------------------------------------------------------------ */

/* Two 16-bit arguments share one word, first argument in the low half. */
static inline uint32_t pack16(int32_t lo, int32_t hi)
{
    return ((uint32_t)lo & 0xFFFFu) | (((uint32_t)hi & 0xFFFFu) << 16);
}

void eve_cmd_dlstart(void)
{
    eve_cmd(CMD_DLSTART);
}

void eve_cmd_swap(void)
{
    eve_cmd(CMD_SWAP);
}

void eve_cmd_fgcolor(uint32_t rgb)
{
    eve_cmd(CMD_FGCOLOR);
    eve_cmd(rgb & 0xFFFFFFu);
}

void eve_cmd_bgcolor(uint32_t rgb)
{
    eve_cmd(CMD_BGCOLOR);
    eve_cmd(rgb & 0xFFFFFFu);
}

void eve_cmd_gradcolor(uint32_t rgb)
{
    eve_cmd(CMD_GRADCOLOR);
    eve_cmd(rgb & 0xFFFFFFu);
}

void eve_cmd_gradient(int16_t x0, int16_t y0, uint32_t rgb0, int16_t x1, int16_t y1, uint32_t rgb1)
{
    eve_cmd(CMD_GRADIENT);
    eve_cmd(pack16(x0, y0));
    eve_cmd(rgb0 & 0xFFFFFFu);
    eve_cmd(pack16(x1, y1));
    eve_cmd(rgb1 & 0xFFFFFFu);
}

void eve_cmd_text(int16_t x, int16_t y, int16_t font, uint16_t options, const char *s)
{
    eve_cmd(CMD_TEXT);
    eve_cmd(pack16(x, y));
    eve_cmd(pack16(font, options));
    eve_cmd_str(s);
}

void eve_cmd_number(int16_t x, int16_t y, int16_t font, uint16_t options, int32_t n)
{
    eve_cmd(CMD_NUMBER);
    eve_cmd(pack16(x, y));
    eve_cmd(pack16(font, options));
    eve_cmd((uint32_t)n);
}

void eve_cmd_progress(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t options, uint16_t val,
                      uint16_t range)
{
    eve_cmd(CMD_PROGRESS);
    eve_cmd(pack16(x, y));
    eve_cmd(pack16(w, h));
    eve_cmd(pack16(options, val));
    eve_cmd(pack16(range, 0));
}

void eve_cmd_gauge(int16_t x, int16_t y, int16_t r, uint16_t options, uint16_t major, uint16_t minor,
                   uint16_t val, uint16_t range)
{
    eve_cmd(CMD_GAUGE);
    eve_cmd(pack16(x, y));
    eve_cmd(pack16(r, options));
    eve_cmd(pack16(major, minor));
    eve_cmd(pack16(val, range));
}

void eve_cmd_button(int16_t x, int16_t y, int16_t w, int16_t h, int16_t font, uint16_t options,
                    const char *s)
{
    eve_cmd(CMD_BUTTON);
    eve_cmd(pack16(x, y));
    eve_cmd(pack16(w, h));
    eve_cmd(pack16(font, options));
    eve_cmd_str(s);
}

void eve_cmd_romfont(uint32_t font, uint32_t romslot)
{
    eve_cmd(CMD_ROMFONT);
    eve_cmd(font);
    eve_cmd(romslot);
}
