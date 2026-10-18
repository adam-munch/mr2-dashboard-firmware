/*
 * CAN driver stand-in for host tests: frames injected with
 * mock_can_inject() come out of can_receive(), frames passed to can_send()
 * are captured, and the bus state is whatever the test sets.
 */
#ifndef MOCK_CAN_H
#define MOCK_CAN_H

#include <stdbool.h>
#include <stddef.h>

#include "can.h"

#define MOCK_CAN_MAX_FRAMES 512

typedef struct {
    /* Captured from can_init() */
    bool init_called;
    uint32_t bitrate;
    can_filter_t filters[CAN_MAX_FILTERS];
    size_t n_filters;

    /* Behaviour knobs */
    bool init_ok;      /* can_init() result (default true) */
    bool tx_full;      /* can_send() fails (default false) */
    can_state_t state; /* reported by can_get_stats() (default ACTIVE) */

    can_frame_t rx[MOCK_CAN_MAX_FRAMES];
    size_t rx_len;
    size_t rx_pos;

    can_frame_t tx[MOCK_CAN_MAX_FRAMES];
    size_t tx_len;
    uint32_t tx_dropped;
} mock_can_t;

extern mock_can_t g_can;

void mock_can_reset(void);

/* Queue a frame for can_receive(). */
void mock_can_inject(const can_frame_t *frame);

#endif /* MOCK_CAN_H */
