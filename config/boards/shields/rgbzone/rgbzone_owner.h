/*
 * Which half is holding a layer open.
 *
 * Split out of rgbzone.h so that rgbkey can use the same mechanism without
 * dragging in the eyelash's zone map and palette. The implementation is
 * rgbzone_owner.c, which also provides the momentary-layer behaviour that feeds
 * it -- jjb.keymap retargets ZMK's &mo at that behaviour whenever a shield
 * defines RGBZONE_PRESENT, so every route into a momentary layer is covered:
 * &mo, &lt, the hold-tap halves, the sticky-layer keys, the layer+modifier
 * macros.
 *
 * The holding hand CANNOT be inferred after the fact -- "whichever hand typed
 * last" is wrong the moment the other hand does anything -- so it is recorded
 * at the only point where it is known for certain: the key that opened the
 * layer.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

void rgbzone_owner_pressed(uint8_t layer, uint32_t position, bool from_right);
void rgbzone_owner_released(uint8_t layer, uint32_t position);

/*
 * True if the holding side is known for this layer, with *from_right set.
 * False when the layer was not opened by a momentary press at all -- a locked
 * layer, or one entered with &to -- in which case there is no holding hand and
 * the caller should light both halves rather than guess at one.
 */
bool rgbzone_owner_side(uint8_t layer, bool *from_right);

/*
 * The same lookup, but also handing back WHICH key it was. rgbkey lights that
 * key in the layer's own colour, so the thumb you are holding says what it is
 * holding open -- on a per-key board the position is the whole point, and it
 * is already recorded, so nothing new has to be tracked to get it.
 *
 * Positions are this keyboard's matrix positions, the same numbering the
 * layout tables use. Only meaningful when the function returns true.
 */
bool rgbzone_owner_key(uint8_t layer, bool *from_right, uint8_t *position);
