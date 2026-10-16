#include "dashboard.h"

#include <stdio.h>

#include "eve_hal.h"
#include "guide.h"
#include "logo.h"
#include "ui.h"

/*
 * F1-style layout. While everything is normal the screen is monochrome apart
 * from the shift lights; a value only turns blue, amber or red when it needs
 * attention (see ui.h).
 *
 *   +----------------------------------------------------------------+
 *   | shift lights: green, red, purple; all flash at the shift point |
 *   | rev bar 0..8000 rpm with the redline zone tinted               |
 *   |                                                                |
 *   |     SPEED              [ GEAR ]              RPM               |
 *   |                       [ alert  ]  (only when there is one)     |
 *   |                                                                |
 *   | [ COOLANT        ]  [ FUEL          ]  [ BATTERY       ]      |
 *   +----------------------------------------------------------------+
 */

/* Engine and warning thresholds */
#define RPM_MAX          8000u
#define RPM_REDLINE      7000u
#define RPM_SHIFT        6500u /* all shift lights lit and flashing */
#define RPM_LEDS_START   4500u /* first shift light */
#define COOLANT_COLD_C   60
#define COOLANT_HIGH_C   100
#define COOLANT_HOT_C    105
#define FUEL_LOW_PCT     15
#define FUEL_EMPTY_PCT   5
#define BATT_LOW_MV      11800
#define BATT_HIGH_MV     15000
#define BATT_CHARGING_MV 13000

/* Ranges of the tile bars */
#define COOLANT_BAR_MIN 40
#define COOLANT_BAR_MAX 120
#define BATT_BAR_MIN    10000
#define BATT_BAR_MAX    16000

/* Shift lights: a row of LEDs across the top edge, in three colour groups. */
#define LED_COUNT 15u
#define LED_GROUP 5u
#define LED_X0    16
#define LED_X1    784
#define LED_Y0    14
#define LED_Y1    34
#define LED_GAP   8
#define LED_GLOW  5

/* Rev bar: one segment per 250 rpm. */
#define REV_SEGS 32u
#define REV_X0   16
#define REV_X1   784
#define REV_Y0   50
#define REV_Y1   62
#define REV_GAP  4

/* Centre row: speed | gear | rpm */
#define GEAR_X0 324
#define GEAR_X1 476
#define GEAR_Y0 102
#define GEAR_Y1 270
#define VALUE_Y 176 /* centre line of the big readouts */
#define SPEED_X 164
#define RPM_X   636

/* Alert slot under the gear */
#define ALERT_X0 288
#define ALERT_X1 512
#define ALERT_Y0 284
#define ALERT_Y1 318

/* Vitals tiles along the bottom */
#define TILE_COUNT 3u
#define TILE_X0    16
#define TILE_Y0    344
#define TILE_Y1    448
#define TILE_W     248
#define TILE_GAP   12
#define TILE_PAD   20

/* Splash: team logo, title, start lights */
#define SPLASH_LOGO_W     320
#define SPLASH_LOGO_Y     84
#define SPLASH_TITLE_Y    288
#define START_LIGHT_Y     352
#define START_LIGHT_R     16
#define START_LIGHT_PITCH 64

/* ---------------------------------------------------------------------------
 * Status of the monitored values
 * ------------------------------------------------------------------------ */

typedef enum {
    LEVEL_OK,
    LEVEL_COLD, /* informational: not up to temperature yet */
    LEVEL_WARN,
    LEVEL_CRIT,
} level_t;

typedef struct {
    level_t level;
    const char *word;  /* shown on the tile; NULL when normal  */
    const char *alert; /* shown in the alert slot; NULL if none */
} vital_status_t;

static uint32_t level_colour(level_t level)
{
    switch (level) {
    case LEVEL_COLD:
        return COL_BLUE;
    case LEVEL_WARN:
        return COL_AMBER;
    case LEVEL_CRIT:
        return COL_RED;
    default:
        return COL_TEXT;
    }
}

