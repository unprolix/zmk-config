/*
 * THIS IS THE FILE TO EDIT for the Corne v4.
 *
 * Which keys light, in what colour, and when. The shared half of rgbkey.h holds
 * the palette, the enums and the wire format; this file is the whole of what
 * makes the lighting this keyboard's rather than the Rolio's.
 *
 * POSITIONS ARE NUMBERED THE OTHER WAY ROUND FROM THE ROLIO'S. Both boards call
 * their innermost column C0, but they disagree about the raw numbers: the
 * Rolio's transform counts its top row inward, so LT0 -- the outer pinky -- is
 * position 5, while this board's counts outward, so LT0 is position 0. Every
 * literal below is in THIS board's numbering; a list copied across from
 * rgbkey_layout_rolio.h without renumbering will light plausible-looking
 * nonsense rather than fail.
 *
 * The 46 positions, in the order the transform in corne_v4.dtsi lays them out:
 *
 *      0..5   left top row, outer -> inner
 *      6      left ex2 upper (the push switch under the encoder)
 *      7      right ex2 upper
 *      8..13  right top row, inner -> outer
 *     14..19  left home row          20  left ex2 lower
 *     21      right ex2 lower    22..27  right home row
 *     28..33  left bottom row    34..39  right bottom row
 *     40..42  left thumbs        43..45  right thumbs
 *
 * Every key has an LED here -- 23 a side against 23 switches -- so unlike the
 * Rolio there is no key that simply cannot be lit.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/*
 * Held modifiers are shown only when the layer itself is dark, which is the
 * eyelash's rule (rgbzone_run.c: `if (!layer_speaks) overlay_mods(...)`) rather
 * than the Rolio's per-row `hrm` flag. The `hrm` field on each row below is
 * therefore ignored on this board; it is left in place because the struct is
 * shared with the Rolio's table.
 *
 * The rule earns itself on navigation: the arrow keys there are home-row mods,
 * so under the per-row rule merely resting on one repainted its column in that
 * modifier's colour and the cluster flickered under the hand.
 */
#define RGBKEY_HRM_ONLY_WHEN_LAYER_SILENT 1

/*
 * This board asks for the holding-hand mechanism (rgbkey.overlay defines
 * LAYER_OWNER_PRESENT, CMakeLists compiles rgbzone_owner.c), so sided groups
 * mean something here.
 */
#define RGBKEY_HAS_LAYER_OWNER 1

/*
 * COLUMNS, as key lists.
 *
 * Same vocabulary as the Rolio's layout so a row can be carried across by
 * changing nothing but the file it lives in: C0 is the INNERMOST column, and a
 * column IS its three keys, so these expand into ordinary key lists and the
 * renderer only ever knows about keys.
 */
#define RK_L_C0 5, 19, 33
#define RK_L_C1 4, 18, 32
#define RK_L_C2 3, 17, 31
#define RK_L_C3 2, 16, 30
#define RK_L_C4 1, 15, 29
#define RK_L_C5 0, 14, 28

#define RK_R_C0 8, 22, 34
#define RK_R_C1 9, 23, 35
#define RK_R_C2 10, 24, 36
#define RK_R_C3 11, 25, 37
#define RK_R_C4 12, 26, 38
#define RK_R_C5 13, 27, 39

/*
 * A column's top two keys: the top row and the home row beneath it. On a
 * modifier's mirrored column the second of the two IS the opposite hand's own
 * key for that modifier.
 */
#define RK_L_C0_TOP2 5, 19
#define RK_L_C1_TOP2 4, 18
#define RK_L_C2_TOP2 3, 17
#define RK_L_C3_TOP2 2, 16
#define RK_L_C4_TOP2 1, 15
#define RK_L_C5_TOP2 0, 14

#define RK_R_C0_TOP2 8, 22
#define RK_R_C1_TOP2 9, 23
#define RK_R_C2_TOP2 10, 24
#define RK_R_C3_TOP2 11, 25
#define RK_R_C4_TOP2 12, 26
#define RK_R_C5_TOP2 13, 27

