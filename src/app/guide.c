#include "guide.h"

#include "ui.h"

/*
 *   +----------------------------------------------------------------+
 *   | (1 E-STOP OFF) ---- (2 WHEEL IN) ---- (3 START)                |
 *   |                                                                |
 *   |                          [ icon ]                              |
 *   |                     TITLE OF THE STEP                          |
 *   |                     what to do, in a line                      |
 *   |                                                                |
 *   |                   live state of the input                      |
 *   +----------------------------------------------------------------+
 *
 * The start step swaps the last two lines for an amber-framed card with the
 * neutral-or-clutch rule and the live state of both conditions.
 */

#define COL_ESTOP_YELLOW 0xFFC400u /* e-stop backplate */
#define COL_ESTOP_SHADE  0x8A0600u

/* Stepper along the top */
#define STEP_X0  16
#define STEP_X1  784
#define STEP_Y0  20
#define STEP_Y1  60
#define STEP_GAP 40

/* Content, centred */
#define CX      (SCREEN_W / 2)
#define ICON_Y  156
#define TITLE_Y 258
#define TEXT_Y  300
#define CHIP_Y0 344
#define CHIP_Y1 380
#define CHIP_W  300

/* Start step: rule card with the two conditions, then the verdict */
#define CARD_X0   136
#define CARD_X1   664
#define CARD_Y0   292
#define CARD_Y1   404
#define COND_Y0   (CARD_Y0 + 50)
#define COND_Y1   (CARD_Y1 - 14)
#define COND_GAP  56
#define VERDICT_Y 436

/* ---------------------------------------------------------------------------
 * Step logic
 * ------------------------------------------------------------------------ */

/* The input each step waits for; 0 for the start step, which is always shown. */
static const uint16_t k_step_input[GUIDE_STEPS] = {DASH_VALID_ESTOP, DASH_VALID_WHEEL, 0u};

guide_step_t guide_step(const dash_data_t *d)
{
    const bool parked = dash_valid(d, DASH_VALID_RPM | DASH_VALID_SPEED) && d->rpm < DASH_RPM_RUNNING &&
                        d->speed_kph == 0u;
    if (!parked && d->link != DASH_LINK_WAITING) {
        return GUIDE_NONE;
    }
    if (dash_valid(d, DASH_VALID_ESTOP) && d->estop_active) {
        return GUIDE_ESTOP;
    }
    if (dash_valid(d, DASH_VALID_WHEEL) && !d->wheel_connected) {
        return GUIDE_WHEEL;
    }
    return GUIDE_START;
}

guide_start_t guide_start_state(const dash_data_t *d)
{
    const bool gear_known = dash_valid(d, DASH_VALID_GEAR);
    const bool clutch_known = dash_valid(d, DASH_VALID_CLUTCH);
    if ((gear_known && d->gear == 0u) || (clutch_known && d->clutch_held)) {
        return GUIDE_START_READY;
    }
    return gear_known && clutch_known ? GUIDE_START_BLOCKED : GUIDE_START_UNKNOWN;
}

/* ---------------------------------------------------------------------------
 * Pieces
 * ------------------------------------------------------------------------ */

/* Three pills: done ones green with a tick, the current one outlined in white,
 * and ones whose input isn't wired up dimmed with a dash. */