static vital_status_t coolant_status(int16_t c)
{
    if (c > COOLANT_HOT_C) {
        return (vital_status_t){LEVEL_CRIT, "HOT", "COOLANT HOT"};
    }
    if (c > COOLANT_HIGH_C) {
        return (vital_status_t){LEVEL_WARN, "HIGH", "COOLANT HIGH"};
    }
    if (c < COOLANT_COLD_C) {
        return (vital_status_t){LEVEL_COLD, "COLD", NULL};
    }
    return (vital_status_t){LEVEL_OK, NULL, NULL};
}

static vital_status_t fuel_status(uint8_t pct)
{
    if (pct < FUEL_EMPTY_PCT) {
        return (vital_status_t){LEVEL_CRIT, "EMPTY", "FUEL EMPTY"};
    }
    if (pct < FUEL_LOW_PCT) {
        return (vital_status_t){LEVEL_WARN, "LOW", "LOW FUEL"};
    }
    return (vital_status_t){LEVEL_OK, NULL, NULL};
}

static vital_status_t battery_status(uint16_t mv, uint16_t rpm)
{
    if (mv < BATT_LOW_MV) {
        return (vital_status_t){LEVEL_WARN, "LOW", "BATTERY LOW"};
    }
    if (mv > BATT_HIGH_MV) {
        return (vital_status_t){LEVEL_WARN, "HIGH", "VOLTAGE HIGH"};
    }
    /* With the engine running the alternator should be charging. */
    if (rpm >= DASH_RPM_RUNNING && mv < BATT_CHARGING_MV) {
        return (vital_status_t){LEVEL_WARN, "NO CHG", "NOT CHARGING"};
    }
    return (vital_status_t){LEVEL_OK, NULL, NULL};
}

/* ---------------------------------------------------------------------------
 * Drawing helpers
 * ------------------------------------------------------------------------ */

static uint32_t clamp_permille(int32_t value, int32_t lo, int32_t hi)
{
    if (value <= lo) {
        return 0u;
    }
    if (value >= hi) {
        return 1000u;
    }
    return (uint32_t)((value - lo) * 1000 / (hi - lo));
}

/* ---------------------------------------------------------------------------
 * Shift lights and rev bar
 * ------------------------------------------------------------------------ */

/* The first LED lights at RPM_LEDS_START and one more every ~140 rpm; the
 * last one joins at RPM_SHIFT, where they all flash. */
static uint32_t leds_lit(uint32_t rpm)
{
    if (rpm < RPM_LEDS_START) {
        return 0u;
    }
    if (rpm >= RPM_SHIFT) {
        return LED_COUNT;
    }
    return 1u + (rpm - RPM_LEDS_START) * (LED_COUNT - 1u) / (RPM_SHIFT - RPM_LEDS_START);
}

static uint32_t led_colour(uint32_t i, bool on)
{
    if (i < LED_GROUP) {
        return on ? COL_GREEN : COL_GREEN_DARK;
    }
    if (i < 2u * LED_GROUP) {
        return on ? COL_RED : COL_RED_DARK;
    }
    return on ? COL_PURPLE : COL_PURPLE_DK;
}

/* Colour of the highest lit LED, or `none` when they are all off. */
static uint32_t shift_stage_colour(uint32_t rpm, uint32_t none)
{
    const uint32_t lit = leds_lit(rpm);
    if (lit == 0u) {
        return none;
    }
    return rpm >= RPM_SHIFT ? COL_PURPLE : led_colour(lit - 1u, true);
}

