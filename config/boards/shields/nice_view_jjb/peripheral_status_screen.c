/*
 * Peripheral-half status screen for the nice!view.
 *
 * The peripheral knows almost nothing: not the layer, not the endpoint, not
 * the central's battery. What it does know is its own charge and whether the
 * split link is up, which is exactly the pair worth showing when the right
 * half stops responding. ZMK's own widget renders that as LV_SYMBOL_WIFI plus
 * OK or CLOSE; there is room here for the words.
 *
 * Same rotated-canvas machinery as the central screen -- see canvas_util.h for
 * why a nice!view cannot simply be drawn on.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/display.h>
#include <zmk/display/status_screen.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

#include "canvas_util.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STATUS_MAX 24

struct jjb_periph_status {
    bool connected;
    uint8_t level;
};

static struct jjb_periph_status status;
static lv_obj_t *band_top;
static lv_obj_t *band_middle;

static uint8_t cbuf_top[CANVAS_BUF_SIZE];
static uint8_t cbuf_middle[CANVAS_BUF_SIZE];

static void draw_top(void) {
    if (band_top == NULL) {
        return;
    }

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_14, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(band_top, CANVAS_BACKGROUND, LV_OPA_COVER);
    jjb_canvas_draw_text(band_top, 0, 8, CANVAS_SIZE, &dsc, status.connected ? "linked" : "no link");
    jjb_rotate_canvas(band_top);
}

static void draw_middle(void) {
    if (band_middle == NULL) {
        return;
    }

    lv_draw_label_dsc_t dsc;
    jjb_init_label_dsc(&dsc, &lv_font_montserrat_18, LV_TEXT_ALIGN_CENTER);

    lv_canvas_fill_bg(band_middle, CANVAS_BACKGROUND, LV_OPA_COVER);

    /* The one number anybody walks over to this half to read. */
    char text[STATUS_MAX];
    snprintf(text, sizeof(text), "R%d%%", status.level);
    jjb_canvas_draw_text(band_middle, 0, 20, CANVAS_SIZE, &dsc, text);

    jjb_rotate_canvas(band_middle);
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
    draw_middle();
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

    /* Both callbacks paint on their first, immediate call -- canvases first. */
    jjb_link_status_init();
    jjb_periph_batt_status_init();

    return screen;
}
