/*
 * Status screen for the Vista508, laid out for its 144x168 portrait panel.
 *
 * ZMK's built-in screen targets a nice!view (160x68 landscape) and pins each
 * widget to a corner, which on a narrow portrait panel truncates the layer name
 * ("hierophant" came out as "hierophan..."). This lays the same widgets out
 * vertically instead and lets the layer name wrap.
 *
 * Written against LVGL 9 rather than forward-porting the vendor's screen, whose
 * ~200 LVGL 8 call sites and 1.5MB art blob buy artwork we do not need.
 *
 * Widgets are reused from ZMK as-is; all four are plain labels, which is what
 * makes the wrapping and alignment below possible.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/display/status_screen.h>
#include <zmk/display.h>
#include <zmk/battery.h>
#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk-leader-key/leader_state.h>
#include <zmk/display/widgets/wpm_status.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/hid.h>

#include "hierophant_img.h"
#include "vista_canvas.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Track the panel size from devicetree so this follows any geometry change. */
#define SCREEN_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCREEN_H DT_PROP(DT_CHOSEN(zephyr_display), height)

/* Leave room for the themed screen's own padding on each side. */
#define CONTENT_W  (SCREEN_W - 16)
#define ROW_H      22
#define STATUS_MAX 24

/*
 * The emblem is full height, and the status readouts sit in the four corners it
 * leaves blank -- only the narrow spear reaches the top and bottom edges, so
 * each corner has roughly CORNER_W x CORNER_H free. See hierophant_img.h.
 *
 * That is why the corners use a smaller face than the middle of the screen: at
 * the default 20px, "BT2 ok" alone overruns the gap.
 */
#define CORNER_W 60
#define CORNER_H 18
#define CORNER_FONT (&lv_font_montserrat_14)

/*
 * Connection and battery are drawn here rather than with ZMK's own widgets.
 * Those render LVGL glyphs -- WIFI/USB/OK/CLOSE/SETTINGS -- which are compact
 * enough for a nice!view but cryptic, and this panel has room for words.
 * The state comes from the same APIs ZMK's widgets use.
 */
static lv_obj_t *conn_label;
static lv_obj_t *batt_label;
static lv_obj_t *leader_label;

/*
 * The base layer gets an emblem instead of its name, and held modifiers get a
 * row of line-art glyphs. Both are canvases rather than labels: the emblem is
 * a 1bpp bitmap, and the glyphs exist in no font available here.
 *
 * Their buffers are static rather than drawn from LVGL's pool, which is sized
 * for the widgets (CONFIG_LV_Z_MEM_POOL_SIZE); the emblem alone is larger than
 * a nice!view's entire screen.
 */
static lv_obj_t *emblem_canvas;
static lv_obj_t *mods_canvas;

static uint8_t emblem_buf[VISTA_CANVAS_BUF_SIZE(HIEROPHANT_W, HIEROPHANT_H)];
static uint8_t mods_buf[VISTA_CANVAS_BUF_SIZE(VISTA_MOD_ROW_W, VISTA_MOD_ROW_H)];

/* The layer that gets an emblem instead of its name. */
#define EMBLEM_LAYER_NAME "hierophant"

/* Peripheral level arrives by event; there is no polling accessor for it. */
static uint8_t peripheral_soc;
static bool peripheral_seen;
/*
 * Local layer-name widget.
 *
 * ZMK's own layer_status widget formats into `char text[14]` as
 * LV_SYMBOL_KEYBOARD " %s". LV_SYMBOL_KEYBOARD is three UTF-8 bytes, so with
 * the space that leaves ten bytes for the name plus a terminator -- and
 * "hierophant" is exactly ten, so snprintf drops its last character. Any layer
 * name of ten or more characters loses its tail. This one has room to spare and
 * omits the symbol, which buys back four more columns.
 */
#define LAYER_NAME_MAX 32

static lv_obj_t *layer_label;

struct rolio_layer_state {
    zmk_keymap_layer_index_t index;
    const char *label;
};

