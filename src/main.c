/*
 * MR2 dashboard firmware: STM32G0B1CBT6 driving a Newhaven
 * NHD-7.0-800480FT-CSXP (FT812 EVE) 7" display.
 *
 * Boot: splash -> backlight fade-in -> tach needle sweep -> live screen.
 * Values come from a built-in simulation until real inputs are wired up.
 * If the display stops responding, it is re-initialised once a second.
 */
#include <stdio.h>

#include "board.h"
#include "dashboard.h"
#include "demo_data.h"
#include "eve.h"
#include "eve_panels.h"
#include "systick.h"
#include "version.h"

#define FRAME_PERIOD_MS    16u  /* ~60 fps, matching the panel's 61.6 Hz */
#define BACKLIGHT_DUTY     128u /* 0..128. Full brightness draws ~760 mA from VBL at 3.3 V */
#define BACKLIGHT_FADE_MS  400u
#define SPLASH_HOLD_MS     800u
#define NEEDLE_SWEEP_MS    1500u
#define HEARTBEAT_MS       500u

static void backlight_fade_in(uint8_t target, uint32_t duration_ms)
{
    for (uint32_t t = 0u; t < duration_ms; t += 10u) {
        eve_backlight_set((uint8_t)(target * t / duration_ms));
        delay_ms(10u);
    }
    eve_backlight_set(target);
}

/* Runs the dashboard. Returns only if the display reports an error. */
static eve_status_t run_dashboard(void)
{
    dash_data_t data;
    demo_data_init(&data, millis());

    eve_status_t st = dashboard_draw_splash(FW_VERSION, eve_chip_id());
    if (st != EVE_OK) {
        return st;
    }
    backlight_fade_in(BACKLIGHT_DUTY, BACKLIGHT_FADE_MS);
    delay_ms(SPLASH_HOLD_MS);

    st = dashboard_needle_sweep(&data, NEEDLE_SWEEP_MS);
    if (st != EVE_OK) {
        return st;
    }

    uint32_t last_frame = millis();
    uint32_t last_beat = last_frame;

    for (;;) {
        const uint32_t now = millis();

        if ((uint32_t)(now - last_beat) >= HEARTBEAT_MS) {
            last_beat = now;
            board_led_toggle();
        }

        if ((uint32_t)(now - last_frame) < FRAME_PERIOD_MS) {
            continue;
        }
        last_frame = now;

        demo_data_update(&data, now);

        /* Let the previous frame finish before queueing the next one. */
        st = eve_cmd_wait_idle(100u);
        if (st == EVE_OK) {
            st = dashboard_draw(&data, true, now);
        }
        if (st != EVE_OK) {
            return st;
        }
    }
}

int main(void)
{
    board_init();

    printf("\nMR2 dashboard firmware %s\n", FW_VERSION);
    printf("SYSCLK %lu Hz\n", (unsigned long)SystemCoreClock);

    for (;;) {
        printf("EVE: initialising\n");
        eve_status_t st = eve_init(&eve_panel_nhd_70_800480ft_csxp);
        if (st == EVE_OK) {
            const uint32_t id = eve_chip_id();
            printf("EVE: FT8%02X ready (chip id 0x%08lX)\n", (unsigned)((id >> 8) & 0xFFu),
                   (unsigned long)id);
            st = run_dashboard();
        }
        printf("EVE: %s, retrying in 1 s\n", eve_status_str(st));

        /* Fast LED blink while the display is down. */
        for (uint32_t i = 0u; i < 10u; i++) {
            board_led_toggle();
            delay_ms(100u);
        }
    }
}
