/*
 * Full-screen pages for the menu (MENU_PAGE items). Each draws into the
 * menu body (MENU_BODY_Y0..MENU_BODY_Y1), under the menu's title.
 */
#ifndef PAGES_H
#define PAGES_H

#include <stdint.h>

#include "dash_data.h"

/* More from the ECU than the main screen has room for: oil pressure,
 * lambda, MAP, intake air, throttle, ignition timing. */
void pages_draw_engine(const dash_data_t *d, uint32_t now_ms);

/* For bring-up: firmware, display, uptime, the knob's raw state, and the
 * CAN controller's state and counters. */
void pages_draw_diagnostics(const dash_data_t *d, uint32_t now_ms);

#endif /* PAGES_H */
