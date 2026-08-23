/*
 * THIS IS THE FILE TO EDIT.
 *
 * Per-layer column colours. Six addressable zones per half, one per column,
 * running INNER to OUTER -- the hardware has one LED per column, not per key
 * (see zone_map.h), so this is the finest control available.
 *
 * Colours come from a sixteen-entry palette rather than being written as RGB
 * because the far half's colours have to cross the split link, where a relayed
 * behaviour carries only eight bytes: six four-bit palette indices fit, six
 * RGB triples do not. Both halves compile this same table, so an index means
 * the same thing on each.
 *
 * A layer not listed here stays dark. So does any layer whose row is all OFF,
 * which costs nothing -- the strip's supply is only raised when something is
 * actually lit.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>

#include <zmk/keys.h>

#include "zone_map.h"

/*
 * The only brightness knob: every palette entry is built from these two.
 *
 * Still well short of full scale, deliberately. Twenty-one emitters a side at
 * maximum is dazzling at arm's length, and current draw scales with them --
 * on a keyboard that otherwise idles in single-digit milliamps, that is the
 * difference between weeks of battery and hours. Raise further if these are
 * still too dim; halve them if the strip starts costing noticeable runtime.
 */
#define ZONE_DIM 0x30
#define ZONE_MID 0x50

/* Palette. Sixteen slots; indices are what travel over the split. */
enum zone_colour {
    ZC_OFF = 0,
    ZC_RED,
    ZC_GREEN,
    ZC_BLUE,
    ZC_YELLOW,
    ZC_CYAN,
    ZC_MAGENTA,
    ZC_WHITE,
    ZC_ORANGE,
    ZC_PURPLE,
    ZC_TEAL,
    ZC_LIME,
    ZC_PINK,
    ZC_AMBER,
    /* Violet took indigo's slot: the palette is exactly full at sixteen, the
       two are barely distinguishable on these emitters, and only the
       bluetooth row used indigo. */
    ZC_VIOLET,
    ZC_DIM_WHITE,
    ZC_COUNT
};

BUILD_ASSERT(ZC_COUNT <= 16, "Palette must fit in four bits to cross the split link");

static const uint8_t zone_palette[ZC_COUNT][3] = {
    [ZC_OFF] = {0, 0, 0},
    [ZC_RED] = {ZONE_MID, 0, 0},
    [ZC_GREEN] = {0, ZONE_MID, 0},
    [ZC_BLUE] = {0, 0, ZONE_MID},
    [ZC_YELLOW] = {ZONE_MID, ZONE_MID, 0},
    [ZC_CYAN] = {0, ZONE_MID, ZONE_MID},
    [ZC_MAGENTA] = {ZONE_MID, 0, ZONE_MID},
    [ZC_WHITE] = {ZONE_MID, ZONE_MID, ZONE_MID},
    [ZC_ORANGE] = {ZONE_MID, ZONE_DIM, 0},
    [ZC_PURPLE] = {ZONE_DIM, 0, ZONE_MID},
    [ZC_TEAL] = {0, ZONE_MID, ZONE_DIM},
    [ZC_LIME] = {ZONE_DIM, ZONE_MID, 0},
    [ZC_PINK] = {ZONE_MID, 0, ZONE_DIM},
    [ZC_AMBER] = {ZONE_MID, ZONE_DIM / 2, 0},
    /* Clearly bluer than ZC_PURPLE, which alt uses. */
    [ZC_VIOLET] = {ZONE_DIM, 0, ZONE_MID + 0x10},
    [ZC_DIM_WHITE] = {ZONE_DIM, ZONE_DIM, ZONE_DIM},
};

/*
 * A layer's colours are given relative to the half whose key is being held,
 * not as fixed left/right. Which side that is comes from the split source of
 * the press that activated the layer, so "the opposite side from the pressed
 * mod key" is expressible -- and a pair like LGUI+nav / RGUI+nav collapses to
 * a single row, since symmetrical means identical once stated this way.
 */
struct zone_layer {
    /* Matches the layer's display-name in the keymap. */
    const char *name;
    /* The half holding the key. Inner column first (col 0), outer last. */
    enum zone_colour pressed[ZONE_COUNT];
    /* The other half. */
    enum zone_colour other[ZONE_COUNT];
};

