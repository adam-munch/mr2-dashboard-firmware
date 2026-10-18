#include "ui.h"

#define FONT_BIG_ROM  33
#define FONT_HUGE_ROM 34

void ui_attach_fonts(void)
{
    eve_cmd_romfont(FONT_HUGE, FONT_HUGE_ROM);
    eve_cmd_romfont(FONT_BIG, FONT_BIG_ROM);
}

void ui_rect_vertices(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t radius)
{
    eve_cmd(VERTEX2F((x0 + radius) * 16, (y0 + radius) * 16));
    eve_cmd(VERTEX2F((x1 - radius) * 16, (y1 - radius) * 16));
}

void ui_rect(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t rgb, int32_t radius)
{
    if (x1 - x0 < 2 * radius || y1 - y0 < 2 * radius) {
        return;
    }
    eve_color(rgb);
    eve_cmd(LINE_WIDTH(radius * 16));
    eve_cmd(BEGIN(RECTS));
    ui_rect_vertices(x0, y0, x1, y1, radius);
    eve_cmd(END());
}

void ui_text_heavy(int16_t x, int16_t y, int16_t font, const char *s)
{
    for (int16_t dx = -1; dx <= 1; dx++) {
        eve_cmd_text((int16_t)(x + dx), y, font, OPT_CENTER, s);
    }
}

void ui_dot(int32_t cx, int32_t cy, int32_t r, uint32_t rgb)
{
    eve_color(rgb);
    eve_cmd(POINT_SIZE(r * 16));
    eve_cmd(BEGIN(POINTS));
    eve_cmd(VERTEX2F(cx * 16, cy * 16));
    eve_cmd(END());
}

void ui_check(int32_t cx, int32_t cy, uint32_t rgb)
{
    eve_color(rgb);
    eve_cmd(LINE_WIDTH(22));
    eve_cmd(BEGIN(LINE_STRIP));
    eve_cmd(VERTEX2F((cx - 5) * 16, cy * 16));
    eve_cmd(VERTEX2F((cx - 1) * 16 - 8, (cy + 4) * 16));
    eve_cmd(VERTEX2F((cx + 6) * 16, (cy - 4) * 16 - 8));
    eve_cmd(END());
}

void ui_degree(int32_t cx, int32_t cy, uint32_t rgb, uint32_t bg)
{
    eve_cmd(BEGIN(POINTS));
    eve_color(rgb);
    eve_cmd(POINT_SIZE(3 * 16));
    eve_cmd(VERTEX2F(cx * 16, cy * 16));
    eve_color(bg);
    eve_cmd(POINT_SIZE(26));
    eve_cmd(VERTEX2F(cx * 16, cy * 16));
    eve_cmd(END());
}

int32_t ui_spread_x(uint32_t i, uint32_t n, int32_t x0, int32_t x1, int32_t gap)
{
    return x0 + (int32_t)i * (x1 - x0 + gap) / (int32_t)n;
}
