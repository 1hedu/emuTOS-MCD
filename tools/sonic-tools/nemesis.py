#!/usr/bin/env python3
"""Nemesis decompression, the format nearly all of Sonic 1's art is stored in.

Header word: tile count, bit 15 set means XOR mode (each row is XORed with the
previous one).  Then a code table, then a bitstream.

Code table entries:
    $FF                 end of table
    $80..$FE            low nibble is the palette index for what follows,
                        and the next byte is the entry descriptor
    %0RRRLLLL           RRR = repeat count - 1, LLLL = code length in bits;
                        the following byte is the code, right-aligned

Bitstream: read bits MSB first.  Six set bits in a row is the inline escape --
the next seven bits are %RRRPPPP, a literal repeat count and palette index.
Otherwise accumulate bits until they match a table entry.

Output is nibbles: eight to a tile row, eight rows to a tile.
"""
import struct


class Bits:
    def __init__(self, data, pos):
        self.d, self.p, self.b = data, pos, 0

    def bit(self):
        if self.p >= len(self.d):
            raise EOFError
        v = (self.d[self.p] >> (7 - self.b)) & 1
        self.b += 1
        if self.b == 8:
            self.b, self.p = 0, self.p + 1
        return v

    def bits(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | self.bit()
        return v


def decompress(rom, off, limit_tiles=0x1000):
    n = struct.unpack_from('>H', rom, off)[0]
    xor = bool(n & 0x8000)
    tiles = n & 0x7FFF
    if not 0 < tiles <= limit_tiles:
        raise ValueError('implausible tile count %d' % tiles)

    p = off + 2
    table = {}
    pal = 0
    while True:
        if p >= len(rom):
            raise EOFError
        b = rom[p]
        p += 1
        if b == 0xFF:
            break
        if b >= 0x80:
            pal = b & 0x0F
            if p >= len(rom):
                raise EOFError
            b = rom[p]
            p += 1
            if b == 0xFF:
                break
        length = b & 0x0F
        if not 1 <= length <= 8:
            raise ValueError('bad code length %d' % length)
        repeat = ((b >> 4) & 0x07) + 1
        code = rom[p]
        p += 1
        table[(length, code)] = (pal, repeat)
    if not table:
        raise ValueError('empty code table')

    bs = Bits(rom, p)
    need = tiles * 64                       # nibbles
    out = []
    while len(out) < need:
        # Six set bits is the inline escape.
        v, ln = 0, 0
        while True:
            v = (v << 1) | bs.bit()
            ln += 1
            if ln == 6 and v == 0x3F:
                w = bs.bits(7)
                out += [w & 0x0F] * (((w >> 4) & 7) + 1)
                break
            if (ln, v) in table:
                pi, rp = table[(ln, v)]
                out += [pi] * rp
                break
            if ln >= 8:
                raise ValueError('no code matches %d bits %02X' % (ln, v))
    out = out[:need]

    # Nibbles -> tile bytes, applying XOR mode row by row.
    data = bytearray()
    # XOR mode is one running accumulator over the whole output, not one per
    # tile.  NemDecMain does `moveq #0,d2` once before the loop and
    # NemPCD_WriteRowToVDP_XOR does `eor.l d4,d2 / move.l d2,(a4)` -- d2 is
    # never reset, so the carry runs straight through every tile boundary.
    prev = 0
    for r in range(tiles * 8):
        row = 0
        for i in range(8):
            row = (row << 4) | out[r * 8 + i]
        if xor:
            row ^= prev
            prev = row
        data += struct.pack('>I', row)
    return bytes(data), tiles, xor, p


if __name__ == '__main__':
    import sys
    rom = open(sys.argv[1], 'rb').read()
    art, n, x, end = decompress(rom, int(sys.argv[2], 0))
    out = sys.argv[3] if len(sys.argv) > 3 else '/dev/stdout'
    open(out, 'wb').write(art)
    print('%d tiles, xor=%s, stream ends $%06X' % (n, x, end))
