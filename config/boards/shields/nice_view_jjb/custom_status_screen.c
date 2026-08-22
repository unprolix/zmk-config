/*
 * Central-half status screen for the nice!view.
 *
 * The widgets are the ones worked out on the Rolio's vista508 screen --
 * connection as words rather than glyphs, both halves' battery, the live
 * leader sequence -- but neither that screen's layout nor its implementation
 * ports. vista508 is a 144x168 panel that LVGL can address directly with
 * ordinary labels. A nice!view cannot be: its framebuffer is 160x68 while the
 * readable orientation is 68x160, and a 1-bit display cannot be rotated by
 * LVGL (see canvas_util.h). Everything therefore goes through rotated L8
 * canvases, which means drawing rather than laying out labels, and redrawing
 * a whole band whenever anything in it changes.
 *
 * Three bands down the panel:
 *   top     connection, then both batteries
 *   middle  layer name -- or the leader sequence while one is in progress
 *   bottom  WPM (only 24 pixels of this band are on screen)
 *
 * 68 pixels of width is the binding constraint. Montserrat 14 fits roughly
 * nine characters, so the longest layer names ("superscript", "hierophant")
 * wrap to two lines, and leader names wrap too -- which is the other reason
 * underscores become spaces.
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
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/wpm.h>
#include <zmk-leader-key/leader_state.h>

#include "canvas_util.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_MAX     24
#define LAYER_NAME_MAX 32

/*
 * With well over a hundred sequences defined, listing them the instant leader
 * is pressed would be noise, so nothing is listed until the candidate set is
 * small enough to read. Three fit the middle band once names wrap.
 */
#define LEADER_LIST_THRESHOLD 3
#define LEADER_TEXT_MAX       128
#define LEADER_NAME_MAX       32

/*
 * Canvases have to be redrawn wholesale, so unlike a screen built from labels
 * every band needs the current value of everything drawn in it. Keep one copy
 * of the lot and redraw the affected band.
 */
struct jjb_status {
    /* top band */
    struct zmk_endpoint_instance selected;
    enum zmk_transport preferred;
    bool profile_connected;
    bool profile_bonded;
    uint8_t central_batt;
    uint8_t peripheral_batt;
    bool have_peripheral;

    /* middle band */
    zmk_keymap_layer_index_t layer_index;
    const char *layer_label;
    bool leader_active;
    uint8_t leader_candidates;
    char leader_text[LEADER_TEXT_MAX];

    /* bottom band */
    uint8_t wpm;
};

static struct jjb_status status;
static lv_obj_t *band_top;
static lv_obj_t *band_middle;
static lv_obj_t *band_bottom;

static uint8_t cbuf_top[CANVAS_BUF_SIZE];
static uint8_t cbuf_middle[CANVAS_BUF_SIZE];
static uint8_t cbuf_bottom[CANVAS_BUF_SIZE];

/* ------------------------------------------------------------------ */
/* Band painting                                                       */
/* ------------------------------------------------------------------ */

static void draw_top(void) {
    if (band_top == NULL) {
        return;
    }

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(band_top, CANVAS_BACKGROUND, LV_OPA_COVER);

    char text[STATUS_MAX];
    enum zmk_transport transport = status.selected.transport;
    bool connected = transport != ZMK_TRANSPORT_NONE;

    /* When nothing is connected, show what it is reaching for instead. */
    if (!connected) {
        transport = status.preferred;
    }

    switch (transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), connected ? "USB" : "USB ...");
        break;
    case ZMK_TRANSPORT_BLE:
        if (!status.profile_bonded) {
            snprintf(text, sizeof(text), "BT%d open", zmk_ble_active_profile_index() + 1);
        } else {
            snprintf(text, sizeof(text), "BT%d %s", zmk_ble_active_profile_index() + 1,
                     status.profile_connected ? "ok" : "...");
        }
        break;
    default:
        snprintf(text, sizeof(text), "offline");
        break;
    }
    jjb_canvas_draw_text(band_top, 0, 8, CANVAS_SIZE, &dsc, text);

    /*
     * No "%" and no space between the halves: "L100 R100" is already at the
     * width limit for this font.
     */
    if (status.have_peripheral) {
        snprintf(text, sizeof(text), "L%d R%d", status.central_batt, status.peripheral_batt);
    } else {
        snprintf(text, sizeof(text), "L%d", status.central_batt);
    }
    jjb_canvas_draw_text(band_top, 0, 32, CANVAS_SIZE, &dsc, text);

    jjb_rotate_canvas(band_top);
}

