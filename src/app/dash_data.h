/* Values shown on the dashboard. */
#ifndef DASH_DATA_H
#define DASH_DATA_H

#include <stdbool.h>
#include <stdint.h>

/* dash_data_t.valid: which values are current. The others (no data yet, or
 * the ECU stopped sending them) are drawn as "--". */
#define DASH_VALID_RPM     (1u << 0)
#define DASH_VALID_SPEED   (1u << 1)
#define DASH_VALID_COOLANT (1u << 2)
#define DASH_VALID_FUEL    (1u << 3)
#define DASH_VALID_BATTERY (1u << 4)
#define DASH_VALID_GEAR    (1u << 5)

/* The start-procedure inputs (see guide.h). They don't come from the ECU, so
 * set these once the e-stop, steering-wheel detect and clutch switch are
 * wired up. Until then the pre-start guide skips what it can't check. */
#define DASH_VALID_ESTOP   (1u << 6)
#define DASH_VALID_WHEEL   (1u << 7)
#define DASH_VALID_CLUTCH  (1u << 8)
#define DASH_VALID_INPUTS  (DASH_VALID_ESTOP | DASH_VALID_WHEEL | DASH_VALID_CLUTCH)

/* The engine data page (pages.h). Each needs its sensor fitted on the ECU. */
#define DASH_VALID_OIL_PRESS (1u << 9)
#define DASH_VALID_LAMBDA    (1u << 10)
#define DASH_VALID_INTAKE    (1u << 11) /* MAP and intake air temperature */
#define DASH_VALID_THROTTLE  (1u << 12)
#define DASH_VALID_TIMING    (1u << 13)
#define DASH_VALID_ENGINE    (DASH_VALID_OIL_PRESS | DASH_VALID_LAMBDA | DASH_VALID_INTAKE | \
                              DASH_VALID_THROTTLE | DASH_VALID_TIMING)

/* Everything the ECU sends: all of the above but the start inputs. */
#define DASH_VALID_ALL (0x3Fu | DASH_VALID_ENGINE)

/* dash_data_t.warnings: warning lamps. */
#define DASH_WARN_CHECK_ENGINE (1u << 0) /* the ECU's check engine output is on */
#define DASH_WARN_LIMITER      (1u << 1) /* the ECU is cutting fuel or spark */

/* Where the values come from. Anything but OK is shown in the status line. */
typedef enum {
    DASH_LINK_OK = 0,    /* live data from the ECU */
    DASH_LINK_DEMO,      /* built-in simulation */
    DASH_LINK_WAITING,   /* nothing from the ECU since power-up */
    DASH_LINK_LOST,      /* the ECU stopped sending */
    DASH_LINK_CAN_FAULT, /* CAN controller not running, or off the bus */
} dash_link_t;

/* Engine speed above which the engine counts as running (cranking is slower). */
#define DASH_RPM_RUNNING 400u

typedef struct {
    uint16_t rpm;
    uint16_t speed_kph;
    int16_t coolant_c;
    uint8_t fuel_pct;
    uint16_t battery_mv;
    uint8_t gear;      /* 0 = neutral, 1..9 */
    uint16_t valid;    /* DASH_VALID_* */
    uint8_t warnings;  /* DASH_WARN_* */
    dash_link_t link;

    /* Start procedure */
    bool estop_active;    /* emergency stop pressed in: the car can't start */
    bool wheel_connected; /* steering wheel plugged in */
    bool clutch_held;     /* clutch pedal down */

    /* Engine data page */
    uint16_t oil_kpa;       /* oil pressure, gauge */
    uint16_t lambda_x1000;  /* wideband sensor 1 */
    uint16_t map_kpa;       /* manifold absolute pressure */
    int16_t intake_c;       /* intake air temperature */
    uint8_t throttle_pct;
    int16_t timing_deg_x10; /* ignition advance, degrees before TDC */
} dash_data_t;

static inline bool dash_valid(const dash_data_t *d, uint16_t mask)
{
    return (d->valid & mask) == mask;
}

static inline bool dash_engine_running(const dash_data_t *d)
{
    return dash_valid(d, DASH_VALID_RPM) && d->rpm >= DASH_RPM_RUNNING;
}

#endif /* DASH_DATA_H */
