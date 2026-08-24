#!/usr/bin/env bash
#
# Flash whichever keyboard half is currently in its bootloader.
#
# Takes no arguments: it finds the bootloader volume, reads the nRF52840 serial,
# and picks the matching firmware from ~/zmk-flash. Anything whose serial is
# not in the table below is refused rather than guessed at -- several different
# keyboards get plugged into these machines, and a Rolio once turned up where
# an eyelash was expected.
#
#   ./zmk-flash.sh            flash what is in bootloader
#   ./zmk-flash.sh --list      show what is staged and what is plugged in
#   ./zmk-flash.sh --watch [s] wait for a bootloader volume, then flash it
#
# Put the half in bootloader first: the FUNCTION-layer &bootloader key works on
# whichever half it is pressed on, or double-tap that half's reset button.
#
# On firmware carrying the (temporary) dfu shield, dfu-trigger.py does it from
# here without touching the keyboard -- including relaying to the peripheral.
# Pass it --name when more than one device advertises usage page 0xFF60, which
# on this machine it does: a Ploopy trackball claims the same vendor page.

set -uo pipefail

STAGING="$HOME/zmk-flash"
MOUNT=/mnt/keeb

# serial -> firmware suffix, human name
serial_to_fw() {
    case "$1" in
        6361E3331604E096) echo "eyelash_corne_left_lavendre|Lavendre Left" ;;
        77A579C234F0B31D) echo "eyelash_corne_right_lavendre|Lavendre Right" ;;
        C6C6F3CDFE96B443) echo "eyelash_corne_left_bureau|Bureau Left" ;;
        1245D185DFA8A397) echo "eyelash_corne_right_bureau|Bureau Right" ;;
        E88C0775CF33E8B3) echo "eyelash_corne_left_salon|Salon Left" ;;
        9426C60A137338A0) echo "eyelash_corne_right_salon|Salon Right" ;;
        D13102971CB6A436) echo "eyelash_corne_left_fuligin|Fuligin Left" ;;
        A783532A83EAAD26) echo "eyelash_corne_right_fuligin|Fuligin Right" ;;
        B055E5020DDF4EA9) echo "eyelash_corne_left_xan|Xan Left" ;;
        4FF0B46AD2AF9292) echo "eyelash_corne_right_xan|Xan Right" ;;
        # The Rolio is not a nice!nano: its bootloader volume is ROLIO-BOOT,
        # and its firmware names carry no variant, so the suffixes differ in
        # shape from the eyelash ones above. Serials read off the hardware
        # 2026-08-23. These name the -dfu builds, which is what is on them; drop
        # the suffix once the raw-HID trigger is removed.
        536DB3A90D5D42F8) echo "rolio_left-dfu|Rolio Left" ;;
        C996C64AEB32F99C) echo "rolio_right-dfu|Rolio Right" ;;
        # The Toucan is a Seeed XIAO nRF52840: its bootloader volume is
        # XIAO-BOOT, not NICENANO. Serials read off the hardware 2026-08-15.
        # These name the -dfu builds, which is what is on them; drop the
        # suffix once the raw-HID trigger is removed.
        692C03EA68AC36B1) echo "toucan_left-dfu|Toucan Left" ;;
        F3A6B1329E98446C) echo "toucan_right-dfu|Toucan Right" ;;
        *) return 1 ;;
    esac
}

find_bootloader() {
    # NICENANO is the eyelash's; ROLIO-BOOT is the Rolio's; the Toucan (Seeed
    # XIAO nRF52840) uses XIAO-SENSE on the Sense variant and XIAO-BOOT on the
    # plain one, so take either rather than betting on which board is in hand.
    # Matching on the label rather than on size keeps a stray USB stick out.
    lsblk -rno NAME,LABEL | awk '$2=="NICENANO" || $2=="ROLIO-BOOT" || $2=="XIAO-SENSE" || $2=="XIAO-BOOT"{print $1; exit}'
}

if [ "${1:-}" = "--list" ]; then
    echo "Staged in $STAGING:"
    ls -1 "$STAGING"/*.uf2 2>/dev/null | sed 's|.*/|  |' || echo "  (nothing)"
    echo
    echo "In bootloader now:"
    dev=$(find_bootloader)
    if [ -z "$dev" ]; then
        echo "  (nothing -- press the bootloader key on the half you want)"
    else
        s=$(udevadm info --query=property --name="/dev/$dev" | sed -n 's/^ID_SERIAL_SHORT=//p' | head -1)
        if info=$(serial_to_fw "$s"); then
            echo "  /dev/$dev  ${info#*|}  ($s)"
        else
            echo "  /dev/$dev  UNKNOWN SERIAL $s -- will be refused"
        fi
    fi
    exit 0
