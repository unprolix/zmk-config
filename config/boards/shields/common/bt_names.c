/*
 * Naming the host on a bluetooth profile.
 *
 * Two sources, tried in order. A host that answers a GATT read of its own
 * Device Name names itself, which costs nothing to maintain; one that does not
 * -- and some hosts refuse, or refuse until encrypted -- falls back to the
 * table in bt_names.h. Failing both, the tail of the address, which is at
 * least distinct per host and is what you need in order to write the entry.
 *
 * Learned names are kept in settings, with the address they were learned from.
 * Only the connected host can answer, so holding them in RAM alone would mean
 * that after every reboot the list -- the thing this exists for -- showed one
 * name and four addresses until each host had been switched to in turn.
 *
 * Storing the address alongside is what keeps them honest: a profile that has
 * been re-paired to a different machine does not match, so the old name is
 * ignored rather than shown against the wrong host. A machine that is merely
 * renamed corrects itself on its next connect.
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>
#include <stdio.h>
#include <string.h>

#include <zmk/ble.h>

#include "bt_names.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Overridden by a screen that wants to repaint when a name arrives. */
__weak void jjb_bt_name_changed(void) {}

/* ZMK reserves fifteen; the panel fits nine. Keep ZMK's, cut when drawing. */
#define JJB_BT_NAME_MAX 16

/* "34:56" plus a terminator. */
#define JJB_BT_TAIL_MAX 6

struct learned_name {
    bt_addr_le_t addr;
    char name[JJB_BT_NAME_MAX];
};

static struct learned_name learned[ZMK_BLE_PROFILE_COUNT];
static char tails[ZMK_BLE_PROFILE_COUNT][JJB_BT_TAIL_MAX];

#define SETTINGS_ROOT "jjbbt"

static void learned_save(uint8_t profile) {
#if IS_ENABLED(CONFIG_SETTINGS)
    char key[32];
    snprintf(key, sizeof(key), SETTINGS_ROOT "/%d", profile);
    int ret = settings_save_one(key, &learned[profile], sizeof(learned[profile]));
    if (ret < 0) {
        LOG_WRN("Could not save name for profile %d: %d", profile, ret);
    }
#endif
}

static bool addr_is_set(const bt_addr_le_t *addr) {
    return addr != NULL && bt_addr_le_cmp(addr, BT_ADDR_LE_ANY) != 0;
}

/* Compare only the six address bytes, so the table can be written in whatever
   form the host tool prints -- with or without a type suffix. */
