/*
 * Working out what each column is for on the active layer, central only.
 *
 * The keymap is queried live rather than tabulated at build time, so this stays
 * correct as the keymap is edited. That also means it can only run on the
 * central -- a peripheral has no keymap -- so the far half's six zones are
 * computed here and relayed as twelve bits.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void rgbzone_relay_send(uint32_t packed);
void rgbzone_diag_report(uint8_t layer_index, const char *layer_name, uint8_t mods,
                         uint32_t packed_left, uint32_t packed_right);

/*
 * Which half is holding the layer open.
 *
 * Latched when the layer changes, NOT on every press: hold a layer key with
 * one hand and type with the other, and following the most recent press would
 * make the layer's colours jump to the typing hand.
 *
 * The split source distinguishes the halves for free -- a peripheral press
 * carries its peripheral index, a local one carries
 * ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL -- so no position table is needed.
 */
static bool layer_from_right;

/*
 * Which keys are down, and on which half.
 *
 * The obvious approach -- remember the side of the last press and latch it
 * when the layer changes -- does not work, because the layer-opening keys are
 * hold-taps. Their layer does not open on the press; it opens when the hold
 * resolves, which is after the position event has been and gone. At that point
 * the last press looks like it changed nothing, and the side left over is
 * whichever hand typed most recently. Holding symbol on the right then lit the
 * left.
 *
 * Tracking what is still held answers it directly: whatever opened the layer
 * is, by definition, still down.
 */
#define MAX_HELD 10

static struct held_key {
    uint32_t position;
    bool from_right;
    uint32_t seq; /* 0 = slot free */
} held_keys[MAX_HELD];

static uint32_t held_seq;

static void held_press(uint32_t position, bool from_right) {
    for (int i = 0; i < MAX_HELD; i++) {
        if (held_keys[i].seq == 0) {
            held_keys[i] = (struct held_key){position, from_right, ++held_seq};
            return;
        }
    }
    /* Full: drop the oldest rather than ignore the newest. */
    int oldest = 0;
    for (int i = 1; i < MAX_HELD; i++) {
        if (held_keys[i].seq < held_keys[oldest].seq) {
            oldest = i;
        }
    }
    held_keys[oldest] = (struct held_key){position, from_right, ++held_seq};
}

static void held_release(uint32_t position) {
    for (int i = 0; i < MAX_HELD; i++) {
        if (held_keys[i].seq != 0 && held_keys[i].position == position) {
            held_keys[i].seq = 0;
            return;
        }
    }
}

/* The most recently pressed key that is still down, if any. */
static bool held_newest_side(bool *from_right) {
    int best = -1;
    for (int i = 0; i < MAX_HELD; i++) {
        if (held_keys[i].seq != 0 && (best < 0 || held_keys[i].seq > held_keys[best].seq)) {
            best = i;
        }
    }
    if (best < 0) {
        return false;
    }
    *from_right = held_keys[best].from_right;
    return true;
}

/*
 * Colours are looked up by the layer's display-name rather than its index, so
 * inserting a layer in the keymap does not silently repaint every other one.
 * A layer with no row in the table stays dark.
 */
static const struct zone_layer *row_for(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < ZONE_LAYER_COUNT; i++) {
        if (strcmp(zone_layers[i].name, name) == 0) {
            return &zone_layers[i];
        }
    }
    return NULL;
}

/*
 * Held modifiers paint over whatever the layer says, on every layer.
 *
 * They used to be confined to the base layer, which had two bad consequences:
 * a mod held on any other layer was invisible, and a modifier that arrives by
 * way of a layer switch could never show at all. The cost is that a column
 * carrying a modifier cannot be given a different colour by a layer while that
 * modifier is down -- which is the right trade, since the modifier is the more
 * urgent thing to see.
 */
static void overlay_mods(const struct zone_hrm *table, size_t len, zmk_mod_flags_t mods,
                         enum zone_colour *out) {
    for (size_t i = 0; i < len; i++) {
        if ((mods & table[i].mod) && table[i].zone < ZONE_COUNT) {
            out[table[i].zone] = table[i].colour;
        }
    }
}

