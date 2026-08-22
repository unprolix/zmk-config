/*
 * Peripheral-half status screen for the nice!view.
 *
 * On its own the peripheral knows almost nothing: not the layer, not the
 * endpoint, not the central's battery. Only its own charge and whether the
 * split link is up.
 *
 * Everything else arrives over the split link as a relayed behaviour
 * invocation (see leader_relay.h): the leader sequence the central is running,
 * carried as sequence indices rather than names -- those come from the
 * devicetree table the zmk-leader-key fork compiles into both halves -- plus
 * one bit saying whether the base layer is active, since this half cannot work
 * that out for itself.
 *
 * Putting the candidate list here rather than on the central is the point of
 * the exercise: this panel is otherwise idle, so the list gets the full height
 * and the central's layer display never has to be displaced.
 *
 * Status is compressed into a single top row -- link on the left, charge on
 * the right -- leaving everything below it free.
 *
 * Same drawing surface as the central screen; see canvas_util.h for why a
 * nice!view cannot simply be drawn on.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk-leader-key/leader_state.h>

#include "canvas_util.h"
#include "hierophant_img.h"
#include "leader_relay.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_MAX      24
#define LEADER_TEXT_MAX 224
#define LEADER_NAME_MAX 32

/* Upright panel coordinates; the emblem sits where the central's does. */
#define ROW_STATUS 4
#define ROW_LEADER 30
#define IMG_Y      50

struct jjb_periph_status {
    bool connected;
    uint8_t level;

    bool leader_active;
    bool hierophant;
    char leader_text[LEADER_TEXT_MAX];
};

static struct jjb_periph_status status;
static lv_obj_t *panel_draw;
static lv_obj_t *panel_show;

static uint8_t draw_buf[PANEL_DRAW_BUF_SIZE];
static uint8_t show_buf[PANEL_SHOW_BUF_SIZE];

static void draw_panel(void) {
    if (panel_draw == NULL) {
        return;
    }

    lv_canvas_fill_bg(panel_draw, CANVAS_BACKGROUND, LV_OPA_COVER);

    /*
     * Status row: the link as a glyph hard left, the charge right-aligned.
     * LV_SYMBOL_* live in the FontAwesome range LVGL builds into its Montserrat
     * faces, so this costs no extra font.
     */
    lv_draw_label_dsc_t left;
    jjb_init_label_dsc(&left, &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    jjb_canvas_draw_text(panel_draw, 0, ROW_STATUS, PANEL_W / 2, 20, &left,
                         status.connected ? LV_SYMBOL_WIFI : LV_SYMBOL_CLOSE);

    lv_draw_label_dsc_t right;
    jjb_init_label_dsc(&right, &lv_font_montserrat_14, LV_TEXT_ALIGN_RIGHT);
    char batt[STATUS_MAX];
    snprintf(batt, sizeof(batt), "%d%%", status.level);
    jjb_canvas_draw_text(panel_draw, PANEL_W / 2, ROW_STATUS, PANEL_W / 2, 20, &right, batt);

    /*
     * A leader sequence takes the rest of the panel; otherwise the base
     * layer's emblem does, when the central says that layer is active.
     */
    if (status.leader_active) {
        lv_draw_label_dsc_t dsc;
        jjb_init_label_dsc(&dsc, &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);
        jjb_canvas_draw_text(panel_draw, 0, ROW_LEADER, PANEL_W, PANEL_H - ROW_LEADER, &dsc,
                             status.leader_text);
    } else if (status.hierophant) {
        jjb_draw_bitmap(panel_draw, (PANEL_W - HIEROPHANT_W) / 2, IMG_Y, hierophant_bits,
                        HIEROPHANT_W, HIEROPHANT_H, HIEROPHANT_STRIDE);
    }

    jjb_panel_present(panel_draw, panel_show);
}

/* ------------------------------------------------------------------ */
/* State arriving over the split link                                  */
/* ------------------------------------------------------------------ */

/* Written by the split thread, consumed by the display work queue. */
static atomic_t relay_param1;
static atomic_t relay_param2;

static void jjb_relay_apply(uint32_t param1, uint32_t param2) {
    status.leader_active = leader_relay_active(param1);
    status.hierophant = leader_relay_hierophant(param1);

    if (!status.leader_active) {
        status.leader_text[0] = '\0';
    } else {
        uint8_t count = leader_relay_count(param1);
        uint8_t listed = leader_relay_listed(param1);

        if (count == 0) {
            snprintf(status.leader_text, sizeof(status.leader_text), "no match");
        } else if (listed == 0) {
            /* Too many to name; the central did not send indices. */
            snprintf(status.leader_text, sizeof(status.leader_text), "%d options", count);
        } else {
            int used = 0;
            status.leader_text[0] = '\0';
            for (uint8_t i = 0; i < listed && used < (int)sizeof(status.leader_text) - 1; i++) {
                uint8_t seq = leader_relay_index(param2, i);
                if (seq == LEADER_RELAY_INDEX_NONE) {
                    continue;
                }
                const char *name = zmk_leader_sequence_name(seq);
                if (name == NULL) {
                    continue;
                }
                char pretty[LEADER_NAME_MAX];
                snprintf(pretty, sizeof(pretty), "%s", name);
                jjb_humanize(pretty);
                used += snprintf(status.leader_text + used, sizeof(status.leader_text) - used,
                                 "%s%s", used > 0 ? "\n" : "", pretty);
            }
        }
    }

    draw_panel();
}

static void jjb_relay_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    jjb_relay_apply((uint32_t)atomic_get(&relay_param1), (uint32_t)atomic_get(&relay_param2));
}

