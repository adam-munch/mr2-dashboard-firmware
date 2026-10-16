/*
 * Host tests for the EVE driver, the dashboard renderer and helpers.
 * Run: cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dashboard.h"
#include "demo_data.h"
#include "eve.h"
#include "eve_panels.h"
#include "fixmath.h"
#include "mock_eve.h"

static int s_failures;
static int s_checks;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        s_checks++;                                                                 \
        if (!(cond)) {                                                              \
            s_failures++;                                                           \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                \
        }                                                                           \
    } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        s_checks++;                                                                 \
        const unsigned long _a = (unsigned long)(a), _b = (unsigned long)(b);       \
        if (_a != _b) {                                                             \
            s_failures++;                                                           \
            printf("  FAIL %s:%d: %s == %s (0x%lx != 0x%lx)\n", __FILE__, __LINE__, \
                   #a, #b, _a, _b);                                                 \
        }                                                                           \
    } while (0)

#define RUN(test)                    \
    do {                             \
        printf("%s\n", #test);       \
        mock_eve_reset();            \
        test();                      \
    } while (0)

/* Bring the driver up against the mock and clear the capture buffers. */
static void init_ok(void)
{
    CHECK_EQ(eve_init(&eve_panel_nhd_70_800480ft_csxp), EVE_OK);
    g_mock.frame_count = 0;
    g_mock.fifo_len = 0;
}

/* ---------------------------------------------------------------------------
 * Bring-up
 * ------------------------------------------------------------------------ */

static void test_init_sequence(void)
{
    const eve_panel_t *p = &eve_panel_nhd_70_800480ft_csxp;

    CHECK_EQ(eve_init(p), EVE_OK);

    /* /PD pulse: >= 5 ms low, then >= 20 ms before the first SPI frame. */
    CHECK(!g_mock.pd_asserted);
    CHECK(g_mock.pd_release_time - g_mock.pd_assert_time >= 5);
    CHECK(g_mock.frame_count > 2);
    CHECK(g_mock.frames[0].time_ms - g_mock.pd_release_time >= 20);

    /* CLKEXT (crystal fitted) then ACTIVE, both at the slow SPI clock. */
    CHECK_EQ(g_mock.frames[0].kind, FRAME_HOST_CMD);
    CHECK_EQ(g_mock.frames[0].hdr[0], HCMD_CLKEXT);
    CHECK_EQ(g_mock.frames[1].kind, FRAME_HOST_CMD);
    CHECK_EQ(g_mock.frames[1].hdr[0], HCMD_ACTIVE);
    CHECK(!g_mock.frames[0].spi_fast && !g_mock.frames[1].spi_fast);

    /* Nothing runs fast until REG_ID has answered. */
    int first_fast = -1;
    int first_regid = -1;
    for (size_t i = 0; i < g_mock.frame_count; i++) {
        if (first_regid < 0 && g_mock.frames[i].kind == FRAME_READ && g_mock.frames[i].addr == REG_ID) {
            first_regid = (int)i;
        }
        if (first_fast < 0 && g_mock.frames[i].spi_fast) {
            first_fast = (int)i;
        }
    }
    CHECK(first_regid > 0);
    CHECK(first_fast > first_regid);

    /* Timing from the datasheet. */
    CHECK_EQ(mock_mem16(REG_HSIZE), 800);
    CHECK_EQ(mock_mem16(REG_HCYCLE), 928);
    CHECK_EQ(mock_mem16(REG_HOFFSET), 88);
    CHECK_EQ(mock_mem16(REG_HSYNC0), 0);
    CHECK_EQ(mock_mem16(REG_HSYNC1), 48);
    CHECK_EQ(mock_mem16(REG_VSIZE), 480);
    CHECK_EQ(mock_mem16(REG_VCYCLE), 525);
    CHECK_EQ(mock_mem16(REG_VOFFSET), 32);
    CHECK_EQ(mock_mem16(REG_VSYNC0), 0);
    CHECK_EQ(mock_mem16(REG_VSYNC1), 3);
    CHECK_EQ(g_mock.mem[REG_PCLK], 2);
    CHECK_EQ(g_mock.mem[REG_SWIZZLE], 0);
    CHECK_EQ(g_mock.mem[REG_PCLK_POL], 1);
    CHECK_EQ(g_mock.mem[REG_CSPREAD], 0);
    CHECK_EQ(g_mock.mem[REG_DITHER], 1);
    CHECK_EQ(mock_mem16(REG_PWM_HZ), 1000);
    CHECK_EQ(g_mock.mem[REG_PWM_DUTY], 0);
    CHECK_EQ(g_mock.mem[REG_TOUCH_MODE], TOUCHMODE_OFF);
    CHECK_EQ(g_mock.mem[REG_VOL_SOUND], 0);
    CHECK_EQ(g_mock.mem[REG_VOL_PB], 0);

    /* Black first frame, swapped in, DISP on, pixel clock started last. */
    CHECK_EQ(mock_mem32(RAM_DL + 0), CLEAR_COLOR_RGB(0, 0, 0));
    CHECK_EQ(mock_mem32(RAM_DL + 4), CLEAR(1, 1, 1));
    CHECK_EQ(mock_mem32(RAM_DL + 8), DISPLAY());
    CHECK_EQ(g_mock.mem[REG_DLSWAP], DLSWAP_FRAME);
    CHECK(g_mock.mem[REG_GPIO_DIR] & EVE_GPIO_DISP);
    CHECK(g_mock.mem[REG_GPIO] & EVE_GPIO_DISP);

    const int pclk = mock_find_write(REG_PCLK);
    CHECK(pclk > mock_find_write(REG_HCYCLE));
    CHECK(pclk > mock_find_write(REG_VSIZE));
    CHECK(pclk > mock_find_write(REG_DLSWAP));
    CHECK(pclk > mock_find_write(REG_GPIO));
    CHECK(mock_find_write(REG_PWM_DUTY) < mock_find_write(REG_HCYCLE)); /* backlight off first */

    CHECK_EQ(eve_chip_id(), 0x00011208u);
}

