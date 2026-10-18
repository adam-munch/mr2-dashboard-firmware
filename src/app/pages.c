#include "pages.h"

#include <stdio.h>
#include <stdlib.h>

#include "can.h"
#include "encoder.h"
#include "eve.h"
#include "menu.h"
#include "ui.h"
#include "version.h"

#define NO_VALUE "--"

/* ---------------------------------------------------------------------------
 * Engine data: six tiles in the style of the main screen's vitals
 *
 *   [ OIL PRESSURE   BAR ]  [ LAMBDA            ]  [ MAP          KPA ]
 *   [ INTAKE AIR      C  ]  [ THROTTLE        % ]  [ IGNITION DEG BTDC ]
 *
 * Monochrome: there are no thresholds here, just the readings. A value the
 * ECU isn't sending (or has no sensor for) shows "--" with an empty bar.
 * ------------------------------------------------------------------------ */

#define TILE_COLS 3u
#define TILE_X0   16
#define TILE_W    248
#define TILE_GAP  12
#define TILE_H    ((MENU_BODY_Y1 - MENU_BODY_Y0 - TILE_GAP) / 2)
#define TILE_PAD  20

typedef struct {
    const char *label;
    const char *unit;
    bool degree;      /* degree ring in front of the unit */
    uint16_t valid;   /* the DASH_VALID_* bit it needs */
    int32_t value;    /* times 10^decimals */
    uint8_t decimals; /* 0..2 */
    int32_t bar_min;  /* bar range, on the same scale as value */
    int32_t bar_max;
} engine_tile_t;

/* `v` with 0, 1 or 2 decimals: format_fixed(..., -55, 1) gives "-5.5". */
static void format_fixed(char *buf, size_t size, int32_t v, uint8_t decimals)
{
    const char *sign = v < 0 ? "-" : "";
    const long mag = labs((long)v);
    if (decimals == 0u) {
        snprintf(buf, size, "%ld", (long)v);
    } else if (decimals == 1u) {
        snprintf(buf, size, "%s%ld.%01ld", sign, mag / 10, mag % 10);
    } else {
        snprintf(buf, size, "%s%ld.%02ld", sign, mag / 100, mag % 100);
    }
}

static void draw_engine_tile(uint32_t index, const engine_tile_t *t, const dash_data_t *d)
{
    const int32_t x0 = TILE_X0 + (int32_t)(index % TILE_COLS) * (TILE_W + TILE_GAP);
    const int32_t x1 = x0 + TILE_W;
    const int32_t y0 = MENU_BODY_Y0 + (int32_t)(index / TILE_COLS) * (TILE_H + TILE_GAP);
    const int32_t y1 = y0 + TILE_H;
    const bool known = dash_valid(d, t->valid);

    ui_rect(x0, y0, x1, y1, COL_PANEL, 8);
    eve_color(COL_TEXT_DIM);
    eve_cmd_text((int16_t)(x0 + TILE_PAD), (int16_t)(y0 + 20), FONT_TINY, OPT_CENTERY, t->label);
    eve_cmd_text((int16_t)(x1 - 16), (int16_t)(y0 + 20), FONT_TINY, OPT_RIGHTX | OPT_CENTERY, t->unit);
    if (t->degree) {
        ui_degree(x1 - 32, y0 + 16, COL_TEXT_DIM, COL_PANEL);
    }

    char text[24];
    if (known) {
        format_fixed(text, sizeof text, t->value, t->decimals);
    } else {
        snprintf(text, sizeof text, "%s", NO_VALUE);
    }
    eve_color(known ? COL_TEXT : COL_TEXT_DIM);
    eve_cmd_text((int16_t)(x0 + TILE_PAD - 2), (int16_t)(y0 + 70), FONT_BIG, OPT_CENTERY, text);

    const int32_t bx0 = x0 + TILE_PAD;
    const int32_t bx1 = x1 - 16;
    const int32_t by0 = y1 - 22;
    ui_rect(bx0, by0, bx1, by0 + 6, COL_TRACK, 2);
    if (known && t->bar_max > t->bar_min) {
        int32_t fill = (t->value - t->bar_min) * (bx1 - bx0) / (t->bar_max - t->bar_min);
        fill = fill < 0 ? 0 : (fill > bx1 - bx0 ? bx1 - bx0 : fill);
        ui_rect(bx0, by0, bx0 + fill, by0 + 6, COL_TEXT, 2);
    }
}

void pages_draw_engine(const dash_data_t *d, uint32_t now_ms)
{
    const engine_tile_t tiles[] = {
        {"OIL PRESSURE", "BAR", false, DASH_VALID_OIL_PRESS, (d->oil_kpa + 5) / 10, 1u, 0, 60},
        {"LAMBDA", "", false, DASH_VALID_LAMBDA, (d->lambda_x1000 + 5) / 10, 2u, 60, 140},
        {"MAP", "KPA", false, DASH_VALID_INTAKE, d->map_kpa, 0u, 0, 250},
        {"INTAKE AIR", "C", true, DASH_VALID_INTAKE, d->intake_c, 0u, 0, 80},
        {"THROTTLE", "%", false, DASH_VALID_THROTTLE, d->throttle_pct, 0u, 0, 100},
        {"IGNITION", "DEG BTDC", false, DASH_VALID_TIMING, d->timing_deg_x10, 1u, -100, 500},
    };

    ui_attach_fonts();
    for (uint32_t i = 0u; i < sizeof tiles / sizeof tiles[0]; i++) {
        draw_engine_tile(i, &tiles[i], d);
    }
}

/* ---------------------------------------------------------------------------
 * Diagnostics: two cards of label / value rows
 * ------------------------------------------------------------------------ */

