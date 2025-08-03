#!/bin/sh

scripts/mount-device.py ~/mnt/keeb --verbose && cp ~/src/zmk-workspace/firmware/nice_view-eyelash_corne_left.uf2 ~/mnt/keeb/CURRENT.UF2 && sync && sudo umount ~/mnt/keeb

