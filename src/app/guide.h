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
    GUIDE_NONE,  /* engine running, or the car is moving: no guide */
} guide_step_t;

#define GUIDE_STEPS 3u

/* Engine speed from which the start screen says the engine is cranking. */
#define GUIDE_RPM_CRANKING 50u

/*
 * The step to show: the first one not done yet. Nothing while the engine
 * runs, and nothing while the car is still rolling, so a stall on the move
 * keeps the speed on screen.
 */
guide_step_t guide_step(const dash_data_t *d);

/* The start button only works in neutral or with the clutch held. */
bool guide_start_ready(const dash_data_t *d);

/* Draw `step`'s screen into the current display list. `now_ms` drives the
 * animation. `step` must not be GUIDE_NONE. */
void guide_draw(const dash_data_t *d, guide_step_t step, uint32_t now_ms);

#endif /* GUIDE_H */
