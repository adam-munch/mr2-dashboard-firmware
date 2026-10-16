/* Values shown on the dashboard. */
#ifndef DASH_DATA_H
#define DASH_DATA_H

#include <stdbool.h>
#include <stdint.h>

/* Engine speed above which the engine counts as running (cranking is slower). */
#define DASH_RPM_RUNNING 400u

typedef struct {
    uint16_t rpm;
    uint16_t speed_kph;
    int16_t coolant_c;
    uint8_t fuel_pct;
    uint16_t battery_mv;
    uint8_t gear; /* 0 = neutral, 1..5 */

    /* Start procedure */
    bool estop_active;    /* emergency stop pressed in: the car can't start */
    bool wheel_connected; /* steering wheel plugged in */
    bool clutch_held;     /* clutch pedal down */
} dash_data_t;

static inline bool dash_engine_running(const dash_data_t *d)
{
    return d->rpm >= DASH_RPM_RUNNING;
}

#endif /* DASH_DATA_H */
