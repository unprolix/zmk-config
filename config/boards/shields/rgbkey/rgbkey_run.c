/*
 * Deciding what the strip should show. Central only.
 *
 * Working out the active layer needs the keymap, which only the central has,
 * and the live modifiers come out of HID state, which only the central keeps.
 * Both are resolved here and sent to the far half, which paints its own keys.
 *
 * On the Rolio this is also where the bigram mode lives. The characters typed,
 * the gap between presses and whether the arcane key has an expansion waiting
 * are all things only the central hears, so they are worked out here too and
 * travel in the relay's second word.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/modifiers.h>

#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/rgb_underglow.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid_indicators.h>
#endif

#include "rgbkey.h"
#if RGBKEY_HAS_LAYER_OWNER
#include "../rgbzone/rgbzone_owner.h"
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

void rgbkey_relay_send(uint32_t packed, uint32_t blink);

/*
 * Layers are matched by display-name rather than by index so that inserting a
 * layer in the keymap does not silently repaint every other one.
 */
static enum rgbkey_scene scene_for(const char *name) {
    if (name == NULL) {
        return RKS_NONE;
    }
    for (size_t i = 0; i < RGBKEY_LAYER_COUNT; i++) {
        if (strcmp(rgbkey_layers[i].name, name) == 0) {
            return rgbkey_layers[i].scene;
        }
    }
    return RKS_NONE;
}

/*
 * Caps lock as the host has it. Bit 1 of the HID keyboard LED report, per the
 * USB HID usage tables; ZMK carries the report through but names none of the
 * bits.
 *
 * Cached rather than read fresh on every repaint: the indicators are stored per
 * endpoint, so a repaint landing while the endpoint switches -- or while none is
 * selected, which happens on every disconnect -- reads an empty slot and decides
 * caps is off. The host has not changed its mind; we asked at a bad moment.
 */
#define HID_LED_CAPS_LOCK BIT(1)

static uint8_t cached_indicators;

static bool host_caps_lock(void) { return (cached_indicators & HID_LED_CAPS_LOCK) != 0; }

static uint32_t refresh(bool force_relay);

#if RGBKEY_HAS_BLINK

/*
 * THE BIGRAM MODE ("blink", leader B L I N K).
 *
 * Off at boot; the toggle does not survive a reboot.
 *
 * What it works out, all of it from events only the central sees:
 *
 *   - the last two printable characters, in the order the host received them
 *   - how long passed between the PRESSES that produced them, and which hand
 *     made the second
 *   - whether the key just pressed is a letter the arcane key would expand,
 *     read straight from the arcane_left / arcane_right devicetree nodes so the
 *     hint cannot drift from arcane.dtsi
 *   - any key that registers twice inside a window no finger can manage, which
 *     is the phantom-contact doubling, caught as it happens
 */

/* Characters kept, so backspace can step back through them. */
#define RGBKEY_BLINK_HISTORY 8

/*
 * Two presses of one key closer than this are a contact bouncing, not a finger:
 * a deliberate double tap takes several times longer. The residual phantom
 * contacts measured on the eyelash sit at 2-3ms.
 */
#define RGBKEY_BLINK_DOUBLE_MS       15
#define RGBKEY_BLINK_DOUBLE_FLASH_MS 400

/* The gap between the two presses of a bigram, bucketed. Past HALTING it is not a bigram in
   flow at all, and shows nothing. */
#define RGBKEY_BLINK_FAST_MS    120
#define RGBKEY_BLINK_STEADY_MS  250
#define RGBKEY_BLINK_SLOW_MS    500
#define RGBKEY_BLINK_HALTING_MS 1000

/*
 * The mode changes the relayed word on nearly every keystroke, where the scene
 * word used to change on layer switches alone. An arcane expansion fires several
 * characters inside a few milliseconds, and each one would be its own split
 * command. Coalesce: never send more often than this, and always send the latest.
 */
#define RGBKEY_RELAY_MIN_INTERVAL_MS 40

/* Keyboard-page usages 0xE0..0xE7 are the eight modifier keys themselves. */
#define HID_USAGE_KEY_FIRST_MODIFIER 0xE0
#define HID_USAGE_KEY_LAST_MODIFIER  0xE7

