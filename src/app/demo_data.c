#include "demo_data.h"

/*
 * A repeating drive cycle: idle, pull through the gears, coast down
 * through the gears, idle again. Gearing is roughly an SW20 MR2 (E153
 * gearbox, 4.285 final drive, 225/50R15 rear tyres).
 */

/* km/h per 1000 rpm, x10, per gear (index 0 = neutral). */
static const uint16_t k_kph_x10_per_krpm[6] = {0, 83, 139, 212, 290, 365};
/* Acceleration in each gear, rpm per second. */
static const uint16_t k_accel_rpm_s[6] = {0, 3500, 2200, 1500, 1000, 700};

#define IDLE_RPM         850u
#define IDLE_TIME_MS     2500u
#define UPSHIFT_RPM      6600u
#define TOP_GEAR_MAX_RPM 6000u
#define DOWNSHIFT_RPM    2000u
#define STOP_RPM         1100u /* in 1st: below this, clutch in and idle */
#define DECEL_RPM_S      1200u
#define SHIFT_RPM_S      12000u /* how fast rpm settles during a gear change */

typedef enum {
    PHASE_IDLE,
    PHASE_ACCEL,
    PHASE_DECEL,
    PHASE_SHIFT,
} phase_t;

static phase_t s_phase;
static phase_t s_after_shift;
static uint32_t s_rpm_x1000; /* rpm * 1000, so slow rates still accumulate */
static uint32_t s_shift_target_rpm;
static uint32_t s_speed_x10;
static uint32_t s_phase_start_ms;
static uint32_t s_last_ms;
static uint32_t s_temp_acc_ms;
static uint32_t s_fuel_acc_ms;

static uint32_t rpm_for_speed(uint32_t speed_x10, uint8_t gear)
{
    return speed_x10 * 1000u / k_kph_x10_per_krpm[gear];
}

static void start_shift(dash_data_t *d, uint8_t new_gear, phase_t after)
{
    d->gear = new_gear;
    s_shift_target_rpm = rpm_for_speed(s_speed_x10, new_gear);
    s_after_shift = after;
    s_phase = PHASE_SHIFT;
}

void demo_data_init(dash_data_t *d, uint32_t now_ms)
{
    d->rpm = IDLE_RPM;
    d->speed_kph = 0u;
    d->coolant_c = 25;
    d->fuel_pct = 72u;
    d->battery_mv = 13800u;
    d->gear = 0u;

    s_phase = PHASE_IDLE;
    s_rpm_x1000 = IDLE_RPM * 1000u;
    s_speed_x10 = 0u;
    s_phase_start_ms = now_ms;
    s_last_ms = now_ms;
    s_temp_acc_ms = 0u;
    s_fuel_acc_ms = 0u;
}

static void update_drivetrain(dash_data_t *d, uint32_t now_ms, uint32_t dt)
{
    const uint32_t rpm = s_rpm_x1000 / 1000u;

    switch (s_phase) {
    case PHASE_IDLE:
        s_rpm_x1000 = IDLE_RPM * 1000u + ((now_ms / 97u) % 30u) * 1000u; /* slight hunt */
        if ((uint32_t)(now_ms - s_phase_start_ms) >= IDLE_TIME_MS) {
            d->gear = 1u;
            s_rpm_x1000 = 1500u * 1000u;
            s_phase = PHASE_ACCEL;
        }
        break;

    case PHASE_ACCEL:
        s_rpm_x1000 += k_accel_rpm_s[d->gear] * dt;
        if (d->gear < 5u && rpm >= UPSHIFT_RPM) {
            start_shift(d, (uint8_t)(d->gear + 1u), PHASE_ACCEL);
        } else if (d->gear == 5u && rpm >= TOP_GEAR_MAX_RPM) {
            s_phase = PHASE_DECEL;
        }
        break;

    case PHASE_DECEL:
        s_rpm_x1000 -= DECEL_RPM_S * dt;
        if (d->gear > 1u && rpm < DOWNSHIFT_RPM) {
            start_shift(d, (uint8_t)(d->gear - 1u), PHASE_DECEL);
        } else if (d->gear == 1u && rpm < STOP_RPM) {
            /* Clutch in, rolled to a stop. */
            d->gear = 0u;
            s_rpm_x1000 = IDLE_RPM * 1000u;
            s_phase = PHASE_IDLE;
            s_phase_start_ms = now_ms;
        }
        break;

    case PHASE_SHIFT: {
        const uint32_t target = s_shift_target_rpm * 1000u;
        const uint32_t step = SHIFT_RPM_S * dt;
        if (s_rpm_x1000 > target + step) {
            s_rpm_x1000 -= step;
        } else if (s_rpm_x1000 + step < target) {
            s_rpm_x1000 += step;
        } else {
            s_rpm_x1000 = target;
            s_phase = s_after_shift;
        }
        break;
    }
    }

    d->rpm = (uint16_t)(s_rpm_x1000 / 1000u);
    /* Road speed follows the revs, except mid-shift where it holds. */
    if (s_phase != PHASE_SHIFT) {
        s_speed_x10 = d->gear == 0u ? 0u : (uint32_t)d->rpm * k_kph_x10_per_krpm[d->gear] / 1000u;
    }
    d->speed_kph = (uint16_t)((s_speed_x10 + 5u) / 10u);
}

static void update_gauges(dash_data_t *d, uint32_t dt)
{
    /* Warm up 1 degC every 400 ms to 88 degC, then the thermostat holds it. */
    s_temp_acc_ms += dt;
    while (s_temp_acc_ms >= 400u) {
        s_temp_acc_ms -= 400u;
        if (d->coolant_c < 88) {
            d->coolant_c++;
        }
    }
    if (d->coolant_c >= 88) {
        d->coolant_c = (int16_t)(88 + (d->rpm > 5000u ? 3 : 0));
    }

    /* Burn 1 % fuel every 10 s; "refill" at 8 %. */
    s_fuel_acc_ms += dt;
    while (s_fuel_acc_ms >= 10000u) {
        s_fuel_acc_ms -= 10000u;
        d->fuel_pct = (d->fuel_pct <= 8u) ? 95u : (uint8_t)(d->fuel_pct - 1u);
    }

    /* Alternator output rises off idle. */
    d->battery_mv = d->rpm > 1500u ? 14200u : 13800u;
}

void demo_data_update(dash_data_t *d, uint32_t now_ms)
{
    uint32_t dt = now_ms - s_last_ms;
    s_last_ms = now_ms;
    if (dt > 100u) {
        dt = 100u; /* don't jump after a stall (e.g. display re-init) */
    }

    update_drivetrain(d, now_ms, dt);
    update_gauges(d, dt);
}
