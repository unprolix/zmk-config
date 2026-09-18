/*
 * Status screen for the Ergohaven Qube: a 280x240 colour panel on a split
 * central that has no keys of its own.
 *
 * Laid out around what is worth a glance mid-typing, in descending order:
 *
 *   - the LAYER, as the largest thing on the panel;
 *   - held MODIFIERS, so a home-row mod that fired unbidden is visible;
 *   - leader candidates, and the bluetooth profile list on that layer, both of
 *     which take over the middle band the way they do on the Rolio;
 *   - caps lock, which changes what every key does;
 *   - the host connection, named where the host can be named.
 *
 * Battery and link state are deliberately SMALL -- two chips in the top right --
 * because they matter for about ten seconds a week. When a half actually needs
 * charging the screen stops being polite and puts a red bar across the bottom.
 *
 * The circle-cube is a BACKDROP, not a panel of its own: full panel height,
 * centred, grey on black, with every readout drawn over it, turning slowly --
 * one degree every tenth of a second, which LVGL does by rotating the single
 * greyscale image rather than by stepping through pre-rendered frames.
 *
 * It stays up through the lists too. What a list gets instead is a scrim: the
 * label paints a near-opaque backing behind its own text, so the cube shows
 * around the candidates rather than through them.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/iterable_sections.h>

#include <lvgl.h>

#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/endpoints.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>
#include <zmk/split/transport/central.h>
#include <zmk-leader-key/leader_state.h>

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
#include <zmk/events/hid_indicators_changed.h>
#endif

#include "circlecube_img.h"
#include "vista_canvas.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * What this file needs, stated where the need lives. ZMK's custom-screen
 * choice selects none of it, and a missing one otherwise shows up as a blank
 * panel or an error naming neither LVGL nor this file.
 */
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#error "qube status screen: the Qube is a split central; build it with ZMK_SPLIT_ROLE_CENTRAL"
#endif
#if !IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#error "qube status screen: needs CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING=y"
#endif
#if !IS_ENABLED(CONFIG_LV_USE_LABEL) || !IS_ENABLED(CONFIG_LV_USE_CANVAS)
#error "qube status screen: needs CONFIG_LV_USE_LABEL and CONFIG_LV_USE_CANVAS"
#endif
#if !IS_ENABLED(CONFIG_LV_USE_IMAGE)
#error "qube status screen: the backdrop is an image widget -- needs CONFIG_LV_USE_IMAGE"
#endif
#if !IS_ENABLED(CONFIG_LV_DRAW_SW_SUPPORT_L8)
#error "qube status screen: the emblem is an L8 image -- needs CONFIG_LV_DRAW_SW_SUPPORT_L8"
#endif
#if !IS_ENABLED(CONFIG_LV_DRAW_SW_SUPPORT_ARGB8888)
#error "qube status screen: the modifier row is an ARGB8888 canvas, so it needs alpha"
#endif
#if !IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_14) || !IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_16) ||      \
    !IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_20) || !IS_ENABLED(CONFIG_LV_FONT_MONTSERRAT_28)
#error "qube status screen: needs CONFIG_LV_FONT_MONTSERRAT_14, _16, _20 and _28"
#endif

/* ------------------------------------------------------------------ */
/* Geometry and colour                                                 */
/* ------------------------------------------------------------------ */

#define SCREEN_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCREEN_H DT_PROP(DT_CHOSEN(zephyr_display), height)

#define MARGIN 8

/* Top strip: host connection on the left, the two half chips on the right. */
#define TOP_Y    6
#define TOP_H    22
#define CHIP_W   62
#define CHIP_GAP 6

/* The middle band: caps lock above, then layer name or a list. */
#define BAND_TOP    36
#define CAPS_H      26
#define BAND_H      (MODS_Y - BAND_TOP - MARGIN)

/* Held modifiers, centred near the bottom. */
#define MODS_ROW_W (VISTA_MOD_ICON_SLOTS * VISTA_MOD_ICON_SLOT_W)
#define MODS_Y     192

/* The charge warning, full width along the bottom; hidden when not needed. */
#define ALERT_H 30
#define ALERT_Y (SCREEN_H - ALERT_H)

#define COLOR_BG      0x000000
#define COLOR_TEXT    0xe8eaed
#define COLOR_MUTED   0x8a93a3
#define COLOR_OK      0x3ddc84
#define COLOR_WAIT    0xffb020
#define COLOR_BAD     0xff5c5c
#define COLOR_LAYER   0x6cb8ff
#define COLOR_ALERT   0xb3241f

