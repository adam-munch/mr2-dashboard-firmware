/*
 * Simulated FT81x behind the eve_hal interface, for host tests.
 *
 * Decodes SPI frames the way the chip does (host commands, memory reads
 * and writes), keeps a 4 MB memory image, and captures everything
 * written to REG_CMDB_WRITE as the co-processor command stream.
 */
#ifndef MOCK_EVE_H
#define MOCK_EVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MOCK_MAX_FRAMES 4096
#define MOCK_FIFO_SIZE  (64 * 1024)

typedef enum {
    FRAME_HOST_CMD,
    FRAME_READ,
    FRAME_WRITE,
} mock_frame_kind_t;

typedef struct {
    mock_frame_kind_t kind;
    uint32_t addr;     /* memory frames */
    uint8_t hdr[4];    /* first bytes on the wire */
    size_t data_len;   /* payload bytes (excluding header) */
    uint32_t time_ms;  /* when CS was asserted */
    bool spi_fast;     /* bus speed during the frame */
} mock_frame_t;

typedef struct {
    uint8_t mem[0x400000];

    mock_frame_t frames[MOCK_MAX_FRAMES];
    size_t frame_count;

    uint8_t fifo[MOCK_FIFO_SIZE]; /* bytes written to REG_CMDB_WRITE */
    size_t fifo_len;

    /* Behaviour knobs */
    bool reg_id_ok;            /* REG_ID reads 0x7C once ACTIVE was sent */
    uint16_t cmdb_space;       /* REG_CMDB_SPACE value (default 4092 = idle) */
    const uint16_t *space_seq; /* if set, consumed one value per REG_CMDB_SPACE read */
    size_t space_seq_len;

    /* Observed state */
    bool active_sent;
    bool spi_fast;
    bool pd_asserted;
    uint32_t pd_assert_time;
    uint32_t pd_release_time;
    uint32_t now_ms;
} mock_eve_t;

extern mock_eve_t g_mock;

/* Reset the simulation to power-on defaults. */
void mock_eve_reset(void);

/* Index of the first write frame to `addr`, or -1. */
int mock_find_write(uint32_t addr);

/* Read little-endian values from the memory image. */
uint32_t mock_mem32(uint32_t addr);
uint16_t mock_mem16(uint32_t addr);

/* Captured command stream as 32-bit words. */
uint32_t mock_fifo_word(size_t index);
size_t mock_fifo_words(void);

#endif /* MOCK_EVE_H */
