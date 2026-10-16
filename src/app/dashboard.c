#include "dashboard.h"

#include <stdio.h>

#include "eve_hal.h"
#include "fixmath.h"

#define SCREEN_W 800
#define SCREEN_H 480

/* Colours (0xRRGGBB) */
#define COL_BG_TOP    0x0B1622u
#define COL_BG_BOTTOM 0x000000u
#define COL_DIAL_RIM  0x2A3644u
#define COL_DIAL_FACE 0x0E161Fu
#define COL_TICK      0xE8EEF2u
#define COL_REDLINE   0xE0301Eu
#define COL_NEEDLE    0xFF6A1Au
#define COL_TEXT      0xFFFFFFu
#define COL_TEXT_DIM  0x8A98A8u
#define COL_TRACK     0x1C2733u
#define COL_OK        0x3CCB6Eu
#define COL_WARN      0xF0B323u
#define COL_COLD      0x3D8BFFu

/* Fonts: 26..31 are the FT81x anti-aliased ROM fonts, smallest to largest. */
#define FONT_SMALL    26
#define FONT_LABEL    27
#define FONT_MEDIUM   28
#define FONT_DIAL     30
#define FONT_LARGE    31
#define FONT_HUGE     1  /* bitmap handle that ROM font 34 is attached to */
#define FONT_HUGE_ROM 34

/* Tachometer: 270 degree dial, 0 rpm at bottom-left, clockwise. */
#define TACH_CX          255
#define TACH_CY          245
#define TACH_R           215
#define TACH_MAX_RPM     8000u
#define TACH_REDLINE_RPM 7000u
#define TACH_SHIFT_RPM   6500u
#define TACH_ANGLE_ZERO  ANGLE_DEG(225)
#define TACH_SWEEP       ANGLE_DEG(270)

/* Right-hand panel */
#define PANEL_X 525
#define PANEL_W 255

/* ---------------------------------------------------------------------------
 * Drawing helpers
 * ------------------------------------------------------------------------ */

/* Filled rectangle with rounded corners, outer edge at the given pixels. */
static void draw_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb, int32_t radius)
{
    /* RECTS grows each corner outwards by LINE_WIDTH, so inset by the radius. */
    if (x1 - x0 < 2 * radius || y1 - y0 < 2 * radius) {
        return;
    }
    eve_color(rgb);
    eve_cmd(LINE_WIDTH(radius * 16));
    eve_cmd(BEGIN(RECTS));
    eve_cmd(VERTEX2F((x0 + radius) * 16, (y0 + radius) * 16));
    eve_cmd(VERTEX2F((x1 - radius) * 16, (y1 - radius) * 16));
    eve_cmd(END());
}

static uint32_t tach_angle(uint32_t rpm)
{
    if (rpm > TACH_MAX_RPM) {
        rpm = TACH_MAX_RPM;
    }
    /* Clockwise on screen = decreasing angle. */
    return (TACH_ANGLE_ZERO + ANGLE_FULL - rpm * TACH_SWEEP / TACH_MAX_RPM) % ANGLE_FULL;
}

/* Point `radius` pixels from the dial centre, in 1/16 pixel units. */
static void tach_polar16(uint32_t angle, int32_t radius, int32_t *x16, int32_t *y16)
{
    *x16 = TACH_CX * 16 + radius * 16 * icos_q14(angle) / 16384;
    *y16 = TACH_CY * 16 - radius * 16 * isin_q14(angle) / 16384;
}

static void tach_vertex(uint32_t angle, int32_t radius)
{
    int32_t x16;
    int32_t y16;
    tach_polar16(angle, radius, &x16, &y16);
    eve_cmd(VERTEX2F(x16, y16));
}

/* ---------------------------------------------------------------------------
 * Screen elements
 * ------------------------------------------------------------------------ */