/* Battery bands. Below the alert level the bottom bar appears. */
#define BATT_GOOD_PCT  50
#define BATT_LOW_PCT   25
#define BATT_ALERT_PCT 15

#define FONT_SMALL  (&lv_font_montserrat_14)
#define FONT_MEDIUM (&lv_font_montserrat_16)
#define FONT_LARGE  (&lv_font_montserrat_20)

/*
 * The layer name gets Strong Glasgow at 40px (glasgow_40.c), not a montserrat.
 * It is the one piece of text on the panel that is read at a glance rather
 * than examined, so it is worth a display face and the size to carry it.
 * Everything else stays montserrat, which is built for small sizes and has the
 * full character set the lists need.
 */
LV_FONT_DECLARE(glasgow_40);
#define FONT_HUGE (&glasgow_40)

/* How often link state and battery are read back from the split transport. */
#define LINK_POLL_MS 1000

#define TEXT_MAX 40

/*
 * The backdrop sits at the middle of the panel in both axes. It is as tall as
 * the panel, so the vertical term is zero today; it is written out anyway
 * because the emblem is regenerated by a script and its size is a constant
 * there, not here.
 */
BUILD_ASSERT(CIRCLECUBE_W <= SCREEN_W && CIRCLECUBE_H <= SCREEN_H,
             "the circle-cube backdrop is larger than the panel");
#define BACKDROP_X ((SCREEN_W - CIRCLECUBE_W) / 2)
#define BACKDROP_Y ((SCREEN_H - CIRCLECUBE_H) / 2)

/*
 * The turn. LVGL rotates the one emblem image itself, in tenths of a degree,
 * so the step can be as fine as the eye wants rather than as coarse as flash
 * allows: one degree every tenth of a second is 36 seconds a revolution, slow
 * enough to be scenery and smooth enough not to read as stepping.
 */
#define BACKDROP_STEP_MS     100
#define BACKDROP_FULL_TENTHS 3600

/*
 * The speeds the two SYSTEM-layer keys step through, in tenths of a degree per
 * tick. The first is a full stop, and the default is a degree a tick: 36
 * seconds a revolution. Each step up is roughly double, so a handful of
 * presses covers everything from stationary to unmistakably spinning.
 */
static const uint8_t backdrop_speeds[] = {0, 2, 5, 10, 20, 40, 80};
#define BACKDROP_SPEED_DEFAULT 3

/*
 * A list drawn straight over the art is unreadable, but the art stays: the
 * LIST takes a scrim instead, painting its own near-opaque backing so the cube
 * shows around the text rather than through it.
 */
#define BAND_SCRIM_OPA    LV_OPA_80
#define BAND_SCRIM_PAD    8
#define BAND_SCRIM_RADIUS 10

static lv_obj_t *backdrop;
static int32_t backdrop_angle;
static uint8_t backdrop_speed = BACKDROP_SPEED_DEFAULT;

static void backdrop_spin_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(backdrop_spin, backdrop_spin_cb);

static void backdrop_spin_cb(struct k_work *work) {
    ARG_UNUSED(work);
    const uint8_t tenths = backdrop_speeds[backdrop_speed];

    /*
     * A stopped emblem is left exactly where it was rather than snapped to
     * upright: stopping it is for looking at it, and where it happens to be is
     * as good a place as any.
     */
    if (tenths > 0 && backdrop != NULL) {
        backdrop_angle = (backdrop_angle + tenths) % BACKDROP_FULL_TENTHS;
        lv_image_set_rotation(backdrop, backdrop_angle);
    }
    k_work_reschedule_for_queue(zmk_display_work_q(), &backdrop_spin, K_MSEC(BACKDROP_STEP_MS));
}

/*
 * Called from the spin_speed behaviour (../op36/spin_speed.c), already on the
 * display queue. Positive is faster; zero restores the default. The ends of
 * the table hold rather than wrap, so leaning on a key parks it at stopped or
 * at the fastest instead of flipping to the far end.
 */
void qube_spin_adjust(int step) {
    if (step == 0) {
        backdrop_speed = BACKDROP_SPEED_DEFAULT;
    } else if (step > 0) {
        const int room = (int)ARRAY_SIZE(backdrop_speeds) - 1 - backdrop_speed;
        backdrop_speed += MIN(step, room);
    } else {
        backdrop_speed -= MIN(-step, (int)backdrop_speed);
    }
    LOG_INF("qube screen: emblem speed %u (%u tenths of a degree a tick)", backdrop_speed,
            backdrop_speeds[backdrop_speed]);
}

