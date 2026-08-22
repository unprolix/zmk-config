/*
 * Which key positions belong to which LED zone.
 *
 * The eyelash's RGB is per-COLUMN, not per-key: the LEDs in a column are wired
 * in parallel onto a single chain address, so there are six addressable zones
 * per half even though chain-length says 21. Indices 6..20 drive nothing. This
 * was established empirically -- lighting indices 0..3 in four colours gives
 * four solid vertical bands -- because nothing documents it.
 *
 * Positions come from the shield's matrix transform, numbered in the order
 * they appear in its map:
 *
 *   row 0   0..5   left      6  centre     7..12   right
 *   row 1  13..18  left  19..21 centre    22..27   right
 *   row 2  28..33  left     34  extra  35 centre   36..41  right
 *   row 3  42..44  left thumbs            45..47   right thumbs
 *
 * Zone index runs inner to outer on both halves, matching the chain order
 * measured on the left: LED i lights column 5 - i, where column 0 is the
 * outermost. Thumbs fold into the three inner columns, which is what "the
 * thumb keys right-aligned towards the centre" looked like on the hardware.
 *
 * The centre cluster (the joystick positions, 6/19/20/21/35) has no LED of its
 * own and is deliberately absent.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

#define ZONE_COUNT       6
#define ZONE_MAX_KEYS    4
#define ZONE_KEY_NONE    0xFF

/*
 * Indexed by LED index, so zone_keys_left[0] is the innermost column. The
 * right half is assumed to mirror; if its chain runs the other way the two
 * halves will simply appear reversed relative to each other, which is obvious
 * on sight and fixed by reversing this table.
 */
static const uint8_t zone_keys_left[ZONE_COUNT][ZONE_MAX_KEYS] = {
    /* LED 0 -> column 5, innermost, with its thumb */
    {5, 18, 33, 44},
    {4, 17, 32, 43},
    {3, 16, 31, 42},
    /* the remaining columns have no thumb; 34 is the odd extra key by column 2 */
    {2, 15, 30, 34},
    {1, 14, 29, ZONE_KEY_NONE},
    /* LED 5 -> column 0, outermost */
    {0, 13, 28, ZONE_KEY_NONE},
};

static const uint8_t zone_keys_right[ZONE_COUNT][ZONE_MAX_KEYS] = {
    /* LED 0 -> innermost column, with its thumb */
    {7, 22, 36, 45},
    {8, 23, 37, 46},
    {9, 24, 38, 47},
    {10, 25, 39, ZONE_KEY_NONE},
    {11, 26, 40, ZONE_KEY_NONE},
    /* LED 5 -> outermost */
    {12, 27, 41, ZONE_KEY_NONE},
};