static void draw_middle(void) {
    if (band_middle == NULL) {
        return;
    }

    lv_canvas_fill_bg(band_middle, CANVAS_BACKGROUND, LV_OPA_COVER);

    if (status.leader_active) {
        /* Smaller face: the candidate list needs the lines more than the size. */
        lv_draw_label_dsc_t dsc;
        jjb_init_label_dsc(&dsc, &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);
        jjb_canvas_draw_text(band_middle, 0, 2, CANVAS_SIZE, &dsc, status.leader_text);
    } else {
        lv_draw_label_dsc_t dsc;
        jjb_init_label_dsc(&dsc, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

        char text[LAYER_NAME_MAX];
        if (status.layer_label == NULL || strlen(status.layer_label) == 0) {
            snprintf(text, sizeof(text), "%i", status.layer_index);
        } else {
            snprintf(text, sizeof(text), "%s", status.layer_label);
        }
        jjb_canvas_draw_text(band_middle, 0, 20, CANVAS_SIZE, &dsc, text);
    }

    jjb_rotate_canvas(band_middle);
}

static void draw_bottom(void) {
    if (band_bottom == NULL) {
        return;
    }

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(band_bottom, CANVAS_BACKGROUND, LV_OPA_COVER);

    /*
     * Only BAND_BOTTOM_VISIBLE pixels of this band are on screen, so anything
     * drawn here has to sit right at the top of the canvas.
     */
    char text[STATUS_MAX];
    snprintf(text, sizeof(text), "%d wpm", status.wpm);
    jjb_canvas_draw_text(band_bottom, 0, 2, CANVAS_SIZE, &dsc, text);

    jjb_rotate_canvas(band_bottom);
}

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
    status.selected = state.selected;
    status.preferred = state.preferred;
    status.profile_connected = state.profile_connected;
    status.profile_bonded = state.profile_bonded;
    draw_top();
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

/* Peripheral level arrives by event; there is no polling accessor for it. */
static uint8_t peripheral_soc;
static bool peripheral_seen;

static void jjb_batt_update_cb(struct jjb_batt_state state) {
    status.central_batt = state.central;
    status.peripheral_batt = state.peripheral;
    status.have_peripheral = state.have_peripheral;
    draw_top();
}

static struct jjb_batt_state jjb_batt_get_state(const zmk_event_t *eh) {
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
/* Layer                                                               */
/* ------------------------------------------------------------------ */

struct jjb_layer_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void jjb_layer_update_cb(struct jjb_layer_state state) {
    status.layer_index = state.index;
    status.layer_label = state.label;
    draw_middle();
}

static struct jjb_layer_state jjb_layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct jjb_layer_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_layer_status, struct jjb_layer_state, jjb_layer_update_cb,
                            jjb_layer_get_state)
ZMK_SUBSCRIPTION(jjb_layer_status, zmk_layer_state_changed);

/* ------------------------------------------------------------------ */
/* WPM                                                                 */
/* ------------------------------------------------------------------ */

struct jjb_wpm_state {
    uint8_t wpm;
};

static void jjb_wpm_update_cb(struct jjb_wpm_state state) {
    status.wpm = state.wpm;
    draw_bottom();
}

static struct jjb_wpm_state jjb_wpm_get_state(const zmk_event_t *eh) {
    return (struct jjb_wpm_state){.wpm = zmk_wpm_get_state()};
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_wpm_status, struct jjb_wpm_state, jjb_wpm_update_cb,
                            jjb_wpm_get_state)
ZMK_SUBSCRIPTION(jjb_wpm_status, zmk_wpm_state_changed);

/* ------------------------------------------------------------------ */
/* Leader sequence                                                     */
/* ------------------------------------------------------------------ */

static void jjb_leader_update_cb(struct zmk_leader_state_changed state) {
    status.leader_active = state.active;
    status.leader_candidates = state.candidate_count;

    if (state.active) {
        int used = snprintf(status.leader_text, sizeof(status.leader_text), "LEADER");

        if (state.candidate_count == 0) {
            snprintf(status.leader_text, sizeof(status.leader_text), "LEADER\nno match");
        } else if (state.candidate_count > LEADER_LIST_THRESHOLD) {
            snprintf(status.leader_text, sizeof(status.leader_text), "LEADER\n%d options",
                     state.candidate_count);
        } else {
            for (uint8_t i = 0;
                 i < state.candidate_count && used < (int)sizeof(status.leader_text) - 1; i++) {
                const char *name = zmk_leader_candidate_name(i);
                if (name == NULL) {
                    break;
                }
                /* Copy before humanising: the fork hands back its own storage. */
                char pretty[LEADER_NAME_MAX];
                snprintf(pretty, sizeof(pretty), "%s", name);
                jjb_humanize(pretty);
                used += snprintf(status.leader_text + used, sizeof(status.leader_text) - used,
                                 "\n%s", pretty);
            }
        }
    }

    draw_middle();
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
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Band offsets are ZMK's nice_view values, which are known to land the
     * right way up on this panel. See canvas_util.h for what they mean.
     */
    band_top = lv_canvas_create(screen);
    lv_obj_align(band_top, LV_ALIGN_TOP_RIGHT, BAND_TOP_X, 0);
    lv_canvas_set_buffer(band_top, cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    band_middle = lv_canvas_create(screen);
    lv_obj_align(band_middle, LV_ALIGN_TOP_LEFT, BAND_MIDDLE_X, 0);
    lv_canvas_set_buffer(band_middle, cbuf_middle, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    band_bottom = lv_canvas_create(screen);
    lv_obj_align(band_bottom, LV_ALIGN_TOP_LEFT, BAND_BOTTOM_X, 0);
    lv_canvas_set_buffer(band_bottom, cbuf_bottom, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    /*
     * Each of these runs its callback once immediately, which paints the band
     * it owns -- so all three canvases must already exist by this point.
     */
    jjb_conn_status_init();
    jjb_batt_status_init();
    jjb_layer_status_init();
    jjb_wpm_status_init();
    jjb_leader_status_init();

    return screen;
}
