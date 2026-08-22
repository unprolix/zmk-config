/*
 * The behaviour the central uses to drive the peripheral's strip.
 *
 * GLOBAL locality means ZMK relays every invocation to each peripheral before
 * running it locally, so the central can light an LED on the far half without
 * any split-protocol code of its own. The central ignores its own copy: it
 * drives its local strip directly.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgb_calibrate

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "rgbcal.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int on_rgbcal_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    if ((uint16_t)binding->param1 == RGBCAL_LIGHT_ALL) {
        rgbcal_light_all();
    } else if ((uint16_t)binding->param1 == RGBCAL_LIGHT_STRIPES) {
        rgbcal_light_stripes();
    } else {
        rgbcal_light((uint16_t)binding->param1);
    }
#else
    ARG_UNUSED(binding);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_rgbcal_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int rgbcal_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api rgbcal_driver_api = {
    .binding_pressed = on_rgbcal_binding_pressed,
    .binding_released = on_rgbcal_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

#define RGBCAL_INST(n)                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, rgbcal_init, NULL, NULL, NULL, POST_KERNEL,                         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rgbcal_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RGBCAL_INST)