static void draw_tach(uint32_t rpm, uint8_t gear)
{
    /* Dial face with a rim. */
    eve_cmd(BEGIN(POINTS));
    eve_color(COL_DIAL_RIM);
    eve_cmd(POINT_SIZE((TACH_R + 6) * 16));
    eve_cmd(VERTEX2F(TACH_CX * 16, TACH_CY * 16));
    eve_color(COL_DIAL_FACE);
    eve_cmd(POINT_SIZE((TACH_R + 2) * 16));
    eve_cmd(VERTEX2F(TACH_CX * 16, TACH_CY * 16));
    eve_cmd(END());

    /* Ticks: major every 1000 rpm, minor every 250. */
    eve_cmd(BEGIN(LINES));
    for (uint32_t r = 0u; r <= TACH_MAX_RPM; r += 250u) {
        const bool major = (r % 1000u) == 0u;
        const uint32_t a = tach_angle(r);
        eve_color(r >= TACH_REDLINE_RPM ? COL_REDLINE : COL_TICK);
        eve_cmd(LINE_WIDTH(major ? 24 : 10)); /* half-width in 1/16 px */
        tach_vertex(a, TACH_R - (major ? 28 : 14));
        tach_vertex(a, TACH_R - 4);
    }
    eve_cmd(END());

    /* Red band from the redline to the stop. */
    eve_color(COL_REDLINE);
    eve_cmd(LINE_WIDTH(40));
    eve_cmd(BEGIN(LINE_STRIP));
    for (uint32_t r = TACH_REDLINE_RPM; r <= TACH_MAX_RPM; r += 50u) {
        tach_vertex(tach_angle(r), TACH_R - 2);
    }
    eve_cmd(END());

    /* Numerals 0..8. */
    for (uint32_t k = 0u; k <= TACH_MAX_RPM / 1000u; k++) {
        int32_t x16;
        int32_t y16;
        tach_polar16(tach_angle(k * 1000u), TACH_R - 58, &x16, &y16);
        eve_color(k * 1000u >= TACH_REDLINE_RPM ? COL_REDLINE : COL_TEXT);
        eve_cmd_number((int16_t)(x16 / 16), (int16_t)(y16 / 16), FONT_DIAL, OPT_CENTER, (int32_t)k);
    }

    eve_color(COL_TEXT_DIM);
    eve_cmd_text(TACH_CX, TACH_CY - 80, FONT_SMALL, OPT_CENTER, "x1000 r/min");

    /* Digital rpm and gear in the open bottom of the dial. */
    eve_color(COL_TEXT);
    eve_cmd_number(TACH_CX, TACH_CY + 85, FONT_LARGE, OPT_CENTER, (int32_t)rpm);

    const char gear_str[2] = {gear == 0u ? 'N' : (char)('0' + gear), '\0'};
    draw_rect(TACH_CX - 26, TACH_CY + 122, TACH_CX + 26, TACH_CY + 178, COL_TRACK, 6);
    eve_color(COL_WARN);
    eve_cmd_text(TACH_CX, TACH_CY + 150, FONT_LARGE, OPT_CENTER, gear_str);

    /* Needle with a short tail, then the hub on top. */
    const uint32_t a = tach_angle(rpm);
    eve_color(COL_NEEDLE);
    eve_cmd(LINE_WIDTH(28));
    eve_cmd(BEGIN(LINES));
    tach_vertex((a + ANGLE_FULL / 2u) % ANGLE_FULL, 22);
    tach_vertex(a, TACH_R - 18);
    eve_cmd(END());

    eve_cmd(BEGIN(POINTS));
    eve_color(COL_DIAL_RIM);
    eve_cmd(POINT_SIZE(18 * 16));
    eve_cmd(VERTEX2F(TACH_CX * 16, TACH_CY * 16));
    eve_color(COL_NEEDLE);
    eve_cmd(POINT_SIZE(6 * 16));
    eve_cmd(VERTEX2F(TACH_CX * 16, TACH_CY * 16));
    eve_cmd(END());
}

static void draw_shift_light(uint32_t rpm, uint32_t now_ms)
{
    const bool lit = rpm >= TACH_SHIFT_RPM && ((now_ms / 100u) & 1u) == 0u; /* 5 Hz flash */

    draw_rect(PANEL_X, 18, PANEL_X + PANEL_W, 46, lit ? COL_REDLINE : COL_TRACK, 4);
    eve_color(lit ? COL_TEXT : COL_TEXT_DIM);
    eve_cmd_text(PANEL_X + PANEL_W / 2, 32, FONT_LABEL, OPT_CENTER, "SHIFT");
}

static void draw_speed(uint32_t speed_kph)
{
    eve_color(COL_TEXT);
    eve_cmd_number(PANEL_X + PANEL_W / 2, 130, FONT_HUGE, OPT_CENTER, (int32_t)speed_kph);
    eve_color(COL_TEXT_DIM);
    eve_cmd_text(PANEL_X + PANEL_W / 2, 200, FONT_MEDIUM, OPT_CENTER, "km/h");
}

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

/* Label + value on one line, bar graph underneath. */
static void draw_bar(int32_t y, const char *label, const char *value, uint32_t permille, uint32_t rgb)
{
    eve_color(COL_TEXT_DIM);
    eve_cmd_text(PANEL_X, (int16_t)(y + 12), FONT_LABEL, OPT_CENTERY, label);
    eve_color(COL_TEXT);
    eve_cmd_text(PANEL_X + PANEL_W, (int16_t)(y + 12), FONT_MEDIUM, OPT_RIGHTX | OPT_CENTERY, value);

    const int32_t by = y + 30;
    draw_rect(PANEL_X, by, PANEL_X + PANEL_W, by + 14, COL_TRACK, 4);
    draw_rect(PANEL_X, by, PANEL_X + (int32_t)(permille * PANEL_W / 1000u), by + 14, rgb, 4);
}

