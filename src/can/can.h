/*
 * Classic CAN (2.0B) driver interface. can_stm32.c implements it with the
 * STM32G0B1's FDCAN (pins in board.h); the host tests swap in a mock.
 *
 * Received frames are queued by the interrupt handler, so the main loop
 * can be busy for a while (drawing a frame takes several ms) without
 * losing any.
 */
#ifndef CAN_H
#define CAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAN_MAX_DLC     8u
#define CAN_STD_ID_MAX  0x7FFu
#define CAN_EXT_ID_MAX  0x1FFFFFFFu
#define CAN_MAX_FILTERS 8u

typedef struct {
    uint32_t id;      /* 11-bit (ext = false) or 29-bit (ext = true) identifier */
    bool ext;
    uint8_t dlc;      /* 0..8 data bytes */
    uint8_t data[CAN_MAX_DLC];
    uint32_t time_ms; /* received frames: millis() when it arrived. Ignored for transmit. */
} can_frame_t;

/* Accept identifiers first..last (inclusive) of one identifier type. */
typedef struct {
    uint32_t first;
    uint32_t last;
    bool ext;
} can_filter_t;

typedef enum {
    CAN_STATE_STOPPED, /* not started, or failed to start */
    CAN_STATE_ACTIVE,  /* normal operation */
    CAN_STATE_WARNING, /* an error counter passed 96 */
    CAN_STATE_PASSIVE, /* an error counter passed 127, e.g. nobody acknowledges our frames */
    CAN_STATE_BUS_OFF, /* too many transmit errors: off the bus, recovering */
} can_state_t;

typedef struct {
    can_state_t state;
    uint8_t tx_errors;      /* transmit error counter (TEC) */
    uint8_t rx_errors;      /* receive error counter (REC) */
    uint32_t rx_frames;     /* received and queued */
    uint32_t rx_lost;       /* dropped because a receive FIFO or the queue was full */
    uint32_t tx_frames;     /* queued for transmission */
    uint32_t tx_dropped;    /* refused by can_send() */
    uint32_t bus_off_count; /* times the controller went bus-off */
} can_stats_t;

/*
 * Configure the controller and its pins and join the bus. Only frames that
 * match one of the filters are received (at most CAN_MAX_FILTERS). Returns
 * false if the bit rate can't be made from the CAN clock or the controller
 * doesn't respond.
 */
bool can_init(uint32_t bitrate, const can_filter_t *filters, size_t n_filters);

/* Take the oldest received frame. Returns false if there is none. */
bool can_receive(can_frame_t *frame);

/* Queue a frame for transmission. Returns false if it can't be queued:
 * transmit FIFO full, bus off, bad identifier or length. */
bool can_send(const can_frame_t *frame);

/* Housekeeping (bus-off recovery). Call regularly from the main loop. */
void can_poll(uint32_t now_ms);

void can_get_stats(can_stats_t *stats);

#endif /* CAN_H */
