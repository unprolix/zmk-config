#!/usr/bin/env python3
"""
Drive the eyelash's LEDs directly from the host, bypassing layers and palettes.

Point of this: "the wrong column lit in the wrong colour" has several possible
causes -- a wrong zone map, a wrong palette index, a relay that never arrived,
a strip that swallowed its write -- and they are indistinguishable from across
the desk. Poking one pixel at a time separates them without a flash cycle.

While an override is in force the keyboard stops painting its own colours, so
whatever is poked in stays put. Release to hand control back.

Usage:
    scripts/rgb-poke.py left  2 red        # index 2 on the central
    scripts/rgb-poke.py right 0 green
    scripts/rgb-poke.py left  3 40,0,40    # explicit r,g,b
    scripts/rgb-poke.py left  all blue
    scripts/rgb-poke.py left  off          # all dark, override still held
    scripts/rgb-poke.py both  release      # normal behaviour resumes
    scripts/rgb-poke.py left  walk         # step 0..5, a second each
    scripts/rgb-poke.py left  watch        # print what the firmware computes

TEMPORARY development tooling, like the bootloader trigger it shares a channel
with.
"""

import glob
import os
import select
import signal
import sys
import time

MAGIC = b"ZMKRGB!"
USAGE_PAGE = 0xFF60
REPORT_SIZE = 32

# Set from --name; restricts every lookup to one keyboard.
WANT_NAME = None

HALVES = {"left": 0, "central": 0, "right": 1, "peripheral": 1}
CMD_PIXEL, CMD_ALL, CMD_RELEASE, CMD_REPORT = 1, 2, 3, 4

# Dim on purpose: these are looked at from a few inches away.
COLOURS = {
    "red": (40, 0, 0),
    "green": (0, 40, 0),
    "blue": (0, 0, 40),
    "yellow": (40, 40, 0),
    "cyan": (0, 40, 40),
    "magenta": (40, 0, 40),
    "white": (40, 40, 40),
    "off": (0, 0, 0),
}


def raw_hid_nodes(name=None):
    """Raw-HID interfaces, optionally only those of a named keyboard.

    More than one board can carry this channel -- a Rolio and an eyelash on the
    same machine both do -- and without a name the first one found answers,
    which looks exactly like the keyboard you meant having gone quiet.
    """
    found = []
    for path in sorted(glob.glob("/sys/class/hidraw/hidraw*")):
        node = "/dev/" + os.path.basename(path)
        try:
            desc = open(os.path.join(path, "device", "report_descriptor"), "rb").read()
        except OSError:
            continue
        if bytes([0x06, USAGE_PAGE & 0xFF, (USAGE_PAGE >> 8) & 0xFF]) not in desc:
            continue
        if name is not None:
            try:
                uevent = open(os.path.join(path, "device", "uevent")).read()
            except OSError:
                continue
            if name.lower() not in uevent.lower():
                continue
        found.append(node)
    return found


def announce_ambiguity():
    """Say so when the choice was not obvious, rather than picking in silence."""
    nodes = raw_hid_nodes()
    if len(nodes) > 1 and WANT_NAME is None:
        names = []
        for n in nodes:
            base = os.path.basename(n)
            try:
                u = open("/sys/class/hidraw/%s/device/uevent" % base).read()
                names.append([l.split("=", 1)[1] for l in u.splitlines()
                              if l.startswith("HID_NAME=")][0])
            except (OSError, IndexError):
                names.append(base)
        uniq = sorted(set(names))
        if len(uniq) > 1:
            print("More than one keyboard offers this channel: %s" % ", ".join(uniq))
            print("Using %s. Pass --name <text> to choose." % nodes[0])


# Remembered so a subsequent read uses the interface that actually worked; a
# keyboard presents several hidraw nodes and only one is the raw channel.
_working_node = None


def send(half, cmd, index=0, rgb=(0, 0, 0)):
    global _working_node
    payload = bytearray(MAGIC)
    payload += bytes([half, cmd, index & 0xFF, rgb[0] & 0xFF, rgb[1] & 0xFF, rgb[2] & 0xFF])
    payload += bytes(REPORT_SIZE - len(payload))
    report = bytes([0x00]) + bytes(payload)

    nodes = raw_hid_nodes(WANT_NAME)
    if not nodes:
        print("No raw-HID interface found; is the keyboard on the dfu/rgbzone build?")
        return False

    for node in nodes:
        try:
            fd = os.open(node, os.O_WRONLY)
        except OSError:
            continue
        try:
            os.write(fd, report)
            _working_node = node
            return True
        except OSError:
            pass
        finally:
            os.close(fd)
    print("No interface accepted the command.")
    return False


def parse_colour(text):
    if text in COLOURS:
        return COLOURS[text]
    if "," in text:
        parts = text.split(",")
        if len(parts) == 3:
            try:
                return tuple(max(0, min(255, int(p))) for p in parts)
            except ValueError:
                pass
    return None


