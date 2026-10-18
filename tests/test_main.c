/*
 * Host tests for the EVE driver, the CAN pipeline, the dashboard renderer,
 * the knob and menus, and helpers.
 * Run: cmake -S tests -B build-tests && cmake --build build-tests && ctest --test-dir build-tests
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "can_queue.h"
#include "dash_menu.h"
#include "dashboard.h"
#include "demo_data.h"
#include "ecu_link.h"
#include "eve.h"
#include "eve_panels.h"
#include "fixmath.h"
#include "guide.h"
#include "knob.h"
#include "logo.h"
#include "menu.h"
#include "mock_can.h"
#include "mock_encoder.h"
#include "mock_eve.h"
#include "rusefi_can.h"
#include "settings.h"
#include "ui.h"
#include "version.h"

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
        mock_can_reset();            \
        mock_encoder_reset();        \
        settings_reset();            \
        menu_init(&dash_menu);       \
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
    int context_depth;     /* SAVE_CONTEXT minus RESTORE_CONTEXT */
    bool context_underflow;
    int vertices;
    int vertex_out_of_range;
    bool starts_with_dlstart;
    bool ends_with_display_swap;
    bool parse_error;
    int texts;
    int needle_hits; /* CMD_TEXT strings equal to the needle */
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

/* Display list words the co-processor emits for `chars` glyphs: one CELL and
 * one VERTEX2F each past x = 511 (VERTEX2II can't reach), plus setup. */
static size_t glyph_dl_words(size_t chars)
{
    return 8 + 2 * chars;
}

static size_t number_chars(int32_t n)
{
    size_t chars = n < 0 ? 2 : 1;
    for (int64_t v = n < 0 ? -(int64_t)n : n; v >= 10; v /= 10) {
        chars++;
    }
    return chars;
}

/* Walk the captured co-processor stream. If `needle` is set, count the
 * CMD_TEXT strings that match it exactly. */
static stream_stats_t parse_stream(const char *needle)
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
            case CMD_TEXT: {
                const char *str = (const char *)&g_mock.fifo[(i + 3) * 4];
                s.texts++;
                s.dl_words += glyph_dl_words(strlen(str));
                if (needle != NULL && strcmp(str, needle) == 0) {
                    s.needle_hits++;
                }
                i += 3 + str_words(i + 3);
                break;
            }
            case CMD_NUMBER:
                s.texts++;
                s.dl_words += glyph_dl_words(number_chars((int32_t)mock_fifo_word(i + 3)));
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
        } else if (w == SAVE_CONTEXT()) {
            s.context_depth++;
        } else if (w == RESTORE_CONTEXT()) {
            s.context_underflow = s.context_underflow || s.context_depth == 0;
            s.context_depth--;
        }
        i++;
    }
    return s;
}

static void check_frame(const char *name)
{
    const stream_stats_t s = parse_stream(NULL);
    printf("  %s: %zu FIFO words (%zu bytes), ~%zu DL words, %d vertices, %d text items\n", name, s.words,
           s.words * 4, s.dl_words, s.vertices, s.texts);
    CHECK(!s.parse_error);
    CHECK(s.starts_with_dlstart);
    CHECK(s.ends_with_display_swap);
    CHECK_EQ(s.begin_depth, 0);
    CHECK_EQ(s.max_begin_depth, 1);
    CHECK_EQ(s.vertex_out_of_range, 0);
    CHECK_EQ(s.context_depth, 0);
    CHECK(!s.context_underflow);
    CHECK(s.dl_words < RAM_DL_SIZE / 4); /* fits in the 8 KB display list */
}

/* How many times `text` is drawn in the captured frame. */
static int frame_text_count(const char *text)
{
    return parse_stream(text).needle_hits;
}

/* Number of vertices in the captured frame. */
static int frame_vertices(void)
{
    return parse_stream(NULL).vertices;
}

static void test_render_splash(void)
{
    int vertices[3];
    static const uint32_t lights[3] = {0, DASHBOARD_START_LIGHTS, 99};
    for (size_t i = 0; i < 3; i++) {
        init_ok();
        CHECK_EQ(dashboard_draw_splash("0.1.0", 0x00011208u, lights[i]), EVE_OK);
        check_frame("splash");
        vertices[i] = frame_vertices();
    }
    /* Each lit start light adds its glow; out-of-range counts clamp. */
    CHECK_EQ(vertices[1] - vertices[0], DASHBOARD_START_LIGHTS);
    CHECK_EQ(vertices[2], vertices[1]);
}

/* True if the captured stream draws the string `s` with CMD_TEXT. */
static bool stream_has_text(const char *s)
{
    const size_t n = mock_fifo_words();
    for (size_t i = 0; i + 3 < n; i++) {
        if (mock_fifo_word(i) == CMD_TEXT && strcmp((const char *)&g_mock.fifo[(i + 3) * 4], s) == 0) {
            return true;
        }
    }
    return false;
}

static void test_render_dashboard(void)
{
    static const dash_data_t cases[] = {
        {.rpm = 0, .speed_kph = 0, .coolant_c = -20, .fuel_pct = 0, .battery_mv = 0, .gear = 0,
         .valid = DASH_VALID_ALL},
        {.rpm = 3456, .speed_kph = 88, .coolant_c = 90, .fuel_pct = 55, .battery_mv = 14100, .gear = 3,
         .valid = DASH_VALID_ALL, .link = DASH_LINK_DEMO},
        {.rpm = 8000, .speed_kph = 240, .coolant_c = 125, .fuel_pct = 100, .battery_mv = 16500, .gear = 5,
         .valid = DASH_VALID_ALL, .warnings = DASH_WARN_CHECK_ENGINE | DASH_WARN_LIMITER},
        {.rpm = 9999, .speed_kph = 999, .coolant_c = 999, .fuel_pct = 255, .battery_mv = 65535, .gear = 9,
         .valid = DASH_VALID_ALL, .warnings = 0xFF},
        {.rpm = 9999, .gear = 200, .valid = DASH_VALID_ALL}, /* out-of-range gear */
        {.rpm = 5000, .valid = 0, .link = DASH_LINK_WAITING},
        {.rpm = 5000, .valid = DASH_VALID_RPM, .link = DASH_LINK_LOST},
        {.valid = 0, .link = DASH_LINK_CAN_FAULT},
    };
    /* Both phases of the shift-light and alert flashing. */
    static const uint32_t times[] = {0, 100, 250};
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        for (size_t t = 0; t < sizeof times / sizeof times[0]; t++) {
            init_ok();
            CHECK_EQ(dashboard_draw(&cases[i], times[t]), EVE_OK);
            char name[48];
            snprintf(name, sizeof name, "dashboard #%zu at %u ms", i, (unsigned)times[t]);
            check_frame(name);
        }
    }
}

static void test_render_validity_and_status(void)
{
    /* Everything valid and the link up: no "--" and no status line. */
    dash_data_t d = {.rpm = 3000, .speed_kph = 80, .coolant_c = 85, .fuel_pct = 50, .battery_mv = 14000,
                     .gear = 0, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK};
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK(!stream_has_text("--"));
    CHECK(stream_has_text("N"));
    CHECK(!stream_has_text("CHECK ENGINE")); /* lamps only appear while on */
    CHECK(!stream_has_text("LIMITER"));
    CHECK(!stream_has_text("DEMO DATA"));
    CHECK(!stream_has_text("WAITING FOR ECU"));
    CHECK(!stream_has_text("NO DATA FROM ECU"));

    /* Each lamp shows while its warning is on. */
    d.warnings = DASH_WARN_CHECK_ENGINE | DASH_WARN_LIMITER;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK(stream_has_text("CHECK ENGINE"));
    CHECK(stream_has_text("LIMITER"));
    d.warnings = 0;

    /* Nothing valid (the ECU went quiet, maybe on the move): the gauges stay,
     * every value is "--" and the gear "-". */
    d.valid = 0;
    d.link = DASH_LINK_LOST;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK(stream_has_text("KM/H"));
    CHECK(stream_has_text("--"));
    CHECK(stream_has_text("-"));
    CHECK(!stream_has_text("N"));
    CHECK(stream_has_text("NO DATA FROM ECU"));

    static const struct {
        dash_link_t link;
        const char *text;
    } lines[] = {
        {DASH_LINK_DEMO, "DEMO DATA"},
        {DASH_LINK_WAITING, "WAITING FOR ECU"},
        {DASH_LINK_LOST, "NO DATA FROM ECU"},
        {DASH_LINK_CAN_FAULT, "CAN BUS FAULT"},
    };
    for (size_t i = 0; i < sizeof lines / sizeof lines[0]; i++) {
        d.link = lines[i].link;
        init_ok();
        CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
        CHECK(stream_has_text(lines[i].text));
    }
}

static void test_dashboard_alerts(void)
{
    const dash_data_t normal = {.rpm = 3000, .speed_kph = 90, .coolant_c = 88, .fuel_pct = 60,
                                .battery_mv = 14100, .gear = 4, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK};

    /* All normal: no alert, no status words. */
    init_ok();
    CHECK_EQ(dashboard_draw(&normal, 0), EVE_OK);
    CHECK_EQ(frame_text_count("LOW FUEL"), 0);
    CHECK_EQ(frame_text_count("COOLANT HOT"), 0);
    CHECK_EQ(frame_text_count("NOT CHARGING"), 0);
    CHECK_EQ(frame_text_count("COLD"), 0);
    CHECK_EQ(frame_text_count("DEMO DATA"), 0);

    /* One warning. */
    dash_data_t d = normal;
    d.fuel_pct = 14;
    d.link = DASH_LINK_DEMO;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_text_count("LOW FUEL"), 1);
    CHECK_EQ(frame_text_count("LOW"), 1); /* on the tile */
    CHECK_EQ(frame_text_count("DEMO DATA"), 1);

    /* Critical outranks warnings, and the rest are counted. */
    d.link = DASH_LINK_OK;
    d.coolant_c = 110;
    d.battery_mv = 12400; /* engine running, so: not charging */
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_text_count("COOLANT HOT  +2"), 1);
    CHECK_EQ(frame_text_count("HOT"), 1);
    CHECK_EQ(frame_text_count("NO CHG"), 1);

    /* Critical alerts flash at 2 Hz; the tiles keep their status. */
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 250), EVE_OK);
    CHECK_EQ(frame_text_count("COOLANT HOT  +2"), 0);
    CHECK_EQ(frame_text_count("HOT"), 1);

    /* Low voltage with the engine off is only a flat battery... */
    d = normal;
    d.rpm = 0;
    d.battery_mv = 12400;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_text_count("NOT CHARGING"), 0);

    /* An rpm the ECU isn't sending doesn't count as running, however high. */
    d = normal;
    d.battery_mv = 12400;
    d.valid &= (uint16_t)~DASH_VALID_RPM;
    CHECK(!dash_engine_running(&d));
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_text_count("NOT CHARGING"), 0);

    /* ...and a cold engine is information, not an alert. */
    d = normal;
    d.coolant_c = 30;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_text_count("COLD"), 1);
    init_ok();
    CHECK_EQ(dashboard_draw(&normal, 0), EVE_OK);
    const int normal_texts = parse_stream(NULL).texts;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(parse_stream(NULL).texts, normal_texts + 1); /* just the word, no alert */
}

