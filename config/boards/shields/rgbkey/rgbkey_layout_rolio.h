/*
 * THIS IS THE FILE TO EDIT for the Rolio.
 *
 * Which keys light, in what colour, and when. Split out of rgbkey.h unchanged
 * when the Corne v4 gained per-key lighting too; the shared half of that file
 * still holds the palette, the enums and the wire format.
 *
 * Positions throughout are the 48 matrix positions from config/rolio48.h.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* Per-row `hrm` flags, as this board has always done. */
#define RGBKEY_HRM_ONLY_WHEN_LAYER_SILENT 0

/*
 * No holding-hand mechanism here: none of this board's rows are sided, so it
 * would be a retargeted &mo and a kilobyte of bookkeeping for a fact nothing
 * reads. Sided groups, were any added, would simply light on both halves.
 */
#define RGBKEY_HAS_LAYER_OWNER 0

/*
 * COLUMNS, as key lists.
 *
 * rgbzone can only address columns -- that is all the eyelash's hardware
 * offers -- so the layers copied from it are stated column by column. There is
 * no column mechanism here, though: a column IS its three keys, so these expand
 * into an ordinary key list and the renderer only ever knows about keys.
 *
 * That keeps one way of saying things instead of two, and it means any row
 * copied wholesale can be tuned key by key afterwards just by editing the list
 * -- which is the point of having per-key hardware.
 *
 * C0 is the INNERMOST column, matching rgbzone's ordering so a row copied from
 * zone_palette.h keeps its shape. Positions are from config/rolio48.h: top,
 * middle, bottom. Thumbs belong to no column and are named directly.
 */
/*
 * RGBKEY_TOP_ROW_ONLY collapses every column to just its top key.
 *
 * The keycaps on this board are opaque and block most of the light from below,
 * and the top row reads best -- it is the row that was legible during
 * calibration when the others needed looking for. Since a column is a macro
 * rather than a mechanism, moving every effect up is this one switch, and it
 * changes only how far each column expands.
 *
 * Rows that name keys directly (WASD, the arrow clusters) are unaffected: they
 * are deliberate per-key designs, not columns, so they stay where the keys are.
 */
#define RGBKEY_TOP_ROW_ONLY 0

#if RGBKEY_TOP_ROW_ONLY
#define RK_L_C0 5
#define RK_L_C1 4
#define RK_L_C2 3
#define RK_L_C3 2
#define RK_L_C4 1
#define RK_L_C5 0

#define RK_R_C0 6
#define RK_R_C1 7
#define RK_R_C2 8
#define RK_R_C3 9
#define RK_R_C4 10
#define RK_R_C5 11
#else
#define RK_L_C0 5, 17, 29
#define RK_L_C1 4, 16, 28
#define RK_L_C2 3, 15, 27
#define RK_L_C3 2, 14, 26
#define RK_L_C4 1, 13, 25
#define RK_L_C5 0, 12, 24

#define RK_R_C0 6, 18, 32
#define RK_R_C1 7, 19, 33
#define RK_R_C2 8, 20, 34
#define RK_R_C3 9, 21, 35
#define RK_R_C4 10, 22, 36
#define RK_R_C5 11, 23, 37
#endif

/*
 * THE TOP ROW AND THE THUMB ROW ARE THE ONES YOU CAN SEE.
 *
 * Measured on hardware: the keycaps swallow the middle rows almost entirely,
 * while the top row and the thumbs read clearly. Lighting a buried LED is not
 * harmful -- it just costs current and shows nothing -- so the way to make a
 * layer legible is to cover more of the visible surface, not to stop lighting
 * the rest.
 *
 * A column already contains its top key, so any column wash lights something
 * visible. The layers that were hard to see were simply the ones using very few
 * columns; those spend the whole top row instead.
 */
/*
 * A column's top two keys: the top row and the home row beneath it. Twice the
 * light of the top key alone, and on a modifier's mirrored column the second of
 * the two IS the opposite hand's own key for that modifier.
 */
#define RK_L_C0_TOP2 5, 17
#define RK_L_C1_TOP2 4, 16
#define RK_L_C2_TOP2 3, 15
#define RK_L_C3_TOP2 2, 14
#define RK_L_C4_TOP2 1, 13
#define RK_L_C5_TOP2 0, 12

#define RK_R_C0_TOP2 6, 18
#define RK_R_C1_TOP2 7, 19
#define RK_R_C2_TOP2 8, 20
#define RK_R_C3_TOP2 9, 21
#define RK_R_C4_TOP2 10, 22
#define RK_R_C5_TOP2 11, 23

/* A single column's top key, for when only the visible one is wanted. */
#define RK_L_C0_TOP 5
#define RK_L_C1_TOP 4
#define RK_L_C2_TOP 3
#define RK_L_C3_TOP 2
#define RK_L_C4_TOP 1
#define RK_L_C5_TOP 0

#define RK_R_C0_TOP 6
#define RK_R_C1_TOP 7
#define RK_R_C2_TOP 8
#define RK_R_C3_TOP 9
#define RK_R_C4_TOP 10
#define RK_R_C5_TOP 11

