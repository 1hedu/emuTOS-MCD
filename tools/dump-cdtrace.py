#!/usr/bin/env python3
"""What did the CDBIOS actually say to the drive?

boot/cddtrace.S watches the BIOS's command and status packets while the
loader reads the disc -- a read that has never once failed, on the same
drive our own driver struggles with. The IOFW copies the log into work
RAM before the BIOS is evicted; this decodes it out of a --dump-wram.

On hardware there is no dump, and the same log is on screen: hold Start
at power-on. This exists so the emulator can be checked against the
console, and so a trace can be read without squinting at a photograph.

Usage:
  tools/dump-cdtrace.py <wram.bin>
"""
import sys

TRACE = 0xEE00          # iofw's CDTRACE_WRAM, relative to 0xFF0000
ENTS = 60
SIZE = 32

# What each drive-state nibble means, from the sixteen handlers at
# $1434 (docs/cdd-bios-notes.md). The point of the trace is to find out
# which of these the drive actually sits in during a working read.
STATE = {
    0x0: "stopped", 0x1: "playing", 0x2: "seeking", 0x3: "scanning",
    0x4: "paused", 0x5: "open", 0x6: "checksum error", 0x7: "command error",
    0x8: "function error", 0x9: "toc read", 0xA: "tracking",
    0xB: "no disc", 0xC: "lead-out", 0xD: "lead-in", 0xE: "tray",
    0xF: "not ready",
}

# Nibble 1 of the status packet: which question this packet answers.
REPORT = {
    0x0: "absolute position", 0x1: "position in track", 0x2: "track",
    0x3: "disc length", 0x4: "first and last track", 0x5: "track start",
    0x6: "error", 0xE: "command error", 0xF: "not ready",
}

# Nibble 0 of the command packet.
COMMAND = {
    0x0: "no change", 0x1: "stop", 0x2: "report request", 0x3: "read",
    0x4: "seek", 0x6: "pause/hold", 0x7: "resume", 0x8: "fast forward",
    0x9: "rewind", 0xA: "track skip", 0xC: "track cue", 0xD: "door close",
    0xE: "door open",
}


def unswap(b):
    """The harness dumps work RAM word-byteswapped. Every earlier tool
    here has had to learn this the hard way."""
    out = bytearray(len(b))
    out[0::2] = b[1::2]
    out[1::2] = b[0::2]
    return bytes(out)


def nib(bs):
    return "".join("%X" % (x & 0x0F) for x in bs)


def msf(bs):
    """Six nibbles, BCD minutes/seconds/frames."""
    return "%X%X:%X%X:%X%X" % tuple(x & 0x0F for x in bs)


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    w = unswap(open(sys.argv[1], "rb").read())

    rows = []
    for k in range(ENTS):
        e = w[TRACE + k * SIZE:TRACE + (k + 1) * SIZE]
        if len(e) < SIZE:
            break
        xchg = (e[28] << 8) | e[29]
        if xchg == 0:
            break               # the log ends at the first unwritten entry
        rows.append((k, e, xchg))

    if not rows:
        print("no trace in the dump.")
        print("  either the loader never got to a read (unlikely -- it")
        print("  cannot boot without one), or the IOFW never found the")
        print("  CDTRACE1 tag in PRG bank 0.")
        return 2

    print("%-3s %-4s %-11s %-11s %-9s %-6s" %
          ("no", "mode", "command", "status", "where", "xchg"))
    for k, e, xchg in rows:
        print("%-3d %-4X %-11s %-11s %-9s %-6d  %s / %s%s" % (
            k, e[26] & 0x0F, nib(e[0:10]), nib(e[10:20]), msf(e[20:26]),
            xchg,
            COMMAND.get(e[0] & 0x0F, "?"),
            STATE.get(e[10] & 0x0F, "?"),
            "" if (e[11] & 0x0F) not in REPORT
            else ", " + REPORT[e[11] & 0x0F]))

    print("")
    print("%d entries, %d exchanges (%.1f s of drive conversation)"
          % (len(rows), sum(r[2] for r in rows),
             sum(r[2] for r in rows) / 75.0))

    seen = sorted({r[1][10] & 0x0F for r in rows})
    print("drive rested in state(s): %s"
          % ", ".join("%X (%s)" % (s, STATE.get(s, "?")) for s in seen))
    cmds = sorted({r[1][0] & 0x0F for r in rows})
    print("commands sent: %s"
          % ", ".join("%X (%s)" % (c, COMMAND.get(c, "?")) for c in cmds))
    return 0


if __name__ == "__main__":
    sys.exit(main())