static K_WORK_DEFINE(jjb_relay_work, jjb_relay_work_cb);

/*
 * Called from the behaviour the central relayed, which runs on the split
 * thread -- NOT the display work queue. LVGL is not thread-safe and ZMK drives
 * the panel from its own queue, so nothing here may touch LVGL directly: stash
 * the two words and let the display thread draw. Formatting happens over there
 * too, so the text buffer is only ever touched by one thread.
 */
void jjb_leader_relay_received(uint32_t param1, uint32_t param2) {
    atomic_set(&relay_param1, (atomic_val_t)param1);
    atomic_set(&relay_param2, (atomic_val_t)param2);

    if (!zmk_display_is_initialized()) {
        return;
    }
    k_work_submit_to_queue(zmk_display_work_q(), &jjb_relay_work);
}

/* ------------------------------------------------------------------ */
/* Split link                                                          */
/* ------------------------------------------------------------------ */

struct jjb_link_state {
    bool connected;
};

static void jjb_link_update_cb(struct jjb_link_state state) {
    status.connected = state.connected;
    draw_panel();
}

static struct jjb_link_state jjb_link_get_state(const zmk_event_t *eh) {
    return (struct jjb_link_state){.connected = zmk_split_bt_peripheral_is_connected()};
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_link_status, struct jjb_link_state, jjb_link_update_cb,
                            jjb_link_get_state)
ZMK_SUBSCRIPTION(jjb_link_status, zmk_split_peripheral_status_changed);

/* ------------------------------------------------------------------ */
/* Own battery                                                         */
/* ------------------------------------------------------------------ */

struct jjb_periph_batt_state {
    uint8_t level;
};

static void jjb_periph_batt_update_cb(struct jjb_periph_batt_state state) {
    status.level = state.level;
    draw_panel();
}

static struct jjb_periph_batt_state jjb_periph_batt_get_state(const zmk_event_t *eh) {
    return (struct jjb_periph_batt_state){.level = zmk_battery_state_of_charge()};
}

ZMK_DISPLAY_WIDGET_LISTENER(jjb_periph_batt_status, struct jjb_periph_batt_state,
                            jjb_periph_batt_update_cb, jjb_periph_batt_get_state)
ZMK_SUBSCRIPTION(jjb_periph_batt_status, zmk_battery_state_changed);

/* ------------------------------------------------------------------ */

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    jjb_panel_init(screen, &panel_draw, &panel_show, draw_buf, show_buf);

    /* Both callbacks paint on their first, immediate call -- canvases first. */
    jjb_link_status_init();
    jjb_periph_batt_status_init();

    return screen;
}
