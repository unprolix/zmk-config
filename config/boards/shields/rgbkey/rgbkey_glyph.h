/*
 * The bigram mode's alphabet: which character a key makes, and how that
 * character is drawn on four LEDs.
 *
 * Built for LEARNING the characters, so every choice here is one that can be
 * worked out rather than memorised:
 *
 *   top     off = the letters-and-digits grid, white = the punctuation grid
 *   home    the grid row
 *   bottom  the grid column
 *   thumb   white = the shifted twin of the same key
 *
 * White always means "the other one". Rows and columns only ever use six hues,
 * in colour-wheel order -- red yellow green cyan blue magenta -- so a white LED
 * in the middle of a glyph can only be the space.
 *
 * A shifted character shares its key's cell: `a` and `A` differ by one LED, and
 * so do `2` and `@`. This follows the US keycodes the host sees, not where a
 * character happens to live on jjb's layers, because a character is the thing
 * being learned.
 *
 * Grid A, letters then digits, alphabetical so any of them can be derived:
 *
 *        R  Y  G  C  B  M
 *     R  a  b  c  d  e  f
 *     Y  g  h  i  j  k  l
 *     G  m  n  o  p  q  r
 *     C  s  t  u  v  w  x
 *     B  y  z  0  1  2  3
 *     M  4  5  6  7  8  9
 *
 * Grid B, the eleven US punctuation keys, grouped by family so the row says
 * which family and the column which member (shifted twins in brackets):
 *
 *     R  sentence   ,(<)  .(>)  ;(:)  '(")
 *     Y  math       -(_)  =(+)
 *     G  brackets   [({)  ](})
 *     C  slashes    /(?)  \(|)  `(~)
 *
 * The space, the commonest character of all, gets the one shape no cell can
 * make: the whole column dim white.
 *
 * Pure C with no Zephyr in it, so the whole table can be checked on the host.
 * Include it after enum rgbkey_colour is declared; rgbkey.h does.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <dt-bindings/zmk/hid_usage.h>

/* Not a character: printable ASCII never uses code 0. */
#define RGBKEY_GLYPH_NO_CHAR 0

#define RGBKEY_GLYPH_HUE_COUNT 6

/* Row and column hues, in colour-wheel order. */
static const enum rgbkey_colour rgbkey_glyph_hues[RGBKEY_GLYPH_HUE_COUNT] = {
    RK_RED, RK_YELLOW, RK_GREEN, RK_CYAN, RK_BLUE, RK_MAGENTA,
};

#define RGBKEY_GLYPH_CELL(row, col) ((row) * RGBKEY_GLYPH_HUE_COUNT + (col))

/* Grid A: a..z fill cells 0..25, then 0..9 fill 26..35. */
#define RGBKEY_GLYPH_DIGIT_CELL 26
#define RGBKEY_GLYPH_DIGITS     10

/* "The other one": the punctuation grid on the top LED, shift on the thumb. */
#define RGBKEY_GLYPH_MARK  RK_WHITE
#define RGBKEY_GLYPH_SPACE RK_DIM_WHITE

/* What shift makes of each digit key on a US layout, indexed by the digit. */
static const char rgbkey_glyph_shifted_digits[RGBKEY_GLYPH_DIGITS] = {
    ')', '!', '@', '#', '$', '%', '^', '&', '*', '(',
};

struct rgbkey_punct_key {
    uint8_t usage;
    char plain;
    char shifted;
    uint8_t cell;
};

static const struct rgbkey_punct_key rgbkey_punct_keys[] = {
    /* sentence */
    {HID_USAGE_KEY_KEYBOARD_COMMA_AND_LESS_THAN, ',', '<', RGBKEY_GLYPH_CELL(0, 0)},
    {HID_USAGE_KEY_KEYBOARD_PERIOD_AND_GREATER_THAN, '.', '>', RGBKEY_GLYPH_CELL(0, 1)},
    {HID_USAGE_KEY_KEYBOARD_SEMICOLON_AND_COLON, ';', ':', RGBKEY_GLYPH_CELL(0, 2)},
    {HID_USAGE_KEY_KEYBOARD_APOSTROPHE_AND_QUOTE, '\'', '"', RGBKEY_GLYPH_CELL(0, 3)},
    /* math */
    {HID_USAGE_KEY_KEYBOARD_MINUS_AND_UNDERSCORE, '-', '_', RGBKEY_GLYPH_CELL(1, 0)},
    {HID_USAGE_KEY_KEYBOARD_EQUAL_AND_PLUS, '=', '+', RGBKEY_GLYPH_CELL(1, 1)},
    /* brackets */
    {HID_USAGE_KEY_KEYBOARD_LEFT_BRACKET_AND_LEFT_BRACE, '[', '{', RGBKEY_GLYPH_CELL(2, 0)},
    {HID_USAGE_KEY_KEYBOARD_RIGHT_BRACKET_AND_RIGHT_BRACE, ']', '}', RGBKEY_GLYPH_CELL(2, 1)},
    /* slashes */
    {HID_USAGE_KEY_KEYBOARD_SLASH_AND_QUESTION_MARK, '/', '?', RGBKEY_GLYPH_CELL(3, 0)},
    {HID_USAGE_KEY_KEYBOARD_BACKSLASH_AND_PIPE, '\\', '|', RGBKEY_GLYPH_CELL(3, 1)},
    {HID_USAGE_KEY_KEYBOARD_GRAVE_ACCENT_AND_TILDE, '`', '~', RGBKEY_GLYPH_CELL(3, 2)},
};