static void test_init_no_response(void)
{
    g_mock.reg_id_ok = false;
    CHECK_EQ(eve_init(&eve_panel_nhd_70_800480ft_csxp), EVE_ERR_NO_RESPONSE);
}

static void test_init_reset_timeout(void)
{
    g_mock.mem[REG_CPURESET] = 0x01; /* co-processor stuck in reset */
    CHECK_EQ(eve_init(&eve_panel_nhd_70_800480ft_csxp), EVE_ERR_RESET_TIMEOUT);
}

/* ---------------------------------------------------------------------------
 * SPI framing
 * ------------------------------------------------------------------------ */

static void test_read_framing(void)
{
    g_mock.active_sent = true;
    CHECK_EQ(eve_rd8(REG_ID), 0x7C);
    const mock_frame_t *f = &g_mock.frames[0];
    CHECK_EQ(f->kind, FRAME_READ);
    CHECK_EQ(f->hdr[0], 0x30);
    CHECK_EQ(f->hdr[1], 0x20);
    CHECK_EQ(f->hdr[2], 0x00);
    CHECK_EQ(f->hdr[3], 0x00); /* dummy byte */
    CHECK_EQ(f->data_len, 1);

    g_mock.mem[0x1234] = 0x78;
    g_mock.mem[0x1235] = 0x56;
    g_mock.mem[0x1236] = 0x34;
    g_mock.mem[0x1237] = 0x12;
    CHECK_EQ(eve_rd32(0x1234), 0x12345678u);
    CHECK_EQ(eve_rd16(0x1236), 0x1234u);
}