static void draw_stepper(const dash_data_t *d, guide_step_t current)
{
    static const char *const labels[GUIDE_STEPS] = {"E-STOP OFF", "WHEEL IN", "START"};
    const int32_t r = (STEP_Y1 - STEP_Y0) / 2;
    const int32_t cy = (STEP_Y0 + STEP_Y1) / 2;

    for (uint32_t i = 0u; i < GUIDE_STEPS; i++) {
        const bool now = i == (uint32_t)current;
        const bool unknown = !now && k_step_input[i] != 0u && !dash_valid(d, k_step_input[i]);
        const bool done = !unknown && i < (uint32_t)current;
        const int32_t x0 = ui_spread_x(i, GUIDE_STEPS, STEP_X0, STEP_X1, STEP_GAP);
        const int32_t x1 = ui_spread_x(i + 1u, GUIDE_STEPS, STEP_X0, STEP_X1, STEP_GAP) - STEP_GAP;

        if (i + 1u < GUIDE_STEPS) { /* connector to the next step */
            ui_rect(x1 + 8, cy - 1, x1 + STEP_GAP - 8, cy + 1, done ? COL_GREEN : COL_TRACK, 1);
        }
        if (now) {
            ui_rect(x0, STEP_Y0, x1, STEP_Y1, COL_TEXT, r);
            ui_rect(x0 + 2, STEP_Y0 + 2, x1 - 2, STEP_Y1 - 2, COL_PANEL, r - 2);
        } else {
            ui_rect(x0, STEP_Y0, x1, STEP_Y1, done ? COL_GREEN_DARK : COL_PANEL, r);
        }

        const int32_t bx = x0 + r;
        ui_dot(bx, cy, 13, done ? COL_GREEN : (now ? COL_TEXT : COL_PANEL_EDGE));
        if (done) {
            ui_check(bx, cy, COL_BG);
        } else if (unknown) {
            eve_color(COL_TEXT_FAINT);
            eve_cmd_text((int16_t)bx, (int16_t)cy, FONT_SMALL, OPT_CENTER, "-");
        } else {
            eve_color(now ? COL_BG : COL_TEXT_DIM);
            eve_cmd_number((int16_t)bx, (int16_t)cy, FONT_SMALL, OPT_CENTER, (int32_t)(i + 1u));
        }
        eve_color(done ? COL_GREEN : (now ? COL_TEXT : COL_TEXT_FAINT));
        eve_cmd_text((int16_t)(bx + 24), (int16_t)cy, FONT_SMALL, OPT_CENTERY, labels[i]);
    }
}

static void draw_title(const char *title, const char *text)
{
    eve_color(COL_TEXT);
    eve_cmd_text(CX, TITLE_Y, FONT_VALUE, OPT_CENTER, title);
    if (text != NULL) {
        eve_color(COL_TEXT_DIM);
        eve_cmd_text(CX, TEXT_Y, FONT_MEDIUM, OPT_CENTER, text);
    }
}

/* Live state of the input this step waits for. */
static void draw_status_chip(const char *text, uint32_t colour, uint32_t fill)
{
    const int32_t cy = (CHIP_Y0 + CHIP_Y1) / 2;
    ui_rect(CX - CHIP_W / 2, CHIP_Y0, CX + CHIP_W / 2, CHIP_Y1, fill, (CHIP_Y1 - CHIP_Y0) / 2);
    ui_dot(CX - CHIP_W / 2 + 22, cy, 5, colour);
    eve_color(COL_TEXT);
    eve_cmd_text(CX + 8, (int16_t)cy, FONT_SMALL, OPT_CENTER, text);
}

/* ---------------------------------------------------------------------------
 * Icons, drawn with primitives so they cost no graphics memory
 * ------------------------------------------------------------------------ */

static void draw_estop_icon(int32_t cx, int32_t cy)
{
    ui_dot(cx, cy, 62, COL_ESTOP_YELLOW);
    ui_dot(cx, cy, 44, COL_ESTOP_SHADE);
    ui_dot(cx - 2, cy - 2, 39, COL_RED);
    eve_cmd(COLOR_A(70)); /* a soft highlight on the dome */
    ui_dot(cx - 14, cy - 14, 13, COL_TEXT);
    eve_cmd(COLOR_A(255));
}

