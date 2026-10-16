/* Integer trigonometry for drawing (no libm, no floats). */
#ifndef FIXMATH_H
#define FIXMATH_H

#include <stdint.h>

/* Angles are in 1/1024 of a turn, counter-clockwise from +X (3 o'clock). */
#define ANGLE_FULL 1024u
#define ANGLE_DEG(d) ((uint32_t)((d) * 1024 / 360))

/* Sine / cosine scaled by 16384 (Q14). */
int32_t isin_q14(uint32_t angle);
int32_t icos_q14(uint32_t angle);

#endif /* FIXMATH_H */
