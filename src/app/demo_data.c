#include "demo_data.h"

/*
 * First the start procedure: the car sits with the e-stop pressed in, no
 * steering wheel and first gear still selected. The driver releases the
 * e-stop, plugs the wheel in, holds the clutch (so the start button will
 * work) and starts the engine.
 *
 * Then a repeating drive cycle: idle, pull through the gears, coast down
 * through the gears, idle again. Gearing is roughly an SW20 MR2 (E153
 * gearbox, 4.285 final drive, 225/50R15 rear tyres), and the engine data
 * page shows boost and a rich mixture on the pulls, vacuum on the overrun.
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

/* Start procedure, ms after demo_data_init() */
#define ESTOP_OFF_MS     3000u
#define WHEEL_IN_MS      6000u
#define CLUTCH_IN_MS     8500u /* until here the start button wouldn't work */
#define CRANK_MS         10000u
#define FIRE_MS          10800u
#define CRANK_RPM        200u
#define PARKED_MV        12500u

/* Engine data page: roughly a 3S-GTE, with boost under load. */
#define ATMOSPHERE_KPA   101u
#define IDLE_MAP_KPA     38u
#define OVERRUN_MAP_KPA  28u
#define BOOST_MAP_KPA    180u /* about 0.8 bar of boost */
#define AMBIENT_C        25

typedef enum {
    PHASE_PRESTART,
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
    d->rpm = 0u;
    d->speed_kph = 0u;
    d->coolant_c = 25;
    d->fuel_pct = 72u;
    d->battery_mv = PARKED_MV;
    d->gear = 1u;
    d->valid = (uint16_t)((DASH_VALID_ALL | DASH_VALID_INPUTS) & ~DASH_VALID_LAMBDA); /* engine off */
    d->warnings = 0u;
    d->link = DASH_LINK_DEMO;
    d->estop_active = true;
    d->wheel_connected = false;
    d->clutch_held = false;
    d->oil_kpa = 0u;
    d->lambda_x1000 = 0u;
    d->map_kpa = ATMOSPHERE_KPA;
    d->intake_c = AMBIENT_C;
    d->throttle_pct = 0u;
    d->timing_deg_x10 = 0;

    s_phase = PHASE_PRESTART;
    s_rpm_x1000 = 0u;
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
    case PHASE_PRESTART: {
        const uint32_t t = now_ms - s_phase_start_ms;
        d->estop_active = t < ESTOP_OFF_MS;
        d->wheel_connected = t >= WHEEL_IN_MS;
        d->clutch_held = t >= CLUTCH_IN_MS;
        if (t >= FIRE_MS) {
            s_rpm_x1000 = IDLE_RPM * 1000u;
            s_phase = PHASE_IDLE; /* in first, clutch held, ready to go */
            s_phase_start_ms = now_ms;
        } else if (t >= CRANK_MS) {
            s_rpm_x1000 = CRANK_RPM * 1000u + ((now_ms / 53u) % 60u) * 1000u; /* uneven cranking */
        }
        break;
    }

    case PHASE_IDLE:
        s_rpm_x1000 = IDLE_RPM * 1000u + ((now_ms / 97u) % 30u) * 1000u; /* slight hunt */
        if ((uint32_t)(now_ms - s_phase_start_ms) >= IDLE_TIME_MS) {
            d->gear = 1u;
            d->clutch_held = false;
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
            d->clutch_held = true;
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
    /* Road speed follows the revs when the drive is engaged, except
     * mid-shift where it holds. */
    if (s_phase != PHASE_SHIFT) {
        const bool driven = d->gear != 0u && !d->clutch_held;
        s_speed_x10 = driven ? (uint32_t)d->rpm * k_kph_x10_per_krpm[d->gear] / 1000u : 0u;
    }
    d->speed_kph = (uint16_t)((s_speed_x10 + 5u) / 10u);
}

static void update_gauges(dash_data_t *d, uint32_t dt)
{
    if (!dash_engine_running(d)) {
        d->battery_mv = PARKED_MV;
        return;
    }

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

static void update_engine(dash_data_t *d)
{
    const bool running = dash_engine_running(d);
    uint32_t map = ATMOSPHERE_KPA;
    uint8_t throttle = 0u;
    int32_t timing = 0;
    uint16_t lambda = 1000u;

    switch (s_phase) {
    case PHASE_PRESTART:
        if (d->rpm > 0u) { /* cranking */
            map = 95u;
            timing = 100;
        }
        break;
    case PHASE_IDLE:
        map = IDLE_MAP_KPA;
        timing = 120;
        break;
    case PHASE_ACCEL: {
        /* Full throttle: the turbo builds boost from ~1500 rpm, rich, with
         * timing pulled back as the boost rises. */
        const uint32_t rpm = d->rpm > 1500u ? d->rpm - 1500u : 0u;
        map = 90u + rpm * (BOOST_MAP_KPA - 90u) / 3000u;
        map = map > BOOST_MAP_KPA ? BOOST_MAP_KPA : map;
        throttle = 100u;
        timing = 280 - ((int32_t)map - 100) * 10 / 8;
        lambda = 820u;
        break;
    }
    case PHASE_DECEL:
        map = OVERRUN_MAP_KPA;
        timing = 250;
        lambda = 1100u;
        break;
    case PHASE_SHIFT:
        map = 45u;
        timing = 150;
        break;
    }

    d->map_kpa = (uint16_t)map;
    d->throttle_pct = throttle;
    d->timing_deg_x10 = (int16_t)timing;
    d->intake_c = (int16_t)(AMBIENT_C + 3 + (map > 100u ? (int32_t)(map - 100u) / 8 : 0)); /* intercooled */

    /* Oil pressure rises with the revs. The wideband only reads once the
     * engine runs (the sensor needs exhaust to heat it). */
    d->oil_kpa = running ? (uint16_t)(150u + d->rpm * 40u / 1000u) : 0u;
    d->lambda_x1000 = running ? lambda : 0u;
    if (running) {
        d->valid |= DASH_VALID_LAMBDA;
    } else {
        d->valid &= (uint16_t)~DASH_VALID_LAMBDA;
    }
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
    update_engine(d);
}