/* ---------------------------------------------------------------------------
 * Pre-start guide
 * ------------------------------------------------------------------------ */

/* Draw `d` as one frame and run the usual frame checks on it. */
static void draw_checked(const dash_data_t *d, const char *name)
{
    init_ok();
    CHECK_EQ(dashboard_draw(d, 0), EVE_OK);
    check_frame(name);
}

static void test_guide_screens(void)
{
    static const char *const rule = "Only starts in neutral or with the clutch held";
    dash_data_t d = {.coolant_c = 20, .fuel_pct = 50, .battery_mv = 12500, .gear = 2,
                     .valid = DASH_VALID_ALL | DASH_VALID_INPUTS, .link = DASH_LINK_OK, .estop_active = true};

    /* Step 1 comes first, whatever else is already done. */
    CHECK_EQ(guide_step(&d), GUIDE_ESTOP);
    d.wheel_connected = true;
    CHECK_EQ(guide_step(&d), GUIDE_ESTOP);
    draw_checked(&d, "guide: e-stop");
    CHECK_EQ(frame_text_count("TURN OFF THE E-STOP"), 1);
    CHECK_EQ(frame_text_count("E-STOP ACTIVE"), 1);
    CHECK_EQ(frame_text_count("KM/H"), 0); /* not the gauges */

    /* Step 2 */
    d.estop_active = false;
    d.wheel_connected = false;
    CHECK_EQ(guide_step(&d), GUIDE_WHEEL);
    draw_checked(&d, "guide: wheel");
    CHECK_EQ(frame_text_count("PLUG IN THE STEERING WHEEL"), 1);
    CHECK_EQ(frame_text_count("WHEEL NOT DETECTED"), 1);

    /* Step 3 in gear with the clutch up: the rule is spelled out, start won't work. */
    d.wheel_connected = true;
    CHECK_EQ(guide_step(&d), GUIDE_START);
    CHECK_EQ(guide_start_state(&d), GUIDE_START_BLOCKED);
    draw_checked(&d, "guide: start, not ready");
    CHECK_EQ(frame_text_count("PRESS START"), 1);
    CHECK_EQ(frame_text_count(rule), 1);
    CHECK_EQ(frame_text_count("NOT READY"), 1);
    CHECK_EQ(frame_text_count("READY TO START"), 0);
    CHECK_EQ(frame_text_count("2"), 1); /* the gear it's in */

    /* Either condition is enough. */
    d.clutch_held = true;
    CHECK_EQ(guide_start_state(&d), GUIDE_START_READY);
    draw_checked(&d, "guide: start, clutch held");
    CHECK_EQ(frame_text_count(rule), 1); /* still shown */
    CHECK_EQ(frame_text_count("READY TO START"), 1);
    CHECK_EQ(frame_text_count("NOT READY"), 0);

    d.clutch_held = false;
    d.gear = 0;
    CHECK_EQ(guide_start_state(&d), GUIDE_START_READY);
    draw_checked(&d, "guide: start, neutral");
    CHECK_EQ(frame_text_count("READY TO START"), 1);
    CHECK_EQ(frame_text_count("N"), 1);

    /* Cranking: still the start screen, saying so. */
    d.rpm = 220;
    CHECK_EQ(guide_step(&d), GUIDE_START);
    draw_checked(&d, "guide: cranking");
    CHECK_EQ(frame_text_count("CRANKING"), 1);
    CHECK_EQ(frame_text_count("READY TO START"), 0);

    /* Running: the gauges. */
    d.rpm = DASH_RPM_RUNNING;
    CHECK_EQ(guide_step(&d), GUIDE_NONE);
    draw_checked(&d, "running");
    CHECK_EQ(frame_text_count("KM/H"), 1);
    CHECK_EQ(frame_text_count("PRESS START"), 0);

    /* Stalled on the move: the gauges stay until the car stops. */
    d.rpm = 0;
    d.speed_kph = 40;
    CHECK_EQ(guide_step(&d), GUIDE_NONE);
    d.speed_kph = 0;
    CHECK_EQ(guide_step(&d), GUIDE_START);

    /* The rev sweep shows the gauges even where it passes 0 rpm. */
    d.estop_active = true;
    init_ok();
    CHECK_EQ(dashboard_rev_sweep(&d, 10), EVE_OK);
    CHECK(frame_text_count("KM/H") > 2);
    CHECK_EQ(frame_text_count("TURN OFF THE E-STOP"), 0);
}

/* With the ECU as the data source and the start inputs not wired up yet. */
static void test_guide_unknown_inputs(void)
{
    /* Power-up, ECU silent: the car is parked, so the guide shows. It can't
     * check the e-stop or the wheel, so it goes straight to the start step. */
    dash_data_t d = {.valid = 0, .link = DASH_LINK_WAITING, .estop_active = true};
    CHECK_EQ(guide_step(&d), GUIDE_START);
    CHECK_EQ(guide_start_state(&d), GUIDE_START_UNKNOWN);
    draw_checked(&d, "guide: nothing known");
    CHECK_EQ(frame_text_count("PRESS START"), 1);
    CHECK_EQ(frame_text_count("Only starts in neutral or with the clutch held"), 1);
    CHECK_EQ(frame_text_count("NOT READY"), 0); /* no verdict it can't back up */
    CHECK_EQ(frame_text_count("READY TO START"), 0);
    CHECK_EQ(frame_text_count("--"), 1);        /* clutch unknown */
    CHECK_EQ(frame_text_count("-"), 3);         /* gear unknown, steps 1 and 2 unmonitored */
    CHECK_EQ(frame_text_count("WAITING FOR ECU"), 1);

    /* Data lost, or no speed: the car might be moving, so the gauges stay. */
    d.link = DASH_LINK_LOST;
    CHECK_EQ(guide_step(&d), GUIDE_NONE);
    d.link = DASH_LINK_CAN_FAULT;
    CHECK_EQ(guide_step(&d), GUIDE_NONE);
    d.link = DASH_LINK_OK;
    d.valid = DASH_VALID_RPM; /* rpm 0 but speed unknown */
    CHECK_EQ(guide_step(&d), GUIDE_NONE);
    d.valid = DASH_VALID_RPM | DASH_VALID_SPEED;
    CHECK_EQ(guide_step(&d), GUIDE_START);

    /* Only a known input can hold a step. */
    d.valid |= DASH_VALID_ESTOP;
    CHECK_EQ(guide_step(&d), GUIDE_ESTOP);
    d.estop_active = false;
    CHECK_EQ(guide_step(&d), GUIDE_START); /* wheel still unmonitored */

    /* The rule with one condition unknown: a known-met one is enough... */
    d.valid |= DASH_VALID_GEAR;
    d.gear = 0;
    CHECK_EQ(guide_start_state(&d), GUIDE_START_READY);
    d.gear = 1;
    CHECK_EQ(guide_start_state(&d), GUIDE_START_UNKNOWN); /* ...in gear, clutch unknown */
    d.valid |= DASH_VALID_CLUTCH;
    CHECK_EQ(guide_start_state(&d), GUIDE_START_BLOCKED);
    d.valid &= (uint16_t)~DASH_VALID_GEAR;
    d.clutch_held = true;
    CHECK_EQ(guide_start_state(&d), GUIDE_START_READY);

    /* Cranking needs a known rpm. */
    d.rpm = 200;
    d.valid &= (uint16_t)~DASH_VALID_RPM;
    d.link = DASH_LINK_WAITING;
    draw_checked(&d, "guide: rpm unknown");
    CHECK_EQ(frame_text_count("CRANKING"), 0);
    d.valid |= DASH_VALID_RPM | DASH_VALID_SPEED;
    draw_checked(&d, "guide: cranking");
    CHECK_EQ(frame_text_count("CRANKING"), 1);
}

/* ---------------------------------------------------------------------------
 * Knob
 * ------------------------------------------------------------------------ */

static void test_knob_turn(void)
{
    knob_init(0);
    CHECK_EQ(knob_poll(1).turn, 0);

    mock_encoder_turn(1);
    CHECK_EQ(knob_poll(2).turn, 1);
    mock_encoder_turn(-3);
    CHECK(knob_poll(3).turn == -3);

    /* Bounce and wobble around a detent, up to 3 edges either way: never a click. */
    bool quiet = true;
    for (int k = 1; k < ENCODER_COUNTS_PER_DETENT; k++) {
        static const int dir[4] = {1, -1, -1, 1};
        for (int i = 0; i < 4; i++) {
            g_enc.count = (uint16_t)(g_enc.count + dir[i] * k);
            quiet = quiet && knob_poll(4).turn == 0;
        }
    }
    CHECK(quiet);

    /* The click lands on the last edge into the next detent, and bounce on
     * that edge neither repeats it nor takes it back. */
    g_enc.count = (uint16_t)(g_enc.count + ENCODER_COUNTS_PER_DETENT - 1);
    CHECK_EQ(knob_poll(5).turn, 0);
    g_enc.count++;
    CHECK_EQ(knob_poll(6).turn, 1);
    g_enc.count--;
    CHECK_EQ(knob_poll(7).turn, 0);
    g_enc.count++;
    CHECK_EQ(knob_poll(8).turn, 0);

    /* A fast spin while a frame is drawn: every click counts. */
    mock_encoder_turn(-25);
    CHECK(knob_poll(9).turn == -25);

    /* Across the 16-bit wrap of the hardware counter. */
    g_enc.count = 0xFFFEu;
    knob_init(10);
    mock_encoder_turn(1);
    CHECK_EQ(knob_poll(11).turn, 1);
    mock_encoder_turn(-2);
    CHECK(knob_poll(12).turn == -2);

    /* Turns before knob_init() (e.g. during the splash) don't count. */
    mock_encoder_turn(7);
    knob_init(13);
    CHECK_EQ(knob_poll(14).turn, 0);
}

/* Set the switch at `t` and poll. */
static knob_event_t switch_at(bool pressed, uint32_t t)
{
    g_enc.pressed = pressed;
    return knob_poll(t);
}

