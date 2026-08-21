#!/usr/bin/env python3
"""De-interleave a Super Magic Drive (.smd) dump into a plain Mega Drive image.

A .smd is not a ROM, it is a copier's on-disk format, and nothing that reads a
Mega Drive image can make sense of one until it is undone:

  * a 512-byte header comes first and is thrown away;
  * the rest is 16 KiB blocks, and inside each block the first 8 KiB are the
    ODD bytes of the block in order and the second 8 KiB are the EVEN bytes.

So the file looks like a ROM at a glance -- right size, roughly right entropy --
and every word in it is scrambled.  The giveaway is the size: a real dump is a
power of two, an .smd is that plus 512.

    python3 tools/sonic/smd_to_bin.py sonic2.smd -o assets/sonic/sonic2.md

The ROM itself is not kept in the tree; this is, so a dump can always be turned
back into one.
"""
import argparse
from pathlib import Path

HEADER = 512
BLOCK = 0x4000
HALF = BLOCK // 2


def deinterleave(data: bytes) -> bytes:
    if len(data) % BLOCK == HEADER:
        data = data[HEADER:]
    elif len(data) % BLOCK:
        raise SystemExit(f'{len(data)} bytes is neither a bare nor a headered .smd')
    out = bytearray(len(data))
    for base in range(0, len(data), BLOCK):
        block = data[base:base + BLOCK]
        out[base + 1:base + BLOCK:2] = block[:HALF]
        out[base:base + BLOCK:2] = block[HALF:]
    return bytes(out)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('smd', type=Path)
    ap.add_argument('-o', '--out', type=Path, required=True)
    a = ap.parse_args()
    raw = deinterleave(a.smd.read_bytes())
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(raw)
    # The console name sits at $100 in every Mega Drive image, so it is the
    # cheapest possible proof the de-interleave was right.
    tag = raw[0x100:0x110].decode('ascii', 'replace')
    name = raw[0x120:0x150].decode('ascii', 'replace').strip()
    print(f'{len(raw)} bytes  "{tag}"  {name}')
    if not tag.startswith('SEGA'):
        raise SystemExit('$100 does not say SEGA -- this did not come out as a ROM')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
