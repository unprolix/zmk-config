/*
 * Central-half status screen for the nice!view's 160x68 landscape panel.
 *
 * The widgets are the ones worked out on the Rolio's vista508 screen --
 * connection as words, both halves' battery, the live leader sequence -- but
 * the layout is not portable: vista508 is a 144x168 portrait panel and stacks
 * everything vertically with room for a wrapped list. Here there are 68 pixels
 * of height total, so the same information has to go into two status rows
 * flanking a single centred line, and the leader candidates have to run
 * comma-separated across the width rather than one per line.
 *
 * Written against LVGL 9.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/wpm_status.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk-leader-key/leader_state.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Track the panel size from devicetree so this follows any geometry change. */
#define SCREEN_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCREEN_H DT_PROP(DT_CHOSEN(zephyr_display), height)

/*
 * Height of the status row along the top. The mono theme's default padding
 * would eat 8 pixels a side, which is affordable on a 144x168 panel and is not
 * here, so the screen's padding is zeroed in zmk_display_status_screen() and
 * these offsets are measured from the true panel edge.
 */
#define ROW_TOP_H  15
#define STATUS_MAX 24

static lv_obj_t *conn_label;
static lv_obj_t *batt_label;
static lv_obj_t *leader_label;
static lv_obj_t *layer_label;

/* Peripheral level arrives by event; there is no polling accessor for it. */
static uint8_t peripheral_soc;
static bool peripheral_seen;

/* ------------------------------------------------------------------ */
/* Layer name                                                          */
/* ------------------------------------------------------------------ */

/*
 * Local layer-name widget, for the same reason the vista508 needed one: ZMK's
 * layer_status widget formats into `char text[14]` as LV_SYMBOL_KEYBOARD " %s",
 * and the glyph is three UTF-8 bytes, leaving ten for the name plus a
 * terminator. "hierophant" is exactly ten and "superscript" is eleven, so both
 * lose their last character. This one has room and drops the symbol.
 */
#define LAYER_NAME_MAX 32

struct jjb_layer_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void jjb_layer_update_cb(struct jjb_layer_state state) {
    if (layer_label == NULL) {
        return;
    }

    char text[LAYER_NAME_MAX];

    if (state.label == NULL || strlen(state.label) == 0) {
        snprintf(text, sizeof(text), "%i", state.index);
    } else {
        snprintf(text, sizeof(text), "%s", state.label);
    }

    lv_label_set_text(layer_label, text);
}

static struct jjb_layer_state jjb_layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct jjb_layer_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_layer_status, struct jjb_layer_state, jjb_layer_update_cb,
                            jjb_layer_get_state)
ZMK_SUBSCRIPTION(jjb_layer_status, zmk_layer_state_changed);

#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

/* ------------------------------------------------------------------ */
/* Connection                                                          */
/* ------------------------------------------------------------------ */

struct jjb_conn_state {
    struct zmk_endpoint_instance selected;
    enum zmk_transport preferred;
    bool profile_connected;
    bool profile_bonded;
};

static void jjb_conn_update_cb(struct jjb_conn_state state) {
    if (conn_label == NULL) {
        return;
    }

    char text[STATUS_MAX];
    enum zmk_transport transport = state.selected.transport;
    bool connected = transport != ZMK_TRANSPORT_NONE;

    /* When nothing is connected, show what it is reaching for instead. */
    if (!connected) {
        transport = state.preferred;
    }

    switch (transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), connected ? "USB" : "USB ...");
        break;
    case ZMK_TRANSPORT_BLE:
        if (!state.profile_bonded) {
            snprintf(text, sizeof(text), "BT%d open", zmk_ble_active_profile_index() + 1);
        } else {
            snprintf(text, sizeof(text), "BT%d %s", zmk_ble_active_profile_index() + 1,
                     state.profile_connected ? "ok" : "...");
        }
        break;
    default:
        snprintf(text, sizeof(text), "offline");
        break;
    }

    lv_label_set_text(conn_label, text);
}

