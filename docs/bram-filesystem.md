# One cartridge, both kinds of data

A Mega CD backup RAM cartridge can hold this port's files or a game's
saves. Not both. That is not a limitation of the hardware, it is a
decision this port made early and never revisited: `bram_format()` in
`emutos/bios/segacd.c` lays a FAT12 volume over the whole of the memory,
and Sega's volume keeps its footer in the **last 0x40 bytes**, which our
data area runs straight through.

The fix is not to write Sega's format ourselves. It is to stop writing
over it, and to put our filesystem inside it as a file like any other —
so the console's own Backup RAM Manager, and every game, still sees a
normal volume with our data sitting in it under a name.

## The format, and where it is known from

    end-0x40   0x5F x11, 0x00 x4, 0x40
    end-0x30   free-block count, big-endian, written four times
    end-0x28   number of files, big-endian, also four times
    end-0x20   "SEGA_CD_ROM\0" 0x01 0x00 0x00 0x00
    end-0x10   "RAM_CARTRIDGE___"

A volume is Sega's if and only if the last 0x20 bytes match. Blocks are
0x40 bytes and the count excludes the three the footer occupies.

Cited from Genesis Plus GX's `brm_format` (`libretro/libretro.c`), which
is the check every emulator and the BIOS agree on — and then confirmed
against a live volume rather than taken on trust:

    5f 5f 5f 5f 5f 5f 5f 5f 5f 5f 5f 00 00 00 00 40
    00 7d 00 7d 00 7d 00 7d 00 00 00 00 00 00 00 00
    53 45 47 41 5f 43 44 5f 52 4f 4d 00 01 00 00 00
    52 41 4d 5f 43 41 52 54 52 49 44 47 45 5f 5f 5f

125 four times over: 8192/64 = 128, less the three the footer holds.
The eight bytes at end-0x28 are zero there only because that volume was
empty -- with our filesystem in it they read `00 01` four times, which
is what identified them as the file count rather than padding. Genesis
Plus GX's template has no reason to say so; it only ever writes a blank
volume.

## Not writing it ourselves

None of the above needs to be written by this port, and it should not
be. The machine already carries a Backup RAM file manager — BRMINIT,
BRMSTAT, BRMSERCH, BRMREAD, BRMWRITE, BRMDEL, BRMFORMAT, BRMDIR,
BRMVERIFY — behind the `_BURAM` vector at sub **0x5F16**. Going through
it means the directory layout, the allocation and the free count stay
Sega's code's problem permanently, and no part of this project ever has
to encode a guess about them.

(Vector and function codes: megadev `lib/sub/bram.def.h`, MIT. One of
its comments is wrong and the console said so — see "blocks" below.)

## Reaching it

`_BURAM` is at sub 0x5F16 and this port puts *its own* memory at address
zero, which is the whole point of the port. So the vector is not there
while EmuTOS is running. It is reachable the same way the CD timeshare
already reaches `CDBIOS` at 0x5F22: the loader photographs the firmware
into Word RAM at 0xBA000, and `segacd_bios_call` exchanges 24 KB of low
memory with it for the duration of one call. `segacd_buram_call` in
`segacd2.S` is that same visit to the door three words along.

Two things about it are load-bearing, and both were learned the hard way
earlier in this project rather than reasoned out here:

**Everything passed by pointer must live above 0x6000.** The visit
exchanges 0..0x6000, so during the call that region holds EmuTOS's bytes
rather than its own. EmuTOS's stack is at 0x800 and its `.bss` starts at
0x2140 — and `segacd.o`'s own `.bss` runs 0x35F8..0x7FF2, which
*straddles* the line. A plain static would land inside or outside
depending on declaration order. That is exactly how the printer's page
buffer ended up at 0x2140 and streamed a page of zeros. So the work
area, the string buffer and the call block go at a fixed address,
0x7E000, inside the region already set aside for visits to the parked
firmware — with a `#error` that fires if the timeshare's read buffer
ever grows into them.

**It has to be called in supervisor mode.** The cookie hands a program a
pointer it calls directly, and a direct call keeps the caller's mode, so
a `.PRG` arrives in the driver in user mode and dies on the `ori.w
#0x0700,sr` that masks interrupts before the exchange. `Supexec` is
TOS's own answer. This is written down in `progs/cdtest.c`, about the
identical mistake, and was made again anyway — the panic is
`sr=0010`, and the program's name is nowhere near it.

## What the console says

`BRAMAUTO=1 tools/build-iso.sh U` puts `AUTO/BRAMAUT.PRG` on the disc;
`DIAG=1` puts `BRAMTEST.PRG` on C: to run from the desktop. It calls
BRMINIT then BRMSTAT and prints the answer. It reads and does not
write.

    raw 03027D00
    status  Sega formatted
    size    2 blocks of 4096 = 8192 bytes
    free    125 blocks of 64 = 8000
    files   0

Identical on a CD boot and on a Mode 1 cartridge boot — the Mode 1
loader parks the firmware too, so there is no boot path where this is
unavailable. The volume was byte-identical afterwards, footer and all,
with every byte before the footer still zero.

**Blocks are two different sizes and that is not a transcription error.**
BRMINIT reports the memory in units of 0x1000 (2 to 0x100, so 8 KB to
1 MB); BRMSTAT reports free space in the 0x40 units the directory
allocates in. megadev's header says 0x1000 for both. The console
answered 125 against 8 KB, which is only true of 64-byte blocks, and
Genesis Plus GX computes the same 125 the same way. Two sources, one
number, and the header is the one that is wrong.

## The one that decided the design

