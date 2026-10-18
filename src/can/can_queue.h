/*
 * Single-producer, single-consumer queue of CAN frames: the receive
 * interrupt pushes, the main loop pops. No locking is needed because each
 * index is written by one side only and is a single aligned word.
 */
#ifndef CAN_QUEUE_H
#define CAN_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "can.h"

#define CAN_QUEUE_LEN 64u /* power of two; ~250 ms of a 12-frame broadcast every 50 ms */

typedef struct {
    can_frame_t frames[CAN_QUEUE_LEN];
    volatile uint32_t head; /* frames pushed, written by the producer only */
    volatile uint32_t tail; /* frames popped, written by the consumer only */
} can_queue_t;

static inline void can_queue_init(can_queue_t *q)
{
    q->head = 0u;
    q->tail = 0u;
}

/* Returns false (and drops the frame) if the queue is full. */
static inline bool can_queue_push(can_queue_t *q, const can_frame_t *frame)
{
    const uint32_t head = q->head;
    if (head - q->tail >= CAN_QUEUE_LEN) {
        return false;
    }
    q->frames[head % CAN_QUEUE_LEN] = *frame;
    atomic_signal_fence(memory_order_release); /* frame stored before it is published */
    q->head = head + 1u;
    return true;
}

static inline bool can_queue_pop(can_queue_t *q, can_frame_t *frame)
{
    const uint32_t tail = q->tail;
    if (q->head == tail) {
        return false;
    }
    atomic_signal_fence(memory_order_acquire);
    *frame = q->frames[tail % CAN_QUEUE_LEN];
    atomic_signal_fence(memory_order_release); /* frame copied out before the slot is freed */
    q->tail = tail + 1u;
    return true;
}

#endif /* CAN_QUEUE_H */