static void test_write_framing(void)
{
    eve_wr16(REG_PWM_HZ, 0x1234);
    const mock_frame_t *f = &g_mock.frames[0];
    CHECK_EQ(f->kind, FRAME_WRITE);
    CHECK_EQ(f->hdr[0], 0xB0);
    CHECK_EQ(f->hdr[1], 0x20);
    CHECK_EQ(f->hdr[2], 0xD0);
    CHECK_EQ(f->data_len, 2);
    CHECK_EQ(g_mock.mem[REG_PWM_HZ], 0x34);
    CHECK_EQ(g_mock.mem[REG_PWM_HZ + 1], 0x12);

    eve_wr32(RAM_G + 16, 0xA1B2C3D4u);
    CHECK_EQ(mock_mem32(RAM_G + 16), 0xA1B2C3D4u);

    eve_host_cmd(HCMD_CLKSEL, 0x46);
    f = &g_mock.frames[2];
    CHECK_EQ(f->kind, FRAME_HOST_CMD);
    CHECK_EQ(f->hdr[0], HCMD_CLKSEL);
    CHECK_EQ(f->hdr[1], 0x46);
    CHECK_EQ(f->hdr[2], 0x00);
}

static void test_backlight_clamp(void)
{
    eve_backlight_set(200);
    CHECK_EQ(g_mock.mem[REG_PWM_DUTY], 128);
    eve_backlight_set(64);
    CHECK_EQ(g_mock.mem[REG_PWM_DUTY], 64);
}

/* ---------------------------------------------------------------------------
 * Command FIFO
 * ------------------------------------------------------------------------ */

static void test_cmd_strings(void)
{
    init_ok();
    eve_cmd_text(1, 2, 31, OPT_CENTER, "AB");
    eve_cmd_text(-3, 4, 28, 0, "ABCD");
    eve_cmd_text(0, 0, 26, 0, "");
    CHECK_EQ(eve_cmd_flush(), EVE_OK);

    const uint32_t want[] = {
        CMD_TEXT, 0x00020001u, ((uint32_t)OPT_CENTER << 16) | 31u, 0x00004241u,
        CMD_TEXT, 0x0004FFFDu, 28u, 0x44434241u, 0x00000000u,
        CMD_TEXT, 0x00000000u, 26u, 0x00000000u,
    };
    CHECK_EQ(mock_fifo_words(), sizeof want / sizeof want[0]);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK_EQ(mock_fifo_word(i), want[i]);
    }
    /* Every FIFO write goes to REG_CMDB_WRITE in whole words. */
    for (size_t i = 0; i < g_mock.frame_count; i++) {
        if (g_mock.frames[i].kind == FRAME_WRITE && g_mock.frames[i].addr == REG_CMDB_WRITE) {
            CHECK_EQ(g_mock.frames[i].hdr[0], 0xB0);
            CHECK_EQ(g_mock.frames[i].hdr[1], 0x25);
            CHECK_EQ(g_mock.frames[i].hdr[2], 0x78);
            CHECK_EQ(g_mock.frames[i].data_len % 4, 0);
        }
    }
}

static void test_cmd_widgets(void)
{
    init_ok();
    eve_cmd_number(-5, 10, 28, OPT_RIGHTX, -1234);
    eve_cmd_gauge(100, 200, 80, OPT_NOBACK, 8, 5, 3000, 8000);
    eve_cmd_progress(10, 20, 200, 16, 0, 50, 100);
    eve_cmd_gradient(0, 0, 0x112233, 0, 480, 0x445566);
    eve_cmd_romfont(1, 34);
    CHECK_EQ(eve_cmd_flush(), EVE_OK);

    const uint32_t want[] = {
        CMD_NUMBER, 0x000AFFFBu, ((uint32_t)OPT_RIGHTX << 16) | 28u, (uint32_t)-1234,
        CMD_GAUGE, (200u << 16) | 100u, ((uint32_t)OPT_NOBACK << 16) | 80u, (5u << 16) | 8u, (8000u << 16) | 3000u,
        CMD_PROGRESS, (20u << 16) | 10u, (16u << 16) | 200u, (50u << 16) | 0u, 100u,
        CMD_GRADIENT, 0u, 0x112233u, 480u << 16, 0x445566u,
        CMD_ROMFONT, 1u, 34u,
    };
    CHECK_EQ(mock_fifo_words(), sizeof want / sizeof want[0]);
    for (size_t i = 0; i < sizeof want / sizeof want[0]; i++) {
        CHECK_EQ(mock_fifo_word(i), want[i]);
    }
}

