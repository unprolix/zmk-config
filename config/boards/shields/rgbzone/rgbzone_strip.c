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
#include <zmk/workqueue.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE  DT_CHOSEN(zmk_underglow)
#define STRIP_COUNT DT_PROP(STRIP_NODE, chain_length)

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_COUNT];

/*
 * The eyelash's EXT_POWER node carries init-delay-ms = 50: the rail needs time
 * to come up before anything downstream is usable. Zephyr's driver honours
 * that at init but not on later enable() calls, and a WS2812 cannot latch data
 * until its supply is stable -- so enabling the rail and writing in the same
 * breath loses the write. That is invisible here: there is no error, and
 * because the strip is only written when the picture changes, a swallowed
 * write is never retried and the LEDs simply never come on.
 */
#define EXT_POWER_SETTLE_MS 50

/* Returns true if the rail was just turned on and needs settling. */
static bool set_power(bool on) {
    static int state = -1;
    if (state == (int)on) {
        return false;
    }

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic)
    const struct device *ext = DEVICE_DT_GET_ANY(zmk_ext_power_generic);
#else
    const struct device *ext = NULL;
#endif
    if (ext == NULL || !device_is_ready(ext)) {
        state = (int)on;
        return false;
    }

    if (on ? ext_power_enable(ext) == 0 : ext_power_disable(ext) == 0) {
        state = (int)on;
        return on;
    }
    return false;
}

/*
 * WS2812 data is clocked out as precisely-timed SPI bits, so a transfer that
 * gets preempted comes out corrupted -- which looks like the wrong colour, or
 * the right colour at the wrong brightness, rather than like an error. Writing
 * from the event thread on every keycode put those transfers right next to BLE
 * and display work. Do them on the low-priority queue instead, the same one
 * ZMK's own underglow uses, and only when something actually changed.
 */
static atomic_t pending;

void rgbzone_diag_report_write(uint32_t packed, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                               uint8_t g1, uint8_t b1);

static void strip_write(struct k_work *work) {
    ARG_UNUSED(work);

    uint32_t packed = (uint32_t)atomic_get(&pending);

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
    /*
     * Safe to sleep here: this runs on the low-priority work queue, which is
     * also why the strip write was moved off the event thread in the first
     * place.
     */
    if (set_power(any)) {
        k_msleep(EXT_POWER_SETTLE_MS);
    }

    if (led_strip_update_rgb(strip, pixels, STRIP_COUNT) != 0) {
        LOG_WRN("Failed to update LED strip");
    }

    /*
     * Report what was actually handed to the strip, not what was computed.
     * The two had been assumed identical; this is where that assumption gets
     * checked.
     */
    rgbzone_diag_report_write(packed, pixels[0].r, pixels[0].g, pixels[0].b, pixels[1].r,
                              pixels[1].g, pixels[1].b);
}

static K_WORK_DEFINE(strip_work, strip_write);

/* ------------------------------------------------------------------ */
/* Diagnostics: drive pixels directly, ignoring layers and the palette */
/* ------------------------------------------------------------------ */

static bool diag_on;
static struct led_rgb diag_pixels[STRIP_COUNT];

static void diag_flush(struct k_work *work) {
    ARG_UNUSED(work);
    if (!device_is_ready(strip)) {
        return;
    }

    bool any = false;
    for (int i = 0; i < STRIP_COUNT; i++) {
        if (diag_pixels[i].r || diag_pixels[i].g || diag_pixels[i].b) {
            any = true;
            break;
        }
    }

    if (set_power(any)) {
        k_msleep(EXT_POWER_SETTLE_MS);
    }
    if (led_strip_update_rgb(strip, diag_pixels, STRIP_COUNT) != 0) {
        LOG_WRN("Failed to update LED strip (diag)");
    }
}

static K_WORK_DEFINE(diag_work, diag_flush);

bool rgbzone_diag_active(void) { return diag_on; }

void rgbzone_diag_pixel(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (index >= STRIP_COUNT) {
        return;
    }
    diag_on = true;
    diag_pixels[index].r = r;
    diag_pixels[index].g = g;
    diag_pixels[index].b = b;
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &diag_work);
}

void rgbzone_diag_all(uint8_t r, uint8_t g, uint8_t b) {
    diag_on = true;
    for (int i = 0; i < STRIP_COUNT; i++) {
        diag_pixels[i].r = r;
        diag_pixels[i].g = g;
        diag_pixels[i].b = b;
    }
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &diag_work);
}

void rgbzone_diag_release(void) {
    diag_on = false;
    memset(diag_pixels, 0, sizeof(diag_pixels));
    /* Force the next normal write through: the dedupe would otherwise see an
       unchanged value and leave the poked picture on the strip. */
    extern void rgbzone_force_next(void);
    rgbzone_force_next();
}

static atomic_t last = ATOMIC_INIT(0xFFFFFFFF);

void rgbzone_force_next(void) { atomic_set(&last, (atomic_val_t)0xFFFFFFFF); }

void rgbzone_apply(uint32_t packed) {
    if (!device_is_ready(strip)) {
        return;
    }

    /* A host override owns the strip until it is released. */
    if (diag_on) {
        return;
    }

    /*
     * Repainting an unchanged strip is pure risk: every write is another
     * chance to be preempted mid-transfer, and this is called on every
     * keycode event.
     */
    if (atomic_get(&last) == (atomic_val_t)packed) {
        return;
    }
    atomic_set(&last, (atomic_val_t)packed);

    atomic_set(&pending, (atomic_val_t)packed);
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &strip_work);
}

