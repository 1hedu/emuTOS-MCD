#!/usr/bin/env python3
"""Build a FAT disk image the way EmuTOS's GEMDOS likes it.

Used for the A: boot ramdisk (loaded from CD into PRG-RAM), for
pre-formatting the emulated backup RAM cart (GPGX cart.brm) in CI, and
for the D: filesystem that lives on the disc itself.

FAT12 or FAT16 is chosen by cluster count, the way every other
implementation does it: 4085 clusters and up is FAT16. D: needs to come
out FAT16, because it is mounted through the hard-disk path rather than
the floppy one and GEMDOS calls that a big GEM partition.

Usage:
  mkfat.py out.img --size 131072 --label EMUTOSMD [--oem EmuTOSMD]
           [--add hostfile[:DOSNAME.EXT]] ...
"""
import argparse
import struct
import sys


def build(size, label, oem, files, fat16=False, entropy=False):
    bps = 512                       # bytes per sector
    total = size // bps
    # One sector per cluster when FAT16 is being asked for: the width is
    # decided by cluster count, and small clusters are how a 4 MB image
    # gets enough of them. The A: geometry is left exactly as it was --
    # it is a working floppy image and this is not the place to change
    # it by accident.
    spc = 1 if fat16 else (2 if total > 128 else 1)
    reserved = 1
    nfats = 2
    rootent = 512 if fat16 else 64
    rootsec = (rootent * 32 + bps - 1) // bps

    # Which FAT width, and how big is it? The two questions depend on each
    # other -- a wider entry needs more sectors, which leaves fewer for
    # data -- so settle them together rather than guessing first.
    fatsec = 1
    for _ in range(8):
        datasec = total - reserved - nfats * fatsec - rootsec
        nclust = datasec // spc
        bits = 16 if (fat16 or nclust >= 4085) else 12
        need = (2 + nclust) * bits // 8
        fatsec = (need + bps - 1) // bps
    datasec = total - reserved - nfats * fatsec - rootsec
    nclust = datasec // spc
    bits = 16 if (fat16 or nclust >= 4085) else 12
    if fat16 and nclust < 4085:
        sys.exit('%d clusters is too few to be FAT16: make the image bigger'
                 % nclust)

    img = bytearray(size)

    # ---- boot sector with BPB (little-endian, Atari-compatible) ----
    bs = img
    bs[0:2] = b"\x60\x38"           # bra.s +0x38 (Atari style, non-exec ok)
    bs[2:8] = oem.encode("ascii")[:6].ljust(6)
    bs[8:11] = b"\x24\x08\x26"      # 24-bit serial
    struct.pack_into("<H", bs, 11, bps)
    bs[13] = spc
    struct.pack_into("<H", bs, 14, reserved)
    bs[16] = nfats
    struct.pack_into("<H", bs, 17, rootent)
    struct.pack_into("<H", bs, 19, total)
    bs[21] = 0xF8                   # media descriptor
    struct.pack_into("<H", bs, 22, fatsec)
    struct.pack_into("<H", bs, 24, 16)   # sectors per track (bookkeeping)
    struct.pack_into("<H", bs, 26, 1)    # sides

    fat0 = reserved * bps
    fat1 = fat0 + fatsec * bps
    rootoff = fat1 + fatsec * bps
    dataoff = rootoff + rootsec * bps

    fat = bytearray(fatsec * bps)

    def fat_set(cl, val):
        if bits == 16:
            struct.pack_into("<H", fat, cl * 2, val & 0xFFFF)
            return
        off = cl * 3 // 2
        if cl & 1:
            fat[off] = (fat[off] & 0x0F) | ((val << 4) & 0xF0)
            fat[off + 1] = (val >> 4) & 0xFF
        else:
            fat[off] = val & 0xFF
            fat[off + 1] = (fat[off + 1] & 0xF0) | ((val >> 8) & 0x0F)

    END = 0xFFFF if bits == 16 else 0xFFF
    fat_set(0, 0xFFF8 if bits == 16 else 0xFF8)
    fat_set(1, END)

    nextcl = 2
    dirent = 0

    def add_dirent(dosname, attr, cluster, length):
        nonlocal dirent
        e = rootoff + dirent * 32
        img[e:e + 11] = dosname
        img[e + 11] = attr
        struct.pack_into("<H", img, e + 26, cluster)
        struct.pack_into("<I", img, e + 28, length)
        dirent += 1

    if label:
        add_dirent(label.encode("ascii")[:11].ljust(11), 0x08, 0, 0)

    def store(data):
        """Lay bytes down as a cluster chain; return the first cluster."""
        nonlocal nextcl
        ncl = max(1, (len(data) + spc * bps - 1) // (spc * bps))
        if nextcl + ncl - 2 > nclust:
            sys.exit("image full")
        start = nextcl
        for i in range(ncl):
            fat_set(nextcl, END if i == ncl - 1 else nextcl + 1)
            base = dataoff + (nextcl - 2) * spc * bps
            img[base:base + min(len(data) - i * spc * bps, spc * bps)] = \
                data[i * spc * bps:(i + 1) * spc * bps]
            nextcl += 1
        return start

    # One level of subdirectory, for \AUTO.
    #
    # EmuTOS runs \AUTO\*.PRG before the desktop appears, which is the
    # only way to start a program on this machine without a pointer --
    # and steering a pointer onto an icon by dead reckoning was the weak
    # link that invalidated a whole day of filesystem testing. A
    # subdirectory is a cluster chain holding 32-byte entries like the
    # root, plus the "." and ".." pair every directory must open with;
    # ".." carries cluster 0 when the parent is the root.
    subdirs = {}
    for host, dosname in files:
        if b"/" in dosname:
            d, _, rest = dosname.partition(b"/")
            subdirs.setdefault(d, []).append((host, rest))

    for host, dosname in files:
        if b"/" in dosname:
            continue
        data = open(host, "rb").read()
        add_dirent(dosname, 0x00, store(data), len(data))

    for dname, members in subdirs.items():
        # Reserve the directory's own cluster first so "." can name it.
        dcl = nextcl
        entries = bytearray()

        def dent(nm, attr, cl, ln):
            e = bytearray(32)
            e[0:11] = nm
            e[11] = attr
            struct.pack_into("<H", e, 26, cl)
            struct.pack_into("<I", e, 28, ln)
            entries.extend(e)

        dent(b".".ljust(11), 0x10, dcl, 0)
        dent(b"..".ljust(11), 0x10, 0, 0)      # parent is the root
        placed = []
        for host, member in members:
            placed.append((member, open(host, "rb").read()))
        # The directory occupies one cluster; its members follow it.
        need = len(entries) + 32 * len(placed)
        if need > spc * bps:
            sys.exit("subdirectory %s needs more than one cluster" % dname)
        nextcl += 1                            # dcl is now spoken for
        fat_set(dcl, END)
        for member, data in placed:
            dent(member, 0x00, store(data), len(data))
        base = dataoff + (dcl - 2) * spc * bps
        img[base:base + len(entries)] = entries
        add_dirent(dname, 0x10, dcl, 0)

    if entropy:
        # No zero regions anywhere the drive has to read.
        #
        # A CD-R sector of 2048 identical bytes is the weakest sector
        # the format can hold: mode-1 data is scrambled before writing
        # precisely because long uniform runs produce pathological pit
        # patterns, and a 1992 lens reading a 10x burn is where that
        # stops being theory. Hardware here failed at LBA 1608 and
        # 1618 -- the FAT tail and the root directory, both all zeros
        # -- while every structured sector on the same disc read fine.
        #
        # Three regions, three legal fillers:
        #   free clusters   may hold anything at all; the FAT says free
        #   unused dirents  0xE5 marks deleted, and unlike 0x00 it does
        #                   not end the directory scan, so the rest of
        #                   the entry is free space
        #   FAT tail        entries for free clusters must read zero, so
        #                   this one is answered by allocating almost
        #                   every cluster instead (see build-iso.sh)
        x = 0x13579BD

        def rnd():
            nonlocal x
            x = (1103515245 * x + 12345) & 0x7FFFFFFF
            return ((x >> 16) & 0xFF) or 0x3C

        for c in range(nextcl, nclust + 2):
            base = dataoff + (c - 2) * spc * bps
            for i in range(spc * bps):
                img[base + i] = rnd()
        # The end-of-directory marker has to survive.
        #
        # Filling every unused entry with 0xE5 -- deleted, keep going --
        # deleted the 0x00 that tells a scanner to stop, so a directory
        # lookup that used to finish inside the first 512-byte block had
        # to walk all thirty-two of them: eight CD sectors, including
        # the ones this drive fails on, to find a file sitting in entry
        # one. That turned read trouble into 'file not found'.
        #
        # Only the FIRST BYTE of the terminating entry must be zero, and
        # nothing past that entry is ever read -- so the marker stays,
        # its own remaining bytes carry entropy, and every entry after
        # it is filled outright.
        e = rootoff + dirent * 32
        img[e] = 0x00
        for i in range(1, 32):
            img[e + i] = rnd()
        for e in range(rootoff + (dirent + 1) * 32,
                       rootoff + rootent * 32):
            img[e] = rnd()

    img[fat0:fat0 + len(fat)] = fat
    img[fat1:fat1 + len(fat)] = fat

    # The signature the hard-disk path looks for. EmuTOS reads block 0 of
    # a unit and, seeing 0x55AA with a plausible BPB behind it and no
    # partition table, mounts the whole device as one BGM partition --
    # which is exactly how D: gets a drive letter without us writing a
    # partition table at all.
    struct.pack_into("<H", img, 510, 0xAA55)
    return bytes(img)


def dosify(name):
    """AUTO/FOO.PRG keeps its one directory level; the rest is 8.3.

    The separator survives into the encoded name as a single b"/" so
    build() can split on it -- the two halves are each padded to 11
    bytes exactly as a directory entry wants them."""
    name = name.upper()
    if "/" in name:
        d, rest = name.split("/", 1)
        return d[:8].ljust(11).encode("ascii") + b"/" + dosify(rest)
    if "." in name:
        stem, ext = name.rsplit(".", 1)
    else:
        stem, ext = name, ""
    return (stem[:8].ljust(8) + ext[:3].ljust(3)).encode("ascii")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out")
    ap.add_argument("--size", type=lambda v: int(v, 0), required=True)
    ap.add_argument("--label", default="")
    ap.add_argument("--oem", default="EmuTOS")
    ap.add_argument("--add", action="append", default=[])
    ap.add_argument("--fat16", action="store_true",
                    help="for a partition mounted through the hard-disk path")
    ap.add_argument("--entropy", action="store_true",
                    help="leave no all-zero sectors: fill free clusters and "
                         "unused directory entries (weak-sector avoidance)")
    a = ap.parse_args()

    files = []
    for spec in a.add:
        if ":" in spec:
            host, dos = spec.split(":", 1)
        else:
            host, dos = spec, spec.rsplit("/", 1)[-1]
        files.append((host, dosify(dos)))

    open(a.out, "wb").write(build(a.size, a.label, a.oem, files, a.fat16,
                                  a.entropy))
    print("%s: %d bytes, %d file(s)" % (a.out, a.size, len(files)))


if __name__ == "__main__":
    main()
