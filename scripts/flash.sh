#!/bin/sh

SCRIPT_DIR=$(dirname "$0")
FIRMWARE_DIR="$(dirname "$SCRIPT_DIR")/firmware"

"$SCRIPT_DIR/mount-device.py" ~/mnt/keeb --verbose && \
cp "$FIRMWARE_DIR/nice_view-eyelash_corne_left.uf2" ~/mnt/keeb/CURRENT.UF2 && \
sync && \
sudo umount ~/mnt/keeb
