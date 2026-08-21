#!/usr/bin/env python3
"""Pull files out of a Mega CD backup RAM image.

The image can come from three places and they are all the same bytes:

  * a Mega EverDrive save file. Two things write one: boot/m1tool.S,
    the standalone tool ROM that lifts the console's internal backup
    RAM into cart SRAM; and, on a Mode 1 boot, EmuTOS itself -- the
    cartridge's save RAM is drive C: there, so anything saved to it
    lands in the same file. SRAMTOOL.PRG on the A: ramdisk puts the
    internal memory's raw image there as C:\BRAM.BIN, which is the
    tool ROM's whole job done from the desktop instead;
  * `scd_*.brm`, the internal backup RAM as an emulator saves it;
  * `cart.brm`, a backup RAM cartridge, same thing but larger.

EmuTOS lays a small FAT12 volume on that memory, so the file is a disk
image and this is an ordinary FAT reader. Nothing here knows about
CDLOG.TXT specifically: the port may put other things there later, and
a tool that only understands one filename ages badly.

The geometry is read from the boot sector rather than assumed, because
the two volumes this port creates do not agree -- the internal backup
RAM gets one sector per cluster and 32 root entries out of sixteen
sectors, the cartridge gets four and 64 out of however many it has.

  tools/bram-extract.py save.srm              list
  tools/bram-extract.py save.srm CDLOG.TXT    print it
  tools/bram-extract.py save.srm --all out/   write them all out
"""
import sys
import os

TRAILER = b'MCDB'


def unpack_odd(img):
    """A Mega EverDrive saves the raw address window, and cart SRAM lives
    on the odd byte of each word -- so the file is twice the size of the
    memory and every even byte is the unmapped half. Emulator dumps are
    already the odd bytes alone. Both arrive here, so tell them apart:
    only unpack when the even half is uniformly dead and the odd half
    is not. A real de-interleaved volume fails that test the moment it
    holds anything at all."""
    if len(img) < 2 or len(img) % 2:
        return img, False
    if set(img[0::2]) <= {0x00, 0xFF} and not set(img[1::2]) <= {0x00, 0xFF}:
        return img[1::2], True
    return img, False


def trailer(img):
    """The Mode 1 tool writes sixteen bytes at the very top: a magic, the
    length it copied and a checksum. Absent for emulator dumps, which
    are not written by it -- so this reports, it does not require."""
    if len(img) < 16:
        return None
    t = img[-16:]
    if bytes(t[0:4]) != TRAILER:
        return None
    n = (t[4] << 8) | t[5]
    ck = (t[6] << 8) | t[7]
    # The ROM's sum: add the byte, then rotate the 32-bit accumulator
    # left by one. Only its high word is stored, so only that is checked.
    got = 0
    for b in img[:n]:
        got = (got + b) & 0xFFFFFFFF
        got = ((got << 1) | (got >> 31)) & 0xFFFFFFFF
    return n, ck, (got >> 16) == ck


PHASES = {
    0x01: "entered, SRAM mapped",
    0x02: "checking for a Mega CD",
    0x03: "gate array reset",
    0x04: "asking for the sub CPU's bus",
    0x05: "planting the sub program",
    0x06: "releasing the sub CPU",
    0x07: "waiting for it to finish",
    0x7A: "took the bus back",
    0x08: "writing 8192 bytes to SRAM",
    0xFF: "done -- the dump is complete",
    0xF0: "gave up: the bus was never granted",
    0xF1: "gave up: the sub CPU never answered",
    0xF2: "gave up: SRAM read back wrong",
    0x10: "gave up: nothing answered the expansion port",
    0x11: "waking the drive: looking for the firmware",
    0x12: "found it; the console is unpacking its own CDBIOS",
    0x13: "firmware unpacked, stub planted, sub released",
    0x14: "the drive is awake -- DRV_INIT returned",
    0xF3: "no BIOS shape in the boot ROM; drive not woken",
    0xF4: "DRV_INIT never came back (empty tray?); carried on",
    0x15: "drive done with; resetting the gate array back",
    0x16: "gate array back, bus retaken -- normal boot resumes",
    0xF6: "unpacked, but not into anything that looks like firmware",
    0x0E: "the main CPU took an exception",
}


LOADER_AT = 63 * 512    # keep in step with boot/m1emu.S


def loader_marks(img):
    """boot/m1emu.S keeps the top 512 bytes of cart SRAM for itself and
    stamps sixteen phase bytes there, 64 bytes in. Same idea as the tool
    ROM's log below and a different address, because on a Mode 1 boot
    the rest of this memory is drive C: and a report written over sector
    zero is a report written over the boot sector."""
    log = img[LOADER_AT + 64:LOADER_AT + 80]
    if not log or set(log) <= {0x00, 0xFF}:
        return False
    print("# Mode 1 loader progress:", file=sys.stderr)
    for b in log:
        if b == 0:
            break
        print("#   %02X  %s" % (b, PHASES.get(b, "?")), file=sys.stderr)
    return True


