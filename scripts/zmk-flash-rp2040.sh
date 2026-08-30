#!/usr/bin/env bash
#
# Flash a Corne v4 half that is sitting in its RP2040 BOOTSEL bootloader.
#
# Companion to zmk-flash.sh, which handles the nRF52840 keyboards (eyelash,
# Rolio, Toucan). The two never collide: this one takes only the RPI-RP2 volume
# and that one takes only NICENANO / ROLIO-BOOT / XIAO-*. Use whichever matches
# the keyboard in hand.
#
#   ./zmk-flash-rp2040.sh --left           flash the left half
#   ./zmk-flash-rp2040.sh --right          flash the right half
#   ./zmk-flash-rp2040.sh --list           show what is staged, plugged in, known
#   ./zmk-flash-rp2040.sh --watch --left   wait for the volume, then flash
#   ./zmk-flash-rp2040.sh --left --debug   the same, but the -debug image
#   ./zmk-flash-rp2040.sh --left --rgbkey   per-key lighting
#   ./zmk-flash-rp2040.sh --left --rgbkey-debug   lighting + console
#   ./zmk-flash-rp2040.sh --left --dfu     the image carrying the trigger
#   ./zmk-flash-rp2040.sh --left --dfu --trigger    handsfree, and stays that way
#
# --debug picks corne_v4_<half>-debug.uf2: identical firmware plus a USB CDC
# console, for bringing up the wired split. Read it with `screen /dev/ttyACM0
# 115200` or `cat /dev/ttyACM0`. Flash ONE half at a time and leave the other on
# the interlink, which powers it -- never put both halves on USB at once.
#
# GETTING INTO THE BOOTLOADER
#
# HANDSFREE: --trigger, if the half is running a -dfu build. It asks the
# firmware over raw HID to reboot into the bootloader, then waits for the volume
# and writes -- no key held, no cable moved, nothing touched. There is a
# bootstrap: the -dfu image has to get on there once by hand first, and a flash
# that lands a NON-dfu image takes the trigger away again with it.
#
# --trigger --right relays through the central to the peripheral, which reaches
# BOOTSEL but presents no volume unless that half has a USB cable of its own; in
# the normal one-cable setup only the LEFT half can be flashed handsfree. That is
# the half a keymap change needs, so it covers most cycles.
#
# EASIEST: hold the second key of the top row (counting in from the outer pinky
# column) on the half you are plugging in. That is QMK bootmagic while the board
# still runs QMK, and the zmk,boot-key node does the same thing once ZMK is on
# it, so the gesture does not change across the port.
#
# Failing that, hold the BOOT button while plugging in USB, or -- already
# plugged in -- hold BOOT and tap RESET. BOOT is wired to the RP2040's mask ROM,
# so it works with no firmware, bad firmware, or none at all, which makes it the
# true last resort. On this keyboard it is not reachable with the case on, hence
# the key gesture above.
#
# Do NOT expect double-tap-reset to work: that is a QMK feature and ZMK does not
# implement it on RP2040. ZMK's other soft route, the &bootloader binding, sits
# behind a leader sequence on the SYSTEM layer and only works on the half that
# is running the keymap.
#
# WHY IT ASKS WHICH HALF, AND ONLY CHECKS AFTERWARDS
#
# An RP2040 in BOOTSEL says nothing about which half of which keyboard it is:
# the firmware that knew is not running. This script originally tried to learn
# that from the USB serial and refuse a disagreement. That does not work -- both
# halves of this keyboard present the SAME ID_SERIAL_SHORT in BOOTSEL, and the
# guard simply refused the second half outright (jjb, 2026-08-29). Whether that
# is the bootrom or a udev fallback hardly matters; it does not distinguish the
# boards, so nothing may be built on it.
#
# So --left/--right is taken on trust, and the check moved to AFTER the write,
# where there is a real signal: the two images set different
# CONFIG_ZMK_KEYBOARD_NAME, so the name the board enumerates under says which
# image it is actually running. That catches a swap instead of guessing at one,
# and it cannot dead-end you the way the old check did.
#
# A wrong image here is recoverable in any case: the boot-key gesture is bound
# to a physical key position in both builds, so the board can always be put back
# into BOOTSEL and reflashed.
#
# CABLE ORDER -- THIS ONE CAN DESTROY HARDWARE
#
# THE INTERLINK ON THIS BOARD IS USB-C, NOT A TRRS JACK (jjb, 2026-08-30), so
# the usual "never hot-plug it" rule does not apply -- that warning belongs to
# the nRF boards with 3.5mm jacks, where sliding a TRRS plug in shorts ring to
# sleeve on the way past and can take the MCU with it. USB-C makes contact in a
# defined order and is built to be hot-plugged.
#
# It is still the thing to suspect FIRST when a half goes quiet. A marginal
# contact here is silent in both directions and survives a power cycle, which
# makes it look exactly like wedged firmware; see the split-link notes.

set -uo pipefail