/* ------------------------------------------------------------------ */
/* The two halves                                                      */
/* ------------------------------------------------------------------ */

#define HALF_COUNT CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS

enum side { SIDE_LEFT = 0, SIDE_RIGHT = 1, SIDE_COUNT = 2 };

BUILD_ASSERT(HALF_COUNT <= SIDE_COUNT, "one chip per half, and there are two chips");

/*
 * A peripheral's source number is the order it bonded in, not its side. The
 * side is learned from the first key it sends: in the shared 42-position layout
 * (zmk-helpers key-labels/42.h) the first six of each twelve-key row, and the
 * first three thumbs, are the left hand's.
 */
#define LAYOUT_ROW_KEYS        12
#define LAYOUT_MAIN_KEYS       36
#define LAYOUT_THUMBS_PER_SIDE 3

/* Stored as side + 1, so that a zeroed atomic means "not yet known". */
#define SIDE_CODE_UNKNOWN 0

struct half_state {
    atomic_t side_code; /* written from the event thread */
    bool linked;        /* display queue only */
    bool have_batt;
    uint8_t batt;
    /*
     * How many times this half has dropped since the Qube booted. On the
     * screen because a dropping half is the one fault here that a console
     * cannot be left attached for -- the USB logging build loads the same
     * workqueue that scans and receives keys, so the measurement changes what
     * it measures. A number on the panel costs nothing and is always running.
     */
    uint16_t drops;
};

static struct half_state halves[HALF_COUNT];

static enum side side_of_position(uint32_t position) {
    if (position < LAYOUT_MAIN_KEYS) {
        return (position % LAYOUT_ROW_KEYS) < LAYOUT_ROW_KEYS / 2 ? SIDE_LEFT : SIDE_RIGHT;
    }
    return position < LAYOUT_MAIN_KEYS + LAYOUT_THUMBS_PER_SIDE ? SIDE_LEFT : SIDE_RIGHT;
}

/*
 * Which chip a source is drawn on: its own side once known; otherwise the chip
 * the OTHER half is not on, if that one is known; otherwise its source number.
 */
static int chip_for_source(uint8_t source) {
    const atomic_val_t code = atomic_get(&halves[source].side_code);
    if (code != SIDE_CODE_UNKNOWN) {
        return (int)code - 1;
    }
    for (uint8_t other = 0; other < HALF_COUNT; other++) {
        const atomic_val_t other_code = atomic_get(&halves[other].side_code);
        if (other != source && other_code != SIDE_CODE_UNKNOWN) {
            return SIDE_COUNT - (int)other_code;
        }
    }
    return source % SIDE_COUNT;
}

static lv_obj_t *chips[SIDE_COUNT];
static lv_obj_t *alert_bar;
static lv_obj_t *alert_label;

static const char *const side_initials[SIDE_COUNT] = {"L", "R"};
static const char *const side_names[SIDE_COUNT] = {"LEFT", "RIGHT"};

static lv_color_t batt_color(uint8_t pct) {
    if (pct > BATT_GOOD_PCT) {
        return lv_color_hex(COLOR_OK);
    }
    return lv_color_hex(pct > BATT_LOW_PCT ? COLOR_WAIT : COLOR_BAD);
}

