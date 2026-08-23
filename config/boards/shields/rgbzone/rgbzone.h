/*
 * Per-layer column colours.
 *
 * The hardware gives six addressable zones per half, one per column -- see
 * zone_map.h -- so this cannot colour individual keys. What each layer looks
 * like is stated outright in zone_palette.h rather than inferred from the
 * keymap; inference produced colours that were hard to predict and, because
 * the base layer is deliberately dark, hid the home-row mods entirely.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

#include "zone_map.h"
#include "zone_palette.h"

/*
 * Six zones, four bits each: twenty-four bits, which fits the single parameter
 * a relayed behaviour can carry to the far half. Full RGB would not.
 */
static inline uint32_t rgbzone_pack(const enum zone_colour *colours) {
    uint32_t packed = 0;
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
        packed |= ((uint32_t)(colours[i] & 0xF)) << (i * 4);
    }
    return packed;
}

static inline enum zone_colour rgbzone_unpack(uint32_t packed, uint8_t zone) {
    enum zone_colour c = (enum zone_colour)((packed >> (zone * 4)) & 0xF);
    return c < ZC_COUNT ? c : ZC_OFF;
}

/*
 * Brightness rides in the byte above the six zones.
 *
 * It has to reach the far half, and the relayed behaviour that carries the
 * colours is the only channel there is. Six four-bit zones leave the top eight
 * bits of that word free, so the level travels with the picture it applies to
 * rather than as a second message that could arrive out of order -- and the
 * repaint dedupe then covers brightness changes for free.
 */
#define RGBZONE_LEVEL_SHIFT 24
#define RGBZONE_LEVEL_MASK  0x7

static inline uint32_t rgbzone_with_level(uint32_t packed, uint8_t level) {
    return packed | ((uint32_t)(level & RGBZONE_LEVEL_MASK) << RGBZONE_LEVEL_SHIFT);
}

static inline uint8_t rgbzone_level_of(uint32_t packed) {
    uint8_t level = (packed >> RGBZONE_LEVEL_SHIFT) & RGBZONE_LEVEL_MASK;
    return level < ZONE_LEVEL_COUNT ? level : ZONE_LEVEL_DEFAULT;
}

/* Light this half's zones from a packed set of palette indices. */
void rgbzone_apply(uint32_t packed);

/* ------------------------------------------------------------------ */
/* Layer ownership                                                     */
/* ------------------------------------------------------------------ */

/*
 * Which half is holding a layer open, recorded by the momentary-layer
 * behaviour in rgbzone_owner.c rather than inferred from key presses -- see
 * the note there for why inference does not work. rgbzone_owner_side()
 * returns false for a layer that was not opened by holding a key.
 */
void rgbzone_owner_pressed(uint8_t layer, uint32_t position, bool from_right);
void rgbzone_owner_released(uint8_t layer, uint32_t position);
bool rgbzone_owner_side(uint8_t layer, bool *from_right);

/* ------------------------------------------------------------------ */
/* Brightness                                                          */
/* ------------------------------------------------------------------ */

/*
 * The live level, 0 (off) to ZONE_LEVEL_COUNT - 1. Central only: the far half
 * has no say, it applies whatever arrives packed with the colours. Kept in
 * settings, so it survives a reboot.
 */
uint8_t rgbzone_level_get(void);
void rgbzone_level_step(int delta);
void rgbzone_level_toggle(void);

/* ------------------------------------------------------------------ */
/* Diagnostics                                                         */
/* ------------------------------------------------------------------ */

/*
 * A way to drive the strip directly from the host, bypassing layers, mods and
 * the palette entirely. Without it, "the wrong column lit in the wrong colour"
 * cannot be told apart from "the layer logic picked the wrong column" or "the
 * colour never reached the far half" -- and each guess costs a flash cycle.
 *
 * While an override is in force the normal path stops writing, so whatever was
 * poked in stays put and can be looked at.
 */
void rgbzone_diag_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void rgbzone_diag_all(uint8_t r, uint8_t g, uint8_t b);
void rgbzone_diag_release(void);
bool rgbzone_diag_active(void);
