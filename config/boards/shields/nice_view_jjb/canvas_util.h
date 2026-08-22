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

/*
 * Modifier glyphs.
 *
 * There is no font here that has them: LVGL's LV_SYMBOL_* set is a FontAwesome
 * subset with no modifier keys in it, and Montserrat carries no U+21E7 / U+2303
 * / U+2325 / U+2318 either. Adding a font with them would mean generating one
 * offline. Since these bands are canvases rather than labels, it is less work
 * and less flash to draw the four glyphs as line art.
 *
 * Drawn into a MOD_ICON_SIZE box with its top-left at (x, y).
 *
 * The four sit in fixed slots spanning the full 68px, so a given modifier is
 * always in the same place and can be recognised by position as much as by
 * shape. Left and right are not distinguished: which hand a mod came from is
 * not worth a second row of glyphs, and marking it under every icon just adds
 * clutter.
 */
#define MOD_ICON_SLOTS 4
#define MOD_ICON_SLOT_W (CANVAS_SIZE / MOD_ICON_SLOTS) /* 17 */
#define MOD_ICON_SIZE  15

enum jjb_mod_icon {
    JJB_MOD_ICON_CTRL = 0,  /* the chevron */
    JJB_MOD_ICON_SHIFT = 1, /* the outlined up arrow */
    JJB_MOD_ICON_ALT = 2,   /* the option glyph */
    JJB_MOD_ICON_GUI = 3,   /* the looped square */
};

void jjb_draw_mod_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, enum jjb_mod_icon icon);

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