static void draw_shift_lights(uint32_t rpm, bool flash_on)
{
    const bool shift = rpm >= RPM_SHIFT;
    const uint32_t lit = shift ? (flash_on ? LED_COUNT : 0u) : leds_lit(rpm);
    const int32_t r = (LED_Y1 - LED_Y0) / 2;

    eve_cmd(BEGIN(RECTS));

    /* Soft glow behind the lit LEDs... */
    eve_cmd(COLOR_A(70));
    eve_cmd(LINE_WIDTH((r + LED_GLOW) * 16));
    for (uint32_t i = 0u; i < lit; i++) {
        const int32_t x0 = ui_spread_x(i, LED_COUNT, LED_X0, LED_X1, LED_GAP);
        const int32_t x1 = ui_spread_x(i + 1u, LED_COUNT, LED_X0, LED_X1, LED_GAP) - LED_GAP;
        eve_color(shift ? COL_PURPLE : led_colour(i, true));
        ui_rect_vertices(x0 - LED_GLOW, LED_Y0 - LED_GLOW, x1 + LED_GLOW, LED_Y1 + LED_GLOW, r + LED_GLOW);
    }
    eve_cmd(COLOR_A(255));

    /* ...then the LEDs. Unlit ones keep a dark tint of their colour. */
    eve_cmd(LINE_WIDTH(r * 16));
    for (uint32_t i = 0u; i < LED_COUNT; i++) {
        const int32_t x0 = ui_spread_x(i, LED_COUNT, LED_X0, LED_X1, LED_GAP);
        const int32_t x1 = ui_spread_x(i + 1u, LED_COUNT, LED_X0, LED_X1, LED_GAP) - LED_GAP;
        eve_color(shift && i < lit ? COL_PURPLE : led_colour(i, i < lit));
        ui_rect_vertices(x0, LED_Y0, x1, LED_Y1, r);
    }
    eve_cmd(END());
}

static void draw_rev_bar(uint32_t rpm)
{
    const uint32_t rpm_per_seg = RPM_MAX / REV_SEGS;

    eve_cmd(LINE_WIDTH(16));
    eve_cmd(BEGIN(RECTS));
    for (uint32_t i = 0u; i < REV_SEGS; i++) {
        const uint32_t seg_rpm = i * rpm_per_seg;
        const bool red = seg_rpm >= RPM_REDLINE;
        const bool on = rpm > seg_rpm + rpm_per_seg / 2u;
        if (on) {
            eve_color(red ? COL_RED : COL_BAR);
        } else {
            eve_color(red ? COL_RED_DARK : COL_TRACK);
        }
        const int32_t x0 = ui_spread_x(i, REV_SEGS, REV_X0, REV_X1, REV_GAP);
        const int32_t x1 = ui_spread_x(i + 1u, REV_SEGS, REV_X0, REV_X1, REV_GAP) - REV_GAP;
        ui_rect_vertices(x0, REV_Y0, x1, REV_Y1, 1);
    }
    eve_cmd(END());

    /* Scale in thousands, centred on the segment boundaries. */
    const uint32_t segs_per_k = REV_SEGS * 1000u / RPM_MAX;
    for (uint32_t k = 1u; k < RPM_MAX / 1000u; k++) {
        const int32_t x = ui_spread_x(k * segs_per_k, REV_SEGS, REV_X0, REV_X1, REV_GAP) - REV_GAP / 2;
        eve_color(k * 1000u >= RPM_REDLINE ? COL_RED : COL_TEXT_FAINT);
        eve_cmd_number((int16_t)x, REV_Y1 + 14, FONT_TINY, OPT_CENTER, (int32_t)k);
    }
}

/* ---------------------------------------------------------------------------
 * Centre row
 * ------------------------------------------------------------------------ */

static void draw_gear(uint8_t gear, uint32_t rpm, bool flash_on)
{
    const bool shift_now = rpm >= RPM_SHIFT && flash_on;

    /* The frame follows the shift lights; at the shift point the box flashes. */
    ui_rect(GEAR_X0, GEAR_Y0, GEAR_X1, GEAR_Y1, shift_stage_colour(rpm, COL_PANEL_EDGE), 10);
    ui_rect(GEAR_X0 + 3, GEAR_Y0 + 3, GEAR_X1 - 3, GEAR_Y1 - 3, shift_now ? COL_PURPLE : COL_PANEL, 8);

    const char gear_str[2] = {gear == 0u ? 'N' : (char)('0' + gear), '\0'};
    eve_color(gear == 0u && !shift_now ? COL_GREEN : COL_TEXT);
    ui_text_heavy((GEAR_X0 + GEAR_X1) / 2, (GEAR_Y0 + GEAR_Y1) / 2, FONT_HUGE, gear_str);
}