static void test_knob_switch(void)
{
    knob_event_t ev;
    knob_init(0);

    /* A tap with contact bounce at both ends: one press, on the release. */
    bool quiet = true;
    quiet = quiet && !switch_at(true, 100).press;
    quiet = quiet && !switch_at(false, 102).press;
    quiet = quiet && !switch_at(true, 104).press;
    quiet = quiet && !switch_at(true, 114).press; /* debounced down: no event until released */
    quiet = quiet && !switch_at(true, 200).press;
    quiet = quiet && !switch_at(false, 220).press;
    quiet = quiet && !switch_at(true, 222).press;
    quiet = quiet && !switch_at(false, 224).press;
    quiet = quiet && !switch_at(false, 230).press;
    CHECK(quiet);
    ev = switch_at(false, 234);
    CHECK(ev.press && !ev.hold);
    CHECK(!switch_at(false, 300).press); /* once */

    /* A glitch shorter than the debounce time is nothing. */
    switch_at(true, 400);
    switch_at(false, 400 + KNOB_DEBOUNCE_MS - 1);
    ev = switch_at(false, 500);
    CHECK(!ev.press && !ev.hold);

    /* Held: reported once, at KNOB_HOLD_MS while still held; the release
     * that follows isn't a press. */
    switch_at(true, 1000);
    switch_at(true, 1000 + KNOB_DEBOUNCE_MS);
    CHECK(!switch_at(true, 1000 + KNOB_HOLD_MS - 1).hold);
    ev = switch_at(true, 1000 + KNOB_HOLD_MS);
    CHECK(ev.hold && !ev.press);
    CHECK(!switch_at(true, 1800).hold);
    switch_at(false, 1900);
    ev = switch_at(false, 1900 + KNOB_DEBOUNCE_MS);
    CHECK(!ev.press && !ev.hold);

    /* Let go just before the hold time, but debounced after it: a press. */
    switch_at(true, 3000);
    switch_at(true, 3000 + KNOB_DEBOUNCE_MS);
    switch_at(false, 3000 + KNOB_HOLD_MS - 5);
    ev = switch_at(false, 3000 + KNOB_HOLD_MS + 10);
    CHECK(ev.press && !ev.hold);

    /* Held since before knob_init(): ignored until released, then normal. */
    g_enc.pressed = true;
    knob_init(5000);
    ev = switch_at(true, 5000 + 2 * KNOB_HOLD_MS);
    CHECK(!ev.press && !ev.hold);
    switch_at(false, 6500);
    ev = switch_at(false, 6600);
    CHECK(!ev.press && !ev.hold);
    switch_at(true, 7000);
    switch_at(true, 7050);
    switch_at(false, 7100);
    CHECK(switch_at(false, 7150).press);
}

/* ---------------------------------------------------------------------------
 * Menu engine, against a menu with one of everything
 * ------------------------------------------------------------------------ */

static int16_t s_t_number;
static int16_t s_t_live;
static int16_t s_t_colour;
static int16_t s_t_switch;
static int s_t_runs;
static const char *const k_t_colours[] = {"RED", "GREEN", "BLUE"};

static const char *t_action(void)
{
    s_t_runs++;
    return "DONE";
}

static void t_page_a(const dash_data_t *d, uint32_t now_ms)
{
    eve_cmd_text(400, 300, FONT_MEDIUM, OPT_CENTER, "BODY OF PAGE A");
}

static void t_page_b(const dash_data_t *d, uint32_t now_ms)
{
    eve_cmd_text(400, 300, FONT_MEDIUM, OPT_CENTER, "BODY OF PAGE B");
}

static const menu_item_t k_t_level5[] = {MENU_ACTION_ITEM("TOO DEEP", t_action)};
static const menu_item_t k_t_level4[] = {MENU_SUBMENU_ITEM("LEVEL 5", k_t_level5)};
static const menu_item_t k_t_level3[] = {MENU_SUBMENU_ITEM("LEVEL 4", k_t_level4)};
static const menu_item_t k_t_level2[] = {MENU_SUBMENU_ITEM("LEVEL 3", k_t_level3)};
static const menu_item_t k_t_items[] = {
    MENU_VALUE_ITEM("NUMBER", s_t_number, -5, 5, 2, "MM", 0u),
    MENU_VALUE_ITEM("LIVE", s_t_live, 0, 100, 10, NULL, MENU_LIVE),
    MENU_CHOICE_ITEM("COLOUR", s_t_colour, k_t_colours, 0u),
    MENU_TOGGLE_ITEM("SWITCH", s_t_switch),
    MENU_ACTION_ITEM("DO IT", t_action),
    MENU_PAGE_ITEM("PAGE A", t_page_a),
    MENU_SUBMENU_ITEM("LEVEL 2", k_t_level2),
    MENU_PAGE_ITEM("PAGE B", t_page_b),
};
static const menu_item_t k_t_menu = MENU_SUBMENU_ITEM("TEST MENU", k_t_items);

static uint32_t s_t_now;

static void ev_turn(int16_t clicks)
{
    const knob_event_t ev = {clicks, false, false};
    menu_update(&ev, s_t_now);
}

static void ev_press(void)
{
    const knob_event_t ev = {0, true, false};
    menu_update(&ev, s_t_now);
}

static void ev_hold(void)
{
    const knob_event_t ev = {0, false, true};
    menu_update(&ev, s_t_now);
}

/* Nothing from the knob, `ms` later. */
static void ev_idle(uint32_t ms)
{
    const knob_event_t ev = {0, false, false};
    s_t_now += ms;
    menu_update(&ev, s_t_now);
}

/* The highlighted / edited / shown item is `label`; NULL for a BACK row. */
static bool at(const char *label)
{
    const menu_item_t *it = menu_current();
    if (label == NULL) {
        return menu_is_open() && it == NULL;
    }
    return it != NULL && strcmp(it->label, label) == 0;
}

static void open_test_menu(void)
{
    s_t_now = 1000;
    s_t_number = 1;
    s_t_live = 50;
    s_t_colour = 0;
    s_t_switch = 0;
    s_t_runs = 0;
    menu_init(&k_t_menu);
    ev_turn(3); /* turning on the home screen does nothing... */
    ev_hold();  /* ...nor does holding */
    CHECK(!menu_is_open());
    ev_press();
    CHECK_EQ(menu_view(), MENU_LIST);
    CHECK(at("NUMBER"));
}

static void test_menu_navigation(void)
{
    open_test_menu();

    /* The highlight stops at the ends, the last row being EXIT. */
    ev_turn(-1);
    CHECK(at("NUMBER"));
    ev_turn(7);
    CHECK(at("PAGE B"));
    ev_turn(5);
    CHECK(at(NULL));
    ev_turn(-2);
    CHECK(at("LEVEL 2"));

    /* Into a list and back out: by holding, or with its BACK row. */
    ev_press();
    CHECK(at("LEVEL 3"));
    ev_hold();
    CHECK(at("LEVEL 2")); /* the parent keeps its place */
    ev_press();
    ev_turn(1);
    CHECK(at(NULL));
    ev_press();
    CHECK(at("LEVEL 2"));

    /* Lists nest MENU_MAX_DEPTH deep, and no deeper. */
    ev_press(); /* into LEVEL 2 */
    ev_press(); /* LEVEL 3 */
    ev_press(); /* LEVEL 4: MENU_MAX_DEPTH lists open */
    CHECK(at("LEVEL 5"));
    ev_press();
    CHECK(at("LEVEL 5")); /* doesn't open */
    for (int i = 0; i < MENU_MAX_DEPTH - 1; i++) {
        ev_hold();
    }
    CHECK(at("LEVEL 2"));

    /* Holding at the top closes the menu; so does EXIT. */
    ev_hold();
    CHECK(!menu_is_open());
    ev_press();
    CHECK(at("NUMBER")); /* opens at the top again */
    ev_turn(20);
    ev_press();
    CHECK(!menu_is_open());
}

static void test_menu_editing(void)
{
    open_test_menu();

    /* A value: turning steps it within its range, only a press keeps it. */
    ev_press();
    CHECK_EQ(menu_view(), MENU_EDIT);
    ev_turn(1);
    CHECK_EQ(s_t_number, 1); /* not live: the setting waits */
    ev_turn(10);
    ev_turn(1);
    ev_press();
    CHECK_EQ(menu_view(), MENU_LIST);
    CHECK_EQ(s_t_number, 5);
    ev_press();
    ev_turn(-100);
    ev_press();
    CHECK(s_t_number == -5);

    /* A hold cancels. */
    ev_press();
    ev_turn(2);
    ev_hold();
    CHECK(s_t_number == -5);
    CHECK_EQ(menu_view(), MENU_LIST);
    CHECK(at("NUMBER"));

    /* A live value follows the knob, and a cancel puts it back. */
    ev_turn(1);
    ev_press();
    ev_turn(2);
    CHECK_EQ(s_t_live, 70);
    ev_turn(10);
    CHECK_EQ(s_t_live, 100);
    ev_hold();
    CHECK_EQ(s_t_live, 50);

    /* A choice steps through its names and stops at the ends. */
    ev_turn(1);
    ev_press();
    ev_turn(5);
    ev_press();
    CHECK_EQ(s_t_colour, 2);
    ev_press();
    ev_turn(-1);
    ev_press();
    CHECK_EQ(s_t_colour, 1);

    /* A toggle flips on a press, no editor. */
    ev_turn(1);
    ev_press();
    CHECK_EQ(s_t_switch, 1);
    CHECK_EQ(menu_view(), MENU_LIST);
    ev_press();
    CHECK_EQ(s_t_switch, 0);

    /* An action runs on each press. */
    ev_turn(1);
    ev_press();
    ev_press();
    CHECK_EQ(s_t_runs, 2);
    CHECK_EQ(menu_view(), MENU_LIST);

    /* A setting that is out of range is pulled into it by the editor. */
    s_t_live = 999;
    ev_turn(-3);
    CHECK(at("LIVE"));
    ev_press();
    ev_press();
    CHECK_EQ(s_t_live, 100);
}

static void test_menu_pages(void)
{
    open_test_menu();
    ev_turn(5);
    ev_press();
    CHECK_EQ(menu_view(), MENU_SHOW_PAGE);
    CHECK(at("PAGE A"));

    /* Turning goes to the next page in the list, skipping what isn't one,
     * and stops at the last. */
    ev_turn(1);
    CHECK(at("PAGE B"));
    ev_turn(1);
    CHECK(at("PAGE B"));
    ev_turn(-5);
    CHECK(at("PAGE A"));

    /* A press, or a hold, goes back to the list, on the page last shown. */
    ev_turn(1);
    ev_press();
    CHECK_EQ(menu_view(), MENU_LIST);
    CHECK(at("PAGE B"));
    ev_press();
    ev_hold();
    CHECK_EQ(menu_view(), MENU_LIST);
}

static void test_menu_timeout(void)
{
    /* A list left alone closes... */
    open_test_menu();
    ev_idle(MENU_TIMEOUT_MS - 1);
    CHECK(menu_is_open());
    ev_turn(1); /* any input restarts the wait */
    ev_idle(MENU_TIMEOUT_MS - 1);
    CHECK(menu_is_open());
    ev_idle(1);
    CHECK(!menu_is_open());

    /* ...an edit in progress is dropped... */
    open_test_menu();
    ev_press();
    ev_turn(2);
    ev_idle(MENU_TIMEOUT_MS);
    CHECK(!menu_is_open());
    CHECK_EQ(s_t_number, 1);

    /* ...unless it's live: then it stays as set. */
    open_test_menu();
    ev_turn(1);
    ev_press();
    ev_turn(-2);
    ev_idle(MENU_TIMEOUT_MS);
    CHECK(!menu_is_open());
    CHECK_EQ(s_t_live, 30);

    /* Pages stay open to be watched. */
    open_test_menu();
    ev_turn(5);
    ev_press();
    ev_idle(10u * MENU_TIMEOUT_MS);
    CHECK_EQ(menu_view(), MENU_SHOW_PAGE);
    ev_press();
    ev_idle(MENU_TIMEOUT_MS); /* back in the list, the clock runs again */
    CHECK(!menu_is_open());
}