static void draw_side_gauges(const dash_data_t *d)
{
    char value[16];

    snprintf(value, sizeof value, "%d C", d->coolant_c);
    const uint32_t coolant_rgb = d->coolant_c < 60 ? COL_COLD : (d->coolant_c > 105 ? COL_REDLINE : COL_OK);
    draw_bar(250, "COOLANT", value, clamp_permille(d->coolant_c, 40, 120), coolant_rgb);

    snprintf(value, sizeof value, "%u %%", (unsigned)d->fuel_pct);
    draw_bar(320, "FUEL", value, clamp_permille(d->fuel_pct, 0, 100), d->fuel_pct < 15u ? COL_WARN : COL_OK);

    snprintf(value, sizeof value, "%u.%u V", (unsigned)(d->battery_mv / 1000u),
             (unsigned)((d->battery_mv % 1000u) / 100u));
    const bool batt_ok = d->battery_mv >= 12000u && d->battery_mv <= 15000u;
    draw_bar(390, "BATTERY", value, clamp_permille(d->battery_mv, 10000, 16000), batt_ok ? COL_OK : COL_WARN);
}

/* ---------------------------------------------------------------------------
 * Screens
 * ------------------------------------------------------------------------ */

eve_status_t dashboard_draw_splash(const char *fw_version, uint32_t chip_id)
{
    char line[48];

    eve_cmd_dlstart();
    eve_cmd(CLEAR_COLOR_RGB(0, 0, 0));
    eve_cmd(CLEAR(1, 1, 1));
    eve_cmd_romfont(FONT_HUGE, FONT_HUGE_ROM);

    eve_color(COL_TEXT);
    eve_cmd_text(SCREEN_W / 2, 200, FONT_HUGE, OPT_CENTER, "MR2");
    draw_rect(SCREEN_W / 2 - 110, 262, SCREEN_W / 2 + 110, 266, COL_NEEDLE, 1);
    eve_color(COL_TEXT_DIM);
    eve_cmd_text(SCREEN_W / 2, 300, FONT_MEDIUM, OPT_CENTER, "DASHBOARD");

    snprintf(line, sizeof line, "firmware %s   -   FT8%02X", fw_version, (unsigned)((chip_id >> 8) & 0xFFu));
    eve_cmd_text(SCREEN_W / 2, SCREEN_H - 30, FONT_SMALL, OPT_CENTER, line);

    eve_cmd(DISPLAY());
    eve_cmd_swap();
    return eve_cmd_flush();
}

eve_status_t dashboard_draw(const dash_data_t *d, bool demo, uint32_t now_ms)
{
    eve_cmd_dlstart();
    eve_cmd(CLEAR_COLOR_RGB(0, 0, 0));
    eve_cmd(CLEAR(1, 1, 1));
    eve_cmd_gradient(0, 0, COL_BG_TOP, 0, SCREEN_H, COL_BG_BOTTOM);
    eve_cmd_romfont(FONT_HUGE, FONT_HUGE_ROM);

    draw_tach(d->rpm, d->gear);
    draw_shift_light(d->rpm, now_ms);
    draw_speed(d->speed_kph);
    draw_side_gauges(d);

    if (demo) {
        eve_color(COL_TEXT_DIM);
        eve_cmd_text(SCREEN_W - 12, SCREEN_H - 14, FONT_SMALL, OPT_RIGHTX | OPT_CENTERY, "DEMO DATA");
    }

    eve_cmd(DISPLAY());
    eve_cmd_swap();
    return eve_cmd_flush();
}

eve_status_t dashboard_needle_sweep(const dash_data_t *base, uint32_t duration_ms)
{
    const uint32_t half = duration_ms / 2u > 0u ? duration_ms / 2u : 1u;
    const uint32_t start = eve_hal_millis();

    for (;;) {
        uint32_t t = eve_hal_millis() - start;
        if (t > 2u * half) {
            t = 2u * half;
        }

        dash_data_t d = *base;
        d.rpm = (uint16_t)(t <= half ? TACH_MAX_RPM * t / half : TACH_MAX_RPM * (2u * half - t) / half);

        /* One frame in flight at a time: the co-processor's CMD_DLSTART
         * waits for the previous swap, so this runs at the panel's frame rate. */
        eve_status_t st = eve_cmd_wait_idle(100u);
        if (st == EVE_OK) {
            st = dashboard_draw(&d, false, start + t);
        }
        if (st != EVE_OK || t >= 2u * half) {
            return st;
        }
    }
}
