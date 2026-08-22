/*
 * Peripheral-half status screen for the nice!view.
 *
 * On its own the peripheral knows almost nothing: not the layer, not the
 * endpoint, not the central's battery. Only its own charge and whether the
 * split link is up -- which is the pair worth showing when this half stops
 * responding, and all this screen used to show.
 *
 * It now also shows the leader sequence the *central* is running. The leader
 * behaviour is central-only, so that state arrives over the split link as a
 * relayed behaviour invocation (see leader_relay.h), carrying sequence indices
 * rather than names; the names come from the devicetree table the zmk-leader-key
 * fork compiles into both halves.
 *
 * Putting the list here rather than on the central is the point of the
 * exercise: this panel is otherwise idle, so the candidates get all three
 * bands, and the central's layer name never has to be displaced.
 *
 * Same rotated-canvas machinery as the central screen -- see canvas_util.h for
 * why a nice!view cannot simply be drawn on.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk-leader-key/leader_state.h>

#include "canvas_util.h"
#include "leader_relay.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_MAX      24
#define LEADER_TEXT_MAX 160
#define LEADER_NAME_MAX 32

/*
 * With well over a hundred sequences defined, listing them the instant leader
 * is pressed would be noise. The relay only sends indices when the candidate
 * set is down to LEADER_RELAY_MAX_INDICES, so beyond that all this half knows
 * is the count -- which is the right thing to show anyway.
 */

struct jjb_periph_status {
    bool connected;
    uint8_t level;

    bool leader_active;
    char leader_text[LEADER_TEXT_MAX];
};

static struct jjb_periph_status status;
static lv_obj_t *band_top;
static lv_obj_t *band_middle;
static lv_obj_t *band_bottom;

static uint8_t cbuf_top[CANVAS_BUF_SIZE];
static uint8_t cbuf_middle[CANVAS_BUF_SIZE];
static uint8_t cbuf_bottom[CANVAS_BUF_SIZE];

static void draw_top(void) {
    if (band_top == NULL) {
        return;
    }

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(band_top, CANVAS_BACKGROUND, LV_OPA_COVER);

    /* While a sequence is running this band names what is going on instead. */
    if (status.leader_active) {
        jjb_canvas_draw_text(band_top, 0, 8, CANVAS_SIZE, &dsc, "LEADER");
    } else {
        jjb_canvas_draw_text(band_top, 0, 8, CANVAS_SIZE, &dsc,
                             status.connected ? "linked" : "no link");
    }

    jjb_rotate_canvas(band_top);
}

/*
 * Middle and bottom together are the candidate list: 68 pixels plus the 24
 * that are on screen of the last band. Drawing one label across both would be
 * simpler but the bands are separate canvases, so the text is split between
 * them -- middle takes as much as it holds, bottom takes the remainder.
 */
static void draw_leader_bands(void) {
    if (band_middle == NULL || band_bottom == NULL) {
        return;
    }

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_12, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(band_middle, CANVAS_BACKGROUND, LV_OPA_COVER);
    lv_canvas_fill_bg(band_bottom, CANVAS_BACKGROUND, LV_OPA_COVER);

    if (status.leader_active) {
        jjb_canvas_draw_text(band_middle, 0, 2, CANVAS_SIZE, &dsc, status.leader_text);
    } else {
        /* Idle: the one number anybody walks over to this half to read. */
        lv_draw_label_dsc_t big;
        jjb_init_label_dsc(&big, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

        char text[STATUS_MAX];
        snprintf(text, sizeof(text), "R%d%%", status.level);
        jjb_canvas_draw_text(band_middle, 0, 20, CANVAS_SIZE, &big, text);
    }

    jjb_rotate_canvas(band_middle);
    jjb_rotate_canvas(band_bottom);
}

/* ------------------------------------------------------------------ */
/* Leader state, arriving over the split link                          */
/* ------------------------------------------------------------------ */

/* Written by the split thread, consumed by the display work queue. */
static atomic_t relay_param1;
static atomic_t relay_param2;

static void jjb_leader_apply(uint32_t param1, uint32_t param2) {
    status.leader_active = leader_relay_active(param1);

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

    draw_top();
    draw_leader_bands();
}

static void jjb_leader_work_cb(struct k_work *work) {
    ARG_UNUSED(work);
    jjb_leader_apply((uint32_t)atomic_get(&relay_param1), (uint32_t)atomic_get(&relay_param2));
}

static K_WORK_DEFINE(jjb_leader_work, jjb_leader_work_cb);

/*
 * Called from the behaviour that the central relayed, which runs on the split
 * thread -- NOT the display work queue. LVGL is not thread-safe and ZMK drives
 * the panel from its own queue, so nothing here may touch LVGL directly;
 * stash the two words and let the display thread do the drawing. Formatting
 * happens over there too, so the text buffer is only ever touched by one
 * thread.
 */
void jjb_leader_relay_received(uint32_t param1, uint32_t param2) {
    atomic_set(&relay_param1, (atomic_val_t)param1);
    atomic_set(&relay_param2, (atomic_val_t)param2);

    if (!zmk_display_is_initialized()) {
        return;
    }
    k_work_submit_to_queue(zmk_display_work_q(), &jjb_leader_work);
}

/* ------------------------------------------------------------------ */
/* Split link                                                          */
/* ------------------------------------------------------------------ */

struct jjb_link_state {
    bool connected;
};

static void jjb_link_update_cb(struct jjb_link_state state) {
    status.connected = state.connected;
    draw_top();
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
    draw_leader_bands();
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
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    band_top = lv_canvas_create(screen);
    lv_obj_align(band_top, LV_ALIGN_TOP_RIGHT, BAND_TOP_X, 0);
    lv_canvas_set_buffer(band_top, cbuf_top, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    band_middle = lv_canvas_create(screen);
    lv_obj_align(band_middle, LV_ALIGN_TOP_LEFT, BAND_MIDDLE_X, 0);
    lv_canvas_set_buffer(band_middle, cbuf_middle, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    band_bottom = lv_canvas_create(screen);
    lv_obj_align(band_bottom, LV_ALIGN_TOP_LEFT, BAND_BOTTOM_X, 0);
    lv_canvas_set_buffer(band_bottom, cbuf_bottom, CANVAS_SIZE, CANVAS_SIZE, CANVAS_COLOR_FORMAT);

    /* Both callbacks paint on their first, immediate call -- canvases first. */
    jjb_link_status_init();
    jjb_periph_batt_status_init();

    return screen;
}