def progress(img):
    """The tool ROM stamps a phase byte into the sixteen logical bytes
    just past the dump, so a run that wedges still says where it got to
    -- on the card, which is the only place a wedged console can still
    be read from."""
    log = img[8192:8208]
    if not log or set(log) <= {0x00, 0xFF}:
        return False
    # Offset 8192 is the tool ROM's log and, on a 32 KB cartridge
    # volume, is also just somewhere in the middle of a file. Reading
    # one as the other printed sixteen "?" phases off the front of a
    # CDLOG line -- a confident readout of nothing, which is the exact
    # failure this file exists to avoid. Every byte the ROM writes here
    # is one of the codes below, so anything else is not this log.
    if any(b and b not in PHASES for b in log):
        return False
    print("# tool-ROM progress:", file=sys.stderr)
    for b in log:
        if b == 0:
            break
        print("#   %02X  %s" % (b, PHASES.get(b, "?")), file=sys.stderr)
    return True


def sega_format(img):
    """Sega's own BRAM format, so an image that is not ours is named
    rather than thrown at a FAT parser. BRMFORMAT writes a 64-byte
    volume block at the very end: eleven underscores, a 0x40 block-size
    marker, and the strings below."""
    if len(img) < 64:
        return None
    tail = img[-64:]
    if tail[0x20:0x2B] == b'SEGA_CD_ROM':
        free = int.from_bytes(tail[0x10:0x12], 'big')
        return "Sega-format backup RAM: %d free blocks of %d bytes" % (
            free, tail[0x0F])
    if img.count(img[:1]) == len(img):
        return "blank: every byte is %02X" % img[0]
    return None


class Fat:
    def __init__(self, img):
        b = img
        self.bps = b[11] | (b[12] << 8)
        self.spc = b[13]
        self.rsv = b[14] | (b[15] << 8)
        self.nfat = b[16]
        self.nroot = b[17] | (b[18] << 8)
        self.total = b[19] | (b[20] << 8)
        self.spf = b[22] | (b[23] << 8)
        if self.bps not in (128, 256, 512, 1024) or self.spc == 0:
            raise ValueError("no FAT boot sector here (bytes/sector %d, "
                             "sectors/cluster %d)" % (self.bps, self.spc))
        self.img = b
        self.rootsec = self.rsv + self.nfat * self.spf
        self.rootlen = (self.nroot * 32 + self.bps - 1) // self.bps
        self.datasec = self.rootsec + self.rootlen

    def fat12(self, n):
        off = self.rsv * self.bps + (n * 3) // 2
        v = self.img[off] | (self.img[off + 1] << 8)
        return (v >> 4) if (n & 1) else (v & 0x0FFF)

    def chain(self, start):
        n, seen = start, set()
        while 2 <= n < 0xFF0:
            if n in seen:                       # a damaged FAT loops
                break
            seen.add(n)
            yield n
            n = self.fat12(n)

    def entries(self):
        base = self.rootsec * self.bps
        for i in range(self.nroot):
            e = self.img[base + i * 32:base + i * 32 + 32]
            if not e or e[0] == 0:
                return
            if e[0] == 0xE5 or (e[11] & 0x08):  # deleted, or the label
                continue
            name = e[0:8].decode('latin1').rstrip()
            ext = e[8:11].decode('latin1').rstrip()
            yield (name + '.' + ext if ext else name,
                   e[26] | (e[27] << 8),
                   int.from_bytes(e[28:32], 'little'))

    def read(self, clus, size):
        out = bytearray()
        for n in self.chain(clus):
            off = (self.datasec + (n - 2) * self.spc) * self.bps
            out += self.img[off:off + self.spc * self.bps]
            if len(out) >= size:
                break
        return bytes(out[:size])


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    img = open(sys.argv[1], 'rb').read()

    img, packed = unpack_odd(img)
    if packed:
        print("# %d bytes of odd-byte cart SRAM, de-interleaved" % len(img),
              file=sys.stderr)

    ran = progress(img)
    loader_marks(img)

    t = trailer(img)
    if t:
        print("# tool-ROM trailer: %d bytes copied, checksum %04X (%s)"
              % (t[0], t[1], "matches" if t[2] else "DOES NOT MATCH"),
              file=sys.stderr)

    try:
        fs = Fat(img)
    except ValueError as e:
        note = sega_format(img)
        if note:
            sys.exit("%s\nNo EmuTOS filesystem here, so nothing to extract."
                     % note)
        if ran:
            # The tool ROM ran and said so; what it copied is simply not
            # a filesystem. That is a fact about the console's backup
            # RAM, not about this file, and the two should not be
            # reported with the same sentence.
            sys.exit("%s\nThe tool ROM ran, so this is its dump -- but the "
                     "console's backup RAM has no EmuTOS filesystem on it "
                     "yet." % e)
        sys.exit("%s\nThis does not look like a backup RAM image this "
                 "port wrote." % e)
    files = list(fs.entries())

    if len(sys.argv) == 2:
        print("%d bytes/sector, %d/cluster, root at %d, data at %d"
              % (fs.bps, fs.spc, fs.rootsec, fs.datasec))
        if not files:
            print("(no files)")
        for name, clus, size in files:
            print("  %-13s %7d bytes  cluster %d" % (name, size, clus))
        return

    if sys.argv[2] == '--all':
        out = sys.argv[3] if len(sys.argv) > 3 else '.'
        os.makedirs(out, exist_ok=True)
        for name, clus, size in files:
            with open(os.path.join(out, name), 'wb') as f:
                f.write(fs.read(clus, size))
            print("%s  %d bytes" % (os.path.join(out, name), size))
        return

    want = sys.argv[2].upper()
    for name, clus, size in files:
        if name.upper() == want:
            sys.stdout.buffer.write(fs.read(clus, size))
            return
    sys.exit("%s: not in this image" % sys.argv[2])


main()
