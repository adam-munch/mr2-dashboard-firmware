#include "logo.h"

#include "eve.h"

/*
 * Traced from the team logo artwork (683 x 683 px, white on transparent) by
 * tools/trace_logo.py: corners fitted to its anti-aliased edges, rounded to
 * whole source pixels. Listed clockwise from the point at the far left; any
 * starting point works.
 */
const logo_point_t logo_outline[] = {
    /* Top bar, with the tops of the three legs standing proud of it */
    {0, 63}, {127, 10}, {178, 10}, {185, 0}, {302, 0}, {295, 10}, {338, 10}, {346, 0},
    {463, 0}, {456, 10}, {499, 10}, {507, 0}, {624, 0},
    /* R: shoulder, bowl and kicked-out leg */
    {540, 109}, {588, 97}, {672, 215}, {599, 244}, {683, 350}, {585, 350}, {500, 225},
    {565, 197}, {544, 164}, {484, 184},
    /* Third, second and first legs, back along the underside of the bar */
    {398, 301}, {280, 301}, {460, 63},
    {417, 63}, {264, 266}, {146, 266}, {300, 63},
    {256, 63}, {131, 234}, {13, 234}, {139, 63},
};

const uint32_t logo_outline_len = sizeof logo_outline / sizeof logo_outline[0];

int32_t logo_height(int32_t width)
{
    return (width * LOGO_HEIGHT + LOGO_WIDTH / 2) / LOGO_WIDTH;
}

/* The closed outline, scaled to `width` and placed at (x, y). */
static void outline_vertices(int32_t x, int32_t y, int32_t width)
{
    for (uint32_t i = 0u; i <= logo_outline_len; i++) {
        const logo_point_t *p = &logo_outline[i < logo_outline_len ? i : 0u];
        const int32_t x16 = x * 16 + ((int32_t)p->x * width * 16 + LOGO_WIDTH / 2) / LOGO_WIDTH;
        const int32_t y16 = y * 16 + ((int32_t)p->y * width * 16 + LOGO_WIDTH / 2) / LOGO_WIDTH;
        eve_cmd(VERTEX2F(x16, y16));
    }
}

void logo_draw(int32_t x, int32_t y, int32_t width, uint32_t rgb)
{
    const int32_t height = logo_height(width);

    eve_cmd(SAVE_CONTEXT());

    /* Keep the stencil work to the logo's own box: the edge strips below
     * would otherwise fill all the way to the bottom of the screen. */
    eve_cmd(SCISSOR_XY(x - 1, y - 1));
    eve_cmd(SCISSOR_SIZE(width + 2, height + 2));

    /* 1. Even-odd fill into the stencil. Every edge inverts everything below
     *    it, so pixels inside the outline end up inverted an odd number of
     *    times (255) and pixels outside an even number (0). */
    eve_cmd(COLOR_MASK(0, 0, 0, 0));
    eve_cmd(STENCIL_FUNC(ALWAYS, 255, 255));
    eve_cmd(STENCIL_OP(KEEP, INVERT));
    eve_cmd(BEGIN(EDGE_STRIP_B));
    outline_vertices(x, y, width);
    eve_cmd(END());

    /* 2. Paint where the stencil is set, zeroing it again as we go. */
    eve_cmd(COLOR_MASK(1, 1, 1, 1));
    eve_cmd(STENCIL_FUNC(EQUAL, 255, 255));
    eve_cmd(STENCIL_OP(KEEP, ZERO));
    eve_color(rgb);
    eve_cmd(LINE_WIDTH(16));
    eve_cmd(BEGIN(RECTS));
    eve_cmd(VERTEX2F(x * 16, y * 16));
    eve_cmd(VERTEX2F((x + width) * 16, (y + height) * 16));
    eve_cmd(END());

    /* 3. Stencilled edges are hard; a 1 px line along the outline smooths them. */
    eve_cmd(STENCIL_FUNC(ALWAYS, 0, 255));
    eve_cmd(STENCIL_OP(KEEP, KEEP));
    eve_cmd(LINE_WIDTH(8));
    eve_cmd(BEGIN(LINE_STRIP));
    outline_vertices(x, y, width);
    eve_cmd(END());

    eve_cmd(RESTORE_CONTEXT());
}