static void set_hidden(lv_obj_t *obj, bool hidden) {
    if (obj == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

/*
 * The chips, and the charge warning underneath them.
 *
 * A chip is four characters at most: the hand, then the charge, or a cross
 * when that half is not connected. Anything more belongs on a screen nobody is
 * typing in front of.
 */
static void halves_draw(void) {
    int worst_chip = -1;
    uint8_t worst_pct = 100;

    for (int c = 0; c < SIDE_COUNT; c++) {
        if (chips[c] == NULL) {
            continue;
        }

        int source = -1;
        for (uint8_t s = 0; s < HALF_COUNT; s++) {
            if (chip_for_source(s) == c) {
                source = s;
                break;
            }
        }

        char text[TEXT_MAX];
        /*
         * The drop count rides along after a dot, and only once a half has
         * actually dropped: a healthy pair shows nothing but hand and charge.
         */
        char drops[TEXT_MAX / 2] = "";
        if (source >= 0 && halves[source].drops > 0) {
            snprintf(drops, sizeof(drops), " %u", halves[source].drops);
        }

        if (source < 0 || !halves[source].linked) {
            snprintf(text, sizeof(text), "%s " LV_SYMBOL_CLOSE "%s", side_initials[c], drops);
            lv_label_set_text(chips[c], text);
            lv_obj_set_style_text_color(chips[c], lv_color_hex(COLOR_BAD), LV_PART_MAIN);
            continue;
        }

        const struct half_state *h = &halves[source];
        if (h->have_batt) {
            snprintf(text, sizeof(text), "%s %u%s", side_initials[c], h->batt, drops);
            lv_obj_set_style_text_color(chips[c], batt_color(h->batt), LV_PART_MAIN);
            if (h->batt < worst_pct) {
                worst_pct = h->batt;
                worst_chip = c;
            }
        } else {
            snprintf(text, sizeof(text), "%s ok", side_initials[c]);
            lv_obj_set_style_text_color(chips[c], lv_color_hex(COLOR_MUTED), LV_PART_MAIN);
        }
        lv_label_set_text(chips[c], text);
    }

    const bool alert = worst_chip >= 0 && worst_pct <= BATT_ALERT_PCT;
    if (alert && alert_label != NULL) {
        char text[TEXT_MAX];
        snprintf(text, sizeof(text), "%s HALF %u%% - CHARGE", side_names[worst_chip], worst_pct);
        lv_label_set_text(alert_label, text);
    }
    set_hidden(alert_bar, !alert);
}

/*
 * Link state and battery, read back from the split transport rather than taken
 * from events: ZMK raises no event on the central when a peripheral links or
 * drops, and battery events from both halves can coalesce into one display
 * update and lose one of them.
 */
#define SOURCE_ID_BUF 8
BUILD_ASSERT(HALF_COUNT <= SOURCE_ID_BUF, "source id buffer smaller than the peripheral count");

static void link_poll_work_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(link_poll_work, link_poll_work_cb);

static void link_poll_work_cb(struct k_work *work) {
    ARG_UNUSED(work);

    bool linked[HALF_COUNT] = {false};
    STRUCT_SECTION_FOREACH(zmk_split_transport_central, t) {
        if (t->api == NULL || t->api->get_available_source_ids == NULL) {
            continue;
        }
        uint8_t ids[SOURCE_ID_BUF];
        const int count = t->api->get_available_source_ids(ids);
        for (int i = 0; i < count; i++) {
            if (ids[i] < HALF_COUNT) {
                linked[ids[i]] = true;
            }
        }
    }

    bool changed = false;
    for (uint8_t s = 0; s < HALF_COUNT; s++) {
        struct half_state *h = &halves[s];
        if (h->linked != linked[s]) {
            LOG_INF("qube screen: half %u %s", s + 1, linked[s] ? "linked" : "unlinked");
            if (!linked[s] && h->drops < UINT16_MAX) {
                h->drops++;
            }
            h->linked = linked[s];
            changed = true;
        }
        uint8_t level = 0;
        if (zmk_split_central_get_peripheral_battery_level(s, &level) == 0 && level > 0 &&
            (!h->have_batt || h->batt != level)) {
            h->batt = level;
            h->have_batt = true;
            changed = true;
        }
    }

    if (changed) {
        halves_draw();
    }

    k_work_reschedule_for_queue(zmk_display_work_q(), &link_poll_work, K_MSEC(LINK_POLL_MS));
}

/*
 * Key presses are not shown, but they are what teaches the chips which hand is
 * which, so the first press from each half still has to be seen.
 */
struct qube_key_state {
    uint8_t learned;
};

static void qube_keys_update_cb(struct qube_key_state state) {
    if (state.learned) {
        halves_draw();
    }
}

static struct qube_key_state qube_keys_get_state(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev =
        eh != NULL ? as_zmk_position_state_changed(eh) : NULL;
    if (ev == NULL || !ev->state || ev->source >= HALF_COUNT) {
        return (struct qube_key_state){.learned = 0};
    }
    const bool learned = atomic_cas(&halves[ev->source].side_code, SIDE_CODE_UNKNOWN,
                                    (atomic_val_t)side_of_position(ev->position) + 1);
    return (struct qube_key_state){.learned = learned ? 1 : 0};
}

ZMK_DISPLAY_WIDGET_LISTENER(qube_keys, struct qube_key_state, qube_keys_update_cb,
                            qube_keys_get_state)
ZMK_SUBSCRIPTION(qube_keys, zmk_position_state_changed);

/* ------------------------------------------------------------------ */
/* Host connection                                                     */
/* ------------------------------------------------------------------ */

/* From ../common/bt_names.c: the host on a profile, by name where it has one. */
const char *jjb_bt_name_for(uint8_t profile);

static lv_obj_t *host_label;

struct qube_host_state {
    struct zmk_endpoint_instance selected;
    enum zmk_transport preferred;
    bool profile_connected;
    bool profile_bonded;
};

static void qube_host_update_cb(struct qube_host_state state) {
    if (host_label == NULL) {
        return;
    }

    char text[TEXT_MAX];
    uint32_t color = COLOR_WAIT;
    enum zmk_transport transport = state.selected.transport;
    const bool connected = transport != ZMK_TRANSPORT_NONE;

    /* When nothing is connected, show what it is reaching for instead. */
    if (!connected) {
        transport = state.preferred;
    }

    switch (transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), connected ? "USB" : "USB ...");
        color = connected ? COLOR_OK : COLOR_WAIT;
        break;
    case ZMK_TRANSPORT_BLE: {
        const int profile = zmk_ble_active_profile_index();
        if (!state.profile_bonded) {
            snprintf(text, sizeof(text), "BT%d unpaired", profile + 1);
        } else {
            const char *who = state.profile_connected ? jjb_bt_name_for((uint8_t)profile) : NULL;
            snprintf(text, sizeof(text), "BT%d %s", profile + 1,
                     who != NULL ? who : (state.profile_connected ? "ok" : "..."));
            color = state.profile_connected ? COLOR_OK : COLOR_WAIT;
        }
        break;
    }
    default:
        snprintf(text, sizeof(text), "offline");
        color = COLOR_BAD;
        break;
    }

    lv_label_set_text(host_label, text);
    lv_obj_set_style_text_color(host_label, lv_color_hex(color), LV_PART_MAIN);
}

