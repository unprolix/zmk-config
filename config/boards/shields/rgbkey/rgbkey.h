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
 * RGBKEY_DEBUG raises everything to full and adds the probe in
 * rgbkey_strip.c. The keycaps here are opaque, so ordinary working brightness
 * is hard to read at a glance; for diagnosis, legibility beats battery and
 * beats not being dazzled.
 *
 * Set back to 0 for daily use -- 23 emitters a side at full is both dazzling
 * and the difference between weeks of battery and hours.
 */
#define RGBKEY_DEBUG 1

#if RGBKEY_DEBUG
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
    [RK_GUI] = {RGBKEY_DIM, 0, RGBKEY_MID},          /* violet, = ZC_VIOLET */
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
 * COLUMNS.
 *
 * rgbzone can only address columns -- that is all the eyelash's hardware
 * offers -- so every layer it colours is stated as a colour per column. To
 * carry those layers across without inventing a per-key design for each, the
 * same idea is expressible here: a column is just its three keys.
 *
 * Index 0 is the INNERMOST column, matching rgbzone's ordering, so a row copied
 * from zone_palette.h keeps its shape. Thumbs are not part of a column; a layer
 * that wants one names it in a key group instead.
 */
#define RGBKEY_COLUMNS 6
#define RGBKEY_COLUMN_KEYS 3

/* From config/rolio48.h: top, middle and bottom of each column, inner to outer. */
static const uint8_t rgbkey_column_left[RGBKEY_COLUMNS][RGBKEY_COLUMN_KEYS] = {
    {5, 17, 29}, {4, 16, 28}, {3, 15, 27}, {2, 14, 26}, {1, 13, 25}, {0, 12, 24},
};

static const uint8_t rgbkey_column_right[RGBKEY_COLUMNS][RGBKEY_COLUMN_KEYS] = {
    {6, 18, 32}, {7, 19, 33}, {8, 20, 34}, {9, 21, 35}, {10, 22, 36}, {11, 23, 37},
};

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
#define RGBKEY_GROUP_MAX 6
#define RGBKEY_POS_NONE  0xFF

struct rgbkey_group {
    enum rgbkey_colour colour;
    uint8_t positions[RGBKEY_GROUP_MAX];
};

struct rgbkey_layer {
    /* Matches the layer's display-name in config/rolio.keymap. */
    const char *name;
    enum rgbkey_scene scene;
    /*
     * Whole-column washes, innermost column first, as copied from rgbzone.
     * Left blank by the layers that name individual keys below instead.
     */
    enum rgbkey_colour left[RGBKEY_COLUMNS];
    enum rgbkey_colour right[RGBKEY_COLUMNS];
    /*
     * Whether a held modifier should light its home-row key on this layer.
     * False on gaming, whose home row is a plain A S D F G -- lighting
     * "the LALT key" there would light S, which is a lie.
     */
    bool hrm;
    struct rgbkey_group groups[2];
};

/*
 * The arrow clusters, named once and reused. Derived from jjb.keymap:
 * NAVIGATION_LT_IN puts UP at LT2 and NAVIGATION_LM_IN puts LEFT/DOWN/RIGHT at
 * LM3/LM2/LM1, so UP sits directly above DOWN on both hands.
 */
#define RGBKEY_ARROWS_LEFT   3, 14, 15, 16, RGBKEY_POS_NONE, RGBKEY_POS_NONE
#define RGBKEY_ARROWS_RIGHT  8, 19, 20, 21, RGBKEY_POS_NONE, RGBKEY_POS_NONE

/* GAMING_LT_IN / GAMING_LM_IN: W at LT3, A S D at LM4 LM3 LM2. */
#define RGBKEY_WASD          2, 13, 14, 15, RGBKEY_POS_NONE, RGBKEY_POS_NONE

/*
 * THIS IS THE TABLE TO EDIT.
 *
 * Two ways to say what a layer looks like, and a row may use either:
 *
 *   left/right   whole columns, innermost first. This is rgbzone's vocabulary,
 *                and the rows carried over from it keep its colours so the two
 *                keyboards read alike.
 *   groups       individual keys. Only this hardware can do it, so it is used
 *                where naming the actual keys says more than washing a column:
 *                WASD, and the arrow clusters.
 *
 * A layer absent from here stays dark, which costs nothing -- the strip's gate
 * is only opened when a pixel is actually lit.
 *
 *                                inner -------------------------> outer
 */