#define RGBKEY_BLINK_SHIFT_MODS (MOD_LSFT | MOD_RSFT)
/* With any of these held a key is a command, not a character: Ctrl+C types nothing. */
#define RGBKEY_BLINK_COMMAND_MODS (MOD_LCTL | MOD_RCTL | MOD_LALT | MOD_RALT | MOD_LGUI | MOD_RGUI)

#define RGBKEY_NO_POSITION UINT32_MAX

#ifndef ZMK_HID_USAGE_ID
#define RGBKEY_HID_USAGE_ID_MASK 0xFFFF
#define ZMK_HID_USAGE_ID(usage) ((usage) & RGBKEY_HID_USAGE_ID_MASK)
#endif

#define RGBKEY_ARCANE_LEFT_NODE  DT_NODELABEL(arcane_left)
#define RGBKEY_ARCANE_RIGHT_NODE DT_NODELABEL(arcane_right)
#define RGBKEY_KEY_REPEAT_NODE   DT_NODELABEL(key_repeat)

#if DT_NODE_EXISTS(RGBKEY_ARCANE_LEFT_NODE) && DT_NODE_EXISTS(RGBKEY_ARCANE_RIGHT_NODE) &&        \
    DT_NODE_EXISTS(RGBKEY_KEY_REPEAT_NODE)
#define RGBKEY_HAS_ARCANE 1

/*
 * Each arcane key is an antecedent-morph whose bindings pair one-to-one with its
 * antecedents. A letter "has an expansion" on that thumb when its binding is
 * anything but &key_repeat, which is what the thumb does for a letter with no
 * table entry.
 */
#define RGBKEY_ARCANE_KEY(node, prop, idx) (uint16_t)ZMK_HID_USAGE_ID(DT_PROP_BY_IDX(node, prop, idx)),
#define RGBKEY_ARCANE_EXPANDS(node, prop, idx)                                                     \
    (!DT_SAME_NODE(DT_PHANDLE_BY_IDX(node, bindings, idx), RGBKEY_KEY_REPEAT_NODE)),

BUILD_ASSERT(DT_PROP_LEN(RGBKEY_ARCANE_LEFT_NODE, antecedents) ==
                 DT_PROP_LEN(RGBKEY_ARCANE_LEFT_NODE, bindings),
             "arcane_left needs exactly one binding per antecedent");
BUILD_ASSERT(DT_PROP_LEN(RGBKEY_ARCANE_RIGHT_NODE, antecedents) ==
                 DT_PROP_LEN(RGBKEY_ARCANE_RIGHT_NODE, bindings),
             "arcane_right needs exactly one binding per antecedent");

struct rgbkey_arcane {
    const uint16_t *keys;
    const bool *expands;
    size_t len;
    int32_t max_delay_ms;
};

static const uint16_t left_arcane_keys[] = {
    DT_FOREACH_PROP_ELEM(RGBKEY_ARCANE_LEFT_NODE, antecedents, RGBKEY_ARCANE_KEY)};
static const bool left_arcane_expands[] = {
    DT_FOREACH_PROP_ELEM(RGBKEY_ARCANE_LEFT_NODE, antecedents, RGBKEY_ARCANE_EXPANDS)};
static const uint16_t right_arcane_keys[] = {
    DT_FOREACH_PROP_ELEM(RGBKEY_ARCANE_RIGHT_NODE, antecedents, RGBKEY_ARCANE_KEY)};
static const bool right_arcane_expands[] = {
    DT_FOREACH_PROP_ELEM(RGBKEY_ARCANE_RIGHT_NODE, antecedents, RGBKEY_ARCANE_EXPANDS)};

static const struct rgbkey_arcane left_arcane = {
    left_arcane_keys, left_arcane_expands, ARRAY_SIZE(left_arcane_keys),
    DT_PROP(RGBKEY_ARCANE_LEFT_NODE, max_delay_ms)};
static const struct rgbkey_arcane right_arcane = {
    right_arcane_keys, right_arcane_expands, ARRAY_SIZE(right_arcane_keys),
    DT_PROP(RGBKEY_ARCANE_RIGHT_NODE, max_delay_ms)};

#define RGBKEY_ARCANE_LONGEST_DELAY_MS                                                             \
    MAX(DT_PROP(RGBKEY_ARCANE_LEFT_NODE, max_delay_ms),                                            \
        DT_PROP(RGBKEY_ARCANE_RIGHT_NODE, max_delay_ms))
