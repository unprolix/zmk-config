#!/usr/bin/env python3
"""
Put an eyelash half into the bootloader over raw HID, without touching it.

TEMPORARY. This exists so a flash cycle needs no keypress during heavy
development; the firmware side lives in config/boards/shields/dfu and is meant
to be removed, not shipped.

Usage:
    scripts/dfu-trigger.py left       # the central
    scripts/dfu-trigger.py right      # relayed to the peripheral
    scripts/dfu-trigger.py --list     # show candidate raw-HID interfaces

The keyboard exposes a vendor-defined HID interface (usage page 0xFF60, usage
0x61). Writing the magic below makes it reboot into DFU about a quarter of a
second later, at which point it enumerates as a NICENANO mass-storage volume.
"""

import glob
import os
import sys

MAGIC = b"ZMKDFU!"
TARGETS = {"left": 0x00, "central": 0x00, "right": 0x01, "peripheral": 0x01}

# The vendor-defined page the firmware's raw-HID descriptor advertises.
USAGE_PAGE = 0xFF60
REPORT_SIZE = 32


def candidate_devices():
    """hidraw nodes belonging to a keyboard that advertises our usage page."""
    found = []
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        node = "/dev/" + os.path.basename(path)
        try:
            uevent = open(os.path.join(path, "device", "uevent")).read()
        except OSError:
            continue

        # HID_NAME / HID_ID identify the device; the report descriptor tells us
        # whether this particular interface is the raw one.
        name = ""
        for line in uevent.splitlines():
            if line.startswith("HID_NAME="):
                name = line.split("=", 1)[1]

        try:
            desc = open(os.path.join(path, "device", "report_descriptor"), "rb").read()
        except OSError:
            desc = b""

        # Usage page 0xFF60 encodes as 06 60 FF in the descriptor.
        if bytes([0x06, USAGE_PAGE & 0xFF, (USAGE_PAGE >> 8) & 0xFF]) in desc:
            found.append((node, name))
    return found


def build_report(target):
    check = target
    for b in MAGIC:
        check ^= b
    payload = bytearray(MAGIC) + bytes([target, check])
    payload += bytes(REPORT_SIZE - len(payload))
    # Linux hidraw expects a leading report-ID byte; 0 means "no report ID".
    return bytes([0x00]) + bytes(payload)


def main():
    args = sys.argv[1:]
    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0

    devices = candidate_devices()

    if args[0] == "--list":
        if not devices:
            print("No raw-HID interfaces found (usage page 0x%04X)." % USAGE_PAGE)
            print("The firmware needs the 'dfu' shield built in.")
            return 1
        for node, name in devices:
            print("%s  %s" % (node, name))
        return 0

    if args[0] not in TARGETS:
        print("Unknown target %r; expected one of: %s" % (args[0], ", ".join(sorted(TARGETS))))
        return 1

    if not devices:
        print("No raw-HID interface found. Either the half is not attached, or")
        print("its firmware predates the 'dfu' shield.")
        return 1

    report = build_report(TARGETS[args[0]])

    # Which hidraw node is the raw interface is not knowable without opening
    # it, and a keyboard presents several; try each and stop at the first that
    # accepts the write.
    for node, name in devices:
        try:
            fd = os.open(node, os.O_WRONLY)
        except OSError as e:
            print("%s: %s" % (node, e))
            continue
        try:
            os.write(fd, report)
            print("Sent DFU command for %r via %s (%s)" % (args[0], node, name))
            print("It should enumerate as NICENANO in a moment.")
            return 0
        except OSError as e:
            print("%s: write failed (%s)" % (node, e))
        finally:
            os.close(fd)

    print("No interface accepted the command.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