/* How many times `n` is drawn with CMD_NUMBER in the captured frame. */
static int frame_number_count(int32_t n)
{
    int hits = 0;
    const size_t words = mock_fifo_words();
    for (size_t i = 0; i + 3 < words; i++) {
        if (mock_fifo_word(i) == CMD_NUMBER && (int32_t)mock_fifo_word(i + 3) == n) {
            hits++;
            i += 3;
        }
    }
    return hits;
}

/* The menu as it is now, as one frame, with the usual frame checks. */
static void draw_menu_checked(const dash_data_t *d, const char *name)
{
    init_ok();
    CHECK_EQ(dashboard_draw(d, s_t_now), EVE_OK);
    check_frame(name);
}

static void test_menu_render(void)
{
    const dash_data_t parked = {.coolant_c = 24, .fuel_pct = 72, .battery_mv = 12500, .gear = 1,
                                .valid = DASH_VALID_ALL | DASH_VALID_INPUTS, .link = DASH_LINK_OK,
                                .estop_active = true};

    /* Opened over the pre-start guide: the menu replaces it, under the shift
     * lights and rev bar (whose scale it keeps). */
    open_test_menu();
    draw_menu_checked(&parked, "menu: list");
    CHECK_EQ(frame_text_count("TEST MENU"), 1);
    CHECK_EQ(frame_text_count("TURN OFF THE E-STOP"), 0);
    CHECK_EQ(frame_number_count(5), 1); /* "5" on the rev bar scale */
    CHECK_EQ(frame_text_count("1 MM"), 1);
    CHECK_EQ(frame_text_count("50"), 1); /* no unit */
    CHECK_EQ(frame_text_count("RED"), 1);
    CHECK_EQ(frame_text_count("OFF"), 1);
    CHECK_EQ(frame_text_count(">"), 1); /* PAGE A; 6 of the 9 rows are on screen */
    CHECK_EQ(frame_text_count("EXIT"), 0);
    CHECK_EQ(frame_text_count("TURN: SELECT     PRESS: EDIT     HOLD: EXIT"), 1);

    /* Scrolled to the end: EXIT shows, the top rows don't. */
    ev_turn(20);
    draw_menu_checked(&parked, "menu: scrolled");
    CHECK_EQ(frame_text_count("EXIT"), 1);
    CHECK_EQ(frame_text_count("NUMBER"), 0);
    CHECK_EQ(frame_text_count("TURN: SELECT     PRESS: EXIT     HOLD: EXIT"), 1);
    ev_turn(-20);

    /* Editing: the value between arrows, the bar, and the hint for it. */
    ev_press();
    ev_turn(1);
    draw_menu_checked(&parked, "menu: editing");
    CHECK_EQ(frame_text_count("3 MM"), 1);
    CHECK_EQ(frame_text_count("<"), 1);
    CHECK_EQ(frame_text_count("TURN: ADJUST     PRESS: KEEP     HOLD: CANCEL"), 1);
    ev_press();

    /* An action's message replaces the hint for a while. */
    ev_turn(4);
    ev_press();
    draw_menu_checked(&parked, "menu: toast");
    CHECK_EQ(frame_text_count("DONE"), 1);
    CHECK_EQ(frame_text_count("TURN: SELECT     PRESS: RUN     HOLD: EXIT"), 0);
    ev_idle(MENU_TOAST_MS);
    draw_menu_checked(&parked, "menu: toast gone");
    CHECK_EQ(frame_text_count("DONE"), 0);
    CHECK_EQ(frame_text_count("TURN: SELECT     PRESS: RUN     HOLD: EXIT"), 1);

    /* A page: its title and body, and the hint to the next page. */
    ev_turn(1);
    ev_press();
    draw_menu_checked(&parked, "menu: page");
    CHECK_EQ(frame_text_count("PAGE A"), 1);
    CHECK_EQ(frame_text_count("BODY OF PAGE A"), 1);
    CHECK_EQ(frame_text_count("NUMBER"), 0);
    CHECK_EQ(frame_text_count("TURN: NEXT PAGE     PRESS: BACK"), 1);

    /* Nested: BACK instead of EXIT. */
    ev_press();
    ev_turn(1);
    ev_press();
    ev_turn(1);
    draw_menu_checked(&parked, "menu: nested");
    CHECK_EQ(frame_text_count("LEVEL 2"), 1);
    CHECK_EQ(frame_text_count("BACK"), 1);
    CHECK_EQ(frame_text_count("TURN: SELECT     PRESS: BACK     HOLD: BACK"), 1);

    /* Over the gauges, with the engine running: the speed and vitals give
     * way, the alert moves up beside the title and still flashes. */
    dash_data_t hot = {.rpm = 5850, .speed_kph = 81, .coolant_c = 110, .fuel_pct = 9, .battery_mv = 14200,
                       .gear = 3, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK};
    s_t_now = 0;
    draw_menu_checked(&hot, "menu: alert");
    CHECK_EQ(frame_text_count("KM/H"), 0);
    CHECK_EQ(frame_text_count("COOLANT"), 0);
    CHECK_EQ(frame_text_count("COOLANT HOT  +1"), 1);
    s_t_now = 250;
    draw_menu_checked(&hot, "menu: alert, flash off");
    CHECK_EQ(frame_text_count("COOLANT HOT  +1"), 0);

    /* Closed: back to whatever the home screen is. */
    menu_close();
    draw_menu_checked(&hot, "menu closed");
    CHECK_EQ(frame_text_count("KM/H"), 1);
    CHECK_EQ(frame_text_count("LEVEL 2"), 0);

    /* The rev sweep is always the gauges, menu or not. */
    open_test_menu();
    init_ok();
    CHECK_EQ(dashboard_rev_sweep(&hot, 10), EVE_OK);
    CHECK(frame_text_count("KM/H") > 2);
    CHECK_EQ(frame_text_count("TEST MENU"), 0);
}

/* ---------------------------------------------------------------------------
 * The dashboard's menu and its demo features
 * ------------------------------------------------------------------------ */

static void test_dash_menu(void)
{
    const dash_data_t d = {.rpm = 3000, .speed_kph = 97, .coolant_c = 88, .fuel_pct = 60, .battery_mv = 14100,
                           .gear = 3, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK};
    s_t_now = 1000;
    menu_init(&dash_menu);
    ev_press();
    CHECK(at("ENGINE DATA"));
    draw_menu_checked(&d, "dash menu");
    CHECK_EQ(frame_text_count("MENU"), 1);
    CHECK_EQ(frame_text_count("EXIT"), 1); /* everything fits on one screen */

    /* DISPLAY > BRIGHTNESS: live, in steps of 10, never below the minimum. */
    ev_turn(1);
    ev_press();
    CHECK(at("BRIGHTNESS"));
    draw_menu_checked(&d, "dash menu: display");
    CHECK_EQ(frame_text_count("100 %"), 1);
    CHECK_EQ(frame_text_count("KM/H"), 1);
    CHECK_EQ(frame_text_count("ON"), 1);
    ev_press();
    ev_turn(-3);
    CHECK_EQ(g_settings.brightness_pct, 70);
    draw_menu_checked(&d, "dash menu: brightness");
    CHECK_EQ(frame_text_count("70 %"), 1);
    ev_turn(-20);
    CHECK_EQ(g_settings.brightness_pct, SETTINGS_BRIGHTNESS_MIN);
    ev_press();

    /* SPEED UNITS: the main screen follows once it's kept. */
    ev_turn(1);
    ev_press();
    ev_turn(1);
    CHECK_EQ(g_settings.speed_units, SPEED_KPH);
    ev_press();
    CHECK_EQ(g_settings.speed_units, SPEED_MPH);

    /* SHIFT FLASH */
    ev_turn(1);
    ev_press();
    CHECK_EQ(g_settings.shift_flash, 0);

    /* ECU MODE: nothing is sent on the way to the value, only once it's kept. */
    ev_hold();
    ev_turn(1);
    CHECK(at("ECU MODE"));
    ev_press();
    ev_turn(3);
    CHECK_EQ(g_settings.ecu_mode, 0);
    ev_press();
    CHECK_EQ(g_settings.ecu_mode, 3);
    ev_press();
    ev_turn(20);
    ev_press();
    CHECK_EQ(g_settings.ecu_mode, SETTINGS_ECU_MODE_MAX);

    /* RESTORE DEFAULTS, with a confirmation. */
    ev_turn(2);
    CHECK(at("RESTORE DEFAULTS"));
    ev_press();
    CHECK_EQ(g_settings.brightness_pct, 100);
    CHECK_EQ(g_settings.speed_units, SPEED_KPH);
    CHECK_EQ(g_settings.shift_flash, 1);
    CHECK_EQ(g_settings.ecu_mode, 0);
    draw_menu_checked(&d, "dash menu: restored");
    CHECK_EQ(frame_text_count("DEFAULTS RESTORED"), 1);

    /* The two pages are next to each other for the knob. */
    ev_turn(-4);
    ev_press();
    CHECK(at("ENGINE DATA"));
    ev_turn(1);
    CHECK(at("DIAGNOSTICS"));
}

static void test_settings_on_screen(void)
{
    dash_data_t d = {.rpm = 3000, .speed_kph = 97, .coolant_c = 88, .fuel_pct = 60, .battery_mv = 14100,
                     .gear = 3, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK};

    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_number_count(97), 1);
    CHECK_EQ(frame_text_count("KM/H"), 1);

    g_settings.speed_units = SPEED_MPH;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_number_count(60), 1); /* 97 km/h is 60.3 mph */
    CHECK_EQ(frame_text_count("MPH"), 1);
    CHECK_EQ(frame_text_count("KM/H"), 0);
    d.speed_kph = 255;
    init_ok();
    CHECK_EQ(dashboard_draw(&d, 0), EVE_OK);
    CHECK_EQ(frame_number_count(158), 1); /* 158.4 */

    /* At the shift point the lights flash at 5 Hz, or stay lit with the
     * flash off. */
    d.rpm = 6600;
    int vertices[2][2];
    for (int flash = 0; flash < 2; flash++) {
        g_settings.shift_flash = (int16_t)flash;
        for (int phase = 0; phase < 2; phase++) {
            init_ok();
            CHECK_EQ(dashboard_draw(&d, (uint32_t)phase * 100u), EVE_OK);
            vertices[flash][phase] = frame_vertices();
        }
    }
    CHECK(vertices[1][0] > vertices[1][1]); /* flashing: off half the time */
    CHECK_EQ(vertices[0][0], vertices[0][1]);
    CHECK_EQ(vertices[0][0], vertices[1][0]);
}

