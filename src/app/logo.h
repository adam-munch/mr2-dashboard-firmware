/*
 * Team logo, stored as its outline and drawn by the FT81x as a filled
 * polygon. The whole logo is a 136-byte table in flash: no bitmap, nothing
 * loaded into graphics RAM, and it scales to any size with clean edges.
 */
#ifndef LOGO_H
#define LOGO_H

#include <stdint.h>

/* Size of the artwork in outline units (pixels of the source image). */
#define LOGO_WIDTH  683
#define LOGO_HEIGHT 350

typedef struct {
    uint16_t x;
    uint16_t y;
} logo_point_t;

/* The outline, in order. The last point joins back to the first. */
extern const logo_point_t logo_outline[];
extern const uint32_t logo_outline_len;

/* Height in pixels of the logo when drawn `width` pixels wide. */
int32_t logo_height(int32_t width);

/*
 * Draw the logo `width` pixels wide with its top-left corner at (x, y).
 * Must lie fully on screen. Uses the stencil buffer inside the logo's
 * bounding box and leaves it cleared there; all other graphics state is
 * restored afterwards.
 */
void logo_draw(int32_t x, int32_t y, int32_t width, uint32_t rgb);

#endif /* LOGO_H */