#else
#define RGBKEY_HAS_ARCANE 0
#endif

static bool blink_enabled;

struct blink_char {
    char c;
    /* When the press that produced it happened, and on which half. */
    int64_t pressed_at;
    bool right;
};

static struct blink_char blink_history[RGBKEY_BLINK_HISTORY];
static size_t blink_count;
static enum rgbkey_rhythm blink_rhythm;
static bool blink_rhythm_right;
static bool blink_doubled;

/* The latest key press on either half. */
static uint32_t press_position = RGBKEY_NO_POSITION;
static int64_t press_at;
static bool press_right;

/* The latest non-modifier key and when it went down: what the arcane key compares against. */
static uint32_t antecedent_usage;
static int64_t antecedent_at;

/* Re-evaluate the word once an arcane expansion's window has closed. */
static void blink_repaint(struct k_work *work) {
    ARG_UNUSED(work);
    refresh(false);
}

static K_WORK_DELAYABLE_DEFINE(blink_expiry_work, blink_repaint);

static void blink_doubled_end(struct k_work *work) {
    ARG_UNUSED(work);
    blink_doubled = false;
    refresh(false);
}

static K_WORK_DELAYABLE_DEFINE(blink_doubled_work, blink_doubled_end);

static enum rgbkey_rhythm rhythm_for(int64_t gap_ms) {
    if (gap_ms < RGBKEY_BLINK_FAST_MS) {
        return RKR_FAST;
    }
    if (gap_ms < RGBKEY_BLINK_STEADY_MS) {
        return RKR_STEADY;
    }
    if (gap_ms < RGBKEY_BLINK_SLOW_MS) {
        return RKR_SLOW;
    }
    if (gap_ms < RGBKEY_BLINK_HALTING_MS) {
        return RKR_HALTING;
    }
    return RKR_NONE;
}

static void blink_push(char c) {
    if (blink_count == RGBKEY_BLINK_HISTORY) {
        memmove(&blink_history[0], &blink_history[1],
                sizeof(blink_history[0]) * (RGBKEY_BLINK_HISTORY - 1));
        blink_count--;
    }
    blink_history[blink_count++] =
        (struct blink_char){.c = c, .pressed_at = press_at, .right = press_right};
}

static void blink_on_position(const struct zmk_position_state_changed *ev) {
    if (!ev->state) {
        return;
    }
    if (ev->position == press_position && ev->timestamp - press_at < RGBKEY_BLINK_DOUBLE_MS) {
        blink_doubled = true;
        LOG_INF("rgbkey: blink: position %u registered twice %dms apart", ev->position,
                (int)(ev->timestamp - press_at));
        k_work_reschedule(&blink_doubled_work, K_MSEC(RGBKEY_BLINK_DOUBLE_FLASH_MS));
    }
    press_position = ev->position;
    press_at = ev->timestamp;
    press_right = rgbkey_position_is_right((uint8_t)ev->position);
}

static void blink_on_keycode(const struct zmk_keycode_state_changed *ev) {
    if (!ev->state || ev->usage_page != HID_USAGE_KEY) {
        return;
    }
    /* A modifier key is not an antecedent, or shift-then-letter would never hint. */
    if (ev->keycode >= HID_USAGE_KEY_FIRST_MODIFIER && ev->keycode <= HID_USAGE_KEY_LAST_MODIFIER) {
        return;
    }

    antecedent_usage = ev->keycode;
    antecedent_at = ev->timestamp;
#if RGBKEY_HAS_ARCANE
    k_work_reschedule(&blink_expiry_work, K_MSEC(RGBKEY_ARCANE_LONGEST_DELAY_MS + 1));
#endif

    const zmk_mod_flags_t mods =
        ev->implicit_modifiers | ev->explicit_modifiers | zmk_hid_get_explicit_mods();

    if (ev->keycode == HID_USAGE_KEY_KEYBOARD_DELETE_BACKSPACE) {
        /* A modified backspace takes a word or more; start clean rather than guess how much. */
        if (mods & RGBKEY_BLINK_COMMAND_MODS) {
            blink_count = 0;
        } else if (blink_count > 0) {
            blink_count--;
        }
        blink_rhythm = RKR_NONE;
        return;
    }
    if (mods & RGBKEY_BLINK_COMMAND_MODS) {
        return;
    }

    const char c =
        rgbkey_char_for(ev->keycode, (mods & RGBKEY_BLINK_SHIFT_MODS) != 0, host_caps_lock());
    if (c == RGBKEY_BLINK_NO_CHAR) {
        return;
    }

    if (blink_count == 0) {
        blink_rhythm = RKR_NONE;
    } else {
        /*
         * Zero when both characters came from one press -- an arcane expansion
         * or any other macro. The rhythm belongs to the press, and was measured
         * when that press arrived, so leave it.
         */
        const int64_t gap = press_at - blink_history[blink_count - 1].pressed_at;
        if (gap > 0) {
            blink_rhythm = rhythm_for(gap);
            blink_rhythm_right = press_right;
        }
    }
    blink_push(c);
}

