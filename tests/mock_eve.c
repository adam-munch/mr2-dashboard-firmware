#include "mock_eve.h"

#include <string.h>

#include "eve_hal.h"
#include "ft81x.h"

mock_eve_t g_mock;

static bool s_cs;
static uint8_t s_hdr[4];
static size_t s_hdr_len;
static size_t s_data_len;
static mock_frame_t *s_cur;

void mock_eve_reset(void)
{
    memset(&g_mock, 0, sizeof g_mock);
    g_mock.reg_id_ok = true;
    g_mock.cmdb_space = 4092u;
    /* Chip ID of an FT812: 08 12 01 00. */
    g_mock.mem[ROM_CHIPID + 0] = 0x08;
    g_mock.mem[ROM_CHIPID + 1] = 0x12;
    g_mock.mem[ROM_CHIPID + 2] = 0x01;
    g_mock.mem[ROM_CHIPID + 3] = 0x00;
    /* REG_GPIO_DIR reset value: DISP (bit 7) is an output. */
    g_mock.mem[REG_GPIO_DIR] = 0x80;
    s_cs = false;
}

int mock_find_write(uint32_t addr)
{
    for (size_t i = 0; i < g_mock.frame_count; i++) {
        if (g_mock.frames[i].kind == FRAME_WRITE && g_mock.frames[i].addr == addr) {
            return (int)i;
        }
    }
    return -1;
}

uint32_t mock_mem32(uint32_t addr)
{
    const uint8_t *m = &g_mock.mem[addr];
    return (uint32_t)m[0] | ((uint32_t)m[1] << 8) | ((uint32_t)m[2] << 16) | ((uint32_t)m[3] << 24);
}

uint16_t mock_mem16(uint32_t addr)
{
    return (uint16_t)(g_mock.mem[addr] | (g_mock.mem[addr + 1] << 8));
}

uint32_t mock_fifo_word(size_t index)
{
    const uint8_t *b = &g_mock.fifo[index * 4];
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

size_t mock_fifo_words(void)
{
    return g_mock.fifo_len / 4;
}

static uint32_t hdr_addr(void)
{
    return ((uint32_t)(s_hdr[0] & 0x3Fu) << 16) | ((uint32_t)s_hdr[1] << 8) | s_hdr[2];
}

static uint8_t read_byte(uint32_t addr)
{
    /* Registers with simulated behaviour. */
    if (addr == REG_ID) {
        return (g_mock.reg_id_ok && g_mock.active_sent) ? 0x7C : 0x00;
    }
    if (addr == REG_CMDB_SPACE || addr == REG_CMDB_SPACE + 1u) {
        uint16_t space = g_mock.cmdb_space;
        if (g_mock.space_seq_len > 0) {
            space = g_mock.space_seq[0];
            if (addr == REG_CMDB_SPACE + 1u) { /* consume after the high byte */
                g_mock.space_seq++;
                g_mock.space_seq_len--;
            }
        }
        return (addr == REG_CMDB_SPACE) ? (uint8_t)space : (uint8_t)(space >> 8);
    }
    return g_mock.mem[addr];
}

/* ---------------------------------------------------------------------------
 * eve_hal implementation
 * ------------------------------------------------------------------------ */

void eve_hal_init(void)
{
    g_mock.spi_fast = false;
}

void eve_hal_spi_fast(bool fast)
{
    g_mock.spi_fast = fast;
}

void eve_hal_cs_assert(void)
{
    s_cs = true;
    s_hdr_len = 0;
    s_data_len = 0;
    s_cur = &g_mock.frames[g_mock.frame_count < MOCK_MAX_FRAMES ? g_mock.frame_count : MOCK_MAX_FRAMES - 1];
    memset(s_cur, 0, sizeof *s_cur);
    s_cur->time_ms = g_mock.now_ms;
    s_cur->spi_fast = g_mock.spi_fast;
}

void eve_hal_cs_release(void)
{
    s_cs = false;
    memcpy(s_cur->hdr, s_hdr, sizeof s_hdr);
    s_cur->data_len = s_data_len;

    if (s_hdr_len == 3 && s_data_len == 0 && (s_hdr[0] & 0xC0u) != 0x80u) {
        s_cur->kind = FRAME_HOST_CMD;
        if (s_hdr[0] == HCMD_ACTIVE) {
            g_mock.active_sent = true;
        }
    } else if ((s_hdr[0] & 0xC0u) == 0x80u) {
        s_cur->kind = FRAME_WRITE;
        s_cur->addr = hdr_addr();
    } else {
        s_cur->kind = FRAME_READ;
        s_cur->addr = hdr_addr();
    }
    if (g_mock.frame_count < MOCK_MAX_FRAMES) {
        g_mock.frame_count++;
    }
}

void eve_hal_power_down(bool asserted)
{
    if (asserted && !g_mock.pd_asserted) {
        g_mock.pd_assert_time = g_mock.now_ms;
        g_mock.active_sent = false;
    } else if (!asserted && g_mock.pd_asserted) {
        g_mock.pd_release_time = g_mock.now_ms;
    }
    g_mock.pd_asserted = asserted;
}

void eve_hal_spi_write(const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        const uint8_t b = data[i];
        const bool is_write = s_hdr_len >= 1 && (s_hdr[0] & 0xC0u) == 0x80u;

        if (s_hdr_len < 3 || (!is_write && s_hdr_len < 4)) {
            s_hdr[s_hdr_len++] = b;
            continue;
        }
        if (is_write) {
            const uint32_t addr = hdr_addr();
            if (addr == REG_CMDB_WRITE) {
                if (g_mock.fifo_len < MOCK_FIFO_SIZE) {
                    g_mock.fifo[g_mock.fifo_len++] = b;
                }
            } else {
                g_mock.mem[(addr + s_data_len) & 0x3FFFFFu] = b;
            }
        }
        s_data_len++;
    }
}

void eve_hal_spi_read(uint8_t *data, size_t len)
{
    const uint32_t addr = hdr_addr();
    for (size_t i = 0; i < len; i++) {
        data[i] = read_byte((addr + s_data_len) & 0x3FFFFFu);
        s_data_len++;
    }
}

void eve_hal_delay_ms(uint32_t ms)
{
    g_mock.now_ms += ms;
}

uint32_t eve_hal_millis(void)
{
    /* Time moves on every call so polling loops always reach their timeout. */
    return g_mock.now_ms++;
}
