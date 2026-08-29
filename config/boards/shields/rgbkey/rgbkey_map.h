/*
 * Key position -> LED index. The tables themselves are per board.
 *
 * The Rolio's were measured on hardware with the rgbcal build. The Corne v4's
 * did not have to be: foostan publishes rgb_matrix.layout in QMK, which tags
 * every LED in chain order with the matrix position it sits under, so the map
 * is derived from vendor data instead of discovered. Both boards snake -- the
 * chain reverses row to row -- which is why neither table is monotonic.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

/* This position has no LED on this half. */
#define RGBKEY_LED_NONE 0xFF

#if defined(CONFIG_SHIELD_CORNE_V4_LEFT) || defined(CONFIG_SHIELD_CORNE_V4_RIGHT)
#include "rgbkey_map_corne_v4.h"
#elif defined(CONFIG_SHIELD_ROLIO_LEFT) || defined(CONFIG_SHIELD_ROLIO_RIGHT)
#include "rgbkey_map_rolio.h"
#else
#error "rgbkey has no LED map for this shield -- add rgbkey_map_<board>.h"
#endif

BUILD_ASSERT(ARRAY_SIZE(rgbkey_led_left) == RGBKEY_POSITIONS,
             "Left LED map must name every matrix position");
BUILD_ASSERT(ARRAY_SIZE(rgbkey_led_right) == RGBKEY_POSITIONS,
             "Right LED map must name every matrix position");
