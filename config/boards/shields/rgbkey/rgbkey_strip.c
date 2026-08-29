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
#define RGBKEY_WE_ARE_LEFT 1
#else
#define RGBKEY_OUR_MAP rgbkey_led_right
#define RGBKEY_WE_ARE_LEFT 0
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

/*
 * How long a clean frame can take before it is worth saying so. 30us a pixel on
 * the wire, the driver's reset delay after it, and a generous margin on top so
 * that ordinary jitter is not reported as tearing.
 */
#define RGBKEY_FRAME_US_PER_PIXEL 30
#define RGBKEY_FRAME_SLACK_US     500
#define RGBKEY_FRAME_SLOW_US      (STRIP_COUNT * RGBKEY_FRAME_US_PER_PIXEL + RGBKEY_FRAME_SLACK_US)

static uint32_t slow_frames;

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
    /*
     * A FRAME MUST NOT BE INTERRUPTED, AND NOTHING IN THE DRIVER ENFORCES IT.
     *
     * ws2812_led_strip_update_rgb() clocks the pixels out with
     * pio_sm_put_blocking() in a plain loop -- CPU-fed, no DMA. The PIO TX FIFO
     * is four words deep and a word is one 24-bit pixel, ~30us on the wire, so
     * there is about 120us of slack between the CPU falling behind and the line
     * going idle. A WS2812 reads ~50us of idle as END OF FRAME: it latches what
     * it has and the NEXT pixel written lands back at LED 0.
     *
     * So a preemption in the middle of a frame does not drop pixels, it SHIFTS
     * the tail of the frame onto the head of the chain. What that looks like is
     * a handful of LEDs at the start of the chain lit in colours meant for keys
     * further along, differently each time, with the keys that should be lit
     * sometimes right and sometimes missing. It looks like a table bug and is
     * not one.
     *
     * This runs on the LOW-priority workqueue, so the system workqueue preempts
     * it -- and on a split central the system workqueue is where the far half's
     * key events are published. That is why the picture only came apart while a
     * key on the OTHER half was held: that is the one thing that schedules work
     * against a repaint.
     *
     * k_sched_lock, not irq_lock. The stall that matters is another THREAD, and
     * ~700us with interrupts off would starve the wired split's read timer,
     * which drains an 8-byte PIO RX FIFO from an ISR every 200us -- and a wired
     * split that loses a fragment does not degrade, it hangs for good. Raising
     * to a cooperative priority stops the preemption while leaving every ISR
     * free to run; an ISR here is far shorter than the 120us of slack.
     */
    uint32_t started = k_cycle_get_32();
    k_sched_lock();
    int err = led_strip_update_rgb(strip, pixels, STRIP_COUNT);
    k_sched_unlock();
    uint32_t elapsed = k_cyc_to_us_floor32(k_cycle_get_32() - started);

    if (err != 0) {
        LOG_WRN("Failed to update LED strip");
    }

    /*
     * A frame is STRIP_COUNT * 30us on the wire plus the driver's reset delay.
     * Anything far past that was interrupted anyway, which is the one thing
     * that cannot be seen by looking at the strip -- a torn frame and a wrong
     * table produce the same wrong pixels. Logged only when it happens, so
     * silence here is the evidence that it does not.
     */
    if (elapsed > RGBKEY_FRAME_SLOW_US) {
        slow_frames++;
        LOG_INF("rgbkey: strip write took %uus (>%uus), torn frame likely; %u so far", elapsed,
                RGBKEY_FRAME_SLOW_US, slow_frames);
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
/*
 * The self-test is a DIAGNOSTIC and is off by default.
 *
 * While it runs, strip_write() refuses to paint anything real, so leaving it on
 * costs the first RGBKEY_SELFTEST_DELAY_SECONDS + RGBKEY_SELFTEST_SECONDS of
 * every boot -- half a minute with no layer or modifier lighting at all, on a
 * build that is flashed daily.
 *
 * Turn it back on when the strip is in doubt: lit means the hardware path works
 * and any failure is in the logic; dark means the reverse, and that is a
 * question no amount of reading the code can answer.
 */
#define RGBKEY_SELFTEST 0

#define RGBKEY_SELFTEST_SECONDS 20

/*
 * Set to 1 to light the chain in four coloured SEGMENTS instead of three single
 * LEDs. Three points confirm three points; they do not validate a 23-entry map,
 * and the calibration walk that produced it is exactly the kind of thing that
 * can be right at the ends and wrong in the middle. This paints
 *
 *     LED  0..5   red     LED 12..17  blue
 *     LED  6..11  green   LED 18..22  white
 *
 * so one glance says which physical keys each run of the chain actually covers.
 *
 * Left at 0 because the map IS trusted: run on hardware 2026-08-23, it gave
 * red across the whole top row, green the middle, blue the bottom and white the
 * thumbs, on both halves -- exactly what rgbkey_map.h claims. Set it back to 1
 * if the map is ever in doubt again; three single LEDs confirm three points,
 * not twenty-three.
 */
#define RGBKEY_SELFTEST_SEGMENTS 0

static atomic_t selftest_running = ATOMIC_INIT(RGBKEY_SELFTEST);

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

#if RGBKEY_SELFTEST_SEGMENTS
    for (int i = 0; i < STRIP_COUNT; i++) {
        if (i < 6) {
            pixels[i].r = RGBKEY_MID;
        } else if (i < 12) {
            pixels[i].g = RGBKEY_MID;
        } else if (i < 18) {
            pixels[i].b = RGBKEY_MID;
        } else {
            pixels[i].r = pixels[i].g = pixels[i].b = RGBKEY_MID;
        }
    }
#else
    /* First, middle and last LED, in three colours that cannot be confused. */
    pixels[0].r = RGBKEY_MID;
    if (STRIP_COUNT > 11) {
        pixels[11].g = RGBKEY_MID;
    }
    if (STRIP_COUNT > 0) {
        pixels[STRIP_COUNT - 1].b = RGBKEY_MID;
    }
#endif
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

    /*
     * The layer's key groups, then held modifiers over them -- broadest first,
     * so the more specific statement wins the pixel.
     *
     * A group may name keys on either half; a position belonging to the other
     * half has no LED in this half's map and is dropped by paint(), so neither
     * side needs to know which one it is.
     */
    if (row != NULL) {
        for (size_t g = 0; g < ARRAY_SIZE(row->groups); g++) {
            const struct rgbkey_group *group = &row->groups[g];
            if (group->positions == NULL || group->colour == RK_OFF) {
                continue; /* an unused slot in the row */
            }
            /*
             * A sided group belongs to one half or the other. Both halves see
             * the same table, so each simply drops the groups that are not
             * about it -- and when nobody knows which hand is holding the
             * layer, both keep the group rather than one of them guessing.
             */
            if (group->side != RK_SIDE_ANY && rgbkey_side_known(packed)) {
                const bool we_are_right = !RGBKEY_WE_ARE_LEFT;
                const bool we_hold = (rgbkey_side_is_right(packed) == we_are_right);
                if ((group->side == RK_SIDE_HOLDING) != we_hold) {
                    continue;
                }
            }
            for (const uint8_t *pos = group->positions; *pos != RGBKEY_POS_NONE; pos++) {
                paint(*pos, group->colour);
            }
        }

        /*
         * THE KEY HOLDING THE LAYER OPEN, in that layer's own colour -- the nav
         * thumb goes green while it is holding navigation.
         *
         * On a per-key board this is the one thing the eyelash's columns cannot
         * say: not just "navigation is on" but "this is the key doing it, let go
         * of it to stop". It costs nothing to find, because the owning position
         * is already recorded to work out which hand is holding the layer; only
         * the side used to travel over the split, and now the position does too.
         *
         * The far half simply finds no LED for a position on this one, so a
         * thumb lights on its own half without either half being told which it
         * is -- the same way every other group here works.
         *
         * Painted AFTER the groups so it wins its own pixel, and before caps
         * lock, which outranks everything.
         *
         * The colour is the row's first stated colour rather than a field of its
         * own. On every layer here the first group IS the layer's identity -- the
         * green wash on navigation, the blue column on symbol -- so a separate
         * field would be the same value written twice, and the two would drift.
         */
        if (rgbkey_side_known(packed)) {
            for (size_t g = 0; g < ARRAY_SIZE(row->groups); g++) {
                if (row->groups[g].positions == NULL || row->groups[g].colour == RK_OFF) {
                    continue;
                }
                paint(rgbkey_owner_pos(packed), row->groups[g].colour);
                break;
            }
        }
    }

    /*
     * Held modifiers last. A layer with no row of its own -- the base layer,
     * where this matters most -- still shows them.
     */
    /*
     * WHEN A HELD MODIFIER IS SHOWN AT ALL.
     *
     * Two boards want different rules, so the layout header picks one.
     *
     * The Rolio decides per layer, with the `hrm` flag on each row. The Corne
     * v4 follows the eyelash instead, whose rule is that modifiers are shown
     * only when the layer itself is saying nothing (rgbzone_run.c: `if
     * (!layer_speaks) overlay_mods(...)`). That matters most on navigation,
     * where the arrow keys ARE home-row mods: with the per-layer rule, touching
     * an arrow repaints its column in the modifier's colour and the cluster
     * appears to flicker under the hand.
     *
     * "Says nothing" is a property of the ROW, not of the pixels this half
     * painted -- a row lighting only the far half must still count as speaking,
     * or the two halves would disagree about whether to show modifiers.
     */
#if RGBKEY_HRM_ONLY_WHEN_LAYER_SILENT
    if (row == NULL) {
#else
    if (row == NULL || row->hrm) {
#endif
        for (size_t i = 0; i < ARRAY_SIZE(rgbkey_hrms); i++) {
            if ((mods & rgbkey_hrms[i].mod) == 0 || rgbkey_hrms[i].positions == NULL) {
                continue;
            }
            for (const uint8_t *pos = rgbkey_hrms[i].positions; *pos != RGBKEY_POS_NONE;
                 pos++) {
                paint(*pos, rgbkey_hrms[i].colour);
            }
        }
    }

    /*
     * Caps lock over the top of all of it. Nothing a layer or a modifier has to
     * say outranks knowing every letter is about to come out capital.
     */
    if (rgbkey_caps_of(packed)) {
        const uint8_t *const caps = RGBKEY_CAPS_KEYS;
        for (const uint8_t *pos = caps; *pos != RGBKEY_POS_NONE; pos++) {
            paint(*pos, RGBKEY_CAPS_COLOUR);
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
#if !RGBKEY_SELFTEST
    return 0;
#else
    /*
     * Everything that touches the strip runs on the one queue, so the
     * self-test and the ordinary repaints can never overlap on `pixels` --
     * and the settle sleep in flush() stays off the system queue, where it
     * would hold up unrelated work.
     */
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &selftest_begin_work,
                                K_SECONDS(RGBKEY_SELFTEST_DELAY_SECONDS));
    return 0;
#endif
}

SYS_INIT(rgbkey_strip_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
