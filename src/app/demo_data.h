/*
 * Simulated engine data so the dashboard can be exercised without a car.
 * Used instead of the ECU's data when built with DASH_DEMO_DATA=1.
 */
#ifndef DEMO_DATA_H
#define DEMO_DATA_H

#include <stdint.h>

#include "dash_data.h"

void demo_data_init(dash_data_t *d, uint32_t now_ms);

/* Advance the simulation to `now_ms`. Call once per frame. */
void demo_data_update(dash_data_t *d, uint32_t now_ms);

#endif /* DEMO_DATA_H */
