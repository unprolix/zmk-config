/*
 * &blink: toggle the Rolio's bigram lighting mode. Bound to leader B L I N K.
 *
 * The mode's state lives in rgbkey_run.c on the central, which is the only half
 * that hears what is typed; the far half learns the mode is on from the relayed
 * word like everything else it draws. So this acts on the central alone, and on
 * the peripheral it is a key that does nothing.
 *
 * Built on both halves anyway, the way the emblem toggle is: the keymap is
 * shared, and a behaviour compiled on one half only is a label the other cannot
 * resolve.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgbkey_blink

#include <zephyr/device.h>
#include <zephyr/kernel.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
void rgbkey_blink_toggle(void);
#endif

static int on_blink_pressed(struct zmk_behavior_binding *binding,
                            struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    rgbkey_blink_toggle();
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_blink_released(struct zmk_behavior_binding *binding,
                             struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api blink_api = {
    .binding_pressed = on_blink_pressed,
    .binding_released = on_blink_released,
    .locality = BEHAVIOR_LOCALITY_CENTRAL,
};

static int blink_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

#define BLINK_INST(n)                                                                              \
    BEHAVIOR_DT_INST_DEFINE(n, blink_init, NULL, NULL, NULL, POST_KERNEL,                          \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &blink_api);

DT_INST_FOREACH_STATUS_OKAY(BLINK_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
