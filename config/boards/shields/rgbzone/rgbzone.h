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

/* Light this half's zones from a packed set of palette indices. */
void rgbzone_apply(uint32_t packed);
