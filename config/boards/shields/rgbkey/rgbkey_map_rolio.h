/*
 * Rolio key position -> LED index, per half.
 *
 * Split out of rgbkey_map.h when the Corne v4 arrived; the tables and the
 * account of how they were measured are unchanged.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>


/*
 * Key position -> LED index, per half.
 *
 * MEASURED on hardware 2026-08-22 with the rgbcal build, not guessed. The
 * chain is genuinely per-key on this board: all 23 indices lit a distinct key,
 * which is what separates it from the eyelash corne, where `chain-length =
 * <21>` also matched the key count but indices 6..20 drove nothing at all
 * because each column is wired in parallel onto one address.
 *
 * The chain snakes. Each row reverses relative to the one above it, so a table
 * that assumes a single direction throughout is wrong on two rows out of three:
 *
 *     LED  0..5   top row,    inner -> outer
 *     LED  6..11  middle row, outer -> inner
 *     LED 12..17  bottom row, inner -> outer
 *     LED 18..22  the two bottom-row extras, then the three thumbs
 *
 * 23 LEDs against 24 keycaps a side: the roller push switch (LEC 30 / REC 31)
 * is the one key with no LED of its own.
 *
 * The right half's last four entries are filled in by mirror rather than read
 * off its calibration run, which returned 18:6 19:7 20:8 22:36 -- positions
 * already claimed by LEDs 0, 1, 2 and 16, so those four presses did not land
 * on the lit key. The mirror is on firm ground: indices 0..17 match the left
 * half exactly, and the one tail entry that did come back clean (21:44, RH1)
 * falls precisely where symmetry predicts. The two halves are the same board
 * artwork, so a chain that ran differently on one side would be the surprise.
 * If the far half's thumbs or bottom-row extras ever light the wrong key,
 * these four are what to correct.
 *
 * Positions are the 48 matrix positions from config/rolio48.h. Each table
 * covers all 48 so a lookup never has to know whose position it is -- a
 * position belonging to the other half reads as RGBKEY_LED_NONE.
 *

/* This position has no LED on this half. */
#define RGBKEY_LED_NONE 0xFF

#define RGBKEY_POSITIONS 48

/* Short alias so the tables below stay readable as a grid. */
#define N RGBKEY_LED_NONE

/* Left half, as measured. */
static const uint8_t rgbkey_led_left[RGBKEY_POSITIONS] = {
    /*   0..5  LT5..LT0, left top row    */  5,  4,  3,  2,  1,  0,
    /*  6..11  RT0..RT5, other half      */  N,  N,  N,  N,  N,  N,
    /* 12..17  LM5..LM0, left middle row */  6,  7,  8,  9, 10, 11,
    /* 18..23  RM0..RM5, other half      */  N,  N,  N,  N,  N,  N,
    /* 24..29  LB5..LB0, left bottom row */ 17, 16, 15, 14, 13, 12,
    /*     30  LEC, roller click, unlit  */  N,
    /*     31  REC, other half           */  N,
    /* 32..37  RB0..RB5, other half      */  N,  N,  N,  N,  N,  N,
    /* 38..39  LX1, LX0, bottom extras   */ 18, 19,
    /* 40..42  LH2, LH1, LH0, left thumb */ 20, 21, 22,
    /* 43..45  RH0..RH2, other half      */  N,  N,  N,
    /* 46..47  RX0, RX1, other half      */  N,  N,
};

/* Right half; indices 18, 19, 20 and 22 inferred by mirror, see above. */
static const uint8_t rgbkey_led_right[RGBKEY_POSITIONS] = {
    /*   0..5  LT5..LT0, other half      */  N,  N,  N,  N,  N,  N,
    /*  6..11  RT0..RT5, right top row   */  0,  1,  2,  3,  4,  5,
    /* 12..17  LM5..LM0, other half      */  N,  N,  N,  N,  N,  N,
    /* 18..23  RM0..RM5, right middle rw */ 11, 10,  9,  8,  7,  6,
    /* 24..29  LB5..LB0, other half      */  N,  N,  N,  N,  N,  N,
    /*     30  LEC, other half           */  N,
    /*     31  REC, roller click, unlit  */  N,
    /* 32..37  RB0..RB5, right bottom rw */ 12, 13, 14, 15, 16, 17,
    /* 38..39  LX1, LX0, other half      */  N,  N,
    /* 40..42  LH2..LH0, other half      */  N,  N,  N,
    /* 43..45  RH0, RH1, RH2, right thmb */ 22, 21, 20,
    /* 46..47  RX0, RX1, bottom extras   */ 19, 18,
};

#undef N

BUILD_ASSERT(ARRAY_SIZE(rgbkey_led_left) == RGBKEY_POSITIONS,
             "Left LED map must name every matrix position");
BUILD_ASSERT(ARRAY_SIZE(rgbkey_led_right) == RGBKEY_POSITIONS,
             "Right LED map must name every matrix position");