static const struct rgbkey_layer rgbkey_layers[] = {
    /* Per-key, because this board can: the keys you actually press. */
    {"gaming", RKS_GAMING, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, false,
     {{RK_DIM_RED, {RGBKEY_WASD}}}},

    {"navigation", RKS_NAV, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, true,
     {{RK_DIM_GREEN, {RGBKEY_ARROWS_LEFT}}, {RK_DIM_GREEN, {RGBKEY_ARROWS_RIGHT}}}},

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
    {"LGUI+nav", RKS_LGUI_NAV, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, true,
     {{RK_DIM_WHITE, {RGBKEY_ARROWS_RIGHT}}}},
    {"RGUI+nav", RKS_RGUI_NAV, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, true,
     {{RK_DIM_WHITE, {RGBKEY_ARROWS_LEFT}}}},

    /*
     * Carried over from rgbzone, column for column. Where its rows are stated
     * relative to "the half holding the key", the holding half is fixed here:
     * NUMPAD and SYMBOL are entered from the left thumb on this board, so left
     * is the pressed side.
     */
    {"numpad", RKS_NUMPAD, {RK_OFF, RK_YELLOW, RK_OFF, RK_OFF, RK_OFF, RK_OFF},
     {RK_OFF, RK_OFF, RK_CYAN, RK_OFF, RK_OFF, RK_OFF}, true, {}},

    {"symbol", RKS_SYMBOL, {RK_OFF, RK_OFF, RK_BLUE, RK_OFF, RK_OFF, RK_OFF}, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF}, false, {}},

    /* Not sided: the outer column on both halves. */
    {"numeric", RKS_NUMERIC, {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_CYAN},
     {RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_OFF, RK_CYAN}, true, {}},

    {"function", RKS_FUNCTION, {RK_YELLOW, RK_YELLOW, RK_AMBER, RK_OFF, RK_OFF, RK_OFF},
     {RK_YELLOW, RK_YELLOW, RK_AMBER, RK_OFF, RK_OFF, RK_OFF}, false, {}},

    {"superscript", RKS_SUPERSCRIPT,
     {RK_TEAL, RK_TEAL, RK_TEAL, RK_TEAL, RK_TEAL, RK_OFF},
     {RK_TEAL, RK_TEAL, RK_TEAL, RK_TEAL, RK_TEAL, RK_OFF}, true, {}},

    /*
     * The one layer where the colours are the point rather than a code.
     * Named "lighting", not "rgb" -- it was renamed when the effect controls
     * became a brightness knob, and a row naming the old layer matches nothing
     * and simply stays dark.
     */
    {"lighting", RKS_LIGHTING,
     {RK_RED, RK_GREEN, RK_BLUE, RK_YELLOW, RK_CYAN, RK_MAGENTA},
     {RK_RED, RK_GREEN, RK_BLUE, RK_YELLOW, RK_CYAN, RK_MAGENTA},
     false, {}},

    {"bluetooth", RKS_BLUETOOTH, {RK_BLUE, RK_BLUE, RK_BLUE, RK_OFF, RK_OFF, RK_OFF},
     {RK_BLUE, RK_BLUE, RK_BLUE, RK_OFF, RK_OFF, RK_OFF}, false, {}},

    {"system", RKS_SYSTEM, {RK_WHITE, RK_WHITE, RK_DIM_WHITE, RK_OFF, RK_OFF, RK_OFF},
     {RK_WHITE, RK_WHITE, RK_DIM_WHITE, RK_OFF, RK_OFF, RK_OFF}, false, {}},

    /* Not in rgbzone; the sticky-modifier layer, so it borrows their colours. */
    {"extra", RKS_EXTRA, {RK_OFF, RK_CTRL, RK_SHIFT, RK_ALT, RK_GUI, RK_OFF},
     {RK_OFF, RK_CTRL, RK_SHIFT, RK_ALT, RK_GUI, RK_OFF}, false, {}},
};

#define RGBKEY_LAYER_COUNT ARRAY_SIZE(rgbkey_layers)

/*
 * Home-row mods.
 *
 * This reads whichever modifiers are actually held, so a modifier applied by
 * something other than its home-row key lights the same key. That is
 * deliberate: the question being answered is "which modifier is live", and the
 * home-row key is simply where the answer is drawn.
 *
 * Positions from jjb.keymap's QWERTY_LM_IN / QWERTY_RM_IN, which NUMERIC and
 * NAVIGATION repeat: LGUI LALT LSHIFT LCTRL running inward on the left, and
 * RCTRL RSHIFT RALT RGUI running outward on the right.
 */
struct rgbkey_hrm {
    zmk_mod_flags_t mod;
    uint8_t position;
    enum rgbkey_colour colour;
};

static const struct rgbkey_hrm rgbkey_hrms[] = {
    {MOD_LGUI, 13, RK_GUI},   /* LM4, the A key -- tap_hold_layer_lgui */
    {MOD_LALT, 14, RK_ALT},   /* LM3, S */
    {MOD_LSFT, 15, RK_SHIFT}, /* LM2, D */
    {MOD_LCTL, 16, RK_CTRL},  /* LM1, F */

    {MOD_RCTL, 19, RK_CTRL},  /* RM1, J */
    {MOD_RSFT, 20, RK_SHIFT}, /* RM2, K */
    {MOD_RALT, 21, RK_ALT},   /* RM3, L */
    {MOD_RGUI, 22, RK_GUI},   /* RM4, the ; key -- tap_hold_layer_rgui */
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
