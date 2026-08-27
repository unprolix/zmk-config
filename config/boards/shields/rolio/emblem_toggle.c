/*
 * The key that swaps which emblem the base layer shows.
 *
 * This lives with the KEYBOARD, not with the vista508 display it drives, and
 * that is deliberate. Only the left half carries a screen, but both halves
 * compile the same keymap, so a `&emblem_toggle` node declared in the display
 * shield's overlay leaves the other half with an undefined label and no build
 * at all. The node is declared in rolio.dtsi and this driver is built on both
 * halves; on the half with no screen the work below simply has nothing to do.
 *
 * The work is handed to ZMK's display queue rather than done here. A behaviour
 * runs on the event thread, and LVGL may only be touched from the display
 * thread -- drawing from anywhere else races it and the shared scratch buffers,
 * which survives a few operations and then faults.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_emblem_toggle

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
#include <zmk/display.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)

/*
 * Defined by the vista508's status screen. Weak, so a build that has a display
 * but not this particular screen still links -- the behaviour then does
 * nothing rather than refusing to build.
 */
__weak void vista_emblem_next(void) {}

static void emblem_toggle_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    vista_emblem_next();
}

static K_WORK_DEFINE(emblem_toggle_work, emblem_toggle_work_cb);

#endif /* CONFIG_ZMK_DISPLAY */

static int on_emblem_toggle_pressed(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
    /*
     * Before the display is up its work queue is not running, so a submission
     * would sit unclaimed; there is also nothing to redraw yet.
     */
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &emblem_toggle_work);
    }
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_emblem_toggle_released(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int emblem_toggle_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api emblem_toggle_api = {
    .binding_pressed = on_emblem_toggle_pressed,
    .binding_released = on_emblem_toggle_released,
};

#define EMBLEM_TOGGLE_INST(n)                                                                      \
    BEHAVIOR_DT_INST_DEFINE(n, emblem_toggle_init, NULL, NULL, NULL, POST_KERNEL,                  \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &emblem_toggle_api);

DT_INST_FOREACH_STATUS_OKAY(EMBLEM_TOGGLE_INST)