static void test_flush_waits_for_space(void)
{
    init_ok();
    /* FIFO full, then 8 bytes free, then empty. */
    static const uint16_t seq[] = {0, 8, 4092};
    g_mock.space_seq = seq;
    g_mock.space_seq_len = 3;

    for (uint32_t i = 0; i < 6; i++) {
        eve_cmd(0x1000u + i);
    }
    CHECK_EQ(eve_cmd_flush(), EVE_OK);

    CHECK_EQ(mock_fifo_words(), 6);
    for (uint32_t i = 0; i < 6; i++) {
        CHECK_EQ(mock_fifo_word(i), 0x1000u + i);
    }
    /* First chunk limited to the 8 bytes that were free. */
    const int w = mock_find_write(REG_CMDB_WRITE);
    CHECK(w >= 0);
    CHECK_EQ(g_mock.frames[w].data_len, 8);
}

static void test_auto_flush_preserves_order(void)
{
    init_ok();
    const uint32_t n = 1500; /* 6000 bytes: more than the 4 KB local buffer */
    for (uint32_t i = 0; i < n; i++) {
        eve_cmd(i * 7u);
    }
    CHECK_EQ(eve_cmd_flush(), EVE_OK);
    CHECK_EQ(mock_fifo_words(), n);
    bool ok = true;
    for (uint32_t i = 0; i < n; i++) {
        ok = ok && mock_fifo_word(i) == i * 7u;
    }
    CHECK(ok);
}

static void test_fault_and_recover(void)
{
    init_ok();
    g_mock.cmdb_space = 0x0FFF; /* low bits set: co-processor fault */
    eve_cmd(CMD_DLSTART);
    CHECK_EQ(eve_cmd_flush(), EVE_ERR_COPRO_FAULT);
    CHECK(!eve_cmd_is_idle());
    CHECK_EQ(eve_cmd_wait_idle(10), EVE_ERR_COPRO_FAULT);

    g_mock.frame_count = 0;
    eve_cmd_recover();
    const int hold = mock_find_write(REG_CPURESET);
    CHECK_EQ(hold, 0);
    CHECK(mock_find_write(REG_CMD_READ) > hold);
    CHECK(mock_find_write(REG_CMD_WRITE) > hold);
    CHECK(mock_find_write(REG_CMD_DL) > hold);
    CHECK_EQ(g_mock.mem[REG_CPURESET], 0); /* released again */

    g_mock.cmdb_space = 4092;
    CHECK(eve_cmd_is_idle());
}

static void test_wait_idle_timeout(void)
{
    init_ok();
    g_mock.cmdb_space = 1000; /* busy forever */
    CHECK_EQ(eve_cmd_wait_idle(50), EVE_ERR_TIMEOUT);
}

static void test_deferred_error(void)
{
    init_ok();
    g_mock.cmdb_space = 0x0FFF;
    for (uint32_t i = 0; i < 1100; i++) { /* forces an automatic flush */
        eve_cmd(0);
    }
    g_mock.cmdb_space = 4092;
    /* The fault from the automatic flush is reported by the next explicit one. */
    CHECK_EQ(eve_cmd_flush(), EVE_ERR_COPRO_FAULT);
    eve_cmd(0);
    CHECK_EQ(eve_cmd_flush(), EVE_OK);
}

/* ---------------------------------------------------------------------------
 * Rendering: parse the captured stream and sanity-check it
 * ------------------------------------------------------------------------ */

