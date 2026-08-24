/*
 * THIS IS THE FILE TO EDIT.
 *
 * Per-key lighting for the Rolio: which keys light, in what colour, and when.
 *
 * Only two things ever light the strip:
 *
 *   1. A modifier that is actually held lights the home-row key that carries
 *      it, in that modifier's signature colour. This is the whole reason the
 *      base layer is otherwise dark -- a home-row mod that fires when it
 *      should not is invisible until its key lights up.
 *
 *   2. A handful of layers light a named group of keys: WASD on gaming, the
 *      arrow cluster on navigation, and the far hand's arrow cluster on each
 *      of the two GUI+nav layers.
 *
 * Everything else stays dark, which costs nothing: the strip's power gate is
 * only raised when a pixel is actually lit.
 *
 * WHAT CROSSES THE SPLIT. A peripheral has no keymap and cannot work out which
 * layer is active or which modifiers are held, so the central sends it both --
 * but not the pixels. The tables in this file are compiled into BOTH halves,
 * so each half renders its own side from the same two bytes. That matters:
 * the relayed behaviour carries only eight bytes of parameters, which is
 * nowhere near enough for 23 per-key colours.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

#include <zmk/keys.h>

#include "rgbkey_map.h"

/*
 * Deliberately dim. Twenty-three emitters a side at full brightness is both
 * dazzling at arm's length and the difference between weeks of battery and
 * hours, on a keyboard that otherwise idles in single-digit milliamps.
 *
 * A held modifier is also drawn brighter than a layer's key group. The two
 * palettes already use disjoint hues, so this is belt and braces -- but it is
 * what makes a mod read as "held" rather than as part of the layer when it
 * lands on a key the layer has already lit, which on navigation is every
 * arrow key.
 */
/*
 * Full brightness, because on this board it is not optional.
 *
 * The keycaps are opaque and swallow most of the light: at the levels the
 * eyelash uses, lit keys were hard to find at all. Every placement decision in
 * this file was made at full, so turning it down changes what is legible, not
 * just how bright it is.
 *
 * It does cost real current -- two dozen emitters a side, lit for as long as a
 * layer is held -- so it wants revisiting for battery use, probably together
 * with lighting fewer keys rather than the same keys more dimly.
 */
#define RGBKEY_BRIGHT 1

#if RGBKEY_BRIGHT
#define RGBKEY_DIM 0xC0
#define RGBKEY_MID 0xFF
#else
#define RGBKEY_DIM 0x30
#define RGBKEY_MID 0x50
#endif

/*
 * Palette. Indices are what travel over the split, so both halves agree.
 *
 * The modifier signatures are kept identical to the eyelash's rgbzone, so a
 * held Ctrl is the same red on either keyboard. That scheme also removes the
 * collision this file used to work around by hand: with Shift on cyan and
 * Alt/GUI on purple/violet, no modifier shares a hue with a layer fill, and
 * the earlier yellow/magenta substitutes are no longer needed.
 */
enum rgbkey_colour {
    RK_OFF = 0,
    RK_DIM_WHITE,
    RK_DIM_GREEN,
    RK_DIM_RED,
    /* Modifier signatures, matching rgbzone's zone_hrm_* exactly. */
    RK_CTRL,
    RK_SHIFT,
    RK_ALT,
    RK_GUI,
    /* The rest of rgbzone's palette, for the layers copied across from it. */
    RK_RED,
    RK_GREEN,
    RK_BLUE,
    RK_YELLOW,
    RK_CYAN,
    RK_MAGENTA,
    RK_WHITE,
    RK_AMBER,
    RK_TEAL,
    RK_COUNT
};

static const uint8_t rgbkey_palette[RK_COUNT][3] = {
    [RK_OFF] = {0, 0, 0},
    [RK_DIM_WHITE] = {RGBKEY_DIM, RGBKEY_DIM, RGBKEY_DIM},
    [RK_DIM_GREEN] = {0, RGBKEY_DIM, 0},
    [RK_DIM_RED] = {RGBKEY_DIM, 0, 0},
    [RK_CTRL] = {RGBKEY_MID, 0, 0},                  /* red,    = ZC_RED */
    [RK_SHIFT] = {0, RGBKEY_MID, RGBKEY_MID},        /* cyan,   = ZC_CYAN */
    [RK_ALT] = {RGBKEY_DIM, 0, RGBKEY_MID},          /* purple, = ZC_PURPLE */
    /*
     * Violet is purple with less red, NOT purple with more blue. rgbzone gets
     * its separation by adding to the blue channel, which cannot work here:
     * at full brightness blue is already at maximum, so adding wrapped and left
     * violet byte-identical to purple -- two modifiers sharing a colour.
     */
    [RK_GUI] = {RGBKEY_DIM / 3, 0, RGBKEY_MID},      /* violet, = ZC_VIOLET */
    [RK_RED] = {RGBKEY_MID, 0, 0},
    [RK_GREEN] = {0, RGBKEY_MID, 0},
    [RK_BLUE] = {0, 0, RGBKEY_MID},
    [RK_YELLOW] = {RGBKEY_MID, RGBKEY_MID, 0},
    [RK_CYAN] = {0, RGBKEY_MID, RGBKEY_MID},
    [RK_MAGENTA] = {RGBKEY_MID, 0, RGBKEY_MID},
    [RK_WHITE] = {RGBKEY_MID, RGBKEY_MID, RGBKEY_MID},
    [RK_AMBER] = {RGBKEY_MID, RGBKEY_DIM / 2, 0},
    [RK_TEAL] = {0, RGBKEY_MID, RGBKEY_DIM},
};

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
 * What the active layer lights. Resolved on the central from the layer's
 * display-name -- by name rather than by index, so inserting a layer in the
 * keymap does not silently repaint every other one -- and sent to the far half
 * as a small number.
 */