static struct qube_host_state qube_host_get_state(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    return (struct qube_host_state){
        .selected = zmk_endpoint_get_selected(),
        .preferred = zmk_endpoint_get_preferred_transport(),
        .profile_connected = zmk_ble_active_profile_is_connected(),
        .profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(qube_host, struct qube_host_state, qube_host_update_cb,
                            qube_host_get_state)
ZMK_SUBSCRIPTION(qube_host, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(qube_host, zmk_ble_active_profile_changed);

/* ------------------------------------------------------------------ */
/* The middle band: layer, leader, profiles, caps                      */
/* ------------------------------------------------------------------ */

/* The layer that lists bluetooth profiles instead of its name. */
#define BLUETOOTH_LAYER_NAME "bluetooth"

/* Caps lock is bit 1 of the host's HID keyboard LED report. */
#define HID_LED_CAPS_LOCK BIT(1)

#define LEADER_LIST_THRESHOLD 6
#define LEADER_TEXT_MAX       128
#define LEADER_NAME_MAX       32
#define PROFILE_LINE_MAX      24

static lv_obj_t *caps_label;
static lv_obj_t *band_label;

static uint8_t cached_indicators;
static char layer_name[TEXT_MAX];
static bool leader_active;
static char leader_text[LEADER_TEXT_MAX];

/*
 * The profile list: marker, number, then whoever is on it -- a name where the
 * host gave one or the table knows it, an address tail if bonded but
 * anonymous, "--" for a free slot. A trailing dot means bonded but not
 * connected right now, as on the Rolio and the eyelash.
 */
static void bluetooth_list_text(char *out, size_t len) {
    const int active = zmk_ble_active_profile_index();
    int used = 0;
    out[0] = '\0';

    for (uint8_t i = 0; i < ZMK_BLE_PROFILE_COUNT && used < (int)len - 1; i++) {
        const char marker = (i == (uint8_t)active) ? '>' : ' ';
        char line[PROFILE_LINE_MAX];

        if (zmk_ble_profile_is_open(i)) {
            snprintf(line, sizeof(line), "%c%d --", marker, i + 1);
        } else {
            const char *who = jjb_bt_name_for(i);
            snprintf(line, sizeof(line), "%c%d %s%s", marker, i + 1, who != NULL ? who : "?",
                     zmk_ble_profile_is_connected(i) ? "" : ".");
        }
        used += snprintf(out + used, len - used, "%s%s", used ? "\n" : "", line);
    }
}

static void band_set(const lv_font_t *font, uint32_t color, const char *text) {
    lv_obj_set_style_text_font(band_label, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(band_label, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(band_label, text);
}

static void band_draw(void) {
    if (band_label == NULL) {
        return;
    }

    set_hidden(caps_label, (cached_indicators & HID_LED_CAPS_LOCK) == 0);

    /*
     * The emblem stays up through a leader sequence and through the profile
     * list; what changes is that those lists paint a scrim behind themselves.
     * A single layer name in 40px needs none -- it reads straight off the art.
     */
    const bool list_showing = leader_active || strcmp(layer_name, BLUETOOTH_LAYER_NAME) == 0;
    lv_obj_set_style_bg_opa(band_label, list_showing ? BAND_SCRIM_OPA : LV_OPA_TRANSP,
                            LV_PART_MAIN);

    if (leader_active) {
        band_set(FONT_MEDIUM, COLOR_TEXT, leader_text);
        return;
    }

    if (strcmp(layer_name, BLUETOOTH_LAYER_NAME) == 0) {
        char list[ZMK_BLE_PROFILE_COUNT * PROFILE_LINE_MAX];
        bluetooth_list_text(list, sizeof(list));
        band_set(FONT_SMALL, COLOR_TEXT, list);
        return;
    }

    band_set(FONT_HUGE, COLOR_LAYER, layer_name);
}

struct qube_layer_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void qube_layer_update_cb(struct qube_layer_state state) {
    if (state.label == NULL || state.label[0] == '\0') {
        snprintf(layer_name, sizeof(layer_name), "%d", state.index);
    } else {
        snprintf(layer_name, sizeof(layer_name), "%s", state.label);
    }
    band_draw();
}

static struct qube_layer_state qube_layer_get_state(const zmk_event_t *eh) {
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
    /*
     * Cached rather than polled: indicators are stored per endpoint, so a
     * repaint landing mid-switch would read an empty slot and drop caps.
     */
    const struct zmk_hid_indicators_changed *ind =
        eh != NULL ? as_zmk_hid_indicators_changed(eh) : NULL;
    if (ind != NULL) {
        cached_indicators = (uint8_t)ind->indicators;
    }
#endif
    const zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct qube_layer_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(qube_layer, struct qube_layer_state, qube_layer_update_cb,
                            qube_layer_get_state)
ZMK_SUBSCRIPTION(qube_layer, zmk_layer_state_changed);
/* Selecting or clearing a profile changes the list without changing layer. */
ZMK_SUBSCRIPTION(qube_layer, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(qube_layer, zmk_endpoint_changed);
#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)
ZMK_SUBSCRIPTION(qube_layer, zmk_hid_indicators_changed);
#endif

/*
 * While a leader sequence is being entered, the band shows what is still
 * reachable -- but only once the candidates are few enough to be worth reading.
 */
static void qube_leader_update_cb(struct zmk_leader_state_changed state) {
    if (!state.active) {
        leader_active = false;
        band_draw();
        return;
    }

    leader_active = true;
    int used = snprintf(leader_text, sizeof(leader_text), "LEADER");

    if (state.candidate_count == 0) {
        snprintf(leader_text, sizeof(leader_text), "LEADER\nno match");
    } else if (state.candidate_count > LEADER_LIST_THRESHOLD) {
        snprintf(leader_text, sizeof(leader_text), "LEADER\n%d options", state.candidate_count);
    } else {
        for (uint8_t i = 0; i < state.candidate_count && used < (int)sizeof(leader_text) - 1; i++) {
            const char *name = zmk_leader_candidate_name(i);
            if (name == NULL) {
                break;
            }
            char pretty[LEADER_NAME_MAX];
            snprintf(pretty, sizeof(pretty), "%s", name);
            vista_humanize(pretty);
            used += snprintf(leader_text + used, sizeof(leader_text) - used, "\n%s", pretty);
        }
    }
    band_draw();
}

static struct zmk_leader_state_changed qube_leader_get_state(const zmk_event_t *eh) {
    const struct zmk_leader_state_changed *ev =
        eh != NULL ? as_zmk_leader_state_changed(eh) : NULL;
    return ev != NULL ? *ev : (struct zmk_leader_state_changed){0};
}

ZMK_DISPLAY_WIDGET_LISTENER(qube_leader, struct zmk_leader_state_changed, qube_leader_update_cb,
                            qube_leader_get_state)
ZMK_SUBSCRIPTION(qube_leader, zmk_leader_state_changed);

/*
 * A host's name is learned on the BLE security callback, a moment AFTER its
 * profile connects. Runs in the BLE stack's context, so it only asks the
 * display queue to redraw.
 */
static void name_refresh_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    qube_host_update_cb(qube_host_get_state(NULL));
    band_draw();
}
static K_WORK_DEFINE(name_refresh_work, name_refresh_work_cb);

void jjb_bt_name_changed(void) { k_work_submit_to_queue(zmk_display_work_q(), &name_refresh_work); }

/* ------------------------------------------------------------------ */
/* Held modifiers                                                      */
/* ------------------------------------------------------------------ */

/*
 * GUI, ALT, SHIFT, CTRL across one row -- the Rolio's two bottom-corner pairs,
 * side by side. Driven off keycode events and read back from HID state: ZMK
 * declares a modifiers event but never raises it.
 */
struct mod_slot {
    enum vista_mod_icon icon;
    zmk_mod_flags_t mods;
};

static const struct mod_slot mod_slots[VISTA_MOD_ICON_SLOTS] = {
    {VISTA_MOD_ICON_GUI, MOD_LGUI | MOD_RGUI},
    {VISTA_MOD_ICON_ALT, MOD_LALT | MOD_RALT},
    {VISTA_MOD_ICON_SHIFT, MOD_LSFT | MOD_RSFT},
    {VISTA_MOD_ICON_CTRL, MOD_LCTL | MOD_RCTL},
};

/*
 * ARGB8888, not the Rolio's L8.
 *
 * An L8 canvas has no alpha, so filling it to draw the glyphs painted an
 * opaque black slab across the emblem -- the art vanished wherever the
 * modifier row sat, whether or not any modifier was held. With alpha the
 * canvas starts fully transparent and only the strokes are opaque, so the
 * glyphs sit ON the emblem rather than in a hole cut out of it.
 *
 * It costs 4 KB of RAM for a 68x16 row, which is the whole reason this is
 * affordable here and was not on a full-panel canvas.
 */
#define MODS_CANVAS_FORMAT LV_COLOR_FORMAT_ARGB8888
#define MODS_BUF_SIZE                                                                              \
    LV_CANVAS_BUF_SIZE(MODS_ROW_W, VISTA_MOD_ROW_H, LV_COLOR_FORMAT_GET_BPP(MODS_CANVAS_FORMAT),   \
                       LV_DRAW_BUF_STRIDE_ALIGN)

static lv_obj_t *mods_canvas;
static uint8_t mods_buf[MODS_BUF_SIZE] __aligned(CONFIG_LV_DRAW_BUF_ALIGN);

struct qube_mods_state {
    zmk_mod_flags_t mods;
};

static void qube_mods_update_cb(struct qube_mods_state state) {
    if (mods_canvas == NULL) {
        return;
    }
    lv_canvas_fill_bg(mods_canvas, lv_color_black(), LV_OPA_TRANSP);
    const lv_coord_t inset = (VISTA_MOD_ICON_SLOT_W - VISTA_MOD_ICON_SIZE) / 2;
    for (uint8_t slot = 0; slot < VISTA_MOD_ICON_SLOTS; slot++) {
        if (state.mods & mod_slots[slot].mods) {
            vista_draw_mod_icon(mods_canvas, slot * VISTA_MOD_ICON_SLOT_W + inset,
                                VISTA_MOD_ICON_PAD, mod_slots[slot].icon);
        }
    }
    lv_obj_invalidate(mods_canvas);
}

static struct qube_mods_state qube_mods_get_state(const zmk_event_t *eh) {
    ARG_UNUSED(eh);
    return (struct qube_mods_state){.mods = zmk_hid_get_explicit_mods()};
}

ZMK_DISPLAY_WIDGET_LISTENER(qube_mods, struct qube_mods_state, qube_mods_update_cb,
                            qube_mods_get_state)
ZMK_SUBSCRIPTION(qube_mods, zmk_keycode_state_changed);

/* ------------------------------------------------------------------ */
/* Construction                                                        */
/* ------------------------------------------------------------------ */

static const struct gpio_dt_spec backlight =
    GPIO_DT_SPEC_GET(DT_NODELABEL(qube_backlight_en), gpios);

static void backlight_on(void) {
    if (!gpio_is_ready_dt(&backlight)) {
        LOG_ERR("qube screen: backlight GPIO not ready");
        return;
    }
    gpio_pin_configure_dt(&backlight, GPIO_OUTPUT_ACTIVE);
}

static lv_obj_t *plain_obj(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static lv_obj_t *text_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(color), LV_PART_MAIN);
    lv_label_set_text(l, "");
    return l;
}

lv_obj_t *zmk_display_status_screen(void) {
    backlight_on();

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, lv_color_hex(COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    /*
     * The backdrop is created FIRST because LVGL's z-order is creation order
     * and nothing here reorders it: every readout below is drawn over the art.
     * The image is opaque -- its palette entry 0 is COLOR_BG -- so it repaints
     * its own rectangle and needs no backing.
     */
    backdrop = lv_image_create(screen);
    lv_image_set_src(backdrop, &circlecube_img);
    lv_obj_set_pos(backdrop, BACKDROP_X, BACKDROP_Y);
    /* Turn about the emblem's own centre, not the object's top-left corner. */
    lv_image_set_pivot(backdrop, CIRCLECUBE_W / 2, CIRCLECUBE_H / 2);
    lv_image_set_antialias(backdrop, true);

    /*
     * Everything a widget callback touches is created BEFORE its _init():
     * ZMK_DISPLAY_WIDGET_LISTENER's generated init runs the callback at once.
     */
    host_label = text_label(screen, FONT_MEDIUM, COLOR_WAIT);
    lv_obj_set_pos(host_label, MARGIN, TOP_Y);
    lv_obj_set_width(host_label, SCREEN_W - 2 * MARGIN - SIDE_COUNT * (CHIP_W + CHIP_GAP));
    lv_label_set_long_mode(host_label, LV_LABEL_LONG_CLIP);

    for (int c = 0; c < SIDE_COUNT; c++) {
        lv_obj_t *chip = plain_obj(screen);
        lv_obj_set_size(chip, CHIP_W, TOP_H);
        lv_obj_set_pos(chip, SCREEN_W - MARGIN - (SIDE_COUNT - c) * (CHIP_W + CHIP_GAP) + CHIP_GAP,
                       TOP_Y - 2);
        /*
         * No pill behind the text. A filled chip is a slab of flat grey across
         * the emblem, and the colour of the text already says everything the
         * background was saying.
         */

        chips[c] = text_label(chip, FONT_SMALL, COLOR_MUTED);
        lv_obj_set_width(chips[c], CHIP_W);
        lv_obj_set_style_text_align(chips[c], LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
        lv_obj_align(chips[c], LV_ALIGN_CENTER, 0, 0);
    }

    caps_label = text_label(screen, FONT_LARGE, COLOR_BAD);
    lv_label_set_text(caps_label, "CAPS LOCK");
    lv_obj_set_width(caps_label, SCREEN_W);
    lv_obj_set_style_text_align(caps_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(caps_label, 0, BAND_TOP);
    set_hidden(caps_label, true);

    band_label = text_label(screen, FONT_HUGE, COLOR_LAYER);
    lv_obj_set_width(band_label, SCREEN_W - 2 * MARGIN);
    lv_label_set_long_mode(band_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(band_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_pos(band_label, MARGIN, BAND_TOP + CAPS_H);
    lv_obj_set_height(band_label, BAND_H);
    /* The scrim itself; band_draw() turns its opacity on for lists only. */
    lv_obj_set_style_bg_color(band_label, lv_color_hex(COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(band_label, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_pad_all(band_label, BAND_SCRIM_PAD, LV_PART_MAIN);
    lv_obj_set_style_radius(band_label, BAND_SCRIM_RADIUS, LV_PART_MAIN);

    mods_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(mods_canvas, mods_buf, MODS_ROW_W, VISTA_MOD_ROW_H, MODS_CANVAS_FORMAT);
    lv_canvas_fill_bg(mods_canvas, lv_color_black(), LV_OPA_TRANSP);
    lv_obj_set_pos(mods_canvas, (SCREEN_W - MODS_ROW_W) / 2, MODS_Y);

    /* The charge warning: the one thing allowed to shout. */
    alert_bar = plain_obj(screen);
    lv_obj_set_size(alert_bar, SCREEN_W, ALERT_H);
    lv_obj_set_pos(alert_bar, 0, ALERT_Y);
    lv_obj_set_style_bg_color(alert_bar, lv_color_hex(COLOR_ALERT), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(alert_bar, LV_OPA_COVER, LV_PART_MAIN);
    alert_label = text_label(alert_bar, FONT_LARGE, COLOR_TEXT);
    lv_obj_set_width(alert_label, SCREEN_W);
    lv_obj_set_style_text_align(alert_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(alert_label, LV_ALIGN_CENTER, 0, 0);
    set_hidden(alert_bar, true);

    qube_host_init();
    qube_keys_init();
    qube_layer_init();
    qube_leader_init();
    qube_mods_init();

    halves_draw();
    k_work_reschedule_for_queue(zmk_display_work_q(), &link_poll_work, K_NO_WAIT);
    k_work_reschedule_for_queue(zmk_display_work_q(), &backdrop_spin, K_MSEC(BACKDROP_STEP_MS));

    return screen;
}
