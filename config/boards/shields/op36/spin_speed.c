/*
 * The keys that change how fast the Qube's emblem turns.
 *
 * This lives with the KEYBOARD rather than with the qube_display shield that
 * owns the screen, for the reason the Rolio's emblem_toggle.c gives: all three
 * parts compile the same keymap, so a `&spin_faster` declared only in the
 * display shield leaves the two halves with an undefined label and no build.
 * The nodes are declared in op36.dtsi and this driver is built everywhere; on a
 * half, the weak hook below has nothing to do.
 *
 * The adjustment is handed to the display queue rather than made here. A
 * behaviour runs on the event thread, and LVGL may only be touched from the
 * display thread -- reaching into it from anywhere else races the shared draw
 * buffers, which survives a while and then faults.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_spin_speed

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
#include <zmk/display.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct behavior_spin_speed_config {
    int8_t step;
};

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)

/*
 * Defined by the Qube's status screen. Weak, so a build with a display but not
 * that particular screen still links: the key then does nothing rather than
 * refusing to build.
 */
__weak void qube_spin_adjust(int step) { ARG_UNUSED(step); }

/*
 * The pending step, rather than a work item per key: two presses closer
 * together than the display queue can drain would otherwise lose one, and the
 * queue rejects a resubmission of work already queued.
 */
static atomic_t pending_step;

static void spin_speed_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    qube_spin_adjust((int)atomic_set(&pending_step, 0));
}

static K_WORK_DEFINE(spin_speed_work, spin_speed_work_cb);

#endif /* CONFIG_ZMK_DISPLAY */

static int spin_speed_binding_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
    const struct behavior_spin_speed_config *cfg =
        zmk_behavior_get_binding(binding->behavior_dev)->config;

    /*
     * Before the display is up its queue is not running, so a submission would
     * sit unclaimed -- and there is nothing turning yet to speed up.
     */
    if (zmk_display_is_initialized()) {
        atomic_add(&pending_step, cfg->step);
        k_work_submit_to_queue(zmk_display_work_q(), &spin_speed_work);
    }
#else
    ARG_UNUSED(binding);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int spin_speed_binding_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api spin_speed_driver_api = {
    .binding_pressed = spin_speed_binding_pressed,
    .binding_released = spin_speed_binding_released,
};

#define SPIN_SPEED_INST(n)                                                                         \
    static const struct behavior_spin_speed_config spin_speed_config_##n = {                       \
        .step = (int8_t)DT_INST_PROP(n, step),                                                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &spin_speed_config_##n, POST_KERNEL,               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &spin_speed_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SPIN_SPEED_INST)
