/*
 * Small drawing helpers for the Vista508's status screen.
 *
 * The panel is 144x168 and, unlike a nice!view, needs no software rotation:
 * the vendor's ls0xxvcom driver fork rotates in its write path via the
 * `rotate-180` property, so everything here draws in ordinary upright
 * coordinates straight onto a canvas.
 *
 * That is the one thing the nice!view's canvas_util.c cannot be shared for --
 * its whole draw/show canvas pair exists to work around LVGL being unable to
 * rotate a 1-bit display (lv_draw_sw_rotate has no I1 path, so
 * lv_display_set_rotation silently does nothing). The pieces below are the
 * parts that ported: the 1bpp blitter, the line-art modifier glyphs and the
 * leader-name tidier. They are deliberately written against whatever canvas
 * they are handed rather than a compiled-in panel size.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

/* L8: one byte per pixel, which is what LVGL's canvas drawing paths expect. */
#define VISTA_CANVAS_COLOR_FORMAT LV_COLOR_FORMAT_L8

#define VISTA_CANVAS_BUF_SIZE(w, h)                                                                \
    LV_CANVAS_BUF_SIZE((w), (h), LV_COLOR_FORMAT_GET_BPP(VISTA_CANVAS_COLOR_FORMAT),               \
                       LV_DRAW_BUF_STRIDE_ALIGN)

/*
 * Light or dark, in one place.
 *
 * Inverting only the emblem is not an option: the status readouts sit in the
 * emblem's blank corners, which invert with it, so they have to flip too -- and
 * once the text is light, the screen behind it must be dark or the layer name
 * and the leader list, which have no emblem behind them, would be invisible.
 * So the whole screen follows this one switch, side margins included.
 *
 * Worth knowing before flipping it back and forth: this is a REFLECTIVE panel
 * with no backlight. Its light state is the one that reflects ambient light, so
 * dark-on-light is the orientation the hardware is built for, the way paper is.
 * A mostly-dark screen is legible but has less to work with. It costs no more
 * power either way -- a Sharp memory LCD holds each pixel's state, so the bill
 * is VCOM inversion and line writes, not how many pixels are ink.
 */
#define VISTA_INVERT 1

#if VISTA_INVERT
#define VISTA_CANVAS_BACKGROUND lv_color_black()
#define VISTA_CANVAS_FOREGROUND lv_color_white()
/* L8: one byte per pixel, 0xFF being white. */
#define VISTA_CANVAS_INK 0xFF
#else
#define VISTA_CANVAS_BACKGROUND lv_color_white()
#define VISTA_CANVAS_FOREGROUND lv_color_black()
/* L8: one byte per pixel, 0 being black. */
#define VISTA_CANVAS_INK 0x00
#endif

/*
 * Is an L8 pixel ink? True once it is nearer the ink value than the background,
 * which is the same threshold the 1-bit panel itself applies. Needed because
 * LVGL antialiases text, so a glyph arrives as a spread of greys rather than
 * the two values everything else here writes.
 */
#if VISTA_INVERT
#define VISTA_IS_INK(px) ((px) >= 0x80)
#else
#define VISTA_IS_INK(px) ((px) < 0x80)
#endif

/*
 * XOR the inked pixels of one canvas into another at (x, y).
 *
 * This is how the status readouts sit over the emblem without eating it. They
 * used to paint an opaque backing, which cost whatever art was underneath --
 * on the full-width circle-cube that was a visible bite out of the disc. XOR
 * costs nothing: where the emblem is ink the text comes out blank, and where
 * the emblem is blank the text comes out ink, so both survive.
 *
 * It has to be done in the buffer. LVGL 9 offers only NORMAL, ADDITIVE,
 * SUBTRACTIVE and MULTIPLY blend modes -- none of them is XOR on a 1-bit
 * panel -- so no arrangement of label styles can produce this.
 *
 * Both canvases must be VISTA_CANVAS_COLOR_FORMAT. Clipped to the destination.
 */
void vista_xor_canvas(lv_obj_t *dst, lv_coord_t x, lv_coord_t y, lv_obj_t *src);

/*
 * Blit a 1-bit-per-pixel bitmap, MSB first, a set bit being ink. Writes the
 * canvas buffer directly rather than going through an LVGL image descriptor:
 * the source is already the panel's depth, so there is nothing to scale or
 * convert, and it avoids pulling in the image decoder. Clipped to the canvas.
 */
void vista_draw_bitmap(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, const uint8_t *bits,
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
 * Each sits in a fixed slot, so a given modifier is always in the same place.
 * Left and right are not distinguished.
 */
/*
 * The four live in two groups of two, one in each bottom corner, rather than
 * as a single row of four in the bottom-left.
 *
 * A four-wide row is about 68px, and on a 144px panel that reserved half the
 * bottom edge for glyphs that are usually absent -- the circle-cube emblem
 * runs full width and lost a visible bite out of its lower left to the space
 * held for GUI. Two pairs cost half as much on either side, and they sit where
 * the emblem is already thinnest.
 */
#define VISTA_MOD_ICON_SIZE   13
#define VISTA_MOD_ICON_SLOTS  4
/* Slots per corner group. Two groups of two make up the four. */
#define VISTA_MOD_GROUP_SLOTS 2
#define VISTA_MOD_ICON_SLOT_W 17

/*
 * Stroke weight for the glyphs. A single pixel is what LVGL defaults to and it
 * reads as faint at this size on a reflective panel; 2 doubles the ink without
 * the strokes closing up the inside of the GUI square or the Shift arrow.
 *
 * At 3 the strokes spread about a pixel and a half either side of the path, so
 * the slot went to 17 to keep neighbouring glyphs from touching. A pair comes
 * to 34, comfortably inside what either bottom corner has free. Going further
 * would need the glyphs themselves made smaller, which defeats the point.
 */
#define VISTA_MOD_ICON_STROKE 3
#define VISTA_MOD_ROW_W (VISTA_MOD_GROUP_SLOTS * VISTA_MOD_ICON_SLOT_W)
/*
 * A stroke straddles the path it is drawn along, so half of it falls outside
 * the glyph's nominal box. Without this padding the top and bottom strokes are
 * clipped by the canvas edge and the glyphs look cut off rather than bold.
 */
#define VISTA_MOD_ROW_H (VISTA_MOD_ICON_SIZE + VISTA_MOD_ICON_STROKE)
#define VISTA_MOD_ICON_PAD (VISTA_MOD_ICON_STROKE / 2)

enum vista_mod_icon {
    VISTA_MOD_ICON_CTRL = 0,  /* the chevron */
    VISTA_MOD_ICON_SHIFT = 1, /* the outlined up arrow */
    VISTA_MOD_ICON_ALT = 2,   /* the option glyph */
    VISTA_MOD_ICON_GUI = 3,   /* the looped square */
};

void vista_draw_mod_icon(lv_obj_t *canvas, lv_coord_t x, lv_coord_t y, enum vista_mod_icon icon);

/*
 * Leader sequence names come from the devicetree node name, so they arrive as
 * home_address_web. Underscores read badly on a panel this narrow; swap them
 * for spaces, which also gives the label somewhere to wrap.
 */
void vista_humanize(char *text);
