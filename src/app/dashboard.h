/* Dashboard screens for the 800x480 display. */
#ifndef DASHBOARD_H
#define DASHBOARD_H

#include <stdbool.h>
#include <stdint.h>

#include "dash_data.h"
#include "eve.h"

/* Number of F1-style start lights on the splash screen. */
#define DASHBOARD_START_LIGHTS 5u

/* Boot splash: firmware version, detected EVE chip, and the start lights
 * with `lights_on` of them lit. */
eve_status_t dashboard_draw_splash(const char *fw_version, uint32_t chip_id, uint32_t lights_on);

/* Sweep the rev bar and shift lights to the stop and back, like a real
 * cluster at key-on. Everything except rpm is drawn from `base`. Blocks for
 * `duration_ms`. */
eve_status_t dashboard_rev_sweep(const dash_data_t *base, uint32_t duration_ms);

/* Draw one frame. Until the engine is running this is the pre-start guide
 * (e-stop, steering wheel, start button; see guide.h), then the main screen.
 * `now_ms` drives the flashing and animation. */
eve_status_t dashboard_draw(const dash_data_t *d, bool demo, uint32_t now_ms);

#endif /* DASHBOARD_H */
