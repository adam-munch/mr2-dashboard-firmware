#include "knob.h"

#include "encoder.h"

#define COUNTS_PER_DETENT ((int32_t)ENCODER_COUNTS_PER_DETENT)

/* Rotation */
static uint16_t s_count;     /* encoder count at the last poll */
static int32_t s_residual;   /* edges since the last whole click, either way */

/* Switch */
static bool s_raw;           /* level at the last poll */
static uint32_t s_raw_since; /* when it last changed */
static bool s_down;          /* debounced level */
static uint32_t s_down_ms;   /* when the current press began */
static bool s_held;          /* hold reported for this press, or it began before knob_init() */

void knob_init(uint32_t now_ms)
{
    s_count = encoder_hw_count();
    s_residual = 0;

    s_raw = encoder_hw_switch();
    s_raw_since = now_ms;
    s_down = s_raw;
    s_down_ms = now_ms;
    s_held = s_raw;
}

knob_event_t knob_poll(uint32_t now_ms)
{
    knob_event_t ev = {0, false, false};

    /* The count wraps at 16 bits; the difference since the last poll doesn't. */
    const uint16_t count = encoder_hw_count();
    s_residual += (int16_t)(uint16_t)(count - s_count);
    s_count = count;
    while (s_residual >= COUNTS_PER_DETENT) {
        s_residual -= COUNTS_PER_DETENT;
        ev.turn++;
    }
    while (s_residual <= -COUNTS_PER_DETENT) {
        s_residual += COUNTS_PER_DETENT;
        ev.turn--;
    }

    const bool raw = encoder_hw_switch();
    if (raw != s_raw) {
        s_raw = raw;
        s_raw_since = now_ms;
    }
    if (s_down != s_raw && (uint32_t)(now_ms - s_raw_since) >= KNOB_DEBOUNCE_MS) {
        s_down = s_raw;
        if (s_down) {
            s_down_ms = s_raw_since;
            s_held = false;
        } else if (!s_held) {
            ev.press = true;
        }
    }
    if (s_down && !s_held && (uint32_t)(now_ms - s_down_ms) >= KNOB_HOLD_MS) {
        s_held = true;
        ev.hold = true;
    }
    return ev;
}
