/*
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include "canvas_util.h"

void jjb_rotate_canvas(lv_obj_t *canvas) {
    uint8_t *buf = lv_canvas_get_draw_buf(canvas)->data;
    /*
     * Static rather than on the stack: CANVAS_BUF_SIZE is about 4.6KB and the
     * display work queue's stack is nowhere near large enough for that. Safe
     * because every caller runs on that one queue.
     */
    static uint8_t buf_copy[CANVAS_BUF_SIZE];
    memcpy(buf_copy, buf, sizeof(buf_copy));

    const uint32_t stride = lv_draw_buf_width_to_stride(CANVAS_SIZE, CANVAS_COLOR_FORMAT);
    lv_draw_sw_rotate(buf_copy, buf, CANVAS_SIZE, CANVAS_SIZE, stride, stride,
                      LV_DISPLAY_ROTATION_270, CANVAS_COLOR_FORMAT);
}

void jjb_init_label_dsc(lv_draw_label_dsc_t *label_dsc, const lv_font_t *font,
                        lv_text_align_t align) {
    lv_draw_label_dsc_init(label_dsc);
    label_dsc->color = CANVAS_FOREGROUND;
    label_dsc->font = font;
    label_dsc->align = align;
}

void jjb_canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                          lv_draw_label_dsc_t *draw_dsc, const char *txt) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->text = txt;
    lv_area_t coords = {x, y, x + max_w, y + CANVAS_SIZE};
    lv_draw_label(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void jjb_humanize(char *text) {
    for (char *c = text; *c != '\0'; c++) {
        if (*c == '_') {
            *c = ' ';
        }
    }
}