#if RGBKEY_HAS_ARCANE
static bool arcane_live(const struct rgbkey_arcane *arcane, int64_t now) {
    if (arcane->max_delay_ms >= 0 && now - antecedent_at >= arcane->max_delay_ms) {
        return false;
    }
    for (size_t i = 0; i < arcane->len; i++) {
        if (arcane->keys[i] == antecedent_usage) {
            return arcane->expands[i];
        }
    }
    return false;
}
#endif

static uint32_t blink_word(void) {
    if (!blink_enabled) {
        return 0;
    }
    uint32_t word = RGBKEY_BLINK_ON_BIT;
    if (blink_count >= 1) {
        word |= ((uint32_t)blink_history[blink_count - 1].c & RGBKEY_BLINK_CHAR_MASK)
                << RGBKEY_BLINK_CUR_SHIFT;
    }
    if (blink_count >= 2) {
        word |= ((uint32_t)blink_history[blink_count - 2].c & RGBKEY_BLINK_CHAR_MASK)
                << RGBKEY_BLINK_PREV_SHIFT;
    }
    word |= ((uint32_t)blink_rhythm & RGBKEY_BLINK_RHYTHM_MASK) << RGBKEY_BLINK_RHYTHM_SHIFT;
    if (blink_rhythm_right) {
        word |= RGBKEY_BLINK_RHYTHM_RIGHT_BIT;
    }
    if (blink_doubled) {
        word |= RGBKEY_BLINK_DOUBLED_BIT;
    }
#if RGBKEY_HAS_ARCANE
    const int64_t now = k_uptime_get();
    if (arcane_live(&left_arcane, now)) {
        word |= RGBKEY_BLINK_ARCANE_LEFT_BIT;
    }
    if (arcane_live(&right_arcane, now)) {
        word |= RGBKEY_BLINK_ARCANE_RIGHT_BIT;
    }
#endif
    return word;
}

void rgbkey_blink_toggle(void) {
    blink_enabled = !blink_enabled;
    blink_count = 0;
    blink_rhythm = RKR_NONE;
    blink_doubled = false;
    press_position = RGBKEY_NO_POSITION;
    antecedent_usage = 0;
    LOG_INF("rgbkey: blink mode %s", blink_enabled ? "on" : "off");
    refresh(false);
}

static uint32_t relay_packed;
static uint32_t relay_blink;
static int64_t relay_sent_at;

static void relay_now(struct k_work *work) {
    ARG_UNUSED(work);
    relay_sent_at = k_uptime_get();
    rgbkey_relay_send(relay_packed, relay_blink);
}

static K_WORK_DELAYABLE_DEFINE(relay_work, relay_now);

static void relay(uint32_t packed, uint32_t blink) {
    relay_packed = packed;
    relay_blink = blink;
    const int64_t wait = relay_sent_at + RGBKEY_RELAY_MIN_INTERVAL_MS - k_uptime_get();
    /* Does nothing if a send is already scheduled; that send picks up these values. */
    k_work_schedule(&relay_work, K_MSEC(wait > 0 ? wait : 0));
}

#else /* !RGBKEY_HAS_BLINK */

static uint32_t blink_word(void) { return 0; }

/* The toggle exists wherever its devicetree node does; with no layout for the mode it does nothing. */
void rgbkey_blink_toggle(void) {}

static void relay(uint32_t packed, uint32_t blink) { rgbkey_relay_send(packed, blink); }

#endif /* RGBKEY_HAS_BLINK */

