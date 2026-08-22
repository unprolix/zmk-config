/*
 * The calibration state machine, central only.
 *
 * Walks the LED chain one pixel at a time and waits for a key to be pressed.
 * Whatever position arrives is taken as the key under that LED, which is the
 * whole mapping: LED index -> key position. It runs the local half first, then
 * drives the peripheral's strip over the split and pairs each LED with the
 * positions that come back from that half.
 *
 * The point of doing it this way is that nobody has to read anything out: the
 * only input is pressing the lit key, and at the end the table is typed into
 * whatever has focus.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/rgb_underglow.h>

#include "rgbcal.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_COUNT DT_PROP(DT_CHOSEN(zmk_underglow), chain_length)

/* Somewhere to put the answers until they are typed out. */
static uint8_t local_map[STRIP_COUNT];
static uint8_t remote_map[STRIP_COUNT];
#define UNSET 0xFF

enum rgbcal_phase {
    PHASE_IDLE,
    PHASE_LOCAL,
    PHASE_REMOTE,
    PHASE_DONE,
};

static enum rgbcal_phase phase = PHASE_IDLE;
static uint16_t cursor;

void rgbcal_type(const char *text);

/*
 * Stop consuming presses after a long idle, so an abandoned run does not sit
 * forever eating the next thing typed. Generous on purpose: working out which
 * key sits under a given LED takes as long as it takes, and an impatient
 * timeout just kills the run silently -- everything goes dark and further
 * presses do nothing, which is exactly what it looks like when the hardware
 * has failed.
 */
#define RGBCAL_IDLE_TIMEOUT_SECONDS 300

/*
 * The report types several hundred characters with a pause between each, so it
 * cannot run on the event thread -- that would block key handling for seconds.
 */
static void rgbcal_report_work_cb(struct k_work *work);
static K_WORK_DEFINE(rgbcal_report_work, rgbcal_report_work_cb);

/*
 * Not every LED in the chain is necessarily under a key -- nothing documents
 * this board's layout, and some of the 21 may be underglow facing the desk. An
 * LED that cannot be seen is indistinguishable from a run that has stopped
 * advancing, so each step announces itself by typing its index. If the numbers
 * keep climbing while nothing lights, those indices simply are not visible.
 */
static void rgbcal_progress_work_cb(struct k_work *work);
static K_WORK_DEFINE(rgbcal_progress_work, rgbcal_progress_work_cb);

static void rgbcal_progress_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    char note[16];
    snprintf(note, sizeof(note), "%s%d ", phase == PHASE_REMOTE ? "r" : "l", cursor);
    rgbcal_type(note);
}

/* Drive the far half's strip; ignored locally, see rgbcal_behavior.c. */
static void light_remote(uint16_t index) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(rgbcal)),
        .param1 = index,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
}

static void rgbcal_abort(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(rgbcal_abort_work, rgbcal_abort);

static void rgbcal_abort(struct k_work *work) {
    ARG_UNUSED(work);
    if (phase == PHASE_LOCAL || phase == PHASE_REMOTE) {
        rgbcal_light(RGBCAL_ALL_OFF);
        light_remote(RGBCAL_ALL_OFF);
        phase = PHASE_DONE;
    }
}

static void report(void) {
    char line[32];

    rgbcal_type("\nleft ");
    for (uint16_t i = 0; i < STRIP_COUNT; i++) {
        snprintf(line, sizeof(line), "%d:%d ", i, local_map[i]);
        rgbcal_type(line);
    }

    rgbcal_type("\nright ");
    for (uint16_t i = 0; i < STRIP_COUNT; i++) {
        snprintf(line, sizeof(line), "%d:%d ", i, remote_map[i]);
        rgbcal_type(line);
    }
    rgbcal_type("\n");
}

static void advance(void) {
    cursor++;

    if (phase == PHASE_LOCAL) {
        if (cursor < STRIP_COUNT) {
            rgbcal_light(cursor);
            k_work_submit(&rgbcal_progress_work);
            return;
        }
        /* Local chain done; hand over to the far half. */
        rgbcal_light(RGBCAL_ALL_OFF);
        phase = PHASE_REMOTE;
        cursor = 0;
        light_remote(0);
        k_work_submit(&rgbcal_progress_work);
        return;
    }

    if (phase == PHASE_REMOTE) {
        if (cursor < STRIP_COUNT) {
            light_remote(cursor);
            k_work_submit(&rgbcal_progress_work);
            return;
        }
        light_remote(RGBCAL_ALL_OFF);
        phase = PHASE_DONE;
        k_work_submit(&rgbcal_report_work);
    }
}

static void rgbcal_report_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    report();
}

