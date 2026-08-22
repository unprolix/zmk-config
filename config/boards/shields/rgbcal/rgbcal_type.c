/*
 * Typing results out as keystrokes.
 *
 * The findings have to leave the keyboard somehow, and every other route is
 * worse: the panel is 68 pixels wide, there is no USB console in this build,
 * and the peripheral has no way to report anything but key positions. But the
 * device is a keyboard -- so it can simply type the table into whatever has
 * focus.
 *
 * Only the characters the table needs are mapped: digits, a few punctuation
 * marks, lowercase letters, newline.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <dt-bindings/zmk/keys.h>
#include <zmk/behavior.h>
#include <zmk/keys.h>

#include "rgbcal.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * Slow enough that whatever is receiving keeps up. A host that drops
 * characters makes the table silently wrong, which is the one failure mode
 * that would waste the whole exercise.
 */
#define TYPE_GAP_MS 12

static int type_key(uint32_t keycode) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)),
        .param1 = keycode,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };

    int err = zmk_behavior_invoke_binding(&binding, event, true);
    if (err) {
        return err;
    }
    k_msleep(TYPE_GAP_MS);
    err = zmk_behavior_invoke_binding(&binding, event, false);
    k_msleep(TYPE_GAP_MS);
    return err;
}

static uint32_t keycode_for(char c) {
    if (c >= 'a' && c <= 'z') {
        return A + (c - 'a');
    }
    if (c >= '1' && c <= '9') {
        return NUMBER_1 + (c - '1');
    }
    switch (c) {
    case '0':
        return NUMBER_0;
    case ' ':
        return SPACE;
    case '\n':
        return RET;
    case ':':
        return SEMICOLON | (MOD_LSFT << 24); /* shifted, via the implicit-mod encoding */
    case '-':
        return MINUS;
    case ',':
        return COMMA;
    case '.':
        return DOT;
    case '=':
        return EQUAL;
    default:
        return 0;
    }
}

void rgbcal_type(const char *text) {
    for (const char *c = text; *c != '\0'; c++) {
        uint32_t code = keycode_for(*c);
        if (code == 0) {
            continue;
        }
        type_key(code);
    }
}