**Can a file's data be written in place? Yes.** Measured, not assumed
-- `BRAMRW=1 tools/build-iso.sh U` puts `AUTO/BRAMRWAU.PRG` on the disc.
It writes a one-block file of known bytes, finds it, alters a byte
through the pointer BRMSERCH handed back, and asks the manager what it
now thinks:

    init    cs=00 size=02
    write   cs=00
    serch   cs=00 blocks=01 at=00FE0080
    raw     FFA0FFA1FFA2FFA3
    wrote   A0A1A2A3A4A5A6A7
    layout  offset 01 step 02
    verify  cs=00 d0=FF   (before)
    altered byte 5 through the pointer
    verify  cs=00 d0=FF   (after)
    read    cs=00  A0A1A2A3A45AA6A7
    byte 5: wrote A5, read back 5A
    IN PLACE WORKS: the change came back.
    delete  cs=00

So the data is stored **plainly, in the clear, and unchecksummed**. A
sector can be written straight through the pointer and BRMREAD sees it;
BRMWRITE is needed only to create a file and to resize one. That is the
answer the whole design was waiting on, and it is the good one.

**The layout.** The address BRMSERCH returns is the even one, and the
data is the odd byte of each word from there on -- `a0out + 1 + 2*i` for
byte i, which is the same odd-byte mapping `bram_rd()` has always used.
The first file's data landed at 0xFE0080, so de-interleaved byte 64:
block 1, with block 0 not given to it.

(The first run of this printed "neither matches" over a perfectly good
`FF A0 FF A1`, because it tested offset 0 and offset 0 stepping two and
not the one in between. The program searches all four now. The console
was right and the test was wrong, which is the usual way round.)

**One anomaly, reported rather than smoothed over.** BRMVERIFY answered
carry-clear -- "data matches" -- both before *and* after the byte was
altered, with d0 = 0xFF each time. It should have disagreed the second
time. Either it does not compare the way the documentation says, or the
14-byte info block this test hands it is not shaped the way it wants.
Nothing here depends on it: BRMREAD returning the altered byte is what
proves the data path, and it did. But BRMVERIFY should not be trusted as
a change detector until someone works out what it is actually doing.

**Deletion does not scrub.** After BRMDEL the free count went back to
125 and the signature was intact, but the file's 64 bytes were still
sitting at block 1. That is ordinary, and worth knowing before treating
"deleted" as "gone".

## It works

`FMTIAUTO=1 tools/build-iso.sh U` formats I: from AUTO. On a Sega volume
that creates the EMUTOS file and lays the FAT12 inside it. Afterwards the
memory reads:

    0040  60 38 45 6d 75 54 4f 53 ...   our boot sector, at byte 64
    1fc0  5f x11 00 00 00 00 40         Sega's footer, untouched
    1fd0  00 05 x4  00 01 x4            5 blocks free, 1 file
    1fe0  SEGA_CD_ROM. RAM_CARTRIDGE___

Byte 64 is block 1: the file's data, exactly where BRMSERCH pointed.
Booting again, the manager says

    raw 03020501 -- Sega formatted, 2 blocks of 4096, 5 free, 1 file

so the console's own Backup RAM Manager sees our filesystem as one
ordinary save sitting beside whatever else is there, which is the whole
point. A plain boot with no formatting leaves a Sega volume byte for
byte alone -- checked, every one of the 8128 bytes before the footer
still zero.

**Where the format actually happens.** FORMATI.PRG formats by writing
sectors through Rwabs, so `segacd_bram_rw()` is the only place "make me
a filesystem here" arrives -- there is no other entry point to hook. On
a write, once per session, to a Sega volume with no file of ours in
it, one is created and the sectors land inside it. On a read, never: an
unformatted drive merely being looked at must not quietly take seven
kilobytes of somebody's cartridge.

**The name is not stored as text.** The directory entry for EMUTOS
reads `42 55 24 3c 50 d2 47 27 53 d1 b1`, which is the manager's own
encoding of the eleven characters. We never decode it -- BRMSERCH finds
the file by name and hands back the address -- and this is only written
down because grepping the image for "EMUTOS" says the file is missing
when it is right there.

## If you formatted with the first build of this

The file was called AROSDISK for the few hours between this landing and
the name being cleaned up -- we built an EmuTOS port, not AROS, and
naming the volume after the operating system we did not build was
funnier than it was useful. It is **EMUTOS** now.

A volume formatted with the earlier build has an AROSDISK in it that
this build will not look for, so I: reads as unformatted -- and on the
internal 8 KB there is no room to make a second one, so formatting will
not fix it either. Delete the AROSDISK save from the console's own
Backup RAM Manager first, then format. On a cartridge with room to
spare it will simply make the new file and leave the old one sitting
there wasting its blocks, which is worth tidying for the same reason.

No fallback for the old name is built in on purpose. It is a few hours
of one machine's history, and carrying a compatibility path for it
forever costs more than the one sentence above.

## Still open

**How large a file will it allocate?** Sizes are in 0x40-byte blocks in
a word, so the ceiling is high, but the manager's own behaviour near the
top of a 1 MB cart is not known here.

**What should the volume look like when both are present?** One large
file is simplest and gives our filesystem a contiguous run. It also
means a game that fills the rest cannot grow into it, and that deleting
it from the console's own manager screen throws away everything at once,
which may be exactly the right escape hatch.

## One thing to watch

`emutos-segacd.img` is now **237400 bytes against a 237568 limit** — the
image may not reach the CDBIOS park at Word RAM 0xBA000, and there are
168 bytes left. `tools/build-iso.sh` checks it and will refuse to build
rather than let the loader overwrite the parked firmware, but the next
feature to land in EmuTOS itself is going to hit that wall.
