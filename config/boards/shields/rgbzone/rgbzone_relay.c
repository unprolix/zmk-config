/*
 * Carrying the far half's zone colours across the split.
 *
 * A peripheral has no keymap and so cannot work out what its own columns do.
 * The twelve bits it needs fit easily in a relayed behaviour's parameters, and
 * GLOBAL locality is what makes ZMK deliver it -- the same mechanism the
 * display's leader relay uses.
 *
 * The name is eight characters on purpose: the BLE split payload carries it in
 * a nine-byte field and silently truncates anything longer, which costs an
 * afternoon to find because the central still reports success.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgb_zone

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/split/bluetooth/service.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

BUILD_ASSERT(sizeof("rgbzone") <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN,
             "Relay behaviour name is too long to survive the split payload intact");

static int on_rgbzone_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    rgbzone_apply((uint32_t)binding->param1);
#else
    /* The central drives its own strip directly; its copy of this is noise. */
    ARG_UNUSED(binding);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_rgbzone_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int rgbzone_relay_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api rgbzone_relay_api = {
    .binding_pressed = on_rgbzone_binding_pressed,
    .binding_released = on_rgbzone_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

#define RGBZONE_INST(n)                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, rgbzone_relay_init, NULL, NULL, NULL, POST_KERNEL,                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rgbzone_relay_api);

DT_INST_FOREACH_STATUS_OKAY(RGBZONE_INST)

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
void rgbzone_relay_send(uint32_t packed) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(rgbzone)),
        .param1 = packed,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
}
#endif
