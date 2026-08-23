/*
 * Which half is holding a layer open.
 *
 * Layer colours are stated relative to the holding hand (zone_palette.h), so
 * something has to say which hand that is. Inferring it from key presses does
 * not work, and two rounds of trying are worth recording:
 *
 *   - The side of the most recent press follows the typing hand, so holding a
 *     layer with one hand and typing with the other made the colours jump.
 *   - Latching that side when the layer changes fails too, because the
 *     layer-opening keys are hold-taps: the layer opens when the hold
 *     resolves, long after the position event, and with flavor = "balanced" a
 *     hold usually resolves *because* the other hand pressed something. The
 *     newest press at that instant is the wrong key by construction.
 *
 * So do not infer it. The momentary-layer behaviour knows the position that
 * invoked it -- and a hold-tap, a tap-dance, or a macro all pass the original
 * position straight through -- so the answer is exact if it is recorded there.
 * rgbzone.overlay points ZMK's &mo node at this driver to collect it; this
 * file is otherwise a copy of ZMK's momentary layer.
 *
 * Layers reached some other way (&tog, &sl, auto-layer) record no owner. They
 * are the layers whose rows are symmetrical anyway, and the caller keeps the
 * last known side rather than guessing a new one.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgbzone_momentary

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * One entry per layer held at once. Layers nest -- a pinky nav layer under a
 * symbol layer under the base -- but not deeply, and an overflow only costs
 * the newest layer its side.
 */
#define RGBZONE_OWNER_MAX 6

static struct rgbzone_owner {
    uint8_t layer;
    uint32_t position;
    bool from_right;
    uint32_t seq; /* 0 = slot free */
} rgbzone_owners[RGBZONE_OWNER_MAX];

static uint32_t rgbzone_owner_seq;

void rgbzone_owner_pressed(uint8_t layer, uint32_t position, bool from_right) {
    int slot = -1;
    for (int i = 0; i < RGBZONE_OWNER_MAX; i++) {
        if (rgbzone_owners[i].seq == 0) {
            slot = i;
            break;
        }
        /* Oldest goes first if every slot is taken. */
        if (slot < 0 || rgbzone_owners[i].seq < rgbzone_owners[slot].seq) {
            slot = i;
        }
    }

    rgbzone_owners[slot] =
        (struct rgbzone_owner){layer, position, from_right, ++rgbzone_owner_seq};
}

void rgbzone_owner_released(uint8_t layer, uint32_t position) {
    for (int i = 0; i < RGBZONE_OWNER_MAX; i++) {
        if (rgbzone_owners[i].seq != 0 && rgbzone_owners[i].layer == layer &&
            rgbzone_owners[i].position == position) {
            rgbzone_owners[i].seq = 0;
            return;
        }
    }
}

bool rgbzone_owner_side(uint8_t layer, bool *from_right) {
    int best = -1;
    for (int i = 0; i < RGBZONE_OWNER_MAX; i++) {
        if (rgbzone_owners[i].seq == 0 || rgbzone_owners[i].layer != layer) {
            continue;
        }
        /* Two keys can open one layer; the one that opened it last is holding
           it now, since releasing either closes it. */
        if (best < 0 || rgbzone_owners[i].seq > rgbzone_owners[best].seq) {
            best = i;
        }
    }
    if (best < 0) {
        return false;
    }
    *from_right = rgbzone_owners[best].from_right;
    return true;
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_rgbzone_mo_config {
    bool locking;
};

static bool binding_from_right(struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    /* A peripheral press carries its peripheral index, a local one carries
       ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL, so the halves separate without a
       position table. The central is the left half. */
    return event.source != ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
#else
    ARG_UNUSED(event);
    return false;
#endif
}

static int rgbzone_mo_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct behavior_rgbzone_mo_config *cfg =
        zmk_behavior_get_binding(binding->behavior_dev)->config;

    /* Record before activating: activation raises the layer-change event
       synchronously, and the listener that repaints asks for the owner. */
    rgbzone_owner_pressed((uint8_t)binding->param1, event.position, binding_from_right(event));

    return zmk_keymap_layer_activate(binding->param1, cfg->locking);
}

static int rgbzone_mo_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    const struct behavior_rgbzone_mo_config *cfg =
        zmk_behavior_get_binding(binding->behavior_dev)->config;

    int ret = zmk_keymap_layer_deactivate(binding->param1, cfg->locking);
    rgbzone_owner_released((uint8_t)binding->param1, event.position);
    return ret;
}

static const struct behavior_driver_api rgbzone_mo_driver_api = {
    .binding_pressed = rgbzone_mo_binding_pressed,
    .binding_released = rgbzone_mo_binding_released,
};

#define RGBZONE_MO_INST(n)                                                                         \
    static const struct behavior_rgbzone_mo_config rgbzone_mo_config_##n = {                       \
        .locking = DT_INST_PROP_OR(n, locking, false),                                             \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &rgbzone_mo_config_##n, POST_KERNEL,              \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rgbzone_mo_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RGBZONE_MO_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