typedef struct {
    size_t words;
    size_t dl_words; /* display list words the co-processor will emit, approx. */
    int begin_depth;
    int max_begin_depth;
    int vertices;
    int vertex_out_of_range;
    bool starts_with_dlstart;
    bool ends_with_display_swap;
    bool parse_error;
    int texts;
} stream_stats_t;

/* Number of words a string argument occupies (NUL included, padded). */
static size_t str_words(size_t at)
{
    size_t n = 0;
    for (;;) {
        const uint32_t w = mock_fifo_word(at + n);
        n++;
        if ((w & 0xFFu) == 0 || (w & 0xFF00u) == 0 || (w & 0xFF0000u) == 0 || (w & 0xFF000000u) == 0) {
            return n;
        }
    }
}

static stream_stats_t parse_stream(void)
{
    stream_stats_t s = {0};
    const size_t n = mock_fifo_words();
    s.words = n;
    s.starts_with_dlstart = n > 0 && mock_fifo_word(0) == CMD_DLSTART;
    s.ends_with_display_swap = n >= 2 && mock_fifo_word(n - 2) == DISPLAY() && mock_fifo_word(n - 1) == CMD_SWAP;

    for (size_t i = 0; i < n;) {
        const uint32_t w = mock_fifo_word(i);
        if ((w & 0xFFFFFF00u) == 0xFFFFFF00u) {
            switch (w) {
            case CMD_DLSTART:
            case CMD_SWAP:
                i += 1;
                break;
            case CMD_TEXT:
                s.texts++;
                s.dl_words += 8 + str_words(i + 3);
                i += 3 + str_words(i + 3);
                break;
            case CMD_NUMBER:
                s.texts++;
                s.dl_words += 12;
                i += 4;
                break;
            case CMD_GRADIENT:
                s.dl_words += 30;
                i += 5;
                break;
            case CMD_ROMFONT:
                s.dl_words += 8;
                i += 3;
                break;
            default:
                printf("  unexpected co-processor command 0x%08x at word %zu\n", (unsigned)w, i);
                s.parse_error = true;
                return s;
            }
            continue;
        }

        s.dl_words++;
        if ((w >> 30) == 1u) { /* VERTEX2F */
            int32_t x = (int32_t)((w >> 15) & 0x7FFFu);
            int32_t y = (int32_t)(w & 0x7FFFu);
            if (x & 0x4000) {
                x -= 0x8000;
            }
            if (y & 0x4000) {
                y -= 0x8000;
            }
            s.vertices++;
            if (x < 0 || x > 800 * 16 || y < 0 || y > 480 * 16) {
                s.vertex_out_of_range++;
            }
            if (s.begin_depth != 1) {
                s.parse_error = true;
            }
        } else if ((w >> 24) == 0x1Fu) { /* BEGIN */
            s.begin_depth++;
            if (s.begin_depth > s.max_begin_depth) {
                s.max_begin_depth = s.begin_depth;
            }
        } else if ((w >> 24) == 0x21u) { /* END */
            s.begin_depth--;
        }
        i++;
    }
    return s;
}

static void check_frame(const char *name)
{
    const stream_stats_t s = parse_stream();
    printf("  %s: %zu FIFO words (%zu bytes), ~%zu DL words, %d vertices, %d text items\n", name, s.words,
           s.words * 4, s.dl_words, s.vertices, s.texts);
    CHECK(!s.parse_error);
    CHECK(s.starts_with_dlstart);
    CHECK(s.ends_with_display_swap);
    CHECK_EQ(s.begin_depth, 0);
    CHECK_EQ(s.max_begin_depth, 1);
    CHECK_EQ(s.vertex_out_of_range, 0);
    CHECK(s.dl_words < RAM_DL_SIZE / 4); /* fits in the 8 KB display list */
}

static void test_render_splash(void)
{
    init_ok();
    CHECK_EQ(dashboard_draw_splash("0.1.0", 0x00011208u), EVE_OK);
    check_frame("splash");
}