/*
 * A layer with no row here stays dark, which costs nothing: the strip's supply
 * is only raised when something is lit.
 *
 *                        col: 0     1     2     3     4     5
 */
static const struct zone_layer zone_layers[] = {
    /* Base: dark. Held modifiers paint over this from zone_hrm_* below. */
    {"hierophant", {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF}},

    /* Yellow on the pressed side, cyan on col 2 of the other hand. */
    {"numpad",     {ZC_OFF, ZC_YELLOW, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_OFF, ZC_OFF, ZC_CYAN, ZC_OFF, ZC_OFF, ZC_OFF}},

    {"symbol",     {ZC_OFF, ZC_OFF, ZC_BLUE, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF}},

    {"navigation", {ZC_OFF, ZC_GREEN, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF}},

    /*
     * The pinky nav layers: violet on col 4 of the holding half, white on
     * col 2 of the other. One row serves both, which is what "symmetrical"
     * means once colours are stated relative to the pressed side.
     */
    {"LGUI+nav",   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_VIOLET, ZC_OFF},
                   {ZC_OFF, ZC_OFF, ZC_WHITE, ZC_OFF, ZC_OFF, ZC_OFF}},

    {"RGUI+nav",   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_VIOLET, ZC_OFF},
                   {ZC_OFF, ZC_OFF, ZC_WHITE, ZC_OFF, ZC_OFF, ZC_OFF}},

    /* Outer column, both halves -- not sided. */
    {"numeric",    {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_CYAN},
                   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_CYAN}},

    /* Outer two columns, both halves -- not sided. */
    {"function",   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_RED, ZC_RED},
                   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_RED, ZC_RED}},

    /* Outermost cyan, the one inside it violet, both halves -- not sided. */
    {"extra",      {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_VIOLET, ZC_CYAN},
                   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_VIOLET, ZC_CYAN}},

    /* Not yet specified; left as they were, to be tuned. */

    {"superscript", {ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_OFF},
                    {ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_OFF}},

    {"gaming",     {ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED},
                   {ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED}},

    {"caps",       {ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK},
                   {ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK}},

    {"rgb",        {ZC_RED, ZC_GREEN, ZC_BLUE, ZC_YELLOW, ZC_CYAN, ZC_MAGENTA},
                   {ZC_RED, ZC_GREEN, ZC_BLUE, ZC_YELLOW, ZC_CYAN, ZC_MAGENTA}},

    {"bluetooth",  {ZC_BLUE, ZC_BLUE, ZC_BLUE, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_BLUE, ZC_BLUE, ZC_BLUE, ZC_OFF, ZC_OFF, ZC_OFF}},

    {"system",     {ZC_WHITE, ZC_WHITE, ZC_DIM_WHITE, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_WHITE, ZC_WHITE, ZC_DIM_WHITE, ZC_OFF, ZC_OFF, ZC_OFF}},
};

#define ZONE_LAYER_COUNT ARRAY_SIZE(zone_layers)

/*
 * Home-row mods on the base layer.
 *
 * The base layer is dark until a modifier is actually held, at which point its
 * column lights. That keeps the strip off during ordinary typing -- the whole
 * reason the base layer is blank -- while still showing a mod the moment it
 * engages, which is what makes a misfire visible.
 *
 * The two halves are laid out symmetrically, so the zone numbers match: from
 * hierophant.dtsi, the home row runs ... ctrl shift alt gui ... outward from
 * the index finger on both sides. Zone 0 is the innermost column, and neither
 * zone 0 nor zone 5 carries a modifier.
 *
 * This reads whichever modifiers are actually held, so a mod applied by
 * something other than a home-row key lights the same column. That is
 * deliberate: the question being answered is "which modifier is live".
 */
struct zone_hrm {
    zmk_mod_flags_t mod;
    uint8_t zone;
    enum zone_colour colour;
};

static const struct zone_hrm zone_hrm_left[] = {
    {MOD_LCTL, 1, ZC_RED},
    {MOD_LSFT, 2, ZC_CYAN},
    {MOD_LALT, 3, ZC_PURPLE},
    {MOD_LGUI, 0, ZC_VIOLET},
};

static const struct zone_hrm zone_hrm_right[] = {
    {MOD_RCTL, 1, ZC_RED},
    {MOD_RSFT, 2, ZC_CYAN},
    {MOD_RALT, 3, ZC_PURPLE},
    {MOD_RGUI, 0, ZC_VIOLET},
};
