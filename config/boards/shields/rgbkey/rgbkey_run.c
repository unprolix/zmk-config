/*
 * Deciding what the strip should show. Central only.
 *
 * Working out the active layer needs the keymap, which only the central has,
 * and the live modifiers come out of HID state, which only the central keeps.
 * Both are resolved here and sent to the far half, which paints its own keys.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#endif

#include "rgbkey.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void rgbkey_relay_send(uint32_t packed);

/*
 * Layers are matched by display-name rather than by index so that inserting a
 * layer in the keymap does not silently repaint every other one.
 */
static enum rgbkey_scene scene_for(const char *name) {
    if (name == NULL) {
        return RKS_NONE;
    }
    for (size_t i = 0; i < RGBKEY_LAYER_COUNT; i++) {
        if (strcmp(rgbkey_layers[i].name, name) == 0) {
            return rgbkey_layers[i].scene;
        }
    }
    return RKS_NONE;
}

/*
 * Caps lock as the host has it. Bit 1 of the HID keyboard LED report, per the
 * USB HID usage tables; ZMK carries the report through but names none of the
 * bits.
 *
 * Cached rather than read fresh on every repaint: the indicators are stored per
 * endpoint, so a repaint landing while the endpoint switches -- or while none is
 * selected, which happens on every disconnect -- reads an empty slot and decides
 * caps is off. The host has not changed its mind; we asked at a bad moment.
 */
#define HID_LED_CAPS_LOCK BIT(1)

static uint8_t cached_indicators;

static bool host_caps_lock(void) { return (cached_indicators & HID_LED_CAPS_LOCK) != 0; }

static void refresh(void) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
    const char *name = zmk_keymap_layer_name(id);

    uint32_t packed =
        rgbkey_pack(scene_for(name), zmk_hid_get_explicit_mods(), host_caps_lock());

    rgbkey_apply(packed);
    rgbkey_relay_send(packed);
}

static int rgbkey_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    const struct zmk_hid_indicators_changed *ind = as_zmk_hid_indicators_changed(eh);
    if (ind != NULL) {
        cached_indicators = (uint8_t)ind->indicators;
    }
#else
    ARG_UNUSED(eh);
#endif
    refresh();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgbkey_run, rgbkey_listener);
ZMK_SUBSCRIPTION(rgbkey_run, zmk_layer_state_changed);
/*
 * Modifier changes arrive as keycode events -- ZMK declares a
 * zmk_modifiers_state_changed type but never raises it -- so the live flags
 * are read back out of HID state on every keycode instead.
 */
ZMK_SUBSCRIPTION(rgbkey_run, zmk_keycode_state_changed);
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
/* Caps lock arrives from the host, not from a keypress here. */
ZMK_SUBSCRIPTION(rgbkey_run, zmk_hid_indicators_changed);
#endif

/*
 * Nothing tells a central that a peripheral has connected:
 * zmk_split_peripheral_status_changed is raised on the peripheral only. A half
 * that boots or reconnects into an unchanged state would otherwise show
 * nothing until the next layer change, so re-send on a timer.
 */
#define RGBKEY_REFRESH_SECONDS 10

static void rgbkey_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(rgbkey_tick_work, rgbkey_tick);

static void rgbkey_tick(struct k_work *work) {
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

    /*
     * refresh() re-sends to the peripheral every time. Its local repaint is
     * suppressed when nothing changed, which is what makes this cheap to run
     * on a timer -- the far half is the only one that needs telling again.
     */
    refresh();

    k_work_reschedule(&rgbkey_tick_work, K_SECONDS(RGBKEY_REFRESH_SECONDS));
}

static void rgbkey_start(struct k_work *work) {
    ARG_UNUSED(work);
    zmk_rgb_underglow_off();
    k_work_reschedule(&rgbkey_tick_work, K_MSEC(500));
}

static K_WORK_DELAYABLE_DEFINE(rgbkey_start_work, rgbkey_start);

static int rgbkey_init(void) {
    /* Let the split link and the strip driver settle before taking the chain. */
    k_work_reschedule(&rgbkey_start_work, K_SECONDS(3));
    return 0;
}

SYS_INIT(rgbkey_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
