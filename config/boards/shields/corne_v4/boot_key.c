/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * Hold one key while the board powers up and it reboots into the bootloader.
 *
 * This exists because the Corne v4's BOOT and RESET buttons are not reachable
 * with the case on, and every other route into the bootloader needs something
 * to be working:
 *
 *   &bootloader keymap binding   needs the keymap, and on this build it sits
 *                                behind a leader sequence on SYSTEM
 *   raw-HID DFU trigger          RAW_HID is `depends on ... ZMK_SPLIT_ROLE_
 *                                CENTRAL`, so it does not exist on the
 *                                peripheral; it reaches that half by relaying
 *                                through the split link
 *   QMK bootmagic                gone the moment ZMK replaces QMK
 *
 * Which leaves a peripheral whose split link does not work with no way back at
 * all -- and the split link is the least proven part of this port. So: read one
 * GPIO at startup, before the keymap, the split transport or USB exist, and
 * act on it. It works on firmware that is broken in every other respect.
 *
 * The key is deliberately the same physical one QMK used for bootmagic on this
 * board (info.json `bootmagic.matrix` [0,1] left, [4,1] right -- the second key
 * of the top row, counting in from the outer pinky column, on each half), so
 * the gesture that worked before the port still works after it.
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

#include <zephyr/retention/bootmode.h>

#define DT_DRV_COMPAT zmk_boot_key

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/*
 * Sampled rather than read once: the pin has just been given its pull-up, and a
 * key held down through a power-up is not a clean edge. Every sample must agree
 * before this throws the board into the bootloader, because a false positive
 * here looks exactly like a keyboard that will not boot.
 */
#define SAMPLE_COUNT 5
#define SAMPLE_INTERVAL_MS 4

static const struct gpio_dt_spec boot_key = GPIO_DT_SPEC_INST_GET(0, gpios);

static int boot_key_check(void) {
    int ret;

    if (!gpio_is_ready_dt(&boot_key)) {
        LOG_ERR("Boot key GPIO not ready; skipping the check");
        return 0;
    }

    ret = gpio_pin_configure_dt(&boot_key, GPIO_INPUT);
    if (ret < 0) {
        LOG_ERR("Failed to configure the boot key (%d)", ret);
        return 0;
    }

    for (int i = 0; i < SAMPLE_COUNT; i++) {
        if (gpio_pin_get_dt(&boot_key) != 1) {
            /* Not held. The overwhelmingly common case: carry on booting. */
            return 0;
        }
        k_msleep(SAMPLE_INTERVAL_MS);
    }

    LOG_WRN("Boot key held; rebooting into the bootloader");

    ret = bootmode_set(BOOT_MODE_TYPE_BOOTLOADER);
    if (ret < 0) {
        /*
         * Nothing useful left to do -- without the boot mode set, rebooting
         * would just come back here and hold the board in a loop. Boot normally
         * and let the user reach for a real button.
         */
        LOG_ERR("Failed to set the bootloader boot mode (%d)", ret);
        return 0;
    }

    sys_reboot(SYS_REBOOT_WARM);

    return 0;
}

/*
 * First thing in APPLICATION, which puts it ahead of ZMK's own init (ZMK uses
 * CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, a larger number, and larger runs later).
 * The retention area this needs is already up by this level, and none of ZMK
 * has started, so a board that faults anywhere later in startup is still
 * recoverable.
 */
#define BOOT_KEY_INIT_PRIORITY 0

SYS_INIT(boot_key_check, APPLICATION, BOOT_KEY_INIT_PRIORITY);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