#define RGBKEY_PUNCT_KEY_COUNT (sizeof(rgbkey_punct_keys) / sizeof(rgbkey_punct_keys[0]))

/*
 * The character a keyboard-page usage prints, or RGBKEY_GLYPH_NO_CHAR.
 *
 * Caps lock flips letters only, exactly as a host does. Keypad digits assume
 * num lock, which is the only way this layout uses them.
 */
static inline char rgbkey_char_for(uint32_t usage, bool shift, bool caps_lock) {
    if (usage >= HID_USAGE_KEY_KEYBOARD_A && usage <= HID_USAGE_KEY_KEYBOARD_Z) {
        const bool upper = shift != caps_lock;
        return (char)((upper ? 'A' : 'a') + (usage - HID_USAGE_KEY_KEYBOARD_A));
    }
    /* The number row runs 1..9 and then 0. */
    if (usage >= HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION &&
        usage <= HID_USAGE_KEY_KEYBOARD_0_AND_RIGHT_PARENTHESIS) {
        const unsigned digit =
            (usage - HID_USAGE_KEY_KEYBOARD_1_AND_EXCLAMATION + 1) % RGBKEY_GLYPH_DIGITS;
        return shift ? rgbkey_glyph_shifted_digits[digit] : (char)('0' + digit);
    }
    /* So does the keypad. */
    if (usage >= HID_USAGE_KEY_KEYPAD_1_AND_END && usage <= HID_USAGE_KEY_KEYPAD_0_AND_INSERT) {
        return (char)('0' + (usage - HID_USAGE_KEY_KEYPAD_1_AND_END + 1) % RGBKEY_GLYPH_DIGITS);
    }
    switch (usage) {
    case HID_USAGE_KEY_KEYBOARD_SPACEBAR:
        return ' ';
    case HID_USAGE_KEY_KEYPAD_SLASH:
        return '/';
    case HID_USAGE_KEY_KEYPAD_ASTERISK:
        return '*';
    case HID_USAGE_KEY_KEYPAD_MINUS:
        return '-';
    case HID_USAGE_KEY_KEYPAD_PLUS:
        return '+';
    default:
        break;
    }
    for (size_t i = 0; i < RGBKEY_PUNCT_KEY_COUNT; i++) {
        if (rgbkey_punct_keys[i].usage == usage) {
            return shift ? rgbkey_punct_keys[i].shifted : rgbkey_punct_keys[i].plain;
        }
    }
    return RGBKEY_GLYPH_NO_CHAR;
}

/* One character's four LEDs. */
struct rgbkey_glyph {
    enum rgbkey_colour top;
    enum rgbkey_colour home;
    enum rgbkey_colour bottom;
    enum rgbkey_colour thumb;
};

static inline void rgbkey_glyph_cell(uint8_t cell, bool punctuation, bool shifted,
                                     struct rgbkey_glyph *out) {
    out->top = punctuation ? RGBKEY_GLYPH_MARK : RK_OFF;
    out->home = rgbkey_glyph_hues[cell / RGBKEY_GLYPH_HUE_COUNT];
    out->bottom = rgbkey_glyph_hues[cell % RGBKEY_GLYPH_HUE_COUNT];
    out->thumb = shifted ? RGBKEY_GLYPH_MARK : RK_OFF;
}

/* How to draw `c`. False, and `out` untouched, for anything not printable ASCII. */
static inline bool rgbkey_glyph_of(char c, struct rgbkey_glyph *out) {
    if (c == ' ') {
        out->top = out->home = out->bottom = RGBKEY_GLYPH_SPACE;
        out->thumb = RK_OFF;
        return true;
    }
    if (c >= 'a' && c <= 'z') {
        rgbkey_glyph_cell((uint8_t)(c - 'a'), false, false, out);
        return true;
    }
    if (c >= 'A' && c <= 'Z') {
        rgbkey_glyph_cell((uint8_t)(c - 'A'), false, true, out);
        return true;
    }
    if (c >= '0' && c <= '9') {
        rgbkey_glyph_cell((uint8_t)(RGBKEY_GLYPH_DIGIT_CELL + (c - '0')), false, false, out);
        return true;
    }
    for (uint8_t digit = 0; digit < RGBKEY_GLYPH_DIGITS; digit++) {
        if (rgbkey_glyph_shifted_digits[digit] == c) {
            rgbkey_glyph_cell((uint8_t)(RGBKEY_GLYPH_DIGIT_CELL + digit), false, true, out);
            return true;
        }
    }
    for (size_t i = 0; i < RGBKEY_PUNCT_KEY_COUNT; i++) {
        if (rgbkey_punct_keys[i].plain == c || rgbkey_punct_keys[i].shifted == c) {
            rgbkey_glyph_cell(rgbkey_punct_keys[i].cell, true, rgbkey_punct_keys[i].shifted == c,
                              out);
            return true;
        }
    }
    return false;
}
