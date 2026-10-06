# Three programs, measured

ORCS, rmac and Dungeon Master were proposed as ports. Each one was
fetched and measured against this machine before any work started.
Two of them fail on size. Dungeon Master fits, and its own memory
manager is the reason.

The constraints that decide it, from `PLAN.md`:

    TPA                    about 330 KB, one contiguous block
    Alt-RAM                60-200 KB, not handed out by Malloc(-1)
    no MFP, YM2149, shifter  ST hardware addresses fall into the
                             gate array's space on the sub CPU
    screen pump            ~24 frames for a full 32000-byte repaint
    input                  Sega mouse, pad-as-mouse, serial keyboard
    progs/ C library       none: hand-written GEMDOS bindings

## ORCS: does not fit, and cannot be made to

ORCS 2.18 (Thorsten Otto, 2020) is freeware and the source is not
public. What can be had is the binary, from the FireBee setup tree:

    git.firebee.org/Firebee/FireBee_Setup-Dev  devtools/orcs218/

The PRG header:

    text 627342  data 108904  bss 88922     load 825168 bytes

That is two and a half times the TPA before ORCS allocates its first
object tree, so Pexec refuses it. There is no source to cut it down
from. Its licence also forbids splitting the archive, so it could not
go on the disc as a single file anyway. ORCS stays a tool for the host,
under Hatari or on a real ST, and the `.RSC` files it writes run here
unchanged.

## rmac: fits only after cutting most of it out

rmac is MADMAC's descendant, a C program. 2.2.14 builds cleanly on the
host from

    https://slackware.uk/~urchlay/src/rmac-2.2.14_20221221.tar.xz

and `size` on x86-64 gives text 301149, data 82568, bss 8163160. The
BSS is mostly `symbolPtr`, a fixed table of a million pointers (4 MB on
a 68000). The data is mostly `machtab` and its siblings, the opcode
tables for the 68000, 68020-060, 68881, DSP56001, 6502 and the Jaguar
RISC. A native build means:

- a C library under `progs/` (libcmini is the usual choice);
- `symbolPtr` sized to the machine;
- every target except the 68000 compiled out.

That leaves something in the 150 KB class that can assemble small
files from C: or the disc, with `EDIT.PRG` as the editor and the
serial keyboard for typing. It is possible, and it is the least useful
of the three: rmac is already the better tool on the host, where it can
be used as it is.

Devpac 3 and QED are closed binaries. There is nothing in them to port.

## Dungeon Master: fits, with three subsystems to replace

ReDMCSB is Christophe Fontanel's reverse-engineered source for every
DM/CSB version except the Super NES. It is not on GitHub; the releases
are 7z archives at

    http://dmweb.free.fr/Stuff/ReDMCSB_WIP20210206.7z   (25.8 MB, latest)
    http://dmweb.free.fr/Stuff/ReDMCSB_Release2.7z

Its own build runs on Windows under Hatari and Mini vMac, so that it
reproduces the original executables byte for byte. That build is not
needed here. The ST versions (1.0a to 1.3b and CSB 2.x) are plain
Megamax C plus inline 68000 assembly, all in
`Toolchains/Common/Source`. One tree serves every version through
`#ifdef MEDIAnnn_...`. The version is picked with `-DEXEID=`: 115 is DM
1.2 English, the game executable `S12E\START.PAK`.

To get one version's source alone:

    gcc -E -P -nostdinc -fdirectives-only -DEXEID=115 -DNOCOPYPROTECTION=1 \
        -I Toolchains/Common/Source -I <dir of empty stub headers> -x c FILE.C

`MKSSGAME.BAT` compiles the game from 50 files, 68,000 lines before
preprocessing, most of them other versions' branches. `-DNOCOPYPROTECTION=1` is the author's own switch; it removes
the fuzzy-bit floppy checks along with the `Floprd`/`Rwabs` calls they
need.

### Memory: it fits because DM adapts to it

The ReDMCSB-built `S12E\START.PAK` is a plain PRG:

    text 143288  data 1340  bss 22606     load 167234 bytes

`F0448_STARTUP1_InitializeMemoryManager` takes `Malloc(-1) - 20`, all of
it. It also takes the GEM region between `os_end` and the `GEM_MUPB`'s
`gm_end`, and uses that region only for permanent allocations.
`F0476_MEMORY_InitializeGraphicMemory` reserves 10000 bytes and turns
everything else into an LRU cache for `GRAPHICS.DAT`. The only fixed
threshold in the file is `> 370000`, and it only decides whether the
flipped wall bitmaps are precomputed. So DM has no minimum heap. With
less memory it evicts graphics sooner and reads `GRAPHICS.DAT` more
often.

Here that leaves roughly 160 KB of heap after the program loads,
against roughly 300 KB on a 520ST. The second region is how to get the
rest back. Alt-RAM goes in where the GEM region went, so the permanent
allocations come out of it and the TPA is left as cache. That is one
`asm` block in `STARTUP1.C`, not a new allocator.

Every cache miss reads `GRAPHICS.DAT` again. Where it reads from
depends on the boot, and the cartridge is the better case: see
"Both boots" below.

