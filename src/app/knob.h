/*
 * The menu knob: turns the encoder's raw edge count and switch level
 * (encoder.h) into clicks and presses.
 *
 *   turn   whole detents since the last poll, positive for clockwise. A
 *          click counts once the shaft has moved a full detent from the
 *          last one, so contact bounce and wobble around a detent never do.
 *   press  pressed and released again within KNOB_HOLD_MS.
 *   hold   held down for KNOB_HOLD_MS. Reported once, while still held, and
 *          the release that follows is not a press.
 *
 * The switch counts as moved once it has read the same for KNOB_DEBOUNCE_MS.
 */
#ifndef KNOB_H
#define KNOB_H

#include <stdbool.h>
#include <stdint.h>

#define KNOB_DEBOUNCE_MS 10u
#define KNOB_HOLD_MS     600u

typedef struct {
    int16_t turn;
    bool press;
    bool hold;
} knob_event_t;

/* Start from the knob as it is now: no clicks yet, and a press already in
 * progress is ignored until it is released. */
void knob_init(uint32_t now_ms);

/* What the knob did since the last call. Call as often as possible from the
 * main loop: the switch is sampled here (the encoder counts in hardware). */
knob_event_t knob_poll(uint32_t now_ms);

#endif /* KNOB_H */
