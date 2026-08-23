/*
 * Driving the LEDs from the host, for diagnosis.
 *
 * Chasing "the wrong column lit in the wrong colour" by editing tables and
 * reflashing is slow and confounded: a bad zone map, a bad palette index, a
 * relay that never arrived and a strip that swallowed its write all look the
 * same from across the desk. This lets the host set an individual pixel on
 * either half and leave it there, which separates those cases in one step.
 *
 * TEMPORARY, like the bootloader trigger it shares a channel with. It is not
 * removed on anyone's initiative but jjb's.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/split/bluetooth/service.h>

#include "rgbzone.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/* The behaviour that carries a poke to the far half                   */
/* ------------------------------------------------------------------ */

#define DT_DRV_COMPAT zmk_behavior_rgb_diag

BUILD_ASSERT(sizeof("rgbdiag") <= ZMK_SPLIT_RUN_BEHAVIOR_DEV_LEN,
             "Relay behaviour name is too long to survive the split payload intact");

/* param1: command. param2: index and colour, one byte each. */
#define DIAG_CMD_PIXEL   1
#define DIAG_CMD_ALL     2
#define DIAG_CMD_RELEASE 3

static void diag_dispatch(uint32_t cmd, uint32_t arg) {
    uint8_t index = arg & 0xFF;
    uint8_t r = (arg >> 8) & 0xFF;
    uint8_t g = (arg >> 16) & 0xFF;
    uint8_t b = (arg >> 24) & 0xFF;

    switch (cmd) {
    case DIAG_CMD_PIXEL:
        rgbzone_diag_pixel(index, r, g, b);
        break;
    case DIAG_CMD_ALL:
        rgbzone_diag_all(r, g, b);
        break;
    case DIAG_CMD_RELEASE:
        rgbzone_diag_release();
        break;
    default:
        break;
    }
}

static int on_rgbdiag_pressed(struct zmk_behavior_binding *binding,
                              struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);
#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    diag_dispatch(binding->param1, binding->param2);
#else
    /* The central pokes its own strip directly; its copy of this is noise. */
    ARG_UNUSED(binding);
#endif
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_rgbdiag_released(struct zmk_behavior_binding *binding,
                               struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int rgbdiag_init(const struct device *dev) {
    ARG_UNUSED(dev);
    return 0;
}

static const struct behavior_driver_api rgbdiag_api = {
    .binding_pressed = on_rgbdiag_pressed,
    .binding_released = on_rgbdiag_released,
    .locality = BEHAVIOR_LOCALITY_GLOBAL,
};

#define RGBDIAG_INST(n)                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, rgbdiag_init, NULL, NULL, NULL, POST_KERNEL,                        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &rgbdiag_api);

DT_INST_FOREACH_STATUS_OKAY(RGBDIAG_INST)

/* ------------------------------------------------------------------ */
/* Host commands, central only                                         */
/* ------------------------------------------------------------------ */

#if IS_ENABLED(CONFIG_RAW_HID) &&                                                                  \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

#include <raw_hid/events.h>

/*
 * Distinct from the bootloader command's magic, and equally deliberate: raw
 * HID is a shared channel and a short command could be hit by accident.
 */
static const uint8_t RGB_MAGIC[] = {'Z', 'M', 'K', 'R', 'G', 'B', '!'};
#define RGB_MAGIC_LEN sizeof(RGB_MAGIC)

/* magic, half, cmd, index, r, g, b */
#define RGB_MIN_LEN (RGB_MAGIC_LEN + 6)

#define RGB_HALF_LEFT  0
#define RGB_HALF_RIGHT 1

static void send_remote(uint32_t cmd, uint32_t arg) {
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(rgbdiag)),
        .param1 = cmd,
        .param2 = arg,
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };
    zmk_behavior_invoke_binding(&binding, event, true);
}

/*
 * Report what the firmware actually decided, back to the host.
 *
 * With the strip, the zone map and the palette all proven good by direct
 * pokes, the only thing left to look at is the value refresh() computes -- and
 * that is invisible from outside. There is no console on this build, and the
 * panel is 68 pixels wide. Raw HID goes both ways, so send it.
 */
static const uint8_t ST_MAGIC[] = {'Z', 'M', 'K', 'S', 'T', '8', '!'};
static bool report_enabled;
static uint8_t report_buf[CONFIG_RAW_HID_REPORT_SIZE];

