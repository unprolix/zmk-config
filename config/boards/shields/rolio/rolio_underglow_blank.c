/*
 * Blank the underglow strip at boot.
 *
 * ZMK's zmk_rgb_underglow_init() only starts its render timer when
 * CONFIG_ZMK_RGB_UNDERGLOW_ON_START is set; with ON_START=n it never writes to
 * the strip at all. The SK6803s therefore keep whatever state they powered up
 * in, which on this board means lit. Writing one all-zero frame settles them.
 *
 * Deliberately not zmk_rgb_underglow_off(): that also persists state to
 * settings, which would mean a flash write on every boot.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(rolio_underglow_blank, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE   DT_CHOSEN(zmk_underglow)
#define STRIP_LENGTH DT_PROP(STRIP_NODE, chain_length)

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb blank_pixels[STRIP_LENGTH];

static int rolio_underglow_blank_init(void) {
    if (!device_is_ready(strip)) {
        LOG_WRN("underglow strip not ready; leaving it as powered up");
        return 0;
    }

    memset(blank_pixels, 0, sizeof(blank_pixels));

    int rc = led_strip_update_rgb(strip, blank_pixels, STRIP_LENGTH);
    if (rc != 0) {
        LOG_WRN("failed to blank underglow: %d", rc);
    }

    return 0;
}

/* After the strip driver and ZMK's own underglow init (APPLICATION default). */
SYS_INIT(rolio_underglow_blank_init, APPLICATION, 99);
