/*
 * Encoder stand-in for host tests: the count and switch level are whatever
 * the test sets.
 */
#ifndef MOCK_ENCODER_H
#define MOCK_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#include "encoder.h"

typedef struct {
    bool init_called;
    uint16_t count;
    bool pressed;
} mock_encoder_t;

extern mock_encoder_t g_enc;

void mock_encoder_reset(void);

/* Turn by `clicks` whole detents (negative: anticlockwise). */
void mock_encoder_turn(int32_t clicks);

#endif /* MOCK_ENCODER_H */
