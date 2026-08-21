#!/usr/bin/env python3
"""Write the fixed-address regions into a built disc image.

Two things need to sit at addresses the sub CPU knows at compile time:

  LBA 1400   one sector of a pattern the sub can regenerate from a
             formula, so a console with no test harness attached can
             still check a disc read byte for byte and reach a verdict
             on its own.

  LBA 1600   the D: filesystem -- a FAT16 image read off the disc on
             demand rather than loaded into RAM, which is the whole
             point of it.

Both land inside PAD.BIN, the padding file, rather than after the end
of the ISO 9660 volume. Appending was the obvious thing and it was
wrong twice over: extending the file with truncate() leaves it sparse,
and the volume descriptor still declares the smaller size, so any tool
that honours that size writes a disc missing everything after it.

The pattern must stay in step with cdr_check() in emutos/bios/segacd.c
and pattern() in tools/check-cdread.py; the addresses with CDR_PATTERN_LBA
and CD_DDISK_LBA in the same file.
"""
import sys

SECTOR = 2048
TEST_LBA = 1400
DDISK_LBA = 1600


def pad_stream():
    """The generator build-iso.sh fills PAD.BIN with, replayed."""
    x = 0x2468ACE
    while True:
        x = (1103515245 * x + 12345) & 0x7FFFFFFF
        yield ((x >> 16) & 0xFF) or 0xA5


_pad_cache = {}


def is_pad(blk, lba):
    """True if this sector is generator output. The padding file starts
    at some sector the builder chose, so the phase is unknown here --
    match on the first sixteen bytes appearing anywhere in the stream,
    which is enough to tell filler from a filesystem."""
    if not _pad_cache:
        g = pad_stream()
        # Must cover the whole of PAD.BIN: the guard hunts the sector's
        # first sixteen bytes in this stream, and a stream shorter than
        # the file makes the file's own tail read as "not padding" --
        # which is exactly the false refusal that fired when the pad
        # grew to 25 MB and this cache stayed at 12.
        _pad_cache['s'] = bytes(next(g) for _ in range(26214400))
    return bytes(blk[:16]) in _pad_cache['s']


def pattern():
    return bytes(((i * 5) ^ (i >> 4) ^ 0xA5) & 0xFF for i in range(SECTOR))


def check_padding(data, lba, sectors, what):
    """Refuse rather than overwrite something real. A guard that fires is
    a build that stops; a guard that does not exist is a disc that tests
    nothing, or worse, a filesystem with a hole punched in it."""
    # PAD.BIN is no longer zeros -- a disc full of zero sectors is a
    # disc full of the weakest sectors a CD-R can hold -- so "is this
    # padding?" is now "does it match the generator?" rather than "is
    # it empty?". Same guard, same refusal, different fingerprint.
    for s in range(lba - 1, lba + sectors + 1):
        blk = data[s * SECTOR:(s + 1) * SECTOR]
        if any(blk) and not is_pad(blk, s):
            sys.exit("sector %d is not padding: %s would overwrite the "
                     "filesystem. Enlarge PAD.BIN or move it." % (s, what))


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    path, ddisk = sys.argv[1], sys.argv[2]
    data = bytearray(open(path, "rb").read())
    dimg = open(ddisk, "rb").read()
    dsectors = (len(dimg) + SECTOR - 1) // SECTOR

    # The volume descriptor's own idea of how big the disc is: LBA 16,
    # offset 80, little-endian first.
    vol = int.from_bytes(data[16 * SECTOR + 80:16 * SECTOR + 84], "little")
    need = DDISK_LBA + dsectors + 8
    if vol < need:
        sys.exit("the volume is %d sectors and %d are needed: enlarge "
                 "PAD.BIN" % (vol, need))
    if len(data) // SECTOR < vol:
        sys.exit("the image is %d sectors but the volume claims %d"
                 % (len(data) // SECTOR, vol))

    check_padding(data, TEST_LBA, 1, "the self-check sector")
    check_padding(data, DDISK_LBA, dsectors, "the D: filesystem")

    data[TEST_LBA * SECTOR:(TEST_LBA + 1) * SECTOR] = pattern()
    data[DDISK_LBA * SECTOR:DDISK_LBA * SECTOR + len(dimg)] = dimg
    open(path, "wb").write(bytes(data))
    print("test sector at LBA %d, D: at LBA %d (%d sectors), "
          "volume %d sectors" % (TEST_LBA, DDISK_LBA, dsectors, vol))


if __name__ == "__main__":
    main()
