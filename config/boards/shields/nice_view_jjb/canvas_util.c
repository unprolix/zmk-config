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

static void icon_line(lv_obj_t *canvas, const lv_point_t *points, uint32_t count) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = CANVAS_FOREGROUND;
    dsc.width = 1;

    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);
    for (uint32_t i = 1; i < count; i++) {
        dsc.p1.x = points[i - 1].x;
        dsc.p1.y = points[i - 1].y;
        dsc.p2.x = points[i].x;
        dsc.p2.y = points[i].y;
        lv_draw_line(&layer, &dsc);
    }
    lv_canvas_finish_layer(canvas, &layer);
}

void jjb_draw_mod_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, enum jjb_mod_icon icon) {
    const lv_coord_t s = MOD_ICON_SIZE - 1;
    const lv_coord_t mid = x + s / 2;

    switch (icon) {
    case JJB_MOD_ICON_SHIFT: {
        /* Outlined up arrow, as U+21E7. */
        const lv_point_t pts[] = {
            {mid, y},           {x + s, y + s / 2},     {x + s - 3, y + s / 2},
            {x + s - 3, y + s}, {x + 3, y + s},         {x + 3, y + s / 2},
            {x, y + s / 2},     {mid, y},
        };
        icon_line(canvas, pts, ARRAY_SIZE(pts));
        break;
    }
    case JJB_MOD_ICON_CTRL: {
        /* Chevron, as U+2303. */
        const lv_point_t pts[] = {{x + 1, y + s - 3}, {mid, y + 2}, {x + s - 1, y + s - 3}};
        icon_line(canvas, pts, ARRAY_SIZE(pts));
        break;
    }
    case JJB_MOD_ICON_ALT: {
        /* U+2325: a long stroke stepping up, and a short bar to its right. */
        const lv_point_t stroke[] = {
            {x, y + s}, {x + s / 3, y + s}, {x + s - 3, y + 1}, {x + s, y + 1}};
        icon_line(canvas, stroke, ARRAY_SIZE(stroke));
        const lv_point_t bar[] = {{x + s / 2, y + s}, {x + s, y + s}};
        icon_line(canvas, bar, ARRAY_SIZE(bar));
        break;
    }
    case JJB_MOD_ICON_GUI: {
        /*
         * U+2318 has four interlocking loops, which at thirteen pixels would
         * be mush. A square with four stubs reads as the same thing at this
         * size and stays legible on a 1-bit panel.
         */
        const lv_point_t box[] = {{x + 3, y + 3},     {x + s - 3, y + 3},
                                  {x + s - 3, y + s - 3}, {x + 3, y + s - 3},
                                  {x + 3, y + 3}};
        icon_line(canvas, box, ARRAY_SIZE(box));
        const lv_point_t tl[] = {{x, y}, {x + 3, y + 3}};
        const lv_point_t tr[] = {{x + s, y}, {x + s - 3, y + 3}};
        const lv_point_t bl[] = {{x, y + s}, {x + 3, y + s - 3}};
        const lv_point_t br[] = {{x + s, y + s}, {x + s - 3, y + s - 3}};
        icon_line(canvas, tl, 2);
        icon_line(canvas, tr, 2);
        icon_line(canvas, bl, 2);
        icon_line(canvas, br, 2);
        break;
    }
    }
}

void jjb_humanize(char *text) {
    for (char *c = text; *c != '\0'; c++) {
        if (*c == '_') {
            *c = ' ';
        }
    }
}
