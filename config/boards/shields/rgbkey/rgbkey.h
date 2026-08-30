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

/*
 * Brightness is per-board, because it is set by what the keycaps let through
 * and by where the power comes from -- see each layout header for its reasoning.
 */
#if defined(CONFIG_SHIELD_CORNE_V4_LEFT) || defined(CONFIG_SHIELD_CORNE_V4_RIGHT)
/*
 * The Corne v4 runs dim. It has no battery to spare either way, but all 46
 * emitters draw from the single USB port in the LEFT half -- the right is
 * powered over the interlink -- and rgbkey writes the strip directly, so ZMK's
 * underglow brightness cap does not apply to it. At full, one colour across
 * every LED is roughly 900mA against a 500mA port budget. This lands near
 * foostan's own max_brightness of 50/255.
 */
#define RGBKEY_BRIGHT 0
#else
#define RGBKEY_BRIGHT 1
#endif

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

/*
 * Which half a group is talking about.
 *
 * The eyelash states its sided layers relative to the hand holding the layer
 * open -- "yellow on the pressed side, cyan on col 2 of the other hand" -- and
 * that hand is genuinely not knowable after the fact. rgbzone_owner.c records
 * it at the press; the packed word carries it here.
 *
 * A group tagged HOLDING or OTHER should list the positions for BOTH halves.
 * Each half then keeps or drops the whole group depending on which one it is,
 * and paint() discards the far half's positions as it already does. That way a
 * sided row needs no position arithmetic -- just the same two columns it would
 * name anyway.
 *
 * ANY is the default, so a row that never says otherwise -- every row the
 * Rolio has -- behaves exactly as before.
 */
enum rgbkey_side {
    RK_SIDE_ANY = 0,
    RK_SIDE_HOLDING,
    RK_SIDE_OTHER,
};

struct rgbkey_group {
    enum rgbkey_colour colour;
    /* RGBKEY_POS_NONE terminated; NULL for an unused slot. */
    const uint8_t *positions;
    enum rgbkey_side side;
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
 * WHICH BOARD'S LAYOUT.
 *
 * Everything above this point is the same on every board: the palette, the
 * scene and colour enums, the wire format. Everything below -- which key sits
 * at which position, what each layer lights, where a held modifier shows --
 * is a property of one keyboard and lives in its own layout header.
 *
 * The split is not tidiness. The two boards disagree about position numbering
 * (the Rolio counts its top row inward, the Corne v4 outward), so a table
 * shared between them would be wrong on one of the two in a way that looks
 * like working code.
 */
#if defined(CONFIG_SHIELD_CORNE_V4_LEFT) || defined(CONFIG_SHIELD_CORNE_V4_RIGHT)
#define RGBKEY_LAYOUT_HEADER "rgbkey_layout_corne_v4.h"
#elif defined(CONFIG_SHIELD_ROLIO_LEFT) || defined(CONFIG_SHIELD_ROLIO_RIGHT)
#define RGBKEY_LAYOUT_HEADER "rgbkey_layout_rolio.h"
#else
#error "rgbkey has no layout for this shield -- add rgbkey_layout_<board>.h"
#endif

#include RGBKEY_LAYOUT_HEADER

#define RGBKEY_LAYER_COUNT ARRAY_SIZE(rgbkey_layers)

/*
 * The wire format: a scene in the high byte, the live modifier flags in the
 * low one. Two bytes, against the eight a relayed behaviour can carry.
 */
/*
 * Caps lock rides in the top bit of the scene byte. Only the central hears from
 * the host, so without carrying it the far half would never show caps at all.
 */
#define RGBKEY_CAPS_BIT 0x80

BUILD_ASSERT(RKS_COUNT <= RGBKEY_CAPS_BIT, "Scenes must not collide with the caps bit");

/*
 * The holding side rides above the scene byte. There is room: the relay carries
 * param1, a full 32 bits, of which only the low sixteen were ever used.
 *
 * Two bits, not one -- "held by the right hand" and "the holding hand is known
 * at all" are different facts. A layer reached by something other than a
 * momentary press has no holding hand, and a sided group then lights on both
 * halves rather than picking one at random.
 */
#define RGBKEY_SIDE_KNOWN_BIT BIT(16)
#define RGBKEY_SIDE_RIGHT_BIT BIT(17)

/*
 * And above those, the POSITION of the key holding the layer open, so the key
 * under the thumb can light in the layer's own colour. Six bits is every
 * position either board has; the assert is what will catch a wider one.
 *
 * Guarded by the same known bit as the side: position 0 is a real key, so
 * "no owner" cannot be spelled as a position.
 */
#define RGBKEY_OWNER_POS_SHIFT 18
#define RGBKEY_OWNER_POS_BITS  6
#define RGBKEY_OWNER_POS_MASK  ((1u << RGBKEY_OWNER_POS_BITS) - 1)

BUILD_ASSERT(RGBKEY_POSITIONS <= RGBKEY_OWNER_POS_MASK + 1,
             "A position must fit in the bits the split link carries for it");

static inline uint32_t rgbkey_pack(enum rgbkey_scene scene, zmk_mod_flags_t mods, bool caps) {
    uint32_t s = (uint32_t)scene | (caps ? RGBKEY_CAPS_BIT : 0);
    return (s << 8) | (uint32_t)mods;
}

static inline uint32_t rgbkey_pack_owner(uint32_t packed, bool known, bool from_right,
                                         uint8_t position) {
    if (!known) {
        return packed;
    }
    return packed | RGBKEY_SIDE_KNOWN_BIT | (from_right ? RGBKEY_SIDE_RIGHT_BIT : 0) |
           ((uint32_t)(position & RGBKEY_OWNER_POS_MASK) << RGBKEY_OWNER_POS_SHIFT);
}

static inline bool rgbkey_side_known(uint32_t packed) {
    return (packed & RGBKEY_SIDE_KNOWN_BIT) != 0;
}

static inline bool rgbkey_side_is_right(uint32_t packed) {
    return (packed & RGBKEY_SIDE_RIGHT_BIT) != 0;
}

/* Only meaningful when rgbkey_side_known(). */
static inline uint8_t rgbkey_owner_pos(uint32_t packed) {
    return (uint8_t)((packed >> RGBKEY_OWNER_POS_SHIFT) & RGBKEY_OWNER_POS_MASK);
}

static inline enum rgbkey_scene rgbkey_scene_of(uint32_t packed) {
    enum rgbkey_scene s = (enum rgbkey_scene)((packed >> 8) & ~RGBKEY_CAPS_BIT & 0xFF);
    return s < RKS_COUNT ? s : RKS_NONE;
}

static inline bool rgbkey_caps_of(uint32_t packed) {
    return ((packed >> 8) & RGBKEY_CAPS_BIT) != 0;
}

static inline zmk_mod_flags_t rgbkey_mods_of(uint32_t packed) {
    return (zmk_mod_flags_t)(packed & 0xFF);
}

/* Light this half's keys from a packed scene-and-modifiers word. */
void rgbkey_apply(uint32_t packed);
