/*
 * Driving this half's strip.
 *
 * Both halves run this. Each renders only its own keys: a key position that
 * belongs to the other half simply has no LED in this half's map, so the same
 * scene-and-modifiers word paints the correct picture on each side without
 * either needing to know which side it is.
 *
 * ZMK's underglow is left compiled in but switched off at runtime. Disabling
 * it in Kconfig also removes LED_STRIP, the WS2812 SPI backend, SPI and the
 * external-power wiring that it selects, leaving the chain with no driver at
 * all -- and rolio.conf's own underglow settings then abort the build as unmet
 * dependencies.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <drivers/ext_power.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_BACKLIGHT)
#include <zmk/backlight.h>
#endif

#include "rgbkey.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE  DT_CHOSEN(zmk_underglow)
#define STRIP_COUNT DT_PROP(STRIP_NODE, chain_length)

static const struct device *strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[STRIP_COUNT];

/*
 * Which map is ours. The left half is the central (see the rolio shield's
 * Kconfig.defconfig), so the split role is what tells the two builds apart --
 * the shield name is not visible from here.
 */
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define RGBKEY_OUR_MAP rgbkey_led_left
#else
#define RGBKEY_OUR_MAP rgbkey_led_right
#endif

/*
 * THE STRIP HAS TWO SUPPLIES, AND BOTH MUST BE UP BEFORE A WRITE LANDS.
 *
 *   P1.11  the zmk,ext-power-generic rail. It also feeds the display and the
 *          peripheral, so it is raised once and NEVER lowered -- dropping it
 *          to save a few milliamps would blank the screen.
 *   P1.13  the "backlight" PWM channel, which on this board gates the LED
 *          chain rather than lighting anything (rolio.dtsi sets
 *          zmk,backlight = &underglow_power). This one is ours to switch.
 *
 * The gate is what made the first version of this shield look like dead
 * hardware. A WS2812 needs its supply stable before it can latch the bits
 * clocked at it, and opening the gate immediately before the write gives it no
 * time at all -- so the write is swallowed, and since a write only happens
 * when the picture changes, nothing ever appears. The calibration build did
 * not hit this only because it ran with CONFIG_ZMK_BACKLIGHT_ON_START=y and so
 * had the gate open from boot.
 */
#define RGBKEY_GATE_SETTLE_MS 25

static void ensure_rail(void) {
    static bool done;
    if (done) {
        return;
    }
    done = true;

#if DT_HAS_COMPAT_STATUS_OKAY(zmk_ext_power_generic)
    const struct device *ext = DEVICE_DT_GET_ANY(zmk_ext_power_generic);
    if (ext != NULL && device_is_ready(ext)) {
        ext_power_enable(ext);
    }
#endif
}

/*
 * Returns true if the gate was shut as far as we knew and has just been opened,
 * meaning the strip needs time to wake before it can be written.
 *
 * The gate is re-asserted on EVERY call rather than only on a change. Caching
 * it would be enough if rgbkey were the only writer, but ZMK's backlight has
 * its own idle handling and can close the gate without going through here --
 * after which a cache reading "open" would clock every subsequent write into an
 * unpowered chain, silently, for ever. rgbkey_rolio.conf turns that auto-off
 * off; re-asserting as well means a stray writer costs one dim frame instead of
 * a dead strip.
 */
static bool set_gate(bool on) {
    static int gate = -1;
    const bool changed = gate != (int)on;
    gate = (int)on;

#if IS_ENABLED(CONFIG_ZMK_BACKLIGHT)
    if (on) {
        zmk_backlight_on();
        return changed;
    }
    zmk_backlight_off();
#else
    ARG_UNUSED(on);
    LOG_WRN("No backlight gate; the strip may stay dark");
#endif
    return false;
}

static const struct rgbkey_layer *row_for_scene(enum rgbkey_scene scene) {
    for (size_t i = 0; i < RGBKEY_LAYER_COUNT; i++) {
        if (rgbkey_layers[i].scene == scene) {
            return &rgbkey_layers[i];
        }
    }
    return NULL;
}

static void paint(uint8_t position, enum rgbkey_colour colour) {
    if (position >= RGBKEY_POSITIONS) {
        return;
    }
    uint8_t led = RGBKEY_OUR_MAP[position];
    if (led == RGBKEY_LED_NONE || led >= STRIP_COUNT) {
        return;
    }
    pixels[led].r = rgbkey_palette[colour][0];
    pixels[led].g = rgbkey_palette[colour][1];
    pixels[led].b = rgbkey_palette[colour][2];
}

/* Push whatever is in `pixels` to the chain, bringing the supplies up first. */
static void flush(bool any) {
    ensure_rail();
    if (set_gate(any)) {
        /*
         * Only ever waited on the dark -> lit transition, which is rare, and
         * this is the low-priority queue precisely so that a pause here costs
         * nothing that matters.
         */
        k_msleep(RGBKEY_GATE_SETTLE_MS);
    }
    if (led_strip_update_rgb(strip, pixels, STRIP_COUNT) != 0) {
        LOG_WRN("Failed to update LED strip");
    }
}

