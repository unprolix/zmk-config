#!/bin/bash

set -e

SCRIPT_DIR=$(dirname "$0")
FIRMWARE_DIR="$(dirname "$SCRIPT_DIR")/firmware"
MOUNT_POINT=~/mnt/keeb

usage() {
    echo "Usage: $0 [keyboard_name] [OPTIONS]"
    echo "  keyboard_name: sofle, corne, toucan, glove80, planck, zen, etc."
    echo ""
    echo "Options:"
    echo "  -l, --left     Flash left side of split keyboard"
    echo "  -r, --right    Flash right side of split keyboard"
    echo "  -h, --help     Show this help message"
    echo "  -v, --verbose  Verbose output"
    echo ""
    echo "If no keyboard name specified, will attempt to auto-detect"
    echo "If no side specified for split keyboard, will show available options"
    echo ""
    echo "Available firmware files:"
    find "$FIRMWARE_DIR" -name "*.uf2" -exec basename {} \; 2>/dev/null | sort || echo "  No firmware files found"
}

find_firmware_file() {
    local keyboard_name="$1"
    local side="$2"
    local firmware_file=""

    # No keyboard name: just take any .uf2 (sorted for stable order).
    if [ -z "$keyboard_name" ]; then
        firmware_file=$(find "$FIRMWARE_DIR" -name "*.uf2" | sort | head -1)
        [ -n "$firmware_file" ] && echo "$firmware_file" && return 0
        return 1
    fi

    local keyboard_lower
    keyboard_lower=$(echo "$keyboard_name" | tr '[:upper:]' '[:lower:]')

    # Substring match keyword against filenames (works whether the keyword is
    # a family like "corne" or a variant suffix like "salon").
    local candidates
    candidates=$(find "$FIRMWARE_DIR" -name "*${keyboard_lower}*.uf2" | sort)

    [ -z "$candidates" ] && return 1

    # If side is requested, filter to firmwares with _left_ / _right_ / -left- / -right- as a token.
    # This handles both <family>_<side>_<variant> and <variant>-<side>-* naming.
    if [ -n "$side" ]; then
        firmware_file=$(echo "$candidates" | grep -E "[_-]${side}[_.-]" | head -1)
        [ -n "$firmware_file" ] && echo "$firmware_file" && return 0
        return 1
    fi

    echo "$candidates" | head -1
    return 0
}

# Derive a recognizable bootloader family from the firmware filename.
# mount-device.py only knows hardcoded family patterns (corne/sofle/glove80/
# planck/zen); user-provided variants like "salon" don't match.
derive_bootloader_family() {
    local fname
    fname=$(basename "$1")
    case "$fname" in
        *toucan*)  echo "toucan" ;;
        *corne*)   echo "corne" ;;
        *sofle*)   echo "sofle" ;;
        *glove80*) echo "glove80" ;;
        *planck*)  echo "planck" ;;
        *zen*)     echo "zen" ;;
        *)         echo "" ;;
    esac
}

# Parse arguments
KEYBOARD_NAME=""
SIDE=""
VERBOSE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--help)
            usage
            exit 0
            ;;
        -v|--verbose)
            VERBOSE="--verbose"
            shift
            ;;
        -l|--left)
            SIDE="left"
            shift
            ;;
        -r|--right)
            SIDE="right"
            shift
            ;;
        *)
            if [ -z "$KEYBOARD_NAME" ]; then
                KEYBOARD_NAME="$1"
            else
                echo "Error: Multiple keyboard names specified"
                usage
                exit 1
            fi
            shift
            ;;
    esac
done

echo "=== ZMK Keyboard Flash Script ==="

# Find firmware file
FIRMWARE_FILE=$(find_firmware_file "$KEYBOARD_NAME" "$SIDE")
if [ -z "$FIRMWARE_FILE" ]; then
    echo "Error: No firmware file found"
    if [ -n "$KEYBOARD_NAME" ]; then
        echo "  Searched for: $KEYBOARD_NAME"
        if [ -n "$SIDE" ]; then
            echo "  Side: $SIDE"
        fi
        
        # Show available sides for this keyboard if any exist
        echo ""
        echo "Available firmware files for $KEYBOARD_NAME:"
        find "$FIRMWARE_DIR" -name "*${KEYBOARD_NAME,,}*.uf2" -exec basename {} \; | sort || echo "  No firmware files found for $KEYBOARD_NAME"
    fi
    echo ""
    usage
    exit 1
fi

echo "Using firmware: $(basename "$FIRMWARE_FILE")"

# Mount the keyboard device. mount-device.py filters by hardcoded family
# patterns (corne/sofle/glove80/planck/zen). The user might pass a variant
# name like "salon" that isn't a family — derive the family from the firmware
# filename so the bootloader filter still matches.
BOOTLOADER_FAMILY=$(derive_bootloader_family "$FIRMWARE_FILE")
if [ -z "$BOOTLOADER_FAMILY" ] && [ -n "$KEYBOARD_NAME" ]; then
    BOOTLOADER_FAMILY="$KEYBOARD_NAME"
fi

KEYBOARD_ARG=""
if [ -n "$BOOTLOADER_FAMILY" ]; then
    KEYBOARD_ARG="--keyboard $BOOTLOADER_FAMILY"
fi

# Wait up to 60s so this works as a single invocation: run it first, then
# double-tap reset on the half being flashed.
BOOTLOADER_WAIT_SECONDS=60
echo "Mounting keyboard device (waiting up to ${BOOTLOADER_WAIT_SECONDS}s — double-tap reset now)..."
if ! "$SCRIPT_DIR/mount-device.py" $KEYBOARD_ARG $VERBOSE --wait "$BOOTLOADER_WAIT_SECONDS" "$MOUNT_POINT"; then
    echo "Error: Failed to mount keyboard device"
    exit 1
fi

# Copy firmware and sync
echo "Flashing firmware..."
cp "$FIRMWARE_FILE" "$MOUNT_POINT/CURRENT.UF2"
sync

# Unmount
echo "Unmounting..."
sudo umount "$MOUNT_POINT"

echo "Flash complete!"