/* A D-shaped racing wheel: rim with a flat bottom, three spokes, hub. */
static void draw_wheel_icon(int32_t cx, int32_t cy)
{
    ui_dot(cx, cy, 60, COL_BAR);
    ui_dot(cx, cy, 49, COL_BG);
    ui_rect(cx - 62, cy + 40, cx + 62, cy + 62, COL_BG, 1); /* cut the bottom flat... */
    ui_rect(cx - 44, cy + 34, cx + 44, cy + 46, COL_BAR, 5); /* ...and close it */

    eve_color(COL_BAR);
    eve_cmd(LINE_WIDTH(6 * 16));
    eve_cmd(BEGIN(LINES));
    eve_cmd(VERTEX2F((cx - 52) * 16, cy * 16)); /* spokes */
    eve_cmd(VERTEX2F((cx + 52) * 16, cy * 16));
    eve_cmd(VERTEX2F(cx * 16, cy * 16));
    eve_cmd(VERTEX2F(cx * 16, (cy + 38) * 16));
    eve_cmd(END());

    ui_dot(cx, cy, 20, COL_BAR);
    ui_dot(cx, cy, 13, COL_PANEL_EDGE); /* the connector in the hub */
    ui_dot(cx - 5, cy - 3, 2, COL_BAR);
    ui_dot(cx + 5, cy - 3, 2, COL_BAR);
    ui_dot(cx, cy + 5, 2, COL_BAR);
}

/* The start button. When it will work, its ring is green and breathes. */
static void draw_start_icon(int32_t cx, int32_t cy, uint32_t ring, bool glow, uint32_t now_ms)
{
    if (glow) {
        const uint32_t phase = now_ms % 1200u; /* 1.2 s breathing cycle */
        const uint32_t level = phase < 600u ? phase : 1200u - phase;
        eve_cmd(COLOR_A(30u + level * 70u / 600u));
        ui_dot(cx, cy, 72, ring);
        eve_cmd(COLOR_A(255));
    }
    ui_dot(cx, cy, 60, COL_PANEL_EDGE);
    ui_dot(cx, cy, 56, ring);
    ui_dot(cx, cy, 48, COL_PANEL);
    eve_color(COL_TEXT);
    ui_text_heavy((int16_t)cx, (int16_t)cy, FONT_MEDIUM, "START");
}

/* ---------------------------------------------------------------------------
 * Start step
 * ------------------------------------------------------------------------ */

/* One of the two conditions: green with a tick when met, hollow when not,
 * dimmed with "--" when its input isn't known. `gear`, if not NULL, is shown
 * in a small box like the main gear display. */
static void draw_condition(int32_t x0, int32_t x1, const char *label, const char *gear, bool known, bool met)
{
    const int32_t cy = (COND_Y0 + COND_Y1) / 2;
    met = met && known;

    ui_rect(x0, COND_Y0, x1, COND_Y1, met ? COL_GREEN : COL_PANEL_EDGE, 10);
    ui_rect(x0 + 2, COND_Y0 + 2, x1 - 2, COND_Y1 - 2, met ? COL_GREEN_DARK : COL_PANEL, 8);
    if (met) {
        ui_dot(x0 + 26, cy, 12, COL_GREEN);
        ui_check(x0 + 26, cy, COL_BG);
    } else {
        ui_dot(x0 + 26, cy, 12, COL_TEXT_FAINT);
        ui_dot(x0 + 26, cy, 10, COL_PANEL);
    }
    eve_color(met ? COL_TEXT : (known ? COL_TEXT_DIM : COL_TEXT_FAINT));
    eve_cmd_text((int16_t)(x0 + 48), (int16_t)cy, FONT_SMALL, OPT_CENTERY, label);
    if (gear == NULL && !known) {
        eve_color(COL_TEXT_FAINT);
        eve_cmd_text((int16_t)(x1 - 18), (int16_t)cy, FONT_MEDIUM, OPT_RIGHTX | OPT_CENTERY, "--");
    }
    if (gear != NULL) {
        ui_rect(x1 - 46, cy - 15, x1 - 14, cy + 15, met ? COL_GREEN : COL_PANEL_EDGE, 6);
        ui_rect(x1 - 44, cy - 13, x1 - 16, cy + 13, met ? COL_GREEN_DARK : COL_PANEL, 5);
        eve_color(met ? COL_GREEN : (known ? COL_TEXT : COL_TEXT_FAINT));
        eve_cmd_text((int16_t)(x1 - 30), (int16_t)cy, FONT_MEDIUM, OPT_CENTER, gear);
    }
}

