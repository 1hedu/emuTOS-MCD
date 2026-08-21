#!/usr/bin/env python3
"""Write a Degas .PI1: ST low resolution, 320x200, 16 colours.

There is a picture on the disc so that SHOW.PRG has something to show
on a machine that has never had a file transferred to it. It is
generated rather than shipped because a generated picture has no
licence attached to it, and this project distributes nobody's artwork.

The image is the Mandelbrot set, which is arithmetic rather than art
and therefore free, and which happens to suit sixteen colours: the
escape-time bands are contours, so a small palette reads as deliberate
shading instead of as posterisation.

Layout, from the Degas specification:
    word    resolution, 0 for ST low
    16 words palette, 0x0RGB, three bits per channel
    32000 bytes bitmap -- four bitplanes interleaved a word at a time,
            so each group of sixteen pixels is four words, plane 0
            first, each word holding that plane's bit for those pixels
            with the leftmost pixel in bit 15.
"""

import struct
import sys

W, H = 320, 200

# A dusk ramp: near-black through blue and magenta into warm white, with
# the interior black. Three bits per channel is all an ST has.
PALETTE = [
    0x000, 0x001, 0x102, 0x203, 0x304, 0x405, 0x516, 0x627,
    0x738, 0x748, 0x759, 0x76a, 0x77b, 0x77c, 0x77e, 0x777,
]


def escape(cx, cy, limit=15):
    """Iterations before |z| leaves the circle of radius 2, capped."""
    zx = zy = 0.0
    for i in range(limit):
        zx2, zy2 = zx * zx, zy * zy
        if zx2 + zy2 > 4.0:
            return i
        zx, zy = zx2 - zy2 + cx, 2.0 * zx * zy + cy
    return limit


def pixels():
    """One pen index per pixel, row by row."""
    out = bytearray(W * H)
    for y in range(H):
        cy = -1.15 + (2.30 * y) / H
        row = y * W
        for x in range(W):
            cx = -2.05 + (2.90 * x) / W
            n = escape(cx, cy)
            # 15 is the interior; it gets pen 0 so the set reads black
            # and the ramp is spent entirely on the outside.
            out[row + x] = 0 if n >= 15 else (n + 1)
    return out


def planar(pens):
    """Pen indices -> ST low-resolution interleaved bitplanes."""
    out = bytearray()
    for y in range(H):
        row = y * W
        for g in range(0, W, 16):
            for p in range(4):
                w = 0
                for i in range(16):
                    if (pens[row + g + i] >> p) & 1:
                        w |= 0x8000 >> i
                out += struct.pack('>H', w)
    return out


def main():
    if len(sys.argv) != 2:
        sys.exit("usage: mkpi1.py out.pi1")
    body = planar(pixels())
    assert len(body) == 32000, len(body)
    hdr = struct.pack('>H', 0) + b''.join(struct.pack('>H', c) for c in PALETTE)
    with open(sys.argv[1], 'wb') as f:
        f.write(hdr + body)
    print("%s: %d bytes" % (sys.argv[1], len(hdr) + len(body)))


if __name__ == '__main__':
    main()
