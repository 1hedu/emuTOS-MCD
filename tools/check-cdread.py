#!/usr/bin/env python3
"""Did the sub CPU actually read the disc?

Stage 3's point is that this question has an answer that is not a squint
at a status line. The sub reads two sectors through the CDC with no BIOS
involved -- LBA 16, the ISO 9660 primary volume descriptor, and a second
one over a thousand sectors further in -- and the servant copies the
result into work RAM. This compares it against the image the disc was
built from.

The far sector is the one that matters. A recognisable descriptor at a
low address is also the likeliest thing to be lying around in a buffer
already; a sector past the thousand mark is not, and reaching it needs a
real seek.

Usage:
  tools/check-cdread.py <wram.bin> <image.iso>
"""
import sys

CDSECT = 0x0000         # within the 64K dump: iofw's CDSECT_WRAM
CDSTAT = 0x0800         # ...and CDSTAT_WRAM
SECTOR = 2048
SLOTS = 2
STATE = {0: "idle", 1: "commanding", 2: "streaming", 3: "done",
         4: "failed", 5: "parked"}
FAIL = {
    0: "-",
    1: "the transfer never became ready (DSR)",
    2: "no end of transfer (EDT)",
    3: "the drive never started reading",
    4: "our sector never came round",
}


def unswap(b):
    """The harness dumps work RAM word-byteswapped. Forgetting this turns
    a perfect match into 2048 wrong bytes, in a pattern that reads
    exactly like a half-working driver."""
    out = bytearray(len(b))
    out[0::2] = b[1::2]
    out[1::2] = b[0::2]
    return bytes(out)


def hash31(b):
    """What the sub computes: h = h*31 + byte, 32 bits. Chosen because a
    68000 has no 32x32 multiply and 31 is shifts and a subtract."""
    h = 0
    for x in b:
        h = ((h << 5) - h + x) & 0xFFFFFFFF
    return h


def bcd(v):
    return (v // 10) * 16 + v % 10


def pattern():
    """What tools/build-iso.sh writes at the far sector, and what the sub
    regenerates to check it. Kept identical in three places on purpose:
    a console with no harness has to be able to reach a verdict alone."""
    return bytes(((i * 5) ^ (i >> 4) ^ 0xA5) & 0xFF for i in range(SECTOR))


def verdict_words(v):
    if v == 0:
        return "nothing concluded yet"
    out = ["slot %d verified" % k for k in range(SLOTS) if v & (1 << k)]
    if v & 0x40:
        out.append("A SECTOR ARRIVED AND WAS WRONG")
    if v & 0x80:
        out.append("A SECTOR NEVER ARRIVED")
    return "; ".join(out)


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    wram = unswap(open(sys.argv[1], "rb").read())
    iso = open(sys.argv[2], "rb").read()

    def w(i):
        a = CDSTAT + 2 * i
        return int.from_bytes(wram[a:a + 2], "big")

    def l(i):
        return (w(i) << 16) | w(i + 1)

    if w(0) != 0xCDC0:
        print("report block not fetched (magic %04X, wanted CDC0)" % w(0))
        print("  either the sub never raised bit 1 of $FF8028, or the")
        print("  servant never got the bus to read PRG bank 3")
        return 2

    state = w(1) >> 12
    ints = (w(1) >> 4) & 0xFF
    fail = w(1) & 0x0F
    head = wram[CDSTAT + 4:CDSTAT + 8]
    pt = w(4)
    seq = w(5)
    pub = w(6)
    verdict = w(7)
    lbas = [l(12 + 2 * k) for k in range(SLOTS)]
    sums = [l(8 + 2 * k) for k in range(SLOTS)]

    print("state      %d (%s)" % (state, STATE.get(state, "?")))
    print("sectors    %d decoded (level 5)" % ints)
    print("failure    %d: %s" % (fail, FAIL.get(fail, "?")))
    print("header     %02X:%02X:%02X mode %02X" % tuple(head))
    print("buffer     PT = %04X, holding slot %d" % (pt, pub))
    print("report seq %d" % seq)
    print("verdict    %02X: %s" % (verdict, verdict_words(verdict)))

    # The drive's own account of how long the disc is, against the image
    # it was built from. Independent of every sector read: it comes back
    # in a status packet, so it checks the CDD path and the BCD parsing
    # without the CDC being involved at all.
    lm, ls = w(32) >> 8, w(32) & 0xFF
    lf, lok = w(33) >> 8, w(33) & 0xFF
    total = len(iso) // SECTOR + 150
    want = (bcd(total // 4500), bcd((total // 75) % 60), bcd(total % 75))
    if not lok:
        print("length     the drive has not reported one")
        rc_len = 1
    elif (lm, ls, lf) == want:
        print("length     %02X:%02X:%02X, and the image is %d sectors: MATCH"
              % (lm, ls, lf, len(iso) // SECTOR))
        rc_len = 0
    else:
        print("length     %02X:%02X:%02X, but the image wants %02X:%02X:%02X"
              % ((lm, ls, lf) + want))
        rc_len = 1

    rc = rc_len
    for k in range(SLOTS):
        lba = lbas[k]
        exp = iso[lba * SECTOR:(lba + 1) * SECTOR]
        frame = lba + 150
        msf = (bcd(frame // 4500), bcd((frame // 75) % 60), bcd(frame % 75))
        print("")
        print("slot %d: LBA %d = MSF %02X:%02X:%02X" % ((k, lba) + msf))
        if len(exp) != SECTOR:
            print("  the image has no such sector")
            rc = 2
            continue
        if exp == pattern():
            print("  this is the self-check pattern the builder wrote")
        elif not any(exp):
            print("  NOTE: the image has this sector all zero, so a match")
            print("        here proves less than it looks like it does")
        want = hash31(exp)
        got = sums[k]
        if got == want:
            print("  hash %08X: MATCH" % got)
        else:
            print("  hash %08X, image hashes to %08X: MISMATCH" % (got, want))
            rc = 1

    if pub >= SLOTS:
        print("\nno sector in the buffer")
        return max(rc, 1)

    lba = lbas[pub]
    got = wram[CDSECT:CDSECT + SECTOR]
    exp = iso[lba * SECTOR:(lba + 1) * SECTOR]
    print("")
    if got == exp:
        print("BYTES: slot %d, LBA %d, all %d bytes match the image"
              % (pub, lba, SECTOR))
        print("  first 16: %s" % got[:16].hex(" "))
    else:
        bad = [i for i in range(SECTOR) if got[i] != exp[i]]
        print("BYTES: slot %d, LBA %d, %d of %d differ"
              % (pub, lba, len(bad), SECTOR))
        print("  first at %d: got %02X, image has %02X"
              % (bad[0], got[bad[0]], exp[bad[0]]))
        print("  got   %s" % got[:32].hex(" "))
        print("  want  %s" % exp[:32].hex(" "))
        # A whole-sector shift is the likeliest wrong answer, and saying
        # so beats leaving a wall of hex to be read by eye.
        for off in list(range(-4, 0)) + list(range(1, 5)):
            other = iso[(lba + off) * SECTOR:(lba + off + 1) * SECTOR]
            if got == other:
                print("  ...but it is exactly LBA %d, %+d sectors off"
                      % (lba + off, off))
                break
        rc = 1

    if verdict != (1 << SLOTS) - 1:
        print("")
        print("the sub itself did not sign off: %02X, %s"
              % (verdict, verdict_words(verdict)))
        rc = 1

    print("")
    print("PASS" if rc == 0 else "FAIL")
    return rc


if __name__ == "__main__":
    sys.exit(main())