static void draw_speed_rpm(uint32_t speed_kph, uint32_t rpm)
{
    eve_color(COL_TEXT);
    eve_cmd_number(SPEED_X, VALUE_Y, FONT_HUGE, OPT_CENTER, (int32_t)speed_kph);
    eve_color(COL_TEXT_DIM);
    eve_cmd_text(SPEED_X, VALUE_Y + 64, FONT_SMALL, OPT_CENTER, "KM/H");

    /* rpm is also on the bar and the shift lights, so it gets the smaller font. */
    eve_color(rpm >= RPM_REDLINE ? COL_RED : COL_TEXT);
    eve_cmd_number(RPM_X, VALUE_Y, FONT_BIG, OPT_CENTER, (int32_t)rpm);
    eve_color(COL_TEXT_DIM);
    eve_cmd_text(RPM_X, VALUE_Y + 64, FONT_SMALL, OPT_CENTER, "RPM");
}

/* The most severe alert, plus how many more there are. Critical ones flash.
 * When everything is normal the slot stays empty. */
static void draw_alert(const vital_status_t *s, uint32_t count, uint32_t now_ms)
{
    const vital_status_t *top = NULL;
    uint32_t active = 0u;
    for (uint32_t i = 0u; i < count; i++) {
        if (s[i].alert != NULL) {
            active++;
            if (top == NULL || s[i].level > top->level) {
                top = &s[i];
            }
        }
    }
    if (top == NULL || (top->level == LEVEL_CRIT && ((now_ms / 250u) & 1u) != 0u)) {
        return;
    }

    char text[24];
    if (active > 1u) {
        snprintf(text, sizeof text, "%s  +%u", top->alert, (unsigned)(active - 1u));
    } else {
        snprintf(text, sizeof text, "%s", top->alert);
    }
    ui_rect(ALERT_X0, ALERT_Y0, ALERT_X1, ALERT_Y1, level_colour(top->level), (ALERT_Y1 - ALERT_Y0) / 2);
    eve_color(COL_BG);
    eve_cmd_text((ALERT_X0 + ALERT_X1) / 2, (ALERT_Y0 + ALERT_Y1) / 2, FONT_SMALL, OPT_CENTER, text);
}

/* ---------------------------------------------------------------------------
 * Vitals tiles
 * ------------------------------------------------------------------------ */

typedef struct {
    const char *label;
    const char *unit;
    bool degree;       /* draw a degree ring in front of the unit */
    const char *value;
    uint32_t fill;     /* bar fill, permille */
    uint32_t zone_lo;  /* part of the bar tinted as the danger zone, permille */
    uint32_t zone_hi;
    vital_status_t status;
} tile_t;

static void draw_tile(uint32_t index, const tile_t *t)
{
    const int32_t x0 = TILE_X0 + (int32_t)index * (TILE_W + TILE_GAP);
    const int32_t x1 = x0 + TILE_W;
    const bool ok = t->status.level == LEVEL_OK;
    const uint32_t colour = level_colour(t->status.level);

    ui_rect(x0, TILE_Y0, x1, TILE_Y1, COL_PANEL, 8);
    ui_rect(x0, TILE_Y0 + 14, x0 + 4, TILE_Y1 - 14, ok ? COL_PANEL_EDGE : colour, 2);

    /* Label and unit */
    eve_color(COL_TEXT_DIM);
    eve_cmd_text((int16_t)(x0 + TILE_PAD), TILE_Y0 + 20, FONT_TINY, OPT_CENTERY, t->label);
    eve_cmd_text((int16_t)(x1 - 16), TILE_Y0 + 20, FONT_TINY, OPT_RIGHTX | OPT_CENTERY, t->unit);
    if (t->degree) { /* the ROM fonts are ASCII only */
        eve_cmd(BEGIN(POINTS));
        eve_cmd(POINT_SIZE(3 * 16));
        eve_cmd(VERTEX2F((x1 - 32) * 16, (TILE_Y0 + 16) * 16));
        eve_color(COL_PANEL);
        eve_cmd(POINT_SIZE(26));
        eve_cmd(VERTEX2F((x1 - 32) * 16, (TILE_Y0 + 16) * 16));
        eve_cmd(END());
    }

    /* Value, and a status word only when there is something to say */
    eve_color(colour);
    eve_cmd_text((int16_t)(x0 + TILE_PAD - 2), TILE_Y0 + 54, FONT_VALUE, OPT_CENTERY, t->value);
    if (t->status.word != NULL) {
        eve_cmd_text((int16_t)(x1 - 16), TILE_Y0 + 58, FONT_SMALL, OPT_RIGHTX | OPT_CENTERY, t->status.word);
    }

    /* Bar, with the danger zone tinted on the track */
    const int32_t bx0 = x0 + TILE_PAD;
    const int32_t bx1 = x1 - 16;
    const int32_t by0 = TILE_Y1 - 22;
    const int32_t by1 = by0 + 6;
    const uint32_t bw = (uint32_t)(bx1 - bx0);
    ui_rect(bx0, by0, bx1, by1, COL_TRACK, 2);
    ui_rect(bx0 + (int32_t)(t->zone_lo * bw / 1000u), by0, bx0 + (int32_t)(t->zone_hi * bw / 1000u), by1,
              COL_RED_DARK, 2);
    ui_rect(bx0, by0, bx0 + (int32_t)(t->fill * bw / 1000u), by1, colour, 2);
}

