#!/bin/bash

set -e

SCRIPT_DIR=$(dirname "$0")
FIRMWARE_DIR="$(dirname "$SCRIPT_DIR")/firmware"
MOUNT_POINT=~/mnt/keeb

usage() {
    echo "Usage: $0 [keyboard_name] [OPTIONS]"
    echo "  keyboard_name: sofle, corne, glove80, planck, zen, etc."
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
    
    # If no keyboard name specified, try to find any .uf2 file
    if [ -z "$keyboard_name" ]; then
        firmware_file=$(find "$FIRMWARE_DIR" -name "*.uf2" | head -1)
        if [ -n "$firmware_file" ]; then
            echo "$firmware_file"
            return 0
        fi
    else
        # Look for firmware files matching keyboard name
        keyboard_lower=$(echo "$keyboard_name" | tr '[:upper:]' '[:lower:]')
        
        # If side is specified, prioritize that side
        if [ -n "$side" ]; then
            for pattern in "${keyboard_lower}_${side}" "${keyboard_lower}-${side}"; do
                firmware_file=$(find "$FIRMWARE_DIR" -name "*${pattern}*.uf2" | head -1)
                if [ -n "$firmware_file" ]; then
                    echo "$firmware_file"
                    return 0
                fi
            done
        fi
        
        # Try exact matches first
        for pattern in "${keyboard_lower}" "${keyboard_lower}_left" "${keyboard_lower}_right" "${keyboard_lower}-left" "${keyboard_lower}-right"; do
            firmware_file=$(find "$FIRMWARE_DIR" -name "*${pattern}*.uf2" | head -1)
            if [ -n "$firmware_file" ]; then
                echo "$firmware_file"
                return 0
            fi
        done
        
        # Try partial matches
        firmware_file=$(find "$FIRMWARE_DIR" -name "*${keyboard_lower}*.uf2" | head -1)
        if [ -n "$firmware_file" ]; then
            echo "$firmware_file"
            return 0
        fi
    fi
    
    return 1
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

# Mount the keyboard device
KEYBOARD_ARG=""
if [ -n "$KEYBOARD_NAME" ]; then
    KEYBOARD_ARG="--keyboard $KEYBOARD_NAME"
fi

echo "Mounting keyboard device..."
if ! "$SCRIPT_DIR/mount-device.py" $KEYBOARD_ARG $VERBOSE "$MOUNT_POINT"; then
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