static void test_pages(void)
{
    /* Engine data: every reading, formatted on its own scale. */
    dash_data_t d = {.rpm = 3456, .speed_kph = 97, .coolant_c = 88, .fuel_pct = 62, .battery_mv = 13850,
                     .gear = 3, .valid = DASH_VALID_ALL, .link = DASH_LINK_OK, .oil_kpa = 350,
                     .lambda_x1000 = 853, .map_kpa = 101, .intake_c = -10, .throttle_pct = 44,
                     .timing_deg_x10 = -55};
    s_t_now = 1000;
    menu_init(&dash_menu);
    ev_press();
    ev_press();
    CHECK_EQ(menu_view(), MENU_SHOW_PAGE);
    draw_menu_checked(&d, "page: engine");
    CHECK_EQ(frame_text_count("ENGINE DATA"), 1);
    CHECK_EQ(frame_text_count("3.5"), 1);
    CHECK_EQ(frame_text_count("0.85"), 1);
    CHECK_EQ(frame_text_count("101"), 1);
    CHECK_EQ(frame_text_count("-10"), 1);
    CHECK_EQ(frame_text_count("44"), 1);
    CHECK_EQ(frame_text_count("-5.5"), 1);
    CHECK_EQ(frame_text_count("--"), 0);
    CHECK_EQ(frame_text_count("TURN: NEXT PAGE     PRESS: BACK"), 1);

    /* Extremes stay in their tiles and on screen. */
    d.oil_kpa = 65535;
    d.lambda_x1000 = 65535;
    d.map_kpa = 65535;
    d.intake_c = -32768;
    d.throttle_pct = 255;
    d.timing_deg_x10 = 32767;
    draw_menu_checked(&d, "page: engine, extremes");

    /* Without the ECU (or without the sensors), every reading is "--". */
    d.valid = DASH_VALID_RPM | DASH_VALID_SPEED;
    d.link = DASH_LINK_LOST;
    draw_menu_checked(&d, "page: engine, no data");
    CHECK_EQ(frame_text_count("--"), 6);
    CHECK_EQ(frame_text_count("-5.5"), 0);
    CHECK_EQ(frame_text_count("NO DATA FROM ECU"), 1); /* the status line stays */

    /* Diagnostics */
    ev_turn(1);
    CHECK(at("DIAGNOSTICS"));
    g_enc.count = 1234;
    g_enc.pressed = true;
    g_can.state = CAN_STATE_PASSIVE;
    g_can.tx_dropped = 17;
    s_t_now = 3723000; /* 1:02:03 */
    draw_menu_checked(&d, "page: diagnostics");
    CHECK_EQ(frame_text_count("DIAGNOSTICS"), 1);
    CHECK_EQ(frame_text_count(FW_VERSION), 1);
    CHECK_EQ(frame_text_count("FT812"), 1);
    CHECK_EQ(frame_text_count("1:02:03"), 1);
    CHECK_EQ(frame_text_count("NO DATA"), 1);
    CHECK_EQ(frame_text_count("1234"), 1);
    CHECK_EQ(frame_text_count("PRESSED"), 1);
    CHECK_EQ(frame_text_count("PASSIVE"), 1);
    CHECK_EQ(frame_text_count("17"), 1);
    CHECK_EQ(frame_text_count("TURN: NEXT PAGE     PRESS: BACK"), 1);

    static const struct {
        can_state_t state;
        const char *text;
    } states[] = {
        {CAN_STATE_STOPPED, "STOPPED"}, {CAN_STATE_ACTIVE, "ACTIVE"}, {CAN_STATE_WARNING, "WARNING"},
        {CAN_STATE_PASSIVE, "PASSIVE"}, {CAN_STATE_BUS_OFF, "BUS OFF"},
    };
    for (size_t i = 0; i < sizeof states / sizeof states[0]; i++) {
        g_can.state = states[i].state;
        draw_menu_checked(&d, "page: diagnostics, CAN state");
        CHECK_EQ(frame_text_count(states[i].text), 1);
    }
    static const struct {
        dash_link_t link;
        const char *text;
    } links[] = {
        {DASH_LINK_OK, "RECEIVING"}, {DASH_LINK_DEMO, "DEMO DATA"}, {DASH_LINK_WAITING, "WAITING"},
        {DASH_LINK_LOST, "NO DATA"},  {DASH_LINK_CAN_FAULT, "CAN FAULT"},
    };
    for (size_t i = 0; i < sizeof links / sizeof links[0]; i++) {
        d.link = links[i].link;
        draw_menu_checked(&d, "page: diagnostics, link");
        CHECK(frame_text_count(links[i].text) >= 1);
    }
}

/* ---------------------------------------------------------------------------
 * Logo
 * ------------------------------------------------------------------------ */

static int64_t cross(const logo_point_t *o, const logo_point_t *a, const logo_point_t *b)
{
    return (int64_t)(a->x - o->x) * (b->y - o->y) - (int64_t)(a->y - o->y) * (b->x - o->x);
}

/* Segments ab and cd cross or touch. */
static bool segments_meet(const logo_point_t *a, const logo_point_t *b, const logo_point_t *c,
                          const logo_point_t *d)
{
    const int64_t d1 = cross(c, d, a);
    const int64_t d2 = cross(c, d, b);
    const int64_t d3 = cross(a, b, c);
    const int64_t d4 = cross(a, b, d);
    if (((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) && ((d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0))) {
        return true;
    }
    /* Collinear and overlapping */
#define ON(p, q, r) (cross(p, q, r) == 0 && (r)->x >= (p->x < q->x ? p->x : q->x) && \
                     (r)->x <= (p->x > q->x ? p->x : q->x) && (r)->y >= (p->y < q->y ? p->y : q->y) && \
                     (r)->y <= (p->y > q->y ? p->y : q->y))
    const bool touch = ON(c, d, a) || ON(c, d, b) || ON(a, b, c) || ON(a, b, d);
#undef ON
    return touch;
}

static void test_logo_outline(void)
{
    const uint32_t n = logo_outline_len;
    CHECK(n >= 3);

    /* The table spans exactly LOGO_WIDTH x LOGO_HEIGHT. */
    uint32_t min_x = UINT32_MAX, min_y = UINT32_MAX, max_x = 0, max_y = 0;
    for (uint32_t i = 0; i < n; i++) {
        const logo_point_t *p = &logo_outline[i];
        min_x = p->x < min_x ? p->x : min_x;
        min_y = p->y < min_y ? p->y : min_y;
        max_x = p->x > max_x ? p->x : max_x;
        max_y = p->y > max_y ? p->y : max_y;
        const logo_point_t *q = &logo_outline[(i + 1) % n];
        CHECK(p->x != q->x || p->y != q->y); /* no zero-length edges */
    }
    CHECK_EQ(min_x, 0);
    CHECK_EQ(min_y, 0);
    CHECK_EQ(max_x, LOGO_WIDTH);
    CHECK_EQ(max_y, LOGO_HEIGHT);

    /* A simple polygon: no edge meets another except its neighbours at the
     * shared corner. Crossing edges would punch holes in the even-odd fill. */
    int meets = 0;
    for (uint32_t i = 0; i < n; i++) {
        for (uint32_t j = i + 2; j < n; j++) {
            if (i == 0 && j == n - 1) {
                continue; /* neighbours through the wrap-around */
            }
            if (segments_meet(&logo_outline[i], &logo_outline[(i + 1) % n], &logo_outline[j],
                              &logo_outline[(j + 1) % n])) {
                printf("  edges %u and %u meet\n", (unsigned)i, (unsigned)j);
                meets++;
            }
        }
    }
    CHECK_EQ(meets, 0);

    CHECK_EQ(logo_height(LOGO_WIDTH), LOGO_HEIGHT);
    CHECK_EQ(logo_height(320), 164);
}

static void test_logo_draw(void)
{
    const int32_t x = 240, y = 84, w = 320;
    const int32_t h = logo_height(w);
    init_ok();
    logo_draw(x, y, w, 0xFFFFFFu);
    CHECK_EQ(eve_cmd_flush(), EVE_OK);

    /* Display list only, wrapped in a saved context. */
    const size_t n = mock_fifo_words();
    CHECK(n > 2 * logo_outline_len);
    CHECK_EQ(mock_fifo_word(0), SAVE_CONTEXT());
    CHECK_EQ(mock_fifo_word(n - 1), RESTORE_CONTEXT());

    uint32_t prim = 0;
    int strip_vertices = 0, outline_vertices = 0, stray = 0;
    uint32_t first = 0, last = 0;
    bool saw_invert = false, saw_fill = false, saw_clear_on_fill = false;
    for (size_t i = 0; i < n; i++) {
        const uint32_t word = mock_fifo_word(i);
        CHECK((word & 0xFFFFFF00u) != 0xFFFFFF00u); /* no co-processor commands */
        if ((word >> 24) == 0x1Fu) {
            prim = word & 0xFu;
            first = 0;
        } else if (word == STENCIL_OP(KEEP, INVERT)) {
            saw_invert = true;
        } else if (word == STENCIL_FUNC(EQUAL, 255, 255)) {
            saw_fill = true;
        } else if (word == STENCIL_OP(KEEP, ZERO)) {
            saw_clear_on_fill = saw_fill;
        } else if ((word >> 30) == 1u && (prim == EDGE_STRIP_B || prim == LINE_STRIP)) {
            const int32_t vx = (int32_t)((word >> 15) & 0x7FFFu);
            const int32_t vy = (int32_t)(word & 0x7FFFu);
            if (vx < x * 16 || vx > (x + w) * 16 || vy < y * 16 || vy > (y + h) * 16) {
                stray++;
            }
            if (first == 0) {
                first = word;
            }
            last = word;
            if (prim == EDGE_STRIP_B) {
                strip_vertices++;
            } else {
                outline_vertices++;
            }
            if (strip_vertices == (int)logo_outline_len + 1 && prim == EDGE_STRIP_B) {
                CHECK_EQ(first, last); /* closed */
            }
        }
    }
    CHECK(saw_invert);
    CHECK(saw_fill);
    CHECK(saw_clear_on_fill); /* the stencil is left clear for whatever comes next */
    CHECK_EQ(strip_vertices, logo_outline_len + 1);
    CHECK_EQ(outline_vertices, logo_outline_len + 1);
    CHECK_EQ(first, last); /* the outline is closed too */
    CHECK_EQ(stray, 0);
}

