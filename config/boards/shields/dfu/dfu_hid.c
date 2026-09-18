/*
 * TEMPORARY: put either half into the bootloader on a command from the host.
 *
 * REMOVE THIS WHEN THE CURRENT DISPLAY/RGB WORK ENDS. It is a remote path into
 * a writable state on a keyboard that types passwords, and it only works if it
 * is present in the firmware being flashed *in*, so every build carries it
 * until it is deliberately taken out.
 *
 * It needs no bootloader code of its own. ZMK's &bootloader behaviour (node
 * `bootload`) already sets the boot mode and reboots, and it declares
 * BEHAVIOR_LOCALITY_EVENT_SOURCE -- meaning ZMK dispatches it to whichever
 * half the event claims to have come from. So invoking it with event.source
 * set to LOCAL reboots the central, and setting it to a peripheral index
 * relays it to that half. `bootload` is also eight characters, which matters:
 * the BLE split payload carries a behaviour name in a nine-byte field and
 * silently truncates anything longer.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

#include <raw_hid/events.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * A deliberate, unlikely prefix. Raw HID is a shared channel and anything on
 * the host can write to it; a short or guessable command would mean stray
 * traffic could reboot the keyboard mid-sentence.
 */
static const uint8_t DFU_MAGIC[] = {'Z', 'M', 'K', 'D', 'F', 'U', '!'};
#define DFU_MAGIC_LEN sizeof(DFU_MAGIC)

/* magic, then target, then a checksum over both. */
#define DFU_OFF_TARGET (DFU_MAGIC_LEN)
#define DFU_OFF_CHECK  (DFU_MAGIC_LEN + 1)
#define DFU_MIN_LEN    (DFU_MAGIC_LEN + 2)

#define DFU_TARGET_CENTRAL 0x00

/*
 * 0x01 is the first peripheral (split source 0), 0x02 the second, and so on.
 * An ordinary split has one; a dongle such as the OP36's Qube has one per half.
 * Source numbers follow bond order, not side -- whichever half lands in its
 * bootloader, the flash script picks its image by serial.
 */
#define DFU_TARGET_PERIPH_FIRST 0x01

#if defined(CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS)
#define DFU_PERIPHERAL_COUNT CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS
#elif IS_ENABLED(CONFIG_ZMK_SPLIT)
#define DFU_PERIPHERAL_COUNT 1
#else
/* Not a split: there is nothing to relay to, only this device itself. */
#define DFU_PERIPHERAL_COUNT 0
#endif

/*
 * Deferred rather than done inline: this arrives on the USB HID callback's
 * thread, and rebooting out from under it is a poor idea. The delay also lets
 * the host see the command acknowledged before the device drops off the bus.
 */
static uint8_t dfu_target;

static void dfu_fire(struct k_work *work) {
    ARG_UNUSED(work);

    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(bootloader)),
    };
    struct zmk_behavior_binding_event event = {
        .position = 0,
        .timestamp = k_uptime_get(),
    };

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    /*
     * The event only carries a source on a split build. Anywhere else the
     * listener has already rejected every target but the central.
     */
    const bool central = dfu_target == DFU_TARGET_CENTRAL;
    event.source = central ? ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL
                           : (uint8_t)(dfu_target - DFU_TARGET_PERIPH_FIRST);
    if (!central) {
        LOG_WRN("DFU requested over raw HID for peripheral source %d", event.source);
    } else
#endif
    {
        LOG_WRN("DFU requested over raw HID for the central");
    }

    int err = zmk_behavior_invoke_binding(&binding, event, true);
    if (err) {
        LOG_ERR("Failed to invoke bootloader behaviour: %d", err);
    }
}

static K_WORK_DELAYABLE_DEFINE(dfu_work, dfu_fire);

static int dfu_hid_listener(const zmk_event_t *eh) {
    const struct raw_hid_received_event *ev = as_raw_hid_received_event(eh);
    if (ev == NULL || ev->data == NULL || ev->length < DFU_MIN_LEN) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (memcmp(ev->data, DFU_MAGIC, DFU_MAGIC_LEN) != 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    uint8_t target = ev->data[DFU_OFF_TARGET];
    uint8_t check = ev->data[DFU_OFF_CHECK];

    uint8_t expect = target;
    for (size_t i = 0; i < DFU_MAGIC_LEN; i++) {
        expect ^= DFU_MAGIC[i];
    }
    if (check != expect) {
        LOG_WRN("DFU command rejected: bad checksum");
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (target != DFU_TARGET_CENTRAL &&
        (target < DFU_TARGET_PERIPH_FIRST ||
         target >= DFU_TARGET_PERIPH_FIRST + DFU_PERIPHERAL_COUNT)) {
        LOG_WRN("DFU command rejected: unknown target %d", target);
        return ZMK_EV_EVENT_BUBBLE;
    }

    dfu_target = target;
    k_work_reschedule(&dfu_work, K_MSEC(250));

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(dfu_hid, dfu_hid_listener);
ZMK_SUBSCRIPTION(dfu_hid, raw_hid_received_event);
