#include "mock_can.h"

#include <string.h>

mock_can_t g_can;

void mock_can_reset(void)
{
    memset(&g_can, 0, sizeof g_can);
    g_can.init_ok = true;
    g_can.state = CAN_STATE_ACTIVE;
}

void mock_can_inject(const can_frame_t *frame)
{
    if (g_can.rx_len < MOCK_CAN_MAX_FRAMES) {
        g_can.rx[g_can.rx_len++] = *frame;
    }
}

bool can_init(uint32_t bitrate, const can_filter_t *filters, size_t n_filters)
{
    g_can.init_called = true;
    g_can.bitrate = bitrate;
    g_can.n_filters = n_filters < CAN_MAX_FILTERS ? n_filters : CAN_MAX_FILTERS;
    memcpy(g_can.filters, filters, g_can.n_filters * sizeof *filters);
    return g_can.init_ok;
}

bool can_receive(can_frame_t *frame)
{
    if (g_can.rx_pos >= g_can.rx_len) {
        return false;
    }
    *frame = g_can.rx[g_can.rx_pos++];
    return true;
}

bool can_send(const can_frame_t *frame)
{
    if (g_can.tx_full || g_can.tx_len >= MOCK_CAN_MAX_FRAMES) {
        g_can.tx_dropped++;
        return false;
    }
    g_can.tx[g_can.tx_len++] = *frame;
    return true;
}

void can_poll(uint32_t now_ms)
{
    (void)now_ms;
}

void can_get_stats(can_stats_t *stats)
{
    memset(stats, 0, sizeof *stats);
    stats->state = g_can.state;
    stats->tx_frames = (uint32_t)g_can.tx_len;
    stats->tx_dropped = g_can.tx_dropped;
}