/*
 * force_relay: send to the far half even when nothing changed. True only from
 * the periodic tick, which exists to catch a peripheral that has just come up.
 */
static uint32_t refresh(bool force_relay) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t id = zmk_keymap_layer_index_to_id(index);
    const char *name = zmk_keymap_layer_name(id);

    uint32_t packed =
        rgbkey_pack(scene_for(name), zmk_hid_get_explicit_mods(), host_caps_lock());

    /*
     * Which hand is holding this layer open, from the momentary behaviour that
     * opened it. Only the central can know -- it is the half with the keymap --
     * so it travels to the other side in the packed word.
     */
    bool from_right = false;
    uint8_t owner_pos = 0;
    bool side_known = false;
#if RGBKEY_HAS_LAYER_OWNER
    /* By ID, not by index: the momentary behaviour records binding->param1,
       which is a layer id (zmk_keymap_layer_activate takes one). The two
       coincide unless layer reordering is on, so a mismatch here would show
       up only on some future build. rgbzone_run.c looks it up the same way. */
    side_known = rgbzone_owner_key((uint8_t)id, &from_right, &owner_pos);
#endif
    packed = rgbkey_pack_owner(packed, side_known, from_right, owner_pos);

    const uint32_t blink = blink_word();

    /*
     * Logged on CHANGE only, which is what makes it readable: this runs on
     * every keycode event, so logging unconditionally buries the transition
     * you are looking for. A picture that will not settle shows up here as the
     * same two or three lines repeating -- and it says WHICH of the three
     * inputs is moving, layer or mods or caps, which the LEDs cannot.
     *
     * The bigram word is deliberately not logged: it is what was typed.
     */
    static uint32_t last_logged = ~0u;
    if (packed != last_logged) {
        last_logged = packed;
        LOG_INF("rgbkey: layer=%s scene=%u mods=0x%02x caps=%d held=%s at=%d",
                name ? name : "(unnamed)", (unsigned)rgbkey_scene_of(packed),
                (unsigned)rgbkey_mods_of(packed), (int)rgbkey_caps_of(packed),
                !rgbkey_side_known(packed) ? "?" : (rgbkey_side_is_right(packed) ? "R" : "L"),
                rgbkey_side_known(packed) ? (int)rgbkey_owner_pos(packed) : -1);
    }

    rgbkey_apply(packed, blink);

    /*
     * ONLY WHEN IT CHANGED. This runs on every keycode event -- press AND
     * release -- and relaying unconditionally put a split command on the wire
     * for every one of them, whether or not the picture had moved. On a 115200
     * half-duplex link that is already polling flat out, ordinary typing
     * overran the central's TX ring: the console filled with
     * "No room to send command to the peripheral 0", and a command that does
     * not fit is a command that goes out truncated. ZMK's wired transport does
     * not degrade when it meets a fragment it cannot parse, it wedges for good
     * (see the split-link notes), so this was a keyboard that stopped working
     * under fast typing and stayed stopped.
     *
     * The local repaint was already deduplicated inside rgbkey_apply; only the
     * relay was not.
     */
    static uint32_t last_relayed = ~0u;
    static uint32_t last_blink_relayed = ~0u;
    if (force_relay || packed != last_relayed || blink != last_blink_relayed) {
        last_relayed = packed;
        last_blink_relayed = blink;
        relay(packed, blink);
    }

    return packed;
}

/* Any real event means the picture may have moved; go back to ticking fast. */
static void rgbkey_tick_now(void);

static int rgbkey_listener(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    const struct zmk_hid_indicators_changed *ind = as_zmk_hid_indicators_changed(eh);
    if (ind != NULL) {
        cached_indicators = (uint8_t)ind->indicators;
    }
#endif
#if RGBKEY_HAS_BLINK
    const struct zmk_position_state_changed *position = as_zmk_position_state_changed(eh);
    if (position != NULL) {
        /* Subscribed for the bigram mode alone: with it off, a press changes nothing here. */
        if (!blink_enabled) {
            return ZMK_EV_EVENT_BUBBLE;
        }
        blink_on_position(position);
    }
    const struct zmk_keycode_state_changed *keycode = as_zmk_keycode_state_changed(eh);
    if (keycode != NULL && blink_enabled) {
        blink_on_keycode(keycode);
    }
#endif
    refresh(false);
    rgbkey_tick_now();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgbkey_run, rgbkey_listener);
