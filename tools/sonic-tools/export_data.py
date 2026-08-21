#!/usr/bin/env python3
"""Pack Sonic's art and frame records into one blob the ROM can .incbin.

Everything stays in the cartridge's own shape: the tile pool verbatim, the DPLC
runs verbatim, and the sprite pieces verbatim.  The only thing added is an index
so the 68000 can find a frame without walking the whole table.

    +$00  .word  frame count
    +$02  .word  tiles in the art pool
    +$04  .long  offset of the art pool
    +$08  .long  offset of the frame index
    +$0C  .word  16 palette entries, as CRAM words
    +$2C  .word  sign post frame count
    +$2E  .word  tiles in the sign post's art
    +$30  .long  offset of the sign post's art
    +$34  .long  offset of the sign post's frame index
    +$38  .word  dust frame count
    +$3A  .word  tiles in the dust's art
    +$3C  .long  offset of the dust's art
    +$40  .long  offset of the dust's frame index
    ...
    frame index: one .long per frame, offset of its record (0 = no frame)
    record:  .byte  piece count
             .byte  DPLC entry count
             .word  * DPLC entries, (tiles-1)<<12 | art tile
             5 bytes * pieces, y / size / tile+flags word / x

The sign post uses the same record layout with no DPLC entries: its art is
loaded once and its pieces index it directly, which is how the cartridge does it
too -- Map_Sign has no dynamic pattern load cues.  It is on Sonic's own palette:
Signpost.asm's pieces all carry pal 0, and Pal_Index puts Pal_Sonic in
v_palette_line_1, which is VDP line 0.

The dust is object 08 and it is Sonic 2's, so it comes out of the other dump.
Three things about it are worth knowing before reading the code:

  * its art is not compressed.  `ArtUnc_SplashAndDust` is 6464 bytes of plain
    tiles, and `Obj08_LoadDustOrSplashArt` DMAs slices of it straight out of
    the ROM, so there is nothing to decode -- only to find, which is done by
    matching the disassembly's own copy against the dump.
  * Sonic 2's mappings are a different format from Sonic 1's.  `SonicMappingsVer`
    is 2 there and 1 here: a word count instead of a byte, eight bytes a piece
    instead of five, a word x position, and a second tile word for two-player
    mode.  The arguments to `spritePiece` are the same nine either way, so this
    reads the source and writes them out in the one shape the 68000 side knows.
  * the four skid cels ($11..$14) carry an *empty* DPLC in the cartridge and
    read tiles a sibling object loaded -- the sixteen at art $BA, which is
    entry $15's cue and covers all four cels at once.  They are given that cue
    here, because this port has no sibling to inherit from.
"""
import argparse
import json
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from nemesis import decompress                                    # noqa: E402

ART_SIGNPOST = 0x03A53E         # Nemesis, 58 tiles
SIGN_MAP = '_maps/Signpost.asm'

# Object 08, out of Sonic 2.  The art is uncompressed, so it is located in the
# dump by matching the disassembly's own copy of it -- which both finds it and
# says the dump is the revision these mappings were written against.
DUST_ART = 'art/uncompressed/Splash and skid dust.bin'
DUST_MAP = 'mappings/sprite/obj08.asm'
DUST_DPLC = 'mappings/spriteDPLC/obj08.asm'
# DPLC_obj08_0078: sixteen tiles at art $BA, which is the whole skid set.
DUST_SKID_FRAMES = (0x11, 0x12, 0x13, 0x14)
DUST_SKID_DPLC = [(0x10, 0xBA)]

