/*
 * Central-half status screen for the nice!view.
 *
 * The widgets are the ones worked out on the Rolio's vista508 screen --
 * connection as words rather than glyphs, both halves' battery -- but that
 * screen's implementation does not port: a nice!view cannot be drawn on
 * directly (see canvas_util.h), so everything is rendered upright into an
 * off-screen 68x160 canvas and rotated onto the panel.
 *
 * Down the panel:
 *   connection, then both batteries
 *   the layer -- its name, or the hierophant emblem on the base layer
 *   held modifiers, or WPM when none are down
 *
 * The leader candidate list is NOT here; it is on the peripheral, which has a
 * whole idle panel for it, and this half relays the state across.
 *
 * 68 pixels of width is the binding constraint: Montserrat 14 fits roughly
 * nine characters, so the longer layer names wrap to two lines.
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
#include "hierophant_img.h"
#include "leader_relay.h"

/* The layer that gets an emblem instead of its name. */
#define HIEROPHANT_LAYER_NAME "hierophant"

/*
 * The layer that gets a profile list instead of its name. The name is the
 * least useful thing to show while standing on the layer whose entire job is
 * moving between bluetooth profiles.
 */
#define BLUETOOTH_LAYER_NAME "bluetooth"
#define PROFILE_ROW_H        14

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

/*
 * Relay state, owned by the event thread. Layer and leader share one relay
 * message; the leader half has to be remembered so a layer change can re-send
 * it, whereas the layer is read live (see jjb_leader_relay) because at boot
 * there has been no layer *change* to have cached.
 */
static struct zmk_leader_state_changed jjb_last_leader;
static bool jjb_layer_was_hierophant;

/*
 * Nothing tells the central that a peripheral has connected -- ZMK raises
 * zmk_split_peripheral_status_changed only on the peripheral itself -- and the
 * relay is otherwise sent only on change. So a peripheral that boots, or
 * reconnects, into an unchanged layer would show nothing until the next layer
 * change. Re-send on the peripheral's battery reports, which only arrive when
 * the link is up, and on a timer as a backstop -- short enough to be the boot
 * path in its own right, since the first tick after the display comes up may
 * still precede the split link.
 */
#define RELAY_HEARTBEAT_SECONDS 10

static lv_obj_t *panel_draw;
static lv_obj_t *panel_show;

static uint8_t draw_buf[PANEL_DRAW_BUF_SIZE];
static uint8_t show_buf[PANEL_SHOW_BUF_SIZE];

/*
 * Vertical layout of the one drawing surface, in upright panel coordinates.
 * The emblem is the tallest thing here at 91 pixels, so it sets the spacing:
 * it runs IMG_Y..IMG_Y+91, and the modifier row starts below that.
 */
#define ROW_STATUS  4
#define IMG_Y       38
#define ROW_LAYER   70
#define ROW_BOTTOM  143
#define TEXT_BOX_H  40

/* Indent for the bluetooth profile list; a list reads better than a column of
   centred lines of differing width. */
#define PROFILE_LIST_X 14


/* ------------------------------------------------------------------ */
/* Painting                                                            */
/* ------------------------------------------------------------------ */

/*
 * One surface, repainted whole. Canvases have no retained scene graph, so
 * there is nothing to update incrementally; redrawing the lot is simpler than
 * tracking which region changed, and at 68x160 it is cheap.
 */