static void rolio_layer_update_cb(struct rolio_layer_state state) {
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

    /*
     * The base layer is where the keyboard sits almost all the time, so it
     * shows the emblem rather than repeating a name that never changes. The
     * two share a band and are never both visible.
     */
    bool emblem = state.label != NULL && strcmp(state.label, EMBLEM_LAYER_NAME) == 0;
    if (emblem_canvas != NULL) {
        if (emblem) {
            lv_obj_remove_flag(emblem_canvas, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(emblem_canvas, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (emblem) {
        lv_obj_add_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static struct rolio_layer_state rolio_layer_get_state(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t index = zmk_keymap_highest_layer_active();
    return (struct rolio_layer_state){
        .index = index, .label = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(index))};
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_layer_status, struct rolio_layer_state, rolio_layer_update_cb,
                            rolio_layer_get_state)
ZMK_SUBSCRIPTION(rolio_layer_status, zmk_layer_state_changed);
#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

/* ------------------------------------------------------------------ */
/* Connection                                                          */
/* ------------------------------------------------------------------ */

struct rolio_conn_state {
    struct zmk_endpoint_instance selected;
    enum zmk_transport preferred;
    bool profile_connected;
    bool profile_bonded;
};

static void rolio_conn_update_cb(struct rolio_conn_state state) {
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
            snprintf(text, sizeof(text), "BT%d unpaired",
                     zmk_ble_active_profile_index() + 1);
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

static struct rolio_conn_state rolio_conn_get_state(const zmk_event_t *eh) {
    return (struct rolio_conn_state){
        .selected = zmk_endpoint_get_selected(),
        .preferred = zmk_endpoint_get_preferred_transport(),
        .profile_connected = zmk_ble_active_profile_is_connected(),
        .profile_bonded = !zmk_ble_active_profile_is_open(),
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_conn_status, struct rolio_conn_state, rolio_conn_update_cb,
                            rolio_conn_get_state)
ZMK_SUBSCRIPTION(rolio_conn_status, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(rolio_conn_status, zmk_ble_active_profile_changed);

/* ------------------------------------------------------------------ */
/* Battery, both halves                                                */
/* ------------------------------------------------------------------ */

struct rolio_batt_state {
    uint8_t central;
    uint8_t peripheral;
    bool have_peripheral;
};

static void rolio_batt_update_cb(struct rolio_batt_state state) {
    if (batt_label == NULL) {
        return;
    }

    char text[STATUS_MAX];

    /*
     * No percent signs: this now lives in a 60px corner beside the emblem's
     * spear, and "L88% R91%" overruns it. The two numbers are self-evidently
     * percentages.
     */
    if (state.have_peripheral) {
        snprintf(text, sizeof(text), "L%d R%d", state.central, state.peripheral);
    } else {
        snprintf(text, sizeof(text), "L%d", state.central);
    }

    lv_label_set_text(batt_label, text);
}

static struct rolio_batt_state rolio_batt_get_state(const zmk_event_t *eh) {
    /*
     * The peripheral's level only ever arrives as an event, so latch it as it
     * goes past; there is no accessor to poll it back out of ZMK.
     */
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev != NULL) {
        peripheral_soc = ev->state_of_charge;
        peripheral_seen = true;
    }

    return (struct rolio_batt_state){
        .central = zmk_battery_state_of_charge(),
        .peripheral = peripheral_soc,
        .have_peripheral = peripheral_seen,
    };
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_batt_status, struct rolio_batt_state, rolio_batt_update_cb,
                            rolio_batt_get_state)
ZMK_SUBSCRIPTION(rolio_batt_status, zmk_battery_state_changed);
ZMK_SUBSCRIPTION(rolio_batt_status, zmk_peripheral_battery_state_changed);

/* ------------------------------------------------------------------ */
/* Held modifiers                                                      */
/* ------------------------------------------------------------------ */

/*
 * Shows which modifiers are live, which is what makes a home-row mod that
 * fired when it should not actually visible.
 *
 * Driven off keycode events rather than a modifier event: ZMK declares a
 * zmk_modifiers_state_changed type but never raises it, so the flags are read
 * back out of HID state instead. Going through ZMK_DISPLAY_WIDGET_LISTENER
 * matters -- it marshals the callback onto the display work queue, and LVGL
 * must not be touched from the event thread.
 */
struct rolio_mods_state {
    zmk_mod_flags_t mods;
};

static void rolio_mods_update_cb(struct rolio_mods_state state) {
    if (mods_canvas == NULL) {
        return;
    }

    lv_canvas_fill_bg(mods_canvas, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);

    static const zmk_mod_flags_t slot_mods[VISTA_MOD_ICON_SLOTS] = {
        [VISTA_MOD_ICON_CTRL] = MOD_LCTL | MOD_RCTL,
        [VISTA_MOD_ICON_SHIFT] = MOD_LSFT | MOD_RSFT,
        [VISTA_MOD_ICON_ALT] = MOD_LALT | MOD_RALT,
        [VISTA_MOD_ICON_GUI] = MOD_LGUI | MOD_RGUI,
    };

    const lv_coord_t inset = (VISTA_MOD_ICON_SLOT_W - VISTA_MOD_ICON_SIZE) / 2;
    for (uint8_t slot = 0; slot < VISTA_MOD_ICON_SLOTS; slot++) {
        if (state.mods & slot_mods[slot]) {
            vista_draw_mod_icon(mods_canvas, slot * VISTA_MOD_ICON_SLOT_W + inset, 0,
                                (enum vista_mod_icon)slot);
        }
    }
}

static struct rolio_mods_state rolio_mods_get_state(const zmk_event_t *eh) {
    return (struct rolio_mods_state){.mods = zmk_hid_get_explicit_mods()};
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_mods_status, struct rolio_mods_state, rolio_mods_update_cb,
                            rolio_mods_get_state)
ZMK_SUBSCRIPTION(rolio_mods_status, zmk_keycode_state_changed);

/* ------------------------------------------------------------------ */
/* Leader sequence                                                     */
/* ------------------------------------------------------------------ */

/*
 * While a leader sequence is being entered, replace the layer name with what
 * is still reachable. With 116 sequences defined, listing them all the instant
 * leader is pressed would be noise, so nothing is listed until the candidate
 * set is small enough to be worth reading.
 */
#define LEADER_LIST_THRESHOLD 6
#define LEADER_TEXT_MAX       96
#define LEADER_NAME_MAX       32

static void rolio_leader_update_cb(struct zmk_leader_state_changed state) {
    if (leader_label == NULL || layer_label == NULL) {
        return;
    }

    if (!state.active) {
        /*
         * Hand the band back. Whether the layer name or the emblem should
         * reappear depends on the layer, so ask the layer widget rather than
         * guessing here.
         */
        lv_label_set_text(leader_label, "");
        rolio_layer_update_cb(rolio_layer_get_state(NULL));
        return;
    }

    lv_obj_add_flag(layer_label, LV_OBJ_FLAG_HIDDEN);
    if (emblem_canvas != NULL) {
        lv_obj_add_flag(emblem_canvas, LV_OBJ_FLAG_HIDDEN);
    }

    char text[LEADER_TEXT_MAX];
    int used = snprintf(text, sizeof(text), "LEADER");

    if (state.candidate_count == 0) {
        snprintf(text, sizeof(text), "LEADER\nno match");
    } else if (state.candidate_count > LEADER_LIST_THRESHOLD) {
        snprintf(text, sizeof(text), "LEADER\n%d options", state.candidate_count);
    } else {
        for (uint8_t i = 0; i < state.candidate_count && used < (int)sizeof(text) - 1; i++) {
            const char *name = zmk_leader_candidate_name(i);
            if (name == NULL) {
                break;
            }
            /*
             * Names come from the devicetree node, so they arrive as
             * home_address_web. Underscores read badly at this width and give
             * the label nowhere to wrap.
             */
            char pretty[LEADER_NAME_MAX];
            snprintf(pretty, sizeof(pretty), "%s", name);
            vista_humanize(pretty);
            used += snprintf(text + used, sizeof(text) - used, "\n%s", pretty);
        }
    }

    lv_label_set_text(leader_label, text);
}

static struct zmk_leader_state_changed rolio_leader_get_state(const zmk_event_t *eh) {
    const struct zmk_leader_state_changed *ev = as_zmk_leader_state_changed(eh);
    if (ev != NULL) {
        return *ev;
    }
    return (struct zmk_leader_state_changed){0};
}

ZMK_DISPLAY_WIDGET_LISTENER(rolio_leader_status, struct zmk_leader_state_changed,
                            rolio_leader_update_cb, rolio_leader_get_state)
ZMK_SUBSCRIPTION(rolio_leader_status, zmk_leader_state_changed);

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    /*
     * Deliberately NOT lv_obj_remove_style_all() here. That strips the mono
     * theme's background along with the padding, leaving bg_opa transparent so
     * the labels render with no contrast and the panel comes out blank. Keep
     * the themed screen exactly as ZMK's built-in one does and adjust only the
     * widgets below.
     */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /*
     * Follow vista_canvas.h's light/dark switch. The mono theme paints the
     * screen light with dark text; overriding both here rather than in each
     * widget means the labels inherit it, including ZMK's own WPM widget, which
     * this file does not construct and so cannot style individually.
     *
     * Setting only the background would leave dark text on a dark screen.
     */
    lv_obj_set_style_bg_color(screen, VISTA_CANVAS_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, VISTA_CANVAS_FOREGROUND, LV_PART_MAIN);

    /*
     * The emblem covers the whole panel, and the layer name and leader list
     * occupy the middle of it; exactly one of the three is visible at a time.
     * Only the narrow spear reaches the top and bottom edges, so the four
     * corners stay free for the status readouts -- see hierophant_img.h.
     *
     * Created FIRST, for two independent reasons. LVGL's z-order follows
     * creation order, and this is a full-panel object -- anything made before
     * it would be painted over, which is the whole corner layout. And
     * rolio_layer_status_init() runs the layer callback straight away, the
     * callback being what chooses between the emblem and the layer name; with
     * the canvas still NULL the base layer would show its name until the first
     * layer change.
     */
    emblem_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(emblem_canvas, emblem_buf, HIEROPHANT_W, HIEROPHANT_H,
                         VISTA_CANVAS_COLOR_FORMAT);
    lv_canvas_fill_bg(emblem_canvas, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);
    vista_draw_bitmap(emblem_canvas, 0, 0, hierophant_bits, HIEROPHANT_W, HIEROPHANT_H,
                      HIEROPHANT_STRIDE);
    lv_obj_align(emblem_canvas, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_add_flag(emblem_canvas, LV_OBJ_FLAG_HIDDEN);

    /*
     * Connection top-left, battery top-right, in the gaps either side of the
     * spear. These are created after the emblem so they draw over it: LVGL's
     * z-order follows creation order, and the emblem is a full-panel object.
     */
    conn_label = lv_label_create(screen);
    lv_obj_set_width(conn_label, CORNER_W);
    lv_obj_set_style_text_font(conn_label, CORNER_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_align(conn_label, LV_TEXT_ALIGN_LEFT, LV_PART_MAIN);
    lv_obj_align(conn_label, LV_ALIGN_TOP_LEFT, 0, 0);

    batt_label = lv_label_create(screen);
    lv_obj_set_width(batt_label, CORNER_W);
    lv_obj_set_style_text_font(batt_label, CORNER_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_align(batt_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(batt_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    rolio_conn_status_init();
    rolio_batt_status_init();

    /*
     * Layer name: the whole point of this screen. LV_LABEL_LONG_WRAP breaks on
     * word boundaries only, so a single long word like "hierophant" cannot wrap
     * and overflows instead -- at the default 20px font the trailing "t" was
     * clipped. Use the smaller face so the longest layer name fits on one line,
     * and keep WRAP as a fallback for multi-word names.
     */
    layer_label = lv_label_create(screen);
    lv_obj_set_width(layer_label, CONTENT_W);
    lv_label_set_long_mode(layer_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(layer_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(layer_label, LV_ALIGN_CENTER, 0, 0);

    rolio_layer_status_init();

    /* Occupies the same middle band as the layer name; only one shows at a time. */
    leader_label = lv_label_create(screen);
    lv_obj_set_width(leader_label, CONTENT_W);
    lv_label_set_long_mode(leader_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(leader_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(leader_label, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(leader_label, "");
    rolio_leader_status_init();

    /*
     * Modifier glyphs share the bottom line with WPM: the emblem takes the
     * whole middle band, so there is no room for a line of their own.
     */
    mods_canvas = lv_canvas_create(screen);
    lv_canvas_set_buffer(mods_canvas, mods_buf, VISTA_MOD_ROW_W, VISTA_MOD_ROW_H,
                         VISTA_CANVAS_COLOR_FORMAT);
    lv_canvas_fill_bg(mods_canvas, VISTA_CANVAS_BACKGROUND, LV_OPA_COVER);
    lv_obj_align(mods_canvas, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    rolio_mods_status_init();

    /* WPM along the bottom. */
#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_t *wpm = zmk_widget_wpm_status_obj(&wpm_status_widget);
    lv_obj_set_width(wpm, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(wpm, CORNER_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_align(wpm, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_obj_align(wpm, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
#endif

    return screen;
}
