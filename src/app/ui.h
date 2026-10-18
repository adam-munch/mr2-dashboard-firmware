/*
 * The look shared by every screen: palette, fonts and drawing helpers.
 *
 * Colour means something or isn't used. Screens are white on black, and
 * colour only appears for the shift lights, for something that needs
 * attention (blue informational, amber warning, red critical), or for a
 * condition that is met (green).
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>

#include "eve.h"

#define SCREEN_W 800
#define SCREEN_H 480

/* Colours (0xRRGGBB) */
#define COL_BG         0x000000u
#define COL_PANEL      0x0E1218u
#define COL_PANEL_EDGE 0x262E38u
#define COL_TRACK      0x1A2029u
#define COL_BAR        0xD2DAE3u /* lit rev bar: a touch softer than the LEDs */
#define COL_TEXT       0xFFFFFFu
#define COL_TEXT_DIM   0x808B99u
#define COL_TEXT_FAINT 0x55606Eu
#define COL_RED        0xE10600u
#define COL_RED_DARK   0x3A0A08u
#define COL_GREEN      0x00D26Au
#define COL_GREEN_DARK 0x06261Au
#define COL_AMBER      0xFFB000u
#define COL_AMBER_DARK 0x2E2206u
#define COL_PURPLE     0xA64DFFu
#define COL_PURPLE_DK  0x221236u
#define COL_BLUE       0x2E9BFFu

/* Fonts: 26..31 are the FT81x anti-aliased ROM fonts, smallest to largest.
 * 32..34 are bigger still; ui_attach_fonts() attaches them to handles. */
#define FONT_TINY     26
#define FONT_SMALL    27
#define FONT_MEDIUM   28
#define FONT_LARGE    30
#define FONT_VALUE    31
#define FONT_BIG      2 /* ROM font 33 */
#define FONT_HUGE     1 /* ROM font 34 */

/* Attach FONT_BIG and FONT_HUGE. Needed in every display list that uses them. */
void ui_attach_fonts(void);

/* One rounded rectangle inside BEGIN(RECTS), outer edge at the given pixels.
 * RECTS grows each corner outwards by LINE_WIDTH, so this insets by `radius`;
 * LINE_WIDTH must already be radius * 16. */
void ui_rect_vertices(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t radius);

/* Filled rectangle with rounded corners, outer edge at the given pixels. */
void ui_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb, int32_t radius);

/* Centred text, overstruck for a heavier stroke than the ROM fonts have. */
void ui_text_heavy(int16_t x, int16_t y, int16_t font, const char *s);

/* Filled circle of radius `r` pixels. */
void ui_dot(int32_t cx, int32_t cy, int32_t r, uint32_t rgb);

/* Tick mark sized for a ~24 px badge, centred on (cx, cy). */
void ui_check(int32_t cx, int32_t cy, uint32_t rgb);

/* Left edge of item `i` of `n` spread across x0..x1 with `gap` pixels between. */
int32_t ui_spread_x(uint32_t i, uint32_t n, int32_t x0, int32_t x1, int32_t gap);

#endif /* UI_H */