static void refresh(void) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
    const char *name = zmk_keymap_layer_name(id);

    static const enum zone_colour dark[ZONE_COUNT] = {ZC_OFF};
    enum zone_colour left[ZONE_COUNT];
    enum zone_colour right[ZONE_COUNT];

    /*
     * Layer colours are written relative to the half holding the key, so they
     * have to be assigned to a side here. Which side that is comes from the
     * split source of the last press: a peripheral press carries its index,
     * a local one carries ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL.
     */
    const struct zone_layer *layer_row = row_for(name);
    const enum zone_colour *pressed = layer_row ? layer_row->pressed : dark;
    const enum zone_colour *other = layer_row ? layer_row->other : dark;

    /* Whatever opened the layer is still down; that is the holding half. */
    held_newest_side(&layer_from_right);

    if (layer_from_right) {
        memcpy(right, pressed, sizeof(right));
        memcpy(left, other, sizeof(left));
    } else {
        memcpy(left, pressed, sizeof(left));
        memcpy(right, other, sizeof(right));
    }

    /*
     * A layer that states colours owns the display; modifiers paint only on
     * layers that say nothing. Otherwise a layer that also carries a modifier
     * -- the pinky nav layers hold GUI -- lights both its own column and the
     * modifier's, which is not what the layer was asked to show. The base
     * layer's row is entirely OFF, so it still gets modifiers, which is the
     * whole point of it.
     */
    zmk_mod_flags_t mods = zmk_hid_get_explicit_mods();
    bool layer_speaks = false;
    for (uint8_t i = 0; i < ZONE_COUNT && !layer_speaks; i++) {
        layer_speaks = pressed[i] != ZC_OFF || other[i] != ZC_OFF;
    }

    if (!layer_speaks) {
        overlay_mods(zone_hrm_left, ARRAY_SIZE(zone_hrm_left), mods, left);
        overlay_mods(zone_hrm_right, ARRAY_SIZE(zone_hrm_right), mods, right);
    }

    uint32_t pl = rgbzone_pack(left);
    uint32_t pr = rgbzone_pack(right);
    rgbzone_apply(pl);
    rgbzone_relay_send(pr);
    rgbzone_diag_report((uint8_t)index, name, mods, pl, pr);
}

static int rgbzone_layer_listener(const zmk_event_t *eh) {
    refresh();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgbzone_run, rgbzone_layer_listener);
ZMK_SUBSCRIPTION(rgbzone_run, zmk_layer_state_changed);

/*
 * Position events serve two purposes: they carry the split source that says
 * which half is holding, and they give a second chance to repaint. ZMK's
 * keymap handles a position event before any shield listener and raises the
 * layer change from inside that handling, so the layer listener can run before
 * this one has seen the press that caused it. Refreshing here too settles it
 * on the very next event, and the dedupe means a redundant refresh costs
 * nothing.
 */
static int rgbzone_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state) {
        held_press(ev->position, ev->source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL);
    } else {
        held_release(ev->position);
    }

    refresh();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgbzone_pos, rgbzone_position_listener);
ZMK_SUBSCRIPTION(rgbzone_pos, zmk_position_state_changed);
/*
 * Modifier changes arrive as keycode events -- ZMK declares a
 * zmk_modifiers_state_changed type but never raises it -- so the live flags
 * are read back out of the HID state on every keycode instead.
 */
ZMK_SUBSCRIPTION(rgbzone_run, zmk_keycode_state_changed);

/*
 * Same problem the display relay has: nothing tells the central that a
 * peripheral has connected, and there is no layer *change* at boot to react
 * to. Take the strip off underglow, then refresh on a timer so the far half
 * catches up after a connect or a reconnect.
 */
#define RGBZONE_REFRESH_SECONDS 10

static void rgbzone_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(rgbzone_tick_work, rgbzone_tick);

static void rgbzone_tick(struct k_work *work) {
    ARG_UNUSED(work);

    /*
     * Underglow can switch itself back on without being asked: with
     * AUTO_OFF_IDLE it restores whatever it was doing before the keyboard went
     * idle, so a single off() at boot is not enough. Two writers on one strip
     * shows up as wrong colours at random brightness, not as an error.
     */
    bool on = false;
    if (zmk_rgb_underglow_get_state(&on) == 0 && on) {
        zmk_rgb_underglow_off();
    }

    refresh();
    k_work_reschedule(&rgbzone_tick_work, K_SECONDS(RGBZONE_REFRESH_SECONDS));
}

static void rgbzone_start(struct k_work *work) {
    ARG_UNUSED(work);
    zmk_rgb_underglow_off();
    k_work_reschedule(&rgbzone_tick_work, K_MSEC(500));
}

static K_WORK_DELAYABLE_DEFINE(rgbzone_start_work, rgbzone_start);

static int rgbzone_init(void) {
    k_work_reschedule(&rgbzone_start_work, K_SECONDS(3));
    return 0;
}

SYS_INIT(rgbzone_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