static void test_rev_sweep(void)
{
    init_ok();
    /* With the ECU silent the live screen would be the guide; the sweep isn't. */
    const dash_data_t base = {.rpm = 0, .speed_kph = 0, .coolant_c = 25, .fuel_pct = 50, .battery_mv = 12600,
                              .link = DASH_LINK_WAITING};
    CHECK_EQ(dashboard_rev_sweep(&base, 10), EVE_OK);
    CHECK(frame_text_count("KM/H") > 2);
    CHECK_EQ(frame_text_count("PRESS START"), 0);
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

    /* The start procedure: the guide steps come in order, the engine only
     * cranks once everything allows it, and it doesn't stall afterwards. */
    guide_step_t last_step = guide_step(&d);
    const bool starts_at_estop = last_step == GUIDE_ESTOP;
    bool steps_in_order = true;
    bool saw_start_blocked = false;
    bool saw_start_ready = false;
    bool cranked_when_not_allowed = false;
    bool started = false;
    bool stalled = false;
    bool engine_sane = true;
    bool saw_boost = false;

    for (uint32_t t = 0; t <= 120000; t += 16) { /* two minutes at 60 fps */
        demo_data_update(&d, t);
        saw_gear[d.gear] = true;
        max_rpm = d.rpm > max_rpm ? d.rpm : max_rpm;
        max_speed = d.speed_kph > max_speed ? d.speed_kph : max_speed;
        sane = sane && d.gear <= 5 && d.rpm <= 7500 && d.speed_kph <= 260 && d.fuel_pct <= 100 &&
               d.coolant_c >= 20 && d.coolant_c <= 100;
        if (d.gear == 0 || d.clutch_held || !started) {
            sane = sane && d.speed_kph == 0;
        }

        const guide_step_t step = guide_step(&d);
        steps_in_order = steps_in_order && (step == last_step || step == last_step + 1);
        last_step = step;
        if (step == GUIDE_START) {
            saw_start_blocked = saw_start_blocked || guide_start_state(&d) == GUIDE_START_BLOCKED;
            saw_start_ready = saw_start_ready || guide_start_state(&d) == GUIDE_START_READY;
        }
        if (!started && d.rpm >= GUIDE_RPM_CRANKING) {
            cranked_when_not_allowed =
                cranked_when_not_allowed || d.estop_active || !d.wheel_connected ||
                guide_start_state(&d) != GUIDE_START_READY;
        }
        if (dash_engine_running(&d)) {
            started = true;
        } else {
            stalled = stalled || started;
        }

        /* The engine data page: boost on the pulls, and a lambda reading
         * only while the engine runs. */
        engine_sane = engine_sane && d.map_kpa >= 20 && d.map_kpa <= 200 && d.throttle_pct <= 100 &&
                      d.intake_c >= 20 && d.intake_c <= 50 && d.oil_kpa <= 500 &&
                      dash_valid(&d, DASH_VALID_LAMBDA) == dash_engine_running(&d);
        saw_boost = saw_boost || d.map_kpa > 150;
    }
    CHECK(engine_sane);
    CHECK(saw_boost);
    CHECK(sane);
    CHECK(starts_at_estop);
    CHECK(steps_in_order);
    CHECK_EQ(last_step, GUIDE_NONE);
    CHECK(saw_start_blocked); /* the demo shows the rule being enforced... */
    CHECK(saw_start_ready);   /* ...and met */
    CHECK(!cranked_when_not_allowed);
    CHECK(started);
    CHECK(!stalled);
    for (int g = 0; g <= 5; g++) {
        CHECK(saw_gear[g]);
    }
    CHECK(max_rpm >= 6500);
    CHECK(max_speed >= 150);
    CHECK_EQ(d.coolant_c >= 88, 1); /* warmed up */
    CHECK_EQ(d.valid, DASH_VALID_ALL | DASH_VALID_INPUTS); /* the start inputs too */
    CHECK_EQ(d.link, DASH_LINK_DEMO);
}

/* ---------------------------------------------------------------------------
 * CAN: receive queue
 * ------------------------------------------------------------------------ */

static void test_can_queue(void)
{
    static can_queue_t q;
    can_queue_init(&q);
    can_frame_t f = {0};
    can_frame_t out;

    CHECK(!can_queue_pop(&q, &out));
    for (uint32_t i = 0; i < CAN_QUEUE_LEN; i++) {
        f.id = i;
        CHECK(can_queue_push(&q, &f));
    }
    f.id = 999;
    CHECK(!can_queue_push(&q, &f)); /* full: newest is dropped */
    for (uint32_t i = 0; i < CAN_QUEUE_LEN; i++) {
        CHECK(can_queue_pop(&q, &out));
        CHECK_EQ(out.id, i);
    }
    CHECK(!can_queue_pop(&q, &out));

    /* Many laps around the ring, in order, with the queue partly full. */
    bool ok = true;
    uint32_t next_in = 0;
    uint32_t next_out = 0;
    for (uint32_t round = 0; round < 1000; round++) {
        for (uint32_t k = 0; k < 1 + round % 7; k++) {
            f.id = next_in;
            if (can_queue_push(&q, &f)) {
                next_in++;
            }
        }
        for (uint32_t k = 0; k < 1 + round % 5; k++) {
            if (can_queue_pop(&q, &out)) {
                ok = ok && out.id == next_out;
                next_out++;
            }
        }
    }
    CHECK(ok);
    CHECK(next_in > 3 * CAN_QUEUE_LEN);

    /* Unsigned indices wrap cleanly. */
    q.head = q.tail = 0xFFFFFFFEu;
    for (uint32_t i = 0; i < 4; i++) {
        f.id = i;
        CHECK(can_queue_push(&q, &f));
    }
    for (uint32_t i = 0; i < 4; i++) {
        CHECK(can_queue_pop(&q, &out));
        CHECK_EQ(out.id, i);
    }
}

/* ---------------------------------------------------------------------------
 * CAN: rusEFI broadcast decoder
 *
 * Frames are built from the signal definitions in rusEFI_CAN_verbose.dbc
 * (start bit, length, scale, offset; all Intel byte order), not from the
 * decoder's byte offsets, so the two are checked against each other.
 * ------------------------------------------------------------------------ */

static can_frame_t rusefi_frame(uint32_t base, uint32_t offset, uint32_t time_ms)
{
    can_frame_t f;
    memset(&f, 0, sizeof f);
    f.id = base + offset;
    f.dlc = 8;
    f.time_ms = time_ms;
    return f;
}

/* Place a raw DBC signal value: Intel (little-endian) bit numbering. */
static void dbc_put(can_frame_t *f, unsigned start, unsigned len, int32_t raw)
{
    for (unsigned i = 0; i < len; i++) {
        if ((((uint32_t)raw) >> i) & 1u) {
            const unsigned bit = start + i;
            f->data[bit / 8] |= (uint8_t)(1u << (bit % 8));
        }
    }
}

/* Physical value -> raw, as a DBC tool would: raw = (phys - offset) / scale. */
static int32_t dbc_raw(double phys, double scale, double offset)
{
    return (int32_t)lround((phys - offset) / scale);
}

/* One full broadcast at `base`, all 12 messages, with distinctive values. */
static size_t make_broadcast(can_frame_t *out, uint32_t base, uint32_t time_ms)
{
    can_frame_t *f = out;

    *f = rusefi_frame(base, 0, time_ms); /* BASE0 */
    dbc_put(f, 0, 16, 3);                /* WarningCounter */
    dbc_put(f, 16, 16, 0x1234);          /* LastError */
    dbc_put(f, 32, 1, 1);                /* RevLimAct */
    dbc_put(f, 33, 1, 1);                /* MainRelayAct */
    dbc_put(f, 34, 1, 0);                /* FuelPumpAct */
    dbc_put(f, 35, 1, 1);                /* CELAct */
    dbc_put(f, 36, 1, 0);                /* EGOHeatAct */
    dbc_put(f, 37, 1, 1);                /* LambdaProtectAct */
    dbc_put(f, 38, 1, 1);                /* Fan */
    dbc_put(f, 39, 1, 0);                /* Fan2 */
    dbc_put(f, 40, 8, 3);                /* CurrentGear */
    dbc_put(f, 48, 16, dbc_raw(1234.5, 0.1, 0)); /* DistanceTraveled, km */
    f++;

    *f = rusefi_frame(base, 1, time_ms); /* BASE1 */
    dbc_put(f, 0, 16, 3456);                      /* RPM */
    dbc_put(f, 16, 16, dbc_raw(-5.5, 0.02, 0));   /* IgnitionTiming, signed */
    dbc_put(f, 32, 8, dbc_raw(37.5, 0.5, 0));     /* InjDuty */
    dbc_put(f, 40, 8, dbc_raw(12.0, 0.5, 0));     /* IgnDuty */
    dbc_put(f, 48, 8, 97);                        /* VehicleSpeed */
    dbc_put(f, 56, 8, 85);                        /* FlexPct */
    f++;

    *f = rusefi_frame(base, 2, time_ms); /* BASE2 */
    dbc_put(f, 0, 16, dbc_raw(45.67, 0.01, 0));   /* PPS */
    dbc_put(f, 16, 16, dbc_raw(44.10, 0.01, 0));  /* TPS1 */
    dbc_put(f, 32, 16, dbc_raw(-1.0, 0.01, 0));   /* TPS2: no sensor */
    dbc_put(f, 48, 16, dbc_raw(0.0, 0.01, 0));    /* Wastegate */
    f++;

    *f = rusefi_frame(base, 3, time_ms); /* BASE3 */
    dbc_put(f, 0, 16, dbc_raw(101.3, 1.0 / 30, 0)); /* MAP */
    dbc_put(f, 16, 8, dbc_raw(88, 1, -40));         /* CoolantTemp */
    dbc_put(f, 24, 8, dbc_raw(-10, 1, -40));        /* IntakeTemp */
    dbc_put(f, 32, 8, dbc_raw(0, 1, -40));          /* AUX1Temp */
    dbc_put(f, 40, 8, dbc_raw(150, 1, -40));        /* AUX2Temp */
    dbc_put(f, 48, 8, dbc_raw(45, 1, -40));         /* MCUTemp */
    dbc_put(f, 56, 8, dbc_raw(62.5, 0.5, 0));       /* FuelLevel */
    f++;

    *f = rusefi_frame(base, 4, time_ms); /* BASE4 */
    dbc_put(f, 16, 16, dbc_raw(350, 1.0 / 30, 0)); /* OilPress */
    dbc_put(f, 32, 8, dbc_raw(95, 1, -40));        /* OilTemperature */
    dbc_put(f, 40, 8, dbc_raw(30, 1, -40));        /* FuelTemperature */
    dbc_put(f, 48, 16, dbc_raw(13.85, 0.001, 0));  /* BattVolt */
    f++;

    *f = rusefi_frame(base, 5, time_ms); /* BASE5 */
    dbc_put(f, 0, 16, 412);                         /* CylAM, mg */
    dbc_put(f, 16, 16, dbc_raw(123.45, 0.01, 0));   /* EstMAF */
    dbc_put(f, 32, 16, dbc_raw(4.5, 1.0 / 300, 0)); /* InjPW */
    dbc_put(f, 48, 16, 7);                          /* KnockCt */
    f++;

    *f = rusefi_frame(base, 6, time_ms); /* BASE6 */
    dbc_put(f, 0, 16, 2500);                       /* FuelUsed */
    dbc_put(f, 16, 16, dbc_raw(1.5, 0.005, 0));    /* FuelFlow */
    dbc_put(f, 32, 16, dbc_raw(-3.25, 0.01, 0));   /* FuelTrim1, signed */
    dbc_put(f, 48, 16, dbc_raw(2.0, 0.01, 0));     /* FuelTrim2 */
    f++;

    *f = rusefi_frame(base, 7, time_ms); /* BASE7 */
    dbc_put(f, 0, 16, dbc_raw(0.85, 0.0001, 0));    /* Lam1 */
    dbc_put(f, 16, 16, dbc_raw(1.0, 0.0001, 0));    /* Lam2 */
    dbc_put(f, 32, 16, dbc_raw(300, 1.0 / 30, 0));  /* FpLow */
    dbc_put(f, 48, 16, dbc_raw(120.5, 0.1, 0));     /* FpHigh */
    f++;

    *f = rusefi_frame(base, 8, time_ms); /* BASE8, all signed */
    const int32_t cams[8] = {25, 30, -10, -12, 5, 6, -7, -8};
    for (unsigned i = 0; i < 8; i++) {
        dbc_put(f, 8 * i, 8, cams[i]);
    }
    f++;

    *f = rusefi_frame(base, 9, time_ms); /* BASE9 */
    dbc_put(f, 0, 8, dbc_raw(850, 5, 0)); /* Egt1 */
    dbc_put(f, 8, 8, dbc_raw(845, 5, 0)); /* Egt2 */
    f++;

    *f = rusefi_frame(base, 10, time_ms); /* BASE10 */
    dbc_put(f, 0, 8, -40); /* knock0 */
    dbc_put(f, 24, 8, 3);  /* knock3 */
    f++;

    *f = rusefi_frame(base, 11, time_ms); /* BASE11, 1 byte */
    f->dlc = 1;
    dbc_put(f, 0, 1, 1); /* BrakePedal */
    f++;

    return (size_t)(f - out);
}