#define RK_L_TOP 0, 1, 2, 3, 4, 5
#define RK_R_TOP 6, 7, 8, 9, 10, 11
#define RK_BOTH_TOP RK_L_TOP, RK_R_TOP

/*
 * The bottom row: the two Sofle-style "sometimes" keys and the three thumbs.
 * It belongs to no column, and the roller push switch is deliberately absent --
 * it is the one key on each half with no LED of its own.
 *
 * Included because the keycaps swallow a lot of light and the limit on how
 * visible a layer is turns out to be how MANY keys it lights, not how brightly.
 * These five a side are the cheapest keys to add: nothing else was using them.
 */
#define RK_L_THUMB 38, 39, 40, 41, 42
#define RK_R_THUMB 43, 44, 45, 46, 47
#define RK_BOTH_THUMB RK_L_THUMB, RK_R_THUMB

/* The two outermost columns of a half, which several layers use as a block. */
#define RK_L_OUTER2 RK_L_C5, RK_L_C4
#define RK_R_OUTER2 RK_R_C5, RK_R_C4
#define RK_BOTH_OUTER2 RK_L_OUTER2, RK_R_OUTER2

/* Both halves' copy of a column, for the layers that are not sided. */
#define RK_BOTH_C0 RK_L_C0, RK_R_C0
#define RK_BOTH_C1 RK_L_C1, RK_R_C1
#define RK_BOTH_C2 RK_L_C2, RK_R_C2
#define RK_BOTH_C3 RK_L_C3, RK_R_C3
#define RK_BOTH_C4 RK_L_C4, RK_R_C4
#define RK_BOTH_C5 RK_L_C5, RK_R_C5


/*
 * The arrow clusters, named once and reused. Derived from jjb.keymap:
 * NAVIGATION_LT_IN puts UP at LT2 and NAVIGATION_LM_IN puts LEFT/DOWN/RIGHT at
 * LM3/LM2/LM1, so UP sits directly above DOWN on both hands.
 */
#define RGBKEY_ARROWS_LEFT  3, 14, 15, 16
#define RGBKEY_ARROWS_RIGHT 8, 19, 20, 21

/* GAMING_LT_IN / GAMING_LM_IN: W at LT3, A S D at LM4 LM3 LM2. */
#define RGBKEY_WASD 2, 13, 14, 15

/*
 * THIS IS THE TABLE TO EDIT.
 *
 * Each layer names groups of keys and the colour to paint them. Rows carried
 * over from the eyelash's rgbzone use the RK_*_C* column macros, since that is
 * the only vocabulary its hardware has; rows written for this board name the
 * keys directly. They are the same thing -- a column expands to its three keys
 * -- so a copied row can be tuned key by key just by editing its list.
 *
 * A layer absent from here stays dark, which costs nothing: the strip's gate is
 * only opened when a pixel is actually lit.
 */