def main():
    global WANT_NAME
    args = sys.argv[1:]

    # --name picks the keyboard when more than one carries this channel.
    if "--name" in args:
        i = args.index("--name")
        if i + 1 >= len(args):
            print("--name wants something to match, e.g. --name Bureau")
            return 1
        WANT_NAME = args[i + 1]
        del args[i:i + 2]

    if not args or args[0] in ("-h", "--help"):
        print(__doc__)
        return 0

    announce_ambiguity()

    half_name = args[0]
    if half_name == "both":
        halves = [0, 1]
    elif half_name in HALVES:
        halves = [HALVES[half_name]]
    else:
        print("Unknown half %r; expected left, right or both." % half_name)
        return 1

    if len(args) < 2:
        print("Nothing to do. See --help.")
        return 1

    what = args[1]

    if what == "watch":
        # Ask the firmware to start reporting, then decode what it sends.
        if not send(0, CMD_REPORT, 1, (0, 0, 0)):
            return 1

        node = _working_node or (raw_hid_nodes() or [None])[0]
        if node is None:
            print("No raw-HID interface.")
            return 1

        print("Watching %s -- hold a modifier. Ctrl-C to stop." % node)
        try:
            fd = os.open(node, os.O_RDONLY | os.O_NONBLOCK)
        except OSError as e:
            print("Cannot open %s: %s" % (node, e))
            return 1

        # An explicit handler rather than relying on KeyboardInterrupt: the
        # loop spends its life in select(), and depending on how the shell
        # forwards the signal that can end up swallowed. A flag always works.
        stop = {"now": False}

        def _stop(signum, frame):
            stop["now"] = True

        signal.signal(signal.SIGINT, _stop)
        signal.signal(signal.SIGTERM, _stop)

        # Also self-terminate, so a forgotten watcher cannot sit on the device.
        limit = float(args[2]) if len(args) > 2 else 120.0
        deadline = time.time() + limit
        print("(stops on Ctrl-C, or after %gs)" % limit)

        seen = 0
        started = time.time()
        try:
            while not stop["now"] and time.time() < deadline:
                # select() rather than a blocking read: a blocking read on
                # hidraw cannot be interrupted, so Ctrl-C would be swallowed
                # and the process left unkillable until a report happened to
                # arrive.
                try:
                    r, _, _ = select.select([fd], [], [], 0.5)
                except InterruptedError:
                    continue
                if not r:
                    if seen == 0 and time.time() - started > 8:
                        print("No reports in 8s. Either reporting did not turn "
                              "on, or nothing has changed yet -- press a key.")
                        started = time.time()
                    continue
                try:
                    data = os.read(fd, REPORT_SIZE)
                except BlockingIOError:
                    continue
                if len(data) >= 17 and bytes(data[:7]) == b"ZMKWR8!":
                    # Raised from inside the strip write: what the LEDs got.
                    pk = int.from_bytes(data[7:11], "little")
                    z = [(pk >> (i * 4)) & 0xF for i in range(6)]
                    print("%s    WROTE packed=%s  px0=(%d,%d,%d) px1=(%d,%d,%d)"
                          % (time.strftime("%H:%M:%S"), z, data[11], data[12], data[13],
                             data[14], data[15], data[16]))
                    seen += 1
                    continue
                if len(data) < 19 or bytes(data[:7]) != b"ZMKST8!":
                    continue
                seen += 1
                layer = data[7]
                mods = data[8]
                pl = int.from_bytes(data[9:13], "little")
                pr = int.from_bytes(data[13:17], "little")
                ind = data[17]
                ep = data[18]
                name = bytes(data[19:]).split(b"\0")[0].decode("ascii", "replace")
                zl = [(pl >> (i * 4)) & 0xF for i in range(6)]
                zr = [(pr >> (i * 4)) & 0xF for i in range(6)]
                # Lock keys as the host reports them, and the endpoint that
                # value was read from -- they are stored per endpoint.
                locks = "".join(n for b, n in
                                ((0x01, "num"), (0x02, "CAPS"), (0x04, "scr")) if ind & b) or "-"
                # Timestamped: without it these cannot be lined up against a
                # capture of what the host received, which is the only way to
                # tell a keymap-synthesised keypress from a real one.
                print("%s layer=%-12s idx=%-2d mods=%02x  ind=%02x(%s) ep=%s  L=%s  R=%s"
                      % (time.strftime("%H:%M:%S"), name, layer, mods, ind, locks,
                         {0: "none", 1: "USB", 2: "BLE"}.get(ep, ep), zl, zr))
        except KeyboardInterrupt:
            print()
        finally:
            os.close(fd)
            send(0, CMD_REPORT, 0, (0, 0, 0))  # index 0 = off
        return 0

    if what == "release":
        for h in halves:
            send(h, CMD_RELEASE)
        print("Released; the keyboard paints its own colours again.")
        return 0

    if what == "walk":
        # Six zones is the whole addressable range on this board.
        for h in halves:
            for i in range(6):
                send(h, CMD_PIXEL, 0, (0, 0, 0))
                send(h, CMD_ALL, 0, (0, 0, 0))
                send(h, CMD_PIXEL, i, COLOURS["white"])
                print("index %d" % i)
                time.sleep(1.0)
        print("Done. 'release' to hand control back.")
        return 0

    if what in ("all", "off"):
        colour = parse_colour(args[2]) if len(args) > 2 else COLOURS["off"]
        if what == "off":
            colour = COLOURS["off"]
        if colour is None:
            print("Unknown colour.")
            return 1
        for h in halves:
            send(h, CMD_ALL, 0, colour)
        return 0

    # index + colour
    try:
        index = int(what)
    except ValueError:
        print("Expected an index, 'all', 'off', 'walk' or 'release'.")
        return 1

    colour = parse_colour(args[2]) if len(args) > 2 else COLOURS["white"]
    if colour is None:
        print("Unknown colour; try a name or r,g,b.")
        return 1

    for h in halves:
        send(h, CMD_PIXEL, index, colour)
    print("index %d -> %s" % (index, colour))
    return 0


if __name__ == "__main__":
    sys.exit(main())
