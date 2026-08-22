/*
 * Canvas helpers for drawing on a nice!view.
 *
 * The panel's framebuffer is 160x68, but it is mounted so that the readable
 * orientation is the other way round: 68 wide by 160 tall. LVGL cannot rotate
 * a 1-bit display to compensate -- lv_draw_sw_rotate has no I1 path, so
 * lv_display_set_rotation silently does nothing -- which is why anything drawn
 * straight onto the screen comes out sideways.
 *
 * The way round it, which is what ZMK's own nice_view widget does, is to draw
 * into an off-screen L8 canvas (the smallest format sw_rotate supports),
 * rotate that, and let LVGL blit the result down to 1 bit. Canvases are square
 * so the rotation preserves their shape, and they tile along framebuffer-x --
 * which, after rotation, is the physical *vertical* axis.
 *
 * So: draw in ordinary upright coordinates within a band, 68 wide, and the
 * band lands somewhere down the length of the panel.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

/* Square, because rotation has to preserve the buffer's shape. */
#define CANVAS_SIZE 68

/* L8 is the smallest format lv_draw_sw_rotate supports; I1 is not among them. */
#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8
#define CANVAS_BUF_SIZE                                                                            \
    LV_CANVAS_BUF_SIZE(CANVAS_SIZE, CANVAS_SIZE, LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT),     \
                       LV_DRAW_BUF_STRIDE_ALIGN)

#define CANVAS_BACKGROUND lv_color_white()
#define CANVAS_FOREGROUND lv_color_black()

/*
 * Band offsets along framebuffer-x, i.e. down the panel once rotated. Taken
 * from ZMK's nice_view widget, which is known to land correctly on this
 * hardware: 160 pixels of length divided into three 68-pixel bands, so the
 * last one is clipped to 24 visible pixels.
 */
#define BAND_TOP_X    0   /* aligned TOP_RIGHT, so x = 92..160 */
#define BAND_MIDDLE_X 24  /* x = 24..92                        */
#define BAND_BOTTOM_X -44 /* x = -44..24, only 24px visible    */

#define BAND_BOTTOM_VISIBLE 24

void jjb_rotate_canvas(lv_obj_t *canvas);
void jjb_init_label_dsc(lv_draw_label_dsc_t *label_dsc, const lv_font_t *font, lv_text_align_t align);
void jjb_canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                          lv_draw_label_dsc_t *draw_dsc, const char *txt);

/*
 * Leader sequence names come from the devicetree node name, so they arrive as
 * home_address_web. Underscores read badly on a panel this narrow; swap them
 * for spaces, which also gives the label somewhere to wrap.
 */
void jjb_humanize(char *text);