static void test_render_dashboard(void)
{
    static const dash_data_t cases[] = {
        {.rpm = 0, .speed_kph = 0, .coolant_c = -20, .fuel_pct = 0, .battery_mv = 0, .gear = 0},
        {.rpm = 3456, .speed_kph = 88, .coolant_c = 90, .fuel_pct = 55, .battery_mv = 14100, .gear = 3},
        {.rpm = 8000, .speed_kph = 240, .coolant_c = 125, .fuel_pct = 100, .battery_mv = 16500, .gear = 5},
        {.rpm = 9999, .speed_kph = 999, .coolant_c = 999, .fuel_pct = 255, .battery_mv = 65535, .gear = 5},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        init_ok();
        CHECK_EQ(dashboard_draw(&cases[i], true, 1234), EVE_OK);
        char name[32];
        snprintf(name, sizeof name, "dashboard #%zu", i);
        check_frame(name);
    }
}

static void test_needle_sweep(void)
{
    init_ok();
    const dash_data_t base = {.rpm = 0, .speed_kph = 0, .coolant_c = 25, .fuel_pct = 50, .battery_mv = 12600};
    CHECK_EQ(dashboard_needle_sweep(&base, 300), EVE_OK);
    CHECK(mock_fifo_words() > 0);
}

/* ---------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------ */

static void test_fixmath(void)
{
    int32_t worst = 0;
    for (uint32_t a = 0; a < 2 * ANGLE_FULL; a++) {
        const double rad = (double)a * 2.0 * M_PI / ANGLE_FULL;
        const int32_t es = abs(isin_q14(a) - (int32_t)lround(sin(rad) * 16384.0));
        const int32_t ec = abs(icos_q14(a) - (int32_t)lround(cos(rad) * 16384.0));
        worst = es > worst ? es : worst;
        worst = ec > worst ? ec : worst;
    }
    CHECK(worst <= 1);
    CHECK_EQ(ANGLE_DEG(225), 640);
    CHECK_EQ(ANGLE_DEG(270), 768);
}

static void test_demo_data(void)
{
    dash_data_t d;
    demo_data_init(&d, 0);
    bool saw_gear[6] = {false};
    uint16_t max_rpm = 0;
    uint16_t max_speed = 0;
    bool sane = true;

    for (uint32_t t = 0; t <= 120000; t += 16) { /* two minutes at 60 fps */
        demo_data_update(&d, t);
        saw_gear[d.gear] = true;
        max_rpm = d.rpm > max_rpm ? d.rpm : max_rpm;
        max_speed = d.speed_kph > max_speed ? d.speed_kph : max_speed;
        sane = sane && d.gear <= 5 && d.rpm >= 500 && d.rpm <= 7500 && d.speed_kph <= 260 &&
               d.fuel_pct <= 100 && d.coolant_c >= 20 && d.coolant_c <= 100;
        if (d.gear == 0) {
            sane = sane && d.speed_kph == 0;
        }
    }
    CHECK(sane);
    for (int g = 0; g <= 5; g++) {
        CHECK(saw_gear[g]);
    }
    CHECK(max_rpm >= 6500);
    CHECK(max_speed >= 150);
    CHECK_EQ(d.coolant_c >= 88, 1); /* warmed up */
}

int main(void)
{
    RUN(test_init_sequence);
    RUN(test_init_no_response);
    RUN(test_init_reset_timeout);
    RUN(test_read_framing);
    RUN(test_write_framing);
    RUN(test_backlight_clamp);
    RUN(test_cmd_strings);
    RUN(test_cmd_widgets);
    RUN(test_flush_waits_for_space);
    RUN(test_auto_flush_preserves_order);
    RUN(test_fault_and_recover);
    RUN(test_wait_idle_timeout);
    RUN(test_deferred_error);
    RUN(test_render_splash);
    RUN(test_render_dashboard);
    RUN(test_needle_sweep);
    RUN(test_fixmath);
    RUN(test_demo_data);

    printf("\n%d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
