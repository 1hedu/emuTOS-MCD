#!/usr/bin/env python3
"""Assemble Sonic's animation frames out of the ROM's own art, mappings and DPLC.

Sonic is not stored as pictures.  He is stored as a pool of 8x8 tiles
(Art_Sonic), a per-frame list of which runs of that pool to DMA into VRAM (the
dynamic pattern load cues), and a per-frame list of sprite pieces that say where
to put the loaded tiles.  A piece is 1-4 tiles wide and 1-4 tall, which is
exactly a Mega Drive hardware sprite -- so the port can place the original's own
pieces as its own sprites, rather than flattening anything.

Formats, from s1disasm/_maps/_MapMacros.asm with SonicMappingsVer=1, SonicDplcVer=1:

    spriteHeader   db  piece count
    spritePiece    db  y
                   db  ((width-1)&3)<<2 | ((height-1)&3)
                   dw  pri<<15 | pal<<13 | yflip<<12 | xflip<<11 | tile
                   db  x
    dplcHeader     db  entry count
    dplcEntry      dw  (tiles-1)<<12 | art tile offset

Verified against the cartridge: Art_Sonic, Pal_Sonic and the compiled bytes of
MS_Stand and SonPLC_Stand are all present verbatim in a genuine REV00 dump.
"""
import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rom_tiles import tile_pixels, write_png  # noqa: E402

ART_SONIC = 0x021AFE          # 1289 tiles
ART_SONIC_LEN = 41248
PAL_SONIC = 0x002388


def parse_order(path, prefix):
    """The mappingsTable entries, in table order."""
    names = []
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.search(r'mappingsTableEntry\.w\s+(\S+)', line)
        if m:
            names.append(m.group(1))
    return names


def parse_blocks(path, kind):
    """label -> list of argument tuples for every spritePiece / dplcEntry."""
    blocks, cur = {}, None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.split(';')[0].rstrip()
        if not line:
            continue
        m = re.match(r'^([A-Za-z_.][\w.]*):?\s+(spriteHeader|dplcHeader)', line)
        if m:
            cur = m.group(1)
            blocks[cur] = []
            continue
        m = re.search(r'\b(spritePiece|dplcEntry)\s+(.*)$', line)
        if m and cur is not None and m.group(1) == kind:
            args = [a.strip() for a in m.group(2).split(',')]
            blocks[cur].append([num(a) for a in args])
    return blocks


def num(tok):
    tok = tok.strip()
    neg = tok.startswith('-')
    if neg:
        tok = tok[1:]
    v = int(tok[1:], 16) if tok.startswith('$') else int(tok, 0)
    return -v if neg else v


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rom', default='assets/sonic/sonic1.md')
    ap.add_argument('--disasm', default='/workspace/sonicretro/s1disasm')
    ap.add_argument('--out', default='build/sonic')
    ap.add_argument('--png', action='store_true', help='also write a PNG per frame')
    a = ap.parse_args()

    rom = open(a.rom, 'rb').read()
    art = rom[ART_SONIC:ART_SONIC + ART_SONIC_LEN]
    pal = [struct.unpack_from('>H', rom, PAL_SONIC + i * 2)[0] for i in range(16)]

    mpath = os.path.join(a.disasm, '_maps', 'Sonic.asm')
    dpath = os.path.join(a.disasm, '_maps', 'Sonic - Dynamic Gfx Script.asm')
    order = parse_order(mpath, 'MS_')
    dorder = parse_order(dpath, 'SonPLC_')
    pieces = parse_blocks(mpath, 'spritePiece')
    dplcs = parse_blocks(dpath, 'dplcEntry')
    assert len(order) == len(dorder), (len(order), len(dorder))

    os.makedirs(a.out, exist_ok=True)
    rgb = []
    for w in pal:
        b, g, r = (w >> 8) & 0xE, (w >> 4) & 0xE, w & 0xE
        rgb.append((r * 0x11, g * 0x11, b * 0x11))

    frames = []
    for idx, (mn, dn) in enumerate(zip(order, dorder)):
        plist = pieces.get(mn, [])
        dlist = dplcs.get(dn, [])

        # DPLC: concatenate the named runs of the art pool -> this frame's VRAM.
        vram = []
        for tiles, off in dlist:
            vram.extend(range(off, off + tiles))

        out_pieces = []
        for (x, y, w, h, tile, xf, yf, p, pri) in plist:
            out_pieces.append(dict(x=x, y=y, w=w, h=h, tile=tile,
                                   xflip=xf, yflip=yf, pal=p, pri=pri))
        frames.append(dict(index=idx, name=mn, vram=vram, pieces=out_pieces))

        if a.png and plist:
            render(art, vram, out_pieces, rgb,
                   os.path.join(a.out, '%03d_%s.png' % (idx, mn)))

    json.dump(dict(art=ART_SONIC, art_len=ART_SONIC_LEN, palette=pal,
                   frames=frames), open(os.path.join(a.out, 'frames.json'), 'w'),
              indent=1)
    open(os.path.join(a.out, 'art_sonic.bin'), 'wb').write(art)
    print('%d frames, %d tiles of art, palette %s'
          % (len(frames), ART_SONIC_LEN // 32,
             ' '.join('%04X' % v for v in pal[:4]) + ' ...'))


def render(art, vram, pieces, rgb, path):
    xs = [p['x'] for p in pieces] + [p['x'] + p['w'] * 8 for p in pieces]
    ys = [p['y'] for p in pieces] + [p['y'] + p['h'] * 8 for p in pieces]
    x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)
    W, H = x1 - x0, y1 - y0
    grid = [[(0x60, 0x60, 0x70)] * W for _ in range(H)]
    for p in pieces:
        n = p['tile']
        for cx in range(p['w']):
            for cy in range(p['h']):
                if n >= len(vram):
                    continue
                px = tile_pixels(art, vram[n] * 32)
                n += 1
                for yy in range(8):
                    for xx in range(8):
                        v = px[yy][xx]
                        if not v:
                            continue
                        sx = cx if not p['xflip'] else p['w'] - 1 - cx
                        sy = cy if not p['yflip'] else p['h'] - 1 - cy
                        ox = p['x'] - x0 + sx * 8 + (7 - xx if p['xflip'] else xx)
                        oy = p['y'] - y0 + sy * 8 + (7 - yy if p['yflip'] else yy)
                        if 0 <= ox < W and 0 <= oy < H:
                            grid[oy][ox] = rgb[v]
    write_png(path, W, H, grid)


if __name__ == '__main__':
    main()
