/*
 * MR2 dashboard firmware: STM32G0B1CBT6 driving a Newhaven
 * NHD-7.0-800480FT-CSXP (FT812 EVE) 7" display, fed by a rusEFI ECU
 * (uaEFI) over CAN.
 *
 * Boot: splash -> backlight fade-in -> tach needle sweep -> live screen.
 * Values come from the ECU's CAN broadcast (ecu_link.c), or from a
 * built-in simulation when built with DASH_DEMO_DATA=1. If the display
 * stops responding, it is re-initialised once a second; the CAN link keeps
 * running meanwhile.
 */
#include <stdio.h>

#include "board.h"
#include "can.h"
#include "clock.h"
#include "dashboard.h"
#include "demo_data.h"
#include "ecu_link.h"
#include "eve.h"
#include "eve_panels.h"
#include "systick.h"
#include "version.h"

#ifndef DASH_DEMO_DATA
#define DASH_DEMO_DATA 0 /* 1: show the simulation instead of the ECU's data (set by CMake) */
#endif

#define FRAME_PERIOD_MS    16u  /* ~60 fps, matching the panel's 61.6 Hz */
#define BACKLIGHT_DUTY     128u /* 0..128. Full brightness draws ~760 mA from VBL at 3.3 V */
#define BACKLIGHT_FADE_MS  400u
#define SPLASH_HOLD_MS     800u
#define NEEDLE_SWEEP_MS    1500u
#define HEARTBEAT_MS       500u

/* Wait, keeping the CAN link serviced. */
static void wait_ms(uint32_t ms)
{
    const uint32_t start = millis();
    /* `<=` so a partial first tick never shortens the wait, as delay_ms(). */
    while ((uint32_t)(millis() - start) <= ms) {
        ecu_link_poll(millis());
    }
}

static void backlight_fade_in(uint8_t target, uint32_t duration_ms)
{
    for (uint32_t t = 0u; t < duration_ms; t += 10u) {
        eve_backlight_set((uint8_t)(target * t / duration_ms));
        wait_ms(10u);
    }
    eve_backlight_set(target);
}

/* Print ECU link changes on the debug UART, for bring-up. */
static void log_link_state(uint32_t now)
{
    static int s_last = -1;
    const dash_link_t link = ecu_link_state(now);
    if ((int)link == s_last) {
        return;
    }
    s_last = (int)link;

    can_stats_t st;
    can_get_stats(&st);
    switch (link) {
    case DASH_LINK_OK:
        printf("ECU: receiving data\n");
        break;
    case DASH_LINK_WAITING:
        printf("ECU: waiting for data at 0x%03lX\n", (unsigned long)ECU_RUSEFI_BASE_ID);
        break;
    case DASH_LINK_LOST:
        printf("ECU: no data for %lu ms (CAN state %d, TEC %u, REC %u, lost %lu)\n",
               (unsigned long)ECU_DATA_TIMEOUT_MS, (int)st.state, (unsigned)st.tx_errors,
               (unsigned)st.rx_errors, (unsigned long)st.rx_lost);
        break;
    case DASH_LINK_CAN_FAULT:
        printf("CAN: fault (state %d, bus-off count %lu)\n", (int)st.state, (unsigned long)st.bus_off_count);
        break;
    case DASH_LINK_DEMO:
        break;
    }
}

static void get_data(dash_data_t *d, uint32_t now)
{
    if (DASH_DEMO_DATA) {
        demo_data_update(d, now);
    } else {
        ecu_link_get_dash(d, now);
    }
}

/* Runs the dashboard. Returns only if the display reports an error. */
static eve_status_t run_dashboard(void)
{
    dash_data_t data;
    if (DASH_DEMO_DATA) {
        demo_data_init(&data, millis());
    } else {
        ecu_link_get_dash(&data, millis());
    }

    eve_status_t st = dashboard_draw_splash(FW_VERSION, eve_chip_id());
    if (st != EVE_OK) {
        return st;
    }
    ecu_link_set_display_ok(true);
    backlight_fade_in(BACKLIGHT_DUTY, BACKLIGHT_FADE_MS);
    wait_ms(SPLASH_HOLD_MS);

    st = dashboard_needle_sweep(&data, NEEDLE_SWEEP_MS);
    if (st != EVE_OK) {
        return st;
    }

    uint32_t last_frame = millis();
    uint32_t last_beat = last_frame;

    for (;;) {
        const uint32_t now = millis();

        ecu_link_poll(now);
        log_link_state(now);

        if ((uint32_t)(now - last_beat) >= HEARTBEAT_MS) {
            last_beat = now;
            board_led_toggle();
        }

        if ((uint32_t)(now - last_frame) < FRAME_PERIOD_MS) {
            continue;
        }
        last_frame = now;

        get_data(&data, now);

        /* Let the previous frame finish before queueing the next one. */
        st = eve_cmd_wait_idle(100u);
        if (st == EVE_OK) {
            st = dashboard_draw(&data, now);
        }
        if (st != EVE_OK) {
            return st;
        }
    }
}

int main(void)
{
    board_init();

    printf("\nMR2 dashboard firmware %s%s\n", FW_VERSION, DASH_DEMO_DATA ? " (demo data)" : "");
    printf("SYSCLK %lu Hz from %s\n", (unsigned long)SystemCoreClock,
           clock_using_hse() ? "crystal" : "HSI16");
    if (BOARD_HSE_HZ != 0u && !clock_using_hse()) {
        printf("Clock: crystal did not start, CAN timing may be off\n");
    }

    if (ecu_link_init(millis())) {
        printf("CAN: %lu kbit/s, rusEFI broadcast at 0x%03lX, dash status at 0x%03lX\n",
               (unsigned long)(ECU_CAN_BITRATE / 1000u), (unsigned long)ECU_RUSEFI_BASE_ID,
               (unsigned long)DASH_TX_ID);
    } else {
        printf("CAN: controller did not start\n");
    }

    for (;;) {
        printf("EVE: initialising\n");
        eve_status_t st = eve_init(&eve_panel_nhd_70_800480ft_csxp);
        if (st == EVE_OK) {
            const uint32_t id = eve_chip_id();
            printf("EVE: FT8%02X ready (chip id 0x%08lX)\n", (unsigned)((id >> 8) & 0xFFu),
                   (unsigned long)id);
            st = run_dashboard();
        }
        ecu_link_set_display_ok(false);
        printf("EVE: %s, retrying in 1 s\n", eve_status_str(st));

        /* Fast LED blink while the display is down. */
        for (uint32_t i = 0u; i < 10u; i++) {
            board_led_toggle();
            wait_ms(100u);
        }
    }
}
