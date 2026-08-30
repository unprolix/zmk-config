/*
 * THIS IS THE FILE TO EDIT: which host is on which bluetooth profile.
 *
 * Shared. Both custom screens build it -- nice_view_jjb for the eyelash and
 * vista508 for the Rolio and Toucan -- so one table names hosts on every
 * keyboard, which is the point: they are the same hosts.
 *
 * The keyboard stores a bonded host's address and nothing else -- ZMK reserves
 * a name field per profile and never fills it -- so "BT2" is all the screen can
 * honestly say. This puts a name to the address.
 *
 * Addresses are matched on the six bytes only, so write them however
 * bluetoothctl prints them; the "(public)" or "(random)" suffix is ignored.
 * Case does not matter. To find one, hold the bluetooth layer with the host
 * connected: the screen shows the tail of the address for any profile it
 * cannot name.
 *
 * Names are drawn in a 68-pixel column, so about nine characters land. Longer
 * ones are not an error, they are just cut.
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

struct jjb_bt_name {
    /* "AA:BB:CC:DD:EE:FF", any case, suffix optional. */
    const char *addr;
    const char *name;
};

static const struct jjb_bt_name jjb_bt_names[] = {
    /* Fill these in as you pair. Until then a profile shows its address tail,
       which is exactly what you need to write the entry. */
    /* {"AA:BB:CC:DD:EE:FF", "quignon"}, */
};