static void draw_panel(void) {
    if (panel_draw == NULL) {
        return;
    }

    lv_canvas_fill_bg(panel_draw, CANVAS_BACKGROUND, LV_OPA_COVER);

    char text[STATUS_MAX];
    enum zmk_transport transport = status.selected.transport;
    bool connected = transport != ZMK_TRANSPORT_NONE;

    /* When nothing is connected, show what it is reaching for instead. */
    if (!connected) {
        transport = status.preferred;
    }

    /*
     * Kept to four characters or so: this shares one 68-pixel line with the
     * charge, and spelling the state out ("BT2 open") overflows it. The
     * ellipsis means reaching-but-not-connected, the star means unpaired.
     */
    switch (transport) {
    case ZMK_TRANSPORT_USB:
        snprintf(text, sizeof(text), connected ? "USB" : "USB.");
        break;
    case ZMK_TRANSPORT_BLE:
        if (!status.profile_bonded) {
            snprintf(text, sizeof(text), "BT%d*", zmk_ble_active_profile_index() + 1);
        } else {
            snprintf(text, sizeof(text), "BT%d%s", zmk_ble_active_profile_index() + 1,
                     status.profile_connected ? "" : ".");
        }
        break;
    default:
        snprintf(text, sizeof(text), "--");
        break;
    }

    lv_draw_label_dsc_t left;
    jjb_init_label_dsc(&left, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    jjb_canvas_draw_text(panel_draw, 0, ROW_STATUS, PANEL_W / 2, 20, &left, text);

    /* Own charge, right-aligned, laid out the same way as the peripheral's. */
    lv_draw_label_dsc_t right;
    jjb_init_label_dsc(&right, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
    snprintf(text, sizeof(text), "%d%%", status.central_batt);
    jjb_canvas_draw_text(panel_draw, PANEL_W / 2, ROW_STATUS, PANEL_W / 2, 20, &right, text);

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    /* The base layer gets its emblem rather than its name. */
    if (status.layer_label != NULL && strcmp(status.layer_label, HIEROPHANT_LAYER_NAME) == 0) {
        jjb_draw_bitmap(panel_draw, (PANEL_W - HIEROPHANT_W) / 2, IMG_Y, hierophant_bits,
                        HIEROPHANT_W, HIEROPHANT_H, HIEROPHANT_STRIDE);
    } else if (status.layer_label != NULL &&
               strcmp(status.layer_label, BLUETOOTH_LAYER_NAME) == 0) {
        /*
         * One line per profile, in the order the keys select them, so a line
         * and a key are the same thing. Spelled out rather than shown as
         * glyphs: five lines of a legend nobody remembers is worse than five
         * lines of small words, and there is room for the words.
         *
         * The columns say the same thing in colour while the layer is held --
         * this is for when the answer is wanted rather than glanced at.
         */
        lv_draw_label_dsc_t list;
        jjb_init_label_dsc(&list, &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT);

        int active = zmk_ble_active_profile_index();
        for (uint8_t i = 0; i < ZMK_BLE_PROFILE_COUNT; i++) {
            const char *state = zmk_ble_profile_is_connected(i) ? "live"
                                : !zmk_ble_profile_is_open(i)   ? "idle"
                                                                : "--";
            snprintf(text, sizeof(text), "%c%d %s", i == (uint8_t)active ? '>' : ' ', i + 1,
                     state);
            jjb_canvas_draw_text(panel_draw, PROFILE_LIST_X, ROW_LAYER + i * PROFILE_ROW_H,
                                 PANEL_W - PROFILE_LIST_X, PROFILE_ROW_H, &list, text);
        }
    } else {
        char name[LAYER_NAME_MAX];
        if (status.layer_label == NULL || strlen(status.layer_label) == 0) {
            snprintf(name, sizeof(name), "%i", status.layer_index);
        } else {
            snprintf(name, sizeof(name), "%s", status.layer_label);
        }
        jjb_canvas_draw_text(panel_draw, 0, ROW_LAYER, PANEL_W, TEXT_BOX_H, &dsc, name);
    }

    /*
     * Held modifiers take the last row when there are any: they are what you
     * want to see at the instant you are looking, whereas WPM is idle
     * curiosity. One slot per modifier, left and right folded together, each
     * always in the same place so position identifies it as much as the glyph.
     */
    if (status.mods != 0) {
        static const zmk_mod_flags_t slot_mods[MOD_ICON_SLOTS] = {
            [JJB_MOD_ICON_CTRL] = MOD_LCTL | MOD_RCTL,
            [JJB_MOD_ICON_SHIFT] = MOD_LSFT | MOD_RSFT,
            [JJB_MOD_ICON_ALT] = MOD_LALT | MOD_RALT,
            [JJB_MOD_ICON_GUI] = MOD_LGUI | MOD_RGUI,
        };

        const lv_coord_t inset = (MOD_ICON_SLOT_W - MOD_ICON_SIZE) / 2;
        for (uint8_t slot = 0; slot < MOD_ICON_SLOTS; slot++) {
            if (status.mods & slot_mods[slot]) {
                jjb_draw_mod_icon(panel_draw, slot * MOD_ICON_SLOT_W + inset, ROW_BOTTOM,
                                  (enum jjb_mod_icon)slot);
            }
        }
    } else {
        lv_draw_label_dsc_t small;
        jjb_init_label_dsc(&small, &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);
        snprintf(text, sizeof(text), "%d wpm", status.wpm);
        jjb_canvas_draw_text(panel_draw, 0, ROW_BOTTOM, PANEL_W, 18, &small, text);
    }

    jjb_panel_present(panel_draw, panel_show);
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
    draw_panel();
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
    draw_panel();
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
    draw_panel();
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
    draw_panel();
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
    draw_panel();
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
/* Read live rather than cached: at boot no layer change has happened yet. */
static bool jjb_layer_hierophant_now(void) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index));
    return name != NULL && strcmp(name, HIEROPHANT_LAYER_NAME) == 0;
}

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
                                           listed, jjb_layer_hierophant_now()),
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
        jjb_last_leader = *ev;
        jjb_leader_relay(*ev);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(jjb_leader_relay_mod, jjb_leader_relay_listener);
