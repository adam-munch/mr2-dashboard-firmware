/*
 * Pre-start guide: walks the driver through the three things needed to start
 * the engine, one screen per step, driven by the live inputs.
 */
#ifndef GUIDE_H
#define GUIDE_H

#include <stdint.h>

#include "dash_data.h"

typedef enum {
    GUIDE_ESTOP, /* turn off the e-stop       */
    GUIDE_WHEEL, /* plug in the steering wheel */
    GUIDE_START, /* press start                */
    GUIDE_NONE,  /* no guide: engine running, car moving, or no way to tell */
} guide_step_t;

#define GUIDE_STEPS 3u

/* Engine speed from which the start screen says the engine is cranking. */
#define GUIDE_RPM_CRANKING 50u

/* Whether the start button would work right now. */
typedef enum {
    GUIDE_START_BLOCKED, /* in gear and the clutch is up */
    GUIDE_START_READY,   /* in neutral, or the clutch is held */
    GUIDE_START_UNKNOWN, /* neither is known to be met, and one can't be checked */
} guide_start_t;

/*
 * The step to show: the first one not done yet, skipping any whose input
 * isn't wired up (see DASH_VALID_INPUTS), since the guide can't check it.
 *
 * The guide is for a parked car: it shows only when rpm and speed say the
 * engine is off and the car is stopped, or before the ECU has said anything
 * since power-up. A stall or a lost ECU link on the move keeps the gauges.
 */
guide_step_t guide_step(const dash_data_t *d);

/* The start button only works in neutral or with the clutch held. */
guide_start_t guide_start_state(const dash_data_t *d);

/* Draw `step`'s screen into the current display list. `now_ms` drives the
 * animation. `step` must not be GUIDE_NONE. */
void guide_draw(const dash_data_t *d, guide_step_t step, uint32_t now_ms);

#endif /* GUIDE_H */
