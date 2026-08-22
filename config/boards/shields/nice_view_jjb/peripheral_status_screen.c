/*
 * Peripheral-half status screen for the nice!view's 160x68 landscape panel.
 *
 * The peripheral knows almost nothing: not the layer, not the endpoint, not
 * the central's battery. What it does know is its own charge and whether the
 * split link is up, which is exactly the pair worth showing when the right
 * half stops responding.
 *
 * ZMK's own peripheral_status widget shows this as LV_SYMBOL_WIFI plus OK or
 * CLOSE. That is compact enough for the stock canvas layout but tells you
 * nothing at a glance, and there is room here for the words instead.
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

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define SCREEN_W DT_PROP(DT_CHOSEN(zephyr_display), width)

#define STATUS_MAX 24
#define ROW_TOP_H  15

static lv_obj_t *link_label;
static lv_obj_t *batt_label;

/* ------------------------------------------------------------------ */
/* Split link                                                          */
/* ------------------------------------------------------------------ */

struct jjb_link_state {
    bool connected;
};

static void jjb_link_update_cb(struct jjb_link_state state) {
    if (link_label == NULL) {
        return;
    }

    lv_label_set_text(link_label, state.connected ? "linked" : "no link");
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
    if (batt_label == NULL) {
        return;
    }

    char text[STATUS_MAX];
    /* "R" because this is the right half on every board that builds it. */
    snprintf(text, sizeof(text), "R%d%%", state.level);
    lv_label_set_text(batt_label, text);
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

    /* Same reasoning as the central screen: keep the theme, drop the padding. */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    link_label = lv_label_create(screen);
    lv_obj_set_style_text_font(link_label, &lv_font_montserrat_12, LV_PART_MAIN);
    lv_obj_align(link_label, LV_ALIGN_TOP_LEFT, 0, 0);

    /* The one number anybody walks over to this half to read. */
    batt_label = lv_label_create(screen);
    lv_obj_set_width(batt_label, SCREEN_W);
    lv_obj_set_style_text_font(batt_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_set_style_text_align(batt_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(batt_label, LV_ALIGN_TOP_MID, 0, ROW_TOP_H + 12);

    jjb_link_status_init();
    jjb_periph_batt_status_init();

    return screen;
}