/* A single column's top key, for when only the visible one is wanted. */
#define RK_L_C0_TOP 5
#define RK_L_C1_TOP 4
#define RK_L_C2_TOP 3
#define RK_L_C3_TOP 2
#define RK_L_C4_TOP 1
#define RK_L_C5_TOP 0

#define RK_R_C0_TOP 8
#define RK_R_C1_TOP 9
#define RK_R_C2_TOP 10
#define RK_R_C3_TOP 11
#define RK_R_C4_TOP 12
#define RK_R_C5_TOP 13

#define RK_L_TOP 0, 1, 2, 3, 4, 5
#define RK_R_TOP 8, 9, 10, 11, 12, 13
#define RK_BOTH_TOP RK_L_TOP, RK_R_TOP

/* Three thumbs a side, and all three have an LED. */
#define RK_L_THUMB 40, 41, 42
#define RK_R_THUMB 43, 44, 45
#define RK_BOTH_THUMB RK_L_THUMB, RK_R_THUMB

/*
 * The ex2 pair at the inner edge of each half: the encoder's push switch and
 * the key below it. They belong to no column, and they are the most visible
 * keys on the board -- nothing overhangs them and no finger rests there -- so
 * they are worth spending on any layer that needs to be unmistakable.
 */
#define RK_L_EX 6, 20
#define RK_R_EX 7, 21
#define RK_BOTH_EX RK_L_EX, RK_R_EX

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
 * THIS IS THE TABLE TO EDIT.
 *
 * TRANSCRIBED FROM THE EYELASH, column for column: this is
 * config/boards/shields/rgbzone/zone_palette.h's zone_layers[], so a layer wears
 * the same colours in the same columns on both keyboards. Its numbering already
 * agrees with ours -- rgbzone counts columns inner to outer and calls the
 * innermost 0, exactly as RK_*_C0 does here -- so a row maps straight across.
 *
 * Two things had to be decided rather than copied:
 *
 * SIDED ROWS are stated relative to the hand, as the eyelash states them:
 * RK_SIDE_HOLDING and RK_SIDE_OTHER, with both halves' columns listed and each
 * half keeping the group that is about it. The holding hand comes from
 * rgbzone_owner.c -- shared with the eyelash rather than reimplemented.
 *
 * An earlier version pinned each sided row to a fixed half instead, reasoning
 * that NUMPAD and SYMBOL come off the left thumb. That was wrong:
 * MAKE_ADAPTIVE_REPEAT puts SYMBOL on a key in EACH hand, so held from the
 * right it lit the far half and nothing under the hand doing the holding.
 *
 * The two GUI+nav layers stay absolute, because their names already say which
 * hand holds the GUI -- there is nothing to look up.
 *
 * PER-KEY, WHERE IT EARNS ITS PLACE. The columns are the eyelash's, and two
 * layers then say more than the eyelash can: navigation lights the real arrow
 * cluster and gaming the real WASD, over the top of the column wash. The wash
 * says which layer you are on, the cluster says where the keys are.
 */

/*
 * The clusters this board can light and the eyelash cannot: the real arrow keys
 * and the real WASD, rather than the column they happen to sit in.
 *
 * COUNTED OFF THE ROW MACROS IN jjb.keymap, not off the key labels. A full row
 * is the outer key plus the inner five -- `X_LT` is `<outer> X_LT_IN` on the
 * left and `X_RT_IN <outer>` on the right -- so every entry in an _IN macro is
 * one position further in than its bare index suggests. Getting that wrong
 * lights the neighbouring key, which looks deliberate and is very hard to spot.
 *
 *   NAVIGATION_LT_IN  PG_UP END UP HOME msc_up      -> UP at 3
 *   NAVIGATION_LM_IN  PG_DN LEFT DOWN RIGHT msc_dn  -> LEFT 16, DOWN 17, RIGHT 18
 *   NAVIGATION_RT_IN  msc_up HOME UP END PG_UP      -> UP at 10
 *   NAVIGATION_RM_IN  msc_dn LEFT DOWN RIGHT PG_DN  -> LEFT 23, DOWN 24, RIGHT 25
 *   GAMING_LT_IN      Q W E R T                     -> W at 2
 *   GAMING_LM_IN      A S D F G                     -> A 15, S 16, D 17
 *
 * On both hands UP sits directly above DOWN, in the same column, with LEFT and
 * RIGHT to either side of DOWN -- so the four read as a cluster.
 */