static void check_broadcast_values(const rusefi_data_t *d)
{
    CHECK_EQ(d->warning_count, 3);
    CHECK_EQ(d->last_error, 0x1234);
    CHECK_EQ(d->flags, RUSEFI_FLAG_FUEL_SPARK_CUT | RUSEFI_FLAG_MAIN_RELAY | RUSEFI_FLAG_CHECK_ENGINE |
                           RUSEFI_FLAG_LAMBDA_PROTECT | RUSEFI_FLAG_FAN);
    CHECK_EQ(d->gear, 3);
    CHECK_EQ(d->distance_km_x10, 12345);

    CHECK_EQ(d->rpm, 3456);
    CHECK(d->timing_deg_x50 == -275);
    CHECK_EQ(d->inj_duty_pct_x2, 75);
    CHECK_EQ(d->coil_duty_pct_x2, 24);
    CHECK_EQ(d->speed_kph, 97);
    CHECK_EQ(d->ethanol_pct, 85);

    CHECK(d->pedal_pct_x100 == 4567);
    CHECK(d->tps1_pct_x100 == 4410);
    CHECK(d->tps2_pct_x100 == RUSEFI_PCT_NO_SENSOR);
    CHECK(d->wastegate_pct_x100 == 0);

    CHECK_EQ(d->map_kpa_x30, 3039);
    CHECK(d->coolant_c == 88);
    CHECK(d->intake_c == -10);
    CHECK(d->aux1_c == 0);
    CHECK(d->aux2_c == 150);
    CHECK(d->mcu_c == 45);
    CHECK_EQ(d->fuel_level_pct_x2, 125);

    CHECK_EQ(d->oil_press_kpa_x30, 10500);
    CHECK(d->oil_temp_c == 95);
    CHECK(d->fuel_temp_c == 30);
    CHECK_EQ(d->battery_mv, 13850);

    CHECK_EQ(d->cyl_airmass_mg, 412);
    CHECK_EQ(d->airflow_kgh_x100, 12345);
    CHECK_EQ(d->inj_pulse_ms_x300, 1350);
    CHECK_EQ(d->knock_count, 7);

    CHECK_EQ(d->fuel_used_g, 2500);
    CHECK_EQ(d->fuel_flow_gps_x200, 300);
    CHECK(d->fuel_trim_pct_x100[0] == -325);
    CHECK(d->fuel_trim_pct_x100[1] == 200);

    CHECK_EQ(d->lambda_x10000[0], 8500);
    CHECK_EQ(d->lambda_x10000[1], 10000);
    CHECK_EQ(d->fuel_press_low_kpa_x30, 9000);
    CHECK_EQ(d->fuel_press_high_bar_x10, 1205);

    const int8_t cams[8] = {25, 30, -10, -12, 5, 6, -7, -8};
    for (unsigned i = 0; i < 8; i++) {
        CHECK(d->cam_deg[i] == cams[i]);
    }

    CHECK_EQ(d->egt_c[0], 850);
    CHECK_EQ(d->egt_c[1], 845);
    CHECK_EQ(d->egt_c[2], 0);

    CHECK(d->knock_db[0] == -40);
    CHECK(d->knock_db[3] == 3);

    CHECK(d->brake_pedal);
}

static void test_rusefi_decode(void)
{
    can_frame_t frames[RUSEFI_MSG_COUNT];
    const size_t n = make_broadcast(frames, RUSEFI_DEFAULT_BASE_ID, 1000);
    CHECK_EQ(n, RUSEFI_MSG_COUNT);

    rusefi_data_t d;
    rusefi_init(&d);
    for (size_t i = 0; i < n; i++) {
        CHECK(rusefi_decode(&d, RUSEFI_DEFAULT_BASE_ID, false, &frames[i]));
    }
    check_broadcast_values(&d);
    CHECK_EQ(d.seen, (1u << RUSEFI_MSG_COUNT) - 1u);
    CHECK_EQ(d.frames, RUSEFI_MSG_COUNT);
    CHECK_EQ(d.last_rx_ms, 1000);

    /* Freshness: timeout boundary, never-seen messages, a frame stamped
     * just after `now` was read. */
    CHECK(rusefi_fresh(&d, RUSEFI_MSG_SPEEDS, 1000 + 499, 500));
    CHECK(!rusefi_fresh(&d, RUSEFI_MSG_SPEEDS, 1000 + 500, 500));
    CHECK(rusefi_fresh(&d, RUSEFI_MSG_SPEEDS, 999, 500));
    rusefi_data_t empty;
    rusefi_init(&empty);
    CHECK(!rusefi_fresh(&empty, RUSEFI_MSG_SPEEDS, 0, 500));
}

static void test_rusefi_rejects(void)
{
    rusefi_data_t d;
    rusefi_init(&d);

    can_frame_t f = rusefi_frame(RUSEFI_DEFAULT_BASE_ID, 1, 0);
    dbc_put(&f, 0, 16, 5000);

    can_frame_t bad = f;
    bad.id = RUSEFI_DEFAULT_BASE_ID - 1u; /* below the range */
    CHECK(!rusefi_decode(&d, RUSEFI_DEFAULT_BASE_ID, false, &bad));
    bad.id = RUSEFI_DEFAULT_BASE_ID + RUSEFI_MSG_COUNT; /* above it */
    CHECK(!rusefi_decode(&d, RUSEFI_DEFAULT_BASE_ID, false, &bad));
    bad = f;
    bad.ext = true; /* same number, but a 29-bit ID */
    CHECK(!rusefi_decode(&d, RUSEFI_DEFAULT_BASE_ID, false, &bad));
    bad = f;
    bad.dlc = 7; /* truncated */
    CHECK(!rusefi_decode(&d, RUSEFI_DEFAULT_BASE_ID, false, &bad));
    CHECK_EQ(d.seen, 0);
    CHECK_EQ(d.rpm, 0);

    /* A custom base address with 29-bit IDs. */
    const uint32_t base = 0x1F00000u;
    can_frame_t frames[RUSEFI_MSG_COUNT];
    const size_t n = make_broadcast(frames, base, 5);
    for (size_t i = 0; i < n; i++) {
        frames[i].ext = true;
        CHECK(!rusefi_decode(&d, base, false, &frames[i]));
        CHECK(rusefi_decode(&d, base, true, &frames[i]));
    }
    check_broadcast_values(&d);
}

/* ---------------------------------------------------------------------------
 * CAN: ECU link (decode -> dash values, dash status frame -> ECU)
 * ------------------------------------------------------------------------ */

static void inject_broadcast(uint32_t time_ms)
{
    can_frame_t frames[RUSEFI_MSG_COUNT];
    const size_t n = make_broadcast(frames, ECU_RUSEFI_BASE_ID, time_ms);
    for (size_t i = 0; i < n; i++) {
        mock_can_inject(&frames[i]);
    }
}

static void inject_rpm(uint16_t rpm, uint32_t time_ms)
{
    can_frame_t f = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_SPEEDS, time_ms);
    dbc_put(&f, 0, 16, rpm);
    mock_can_inject(&f);
}

static void test_ecu_link_pipeline(void)
{
    dash_data_t d;

    CHECK(ecu_link_init(0));
    CHECK(g_can.init_called);
    CHECK_EQ(g_can.bitrate, 500000);
    CHECK_EQ(g_can.n_filters, 1);
    CHECK_EQ(g_can.filters[0].first, 0x200);
    CHECK_EQ(g_can.filters[0].last, 0x20B);
    CHECK(!g_can.filters[0].ext);

    ecu_link_poll(0);
    ecu_link_get_dash(&d, 0);
    CHECK_EQ(d.link, DASH_LINK_WAITING);
    CHECK_EQ(d.valid, 0);

    /* A full broadcast: every dash value from the right message. */
    inject_broadcast(100);
    ecu_link_poll(101);
    ecu_link_get_dash(&d, 110);
    CHECK_EQ(d.link, DASH_LINK_OK);
    CHECK_EQ(d.valid, DASH_VALID_ALL);
    CHECK_EQ(d.rpm, 3456); /* first value: no smoothing from nothing */
    CHECK_EQ(d.speed_kph, 97);
    CHECK(d.coolant_c == 88);
    CHECK_EQ(d.fuel_pct, 63); /* 62.5 % rounds up */
    CHECK_EQ(d.battery_mv, 13850);
    CHECK_EQ(d.gear, 3);
    CHECK_EQ(d.warnings, DASH_WARN_CHECK_ENGINE | DASH_WARN_LIMITER);
    CHECK_EQ(ecu_link_data()->map_kpa_x30, 3039); /* the full decode is available too */

    /* The engine data page's values. */
    CHECK_EQ(d.oil_kpa, 350);
    CHECK_EQ(d.lambda_x1000, 850);
    CHECK_EQ(d.map_kpa, 101);
    CHECK(d.intake_c == -10);
    CHECK_EQ(d.throttle_pct, 44);
    CHECK(d.timing_deg_x10 == -55);

    /* Values expire together with their message; then the link is lost. */
    ecu_link_get_dash(&d, 100 + ECU_DATA_TIMEOUT_MS - 1);
    CHECK_EQ(d.valid, DASH_VALID_ALL);
    ecu_link_get_dash(&d, 100 + ECU_DATA_TIMEOUT_MS);
    CHECK_EQ(d.valid, 0);
    CHECK_EQ(d.warnings, 0);
    CHECK_EQ(d.link, DASH_LINK_LOST);

    /* Only rpm/speed arrive: only those (and the timing, from the same
     * message) are valid, but the link is up. */
    inject_rpm(2000, 3000);
    ecu_link_poll(3000);
    ecu_link_get_dash(&d, 3000);
    CHECK_EQ(d.link, DASH_LINK_OK);
    CHECK_EQ(d.valid, DASH_VALID_RPM | DASH_VALID_SPEED | DASH_VALID_TIMING);
    CHECK_EQ(d.rpm, 2000);

    /* A gear the screen can't show is dropped, not drawn as garbage. */
    can_frame_t st = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_STATUS, 3000);
    dbc_put(&st, 40, 8, 12);
    mock_can_inject(&st);
    ecu_link_poll(3000);
    ecu_link_get_dash(&d, 3000);
    CHECK_EQ(d.valid & DASH_VALID_GEAR, 0);

    /* Bus off overrides everything. */
    g_can.state = CAN_STATE_BUS_OFF;
    ecu_link_get_dash(&d, 3000);
    CHECK_EQ(d.link, DASH_LINK_CAN_FAULT);
    g_can.state = CAN_STATE_PASSIVE; /* passive alone isn't a fault: e.g. we're the only node acking */
    ecu_link_get_dash(&d, 3000);
    CHECK_EQ(d.link, DASH_LINK_OK);
}

