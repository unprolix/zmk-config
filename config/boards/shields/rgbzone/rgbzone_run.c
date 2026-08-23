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

#include <zmk/ble.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/ble_active_profile_changed.h>
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#endif
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
 * The bluetooth layer is drawn from live state, not from the palette.
 *
 * Its whole purpose is answering "which profile is which, and what is it
 * doing", and a fixed row cannot say that. BT_SEL 0..4 sit along the left top
 * row running outward, so profile i takes the column its own key is in: zone 0
 * is the innermost column, position 0 is the outermost key, hence the reversal.
 *
 * The left half carries the state and the right half marks the selection --
 * two facts about one profile, and only one colour per column to say them in.
 * Splitting them across the halves means neither has to be encoded in a shade
 * of the other.
 */
#define BLUETOOTH_LAYER_NAME "bluetooth"

static bool bluetooth_zones(const char *name, enum zone_colour *left, enum zone_colour *right) {
    if (name == NULL || strcmp(name, BLUETOOTH_LAYER_NAME) != 0) {
        return false;
    }

    memset(left, ZC_OFF, ZONE_COUNT * sizeof(*left));
    memset(right, ZC_OFF, ZONE_COUNT * sizeof(*right));

    uint8_t count = ZMK_BLE_PROFILE_COUNT < ZONE_COUNT ? ZMK_BLE_PROFILE_COUNT : ZONE_COUNT;
    int active = zmk_ble_active_profile_index();

    for (uint8_t i = 0; i < count; i++) {
        uint8_t zone = ZONE_COUNT - 1 - i;

        if (zmk_ble_profile_is_connected(i)) {
            left[zone] = ZC_GREEN;
        } else if (!zmk_ble_profile_is_open(i)) {
            /* Paired, but the host is asleep or out of range. */
            left[zone] = ZC_BLUE;
        }

        if (i == (uint8_t)active) {
            right[zone] = ZC_WHITE;
        }
    }

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

/*
 * Caps lock as the host has it.
 *
 * Bit 1 of the HID keyboard LED report, per the USB HID usage tables; ZMK
 * carries the report through but names none of the bits.
 */
#define HID_LED_CAPS_LOCK BIT(1)

static bool host_caps_lock(void) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    return (zmk_hid_indicators_get_current_profile() & HID_LED_CAPS_LOCK) != 0;
#else
    return false;
#endif
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
    if (!bluetooth_zones(name, left, right)) {
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
        layer_speaks = left[i] != ZC_OFF || right[i] != ZC_OFF;
    }

    if (!layer_speaks) {
        overlay_mods(zone_hrm_left, ARRAY_SIZE(zone_hrm_left), mods, left);
        overlay_mods(zone_hrm_right, ARRAY_SIZE(zone_hrm_right), mods, right);
    }

    /* Caps lock goes on top of all of it -- see zone_palette.h for why. */
    if (host_caps_lock()) {
        for (size_t i = 0; i < ARRAY_SIZE(zone_caps_zones); i++) {
            uint8_t zone = zone_caps_zones[i];
            if (zone < ZONE_COUNT) {
                left[zone] = ZONE_CAPS_COLOUR;
                right[zone] = ZONE_CAPS_COLOUR;
            }
        }
    }

    /* Brightness rides along with the colours so the far half stays in step. */
    uint8_t level = rgbzone_level_get();
    uint32_t pl = rgbzone_with_level(rgbzone_pack(left), level);
    uint32_t pr = rgbzone_with_level(rgbzone_pack(right), level);
    rgbzone_apply(pl);
    rgbzone_relay_send(pr);
    rgbzone_diag_report((uint8_t)index, name, mods, pl, pr);
}

/* For the brightness keys, which change the picture without any event. */
void rgbzone_refresh(void) { refresh(); }

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
/* The bluetooth layer draws live profile state, so it has to follow it. */
ZMK_SUBSCRIPTION(rgbzone_run, zmk_ble_active_profile_changed);
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
/* Caps lock arrives from the host, not from a keypress here. */
ZMK_SUBSCRIPTION(rgbzone_run, zmk_hid_indicators_changed);
#endif

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