fi

flash_it() {
    local dev="$1"
    serial=$(udevadm info --query=property --name="/dev/$dev" | sed -n 's/^ID_SERIAL_SHORT=//p' | head -1)

    if ! info=$(serial_to_fw "$serial"); then
        echo "REFUSING: /dev/$dev has serial $serial, which is not a known eyelash half."
        echo "Nothing was written. (A Rolio's bootloader volume is labelled ROLIO-BOOT,"
        echo "so it would not get this far, but other nice!nano boards would.)"
        exit 1
    fi

    prefix="${info%|*}"
    name="${info#*|}"
    fw="$STAGING/${prefix}-rgbzone.uf2"

    if [ ! -f "$fw" ]; then
        # The prefix may already name the artifact exactly -- the -dfu builds are
        # "rolio_left-dfu.uf2", with nothing after the prefix for the glob below to
        # match. Try the plain name before falling back to a search.
        [ -f "$STAGING/${prefix}.uf2" ] && fw="$STAGING/${prefix}.uf2"
    fi
    if [ ! -f "$fw" ]; then
        # Fall back to any staged build for this half, newest first.
        fw=$(ls -t "$STAGING/${prefix}"-*.uf2 2>/dev/null | head -1)
    fi
    if [ -z "$fw" ] || [ ! -f "$fw" ]; then
        echo "REFUSING: nothing staged for $name in $STAGING"
        exit 1
    fi

    echo "$name  ($serial)"
    echo "  -> $(basename "$fw")"

    # udisksctl needs polkit and a controlling terminal, neither of which exists
    # over a non-interactive ssh; mount directly.
    sudo mkdir -p "$MOUNT"

    # Always mount the device we just identified by serial, never trust whatever is
    # already at $MOUNT. A half that reboots while still mounted leaves the
    # mountpoint occupied by a volume that no longer exists, and skipping the mount
    # because "something is mounted there" reads the wrong thing entirely -- which
    # shows up as REFUSING: unexpected Board-ID 'none', with the correct half
    # plugged in and nothing wrong with it.
    mountpoint -q "$MOUNT" && sudo umount "$MOUNT"
    sudo mount -t vfat "/dev/$dev" "$MOUNT" || {
        echo "Could not mount /dev/$dev"; return 1; }

    board=$(sed -n 's/^Board-ID: *//p' "$MOUNT/INFO_UF2.TXT" 2>/dev/null | tr -d '\r')
    case "$board" in
        *nicenano*|*nRF52840*|*XIAO*|*xiao*) ;;
        *) echo "REFUSING: unexpected Board-ID '${board:-none}'"
           sudo umount "$MOUNT" 2>/dev/null
           return 1 ;;
    esac

    sudo cp "$fw" "$MOUNT/CURRENT.UF2"
    sync
    # The volume usually vanishes as the board reboots; if it lingers, clear it so
    # the next run does not find a stale mount.
    sleep 1
    mountpoint -q "$MOUNT" && sudo umount "$MOUNT" 2>/dev/null
    echo "Done. It should reboot into the new firmware."
}

WATCH_DEFAULT_SECS=300

# Wait for a bootloader volume, then flash it.
#
# For when the keyboard being flashed is the one you type on: start this first,
# then press the bootloader key. There is no second machine and no second
# keyboard to run a command from once the half has vanished from the bus.
watch_and_flash() {
    local secs="${1:-$WATCH_DEFAULT_SECS}" waited=0 dev=""

    echo "Watching for a bootloader volume (up to ${secs}s)."
    echo "Press the FUNCTION-layer &bootloader key now, or double-tap reset."
    while [ "$waited" -lt "$secs" ]; do
        dev=$(find_bootloader)
        if [ -n "$dev" ]; then
            # The volume appears a moment before its filesystem is readable.
            sleep 2
            echo
            flash_it "$dev"
            return $?
        fi
        sleep 1
        waited=$((waited + 1))
        if [ $((waited % 15)) -eq 0 ]; then
            printf '  ...still watching (%ds)\n' "$waited"
        fi
    done

    echo "Timed out after ${secs}s with nothing in bootloader."
    return 1
}

if [ "${1:-}" = "--watch" ]; then
    watch_and_flash "${2:-}"
    exit $?
fi

dev=$(find_bootloader)
if [ -z "$dev" ]; then
    echo "Nothing in bootloader. Press the FUNCTION-layer &bootloader key on the"
    echo "half you want, or double-tap its reset button, then run this again."
    echo "If this is the keyboard you type on, use --watch instead: it waits for"
    echo "the volume to appear and flashes it, so you need no keyboard after."
    exit 1
fi

flash_it "$dev"
exit $?
