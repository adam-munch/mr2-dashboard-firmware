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

    dashboard_draw_splash(FW_VERSION, 0x00011208u);
    err |= dump(dir, "splash");

    const dash_data_t cruise = {.rpm = 3450, .speed_kph = 96, .coolant_c = 89, .fuel_pct = 62,
                                .battery_mv = 14200, .gear = 3, .valid = DASH_VALID_ALL,
                                .link = DASH_LINK_OK};
    dashboard_draw(&cruise, 0);
    err |= dump(dir, "dashboard");

    const dash_data_t redline = {.rpm = 6900, .speed_kph = 188, .coolant_c = 108, .fuel_pct = 9,
                                 .battery_mv = 11800, .gear = 4, .valid = DASH_VALID_ALL,
                                 .warnings = DASH_WARN_CHECK_ENGINE | DASH_WARN_LIMITER,
                                 .link = DASH_LINK_OK};
    dashboard_draw(&redline, 0);
    err |= dump(dir, "dashboard_warnings");

    const dash_data_t no_ecu = {.valid = 0, .link = DASH_LINK_LOST};
    dashboard_draw(&no_ecu, 0);
    err |= dump(dir, "dashboard_no_ecu");

    return err;
}
