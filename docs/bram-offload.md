# Getting the log off the console

The D: diagnostic writes `B:\CDLOG.TXT` into the Mega CD's internal
backup RAM -- 8 KB on the sub CPU's own bus at `0xFE0000`, odd bytes,
the only persistent store a stock console has while a cartridge in the
slot would stop it booting from CD at all.

Which leaves the log somewhere the console can write and nothing else
can read. Two pieces close that gap.

## `boot/m1tool.S` -- the Mode 1 cart ROM

Cart in the slot, Sega CD attached, **tray empty**. Nothing is read
from a disc; there is no disc, and no CD BIOS is used or even mapped.
Booting from the cartridge is Mode 1, where the Mega CD's hardware
appears at `0x400000` and its sub CPU comes up held in reset with
garbage in PRG-RAM -- which is the easy case, because all we need from
the sub CPU is that it can see its own backup RAM.

    main               hold sub in reset, request PRG-RAM
                       copy a twenty-instruction program into it
                       write the sub's stack and reset vector
                       release reset
    sub                copy 8192 odd bytes 0xFE0001 -> PRG 0x10000
                       set 0xFF8020 = 0xC0DE, spin
    main               take PRG-RAM back, read the copy
                       0xA130F1 = 1, write it to 0x200001 step 2
                       verify each byte as it goes
                       trailer at the top of the 32 KB

Every wait is bounded, so an absent or sleeping Mega CD colours the
screen instead of hanging on a black one. The screen is one colour
because a font is a lot of ROM for a program with four outcomes:

| colour | meaning |
|---|---|
| blue | running |
| white | no Mega CD answered the expansion port |
| yellow | it is there, but the sub CPU never came back |
| red | SRAM read back wrong -- the write is not landing |
| magenta | the main CPU took an exception |
| green | 8192 bytes are in SRAM; power off and take the card out |

The display is switched **on** for this, with VRAM wiped so every name
table entry is tile zero and the whole screen is palette entry zero.
The first version left the display disabled and leaned on the VDP
putting out the backdrop, which it does under emulation and did not on
the console: black screen, no blue, nothing at all.

**On a Mega EverDrive Pro, turn the cart's own Sega CD emulation off
before booting this.** Two devices answering the expansion bus is a
crash, not a diagnostic.

The header declares the save at `0x00200001`-`0x0020FFFF`, `RA` /
`0xF820`. Without that declaration the cart has no reason to create a
save file and the dump goes into memory nobody reads back; the build
script refuses to produce a ROM that lost those bytes.

    tools/build-rom.sh          # -> build/mcdbram.bin

## `tools/bram-extract.py` -- reading the save

What lands in SRAM is the backup RAM verbatim from offset zero, so the
save file is a filesystem image rather than a format of our own. EmuTOS
lays a small FAT12 volume on that memory, so this is an ordinary FAT
reader:

    tools/bram-extract.py save.srm                 # list
    tools/bram-extract.py save.srm CDLOG.TXT       # print it
    tools/bram-extract.py save.srm --all out/      # write them out

It reads the geometry from the boot sector rather than assuming it,
because the two volumes this port creates do not agree: the internal
backup RAM gets one sector per cluster and 32 root entries out of
sixteen sectors, the cartridge gets four and 64. It knows nothing about
`CDLOG.TXT` in particular -- a tool that understands one filename ages
badly.

The sixteen-byte trailer the ROM leaves at the top of the 32 KB is
reported when present and not required when absent, so the same tool
reads an emulator's `scd_*.brm` or `cart.brm` unchanged.

Verified end to end against real output: a Genesis Plus GX run of the
diagnostic leaves `cart.brm` with `CDLOG.TXT` in it, and the extractor
pulls the log back out complete.

## What the emulator did verify, and what it caught

Genesis Plus GX turns the Mega CD hardware on for a cartridge whose
header carries `C` in the device field, so most of this could be run
after all -- `scd_U.brm` appears in the save directory, which is the
core saying the CD side is live. A stage byte written to work RAM at
`0xFF0000` and read back out of the harness's RAM dump is how any of
the following was found; none of it would have been visible on a
console showing one colour.

Verified: the gate array reset, the bus grant, clearing write protect
and selecting bank 0, the plant itself (`0x420004` reads back
`00000200` and `0x420200` reads back `33FC`, the first opcode), reset
release reading back set, the second bus grab, and the SRAM write with
its byte-by-byte readback -- green.

Five bugs, every one of which would have failed silently on hardware:

1. **The region field was at `0x1F8`, not `0x1F0`.** Fifty-two bytes of
   padding written as sixty. The header checks in the build script
   would not have caught it; counting the fields did.
2. **`0xA130F1` wants `0x01`, not `0x03`.** Bit 1 is write *protect*,
   not write enable -- the research report says the opposite in prose.
   `0x03` accepts the whole 8192-byte loop and stores nothing. Tested
   both, one build apart: `0x03` fails the readback, `0x01` passes.
   This is why the readback is inside the write loop.
3. **Polling `0xA12020` while the sub CPU runs froze the main 68000** --
   at exactly the third read, every run. Replaced by a delay that
   touches only work RAM, then a single query with the bus in hand and
   the sub halted.
4. **`fail_sram` fell through into the exception handler**, so a red
   result immediately repainted itself magenta. The one failure code
   that most needs to be trustworthy was the one that lied.
5. **The second bus request wrote `0x02`**, which does not just ask for
   the bus, it drops the reset line on a CPU mid-instruction.

## What is still not verified

The sub CPU never executes under Genesis Plus GX in Mode 1. The program
is planted and read back correct, the reset vector is planted and read
back correct, `SRES` reads back set -- and the sub program's very first
instruction, a store to a comm register, never happens. Either the core
does not run a sub CPU from a cart-supplied program, or the release
sequence is wrong in a way the core cannot show. The report recommends
ARES or BlastEm for this; neither is available here.

So on the console this either works or it colours yellow. It cannot
hang, and it cannot quietly write nothing.

## Black screen, and the two more it cost

The console answered the first build with black -- not blue, not any
colour, nothing. Two things were wrong and the emulator only showed the
second one after the first was guessed at:

6. **The display was off.** Relying on the VDP to put out the backdrop
   colour while blanked works under emulation and did not here. It now
   silences the Z80, wipes VRAM so every name table entry is a blank
   tile, and turns the display on -- the ordinary path, where the whole
   screen is palette entry zero because there is nothing else in it.
7. **The Z80 handling that came with that fix hung on its first
   instruction.** It waited unbounded for a bus acknowledge and then
   wrote `0x0100` to `0xA11200` in the belief that this asserts reset;
   `0x0100` *releases* it. A Z80 held in reset from power-on never
   grants a bus it does not have. The stage byte read `1` -- stopped in
   the first thing the program did. The wait is bounded now and nothing
   depends on the answer.

The ROM is also padded to 32 KB. Fifteen hundred bytes is a legal ROM
and an unusual one, and an unusual one is a thing a flash cart's loader
can be forgiven for mishandling.