# Under ~/tmp, not straight in ~: staging has to survive a reboot -- a half can
# sit in its bootloader across one -- so /tmp will not do, but that is no reason
# to leave a directory in the home directory of every machine a keyboard visits.
STAGING="$HOME/tmp/zmk-flash"
MOUNT=/mnt/keeb
FLASH_LOG="$STAGING/rp2040-flashes.log"

# The RP2040 mask-ROM bootloader's volume label. Fixed by Raspberry Pi, not by
# the keyboard, so every RP2040 in BOOTSEL looks like this.
BOOT_LABEL="RPI-RP2"
BOOT_BOARD_ID="RPI-RP2"

# The raw-HID trigger lives beside this script when staged, and beside it in
# the repo. Only the CENTRAL advertises the raw interface, so both targets are
# aimed at the same device name; --name is required because the eyelash halves
# on this machine advertise the same vendor usage page and sort ahead of it.
DFU_TRIGGER="$(dirname "$0")/dfu-trigger.py"
DFU_NAME="Corne v4"
# The firmware reboots about a quarter of a second after the write; give the
# volume time to enumerate before the watch starts looking for it.
DFU_SETTLE_SECS=3

WATCH_DEFAULT_SECS=300
# The volume appears a moment before its filesystem is readable.
SETTLE_SECS=2
# How long to wait for the board to reboot out of BOOTSEL after writing.
REBOOT_WAIT_SECS=5

usage() {
    sed -n '2,/^set -uo/p' "$0" | sed 's/^# \{0,1\}//; $d'
    exit "${1:-0}"
}

find_bootloader() {
    lsblk -rno NAME,LABEL | awk -v l="$BOOT_LABEL" '$2==l{print $1; exit}'
}

serial_of() {
    udevadm info --query=property --name="/dev/$1" \
        | sed -n 's/^ID_SERIAL_SHORT=//p' | head -1
}

# The USB product string each image enumerates under, from the two shield confs.
expected_name() {
    case "$1" in
        left)  echo "Corne v4" ;;
        right) echo "Corne v4 right" ;;
    esac
}

# Log what was flashed, and what serial the bootloader claimed while we did it.
# Kept purely as a record -- nothing branches on the serial, because on this
# hardware it does not distinguish the halves. If a future board turns out to
# report distinct serials, this file is where the evidence will be.
record_flash() {
    mkdir -p "$STAGING"
    printf '%s  %-5s  bootsel-serial=%s\n' "$(date -Is)" "$1" "$2" >> "$FLASH_LOG"
}

