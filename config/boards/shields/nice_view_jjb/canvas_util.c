/*
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>

#include "canvas_util.h"

void jjb_panel_init(lv_obj_t *screen, lv_obj_t **draw, lv_obj_t **show, uint8_t *draw_buf,
                    uint8_t *show_buf) {
    /*
     * The visible one is the framebuffer's own shape and sits at the origin.
     * The screen keeps the mono theme's background -- stripping the style to
     * drop the padding would take bg_opa with it and the panel would come up
     * blank -- so the padding is zeroed on its own.
     */
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(screen, 0, LV_PART_MAIN);

    *show = lv_canvas_create(screen);
    lv_canvas_set_buffer(*show, show_buf, PANEL_H, PANEL_W, CANVAS_COLOR_FORMAT);
    lv_obj_align(*show, LV_ALIGN_TOP_LEFT, 0, 0);

    /*
     * The drawing surface is never rendered by LVGL: it is an off-screen
     * scratch buffer that jjb_panel_present() rotates onto the visible one.
     * It still has to be an object, because that is the only way to get a
     * layer to draw text and lines into.
     */
    *draw = lv_canvas_create(screen);
    lv_canvas_set_buffer(*draw, draw_buf, PANEL_W, PANEL_H, CANVAS_COLOR_FORMAT);
    lv_obj_add_flag(*draw, LV_OBJ_FLAG_HIDDEN);
}

void jjb_panel_present(lv_obj_t *draw, lv_obj_t *show) {
    if (draw == NULL || show == NULL) {
        return;
    }

    const uint8_t *src = lv_canvas_get_draw_buf(draw)->data;
    uint8_t *dst = lv_canvas_get_draw_buf(show)->data;

    const uint32_t src_stride = lv_draw_buf_width_to_stride(PANEL_W, CANVAS_COLOR_FORMAT);
    const uint32_t dst_stride = lv_draw_buf_width_to_stride(PANEL_H, CANVAS_COLOR_FORMAT);

    /*
     * Source and destination are distinct buffers, so unlike an in-place
     * rotation this does not need them to be the same shape -- which is what
     * lets the drawing surface be the whole 68x160 panel.
     */
    lv_draw_sw_rotate(src, dst, PANEL_W, PANEL_H, src_stride, dst_stride, LV_DISPLAY_ROTATION_270,
                      CANVAS_COLOR_FORMAT);

    lv_obj_invalidate(show);
}

void jjb_init_label_dsc(lv_draw_label_dsc_t *label_dsc, const lv_font_t *font,
                        lv_text_align_t align) {
    lv_draw_label_dsc_init(label_dsc);
    label_dsc->color = CANVAS_FOREGROUND;
    label_dsc->font = font;
    label_dsc->align = align;
}

void jjb_canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                          lv_coord_t max_h, lv_draw_label_dsc_t *draw_dsc, const char *txt) {
    lv_layer_t layer;
    lv_canvas_init_layer(canvas, &layer);

    draw_dsc->text = txt;
    lv_area_t coords = {x, y, x + max_w, y + max_h};
    lv_draw_label(&layer, draw_dsc, &coords);

    lv_canvas_finish_layer(canvas, &layer);
}

void jjb_draw_bitmap(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const uint8_t *bits,
                     lv_coord_t w, lv_coord_t h, lv_coord_t stride) {
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(canvas);
    if (buf == NULL || bits == NULL) {
        return;
    }

    const uint32_t canvas_stride = lv_draw_buf_width_to_stride(PANEL_W, CANVAS_COLOR_FORMAT);
    const uint8_t ink = 0x00; /* L8: one byte per pixel, 0 being black */

    for (lv_coord_t row = 0; row < h; row++) {
        const lv_coord_t cy = y + row;
        if (cy < 0 || cy >= PANEL_H) {
            continue;
        }
        const uint8_t *src = bits + (size_t)row * stride;
        uint8_t *dst = buf->data + (size_t)cy * canvas_stride;

        for (lv_coord_t col = 0; col < w; col++) {
            const lv_coord_t cx = x + col;
            if (cx < 0 || cx >= PANEL_W) {
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
            {mid, y},           {x + s, y + s / 2}, {x + s - 3, y + s / 2},
            {x + s - 3, y + s}, {x + 3, y + s},     {x + 3, y + s / 2},
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

void jjb_humanize(char *text) {
    for (char *c = text; *c != '\0'; c++) {
        if (*c == '_') {
            *c = ' ';
        }
    }
}
