/* Values shown on the dashboard. */
#ifndef DASH_DATA_H
#define DASH_DATA_H

#include <stdint.h>

typedef struct {
    uint16_t rpm;
    uint16_t speed_kph;
    int16_t coolant_c;
    uint8_t fuel_pct;
    uint16_t battery_mv;
    uint8_t gear; /* 0 = neutral, 1..5 */
} dash_data_t;

#endif /* DASH_DATA_H */
