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
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void rgbzone_relay_send(uint32_t packed);

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

/* The layer that is dark until a modifier is held. */
#define BASE_LAYER_NAME "hierophant"

static void hrm_colours(const struct zone_hrm *table, size_t len, zmk_mod_flags_t mods,
                        enum zone_colour *out) {
    for (uint8_t i = 0; i < ZONE_COUNT; i++) {
        out[i] = ZC_OFF;
    }
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

    if (name != NULL && strcmp(name, BASE_LAYER_NAME) == 0) {
        /* Dark unless a modifier is live; then just that modifier's column. */
        zmk_mod_flags_t mods = zmk_hid_get_explicit_mods();
        hrm_colours(zone_hrm_left, ARRAY_SIZE(zone_hrm_left), mods, left);
        hrm_colours(zone_hrm_right, ARRAY_SIZE(zone_hrm_right), mods, right);
        rgbzone_apply(rgbzone_pack(left));
        rgbzone_relay_send(rgbzone_pack(right));
        return;
    }

    const struct zone_layer *row = row_for(name);
    rgbzone_apply(rgbzone_pack(row ? row->left : dark));
    rgbzone_relay_send(rgbzone_pack(row ? row->right : dark));
}

static int rgbzone_layer_listener(const zmk_event_t *eh) {
    refresh();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgbzone_run, rgbzone_layer_listener);
ZMK_SUBSCRIPTION(rgbzone_run, zmk_layer_state_changed);
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