static struct jjb_conn_state jjb_conn_get_state(const zmk_event_t *eh) {
    return (struct jjb_conn_state){
        .selected = zmk_endpoint_get_selected(),
        .preferred = zmk_endpoint_get_preferred_transport(),
        .profile_connected = zmk_ble_active_profile_is_connected(),
        .profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_conn_status, struct jjb_conn_state, jjb_conn_update_cb,
                            jjb_conn_get_state)
ZMK_SUBSCRIPTION(jjb_conn_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(jjb_conn_status, zmk_ble_active_profile_changed);

/* ------------------------------------------------------------------ */
/* Battery, both halves                                                */
/* ------------------------------------------------------------------ */

struct jjb_batt_state {
    uint8_t central;
    uint8_t peripheral;
    bool have_peripheral;
};

static void jjb_batt_update_cb(struct jjb_batt_state state) {
    if (batt_label == NULL) {
        return;
    }

    char text[STATUS_MAX];

    /*
     * No "%" here, unlike the vista508: two levels plus two percent signs does
     * not fit beside the connection text in 160 pixels.
     */
    if (state.have_peripheral) {
        snprintf(text, sizeof(text), "L%d R%d", state.central, state.peripheral);
    } else {
        snprintf(text, sizeof(text), "L%d", state.central);
    }

    lv_label_set_text(batt_label, text);
}

static struct jjb_batt_state jjb_batt_get_state(const zmk_event_t *eh) {
    /*
     * The peripheral's level only ever arrives as an event, so latch it as it
     * goes past; there is no accessor to poll it back out of ZMK.
     */
    /*
     * eh is NULL on the one synthetic call the listener macro makes at init.
     * as_zmk_*() dereferences it without checking, and only gets away with it
     * on this SoC because address 0 is mapped flash; do not rely on that.
     */
    const struct zmk_peripheral_battery_state_changed *ev =
        (eh == NULL) ? NULL : as_zmk_peripheral_battery_state_changed(eh);
    if (ev != NULL) {
        peripheral_soc = ev->state_of_charge;
        peripheral_seen = true;
    }

    return (struct jjb_batt_state){
        .central = zmk_battery_state_of_charge(),
        .peripheral = peripheral_soc,
        .have_peripheral = peripheral_seen,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_batt_status, struct jjb_batt_state, jjb_batt_update_cb,
                            jjb_batt_get_state)
ZMK_SUBSCRIPTION(jjb_batt_status, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(jjb_batt_status, zmk_peripheral_battery_state_changed);

/* ------------------------------------------------------------------ */
/* Leader sequence                                                     */
/* ------------------------------------------------------------------ */

/*
 * While a leader sequence is being entered, the layer name and WPM give up the
 * screen to show what is still reachable. With well over a hundred sequences
 * defined, listing them the instant leader is pressed would be noise, so
 * nothing is listed until the candidate set is small enough to read.
 *
 * The threshold is lower than the vista508's six: that panel could stack six
 * names vertically, whereas here they share three short lines running across
 * the width, so four is about what stays legible.
 */
#define LEADER_LIST_THRESHOLD 4
#define LEADER_TEXT_MAX       128

/*
 * ZMK_DISPLAY_WIDGET_LISTENER's generated init() calls the update callback once
 * straight away (see the macro in zmk/display.h: listener##_init() ends with
 * listener##_work_cb(NULL)). That first call arrives while the screen is still
 * being assembled, so the WPM widget may not exist yet and
 * zmk_widget_wpm_status_obj() hands back a NULL lv_obj_t -- which LVGL will
 * happily dereference. Route every show/hide through here.
 */
static void jjb_set_wpm_hidden(bool hidden) {
#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
    lv_obj_t *wpm = zmk_widget_wpm_status_obj(&wpm_status_widget);
    if (wpm == NULL) {
        return;
    }
    if (hidden) {
        lv_obj_add_flag(wpm, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(wpm, LV_OBJ_FLAG_HIDDEN);
    }
#endif
}

static void jjb_leader_update_cb(struct zmk_leader_state_changed state) {
    if (leader_label == NULL || layer_label == NULL) {
        return;
    }

    if (!state.active) {
        /* Hand the screen back to the layer name and WPM. */
        lv_label_set_text(leader_label, "");
        lv_obj_clear_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
        jjb_set_wpm_hidden(false);
        return;
    }

    lv_obj_add_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
    jjb_set_wpm_hidden(true);

    char text[LEADER_TEXT_MAX];

    if (state.candidate_count == 0) {
        snprintf(text, sizeof(text), "LEADER\nno match");
    } else if (state.candidate_count > LEADER_LIST_THRESHOLD) {
        snprintf(text, sizeof(text), "LEADER\n%d options", state.candidate_count);
    } else {
        int used = snprintf(text, sizeof(text), "LEADER\n");
        for (uint8_t i = 0; i < state.candidate_count && used < (int)sizeof(text) - 1; i++) {
            const char *name = zmk_leader_candidate_name(i);
            if (name == NULL) {
                break;
            }
            used += snprintf(text + used, sizeof(text) - used, "%s%s", i > 0 ? ", " : "", name);
        }
    }

    lv_label_set_text(leader_label, text);
}

static struct zmk_leader_state_changed jjb_leader_get_state(const zmk_event_t *eh) {
    /* NULL on the listener macro's synthetic init call -- see jjb_batt_get_state. */
    const struct zmk_leader_state_changed *ev =
        (eh == NULL) ? NULL : as_zmk_leader_state_changed(eh);
    if (ev != NULL) {
        return *ev;
    }
    return (struct zmk_leader_state_changed){0};
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_leader_status, struct zmk_leader_state_changed,
                            jjb_leader_update_cb, jjb_leader_get_state)
ZMK_SUBSCRIPTION(jjb_leader_status, zmk_leader_state_changed);

/* ------------------------------------------------------------------ */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /*
     * Deliberately NOT lv_obj_remove_style_all() here. That strips the mono
     * theme's background along with the padding, leaving bg_opa transparent so
     * the labels render with no contrast and the panel comes out blank. Zero
     * the padding on its own instead -- 68 pixels of height cannot spare the
     * theme's default 8 per side.
     */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    /* Top row: connection on the left, both batteries on the right. */
    conn_label = lv_label_create(screen);
    lv_obj_set_style_text_font(conn_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 0, 0);

    batt_label = lv_label_create(screen);
    lv_obj_set_style_text_font(batt_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(batt_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    jjb_conn_status_init();
    jjb_batt_status_init();

    /*
     * Layer name, centred in what is left. LV_LABEL_LONG_WRAP breaks on word
     * boundaries only, so a single long word cannot wrap and would overflow
     * instead; at font 18 the longest name here ("superscript", eleven
     * characters) still fits the width comfortably.
     */
    layer_label = lv_label_create(screen);
    lv_obj_set_width(layer_label, SCREEN_W);
    lv_label_set_long_mode(layer_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(layer_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(layer_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(layer_label, LV_ALIGN_TOP_MID, 0, ROW_TOP_H + 12);
    jjb_layer_status_init();

    /*
     * WPM in the bottom-left, opposite the layer name's centre line.
     *
     * This has to come before jjb_leader_status_init() below: that call runs
     * the leader callback once immediately, and the callback shows or hides
     * this widget.
     */
#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_t *wpm = zmk_widget_wpm_status_obj(&wpm_status_widget);
    lv_obj_set_style_text_font(wpm, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(wpm, LV_ALIGN_BOTTOM_LEFT, 0, 0);
#endif

    /*
     * Occupies everything below the top row, which is more than the layer name
     * needs but lets three lines of candidates fit. Only one of the two is
     * visible at a time.
     */
    leader_label = lv_label_create(screen);
    lv_obj_set_width(leader_label, SCREEN_W);
    lv_label_set_long_mode(leader_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(leader_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_set_style_text_align(leader_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(leader_label, LV_ALIGN_TOP_MID, 0, ROW_TOP_H);
    lv_label_set_text(leader_label, "");
    jjb_leader_status_init();

    return screen;
}