FRAMES = 'frames.json'


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', default='build/sonic/frames.json')
    ap.add_argument('--rom', default='assets/sonic/sonic1.md')
    ap.add_argument('--rom2', default='assets/sonic/sonic2.md')
    ap.add_argument('-o', '--out', default='build/sonic/sonic_data.bin')
    ap.add_argument('--inc', default='build/sonic/sonic_frames.inc')
    ap.add_argument('--disasm', default='/workspace/sonicretro/s1disasm')
    ap.add_argument('--disasm2', default='/workspace/sonicretro/s2disasm')
    a = ap.parse_args()

    fr = json.load(open(a.frames))
    rom = open(a.rom, 'rb').read()
    art = rom[fr['art']:fr['art'] + fr['art_len']]

    records = []
    for f in fr['frames']:
        if not f['pieces']:
            records.append(None)
            continue
        r = bytearray()
        r.append(len(f['pieces']))
        # Rebuild the DPLC runs from the expanded tile list.
        runs = []
        for t in f['vram']:
            if runs and runs[-1][0] + runs[-1][1] == t and runs[-1][1] < 16:
                runs[-1][1] += 1
            else:
                runs.append([t, 1])
        r.append(len(runs))
        for off, n in runs:
            r += struct.pack('>H', ((n - 1) & 0xF) << 12 | (off & 0xFFF))
        for p in f['pieces']:
            word = ((p['pri'] & 1) << 15 | (p['pal'] & 3) << 13
                    | (p['yflip'] & 1) << 12 | (p['xflip'] & 1) << 11
                    | (p['tile'] & 0x7FF))
            r += struct.pack('>bBHb', p['y'], ((p['w'] - 1) & 3) << 2
                             | ((p['h'] - 1) & 3), word, p['x'])
        records.append(bytes(r))

    sign_art, sign_tiles, _, _ = decompress(rom, ART_SIGNPOST)
    sign_records = [pack_pieces(p) for p in parse_sign(os.path.join(a.disasm, SIGN_MAP))]

    d_art, d_at = dust_art(open(a.rom2, 'rb').read(), a.disasm2)
    dust_recs = dust_records(a.disasm2)

    n = len(records)
    sn = len(sign_records)
    dn = len(dust_recs)
    head = bytearray()
    head += struct.pack('>HH', n, len(art) // 32)
    head += struct.pack('>II', 0, 0)                 # patched below
    for w in fr['palette']:
        head += struct.pack('>H', w)
    head += struct.pack('>HH', sn, sign_tiles)
    head += struct.pack('>II', 0, 0)                 # patched below
    head += struct.pack('>HH', dn, len(d_art) // 32)
    head += struct.pack('>II', 0, 0)                 # patched below
    idx_off = len(head)
    body = bytearray()
    offsets = []
    for r in records:
        if r is None:
            offsets.append(0)
        else:
            offsets.append(len(body))
            body += r
            if len(body) & 1:
                body += b'\0'
    sign_idx_off = idx_off + n * 4 + len(body)
    sign_body = bytearray()
    sign_offsets = []
    for r in sign_records:
        sign_offsets.append(len(sign_body))
        sign_body += r
        if len(sign_body) & 1:
            sign_body += b'\0'
    dust_idx_off = sign_idx_off + sn * 4 + len(sign_body)
    dust_body = bytearray()
    dust_offsets = []
    for r in dust_recs:
        dust_offsets.append(len(dust_body))
        dust_body += r
        if len(dust_body) & 1:
            dust_body += b'\0'
    sign_art_off = dust_idx_off + dn * 4 + len(dust_body)
    if sign_art_off & 1:
        sign_art_off += 1
    dust_art_off = sign_art_off + len(sign_art)
    if dust_art_off & 1:
        dust_art_off += 1
    art_off = dust_art_off + len(d_art)
    if art_off & 1:
        art_off += 1
    struct.pack_into('>II', head, 4, art_off, idx_off)
    struct.pack_into('>II', head, 0x30, sign_art_off, sign_idx_off)
    struct.pack_into('>II', head, 0x3C, dust_art_off, dust_idx_off)

    out = bytearray(head)
    base = idx_off + n * 4
    for r, o in zip(records, offsets):
        out += struct.pack('>I', 0 if r is None else base + o)
    out += body
    sbase = sign_idx_off + sn * 4
    for o in sign_offsets:
        out += struct.pack('>I', sbase + o)
    out += sign_body
    dbase = dust_idx_off + dn * 4
    for o in dust_offsets:
        out += struct.pack('>I', dbase + o)
    out += dust_body
    while len(out) < sign_art_off:
        out += b'\0'
    out += sign_art
    while len(out) < dust_art_off:
        out += b'\0'
    out += d_art
    while len(out) < art_off:
        out += b'\0'
    out += art

    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    open(a.out, 'wb').write(out)

    names = {f['name']: f['index'] for f in fr['frames']}
    with open(a.inc, 'w') as fh:
        fh.write('/* Generated by tools/sonic/export_data.py -- frame indices,\n'
                 ' * which are the cartridge\'s own fr_* numbers. */\n')
        for nm, i in sorted(names.items(), key=lambda kv: kv[1]):
            fh.write('.equ SN_%s, %d\n' % (nm.replace('MS_', '').upper(), i))
    print('%s  %d bytes  (%d frames, %d art tiles; sign post %d frames, %d '
          'tiles; obj08 dust %d frames, %d tiles from $%06X)'
          % (a.out, len(out), n, len(art) // 32, sn, sign_tiles,
             dn, len(d_art) // 32, d_at))


def num(tok):
    tok = tok.strip()
    neg = tok.startswith('-')
    if neg:
        tok = tok[1:]
    v = int(tok[1:], 16) if tok.startswith('$') else int(tok, 0)
    return -v if neg else v


def parse_sign(path):
    """Map_Sign, in mappingsTable order.  Same spritePiece macro as Sonic's."""
    return parse_blocks(path, 'spriteHeader', 'spritePiece')


def parse_blocks(path, header, entry):
    """A mappings- or DPLC-table source file, in table order.  The two have the
    same shape -- a table of entries naming labelled blocks -- and differ only
    in which macro opens a block and which fills it."""
    order, blocks, cur = [], {}, None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.split(';')[0].rstrip()
        m = re.search(r'mappingsTableEntry\.w\s+(\S+)', line)
        if m:
            order.append(m.group(1))
            continue
        m = re.match(r'^([A-Za-z_.][\w.]*):?\s+' + header + r'\b', line)
        if m:
            cur = m.group(1)
            blocks[cur] = []
            continue
        m = re.search(r'\b' + entry + r'\s+(.*)$', line)
        if m and cur is not None:
            blocks[cur].append([num(t) for t in m.group(1).split(',')])
    return [blocks[nm] for nm in order]


def dust_records(disasm2):
    """Object 08's frames, in the cartridge's own numbering."""
    maps = parse_blocks(os.path.join(disasm2, DUST_MAP),
                        'spriteHeader', 'spritePiece')
    dplc = parse_blocks(os.path.join(disasm2, DUST_DPLC),
                        'dplcHeader', 'dplcEntry')
    if len(maps) != len(dplc):
        raise SystemExit('obj08: %d mapping frames but %d DPLC frames'
                         % (len(maps), len(dplc)))
    out = []
    for i, (pieces, cues) in enumerate(zip(maps, dplc)):
        if i in DUST_SKID_FRAMES:
            cues = DUST_SKID_DPLC
        r = bytearray([len(pieces), len(cues)])
        for tiles, off in cues:
            r += struct.pack('>H', ((tiles - 1) & 0xF) << 12 | (off & 0xFFF))
        for (x, y, w, h, tile, xf, yf, pal, pri) in pieces:
            word = ((pri & 1) << 15 | (pal & 3) << 13 | (yf & 1) << 12
                    | (xf & 1) << 11 | (tile & 0x7FF))
            r += struct.pack('>bBHb', y, ((w - 1) & 3) << 2 | ((h - 1) & 3),
                             word, x)
        out.append(bytes(r))
    return out


def dust_art(rom2, disasm2):
    """The art, taken from the dump and proved against the disassembly's copy.
    It is uncompressed, so this is a search and a slice and nothing else."""
    want = open(os.path.join(disasm2, DUST_ART), 'rb').read()
    at = rom2.find(want)
    if at < 0:
        raise SystemExit('the dust art is not in this Sonic 2 dump')
    return rom2[at:at + len(want)], at


def pack_pieces(pieces):
    """One frame record with no DPLC entries."""
    r = bytearray([len(pieces), 0])
    for (x, y, w, h, tile, xf, yf, pal, pri) in pieces:
        word = ((pri & 1) << 15 | (pal & 3) << 13 | (yf & 1) << 12
                | (xf & 1) << 11 | (tile & 0x7FF))
        r += struct.pack('>bBHb', y, ((w - 1) & 3) << 2 | ((h - 1) & 3), word, x)
    return bytes(r)


if __name__ == '__main__':
    main()
