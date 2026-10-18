#include "ecu_link.h"

#include <string.h>

#include "can.h"

#define RX_MAX_PER_POLL     128u /* bounds one poll even if the bus floods us */
#define TX_MIN_GAP_MS       10u  /* between a periodic frame and an on-change one */
#define RPM_SMOOTH_MAX_DT   100u /* don't over-step after a stall */

static rusefi_data_t s_ecu;
static bool s_can_ok;

/* Dashboard -> ECU */
static bool s_display_ok;
static uint16_t s_inputs;
static uint8_t s_mode;
static uint8_t s_tx_counter;
static bool s_tx_due;
static uint32_t s_last_tx_ms;

/* Tach smoothing */
static bool s_rpm_shown_valid;
static int32_t s_rpm_shown_x16; /* rpm * 16, so slow approaches still move */
static uint32_t s_last_dash_ms;

bool ecu_link_init(uint32_t now_ms)
{
    rusefi_init(&s_ecu);
    s_display_ok = false;
    s_inputs = 0u;
    s_mode = 0u;
    s_tx_counter = 0u;
    s_tx_due = true; /* announce ourselves straight away */
    s_last_tx_ms = now_ms - DASH_TX_PERIOD_MS;
    s_rpm_shown_valid = false;
    s_last_dash_ms = now_ms;

    const can_filter_t filter = {
        .first = ECU_RUSEFI_BASE_ID,
        .last = ECU_RUSEFI_BASE_ID + RUSEFI_MSG_COUNT - 1u,
        .ext = ECU_RUSEFI_EXT_ID != 0,
    };
    s_can_ok = can_init(ECU_CAN_BITRATE, &filter, 1u);
    return s_can_ok;
}

dash_link_t ecu_link_state(uint32_t now_ms)
{
    if (!s_can_ok) {
        return DASH_LINK_CAN_FAULT;
    }
    can_stats_t st;
    can_get_stats(&st);
    if (st.state == CAN_STATE_STOPPED || st.state == CAN_STATE_BUS_OFF) {
        return DASH_LINK_CAN_FAULT;
    }
    if (s_ecu.seen == 0u) {
        return DASH_LINK_WAITING;
    }
    if ((int32_t)(now_ms - s_ecu.last_rx_ms) >= (int32_t)ECU_DATA_TIMEOUT_MS) {
        return DASH_LINK_LOST;
    }
    return DASH_LINK_OK;
}

static bool send_status(uint32_t now_ms)
{
    can_frame_t f = {
        .id = DASH_TX_ID,
        .ext = DASH_TX_EXT_ID != 0,
        .dlc = DASH_TX_LEN,
    };
    uint8_t flags = 0u;
    if (s_display_ok) {
        flags |= DASH_STATUS_DISPLAY_OK;
    }
    if (ecu_link_state(now_ms) == DASH_LINK_OK) {
        flags |= DASH_STATUS_ECU_DATA_OK;
    }
    f.data[0] = s_tx_counter;
    f.data[1] = flags;
    f.data[2] = (uint8_t)s_inputs;
    f.data[3] = (uint8_t)(s_inputs >> 8);
    f.data[4] = s_mode;

    if (!can_send(&f)) {
        return false;
    }
    s_tx_counter++;
    return true;
}

void ecu_link_poll(uint32_t now_ms)
{
    can_frame_t f;
    for (uint32_t n = 0u; n < RX_MAX_PER_POLL && can_receive(&f); n++) {
        (void)rusefi_decode(&s_ecu, ECU_RUSEFI_BASE_ID, ECU_RUSEFI_EXT_ID != 0, &f);
    }

    can_poll(now_ms);

    if (DASH_TX_ENABLE && s_can_ok) {
        const uint32_t since = now_ms - s_last_tx_ms;
        if (since >= DASH_TX_PERIOD_MS || (s_tx_due && since >= TX_MIN_GAP_MS)) {
            /* The frame carries state, not events: if it can't be queued
             * (nobody acknowledging, FIFO full), the next one carries the
             * same news. */
            s_last_tx_ms = now_ms;
            s_tx_due = false;
            (void)send_status(now_ms);
        }
    }
}

static uint16_t smooth_rpm(uint16_t rpm, uint32_t now_ms)
{
    const int32_t target = (int32_t)rpm * 16;
    uint32_t dt = now_ms - s_last_dash_ms;
    if (dt > RPM_SMOOTH_MAX_DT) {
        dt = RPM_SMOOTH_MAX_DT;
    }

    if (!s_rpm_shown_valid || ECU_RPM_SMOOTH_MS == 0u) {
        s_rpm_shown_x16 = target;
        s_rpm_shown_valid = true;
    } else {
        /* First-order low-pass: move dt / (tau + dt) of the way there. */
        s_rpm_shown_x16 += (target - s_rpm_shown_x16) * (int32_t)dt / (int32_t)(ECU_RPM_SMOOTH_MS + dt);
    }
    return (uint16_t)((s_rpm_shown_x16 + 8) / 16);
}

void ecu_link_get_dash(dash_data_t *d, uint32_t now_ms)
{
    const rusefi_data_t *e = &s_ecu;
    memset(d, 0, sizeof *d);
    d->link = ecu_link_state(now_ms);

    if (rusefi_fresh(e, RUSEFI_MSG_SPEEDS, now_ms, ECU_DATA_TIMEOUT_MS)) {
        d->rpm = smooth_rpm(e->rpm, now_ms);
        d->speed_kph = e->speed_kph;
        d->valid |= DASH_VALID_RPM | DASH_VALID_SPEED;
    } else {
        s_rpm_shown_valid = false;
    }
    s_last_dash_ms = now_ms;

    if (rusefi_fresh(e, RUSEFI_MSG_SENSORS1, now_ms, ECU_DATA_TIMEOUT_MS)) {
        d->coolant_c = e->coolant_c;
        const uint32_t fuel = (e->fuel_level_pct_x2 + 1u) / 2u;
        d->fuel_pct = (uint8_t)(fuel > 100u ? 100u : fuel);
        d->valid |= DASH_VALID_COOLANT | DASH_VALID_FUEL;
    }

    if (rusefi_fresh(e, RUSEFI_MSG_SENSORS2, now_ms, ECU_DATA_TIMEOUT_MS)) {
        d->battery_mv = e->battery_mv;
        d->valid |= DASH_VALID_BATTERY;
    }

    if (rusefi_fresh(e, RUSEFI_MSG_STATUS, now_ms, ECU_DATA_TIMEOUT_MS)) {
        if (e->gear <= 9u) {
            d->gear = e->gear;
            d->valid |= DASH_VALID_GEAR;
        }
        if ((e->flags & RUSEFI_FLAG_CHECK_ENGINE) != 0u) {
            d->warnings |= DASH_WARN_CHECK_ENGINE;
        }
        if ((e->flags & RUSEFI_FLAG_FUEL_SPARK_CUT) != 0u) {
            d->warnings |= DASH_WARN_LIMITER;
        }
    }
}

const rusefi_data_t *ecu_link_data(void)
{
    return &s_ecu;
}

void ecu_link_set_display_ok(bool ok)
{
    if (ok != s_display_ok) {
        s_display_ok = ok;
        s_tx_due = true;
    }
}

void ecu_link_set_inputs(uint16_t inputs)
{
    if (inputs != s_inputs) {
        s_inputs = inputs;
        s_tx_due = true;
    }
}

void ecu_link_set_mode(uint8_t mode)
{
    if (mode != s_mode) {
        s_mode = mode;
        s_tx_due = true;
    }
}