#define DIAG_COL_W  376
#define DIAG_GAP    16
#define DIAG_HEAD_Y (MENU_BODY_Y0 + 24)
#define DIAG_ROW_Y  (MENU_BODY_Y0 + 60) /* first row */
#define DIAG_ROW_H  34
#define DIAG_ROWS   7

typedef struct {
    const char *label;
    char value[20];
    uint32_t colour;
} diag_row_t;

static void draw_card(uint32_t col, const char *title, const diag_row_t *rows, size_t n)
{
    const int32_t x0 = TILE_X0 + (int32_t)col * (DIAG_COL_W + DIAG_GAP);
    const int32_t x1 = x0 + DIAG_COL_W;

    ui_rect(x0, MENU_BODY_Y0, x1, MENU_BODY_Y1, COL_PANEL, 8);
    eve_color(COL_TEXT_DIM);
    eve_cmd_text((int16_t)(x0 + TILE_PAD), DIAG_HEAD_Y, FONT_TINY, OPT_CENTERY, title);
    for (size_t i = 0u; i < n; i++) {
        const int16_t y = (int16_t)(DIAG_ROW_Y + (int32_t)i * DIAG_ROW_H);
        eve_color(COL_TEXT_DIM);
        eve_cmd_text((int16_t)(x0 + TILE_PAD), y, FONT_SMALL, OPT_CENTERY, rows[i].label);
        eve_color(rows[i].colour);
        eve_cmd_text((int16_t)(x1 - TILE_PAD), y, FONT_SMALL, OPT_RIGHTX | OPT_CENTERY, rows[i].value);
    }
}

/* A counter. One that counts trouble turns amber once it's above 0. */
static void count_row(diag_row_t *row, const char *label, uint32_t n, bool trouble)
{
    row->label = label;
    snprintf(row->value, sizeof row->value, "%lu", (unsigned long)n);
    row->colour = trouble && n > 0u ? COL_AMBER : COL_TEXT;
}

void pages_draw_diagnostics(const dash_data_t *d, uint32_t now_ms)
{
    diag_row_t dash[6];
    const uint32_t s = now_ms / 1000u;

    dash[0].label = "FIRMWARE";
    snprintf(dash[0].value, sizeof dash[0].value, "%s", FW_VERSION);
    dash[0].colour = COL_TEXT;
    dash[1].label = "DISPLAY";
    snprintf(dash[1].value, sizeof dash[1].value, "FT8%02X", (unsigned)((eve_chip_id() >> 8) & 0xFFu));
    dash[1].colour = COL_TEXT;
    dash[2].label = "UPTIME";
    snprintf(dash[2].value, sizeof dash[2].value, "%lu:%02lu:%02lu", (unsigned long)(s / 3600u),
             (unsigned long)(s / 60u % 60u), (unsigned long)(s % 60u));
    dash[2].colour = COL_TEXT;

    /* Same words and colours as the main screen's status line. */
    static const struct {
        const char *text;
        uint32_t colour;
    } k_link[] = {
        [DASH_LINK_OK] = {"RECEIVING", COL_TEXT},       [DASH_LINK_DEMO] = {"DEMO DATA", COL_TEXT_FAINT},
        [DASH_LINK_WAITING] = {"WAITING", COL_AMBER}, [DASH_LINK_LOST] = {"NO DATA", COL_RED},
        [DASH_LINK_CAN_FAULT] = {"CAN FAULT", COL_RED},
    };
    dash[3].label = "ECU DATA";
    snprintf(dash[3].value, sizeof dash[3].value, "%s", k_link[d->link].text);
    dash[3].colour = k_link[d->link].colour;

    /* The knob's raw state: each click should move the count by
     * ENCODER_COUNTS_PER_DETENT, up for clockwise. */
    dash[4].label = "KNOB COUNT";
    snprintf(dash[4].value, sizeof dash[4].value, "%u", (unsigned)encoder_hw_count());
    dash[4].colour = COL_TEXT;
    dash[5].label = "KNOB SWITCH";
    snprintf(dash[5].value, sizeof dash[5].value, "%s", encoder_hw_switch() ? "PRESSED" : "RELEASED");
    dash[5].colour = COL_TEXT;

    can_stats_t st;
    can_get_stats(&st);
    static const struct {
        const char *text;
        uint32_t colour;
    } k_state[] = {
        [CAN_STATE_STOPPED] = {"STOPPED", COL_RED},   [CAN_STATE_ACTIVE] = {"ACTIVE", COL_TEXT},
        [CAN_STATE_WARNING] = {"WARNING", COL_AMBER}, [CAN_STATE_PASSIVE] = {"PASSIVE", COL_AMBER},
        [CAN_STATE_BUS_OFF] = {"BUS OFF", COL_RED},
    };
    diag_row_t can[DIAG_ROWS];
    can[0].label = "STATE";
    snprintf(can[0].value, sizeof can[0].value, "%s", k_state[st.state].text);
    can[0].colour = k_state[st.state].colour;
    can[1].label = "ERRORS TX / RX";
    snprintf(can[1].value, sizeof can[1].value, "%u / %u", (unsigned)st.tx_errors, (unsigned)st.rx_errors);
    can[1].colour = st.tx_errors > 0u || st.rx_errors > 0u ? COL_AMBER : COL_TEXT;
    count_row(&can[2], "RECEIVED", st.rx_frames, false);
    count_row(&can[3], "LOST", st.rx_lost, true);
    count_row(&can[4], "SENT", st.tx_frames, false);
    count_row(&can[5], "NOT SENT", st.tx_dropped, true);
    count_row(&can[6], "BUS-OFF", st.bus_off_count, true);

    draw_card(0u, "DASH", dash, sizeof dash / sizeof dash[0]);
    draw_card(1u, "CAN BUS", can, DIAG_ROWS);
}
