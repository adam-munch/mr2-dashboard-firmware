#include "mock_encoder.h"

#include <string.h>

mock_encoder_t g_enc;

void mock_encoder_reset(void)
{
    memset(&g_enc, 0, sizeof g_enc);
}

void mock_encoder_turn(int32_t clicks)
{
    g_enc.count = (uint16_t)(g_enc.count + clicks * ENCODER_COUNTS_PER_DETENT);
}

void encoder_hw_init(void)
{
    g_enc.init_called = true;
}

uint16_t encoder_hw_count(void)
{
    return g_enc.count;
}

bool encoder_hw_switch(void)
{
    return g_enc.pressed;
}
