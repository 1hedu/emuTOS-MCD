#!/usr/bin/env python3
"""Saxman decompression -- the LZSS variant Sonic 2 keeps its Z80 driver in.

Sonic 2's sound driver is not reimplemented anywhere in this port; it is the
cartridge's own Z80 program, and it lives in the ROM compressed.  This is the
decompressor, written from the algorithm the cartridge itself implements at
`SaxDec_GetByte` in s2.asm -- so the format description here is the one in the
ROM, not one from a wiki.

The shape is ordinary LZSS with one oddity worth stating, because it is the
only part that is easy to get silently wrong:

  * a description byte carries eight flags, LSB first;
  * a set flag means one literal byte;
  * a clear flag means two bytes -- `lo`, then `hi` whose high nibble extends
    the offset and whose low nibble is the run length minus three;
  * the offset is a position in a 4096-byte sliding window that starts as
    zeroes, biased by `+$12`, and it wraps.  A run may therefore read window
    bytes the stream has not written yet, and those read as zero rather than
    as whatever happens to be there.  Getting that wrong decodes the first
    kilobyte correctly and turns the rest into noise, which is the same shape
    of mistake as the Nemesis XOR accumulator elsewhere in this project.

Usage:
    python3 tools/sonic/saxman.py <rom> <offset> <compressed-size> -o out.bin
"""
from __future__ import annotations
import argparse
from pathlib import Path

WINDOW = 0x1000
BIAS = 0x12


def decompress(data: bytes, size: int | None = None) -> bytes:
    """`data` is the compressed stream; `size` its length if it is embedded in
    a larger image.  This follows DecompressSoundDriver instruction for
    instruction -- the register names in the comments are that routine's."""
    if size is None:
        size = len(data)
    out = bytearray()
    src = 0
    desc = 0                                    # d6
    while src < size:
        desc >>= 1                              # lsr.w #1,d6
        if not desc & 0x100:                    # btst #8,d6 -- out of bits?
            if src >= size:
                break
            desc = data[src] | 0xFF00           # ori.w #$FF00,d6
            src += 1
        if desc & 1:
            if src >= size:
                break
            out.append(data[src])               # a literal byte
            src += 1
            continue
        if src + 1 >= size:
            break
        lo, hi = data[src], data[src + 1]
        src += 2
        run = (hi & 0x0F) + 2                   # d3, the length minus one
        off = (lo + ((hi & 0xF0) << 4) + BIAS) & (WINDOW - 1)
        # The high nibble is taken from where the output has got to; if that
        # lands past the end, it means the previous $1000 block, and if THAT
        # would go before the start it is not a reference at all but a run of
        # zeroes.
        end = len(out)
        off += end & 0xF000
        if end < off:
            off -= WINDOW
            if off < 0:
                out.extend(b'\0' * (run + 1))
                continue
        for _ in range(run + 1):
            out.append(out[off] if off < len(out) else 0)
            off += 1
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', type=Path)
    ap.add_argument('offset', type=lambda s: int(s, 0))
    ap.add_argument('size', type=lambda s: int(s, 0))
    ap.add_argument('-o', '--out', type=Path)
    a = ap.parse_args()
    rom = a.rom.read_bytes()
    out = decompress(rom[a.offset:a.offset + a.size])
    if a.out:
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_bytes(out)
    print('%d compressed -> %d bytes' % (a.size, len(out)))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