#define RGBKEY_ARROWS_LEFT  3, 16, 17, 18
#define RGBKEY_ARROWS_RIGHT 10, 23, 24, 25
#define RGBKEY_WASD 2, 15, 16, 17

static const struct rgbkey_layer rgbkey_layers[] = {
    /* Base: dark. Held modifiers paint over this from rgbkey_hrms below. */

    /*
     * Yellow on the holding side, cyan on col 2 of the other -- stated relative
     * to the hand, exactly as the eyelash states it, now that the hand is
     * known. Both halves' columns are listed; each half keeps the group that is
     * about it.
     */
    {"numpad", RKS_NUMPAD, true,
     {{RK_YELLOW, RK_KEYS(RK_L_C1, RK_R_C1), RK_SIDE_HOLDING},
      {RK_CYAN, RK_KEYS(RK_L_C2, RK_R_C2), RK_SIDE_OTHER}}},

    /*
     * SYMBOL is reachable from EITHER hand -- MAKE_ADAPTIVE_REPEAT puts it on
     * adaptive_u_repeat (left) and adaptive_d_repeat (right) -- which is what
     * made pinning it to the left wrong: held from the right it lit the far
     * half and nothing at all under the hand doing the holding.
     */
    {"symbol", RKS_SYMBOL, false,
     {{RK_BLUE, RK_KEYS(RK_L_C2, RK_R_C2), RK_SIDE_HOLDING}}},

    /*
     * Either thumb reaches navigation, so the green column follows the hand
     * holding it, as the eyelash's does. The green says WHICH layer; the arrows
     * in white say where the keys are, which is the part per-key hardware adds.
     *
     * The arrows are NOT sided: they are where they are on both hands, and the
     * point of showing them is that you can find them.
     *
     * The wash is listed first and the cluster second on purpose: groups paint
     * in order, so the cluster takes the two pixels they share -- the left
     * hand's RIGHT arrow and the right hand's LEFT arrow both fall in col 1.
     */
    {"navigation", RKS_NAV, true,
     {{RK_GREEN, RK_KEYS(RK_L_C1, RK_R_C1), RK_SIDE_HOLDING},
      {RK_WHITE, RK_KEYS(RGBKEY_ARROWS_LEFT, RGBKEY_ARROWS_RIGHT)}}},

    /*
     * Violet on col 4 of the hand holding the GUI, white on col 2 of the other.
     * The layer name says which hand that is.
     */
    {"LGUI+nav", RKS_LGUI_NAV, true,
     {{RK_GUI, RK_KEYS(RK_L_C4)},
      {RK_WHITE, RK_KEYS(RK_R_C2)}}},

    {"RGUI+nav", RKS_RGUI_NAV, true,
     {{RK_GUI, RK_KEYS(RK_R_C4)},
      {RK_WHITE, RK_KEYS(RK_L_C2)}}},

    /* Outer column, both halves -- not sided. */
    {"numeric", RKS_NUMERIC, true, {{RK_CYAN, RK_KEYS(RK_BOTH_C5)}}},

    /* Outer two columns, both halves -- not sided. */
    {"function", RKS_FUNCTION, false, {{RK_RED, RK_KEYS(RK_BOTH_OUTER2)}}},

    /* Outermost cyan, the one inside it violet, both halves -- not sided. */
    {"extra", RKS_EXTRA, false,
     {{RK_CYAN, RK_KEYS(RK_BOTH_C5)},
      {RK_GUI, RK_KEYS(RK_BOTH_C4)}}},

    {"superscript", RKS_SUPERSCRIPT, true,
     {{RK_TEAL, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1, RK_BOTH_C2, RK_BOTH_C3, RK_BOTH_C4)}}},

    /*
     * The eyelash's single column, plus the actual WASD in red. W and S fall in
     * col 3, so the cluster is second and takes those two pixels.
     *
     * Left half only: WASD is where the left hand sits, and there is no mirror
     * of it to light on the right.
     */
    {"gaming", RKS_GAMING, false,
     {{RK_DIM_WHITE, RK_KEYS(RK_BOTH_C3)},
      {RK_DIM_RED, RK_KEYS(RGBKEY_WASD)}}},

    /*
     * Every colour at once, which is the point: this is the layer where
     * brightness is set, and a single hue makes it hard to see what a step does
     * to the dimmer end of the palette.
     */
    {"lighting", RKS_LIGHTING, false,
     {{RK_RED, RK_KEYS(RK_BOTH_C0)},
      {RK_GREEN, RK_KEYS(RK_BOTH_C1)},
      {RK_BLUE, RK_KEYS(RK_BOTH_C2)},
      {RK_YELLOW, RK_KEYS(RK_BOTH_C3)},
      {RK_CYAN, RK_KEYS(RK_BOTH_C4)},
      {RK_MAGENTA, RK_KEYS(RK_BOTH_C5)}}},

    /* Inert on this board -- no radio -- but kept so the indices line up. */
    {"bluetooth", RKS_BLUETOOTH, false,
     {{RK_BLUE, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1, RK_BOTH_C2)}}},

    {"system", RKS_SYSTEM, false,
     {{RK_WHITE, RK_KEYS(RK_BOTH_C0, RK_BOTH_C1)},
      {RK_DIM_WHITE, RK_KEYS(RK_BOTH_C2)}}},
};

