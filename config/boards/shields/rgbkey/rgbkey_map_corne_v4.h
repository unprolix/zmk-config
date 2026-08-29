/*
 * Corne v4 key position -> LED index, per half.
 *
 * DERIVED, not measured. foostan publishes rgb_matrix.layout in QMK's
 * keyboards/crkbd/rev4_1/standard/keyboard.json: all 46 LEDs in chain order,
 * each tagged with the matrix position it sits under. Composing that with our
 * own two mappings gives the table below without an rgbcal run:
 *
 *   foostan matrix [row, col] -> kscan index   (the two corne_v4_*.overlay files)
 *   kscan index               -> position      (the transform in corne_v4.dtsi)
 *   foostan matrix            -> LED index     (rgb_matrix.layout)
 *
 * Checked two ways: every one of the 23 LEDs on each half is claimed exactly
 * once, and the two halves came out exact mirrors of each other -- which is
 * what mirrored PCB artwork should produce, and would be the first thing to go
 * wrong if a step above were mistranscribed.
 *
 * The chain snakes, so no row runs the same way as the one above it:
 *
 *     LED  0      inner thumb
 *     LED  1..3   inner column, bottom -> top
 *     LED  4..7   next column out, top -> bottom, then its thumb
 *     ...and so on outward, ending at the two ex2 keys (21, 22).
 *
 * Unlike the Rolio, every key here has an LED: 23 emitters against 23 switches.
 *
 * If a key ever lights its neighbour, this file is derived and therefore
 * falsifiable -- build the rgbcal shield and read the real thing.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

#define RGBKEY_POSITIONS 46

/* Short alias so the tables below stay readable as a grid. */
#define N RGBKEY_LED_NONE

static const uint8_t rgbkey_led_left[RGBKEY_POSITIONS] = {
    /*   0..5   top row, left half                  */ 18, 17, 12, 11,  4,  3,
    /*   6      left ex2 upper (under the encoder)  */ 21,
    /*   7      right ex2 upper                     */  N,
    /*   8..13  top row, right half                 */  N,  N,  N,  N,  N,  N,
    /*  14..19  home row, left half                 */ 19, 16, 13, 10,  5,  2,
    /*  20      left ex2 lower                      */ 22,
    /*  21      right ex2 lower                     */  N,
    /*  22..27  home row, right half                */  N,  N,  N,  N,  N,  N,
    /*  28..33  bottom row, left half               */ 20, 15, 14,  9,  6,  1,
    /*  34..39  bottom row, right half              */  N,  N,  N,  N,  N,  N,
    /*  40..42  left thumbs                         */  8,  7,  0,
    /*  43..45  right thumbs                        */  N,  N,  N,
};

static const uint8_t rgbkey_led_right[RGBKEY_POSITIONS] = {
    /*   0..5   top row, left half                  */  N,  N,  N,  N,  N,  N,
    /*   6      left ex2 upper (under the encoder)  */  N,
    /*   7      right ex2 upper                     */ 21,
    /*   8..13  top row, right half                 */  3,  4, 11, 12, 17, 18,
    /*  14..19  home row, left half                 */  N,  N,  N,  N,  N,  N,
    /*  20      left ex2 lower                      */  N,
    /*  21      right ex2 lower                     */ 22,
    /*  22..27  home row, right half                */  2,  5, 10, 13, 16, 19,
    /*  28..33  bottom row, left half               */  N,  N,  N,  N,  N,  N,
    /*  34..39  bottom row, right half              */  1,  6,  9, 14, 15, 20,
    /*  40..42  left thumbs                         */  N,  N,  N,
    /*  43..45  right thumbs                        */  0,  7,  8,
};

#undef N