static void test_ecu_link_missing_sensors(void)
{
    dash_data_t d;
    CHECK(ecu_link_init(0));

    /* No throttle sensor, no oil pressure sensor, no wideband: those values
     * are "--", the rest of their messages still count. */
    inject_broadcast(100);
    can_frame_t f = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_PEDAL_TPS, 100);
    dbc_put(&f, 16, 16, dbc_raw(-1.0, 0.01, 0));
    mock_can_inject(&f);
    f = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_SENSORS2, 100);
    dbc_put(&f, 16, 16, RUSEFI_OIL_PRESS_NO_SENSOR);
    dbc_put(&f, 48, 16, 13850);
    mock_can_inject(&f);
    f = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_FUELING3, 100); /* lambda 0 */
    mock_can_inject(&f);
    ecu_link_poll(100);
    ecu_link_get_dash(&d, 100);
    CHECK_EQ(d.valid, DASH_VALID_ALL & ~(DASH_VALID_THROTTLE | DASH_VALID_OIL_PRESS | DASH_VALID_LAMBDA));
    CHECK_EQ(d.battery_mv, 13850);

    /* Readings a little out of range are clamped, not wrapped. */
    f = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_PEDAL_TPS, 200);
    dbc_put(&f, 16, 16, dbc_raw(100.6, 0.01, 0));
    mock_can_inject(&f);
    f = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_SENSORS2, 200);
    dbc_put(&f, 16, 16, dbc_raw(-0.5, 1.0 / 30, 0));
    mock_can_inject(&f);
    ecu_link_poll(200);
    ecu_link_get_dash(&d, 200);
    CHECK(dash_valid(&d, DASH_VALID_THROTTLE | DASH_VALID_OIL_PRESS));
    CHECK_EQ(d.throttle_pct, 100);
    CHECK_EQ(d.oil_kpa, 0);
    f = rusefi_frame(ECU_RUSEFI_BASE_ID, RUSEFI_MSG_PEDAL_TPS, 300);
    dbc_put(&f, 16, 16, dbc_raw(-2.0, 0.01, 0));
    mock_can_inject(&f);
    ecu_link_poll(300);
    ecu_link_get_dash(&d, 300);
    CHECK_EQ(d.throttle_pct, 0);
}

static void test_ecu_link_timestamps(void)
{
    dash_data_t d;
    CHECK(ecu_link_init(0));

    /* Frames that sat in the queue past the timeout don't count as live. */
    inject_broadcast(0);
    ecu_link_poll(5000);
    ecu_link_get_dash(&d, 5000);
    CHECK_EQ(d.valid, 0);
    CHECK_EQ(d.link, DASH_LINK_LOST);

    /* A frame stamped just after the main loop read the time is fresh. */
    inject_broadcast(6001);
    ecu_link_poll(6000);
    ecu_link_get_dash(&d, 6000);
    CHECK_EQ(d.valid, DASH_VALID_ALL);
    CHECK_EQ(d.link, DASH_LINK_OK);
}

static void test_ecu_link_ignores_other_traffic(void)
{
    dash_data_t d;
    CHECK(ecu_link_init(0));

    can_frame_t f = rusefi_frame(0, 0x1FF, 10);
    mock_can_inject(&f);
    f.id = 0x20C;
    mock_can_inject(&f);
    f.id = 0x201;
    f.ext = true;
    mock_can_inject(&f);
    ecu_link_poll(10);
    ecu_link_get_dash(&d, 10);
    CHECK_EQ(d.link, DASH_LINK_WAITING);
    CHECK_EQ(d.valid, 0);
}

static void test_ecu_link_can_init_fails(void)
{
    dash_data_t d;
    g_can.init_ok = false;
    CHECK(!ecu_link_init(0));
    for (uint32_t t = 0; t <= 1000; t += 10) {
        ecu_link_poll(t);
    }
    ecu_link_get_dash(&d, 1000);
    CHECK_EQ(d.link, DASH_LINK_CAN_FAULT);
    CHECK_EQ(g_can.tx_len, 0); /* nothing sent through a dead controller */
}

static void test_ecu_link_rpm_smoothing(void)
{
    dash_data_t d;
    CHECK(ecu_link_init(0));

    inject_rpm(1000, 0);
    ecu_link_poll(0);
    ecu_link_get_dash(&d, 0);
    CHECK_EQ(d.rpm, 1000);

    /* Step to 3000: the needle moves there over a few frames, without
     * jumping or overshooting, and lands exactly. */
    inject_rpm(3000, 16);
    ecu_link_poll(16);
    uint16_t prev = 1000;
    bool monotonic = true;
    uint32_t settled_at = 0;
    for (uint32_t t = 16; t <= 400; t += 16) {
        if (t % 48 == 16) { /* the ECU repeats itself every ~50 ms */
            inject_rpm(3000, t);
            ecu_link_poll(t);
        }
        ecu_link_get_dash(&d, t);
        if (t == 16) {
            CHECK(d.rpm > 1000 && d.rpm < 3000);
        }
        monotonic = monotonic && d.rpm >= prev && d.rpm <= 3000;
        prev = d.rpm;
        if (settled_at == 0 && d.rpm == 3000) {
            settled_at = t;
        }
    }
    CHECK(monotonic);
    CHECK_EQ(d.rpm, 3000);
    CHECK(settled_at > 16 && settled_at <= 400);

    /* After the data drops out, the next value is shown as-is. */
    ecu_link_get_dash(&d, 400 + ECU_DATA_TIMEOUT_MS);
    CHECK_EQ(d.valid & DASH_VALID_RPM, 0);
    inject_rpm(800, 2000);
    ecu_link_poll(2000);
    ecu_link_get_dash(&d, 2000);
    CHECK_EQ(d.rpm, 800);
}

static void test_ecu_link_tx(void)
{
    CHECK(ecu_link_init(0));

    /* Announces itself straight away. */
    ecu_link_poll(0);
    CHECK_EQ(g_can.tx_len, 1);
    const can_frame_t *f = &g_can.tx[0];
    CHECK_EQ(f->id, DASH_TX_ID);
    CHECK(!f->ext);
    CHECK_EQ(f->dlc, 8);
    const uint8_t first[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    CHECK(memcmp(f->data, first, 8) == 0);

    /* Then every DASH_TX_PERIOD_MS, counting up. */
    ecu_link_poll(50);
    CHECK_EQ(g_can.tx_len, 1);
    ecu_link_poll(DASH_TX_PERIOD_MS);
    CHECK_EQ(g_can.tx_len, 2);
    CHECK_EQ(g_can.tx[1].data[0], 1);

    /* A change goes out right away (after a short minimum gap). */
    ecu_link_set_display_ok(true);
    ecu_link_poll(DASH_TX_PERIOD_MS + 5);
    CHECK_EQ(g_can.tx_len, 2);
    ecu_link_poll(DASH_TX_PERIOD_MS + 10);
    CHECK_EQ(g_can.tx_len, 3);
    CHECK_EQ(g_can.tx[2].data[0], 2);
    CHECK_EQ(g_can.tx[2].data[1], DASH_STATUS_DISPLAY_OK);

    ecu_link_set_inputs(0x0102);
    ecu_link_set_mode(7);
    ecu_link_set_mode(7); /* no change, no extra frame */
    ecu_link_poll(DASH_TX_PERIOD_MS + 20);
    CHECK_EQ(g_can.tx_len, 4);
    const uint8_t want[8] = {3, DASH_STATUS_DISPLAY_OK, 0x02, 0x01, 7, 0, 0, 0};
    CHECK(memcmp(g_can.tx[3].data, want, 8) == 0);

    /* Once ECU data flows, the frame says so. */
    inject_broadcast(150);
    ecu_link_poll(150);
    ecu_link_poll(DASH_TX_PERIOD_MS + 20 + DASH_TX_PERIOD_MS);
    CHECK_EQ(g_can.tx_len, 5);
    CHECK_EQ(g_can.tx[4].data[1], DASH_STATUS_DISPLAY_OK | DASH_STATUS_ECU_DATA_OK);

    /* TX FIFO full (nobody acking): no retry storm, the counter only counts
     * frames that were queued. */
    g_can.tx_full = true;
    const uint32_t t0 = 1000;
    for (uint32_t t = t0; t < t0 + 1000; t++) {
        ecu_link_poll(t);
    }
    CHECK(g_can.tx_dropped >= 9 && g_can.tx_dropped <= 11);
    g_can.tx_full = false;
    ecu_link_poll(t0 + 1000);
    CHECK_EQ(g_can.tx_len, 6);
    CHECK_EQ(g_can.tx[5].data[0], 5);

    /* Steady state: 10 frames a second, no more. */
    const size_t before = g_can.tx_len;
    for (uint32_t t = 3000; t < 4000; t++) {
        ecu_link_poll(t);
    }
    CHECK_EQ(g_can.tx_len - before, 1000 / DASH_TX_PERIOD_MS);
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
    RUN(test_render_validity_and_status);
    RUN(test_dashboard_alerts);
    RUN(test_guide_screens);
    RUN(test_guide_unknown_inputs);
    RUN(test_knob_turn);
    RUN(test_knob_switch);
    RUN(test_menu_navigation);
    RUN(test_menu_editing);
    RUN(test_menu_pages);
    RUN(test_menu_timeout);
    RUN(test_menu_render);
    RUN(test_dash_menu);
    RUN(test_settings_on_screen);
    RUN(test_pages);
    RUN(test_logo_outline);
    RUN(test_logo_draw);
    RUN(test_rev_sweep);
    RUN(test_fixmath);
    RUN(test_demo_data);
    RUN(test_can_queue);
    RUN(test_rusefi_decode);
    RUN(test_rusefi_rejects);
    RUN(test_ecu_link_pipeline);
    RUN(test_ecu_link_missing_sensors);
    RUN(test_ecu_link_timestamps);
    RUN(test_ecu_link_ignores_other_traffic);
    RUN(test_ecu_link_can_init_fails);
    RUN(test_ecu_link_rpm_smoothing);
    RUN(test_ecu_link_tx);

    printf("\n%d checks, %d failures\n", s_checks, s_failures);
    return s_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