/*
 * Home-row mods, also as the eyelash does them: a held modifier lights its own
 * column ON ITS OWN HALF, in that modifier's signature colour. Columns are
 * rgbzone's zone_hrm_left/right exactly -- GUI on col 0, Ctrl 1, Shift 2,
 * Alt 3 -- and the four colours are already identical between the two shields.
 *
 * NOT mirrored to the far half, which is what the Rolio's layout does. Mirroring
 * exists there because a per-key board lights the very key the finger is resting
 * on; here the whole column lights, so the top key carries the signal from under
 * the hand, and staying on the modifier's own side keeps it saying the same
 * thing as the eyelash.
 */
static const struct rgbkey_hrm rgbkey_hrms[] = {
    {MOD_LGUI, RK_GUI, RK_KEYS(RK_L_C0)},
    {MOD_LCTL, RK_CTRL, RK_KEYS(RK_L_C1)},
    {MOD_LSFT, RK_SHIFT, RK_KEYS(RK_L_C2)},
    {MOD_LALT, RK_ALT, RK_KEYS(RK_L_C3)},

    {MOD_RGUI, RK_GUI, RK_KEYS(RK_R_C0)},
    {MOD_RCTL, RK_CTRL, RK_KEYS(RK_R_C1)},
    {MOD_RSFT, RK_SHIFT, RK_KEYS(RK_R_C2)},
    {MOD_RALT, RK_ALT, RK_KEYS(RK_R_C3)},
};

/*
 * CAPS LOCK, from the host's HID indicator rather than a layer this keyboard
 * toggles -- the two drift the moment anything else presses CAPS. Painted over
 * everything else: no layer's colours matter more.
 *
 * The eyelash has no equivalent, so this is the one thing here it does not
 * copy. The top row and thumbs, being the easiest to see.
 */
#define RGBKEY_CAPS_COLOUR RK_YELLOW
#define RGBKEY_CAPS_KEYS   RK_KEYS(RK_BOTH_TOP, RK_BOTH_THUMB)
