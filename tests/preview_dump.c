/*
 * Renders the dashboard screens against the mock EVE and writes each
 * captured co-processor stream to <outdir>/<name>.bin. Feed those to
 * tools/render_preview.py to get approximate PNG previews.
 */
#include <stdio.h>

#include "dashboard.h"
#include "eve_panels.h"
#include "mock_eve.h"
#include "version.h"

static int dump(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s.bin", dir, name);
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return 1;
    }
    fwrite(g_mock.fifo, 1, g_mock.fifo_len, f);
    fclose(f);
    printf("wrote %s (%zu bytes)\n", path, g_mock.fifo_len);
    g_mock.fifo_len = 0;
    return 0;
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    int err = 0;

    mock_eve_reset();
    if (eve_init(&eve_panel_nhd_70_800480ft_csxp) != EVE_OK) {
        fprintf(stderr, "eve_init failed against the mock\n");
        return 1;
    }
    g_mock.fifo_len = 0;

    dashboard_draw_splash(FW_VERSION, 0x00011208u, 3u);
    err |= dump(dir, "splash");

    /* Cruising: everything normal, so the screen stays monochrome. */
    const dash_data_t cruise = {.rpm = 3450, .speed_kph = 96, .coolant_c = 89, .fuel_pct = 62,
                                .battery_mv = 14200, .gear = 3, .valid = DASH_VALID_ALL,
                                .link = DASH_LINK_OK};
    dashboard_draw(&cruise, 0);
    err |= dump(dir, "dashboard");

    /* Pulling through 2nd: shift lights building. */
    const dash_data_t pull = {.rpm = 5850, .speed_kph = 81, .coolant_c = 91, .fuel_pct = 62,
                              .battery_mv = 14200, .gear = 2, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK};
    dashboard_draw(&pull, 0);
    err |= dump(dir, "dashboard_shift_lights");

    /* At the shift point (flash on), with warnings and both ECU lamps on. */
    const dash_data_t redline = {.rpm = 6900, .speed_kph = 188, .coolant_c = 108, .fuel_pct = 9,
                                 .battery_mv = 11600, .gear = 4, .valid = DASH_VALID_ALL,
                                 .warnings = DASH_WARN_CHECK_ENGINE | DASH_WARN_LIMITER,
                                 .link = DASH_LINK_OK};
    dashboard_draw(&redline, 0);
    err |= dump(dir, "dashboard_warnings");

    /* Cold start, idling in neutral. */
    const dash_data_t cold = {.rpm = 1150, .speed_kph = 0, .coolant_c = 31, .fuel_pct = 72,
                              .battery_mv = 13900, .gear = 0, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK};
    dashboard_draw(&cold, 0);
    err |= dump(dir, "dashboard_cold");

    /* The ECU stopped sending (maybe on the move): the gauges stay, with "--". */
    const dash_data_t no_ecu = {.valid = 0, .link = DASH_LINK_LOST};
    dashboard_draw(&no_ecu, 0);
    err |= dump(dir, "dashboard_no_ecu");

    /* Pre-start guide with every input wired up: one screen per step. */
    dash_data_t parked = {.coolant_c = 24, .fuel_pct = 72, .battery_mv = 12500, .gear = 1,
                          .valid = DASH_VALID_ALL | DASH_VALID_INPUTS, .link = DASH_LINK_OK,
                          .estop_active = true};
    dashboard_draw(&parked, 0);
    err |= dump(dir, "guide_estop");

    parked.estop_active = false;
    dashboard_draw(&parked, 0);
    err |= dump(dir, "guide_wheel");

    parked.wheel_connected = true; /* still in gear, clutch up: start won't work */
    dashboard_draw(&parked, 0);
    err |= dump(dir, "guide_start_blocked");

    parked.clutch_held = true;
    dashboard_draw(&parked, 300);
    err |= dump(dir, "guide_start_ready");

    /* Power-up today: the start inputs aren't wired and the ECU hasn't spoken. */
    const dash_data_t power_up = {.valid = 0, .link = DASH_LINK_WAITING};
    dashboard_draw(&power_up, 0);
    err |= dump(dir, "guide_unwired");

    return err;
}
