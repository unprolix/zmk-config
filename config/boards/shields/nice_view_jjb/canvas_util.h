/*
 * Drawing surface for a nice!view.
 *
 * The panel's framebuffer is 160x68, but it is mounted so the readable
 * orientation is the other way round: 68 wide by 160 tall. LVGL cannot rotate
 * a 1-bit display to compensate -- lv_draw_sw_rotate has no I1 path, so
 * lv_display_set_rotation silently does nothing -- which is why anything drawn
 * straight onto the screen comes out sideways.
 *
 * So everything is drawn upright into an off-screen L8 canvas (L8 being the
 * smallest format sw_rotate accepts), which is then rotated into a second
 * canvas of the framebuffer's shape, and that is what the panel shows.
 *
 * ZMK's own nice_view widget does something similar but rotates *in place*,
 * which forces its canvases to be square (68x68) and so chops the panel into
 * three bands, the last of which is mostly off-screen. Keeping a separate
 * destination costs one more buffer and removes that restriction entirely:
 * the drawing surface here is the whole panel, PANEL_W x PANEL_H, addressed
 * in ordinary upright coordinates.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

/* The panel as read, not as scanned out. */
#define PANEL_W 68
#define PANEL_H 160

/* L8 is the smallest format lv_draw_sw_rotate supports; I1 is not among them. */
#define CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8

#define PANEL_DRAW_BUF_SIZE                                                                        \
    LV_CANVAS_BUF_SIZE(PANEL_W, PANEL_H, LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT),             \
                       LV_DRAW_BUF_STRIDE_ALIGN)
#define PANEL_SHOW_BUF_SIZE                                                                        \
    LV_CANVAS_BUF_SIZE(PANEL_H, PANEL_W, LV_COLOR_FORMAT_GET_BPP(CANVAS_COLOR_FORMAT),             \
                       LV_DRAW_BUF_STRIDE_ALIGN)

#define CANVAS_BACKGROUND lv_color_white()
#define CANVAS_FOREGROUND lv_color_black()

/*
 * Build the pair. `draw` is where everything is rendered, upright and
 * PANEL_W x PANEL_H; `show` is PANEL_H x PANEL_W and is the one on screen.
 */
void jjb_panel_init(lv_obj_t *screen, lv_obj_t **draw, lv_obj_t **show, uint8_t *draw_buf,
                    uint8_t *show_buf);

/* Rotate everything drawn so far onto the visible canvas. Call after drawing. */
void jjb_panel_present(lv_obj_t *draw, lv_obj_t *show);

void jjb_init_label_dsc(lv_draw_label_dsc_t *label_dsc, const lv_font_t *font,
                        lv_text_align_t align);

/* Text laid out in a box `max_w` wide with its top-left at (x, y). */
void jjb_canvas_draw_text(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, lv_coord_t max_w,
                          lv_coord_t max_h, lv_draw_label_dsc_t *draw_dsc, const char *txt);

/*
 * Blit a 1-bit-per-pixel bitmap, MSB first, a set bit being ink. Writes the
 * canvas buffer directly rather than going through an LVGL image descriptor:
 * the source is already the panel's depth, so there is nothing to scale or
 * convert, and it avoids pulling in the image decoder. Clipped to the canvas.
 */
void jjb_draw_bitmap(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const uint8_t *bits,
                     lv_coord_t w, lv_coord_t h, lv_coord_t stride);

/*
 * Modifier glyphs.
 *
 * No font here has them: LVGL's LV_SYMBOL_* set is a FontAwesome subset with
 * no modifier keys, and Montserrat carries no U+2303 / U+21E7 / U+2325 /
 * U+2318 either, so one would have to be generated offline. Since this is a
 * canvas rather than a label, drawing them as line art is less work and less
 * flash.
 *
 * The four sit in fixed slots spanning the full width, so a given modifier is
 * always in the same place. Left and right are not distinguished.
 */
#define MOD_ICON_SLOTS  4
#define MOD_ICON_SLOT_W (PANEL_W / MOD_ICON_SLOTS) /* 17 */
#define MOD_ICON_SIZE   15

enum jjb_mod_icon {
    JJB_MOD_ICON_CTRL = 0,  /* the chevron */
    JJB_MOD_ICON_SHIFT = 1, /* the outlined up arrow */
    JJB_MOD_ICON_ALT = 2,   /* the option glyph */
    JJB_MOD_ICON_GUI = 3,   /* the looped square */
};

void jjb_draw_mod_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, enum jjb_mod_icon icon);

/*
 * Leader sequence names come from the devicetree node name, so they arrive as
 * home_address_web. Underscores read badly on a panel this narrow; swap them
 * for spaces, which also gives the label somewhere to wrap.
 */
void jjb_humanize(char *text);
