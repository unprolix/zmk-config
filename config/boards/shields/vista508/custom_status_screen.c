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
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/output_status.h>
#include <zmk/display/widgets/peripheral_status.h>
#include <zmk/display/widgets/wpm_status.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Track the panel size from devicetree so this follows any geometry change. */
#define SCREEN_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SCREEN_H DT_PROP(DT_CHOSEN(zephyr_display), height)

/* Leave room for the themed screen's own padding on each side. */
#define CONTENT_W  (SCREEN_W - 16)

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
static struct zmk_widget_battery_status battery_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
static struct zmk_widget_output_status output_status_widget;
#endif
#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
static struct zmk_widget_peripheral_status peripheral_status_widget;
#endif
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

    /* Top row: connection on the left, battery on the right. */
#if IS_ENABLED(CONFIG_ZMK_WIDGET_OUTPUT_STATUS)
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT, 0, 0);
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_PERIPHERAL_STATUS)
    zmk_widget_peripheral_status_init(&peripheral_status_widget, screen);
    lv_obj_align(zmk_widget_peripheral_status_obj(&peripheral_status_widget), LV_ALIGN_TOP_LEFT, 0,
                 0);
#endif

#if IS_ENABLED(CONFIG_ZMK_WIDGET_BATTERY_STATUS)
    zmk_widget_battery_status_init(&battery_status_widget, screen);
    lv_obj_align(zmk_widget_battery_status_obj(&battery_status_widget), LV_ALIGN_TOP_RIGHT, 0, 0);
#endif

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

    /* WPM along the bottom. */
#if IS_ENABLED(CONFIG_ZMK_WIDGET_WPM_STATUS)
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_t *wpm = zmk_widget_wpm_status_obj(&wpm_status_widget);
    lv_obj_set_width(wpm, CONTENT_W);
    lv_obj_set_style_text_align(wpm, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_align(wpm, LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

    return screen;
}