# Look for a USB device presenting the given product string.
usb_product_present() {
    local want="$1" p
    for p in /sys/bus/usb/devices/*/product; do
        [ -r "$p" ] || continue
        if [ "$(cat "$p" 2>/dev/null)" = "$want" ]; then
            return 0
        fi
    done
    return 1
}

HALF=""
WATCH=""
WATCH_SECS=""
VARIANT=""
TRIGGER=""

while [ $# -gt 0 ]; do
    case "$1" in
        --left)  HALF=left ;;
        --right) HALF=right ;;
        --list)  HALF=list ;;
        --debug) VARIANT="-debug" ;;
        --dfu) VARIANT="-dfu" ;;
        --rgbkey) VARIANT="-rgbkey" ;;
        --rgbkey-debug) VARIANT="-rgbkey-debug" ;;
        --trigger) TRIGGER=1 ;;
        --watch) WATCH=1
                 case "${2:-}" in [0-9]*) WATCH_SECS="$2"; shift ;; esac ;;
        -h|--help) usage 0 ;;
        *) echo "Unknown argument: $1" >&2; usage 1 ;;
    esac
    shift
done

if [ "$HALF" = list ]; then
    echo "Staged in $STAGING:"
    ls -1 "$STAGING"/corne_v4_*.uf2 2>/dev/null | sed 's|.*/|  |' || echo "  (nothing)"
    echo
    echo "Running now:"
    for h in left right; do
        n=$(expected_name "$h")
        if usb_product_present "$n"; then
            echo "  $h half  -- enumerated as \"$n\""
        fi
    done
    usb_product_present "$(expected_name left)" \
        || usb_product_present "$(expected_name right)" \
        || echo "  (neither half is enumerated)"
    echo
    echo "Recent flashes:"
    if [ -s "$FLASH_LOG" ]; then
        tail -5 "$FLASH_LOG" | sed 's/^/  /'
    else
        echo "  (none recorded)"
    fi
    echo
    echo "In bootloader now:"
    dev=$(find_bootloader)
    if [ -z "$dev" ]; then
        echo "  (nothing -- hold the top row's second key in from the outer pinky,"
        echo "   on the half you want, while plugging that half in)"
    else
        echo "  /dev/$dev  bootsel serial $(serial_of "$dev")"
        echo "  (the serial does not say which half this is -- see the header)"
    fi
    exit 0
fi

if [ -z "$HALF" ]; then
    echo "Say which half: --left or --right (or --list to look around)." >&2
    echo "The bootloader cannot tell you -- see the header of this script." >&2
    exit 1
fi

flash_it() {
    local dev="$1" serial fw board want

    serial=$(serial_of "$dev")

    fw="$STAGING/corne_v4_${HALF}${VARIANT}.uf2"
    if [ ! -f "$fw" ]; then
        echo "REFUSING: nothing staged at $fw"
        return 1
    fi

    echo "Corne v4 $HALF  (bootsel serial ${serial:-unreadable})"
    echo "  -> $(basename "$fw")"

    # udisksctl needs polkit and a controlling terminal, neither of which exists
    # over a non-interactive ssh; mount directly.
    sudo mkdir -p "$MOUNT"
    mountpoint -q "$MOUNT" && sudo umount "$MOUNT"
    sudo mount -t vfat "/dev/$dev" "$MOUNT" || {
        echo "Could not mount /dev/$dev"; return 1; }

    board=$(sed -n 's/^Board-ID: *//p' "$MOUNT/INFO_UF2.TXT" 2>/dev/null | tr -d '\r')
    case "$board" in
        *"$BOOT_BOARD_ID"*) ;;
        *) echo "REFUSING: unexpected Board-ID '${board:-none}' -- this is not an RP2040 in BOOTSEL."
           sudo umount "$MOUNT" 2>/dev/null
           return 1 ;;
    esac

    # The RP2040 bootloader takes a UF2 under any name and reboots the instant
    # the last block lands -- so the volume disappears out from under cp, and cp
    # and sync both report failure on a write that in fact succeeded. Judge it
    # by whether the board left BOOTSEL, never by the exit status here.
    sudo cp "$fw" "$MOUNT/" 2>/dev/null
    sync 2>/dev/null

    sleep "$REBOOT_WAIT_SECS"
    mountpoint -q "$MOUNT" && sudo umount "$MOUNT" 2>/dev/null

    if [ -n "$(find_bootloader)" ]; then
        echo "WARNING: something is still in BOOTSEL. The write may not have taken."
        echo "Check 'lsusb' for a new device before re-flashing."
        return 1
    fi

    record_flash "$HALF" "${serial:-unreadable}"
    echo "Written -- it left BOOTSEL, which is how a successful write ends."

    # Now the part the bootloader could not tell us: the two images enumerate
    # under different names, so whatever comes back says which image is running.
    want=$(expected_name "$HALF")
    if usb_product_present "$want"; then
        echo "Confirmed: enumerated as \"$want\", so the $HALF image is on the $HALF half."
        return 0
    fi

    local other
    other=$(expected_name "$([ "$HALF" = left ] && echo right || echo left)")
    if usb_product_present "$other"; then
        echo "WARNING: this board came back as \"$other\", not \"$want\"."
        echo "The halves are the other way round from what you told me. Put this"
        echo "one back in BOOTSEL and flash it with the other flag."
        return 1
    fi

    echo "NOTE: nothing has enumerated as \"$want\" yet."
    echo "The peripheral half is slower to appear, and a half whose firmware does"
    echo "not come up will never appear at all. Re-run --list in a moment."
    return 0
}

watch_and_flash() {
    local secs="${1:-$WATCH_DEFAULT_SECS}" waited=0 dev=""

    echo "Watching for an $BOOT_LABEL volume (up to ${secs}s)."
    echo "Hold the top row's second key in from the outer pinky on the half you"
    echo "want, then plug that half in."
    while [ "$waited" -lt "$secs" ]; do
        dev=$(find_bootloader)
        if [ -n "$dev" ]; then
            sleep "$SETTLE_SECS"
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

    echo "Timed out after ${secs}s with nothing in BOOTSEL."
    return 1
}

# Ask the running firmware to reboot into the bootloader, then flash it. The
# half must already be on a -dfu build; if it is not, nothing answers and the
# watch below simply times out saying so.
if [ -n "$TRIGGER" ]; then
    if [ ! -x "$DFU_TRIGGER" ]; then
        echo "REFUSING: no dfu-trigger.py beside this script ($DFU_TRIGGER)." >&2
        exit 1
    fi
    echo "Asking the $HALF half to reboot into the bootloader..."
    if ! "$DFU_TRIGGER" "$HALF" --name "$DFU_NAME"; then
        echo "The trigger did not go through. Is this half on a -dfu build?" >&2
        exit 1
    fi
    sleep "$DFU_SETTLE_SECS"
    watch_and_flash "${WATCH_SECS:-60}"
    exit $?
fi

if [ -n "$WATCH" ]; then
    watch_and_flash "${WATCH_SECS:-}"
    exit $?
fi

dev=$(find_bootloader)
if [ -z "$dev" ]; then
    echo "Nothing in BOOTSEL. Hold the BOOT button and plug in USB (or hold BOOT"
    echo "and tap RESET), then run this again -- or use --watch, which waits."
    exit 1
fi

flash_it "$dev"
exit $?
