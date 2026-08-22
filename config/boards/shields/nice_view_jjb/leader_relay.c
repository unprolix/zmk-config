/*
 * Behaviour that carries leader state to split peripherals.
 *
 * Declared with BEHAVIOR_LOCALITY_GLOBAL, which is the whole trick: ZMK's
 * zmk_behavior_invoke_binding() sends a GLOBAL behaviour to every peripheral
 * before running it locally (see zmk/app/src/behavior.c). So the central's
 * status screen invokes this whenever leader state changes and each peripheral
 * receives it, with no split-protocol code of our own.
 *
 * It is never bound to a key.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_leader_relay

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "leader_relay.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int on_leader_relay_binding_pressed(struct zmk_behavior_binding *binding,
                                           struct zmk_behavior_binding_event event) {
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    /*
     * Only the peripheral acts on this. The central invokes it locally too --
     * GLOBAL locality always does -- but there it is redundant: the central's
     * screen reads the real leader state directly.
     */
    jjb_leader_relay_received(binding->param1, binding->param2);
#else
    ARG_UNUSED(binding);
#endif
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_leader_relay_binding_released(struct zmk_behavior_binding *binding,
                                            struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int leader_relay_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api leader_relay_driver_api = {
    .binding_pressed = on_leader_relay_binding_pressed,
    .binding_released = on_leader_relay_binding_released,
    /* The reason this works at all -- see the file comment. */
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

#define LEADER_RELAY_INST(n)                                                                       \
    BEHAVIOR_DT_INST_DEFINE(n, leader_relay_init, NULL, NULL, NULL, POST_KERNEL,                   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &leader_relay_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LEADER_RELAY_INST)
