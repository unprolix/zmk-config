/*
 * Small drawing helpers for the Vista508's status screen. See vista_canvas.h
 * for why these are a copy of the nice!view's rather than shared with it.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include "vista_canvas.h"

/*
 * The canvas carries its own geometry, so clipping is taken from the buffer
 * header rather than from a compiled-in panel size. That is the difference
 * that lets the same code serve the emblem canvas and the modifier row, which
 * are nothing like each other in shape.
 */
void vista_draw_bitmap(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const uint8_t *bits,
                       lv_coord_t w, lv_coord_t h, lv_coord_t stride) {
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);
    if (buf == NULL || bits == NULL) {
        return;
    }

    const lv_coord_t canvas_w = (lv_coord_t)buf->header.w;
    const lv_coord_t canvas_h = (lv_coord_t)buf->header.h;
    const uint32_t canvas_stride =
        lv_draw_buf_width_to_stride(canvas_w, VISTA_CANVAS_COLOR_FORMAT);
    const uint8_t ink = VISTA_CANVAS_INK;

    for (lv_coord_t row = 0; row < h; row++) {
        const lv_coord_t cy = y + row;
        if (cy < 0 || cy >= canvas_h) {
            continue;
        }
        const uint8_t *src = bits + (size_t)row * stride;
        uint8_t *dst = buf->data + (size_t)cy * canvas_stride;

        for (lv_coord_t col = 0; col < w; col++) {
            const lv_coord_t cx = x + col;
            if (cx < 0 || cx >= canvas_w) {
                continue;
            }
            if (src[col >> 3] & (0x80 >> (col & 7))) {
                dst[cx] = ink;
            }
        }
    }
}

static void icon_line(lv_obj_t *canvas, const lv_point_t *points, uint32_t count) {
    lv_draw_line_dsc_t dsc;
    lv_draw_line_dsc_init(&dsc);
    dsc.color = VISTA_CANVAS_FOREGROUND;
    dsc.width = VISTA_MOD_ICON_STROKE;

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

void vista_draw_mod_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, enum vista_mod_icon icon) {
    const lv_coord_t s = VISTA_MOD_ICON_SIZE - 1;
    const lv_coord_t mid = x + s / 2;

    switch (icon) {
    case VISTA_MOD_ICON_SHIFT: {
        /* Outlined up arrow, as U+21E7. */
        const lv_point_t pts[] = {
            {mid, y},           {x + s, y + s / 2}, {x + s - 3, y + s / 2},
            {x + s - 3, y + s}, {x + 3, y + s},     {x + 3, y + s / 2},
            {x, y + s / 2},     {mid, y},
        };
        icon_line(canvas, pts, ARRAY_SIZE(pts));
        break;
    }
    case VISTA_MOD_ICON_CTRL: {
        /* Chevron, as U+2303. */
        const lv_point_t pts[] = {{x + 1, y + s - 3}, {mid, y + 2}, {x + s - 1, y + s - 3}};
        icon_line(canvas, pts, ARRAY_SIZE(pts));
        break;
    }
    case VISTA_MOD_ICON_ALT: {
        /* U+2325: a long stroke stepping up, and a short bar to its right. */
        const lv_point_t stroke[] = {
            {x, y + s}, {x + s / 3, y + s}, {x + s - 3, y + 1}, {x + s, y + 1}};
        icon_line(canvas, stroke, ARRAY_SIZE(stroke));
        const lv_point_t bar[] = {{x + s / 2, y + s}, {x + s, y + s}};
        icon_line(canvas, bar, ARRAY_SIZE(bar));
        break;
    }
    case VISTA_MOD_ICON_GUI: {
        /*
         * U+2318 has four interlocking loops, which at fifteen pixels would be
         * mush. A square with four stubs reads as the same thing at this size
         * and stays legible on a 1-bit panel.
         */
        const lv_point_t box[] = {{x + 3, y + 3},
                                  {x + s - 3, y + 3},
                                  {x + s - 3, y + s - 3},
                                  {x + 3, y + s - 3},
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

void vista_humanize(char *text) {
    for (char *c = text; *c != '\0'; c++) {
        if (*c == '_') {
            *c = ' ';
        }
    }
}
