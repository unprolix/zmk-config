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
#   ./zmk-flash.sh          flash what is in bootloader
#   ./zmk-flash.sh --list    show what is staged and what is plugged in
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
        *) return 1 ;;
    esac
}

find_bootloader() {
    # NICENANO is the eyelash's; ROLIO-BOOT is the Rolio's. Matching on the
    # label rather than on size keeps a stray USB stick out of the running.
    lsblk -rno NAME,LABEL | awk '$2=="NICENANO" || $2=="ROLIO-BOOT"{print $1; exit}'
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

dev=$(find_bootloader)
if [ -z "$dev" ]; then
    echo "Nothing in bootloader. Press the FUNCTION-layer &bootloader key on the"
    echo "half you want, or double-tap its reset button, then run this again."
    exit 1
fi

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
mountpoint -q "$MOUNT" || sudo mount -t vfat "/dev/$dev" "$MOUNT" || {
    echo "Could not mount /dev/$dev"; exit 1; }

board=$(sed -n 's/^Board-ID: *//p' "$MOUNT/INFO_UF2.TXT" 2>/dev/null | tr -d '\r')
case "$board" in
    *nicenano*|*nRF52840*) ;;
    *) echo "REFUSING: unexpected Board-ID '${board:-none}'"
       sudo umount "$MOUNT" 2>/dev/null
       exit 1 ;;
esac

sudo cp "$fw" "$MOUNT/CURRENT.UF2"
sync
# The volume usually vanishes as the board reboots; if it lingers, clear it so
# the next run does not find a stale mount.
sleep 1
mountpoint -q "$MOUNT" && sudo umount "$MOUNT" 2>/dev/null
echo "Done. It should reboot into the new firmware."
