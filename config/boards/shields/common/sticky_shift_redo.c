/*
 * Re-arm a sticky Shift after a Backspace.
 *
 * Tap the sticky Shift, type the wrong letter, Backspace it -- and the Shift is
 * gone, spent on the letter you just deleted. The next key comes out lowercase
 * and you reach for the Shift again. This puts it back: when the key before a
 * Backspace was typed under a Shift that has since let go, the Backspace
 * re-arms a one-shot Shift for the key that follows.
 *
 * Only the thumb's sticky Shift counts. The home-row Shift is a sticky key too
 * (&sk_long), and at the keycode level the two are the same thing: Shift down,
 * letter, Shift up. jjb does not want a Backspace after a home-row-shifted
 * letter to arm anything, so the thumb's Shift goes through the wrapper
 * behaviour at the bottom of this file (&sk_redo), which forwards to &sk and
 * marks the Shift it armed as re-armable. A letter typed under that mark, or
 * under a Shift this file re-armed, is what a Backspace looks for.
 *
 * Everything else is left alone by construction:
 *
 *   - home-row Shift (&sk_long), held or released unused: never marked.
 *   - Shift still held through the Backspace: nothing to re-arm.
 *   - caps_word: shifts by implicit modifier, never a Shift keycode.
 *   - `&kp LS(X)` -- the "{" combos and the like: the Shift rides inside the
 *     keycode and never appears as one, so a deleted "{" arms nothing.
 *
 * Backspace with the re-armed Shift still waiting is the one wrinkle: ZMK's
 * sticky key spends itself on ANY key, so a second Backspace would eat the
 * Shift and send the host a shifted Backspace. That is harmless to every host
 * we use, and here it re-arms again, so a run of Backspaces keeps the Shift
 * waiting for the first real key.
 *
 * The re-arm is invoked synchronously from the Backspace's release, not queued.
 * Backspace on this layout is a tap-dance that resolves on the NEXT key's press,
 * so a queued re-arm would land after that key had already gone out lowercase.
 *
 * Only a central has a keymap or raises keycode events -- a peripheral does not
 * even link the symbols -- so this compiles to nothing there.
 */

#define DT_DRV_COMPAT zmk_behavior_sticky_shift_redo

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define REDO_ON_CENTRAL 1
#else
#define REDO_ON_CENTRAL 0
#endif

/* A Shift armed through &sk_redo is waiting for a key. Central only; on a
 * peripheral the wrapper below still has to exist for the shared keymap to
 * resolve, but nothing there ever presses it. */
static bool source_armed;

#if REDO_ON_CENTRAL


#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/keys.h>

/* The layout's own one-shot Shift; the same node the thumb's tap-dance taps. */
#define REDO_BEHAVIOR_DEV DEVICE_DT_NAME(DT_NODELABEL(sk))
#define REDO_BEHAVIOR_PARAM LSHIFT

/*
 * The sticky-key driver files its instances by key position. This one has no
 * key, so it needs a position no key has: past the largest transform in the
 * workspace (48) and well short of the driver's own "free slot" sentinel
 * (UINT32_MAX).
 */
#define REDO_POSITION 0x1000

static bool lshift_down;
static bool rshift_down;

/* The last non-modifier key went out under a re-armable Shift -- the thumb's
 * or one this file put back -- and was not itself a Backspace. What a
 * Backspace needs to find. */
static bool last_key_shifted;

/* A Shift this file armed is still waiting for a key. */
static bool redo_armed;

/* A qualifying Backspace is down; re-arm on its release. */
static bool redo_pending;

static void arm_sticky_shift(int64_t timestamp) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = REDO_BEHAVIOR_DEV,
        .param1 = REDO_BEHAVIOR_PARAM,
        .param2 = 0,
    };
    struct zmk_behavior_binding_event event = {
        .layer = zmk_keymap_highest_layer_active(),
        .position = REDO_POSITION,
        .timestamp = timestamp,
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
    };

    int ret = zmk_behavior_invoke_binding(&binding, event, true);
    if (ret == 0) {
        ret = zmk_behavior_invoke_binding(&binding, event, false);
    }
    if (ret < 0) {
        LOG_WRN("sticky shift redo: %s failed (%d)", REDO_BEHAVIOR_DEV, ret);
        return;
    }
    redo_armed = true;
    LOG_DBG("sticky shift redo: re-armed");
}

static int keycode_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (ev == NULL || ev->usage_page != HID_USAGE_KEY) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    switch (ev->keycode) {
    case HID_USAGE_KEY_KEYBOARD_LEFTSHIFT:
        lshift_down = ev->state;
        break;
    case HID_USAGE_KEY_KEYBOARD_RIGHTSHIFT:
        rshift_down = ev->state;
        break;
    default:
        break;
    }
    if (is_mod(ev->usage_page, ev->keycode)) {
        if (!lshift_down && !rshift_down) {
            /* Whatever armed it has let go: spent, timed out, or lifted. */
            redo_armed = false;
            source_armed = false;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    const bool shift_down = lshift_down || rshift_down;
    const bool redo_shift_down = shift_down && (source_armed || redo_armed);

    if (ev->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        if (ev->state) {
            /*
             * Re-arm when the Shift that covered the last key is gone -- or
             * when the Shift over this Backspace is the one we armed, which
             * this Backspace is about to spend.
             */
            redo_pending = last_key_shifted && (!shift_down || redo_armed);
        } else if (redo_pending) {
            redo_pending = false;
            arm_sticky_shift(ev->timestamp);
            /* The next key is meant to be capital; a further Backspace
             * re-arms again rather than being the end of it. */
            last_key_shifted = true;
        }
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state) {
        last_key_shifted = redo_shift_down;
        redo_pending = false;
        /* Any other key spends a waiting Shift, whoever armed it. */
        redo_armed = false;
        source_armed = false;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(sticky_shift_redo, keycode_listener);
ZMK_SUBSCRIPTION(sticky_shift_redo, zmk_keycode_state_changed);

#endif /* REDO_ON_CENTRAL */

/* ------------------------------------------------------------------ */
/* &sk_redo: the thumb's sticky Shift, marked as re-armable            */
/* ------------------------------------------------------------------ */

struct sticky_shift_redo_config {
    struct zmk_behavior_binding sticky;
};

static int on_sk_redo_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct sticky_shift_redo_config *cfg = dev->config;
    struct zmk_behavior_binding sticky = cfg->sticky;
    sticky.param1 = binding->param1;

    int ret = zmk_behavior_invoke_binding(&sticky, event, true);
    if (ret == 0 && REDO_ON_CENTRAL) {
        source_armed = true;
    }
    return ret;
}

static int on_sk_redo_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct sticky_shift_redo_config *cfg = dev->config;
    struct zmk_behavior_binding sticky = cfg->sticky;
    sticky.param1 = binding->param1;

    return zmk_behavior_invoke_binding(&sticky, event, false);
}

static int sk_redo_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api sk_redo_api = {
    .binding_pressed = on_sk_redo_pressed,
    .binding_released = on_sk_redo_released,
};

#define SK_REDO_INST(n)                                                                            \
    static const struct sticky_shift_redo_config sk_redo_config_##n = {                           \
        .sticky = {.behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(n, bindings, 0))},        \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, sk_redo_init, NULL, NULL, &sk_redo_config_##n, POST_KERNEL,         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &sk_redo_api);

DT_INST_FOREACH_STATUS_OKAY(SK_REDO_INST)