static bool addr_matches(const bt_addr_le_t *addr, const char *text) {
    if (text == NULL) {
        return false;
    }

    unsigned int b[6];
    if (sscanf(text, "%2x:%2x:%2x:%2x:%2x:%2x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }

    /* bt_addr_le_to_str prints most-significant first; a bt_addr_le_t stores
       least-significant first. */
    for (int i = 0; i < 6; i++) {
        if (addr->a.val[5 - i] != (uint8_t)b[i]) {
            return false;
        }
    }
    return true;
}

const char *jjb_bt_name_for(uint8_t profile) {
    if (profile >= ZMK_BLE_PROFILE_COUNT) {
        return NULL;
    }

    const bt_addr_le_t *addr = zmk_ble_profile_address(profile);
    if (!addr_is_set(addr)) {
        return NULL;
    }

    /* The table wins: it is what you said, and a host is free to call itself
       something useless like "Bluetooth Device". */
    for (size_t i = 0; i < ARRAY_SIZE(jjb_bt_names); i++) {
        if (addr_matches(addr, jjb_bt_names[i].addr)) {
            return jjb_bt_names[i].name;
        }
    }

    /* Only if it was learned from the machine that is on this profile now. */
    if (learned[profile].name[0] != '\0' &&
        bt_addr_le_cmp(&learned[profile].addr, addr) == 0) {
        return learned[profile].name;
    }

    snprintf(tails[profile], sizeof(tails[profile]), "%02x:%02x", addr->a.val[1], addr->a.val[0]);
    return tails[profile];
}

/* ------------------------------------------------------------------ */
/* Asking the host what it is called                                   */
/* ------------------------------------------------------------------ */

static struct bt_gatt_read_params read_params;
static struct bt_gatt_discover_params discover_params;
static uint8_t reading_profile;
static bool read_in_flight;

static uint8_t name_read_cb(struct bt_conn *conn, uint8_t err, struct bt_gatt_read_params *params,
                            const void *data, uint16_t length) {
    read_in_flight = false;

    if (err != 0 || data == NULL || length == 0 || reading_profile >= ZMK_BLE_PROFILE_COUNT) {
        return BT_GATT_ITER_STOP;
    }

    char *name = learned[reading_profile].name;
    size_t n = MIN(length, (uint16_t)(JJB_BT_NAME_MAX - 1));
    memcpy(name, data, n);
    name[n] = '\0';

    /* A name that is only spaces or control characters is worse than none. */
    for (size_t i = 0; i < n; i++) {
        if ((unsigned char)name[i] < 0x20) {
            name[0] = '\0';
            return BT_GATT_ITER_STOP;
        }
    }

    const bt_addr_le_t *addr = zmk_ble_profile_address(reading_profile);
    if (addr == NULL) {
        name[0] = '\0';
        return BT_GATT_ITER_STOP;
    }
    memcpy(&learned[reading_profile].addr, addr, sizeof(bt_addr_le_t));

    LOG_INF("Profile %d is \"%s\"", reading_profile, name);
    learned_save(reading_profile);

    /*
     * Tell whoever is drawing. This lands a moment AFTER the profile connected,
     * so a screen that only repaints on connection events will already have
     * settled on the address tail and would keep showing it indefinitely.
     *
     * Weak, because not every screen wants to know and the eyelash's does not
     * implement it yet. Called from the BLE stack's context -- an implementation
     * must not touch LVGL directly.
     */
    jjb_bt_name_changed();
    return BT_GATT_ITER_STOP;
}

static uint8_t discover_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                           struct bt_gatt_discover_params *params) {
    if (attr == NULL) {
        read_in_flight = false;
        return BT_GATT_ITER_STOP;
    }

    read_params.func = name_read_cb;
    read_params.handle_count = 1;
    read_params.single.handle = bt_gatt_attr_value_handle(attr);
    read_params.single.offset = 0;

    if (bt_gatt_read(conn, &read_params) != 0) {
        read_in_flight = false;
    }
    return BT_GATT_ITER_STOP;
}

/*
 * Started from the security callback rather than from connect: several hosts
 * refuse to serve their own name until the link is encrypted, and asking early
 * gets a permission error rather than a retry.
 */
static void security_changed(struct bt_conn *conn, bt_security_t level, enum bt_security_err err) {
    if (err != BT_SECURITY_ERR_SUCCESS || level < BT_SECURITY_L2 || read_in_flight) {
        return;
    }

    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) != 0 || info.role != BT_CONN_ROLE_PERIPHERAL) {
        return; /* The split peripheral half, not a host. */
    }

    int profile = zmk_ble_profile_index(bt_conn_get_dst(conn));
    if (profile < 0 || profile >= ZMK_BLE_PROFILE_COUNT) {
        return;
    }

    /* Already known, and known to be this machine. Asking again would only
       catch a rename, which the next fresh pairing will catch anyway. */
    const bt_addr_le_t *addr = zmk_ble_profile_address(profile);
    if (addr != NULL && learned[profile].name[0] != '\0' &&
        bt_addr_le_cmp(&learned[profile].addr, addr) == 0) {
        return;
    }

    reading_profile = (uint8_t)profile;
    read_in_flight = true;

    discover_params.uuid = BT_UUID_GAP_DEVICE_NAME;
    discover_params.func = discover_cb;
    discover_params.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    discover_params.end_handle = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    discover_params.type = BT_GATT_DISCOVER_CHARACTERISTIC;

    if (bt_gatt_discover(conn, &discover_params) != 0) {
        read_in_flight = false;
    }
}

static void disconnected(struct bt_conn *conn, uint8_t reason) { read_in_flight = false; }

static struct bt_conn_cb conn_callbacks = {
    .security_changed = security_changed,
    .disconnected = disconnected,
};

#if IS_ENABLED(CONFIG_SETTINGS)

static int learned_load(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    unsigned int profile;
    if (sscanf(name, "%u", &profile) != 1 || profile >= ZMK_BLE_PROFILE_COUNT) {
        return -ENOENT;
    }
    if (len != sizeof(struct learned_name)) {
        return -EINVAL;
    }
    if (read_cb(cb_arg, &learned[profile], sizeof(struct learned_name)) < 0) {
        learned[profile].name[0] = '\0';
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(jjb_bt_names, SETTINGS_ROOT, NULL, learned_load, NULL, NULL);

#endif /* IS_ENABLED(CONFIG_SETTINGS) */

static int jjb_bt_names_init(void) {
    bt_conn_cb_register(&conn_callbacks);
    return 0;
}

SYS_INIT(jjb_bt_names_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
