#!/usr/bin/env python3
"""Render 4bpp Mega Drive tiles straight out of a ROM as a PNG, so a region can
be looked at rather than guessed at.

A Mega Drive tile is 32 bytes: 8 rows of 8 pixels, one nibble each, high nibble
first.  Index 0 is transparent for sprites.

    tools/sonic/rom_tiles.py ROM 0x50000 --tiles 256 --cols 16 -o out.png
    tools/sonic/rom_tiles.py ROM 0x50000 --tiles 60 --cols 3 --stack -o sonic.png

--stack lays the tiles out the way the VDP consumes a sprite: column-major, so
a 3x5 sprite's fifteen tiles come out as the actual 24x40 picture.
"""
import argparse
import struct
import zlib

# A neutral ramp: index 0 shows as mid-grey so transparent pixels are visible
# as a background rather than as black shapes.
RAMP = [(0x60, 0x60, 0x70)] + [
    (v, v, v) for v in (0x00, 0x1c, 0x38, 0x54, 0x70, 0x8c, 0xa8, 0xc4,
                        0xd8, 0xe4, 0xf0, 0xf8, 0xff, 0xc0, 0x88)
]


def md_palette(rom, off):
    """Sixteen Mega Drive CRAM words at off -> RGB tuples."""
    out = []
    for i in range(16):
        w = struct.unpack_from('>H', rom, off + i * 2)[0]
        b, g, r = (w >> 8) & 0xE, (w >> 4) & 0xE, w & 0xE
        out.append((r * 0x11, g * 0x11, b * 0x11))
    return out


def tile_pixels(rom, off):
    """32 bytes -> 8 rows of 8 palette indices."""
    rows = []
    for y in range(8):
        row = []
        for x in range(4):
            byte = rom[off + y * 4 + x]
            row.append(byte >> 4)
            row.append(byte & 15)
        rows.append(row)
    return rows


def write_png(path, w, h, rgb_rows):
    raw = b''.join(b'\0' + bytes(v for px in row for v in px) for row in rgb_rows)

    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c))

    png = (b'\x89PNG\r\n\x1a\n'
           + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9))
           + chunk(b'IEND', b''))
    open(path, 'wb').write(png)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('rom')
    ap.add_argument('offset')
    ap.add_argument('--tiles', type=int, default=256)
    ap.add_argument('--cols', type=int, default=16)
    ap.add_argument('--stack', action='store_true',
                    help='column-major, the way a VDP sprite is stored')
    ap.add_argument('--palette', help='ROM offset of a 16-word CRAM palette')
    ap.add_argument('--scale', type=int, default=1)
    ap.add_argument('-o', '--out', required=True)
    a = ap.parse_args()

    rom = open(a.rom, 'rb').read()
    off = int(a.offset, 0)
    pal = md_palette(rom, int(a.palette, 0)) if a.palette else RAMP
    if a.palette:
        pal[0] = (0x60, 0x60, 0x70)

    cols = a.cols
    rows_of_tiles = (a.tiles + cols - 1) // cols
    w, h = cols * 8, rows_of_tiles * 8
    grid = [[(0, 0, 0)] * w for _ in range(h)]

    for n in range(a.tiles):
        if a.stack:
            tx, ty = n // rows_of_tiles, n % rows_of_tiles
            if tx >= cols:
                break
        else:
            tx, ty = n % cols, n // cols
        px = tile_pixels(rom, off + n * 32)
        for y in range(8):
            for x in range(8):
                grid[ty * 8 + y][tx * 8 + x] = pal[px[y][x]]

    if a.scale > 1:
        s = a.scale
        grid = [[p for p in row for _ in range(s)] for row in grid for _ in range(s)]
        w, h = w * s, h * s
    write_png(a.out, w, h, grid)
    print('%s  %dx%d  %d tiles from $%06X' % (a.out, w, h, a.tiles, off))


if __name__ == '__main__':
    main()