static int rgbcal_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool from_local = ev->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;

    /*
     * Only take presses from the half currently being walked. Otherwise a
     * stray press on the other half would consume a step and shift every
     * remaining entry by one -- a wrong table that still looks plausible.
     */
    if (phase != PHASE_LOCAL && phase != PHASE_REMOTE) {
        /* Finished or aborted: the keyboard is a keyboard again. */
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (phase == PHASE_LOCAL && from_local) {
        local_map[cursor] = (uint8_t)ev->position;
        advance();
    } else if (phase == PHASE_REMOTE && !from_local) {
        remote_map[cursor] = (uint8_t)ev->position;
        advance();
    }

    k_work_reschedule(&rgbcal_abort_work, K_SECONDS(RGBCAL_IDLE_TIMEOUT_SECONDS));

    /*
     * Presses are deliberately NOT swallowed. ZMK's keymap subscribes to this
     * event before this listener does, so returning ZMK_EV_EVENT_HANDLED does
     * not stop the key being processed -- it only looked like it would. Since
     * the keys are going to type either way, let them: it keeps the bootloader
     * key working throughout, which matters more than a tidy capture file.
     */
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgbcal_run, rgbcal_position_listener);
ZMK_SUBSCRIPTION(rgbcal_run, zmk_position_state_changed);

/*
 * Started from a delayed work rather than an init hook: the split link is not
 * up at boot, and the peripheral phase needs it. The local phase does not, but
 * starting both from the same place keeps the sequence obvious.
 */
static void rgbcal_showall(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(rgbcal_showall_work, rgbcal_showall);

static void rgbcal_begin(struct k_work *work) {
    ARG_UNUSED(work);
    memset(local_map, UNSET, sizeof(local_map));
    memset(remote_map, UNSET, sizeof(remote_map));
    phase = PHASE_LOCAL;
    cursor = 0;
    rgbcal_light(0);
    k_work_submit(&rgbcal_progress_work);
    k_work_reschedule(&rgbcal_abort_work, K_SECONDS(RGBCAL_IDLE_TIMEOUT_SECONDS));
}

static K_WORK_DELAYABLE_DEFINE(rgbcal_begin_work, rgbcal_begin);

/*
 * Take the strip off ZMK's underglow first. That stops its tick and queues one
 * last blanking write, so leave a moment before driving pixels or the two
 * writers race and the first LED appears not to light.
 */
/*
 * Before walking the chain, light all of it for a while. How many LEDs this
 * board really has, and which of them can be seen from above, is not written
 * down anywhere -- and a run that steps through invisible LEDs looks identical
 * to one that has died. Fifteen seconds of everything-on answers it directly.
 */
#define RGBCAL_SHOW_ALL_SECONDS 60

static void rgbcal_start(struct k_work *work) {
    ARG_UNUSED(work);
    zmk_rgb_underglow_off();
    k_work_reschedule(&rgbcal_showall_work, K_MSEC(500));
}

static void rgbcal_showall(struct k_work *work) {
    ARG_UNUSED(work);
    rgbcal_light_stripes();
    light_remote(RGBCAL_LIGHT_STRIPES);
    /* Long enough to pull a keycap and look properly. */
    k_work_reschedule(&rgbcal_begin_work, K_SECONDS(RGBCAL_SHOW_ALL_SECONDS));
}

static K_WORK_DELAYABLE_DEFINE(rgbcal_start_work, rgbcal_start);

static int rgbcal_run_init(void) {
    k_work_reschedule(&rgbcal_start_work, K_SECONDS(5));
    return 0;
}

SYS_INIT(rgbcal_run_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
