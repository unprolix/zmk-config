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
 * Not inferred from key presses -- two rounds of trying that failed, and the
 * note in rgbzone_owner.c records why. The momentary-layer behaviour is told
 * the position that invoked it, so it records the owner outright and this only
 * has to ask.
 *
 * Sticky: a layer reached some other way (&tog, &sl, auto-layer) has no
 * holding hand at all, and keeping the last known side leaves those layers
 * steady. They are also the layers whose palette rows are symmetrical, so the
 * value is not used for anything visible.
 */
static bool layer_from_right;

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
     * have to be assigned to a side here. Which side that is was recorded by
     * the key that opened the layer, in rgbzone_owner.c.
     */
    const struct zone_layer *layer_row = row_for(name);
    const enum zone_colour *pressed = layer_row ? layer_row->pressed : dark;
    const enum zone_colour *other = layer_row ? layer_row->other : dark;

    /* Ask the key that opened the layer; keep the last answer if none did. */
    bool owner_side;
    if (rgbzone_owner_side((uint8_t)id, &owner_side)) {
        layer_from_right = owner_side;
    }

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
 * A second chance to repaint. A layer can open or close without a layer-change
 * event reaching this file in a usable state -- a hold-tap resolving, a macro
 * still working through its queue -- so refreshing on the next position event
 * settles it. The dedupe downstream means a redundant refresh costs nothing.
 */
static int rgbzone_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    ARG_UNUSED(ev);

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
