/*
 * Rotary encoder with push switch: the knob that drives the menus.
 * encoder_stm32.c implements it with a timer in encoder mode (pins in
 * board.h); the host tests swap in a mock.
 *
 * This is the raw hardware: a quadrature edge count and the switch level.
 * knob.h turns them into clicks and presses.
 */
#ifndef ENCODER_H
#define ENCODER_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Quadrature edges per detent. The Bourns PEC11L-4120K-S0020 has 20 detents
 * and 20 pulses per turn: one full cycle of A and B, 4 edges, per click.
 * An encoder with half as many pulses as detents (e.g. 12 pulses, 24
 * detents) needs 2.
 */
#ifndef ENCODER_COUNTS_PER_DETENT
#define ENCODER_COUNTS_PER_DETENT 4
#endif

/* Configure the pins and start counting. The count starts at 0. */
void encoder_hw_init(void);

/* Edges of A and B counted since encoder_hw_init(), up for clockwise (if
 * it counts the wrong way, see ENC_REVERSE in board.h). Wraps at 16 bits:
 * take differences, not absolute values. */
uint16_t encoder_hw_count(void);

/* True while the shaft is pressed in. Raw: it bounces for a few ms. */
bool encoder_hw_switch(void);

#endif /* ENCODER_H */
