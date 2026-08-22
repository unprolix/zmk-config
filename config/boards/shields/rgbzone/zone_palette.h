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
 * Deliberately dim. Twenty-one emitters a side at full brightness is both
 * dazzling at arm's length and the difference between weeks of battery and
 * hours on a keyboard that otherwise idles in single-digit milliamps.
 */
#define ZONE_DIM 0x18
#define ZONE_MID 0x28

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
    ZC_INDIGO,
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
    [ZC_INDIGO] = {0, 0, ZONE_DIM},
    [ZC_DIM_WHITE] = {ZONE_DIM, ZONE_DIM, ZONE_DIM},
};

struct zone_layer {
    /* Matches the layer's display-name in the keymap. */
    const char *name;
    /* Inner column first, outer last. */
    enum zone_colour left[ZONE_COUNT];
    enum zone_colour right[ZONE_COUNT];
};

/*
 * Starting point only -- rewrite these rows to taste. The base layer is left
 * dark on purpose: it is where the keyboard sits essentially all the time, so
 * lighting it turns "occasionally lit" into "always lit".
 *
 *                          inner ---------------------> outer
 */
static const struct zone_layer zone_layers[] = {
    {"hierophant", {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF}},

    {"numeric",    {ZC_CYAN, ZC_CYAN, ZC_CYAN, ZC_BLUE, ZC_BLUE, ZC_OFF},
                   {ZC_CYAN, ZC_CYAN, ZC_CYAN, ZC_BLUE, ZC_BLUE, ZC_OFF}},

    {"numpad",     {ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF, ZC_OFF},
                   {ZC_AMBER, ZC_AMBER, ZC_AMBER, ZC_ORANGE, ZC_OFF, ZC_OFF}},

    {"symbol",     {ZC_MAGENTA, ZC_MAGENTA, ZC_MAGENTA, ZC_PURPLE, ZC_PURPLE, ZC_OFF},
                   {ZC_MAGENTA, ZC_MAGENTA, ZC_MAGENTA, ZC_PURPLE, ZC_PURPLE, ZC_OFF}},

    {"navigation", {ZC_GREEN, ZC_GREEN, ZC_GREEN, ZC_LIME, ZC_LIME, ZC_OFF},
                   {ZC_GREEN, ZC_GREEN, ZC_GREEN, ZC_LIME, ZC_LIME, ZC_OFF}},

    {"function",   {ZC_YELLOW, ZC_YELLOW, ZC_YELLOW, ZC_AMBER, ZC_AMBER, ZC_OFF},
                   {ZC_YELLOW, ZC_YELLOW, ZC_YELLOW, ZC_AMBER, ZC_AMBER, ZC_OFF}},

    {"superscript", {ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_OFF},
                    {ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_TEAL, ZC_OFF}},

    {"gaming",     {ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED},
                   {ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED, ZC_RED}},

    {"caps",       {ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK},
                   {ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK, ZC_PINK}},

    {"rgb",        {ZC_RED, ZC_GREEN, ZC_BLUE, ZC_YELLOW, ZC_CYAN, ZC_MAGENTA},
                   {ZC_RED, ZC_GREEN, ZC_BLUE, ZC_YELLOW, ZC_CYAN, ZC_MAGENTA}},

    {"bluetooth",  {ZC_INDIGO, ZC_INDIGO, ZC_INDIGO, ZC_BLUE, ZC_BLUE, ZC_OFF},
                   {ZC_INDIGO, ZC_INDIGO, ZC_INDIGO, ZC_BLUE, ZC_BLUE, ZC_OFF}},

    {"system",     {ZC_WHITE, ZC_WHITE, ZC_WHITE, ZC_DIM_WHITE, ZC_DIM_WHITE, ZC_OFF},
                   {ZC_WHITE, ZC_WHITE, ZC_WHITE, ZC_DIM_WHITE, ZC_DIM_WHITE, ZC_OFF}},
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
    {MOD_LSFT, 2, ZC_YELLOW},
    {MOD_LALT, 3, ZC_GREEN},
    {MOD_LGUI, 4, ZC_BLUE},
};

static const struct zone_hrm zone_hrm_right[] = {
    {MOD_RCTL, 1, ZC_RED},
    {MOD_RSFT, 2, ZC_YELLOW},
    {MOD_RALT, 3, ZC_GREEN},
    {MOD_RGUI, 4, ZC_BLUE},
};