ZMK_SUBSCRIPTION(jjb_leader_relay_mod, zmk_leader_state_changed);

/*
 * The peripheral wants the hierophant emblem too, but it has no keymap and so
 * no idea which layer is active. Layer state therefore rides the same relay,
 * which means re-sending on layer changes as well -- with whatever the leader
 * state currently is, so a layer change mid-sequence does not blank the list.
 *
 * Also a plain listener, for the same reason as above: this runs on the event
 * thread and must not touch LVGL.
 */
static int jjb_layer_relay_listener(const zmk_event_t *eh) {
    bool hierophant = jjb_layer_hierophant_now();

    /* Only the emblem crosses the link, so only a change in it is worth a write. */
    if (hierophant == jjb_layer_was_hierophant) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    jjb_layer_was_hierophant = hierophant;
    jjb_leader_relay(jjb_last_leader);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(jjb_layer_relay_mod, jjb_layer_relay_listener);
ZMK_SUBSCRIPTION(jjb_layer_relay_mod, zmk_layer_state_changed);

/*
 * The peripheral only reports its charge while connected, so these double as
 * proof the link is up -- and the first one after a connect is the earliest
 * moment a re-send can land.
 */
static int jjb_periph_alive_listener(const zmk_event_t *eh) {
    jjb_leader_relay(jjb_last_leader);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(jjb_periph_alive_mod, jjb_periph_alive_listener);
ZMK_SUBSCRIPTION(jjb_periph_alive_mod, zmk_peripheral_battery_state_changed);

static void jjb_relay_heartbeat_cb(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(jjb_relay_heartbeat, jjb_relay_heartbeat_cb);

static void jjb_relay_heartbeat_cb(struct k_work *work) {
    ARG_UNUSED(work);
    jjb_leader_relay(jjb_last_leader);
    k_work_reschedule(&jjb_relay_heartbeat, K_SECONDS(RELAY_HEARTBEAT_SECONDS));
}

/* ------------------------------------------------------------------ */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    jjb_panel_init(screen, &panel_draw, &panel_show, draw_buf, show_buf);

    /*
     * Each of these runs its callback once immediately, and every callback
     * repaints the whole surface -- so both canvases must already exist.
     */
    jjb_conn_status_init();
    jjb_batt_status_init();
    jjb_layer_status_init();
    jjb_wpm_status_init();
    jjb_mods_status_init();

    jjb_layer_was_hierophant = jjb_layer_hierophant_now();
    k_work_reschedule(&jjb_relay_heartbeat, K_SECONDS(2));

    return screen;
}
