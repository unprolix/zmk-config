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
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/hid.h>
#include <zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>
#include <zmk/split/central.h>
#include <zmk/wpm.h>
#include <zmk-leader-key/leader_state.h>

#include <zmk/split/bluetooth/service.h>

#include "canvas_util.h"
#include "leader_relay.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * The BLE split payload carries the behaviour's name in a
 * ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN buffer -- nine bytes, not the sixteen that
 * struct zmk_split_transport_central_command advertises. A longer name is
 * silently strlcpy'd short, the peripheral then fails to find the truncated
 * name, and nothing happens: no error reaches the central, which still sees a
 * successful queue. "ldr_relay" was exactly nine characters and lost its "y".
 */
BUILD_ASSERT(sizeof("ldrelay") <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN,
             "Relay behaviour name is too long to survive the split payload intact");

#define STATUS_MAX     24
#define LAYER_NAME_MAX 32

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

    /* middle band -- the leader list lives on the peripheral, not here */
    zmk_keymap_layer_index_t layer_index;
    const char *layer_label;

    /* bottom band */
    uint8_t wpm;
    zmk_mod_flags_t mods;
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

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    char text[LAYER_NAME_MAX];

    if (status.layer_label == NULL || strlen(status.layer_label) == 0) {
        snprintf(text, sizeof(text), "%i", status.layer_index);
    } else {
        snprintf(text, sizeof(text), "%s", status.layer_label);
    }
    jjb_canvas_draw_text(band_middle, 0, 20, CANVAS_SIZE, &dsc, text);

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
     * drawn here has to sit right at the top of the canvas -- one line, and at
     * 68 pixels wide, about nine characters of it.
     *
     * Held modifiers take the line when there are any: they are what you want
     * to see at the instant you are looking, whereas WPM is idle curiosity.
     */
    if (status.mods != 0) {
        /*
         * One slot per modifier, left and right folded together, each slot in
         * the same place every time so position identifies the modifier as
         * much as the glyph does.
         */
        static const zmk_mod_flags_t slot_mods[MOD_ICON_SLOTS] = {
            [JJB_MOD_ICON_CTRL] = MOD_LCTL | MOD_RCTL,
            [JJB_MOD_ICON_SHIFT] = MOD_LSFT | MOD_RSFT,
            [JJB_MOD_ICON_ALT] = MOD_LALT | MOD_RALT,
            [JJB_MOD_ICON_GUI] = MOD_LGUI | MOD_RGUI,
        };

        const lv_coord_t inset = (MOD_ICON_SLOT_W - MOD_ICON_SIZE) / 2;
        for (uint8_t slot = 0; slot < MOD_ICON_SLOTS; slot++) {
            if (status.mods & slot_mods[slot]) {
                jjb_draw_mod_icon(band_bottom, slot * MOD_ICON_SLOT_W + inset, 3,
                                  (enum jjb_mod_icon)slot);
            }
        }
    } else {
        char text[STATUS_MAX];
        snprintf(text, sizeof(text), "%d wpm", status.wpm);
        jjb_canvas_draw_text(band_bottom, 0, 2, CANVAS_SIZE, &dsc, text);
    }

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
/* Held modifiers                                                      */
/* ------------------------------------------------------------------ */

/*
 * zmk_modifiers_state_changed exists as a type but nothing in ZMK ever raises
 * it, so there is no modifier event to subscribe to. Every modifier change
 * does come with a keycode event, though, and the live flags can just be read
 * back out of the HID state -- including the ones a home-row mod registered.
 */
struct jjb_mods_state {
    zmk_mod_flags_t mods;
};

static void jjb_mods_update_cb(struct jjb_mods_state state) {
    status.mods = state.mods;
    draw_bottom();
}

static struct jjb_mods_state jjb_mods_get_state(const zmk_event_t *eh) {
    return (struct jjb_mods_state){.mods = zmk_hid_get_explicit_mods()};
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_mods_status, struct jjb_mods_state, jjb_mods_update_cb,
                            jjb_mods_get_state)
ZMK_SUBSCRIPTION(jjb_mods_status, zmk_keycode_state_changed);

/* ------------------------------------------------------------------ */
/* Leader sequence                                                     */
/* ------------------------------------------------------------------ */

/*
 * The candidate list is shown on the *peripheral*, which has the whole panel
 * free for it, so this half only forwards the state and otherwise carries on
 * showing the layer name. Sending is what GLOBAL locality is for: ZMK relays
 * the invocation to every peripheral and then runs it locally, where the relay
 * behaviour ignores it.
 */
static void jjb_leader_relay(struct zmk_leader_state_changed state) {
    uint32_t param2 = 0;
    uint8_t listed = 0;

    if (state.active && state.candidate_count > 0 &&
        state.candidate_count <= LEADER_RELAY_MAX_INDICES) {
        for (uint8_t i = 0; i < state.candidate_count && i < LEADER_RELAY_MAX_INDICES; i++) {
            uint16_t seq = zmk_leader_candidate_sequence_index(i);
            /* One byte per index on the wire; anything larger cannot be named. */
            param2 = leader_relay_set_index(
                param2, i, seq > LEADER_RELAY_INDEX_MAX ? LEADER_RELAY_INDEX_NONE : (uint8_t)seq);
            listed++;
        }
    }

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(ldrelay)),
        .param1 = leader_relay_pack_param1(state.active, state.candidate_count, state.press_count,
                                           listed),
        .param2 = param2,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    /*
     * Sent straight down the split rather than through
     * zmk_behavior_invoke_binding(): GLOBAL locality would do the same thing,
     * but it returns the result of the *local* invocation and throws away the
     * send's, so a failed send looks like success. Calling the transport
     * directly surfaces the real error, and the local invocation was useless
     * here anyway -- this half reads leader state directly.
     *
     * Nothing is drawn from here. This runs on the event thread, and LVGL may
     * only be touched from ZMK's display work queue; drawing here raced the
     * display thread (and jjb_rotate_canvas's shared scratch buffer) and
     * crashed the board after a few sequences.
     */
    int err = zmk_split_central_invoke_behavior(0, &binding, event, true);
    if (err) {
        LOG_WRN("Failed to relay leader state to peripheral: %d", err);
    }
}

/*
 * A plain listener rather than ZMK_DISPLAY_WIDGET_LISTENER, deliberately.
 * Nothing here draws: this half's screen is unaffected by leader state now, so
 * there is no reason to hop onto the display work queue -- and good reason not
 * to, since invoking a behaviour from there would push a BLE write onto the
 * thread that repaints the panel. Running on the event thread keeps the
 * invocation where behaviour invocations normally come from.
 */
static int jjb_leader_relay_listener(const zmk_event_t *eh) {
    const struct zmk_leader_state_changed *ev = as_zmk_leader_state_changed(eh);
    if (ev != NULL) {
        jjb_leader_relay(*ev);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(jjb_leader_relay_mod, jjb_leader_relay_listener);
ZMK_SUBSCRIPTION(jjb_leader_relay_mod, zmk_leader_state_changed);

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
    jjb_mods_status_init();

    return screen;
}