static void draw_start_step(const dash_data_t *d, uint32_t now_ms)
{
    const guide_start_t state = guide_start_state(d);
    const bool ready = state == GUIDE_START_READY;
    const bool cranking = dash_valid(d, DASH_VALID_RPM) && d->rpm >= GUIDE_RPM_CRANKING;
    const bool gear_known = dash_valid(d, DASH_VALID_GEAR) && d->gear <= 9u;

    draw_start_icon(CX, ICON_Y, cranking ? COL_TEXT : (ready ? COL_GREEN : COL_TEXT_FAINT), ready && !cranking,
                    now_ms);
    draw_title("PRESS START", NULL);

    /* The rule, framed in amber so it can't be missed. */
    ui_rect(CARD_X0, CARD_Y0, CARD_X1, CARD_Y1, COL_AMBER, 12);
    ui_rect(CARD_X0 + 2, CARD_Y0 + 2, CARD_X1 - 2, CARD_Y1 - 2, COL_AMBER_DARK, 10);
    const int32_t rule_y = CARD_Y0 + 26;
    ui_dot(CARD_X0 + 30, rule_y, 11, COL_AMBER);
    eve_color(COL_BG);
    eve_cmd_text(CARD_X0 + 30, (int16_t)rule_y, FONT_SMALL, OPT_CENTER, "!");
    eve_color(COL_AMBER);
    eve_cmd_text(CARD_X0 + 52, (int16_t)rule_y, FONT_SMALL, OPT_CENTERY,
                 "Only starts in neutral or with the clutch held");

    /* Both conditions, live: either one is enough. */
    char gear_str[2] = {'-', '\0'};
    if (gear_known) {
        gear_str[0] = d->gear == 0u ? 'N' : (char)('0' + d->gear);
    }
    const int32_t mid = (CARD_X0 + CARD_X1) / 2;
    draw_condition(CARD_X0 + 16, mid - COND_GAP / 2, "NEUTRAL", gear_str, gear_known, d->gear == 0u);
    draw_condition(mid + COND_GAP / 2, CARD_X1 - 16, "CLUTCH HELD", NULL, dash_valid(d, DASH_VALID_CLUTCH),
                   d->clutch_held);
    eve_color(COL_AMBER);
    eve_cmd_text((int16_t)mid, (COND_Y0 + COND_Y1) / 2, FONT_TINY, OPT_CENTER, "OR");

    if (cranking) {
        eve_color(COL_TEXT);
        eve_cmd_text(CX, VERDICT_Y, FONT_SMALL, OPT_CENTER, "CRANKING");
    } else if (ready) {
        eve_color(COL_GREEN);
        eve_cmd_text(CX, VERDICT_Y, FONT_SMALL, OPT_CENTER, "READY TO START");
    } else if (state == GUIDE_START_BLOCKED) {
        eve_color(COL_AMBER);
        eve_cmd_text(CX, VERDICT_Y, FONT_SMALL, OPT_CENTER, "NOT READY");
    } /* unknown: no verdict rather than a wrong one */
}

/* ---------------------------------------------------------------------------
 * Screen
 * ------------------------------------------------------------------------ */

void guide_draw(const dash_data_t *d, guide_step_t step, uint32_t now_ms)
{
    draw_stepper(d, step);

    switch (step) {
    case GUIDE_ESTOP:
        draw_estop_icon(CX, ICON_Y);
        draw_title("TURN OFF THE E-STOP", "Release the red emergency stop button");
        draw_status_chip("E-STOP ACTIVE", COL_RED, COL_RED_DARK);
        break;
    case GUIDE_WHEEL:
        draw_wheel_icon(CX, ICON_Y);
        draw_title("PLUG IN THE STEERING WHEEL", "Fit the wheel and connect its plug");
        draw_status_chip("WHEEL NOT DETECTED", COL_AMBER, COL_AMBER_DARK);
        break;
    case GUIDE_START:
        draw_start_step(d, now_ms);
        break;
    default:
        break;
    }
}
