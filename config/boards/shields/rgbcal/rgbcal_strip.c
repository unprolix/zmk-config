/*
 * Direct control of the WS2812 chain, on whichever half this is built for.
 *
 * ZMK's own underglow owns the strip and only ever drives it as one animated
 * whole, so the calibration build turns it off (see rgbcal.conf) and writes
 * pixels here instead.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/ext_power.h>

#include "rgbcal.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE  DT_CHOSEN(zmk_underglow)
#define STRIP_COUNT DT_PROP(STRIP_NODE, chain_length)

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_COUNT];

/*
 * The strip runs off switched external power. ZMK's underglow normally turns
 * that on, but this build disables underglow to get the chain to itself -- so
 * without doing it here the LEDs stay dark no matter what is written to them.
 */
static void ensure_power(void) {
    static bool powered;
    if (powered) {
        return;
    }

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic)
    const struct device *ext = DEVICE_DT_GET_ANY(zmk_ext_power_generic);
#else
    const struct device *ext = NULL;
#endif
    if (ext == NULL || !device_is_ready(ext)) {
        LOG_WRN("No external power control; strip may stay dark");
        powered = true; /* nothing more to try */
        return;
    }
    if (ext_power_enable(ext) == 0) {
        powered = true;
    }
}

void rgbcal_light_all(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip not ready");
        return;
    }
    ensure_power();

    for (int i = 0; i < STRIP_COUNT; i++) {
        pixels[i].r = RGBCAL_LEVEL;
        pixels[i].g = RGBCAL_LEVEL;
        pixels[i].b = RGBCAL_LEVEL;
    }
    int err = led_strip_update_rgb(strip, pixels, STRIP_COUNT);
    if (err) {
        LOG_ERR("Failed to update LED strip: %d", err);
    }
}

void rgbcal_light_stripes(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip not ready");
        return;
    }
    ensure_power();

    /* index 0 red, 1 green, 2 blue, 3 white, rest dark. */
    memset(pixels, 0, sizeof(pixels));
    if (STRIP_COUNT > 0) {
        pixels[0].r = RGBCAL_LEVEL;
    }
    if (STRIP_COUNT > 1) {
        pixels[1].g = RGBCAL_LEVEL;
    }
    if (STRIP_COUNT > 2) {
        pixels[2].b = RGBCAL_LEVEL;
    }
    if (STRIP_COUNT > 3) {
        pixels[3].r = pixels[3].g = pixels[3].b = RGBCAL_LEVEL;
    }

    int err = led_strip_update_rgb(strip, pixels, STRIP_COUNT);
    if (err) {
        LOG_ERR("Failed to update LED strip: %d", err);
    }
}

void rgbcal_light(uint16_t index) {
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip not ready");
        return;
    }
    ensure_power();

    memset(pixels, 0, sizeof(pixels));
    if (index < STRIP_COUNT) {
        /* White: the point is to see *which* LED, not what colour it can do. */
        pixels[index].r = RGBCAL_LEVEL;
        pixels[index].g = RGBCAL_LEVEL;
        pixels[index].b = RGBCAL_LEVEL;
    }

    int err = led_strip_update_rgb(strip, pixels, STRIP_COUNT);
    if (err) {
        LOG_ERR("Failed to update LED strip: %d", err);
    }
}
