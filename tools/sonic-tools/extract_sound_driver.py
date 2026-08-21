#!/usr/bin/env python3
"""Lift Sonic 2's Z80 sound driver out of a cartridge dump, ready to run.

This port has spent four rounds reimplementing an SMPS player from the
disassembly's annotations and getting it wrong in a different way each time --
the operator register order, the total-level slot masks, the eight-bit volume
wrap, the voice byte order.  Every one of those was a place where a comment and
the cartridge disagreed.  The cartridge does not disagree with itself, so this
runs the cartridge's driver instead of another reading of it.

That is cheap on a Genesis, because Sonic 2's driver is a Z80 program and this
machine has the same Z80 sitting idle.  Two things have to be arranged:

  the driver   Saxman compressed at $0EC0E8; tools/sonic/saxman.py undoes it.
               It is position-fixed at Z80 $0000 and needs no relocation.

  its data     the driver reaches sound data through the Z80's 32 KiB ROM
               window, so every pointer it holds is `$8000 + (addr & $7FFF)`
               and every bank select is nine bytes of `ld (hl),e` / `ld (hl),a`
               after `xor a / ld e,1 / ld hl,$6000`, one per bit of the 68000
               address from bit 15 up.  Copy the whole 32 KiB bank verbatim and
               place it at an address with the same offset inside a bank, and
               all sixteen-bit pointers stay correct -- only the nine-byte bank
               sequences need rewriting.

So the transform is: decompress, copy bank 31, rewrite the five bankswitches
that name it.  Nothing is reassembled and nothing is interpreted.

The dump stays out of the tree; this is what is kept, exactly as with the art.
"""
from __future__ import annotations
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from saxman import decompress                                   # noqa: E402

DRIVER_OFFSET = 0x0EC0E8
DRIVER_MAX = 0x1200                 # comfortably over the compressed size
BANK = 0x8000
SFX_BANK = 31                       # SoundIndex and every sound effect

# `xor a / ld e,1 / ld hl,zBankRegister`, then nine bytes selecting the bank.
BANK_PRELUDE = bytes([0xAF, 0x1E, 0x01, 0x21, 0x00, 0x60])
LD_HL_E, LD_HL_A = 0x73, 0x77       # a bit that is set, and one that is clear

# Two tables that must survive the decompression intact, or nothing else here
# is trustworthy.  zFrequencies is little-endian: it is a Z80's table.
MARKERS = {
    'zVolTLMaskTbl': bytes([8, 8, 8, 8, 0x0C, 0x0E, 0x0E, 0x0F]),
    'zFrequencies': bytes([0x5E, 0x02, 0x84, 0x02, 0xAB, 0x02]),
}


def bank_sites(drv: bytes):
    """-> [(offset, bank)] for every bankswitch in the driver."""
    out, i = [], 0
    while True:
        i = drv.find(BANK_PRELUDE, i)
        if i < 0:
            return out
        nine = drv[i + 6:i + 15]
        if len(nine) == 9 and all(b in (LD_HL_E, LD_HL_A) for b in nine):
            bank = sum((1 if b == LD_HL_E else 0) << n for n, b in enumerate(nine))
            out.append((i + 6, bank))
        i += 1


def rewrite_bank(drv: bytearray, at: int, bank: int) -> None:
    for n in range(9):
        drv[at + n] = LD_HL_E if (bank >> n) & 1 else LD_HL_A


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('rom', type=Path, help='a Sonic 2 dump (see smd_to_bin.py)')
    ap.add_argument('--driver-out', type=Path, required=True)
    ap.add_argument('--bank-out', type=Path, required=True)
    ap.add_argument('--place', type=lambda s: int(s, 0), required=True,
                    help='where the sound bank will sit in this ROM')
    a = ap.parse_args()

    rom = a.rom.read_bytes()
    drv = bytearray(decompress(rom[DRIVER_OFFSET:DRIVER_OFFSET + DRIVER_MAX]))
    for name, pat in MARKERS.items():
        if pat not in drv:
            raise SystemExit(f'{name} is not in the decompressed driver -- '
                             'the dump is not Sonic 2, or it decoded wrong')

    if a.place % BANK:
        raise SystemExit(f'--place ${a.place:06X} must start a 32 KiB bank, so '
                         'the data keeps its offset inside one and every '
                         "sixteen-bit pointer in it stays correct")
    new_bank = a.place // BANK

    sites = bank_sites(drv)
    moved = [off for off, bank in sites if bank == SFX_BANK]
    if not moved:
        raise SystemExit('found no bankswitch to the sound-effect bank')
    for off in moved:
        rewrite_bank(drv, off, new_bank)

    a.driver_out.parent.mkdir(parents=True, exist_ok=True)
    a.driver_out.write_bytes(bytes(drv))
    a.bank_out.write_bytes(rom[SFX_BANK * BANK:(SFX_BANK + 1) * BANK])

    others = sorted({b for _, b in sites if b != SFX_BANK})
    print(f'driver {len(drv)} bytes from ${DRIVER_OFFSET:06X}; '
          f'{len(moved)} bankswitch(es) moved from bank {SFX_BANK} to '
          f'{new_bank} (${a.place:06X})')
    print(f'sound bank 32768 bytes from ${SFX_BANK * BANK:06X}; '
          f'banks left alone (music and DAC, never played here): {others}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
