/*
 * Shared bits of the RGB calibration build.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

/* param1 value meaning "light nothing". */
#define RGBCAL_ALL_OFF 0xFFFF

/* param1 value meaning "light the whole chain". */
#define RGBCAL_LIGHT_ALL 0xFFFE

/* param1 value meaning "light the colour-stripe test pattern". */
#define RGBCAL_LIGHT_STRIPES 0xFFFD

/*
 * Brightness of the single lit LED. Deliberately dim: this is looked at from
 * a few inches away in a lit room, and a WS2812 at full tilt is both dazzling
 * and a serious current draw.
 */
#define RGBCAL_LEVEL 40

/* Light `index` alone on this half's strip, or everything off. */
void rgbcal_light(uint16_t index);

/*
 * Light the entire chain. Nothing documents how many LEDs this board actually
 * has or where they sit -- chain-length says 21, but only six proved visible --
 * so start a run by lighting everything, which answers both questions at once.
 */
void rgbcal_light_all(void);

/*
 * Light the first few indices in distinct colours at once.
 *
 * Walking the chain one pixel at a time cannot tell "one LED per column" from
 * "one LED per key, with each write smearing across its neighbours" -- both
 * look like a whole column lighting. Distinct colours separate them: per
 * column gives solid bands of one colour each, smearing gives blended or
 * repeated colour within a band.
 */
void rgbcal_light_stripes(void);