void rgbzone_diag_report(uint8_t layer_index, const char *layer_name, uint8_t mods,
                         uint32_t packed_left, uint32_t packed_right) {
    if (!report_enabled) {
        return;
    }

    memset(report_buf, 0, sizeof(report_buf));
    memcpy(report_buf, ST_MAGIC, sizeof(ST_MAGIC));

    uint8_t *p = report_buf + sizeof(ST_MAGIC);
    p[0] = layer_index;
    p[1] = mods;
    p[2] = packed_left & 0xFF;
    p[3] = (packed_left >> 8) & 0xFF;
    p[4] = (packed_left >> 16) & 0xFF;
    p[5] = (packed_left >> 24) & 0xFF;
    p[6] = packed_right & 0xFF;
    p[7] = (packed_right >> 8) & 0xFF;
    p[8] = (packed_right >> 16) & 0xFF;
    p[9] = (packed_right >> 24) & 0xFF;

    /* Whatever of the name fits in what is left. */
    size_t used = sizeof(ST_MAGIC) + 10;
    size_t room = sizeof(report_buf) - used - 1;
    if (layer_name != NULL) {
        strncpy((char *)report_buf + used, layer_name, room);
    }

    raise_raw_hid_sent_event(
        (struct raw_hid_sent_event){.data = report_buf, .length = sizeof(report_buf)});
}

/*
 * A second report, raised from inside the strip write. Tells apart "the wrong
 * value was computed" from "the right value was computed and something else
 * reached the strip afterwards".
 */
void rgbzone_diag_report_write(uint32_t packed, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                               uint8_t g1, uint8_t b1) {
    if (!report_enabled) {
        return;
    }

    static uint8_t wbuf[CONFIG_RAW_HID_REPORT_SIZE];
    static const uint8_t W_MAGIC[] = {'Z', 'M', 'K', 'W', 'R', '8', '!'};

    memset(wbuf, 0, sizeof(wbuf));
    memcpy(wbuf, W_MAGIC, sizeof(W_MAGIC));
    uint8_t *p = wbuf + sizeof(W_MAGIC);
    p[0] = packed & 0xFF;
    p[1] = (packed >> 8) & 0xFF;
    p[2] = (packed >> 16) & 0xFF;
    p[3] = (packed >> 24) & 0xFF;
    p[4] = r0;
    p[5] = g0;
    p[6] = b0;
    p[7] = r1;
    p[8] = g1;
    p[9] = b1;

    raise_raw_hid_sent_event(
        (struct raw_hid_sent_event){.data = wbuf, .length = sizeof(wbuf)});
}

#define RGB_CMD_REPORT 4

static int rgbzone_diag_listener(const zmk_event_t *eh) {
    const struct raw_hid_received_event *ev = as_raw_hid_received_event(eh);
    if (ev == NULL || ev->data == NULL || ev->length < RGB_MIN_LEN) {
        return ZMK_EV_EVENT_BUBBLE;
    }
    if (memcmp(ev->data, RGB_MAGIC, RGB_MAGIC_LEN) != 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    const uint8_t *p = ev->data + RGB_MAGIC_LEN;
    uint8_t half = p[0];
    uint8_t cmd = p[1];
    uint32_t arg = p[2] | ((uint32_t)p[3] << 8) | ((uint32_t)p[4] << 16) | ((uint32_t)p[5] << 24);

    LOG_WRN("RGB diag: half=%d cmd=%d arg=%08x", half, cmd, arg);

    if (cmd == RGB_CMD_REPORT) {
        report_enabled = (arg & 0xFF) != 0;
        LOG_WRN("RGB state reporting %s", report_enabled ? "on" : "off");
    } else if (half == RGB_HALF_LEFT) {
        diag_dispatch(cmd, arg);
    } else if (half == RGB_HALF_RIGHT) {
        send_remote(cmd, arg);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(rgbzone_diag, rgbzone_diag_listener);
ZMK_SUBSCRIPTION(rgbzone_diag, raw_hid_received_event);

#else

/* Builds without raw HID have nowhere to report to; keep the caller simple. */
void rgbzone_diag_report_write(uint32_t packed, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1,
                               uint8_t g1, uint8_t b1) {
    ARG_UNUSED(packed);
    ARG_UNUSED(r0);
    ARG_UNUSED(g0);
    ARG_UNUSED(b0);
    ARG_UNUSED(r1);
    ARG_UNUSED(g1);
    ARG_UNUSED(b1);
}

void rgbzone_diag_report(uint8_t layer_index, const char *layer_name, uint8_t mods,
                         uint32_t packed_left, uint32_t packed_right) {
    ARG_UNUSED(layer_index);
    ARG_UNUSED(layer_name);
    ARG_UNUSED(mods);
    ARG_UNUSED(packed_left);
    ARG_UNUSED(packed_right);
}

#endif