enum rgbkey_scene {
    RKS_NONE = 0,
    RKS_GAMING,
    RKS_NAV,
    RKS_LGUI_NAV,
    RKS_RGUI_NAV,
    RKS_NUMPAD,
    RKS_SYMBOL,
    RKS_NUMERIC,
    RKS_FUNCTION,
    RKS_SUPERSCRIPT,
    RKS_LIGHTING,
    RKS_BLUETOOTH,
    RKS_SYSTEM,
    RKS_EXTRA,
    RKS_COUNT
};

BUILD_ASSERT(RKS_COUNT <= 0xFF, "Scene must fit in the byte the split link carries");

/*
 * A group of key positions lit in one colour. Positions are the 48 matrix
 * positions from config/rolio48.h; a group may name positions on either half,
 * since each half simply finds no LED for the other's.
 */
#define RGBKEY_POS_NONE 0xFF

/* The most colours any one layer uses; "lighting" needs six. */
#define RGBKEY_GROUPS 6

/*
 * A key list is terminated rather than counted or padded to a fixed width. A
 * fixed array would have to be as wide as the largest layer -- superscript
 * names thirty keys -- and every shorter row would pad with a value that has to
 * mean "nothing"; zero cannot, because zero is a real key position.
 *
 * RK_KEYS builds the list inline so a row still reads as one statement.
 */
#define RK_KEYS(...) ((const uint8_t[]){__VA_ARGS__, RGBKEY_POS_NONE})

struct rgbkey_group {
    enum rgbkey_colour colour;
    /* RGBKEY_POS_NONE terminated; NULL for an unused slot. */
    const uint8_t *positions;
};

struct rgbkey_layer {
    /* Matches the layer's display-name in config/rolio.keymap. */
    const char *name;
    enum rgbkey_scene scene;
    /*
     * Whether a held modifier should light its home-row key on this layer.
     * False on gaming, whose home row is a plain A S D F G -- lighting
     * "the LALT key" there would light S, which is a lie.
     */
    bool hrm;
    struct rgbkey_group groups[RGBKEY_GROUPS];
};

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

#define RGBKEY_LAYER_COUNT ARRAY_SIZE(rgbkey_layers)

/*
 * Home-row mods.
 *
 * A modifier lights the home-row key MIRRORING its own, on the opposite half:
 * hold Ctrl with the left index and the right index's home key lights.
 *
 * Lighting the modifier's own key is the obvious design and it cannot work --
 * the finger holding the key is sitting on top of the LED. The mirror keeps the
 * home-row position, which is what says WHICH modifier without needing the
 * colour, while putting the light under a key nothing is covering.
 *
 * The positions are the same eight either way, so a left modifier and its right
 * counterpart light each other's keys.
 *
 * Note this puts the light back on the home row, which is one of the rows the
 * keycaps swallow. If it is still hard to see with no finger on it, then the
 * obstruction is the cap rather than the hand, and the answer is the top row or
 * the thumbs -- neither of which can say which modifier by position alone.
 *
 * This reads whichever modifiers are actually held, so a modifier applied by
 * something other than a home-row key lights the same thing. That is
 * deliberate: the question being answered is "which modifier is live".
 */
struct rgbkey_hrm {
    zmk_mod_flags_t mod;
    enum rgbkey_colour colour;
    /* RGBKEY_POS_NONE terminated, exactly like a layer's key group. */
    const uint8_t *positions;
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
 * The wire format: a scene in the high byte, the live modifier flags in the
 * low one. Two bytes, against the eight a relayed behaviour can carry.
 */
static inline uint32_t rgbkey_pack(enum rgbkey_scene scene, zmk_mod_flags_t mods) {
    return ((uint32_t)scene << 8) | (uint32_t)mods;
}

static inline enum rgbkey_scene rgbkey_scene_of(uint32_t packed) {
    enum rgbkey_scene s = (enum rgbkey_scene)((packed >> 8) & 0xFF);
    return s < RKS_COUNT ? s : RKS_NONE;
}

static inline zmk_mod_flags_t rgbkey_mods_of(uint32_t packed) {
    return (zmk_mod_flags_t)(packed & 0xFF);
}

/* Light this half's keys from a packed scene-and-modifiers word. */
void rgbkey_apply(uint32_t packed);