ZMK_SUBSCRIPTION(rgbkey_run, zmk_layer_state_changed);
/*
 * Modifier changes arrive as keycode events -- ZMK declares a
 * zmk_modifiers_state_changed type but never raises it -- so the live flags
 * are read back out of HID state on every keycode instead.
 */
ZMK_SUBSCRIPTION(rgbkey_run, zmk_keycode_state_changed);
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
/* Caps lock arrives from the host, not from a keypress here. */
ZMK_SUBSCRIPTION(rgbkey_run, zmk_hid_indicators_changed);
#endif
#if RGBKEY_HAS_BLINK
/* Key presses on either half, for the rhythm and the doubling detector. */
ZMK_SUBSCRIPTION(rgbkey_run, zmk_position_state_changed);
#endif

/*
 * Nothing tells a central that a peripheral has connected:
 * zmk_split_peripheral_status_changed is raised on the peripheral only. A half
 * that boots or reconnects into an unchanged state would otherwise show
 * nothing until the next layer change, so re-send on a timer.
 */
/*
 * The tick exists because nothing tells a central that a peripheral has
 * connected, so a half that reconnects into unchanged state would show nothing
 * until the next layer change.
 *
 * It used to fire every ten seconds forever, which is a wake and a BLE
 * transmission every ten seconds for the whole hour before deep sleep, to
 * re-send a picture that had not changed. The local repaint was already
 * suppressed by the cache in rgbkey_apply; the relay was not.
 *
 * Now it backs off: fast while something is actually lit or has just changed,
 * then progressively slower once the picture has been still for a while. A
 * reconnecting peripheral still catches up within the slow interval, and an
 * idle keyboard showing nothing costs almost nothing to keep showing nothing.
 */
#define RGBKEY_REFRESH_FAST_SECONDS 10
#define RGBKEY_REFRESH_SLOW_SECONDS 120
/* How many unchanged fast ticks before backing off. */
#define RGBKEY_REFRESH_SETTLE_TICKS 6

static void rgbkey_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(rgbkey_tick_work, rgbkey_tick);

static void rgbkey_tick(struct k_work *work) {
    ARG_UNUSED(work);

    /*
     * Underglow can switch itself back on without being asked: with
     * AUTO_OFF_IDLE it restores whatever it was doing before the keyboard went
     * idle, so a single off() at boot is not enough. Two writers on one strip
     * shows up as wrong colours at random brightness, not as an error.
     */
    bool on = false;
    if (zmk_rgb_underglow_get_state(&on) == 0 && on) {
        zmk_rgb_underglow_off();
    }

    /*
     * refresh() re-sends to the peripheral every time; the local repaint is
     * suppressed when nothing changed. Watch the word it would send, and once
     * it has been the same for a while, stop asking so often.
     */
    static uint32_t last_sent;
    static uint8_t unchanged;

    uint32_t before = last_sent;
    last_sent = refresh(true);
    unchanged = (last_sent == before && unchanged < RGBKEY_REFRESH_SETTLE_TICKS)
                    ? unchanged + 1
                    : (last_sent == before ? unchanged : 0);

    bool settled = unchanged >= RGBKEY_REFRESH_SETTLE_TICKS;
    k_work_reschedule(&rgbkey_tick_work,
                      K_SECONDS(settled ? RGBKEY_REFRESH_SLOW_SECONDS
                                        : RGBKEY_REFRESH_FAST_SECONDS));
}

/*
 * Bring the tick back to its fast interval after a real event, so the far half
 * is not waiting out a slow period just as things start happening.
 */
static void rgbkey_tick_now(void) {
    k_work_reschedule(&rgbkey_tick_work, K_SECONDS(RGBKEY_REFRESH_FAST_SECONDS));
}

static void rgbkey_start(struct k_work *work) {
    ARG_UNUSED(work);
    zmk_rgb_underglow_off();
    k_work_reschedule(&rgbkey_tick_work, K_MSEC(500));
}

static K_WORK_DELAYABLE_DEFINE(rgbkey_start_work, rgbkey_start);

static int rgbkey_init(void) {
    /* Let the split link and the strip driver settle before taking the chain. */
    k_work_reschedule(&rgbkey_start_work, K_SECONDS(3));
    return 0;
}

SYS_INIT(rgbkey_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
