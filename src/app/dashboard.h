/* Dashboard screens for the 800x480 display. */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "dash_data.h"
#include "eve.h"

/* Boot splash: firmware version and detected EVE chip. */
eve_status_t dashboard_draw_splash(const char *fw_version, uint32_t chip_id);

/* Sweep the tach needle to the stop and back, like a real cluster at key-on.
 * Everything except rpm is drawn from `base`. Blocks for `duration_ms`. */
eve_status_t dashboard_needle_sweep(const dash_data_t *base, uint32_t duration_ms);

/* Draw one frame of the main gauge screen. `now_ms` drives blinking. */
eve_status_t dashboard_draw(const dash_data_t *d, bool demo, uint32_t now_ms);

#endif /* DASHBOARD_H */