static void draw_vitals(const dash_data_t *d, uint32_t now_ms)
{
    char coolant[8];
    char fuel[8];
    char battery[8];

    snprintf(coolant, sizeof coolant, "%d", d->coolant_c);
    snprintf(fuel, sizeof fuel, "%u", (unsigned)d->fuel_pct);
    snprintf(battery, sizeof battery, "%u.%u", (unsigned)(d->battery_mv / 1000u),
             (unsigned)((d->battery_mv % 1000u) / 100u));

    const tile_t tiles[TILE_COUNT] = {
        {
            .label = "COOLANT", .unit = "C", .degree = true, .value = coolant,
            .fill = clamp_permille(d->coolant_c, COOLANT_BAR_MIN, COOLANT_BAR_MAX),
            .zone_lo = clamp_permille(COOLANT_HOT_C, COOLANT_BAR_MIN, COOLANT_BAR_MAX),
            .zone_hi = 1000u,
            .status = coolant_status(d->coolant_c),
        },
        {
            .label = "FUEL", .unit = "%", .value = fuel,
            .fill = clamp_permille(d->fuel_pct, 0, 100),
            .zone_lo = 0u,
            .zone_hi = clamp_permille(FUEL_LOW_PCT, 0, 100),
            .status = fuel_status(d->fuel_pct),
        },
        {
            .label = "BATTERY", .unit = "V", .value = battery,
            .fill = clamp_permille(d->battery_mv, BATT_BAR_MIN, BATT_BAR_MAX),
            .zone_lo = 0u,
            .zone_hi = clamp_permille(BATT_LOW_MV, BATT_BAR_MIN, BATT_BAR_MAX),
            .status = battery_status(d->battery_mv, d->rpm),
        },
    };

    vital_status_t status[TILE_COUNT];
    for (uint32_t i = 0u; i < TILE_COUNT; i++) {
        draw_tile(i, &tiles[i]);
        status[i] = tiles[i].status;
    }
    draw_alert(status, TILE_COUNT, now_ms);
}

/* ---------------------------------------------------------------------------
 * Screens
 * ------------------------------------------------------------------------ */

