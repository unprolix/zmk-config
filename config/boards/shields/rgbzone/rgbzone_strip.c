/*
 * Driving this half's six zones.
 *
 * ZMK's underglow is left compiled in but switched off at runtime: disabling
 * it in Kconfig also removes LED_STRIP, the WS2812 SPI backend, SPI and the
 * external-power wiring that it selects, leaving the chain with no driver at
 * all. Its off() cuts external power on the way out, so that gets turned back
 * on here.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/ext_power.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE  DT_CHOSEN(zmk_underglow)
#define STRIP_COUNT DT_PROP(STRIP_NODE, chain_length)

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_COUNT];

static void set_power(bool on) {
    static int state = -1;
    if (state == (int)on) {
        return;
    }

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic)
    const struct device *ext = DEVICE_DT_GET_ANY(zmk_ext_power_generic);
#else
    const struct device *ext = NULL;
#endif
    if (ext == NULL || !device_is_ready(ext)) {
        state = (int)on;
        return;
    }

    if (on ? ext_power_enable(ext) == 0 : ext_power_disable(ext) == 0) {
        state = (int)on;
    }
}

void rgbzone_apply(uint32_t packed) {
    if (!device_is_ready(strip)) {
        return;
    }

    memset(pixels, 0, sizeof(pixels));

    bool any = false;
    for (uint8_t zone = 0; zone < ZONE_COUNT && zone < STRIP_COUNT; zone++) {
        enum zone_colour c = rgbzone_unpack(packed, zone);
        pixels[zone].r = zone_palette[c][0];
        pixels[zone].g = zone_palette[c][1];
        pixels[zone].b = zone_palette[c][2];
        if (c != ZC_OFF) {
            any = true;
        }
    }

    /*
     * Only hold the strip's supply up while something is actually lit; an
     * all-dark layer should cost nothing.
     */
    set_power(any);
    if (led_strip_update_rgb(strip, pixels, STRIP_COUNT) != 0) {
        LOG_WRN("Failed to update LED strip");
    }
}

