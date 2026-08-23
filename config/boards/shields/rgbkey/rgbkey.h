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
     {{RK_DIM_GREEN, RK_KEYS(RGBKEY_ARROWS_LEFT, RGBKEY_ARROWS_RIGHT)}}},

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
     {{RK_YELLOW, RK_KEYS(RK_L_C1)}, {RK_CYAN, RK_KEYS(RK_R_C2)}}},

    {"symbol", RKS_SYMBOL, false, {{RK_BLUE, RK_KEYS(RK_L_C2)}}},

    {"numeric", RKS_NUMERIC, true, {{RK_CYAN, RK_KEYS(RK_BOTH_C5)}}},

    {"function", RKS_FUNCTION, false,
     {{RK_YELLOW, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1)}, {RK_AMBER, RK_KEYS(RK_BOTH_C2)}}},

    {"superscript", RKS_SUPERSCRIPT, true,
     {{RK_TEAL, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1, RK_BOTH_C2, RK_BOTH_C3, RK_BOTH_C4)}}},

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
      {RK_MAGENTA, RK_KEYS(RK_BOTH_C5)}}},

    {"bluetooth", RKS_BLUETOOTH, false,
     {{RK_BLUE, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1, RK_BOTH_C2)}}},

    {"system", RKS_SYSTEM, false,
     {{RK_WHITE, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1)}, {RK_DIM_WHITE, RK_KEYS(RK_BOTH_C2)}}},

    /*
     * Not in rgbzone. The sticky-modifier layer, so each column wears the
     * colour of the modifier it applies -- the same signatures the home-row
     * overlay uses.
     */
    {"extra", RKS_EXTRA, false,
     {{RK_CTRL, RK_KEYS(RK_BOTH_C1)},
      {RK_SHIFT, RK_KEYS(RK_BOTH_C2)},
      {RK_ALT, RK_KEYS(RK_BOTH_C3)},
      {RK_GUI, RK_KEYS(RK_BOTH_C4)}}},
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
