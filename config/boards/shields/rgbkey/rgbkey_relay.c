/*
 * Carrying the active scene and the live modifiers across the split.
 *
 * A peripheral has no keymap, so it cannot tell which layer is active, and no
 * HID state, so it cannot tell which modifiers are held. Both fit in two bytes
 * of a relayed behaviour's parameters; the pixels themselves would not, which
 * is why each half renders its own side from the shared tables in rgbkey.h.
 *
 * GLOBAL locality is what makes ZMK relay this at all -- the same mechanism
 * the display's leader relay uses.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgb_key

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/split/bluetooth/service.h>

#include "rgbkey.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * The BLE split payload carries this name in a nine-byte field, NOT the
 * char[16] that struct zmk_split_transport_central_command advertises. A
 * longer name is copied short, the peripheral then fails to find the
 * behaviour, and the central still reports success -- which costs an afternoon
 * to find. Eight usable characters.
 */
BUILD_ASSERT(sizeof("rgbkey") <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN,
             "Relay behaviour name is too long to survive the split payload intact");

static int on_rgbkey_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    rgbkey_apply((uint32_t)binding->param1);
#else
    /* The central drives its own strip directly; its copy of this is noise. */
    ARG_UNUSED(binding);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_rgbkey_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int rgbkey_relay_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api rgbkey_relay_api = {
    .binding_pressed = on_rgbkey_binding_pressed,
    .binding_released = on_rgbkey_binding_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

#define RGBKEY_INST(n)                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, rgbkey_relay_init, NULL, NULL, NULL, POST_KERNEL,                   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rgbkey_relay_api);

DT_INST_FOREACH_STATUS_OKAY(RGBKEY_INST)

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
void rgbkey_relay_send(uint32_t packed) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(rgbkey)),
        .param1 = packed,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
}
#endif