/*
 * BOOT SELF-TEST.
 *
 * Runs on each half independently, without needing the split link, the keymap
 * or a single keypress. It answers the one question that is otherwise
 * impossible to answer from the far end of an ssh connection: is the strip
 * wired, powered and writable at all?
 *
 *   lit at boot  -> the hardware path works, so any later failure is in the
 *                   scene or modifier logic
 *   dark at boot -> the failure is power, the driver, or the chain itself, and
 *                   nothing about layers or modifiers is worth looking at
 *
 * The pattern deliberately marks both ends and the middle of the chain, so it
 * also shows at a glance whether the whole chain is being driven or only part.
 */
#define RGBKEY_SELFTEST_SECONDS 20

static atomic_t selftest_running = ATOMIC_INIT(1);

/* Hoisted out of rgbkey_apply so the self-test can invalidate it on the way out. */
static atomic_t last = ATOMIC_INIT(-1);
static atomic_t pending;

static void selftest_end(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(selftest_end_work, selftest_end);

static void selftest_begin(struct k_work *work) {
    ARG_UNUSED(work);

    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip not ready; self-test cannot run");
        atomic_set(&selftest_running, 0);
        return;
    }

    memset(pixels, 0, sizeof(pixels));
    /* First, middle and last LED, in three colours that cannot be confused. */
    pixels[0].r = RGBKEY_MID;
    if (STRIP_COUNT > 11) {
        pixels[11].g = RGBKEY_MID;
    }
    if (STRIP_COUNT > 0) {
        pixels[STRIP_COUNT - 1].b = RGBKEY_MID;
    }
    flush(true);

    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &selftest_end_work,
                                K_SECONDS(RGBKEY_SELFTEST_SECONDS));
}

static K_WORK_DELAYABLE_DEFINE(selftest_begin_work, selftest_begin);

static void selftest_end(struct k_work *work) {
    ARG_UNUSED(work);

    memset(pixels, 0, sizeof(pixels));
    flush(false);

    /* Nothing has been painted for real yet, so let the next word through. */
    atomic_set(&last, -1);
    atomic_set(&selftest_running, 0);
}

/*
 * WS2812 data is clocked out as precisely-timed SPI bits, so a transfer that
 * gets preempted comes out corrupted -- which looks like the wrong colour, or
 * the right colour at the wrong brightness, rather than like an error. Do the
 * writes on the low-priority queue, the same one ZMK's own underglow uses,
 * rather than from the event thread alongside BLE and display work.
 */
static void strip_write(struct k_work *work) {
    ARG_UNUSED(work);

    if (atomic_get(&selftest_running)) {
        return; /* do not paint over the self-test */
    }

    uint32_t packed = (uint32_t)atomic_get(&pending);
    enum rgbkey_scene scene = rgbkey_scene_of(packed);
    zmk_mod_flags_t mods = rgbkey_mods_of(packed);

    memset(pixels, 0, sizeof(pixels));

    const struct rgbkey_layer *row = row_for_scene(scene);

    /* The layer's key groups first, so a held modifier can draw over them. */
    if (row != NULL) {
        for (size_t g = 0; g < ARRAY_SIZE(row->groups); g++) {
            const struct rgbkey_group *group = &row->groups[g];
            if (group->colour == RK_OFF) {
                continue; /* an unused slot in the row */
            }
            for (size_t p = 0; p < RGBKEY_GROUP_MAX; p++) {
                if (group->positions[p] == RGBKEY_POS_NONE) {
                    continue;
                }
                paint(group->positions[p], group->colour);
            }
        }
    }

    /*
     * Held modifiers last. A layer with no row of its own -- the base layer,
     * where this matters most -- still shows them.
     */
    if (row == NULL || row->hrm) {
        for (size_t i = 0; i < ARRAY_SIZE(rgbkey_hrms); i++) {
            if (mods & rgbkey_hrms[i].mod) {
                paint(rgbkey_hrms[i].position, rgbkey_hrms[i].colour);
            }
        }
    }

    bool any = false;
    for (int i = 0; i < STRIP_COUNT; i++) {
        if (pixels[i].r || pixels[i].g || pixels[i].b) {
            any = true;
            break;
        }
    }

    flush(any);
}

static K_WORK_DEFINE(strip_work, strip_write);

void rgbkey_apply(uint32_t packed) {
    if (!device_is_ready(strip)) {
        return;
    }

    /*
     * Repainting an unchanged strip is pure risk: every write is another
     * chance to be preempted mid-transfer, and this is called on every keycode
     * event.
     */
    if (atomic_get(&last) == (atomic_val_t)packed) {
        return;
    }
    atomic_set(&last, (atomic_val_t)packed);

    atomic_set(&pending, (atomic_val_t)packed);
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &strip_work);
}

/*
 * Late enough that the rolio shield's boot blanking and ZMK's own underglow
 * have both had their turn at the chain; any earlier and the self-test is
 * simply overwritten.
 */
#define RGBKEY_SELFTEST_DELAY_SECONDS 5

static int rgbkey_strip_init(void) {
    /*
     * Everything that touches the strip runs on the one queue, so the
     * self-test and the ordinary repaints can never overlap on `pixels` --
     * and the settle sleep in flush() stays off the system queue, where it
     * would hold up unrelated work.
     */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &selftest_begin_work,
                                K_SECONDS(RGBKEY_SELFTEST_DELAY_SECONDS));
    return 0;
}

SYS_INIT(rgbkey_strip_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