eve_status_t dashboard_draw_splash(const char *fw_version, uint32_t chip_id, uint32_t lights_on)
{
    char line[48];

    eve_cmd_dlstart();
    eve_cmd(CLEAR_COLOR_RGB(0, 0, 0));
    eve_cmd(CLEAR(1, 1, 1));

    logo_draw((SCREEN_W - SPLASH_LOGO_W) / 2, SPLASH_LOGO_Y, SPLASH_LOGO_W, COL_TEXT);
    eve_color(COL_TEXT_DIM);
    eve_cmd_text(SCREEN_W / 2, SPLASH_TITLE_Y, FONT_MEDIUM, OPT_CENTER, "MR2 DASHBOARD");

    /* Start lights: they come on one by one, and go out for the rev sweep. */
    const int32_t x_first = SCREEN_W / 2 - 2 * START_LIGHT_PITCH;
    eve_cmd(BEGIN(POINTS));
    eve_color(COL_RED);
    eve_cmd(COLOR_A(70));
    eve_cmd(POINT_SIZE((START_LIGHT_R + 8) * 16));
    for (uint32_t i = 0u; i < lights_on && i < DASHBOARD_START_LIGHTS; i++) {
        eve_cmd(VERTEX2F((x_first + (int32_t)i * START_LIGHT_PITCH) * 16, START_LIGHT_Y * 16));
    }
    eve_cmd(COLOR_A(255));
    eve_cmd(POINT_SIZE(START_LIGHT_R * 16));
    for (uint32_t i = 0u; i < DASHBOARD_START_LIGHTS; i++) {
        eve_color(i < lights_on ? COL_RED : COL_RED_DARK);
        eve_cmd(VERTEX2F((x_first + (int32_t)i * START_LIGHT_PITCH) * 16, START_LIGHT_Y * 16));
    }
    eve_cmd(END());

    eve_color(COL_TEXT_FAINT);
    snprintf(line, sizeof line, "firmware %s   -   FT8%02X", fw_version, (unsigned)((chip_id >> 8) & 0xFFu));
    eve_cmd_text(SCREEN_W / 2, SCREEN_H - 30, FONT_TINY, OPT_CENTER, line);

    eve_cmd(DISPLAY());
    eve_cmd_swap();
    return eve_cmd_flush();
}

/* One frame of the pre-start guide, or of the main screen when there is no
 * guide step to show or `guide` is false. */
static eve_status_t draw_frame(const dash_data_t *d, bool demo, uint32_t now_ms, bool guide)
{
    const guide_step_t step = guide ? guide_step(d) : GUIDE_NONE;

    eve_cmd_dlstart();
    eve_cmd(CLEAR_COLOR_RGB(0, 0, 0));
    eve_cmd(CLEAR(1, 1, 1));

    if (step != GUIDE_NONE) {
        guide_draw(d, step, now_ms);
    } else {
        const bool flash_on = ((now_ms / 100u) & 1u) == 0u; /* 5 Hz shift flash */
        ui_attach_fonts();
        draw_shift_lights(d->rpm, flash_on);
        draw_rev_bar(d->rpm);
        draw_speed_rpm(d->speed_kph, d->rpm);
        draw_gear(d->gear, d->rpm, flash_on);
        draw_vitals(d, now_ms);
    }

    if (demo) {
        eve_color(COL_TEXT_FAINT);
        eve_cmd_text(SCREEN_W - 16, SCREEN_H - 16, FONT_TINY, OPT_RIGHTX | OPT_CENTERY, "DEMO DATA");
    }

    eve_cmd(DISPLAY());
    eve_cmd_swap();
    return eve_cmd_flush();
}

eve_status_t dashboard_draw(const dash_data_t *d, bool demo, uint32_t now_ms)
{
    return draw_frame(d, demo, now_ms, true);
}

eve_status_t dashboard_rev_sweep(const dash_data_t *base, uint32_t duration_ms)
{
    const uint32_t half = duration_ms / 2u > 0u ? duration_ms / 2u : 1u;
    const uint32_t start = eve_hal_millis();

    for (;;) {
        uint32_t t = eve_hal_millis() - start;
        if (t > 2u * half) {
            t = 2u * half;
        }

        dash_data_t d = *base;
        d.rpm = (uint16_t)(t <= half ? RPM_MAX * t / half : RPM_MAX * (2u * half - t) / half);

        /* One frame in flight at a time: the co-processor's CMD_DLSTART
         * waits for the previous swap, so this runs at the panel's frame rate. */
        eve_status_t st = eve_cmd_wait_idle(100u);
        if (st == EVE_OK) {
            st = draw_frame(&d, false, start + t, false); /* always the gauges */
        }
        if (st != EVE_OK || t >= 2u * half) {
            return st;
        }
    }
}
