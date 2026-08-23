/*
 * Global brightness, and the keys that change it.
 *
 * ZMK's underglow has its own brightness, but rgbzone deliberately keeps
 * underglow switched off -- two writers on one strip shows up as wrong colours
 * at wandering brightness -- so its RGB_BRI/RGB_BRD do nothing here. This is
 * the replacement: one level for both halves, applied where the palette meets
 * the strip.
 *
 * Central only. The far half never decides anything about brightness; the
 * level travels to it in the same word as the colours (see rgbzone.h) and is
 * applied there on arrival.
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_rgbzone_level

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void rgbzone_refresh(void);

#define RGBZONE_SETTINGS_KEY "rgbzone/level"

static uint8_t level = ZONE_LEVEL_DEFAULT;

/*
 * What toggling off restores. Without it, off-then-on lands on the default
 * rather than on the brightness that was actually in use, so a keyboard set
 * dim comes back glaring every time.
 */
static uint8_t level_before_off = ZONE_LEVEL_DEFAULT;

uint8_t rgbzone_level_get(void) { return level; }

/*
 * Saved from the work queue rather than inline: settings writes touch flash,
 * which can block for long enough to matter on the thread raising key events.
 */
static void level_save(struct k_work *work) {
    ARG_UNUSED(work);
#if IS_ENABLED(CONFIG_SETTINGS)
    int ret = settings_save_one(RGBZONE_SETTINGS_KEY, &level, sizeof(level));
    if (ret < 0) {
        LOG_WRN("Failed to save rgbzone level: %d", ret);
    }
#endif
}

static K_WORK_DEFINE(level_save_work, level_save);

static void level_set(uint8_t next) {
    if (next >= ZONE_LEVEL_COUNT || next == level) {
        return;
    }
    level = next;
    if (level != 0) {
        level_before_off = level;
    }

    rgbzone_refresh();
    k_work_submit(&level_save_work);
}

void rgbzone_level_step(int delta) {
    int next = (int)level + delta;

    /*
     * Stepping down stops at the dimmest lit level rather than falling off
     * into the dark: turning the lights off is what the toggle is for, and a
     * key that sometimes dims and sometimes blacks out is worse at both jobs.
     */
    if (next < 1) {
        next = 1;
    }
    if (next > ZONE_LEVEL_COUNT - 1) {
        next = ZONE_LEVEL_COUNT - 1;
    }
    level_set((uint8_t)next);
}

void rgbzone_level_toggle(void) { level_set(level == 0 ? level_before_off : 0); }

#if IS_ENABLED(CONFIG_SETTINGS)

static int level_settings_load(const char *name, size_t len, settings_read_cb read_cb,
                               void *cb_arg) {
    if (settings_name_steq(name, "level", NULL)) {
        uint8_t stored;
        int ret = read_cb(cb_arg, &stored, sizeof(stored));
        if (ret >= 0 && stored < ZONE_LEVEL_COUNT) {
            level = stored;
            if (level != 0) {
                level_before_off = level;
            }
        }
        return 0;
    }
    return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(rgbzone, "rgbzone", NULL, level_settings_load, NULL, NULL);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/*
 * One behaviour, three instances: the step in devicetree says which. Nought
 * means toggle. Doing it this way rather than with a parameter keeps the
 * keymap free of shared constants -- &rgbz_up says what it does.
 */
struct behavior_rgbzone_level_config {
    int8_t step;
};

static int rgbzone_level_binding_pressed(struct zmk_behavior_binding *binding,
                                         struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
    const struct behavior_rgbzone_level_config *cfg =
        zmk_behavior_get_binding(binding->behavior_dev)->config;

    if (cfg->step == 0) {
        rgbzone_level_toggle();
    } else {
        rgbzone_level_step(cfg->step);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int rgbzone_level_binding_released(struct zmk_behavior_binding *binding,
                                          struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api rgbzone_level_driver_api = {
    .binding_pressed = rgbzone_level_binding_pressed,
    .binding_released = rgbzone_level_binding_released,
};

#define RGBZONE_LEVEL_INST(n)                                                                      \
    static const struct behavior_rgbzone_level_config rgbzone_level_config_##n = {                 \
        .step = (int8_t)DT_INST_PROP(n, step),                                                     \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &rgbzone_level_config_##n, POST_KERNEL,           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rgbzone_level_driver_api);

DT_INST_FOREACH_STATUS_OKAY(RGBZONE_LEVEL_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