static const struct rgbkey_layer rgbkey_layers[] = {
    /* Per-key, because this board can: the keys you actually press. */
    {"gaming", RKS_GAMING, false, {{RK_DIM_RED, RK_KEYS(RGBKEY_WASD)}}},

    {"navigation", RKS_NAV, true,
     {{RK_GREEN, RK_KEYS(RK_BOTH_OUTER2, RGBKEY_ARROWS_LEFT, RGBKEY_ARROWS_RIGHT)}}},

    /*
     * The GUI is held by the left hand on LGUI+nav and by the right on
     * RGUI+nav, so in each case it is the OTHER hand that carries the arrows --
     * which is also where jjb.keymap binds them (LGN_RM_IN is the right-hand
     * cluster, RGN_LM_IN the left).
     *
     * White on the far hand matches rgbzone, which lights the holding half's
     * pinky column violet and col 2 of the other half white. The violet half
     * comes free: the GUI being held is what put us on this layer, so the
     * modifier overlay already paints its key violet.
     */
    {"LGUI+nav", RKS_LGUI_NAV, true, {{RK_DIM_WHITE, RK_KEYS(RGBKEY_ARROWS_RIGHT)}}},
    {"RGUI+nav", RKS_RGUI_NAV, true, {{RK_DIM_WHITE, RK_KEYS(RGBKEY_ARROWS_LEFT)}}},

    /*
     * Carried over from rgbzone, column for column. Where its rows are stated
     * relative to "the half holding the key", the holding half is fixed here:
     * NUMPAD and SYMBOL are entered from the left thumb on this board.
     */
    {"numpad", RKS_NUMPAD, true,
     {{RK_YELLOW, RK_KEYS(RK_L_TOP, RK_L_C1, RK_L_THUMB)},
      {RK_CYAN, RK_KEYS(RK_R_TOP, RK_R_C2, RK_R_THUMB)}}},

    {"symbol", RKS_SYMBOL, false,
     {{RK_BLUE, RK_KEYS(RK_BOTH_TOP, RK_L_C2, RK_BOTH_THUMB)}}},

    {"numeric", RKS_NUMERIC, true,
     {{RK_CYAN, RK_KEYS(RK_BOTH_TOP, RK_BOTH_C5, RK_BOTH_THUMB)}}},

    /* The two outermost columns of each half, in red. */
    {"function", RKS_FUNCTION, false, {{RK_RED, RK_KEYS(RK_BOTH_OUTER2)}}},

    {"superscript", RKS_SUPERSCRIPT, true,
     {{RK_TEAL, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1, RK_BOTH_C2, RK_BOTH_C3, RK_BOTH_C4,
                        RK_BOTH_C5, RK_BOTH_THUMB)}}},

    /*
     * The one layer where the colours are the point rather than a code.
     * Named "lighting", not "rgb" -- it was renamed when the effect controls
     * became a brightness knob, and a row naming the old layer matches nothing
     * and simply stays dark.
     */
    {"lighting", RKS_LIGHTING, false,
     {{RK_RED, RK_KEYS(RK_BOTH_C0)},
      {RK_GREEN, RK_KEYS(RK_BOTH_C1)},
      {RK_BLUE, RK_KEYS(RK_BOTH_C2)},
      {RK_YELLOW, RK_KEYS(RK_BOTH_C3)},
      {RK_CYAN, RK_KEYS(RK_BOTH_C4)},
      {RK_MAGENTA, RK_KEYS(RK_BOTH_C5, RK_BOTH_THUMB)}}},

    {"bluetooth", RKS_BLUETOOTH, false,
     {{RK_BLUE, RK_KEYS(RK_BOTH_TOP, RK_BOTH_C0, RK_BOTH_C1, RK_BOTH_C2, RK_BOTH_THUMB)}}},

    {"system", RKS_SYSTEM, false,
     {{RK_WHITE, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1, RK_BOTH_C5, RK_BOTH_THUMB)},
      {RK_DIM_WHITE, RK_KEYS(RK_BOTH_C2)}}},

    /*
     * Not in rgbzone. The sticky-modifier layer, so each column wears the
     * colour of the modifier it applies -- the same signatures the home-row
     * overlay uses.
     */
    {"extra", RKS_EXTRA, false,
     {{RK_CTRL, RK_KEYS(RK_BOTH_C1)},
      {RK_SHIFT, RK_KEYS(RK_BOTH_C2)},
      {RK_ALT, RK_KEYS(RK_BOTH_C3)},
      {RK_GUI, RK_KEYS(RK_BOTH_C4)},
      /* No modifier of its own, so the spare keys just carry the layer. */
      {RK_DIM_WHITE, RK_KEYS(RK_BOTH_C0, RK_BOTH_C5, RK_BOTH_THUMB)}}},
};

/*
 * Each modifier lights the top TWO keys of its own column, on the opposite half.
 *
 * Two obstructions had to be dodged at once. The modifier's own key is under
 * the finger holding it, so the light goes to the mirroring column on the other
 * half; and the home row is one of the rows the keycaps swallow, so the top key
 * of that column carries most of the signal.
 *
 * Both are lit rather than just the top one, for twice the light -- and the
 * second of the two is the opposite hand's own key for that same modifier,
 * which is a tidy thing for it to be.
 *
 * Column still carries the identity -- outermost is GUI, then Alt, Shift, Ctrl
 * inward, the same order as the home row -- so the four are told apart by
 * position as well as by colour, and two held together are two separate lights.
 */
static const struct rgbkey_hrm rgbkey_hrms[] = {
    /* Left modifiers light the right half's matching column, and vice versa. */
    {MOD_LGUI, RK_GUI, RK_KEYS(RK_R_C4_TOP2)},
    {MOD_LALT, RK_ALT, RK_KEYS(RK_R_C3_TOP2)},
    {MOD_LSFT, RK_SHIFT, RK_KEYS(RK_R_C2_TOP2)},
    {MOD_LCTL, RK_CTRL, RK_KEYS(RK_R_C1_TOP2)},

    {MOD_RGUI, RK_GUI, RK_KEYS(RK_L_C4_TOP2)},
    {MOD_RALT, RK_ALT, RK_KEYS(RK_L_C3_TOP2)},
    {MOD_RSFT, RK_SHIFT, RK_KEYS(RK_L_C2_TOP2)},
    {MOD_RCTL, RK_CTRL, RK_KEYS(RK_L_C1_TOP2)},
};

/*
 * CAPS LOCK.
 *
 * Not a layer: the caps layer was retired, because a keyboard-side toggle drifts
 * from the host's real state the moment anything else presses CAPS. It comes
 * from the host's HID indicator instead, and is painted over everything else --
 * no layer's colours matter more than knowing every letter is about to be
 * capital.
 *
 * On the visible surface, so it cannot be the thing that gets swallowed: the
 * whole top row and the thumbs, both halves.
 */
#define RGBKEY_CAPS_COLOUR RK_YELLOW
#define RGBKEY_CAPS_KEYS   RK_KEYS(RK_BOTH_TOP, RK_BOTH_THUMB)