### What touches hardware

DM 1.2 alone, after preprocessing. These are the call sites, not the
macro definitions:

| Where | What | Here |
|---|---|---|
| `PALETTE.C`, `BASE.C` | writes `$FFFF8240` directly, including a `movem.l` of all 16 words from its own VBL handler | goes to the palette block `SHOW.PRG` uses |
| `SOUND.C` | `Xbtimer` timer A at 5486 Hz, ISR writes PSG volume registers (digitised samples) | no MFP, so the timer never fires: silent but harmless. To get sound, the samples go to the RF5C164, which plays 8-bit PCM at any rate itself |
| `STARTUP1.C`, `BASE.C` | `Supexec(F0018_MAIN_SetExceptionVectors)`, `Jdisint(timer C)` | which vectors it takes needs checking against EmuTOS's VBL and the gate-array timer |
| `TITLE.C` | `Setscreen` double-buffering for the title | the servant reads `v_bas_ad` every VBL (`iofw/main.c`), which should follow a flip |
| `IO.C` | `Kbdvbase` for mouse and keyboard packets | the IKBD-shaped events the glue already injects |
| `MEMORY.C`, `LOADSAVE.C` | `Fopen("A:\\GRAPHICS.DAT")`, `"\\DUNGEON.DAT"` | one drive letter, picked at startup: D: on a disc boot, the ROM drive on a cartridge |
| `LOADSAVE.C` | `Flopfmt`/`Protobt`/`Flopwr` to make a save disk | removed; saves go to `S:` or `C:` |

The two palette writes matter most. Without the change, DM writes to
the gate array instead of a palette.

### Both boots

The disc and `m1emu.bin` are the same system, and DM has to run from
both. They differ in where its two data files can come from.

**Disc.** `DUNGEON.DAT` and `GRAPHICS.DAT` go on D:. A cache miss is a
seek and a read through the CDC. That is slow, so a read cache for the
file is the likely fix.

**Cartridge.** There is no D:. C: is the 112 KB ramdisk in PRG-RAM,
and `docs/sonic.md` already found that full with a 52 KB file in it. S:
is the cart's save RAM. Neither one can hold `GRAPHICS.DAT`: DM 1.x's
is a few hundred kilobytes. Check the size against your own dump.

The cartridge has room in its ROM. `m1emu.bin` carries 362624 bytes of
payload in a 512 KB image (`docs/mode1.md`), and a Mode 1 cart can map
2 MB of ROM before its save RAM. In Mode 1 the save RAM is at
`$200001`, switched in by `$A130F1`. So the data has to sit below
`$200000`, and about 1.6 MB of space is free there. Both DM files fit
several times over.

The way in is a read-only drive backed by ROM. The servant already
serves S: one 512-byte sector at a time (`GA_CART_REQ` op 1, cart ->
bounce buffer). A ROM drive is the same op with a different source:
bytes in a row from a fixed ROM offset, rather than every other byte of
save RAM. On the sub side it is another block device, next to I: and S:.

A cache miss then costs a ROM copy with no seek, so the cartridge build
can play DM better than the disc. It can also be tested in emulation:
gpgx boots a Mode 1 cart with no disc, which is exactly this setup.

Three costs:

- **Servant space.** `iofw.bin` has 152 bytes free under the `0xFF7000`
  cache, as of the last commit. The ROM read has to share the loop the
  S: read already uses (a source pointer and a step of 1 or 2), not add
  a second copy of it.
- **`build-rom.sh`.** It has to append the user's two files to the image
  and record where they start, and only when the user supplies them. A
  ROM built without them has no ROM drive.
- **A smaller C: in cart builds.** The ramdisk is all of C:, so
  anything DM writes to C: comes out of that 112 KB. Saves therefore go
  to S:, which is there on both boots.

### The screen

The dungeon view is 224×136, about a third of the screen. At the
measured pump rate that is roughly eight frames per step. DM redraws
only when the party moves or something in view changes, so this is
playable without the asm planar core.

### What cannot be committed

The data files are FTL's: `DUNGEON.DAT`, `GRAPHICS.DAT` and the title
pictures. ReDMCSB carries no licence and is derived from FTL's
executables. The model is the Sonic one: a script fetches ReDMCSB and
extracts and patches the ST sources at build time, and the user
supplies `DUNGEON.DAT` and `GRAPHICS.DAT` from their own disks. Nothing
of either goes into the repository.

### Order of work

1. Compile the extracted S12E sources with `m68k-elf-gcc -mshort`. The
   Megamax-isms to convert are K&R parameter lists, `asm { }` blocks,
   `overlay "..."` and `HUGE`. Link against `progs/tosbind.S` plus the
   handful of XBIOS calls it lacks.
2. Run it in the emulator from a disc boot with the data on D:, sound
   stubbed and the palette redirected, up to the title and the entrance.
3. The ROM drive: the servant op, the block device, `build-rom.sh`
   appending the data. Then the same run from `m1emu.bin` in gpgx.
4. Alt-RAM as the permanent region; count cache misses on a walk
   through level 1, on both boots.
5. Sound on the PCM chip.
6. Saves on S:.
