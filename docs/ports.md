# Three programs, measured

*Dungeon Master is parked: see "Status: parked at a hard limit" below.*

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

Devpac 3 and QED are closed binaries -- which, as it turned out, is no
obstacle: see "Devpac 3: runs as it is" below.

## Devpac 3: runs as it is

Devpac 3.10's files are small: the editor `DEVPAC.PRG` is 93 KB, the
assembler `BIN\GEN.TTP` 70 KB, and the debugger `BIN\MON.PRG` 36 KB.
Each fits a 264 KB TPA many times over, and nothing needed patching.
`tools/install-devpac.sh` puts them on D:, keeping the folders, because
Devpac finds its tools and includes by path, and rewrites those paths
in `HISOFTED.INF` from A: to D:. The romdisk and the disc's D: both
carry one level of folders from `vendor/stsoft/` for this.

Verified in gpgx from the cartridge:

- `DEVPAC.PRG` opens its editor window from the desktop's autorun.
- `GEN.TTP` assembles `D:\EXAMPLES\DEMO.S`, reporting the deliberate
  error the example carries.
- `GEN.TTP` assembles a fresh source to `C:\HELLO.PRG`, with 0 errors.
- `C:\HELLO.PRG` then runs and prints its message.

That makes the machine a 68000 development system for itself.

Not yet tried: MonST, the debugger, which is the part of Devpac most
likely to touch hardware directly, and assembling from inside the
editor, which needs a keyboard the headless harness does not have. QED
was not on the archive that had Devpac.

## From the Atari_ST_Sources archive

ggnkua's Atari_ST_Sources collects about 185,000 files of ST source. These
were checked against the same three tests: it fits a 264 KB TPA, it
leaves the hardware alone, and it works in low resolution with a mouse
or pad.

| | verdict |
|---|---|
| Llamatron (Jeff Minter's source disk) | no. It programs Timer B, the MFP, the palette, the shifter, the keyboard ACIA and the floppy controller directly. It is also a 50 fps full-screen shooter, and the screen pump takes about 24 frames per full screen, so it would be a rewrite onto VDP sprites, not a port. |
| QED 4.53, the editor | no. 18 of its 31 dialogs are wider than 40 columns, up to 86: it was laid out for 640-pixel screens. |
| Peter Lane's Sokoban | **yes**, and it runs. |
| Jim Kent's Cyber Paint | no, on memory. It builds, but needs about 400 KB; parked (below). |
| NEOchrome 1.0 (binary, not from the archive) | **yes**, through a launcher; it runs on both boots (below). |

**Sokoban.** It is pure GEM: a menu bar, windows, VDI drawing, the 50
classic levels built in, about 2,300 lines of C. Its licence, the Open
Works License, allows redistribution. `tools/build-sokoban.sh` builds it:

- It was written for AHCC, whose `int` is 16 bits, so the build uses
  `-mshort`.
- MiNTLib has no `-mshort` build, so the C library is libcmini, built
  `-mshort` by the script. The result is 48 KB, against 159 KB with
  MiNTLib, and that build crashed anyway: its 32-bit `int` did not match
  the game's.
- `patches/sokoban/include/` maps AHCC's GEM headers onto GEMlib's. The
  differences are Pure C's variadic `wind_set`, its two-word
  `evnt_multi` timer, and its colour names.
- `patches/sokoban/megacd.patch` makes three small changes. The level
  table, which was defined in a header and so copied into six files, is
  now defined once. An enum name clash is resolved. The score file is
  `I:\SOKOSCOR.TXT`, so best scores outlive a power-off on either boot.

Verified in gpgx on the cartridge, driven by the pad: the level list
opens, and a double-click opens a level, drawn in colour.

That run found a bug in `AUTORUN`. Its `#Z` line always marked the
program as not GEM, so the AES started a GEM program with the pointer
hidden and no mouse. It now follows the extension, and `AUTORUN_GEM`
overrides it.

Not done: play needs the cursor keys, which on a pad means the on-screen
keyboard (Start). A "d-pad as arrow keys" mode in the servant would make
it a pad game, but the servant has 112 bytes free.

**Cyber Paint: builds, parked at the memory limit.** Jim Kent's
low-resolution paint and cel-animation program (Antic, 1987; BSD
licence) is about 26,000 lines of C and 40 files of assembly.
`tools/build-cyberpaint.sh` builds it:

- It was written for Manx Aztec C, whose `int` is 16 bits, so it is
  built `-mshort` with libcmini, as Sokoban is.
- `tools/aztec2gas.py` converts Aztec's assembler dialect to GNU as. All
  40 files assemble.
- The only file that touches the hardware is `PF.ASM`. It takes the VBL
  vector at `$70`, MFP Timer B, the 200 Hz timer and the keyboard
  vector, and splits the palette in stripes, so the menus' colours 1-3
  differ from the picture's. `patches/cyberpaint/pf_mcd.c` replaces it.
  It keeps PF.ASM's interface and runs from the VBL queue, with one
  palette per frame. When a menu shows, colours 1-3 are the menu's.
  When the menus are hidden, the picture's colours are exact.
- `patches/cyberpaint/megacd.patch` changes about ten lines: two missing
  `extern`s, a duplicate declaration, a two-argument `v_hide_c`, and
  `aline.h` declaring the `aline` pointer.

It links into a 169 KB program that needs 185 KB with its BSS. At
startup it asks `Malloc(-1)` for its pool, keeps 24 KB back for GEM, and
refuses to run with less than 96,000 bytes. From that pool it then
takes five 32,000-byte screens: undo, back buffer, tween start and end,
and the 64 KB next/previous frame pair. The total is about 400 KB
before any cel or animation delta. A 264 KB TPA leaves it 79 KB, and
the bulk arena adds 12 KB with the default C: or 112 KB with a 32 KB
C:. That is short even with both. The animation buffers are used in 11
of its files, so dropping them is a rewrite of the animation engine,
not a port. It is parked with DM: the build stays, for a machine with
more RAM. The script writes `vendor/stsoft/CYP.PRG`; delete it, or it
goes onto D: as a program that refuses to start.

**Found on the way, and fixed since.** The VBL told the servant to show
`v_bas_ad`, the *logical* screen. Cyber Paint draws off-screen by
moving only the logical screen (`Setscreen(buf, -1, -1)`), as ST
programs may. Here that put the buffer on display, and the palette and
pointer blocks went 32,000 bytes past it, inside the program's heap.
EmuTOS patch 0193 makes the shown screen `Physbase()`, with the blocks
beside it. `SPLITAUTO=1 tools/build-iso.sh` adds `progs/scrsplit.c`, an
emulator-only check. It draws bands on the shown screen and solid green
on a second one with only the logical screen moved there. After 300
VBLs it moves the physical screen too. Before the fix the green showed
at once; now the bands show until the move. The desktop frame is
byte-identical, on the disc and on the cartridge.

One limit remains, the same as before: a program that moves the
physical screen to its own buffer has to leave 768 bytes free past the
picture for the blocks, 32,768 bytes from a 256-byte boundary.

**NEOchrome 1.0.** Atari's paint program, as a binary from planetemu,
like Devpac. It is 204 KB with its BSS, so it fits the 264 KB TPA, and
nearly all of it is plain TOS: GEMDOS, Setscreen, Line-A for drawing,
the VDI for the mouse, the AES for its file selector. Three things in it
are ST hardware:

- Its VBL routine goes in vector `$70`, which here is the CD drive's
  interrupt. The routine copies NEOchrome's palette to the shifter every
  frame, counts frames, and starts MFP Timer B for the toolbox's colour
  bands.
- It enables Timer B in the MFP, whose addresses alias the gate array.
- It reads the shifter palette at `$FF8240`, to restore it at exit.

`progs/neo.c` is a 2.4 KB launcher. It loads `NEONEW.PRG` with Pexec 3,
checks every patch site against NEOchrome 1.0's own bytes, patches them
in memory, and starts it with Pexec 4. The VBL work moves into the VBL
queue, run from the launcher, which stays resident underneath. The
MFP writes become NOPs, and the palette read becomes a copy taken
through `Setcolor`. One palette per frame: while the toolbox shows,
colours 14 and 15 are the toolbox's; with it hidden the picture's
sixteen are exact. The toolbox's colour bands are not drawn.

The launcher also places NEOchrome's screens itself. NEOchrome lays its
buffers out back to back, 32,000 bytes apart, and this machine needs two
things of a screen: 768 bytes free past it for the palette and pointer
blocks, and no 128 KB boundary inside it, because the servant reads the
screen through one 128 KB bank of sub RAM. The launcher spaces the two
screens 32,256 bytes apart within the same region NEOchrome would have
used. If the load address puts a boundary where no layout fits, it
loads NEOchrome a little higher and tries again.

NEOchrome draws off-screen with `Setscreen(buf, -1, -1)` and page-flips
between its two screens, which is what EmuTOS patch 0193 (show the
physical screen) is for.

Verified in gpgx, on the cartridge and on the disc: the toolbox
renders, a stroke drawn with the pad lands in the picture, FULL SCREEN
shows the picture alone in its own palette, strokes still draw there,
and A at the bottom edge brings the toolbox back. Not yet tried:
loading and saving pictures.

Running it from the disc found a bug in `AUTORUN`. On a CD boot, D:
refuses reads until the desktop has started, because the boot-time
reads have never worked on the hardware. An autorun program runs
instead of the desktop, so it found no D: at all. EmuTOS patch 0194
ends that window when the AES starts an autorun program too.

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

So the cartridge now has a D: of its own: the romdisk (`docs/build.md`).
Every file in `vendor/stsoft/` goes on D: on both boots. On the disc
it is part of the disc's filesystem; on the cartridge it is a FAT image
in the ROM at 512 KB, which the servant reads a sector at a time for the
sub. That is cart op 11: the S: read loop with a step of one byte
instead of two, which cost 40 of the servant's 152 free bytes. DM is an
ordinary program here: it and its two data files go in `vendor/stsoft/`,
and DM reads them from the drive it was started from, whichever boot
that was.

On the cartridge a cache miss costs a ROM copy with no seek, so the
cartridge build can play DM better than the disc. Verified in gpgx: an
AUTO test (`ROMDAUTO=1`) lists D: on a cart boot and reads a file back
from the ROM. The disc boot's desktop is byte-identical with and without
the change.

Saves go to S:, which is there on both boots: the cart's own save RAM on
a cartridge, a backup RAM cart on a disc. The cartridge's save RAM is 63
sectors, and a DM save carries the whole dungeon state. Whether it fits
there has to be measured against real data files.

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

### Status: parked at a hard limit

**Parked, October 2026.** DM needs about 225 KB of heap and this machine
gives it 89 KB; every scattered region added together does not close
that gap. It is a hard limit of the Mega CD's memory map, not something
the port can engineer around. Revisit with a 32X attached: its RAM is
the missing piece.

Nothing is lost. What stays in the repository, and works:

- `tools/build-dm.sh S11E|S12E` builds DM with ReDMCSB's own Megamax
  toolchain under Hatari, from a pinned ReDMCSB download.
- `tools/dm-reduce.py` and `patches/dm/megacd.patch` are the port.
- The patched DM 1.1 runs on an emulated 1 MB ST to the entrance
  screen.
- On the Mega CD it starts, runs its VBL and palette paths, finds the
  bulk arena, and stops at SYSTEM ERROR 40.

The romdisk, the Setcolor/Setpalette path and `AUTORUN` came out of
this work and stand on their own.

### First runs, and what they found

The data files are DM 1.1's (ReDMCSB's S11E). Their MD5s match the
original disk's in ReDMCSB.xlsx, so `build-dm.sh S11E` builds the
matching executable.

**On an ST, it works.** The same patched DM.PRG runs under Hatari as an
ST with 1 MB and EmuTOS. It shows "Presents", the title and the entrance
screen. So the Megamax build, the VBL-queue change and the Setpalette
palette path are sound, independently of the Mega CD.

**On the Mega CD, two faults.**

1. **The sound chip's address resets the gate array.** DM's sound init
   silences the PSG by writing `$FF8800`. On the sub CPU that address is
   a mirror of the gate array's own reset register (`$FF8001`), so the
   write reset the peripherals and cleared the interrupt mask: no VBL,
   no timer, and DM waiting forever in `Vsync`. Found with a write
   watch on the gate array in a locally patched gpgx. The patch now
   stubs `F0061_SOUND_SetChannelAmplitudes` as well. Any ST program that
   touches the YM2149 directly will do the same thing here.
2. **Memory.** After that fix DM starts, finds the bulk arena (80 KB at
   `$68000`), and stops with SYSTEM ERROR 40, out of memory.

**How much DM needs, measured.** DM was run under Hatari behind a
launcher that leaves it a set amount of TPA. It reaches the entrance
with 400 KB and fails at 390 KB. Its own image is 166 KB plus Megamax's
8 KB stack, so it wants about 225 KB of heap, and in-game may want more.

**How much this machine gives.** A program here gets 263,608 bytes of
TPA from the desktop and 264,154 from AUTO, so the desktop costs almost
nothing. That leaves DM 89 KB of heap plus the 80 KB arena, about
135 KB short. Where more could come from:

| | KB | catch |
|---|---|---|
| a 16 KB C: instead of 32 KB | +16 | to the bulk arena |
| the GEM region | +33 | safe only when the AES has not started, so DM booted from AUTO |
| the timeshare scratch, `$7C000`–`$7EFFF` | +12 | Mode 1 only: no CD BIOS to exchange |
| the CD BIOS parking space in Word RAM | +24 | Mode 1 only |
| an EmuTOS built without the AES and desktop | a lot | a separate "game" boot |

The first four are scattered regions, so DM's allocator would need to
take permanent allocations from more than one. Its large temporary
allocations come off the heap. The title alone takes 133 KB, but DM
skips the title when there is not room for it.

### Order of work

1. ~~Build the extracted S12E sources.~~ **Done, with Megamax C itself.**
   ReDMCSB ships the Megamax compiler and linker, a command shell and a
   shutdown program, and its own build runs them under Hatari. Hatari
   runs on Linux too, with the EmuTOS image ReDMCSB carries as its TOS,
   so the original toolchain builds DM 1.2 headless in about two
   minutes. Nothing has to be translated to gcc. That matters because
   about 3000 lines of DM are inline assembly in Megamax's syntax. The
   output is not byte-identical to ReDMCSB's reference binary, and
   ReDMCSB's own spreadsheet lists this executable as "Wrong compiler
   version". The version reduction is exact: each file preprocesses the
   same as the full tree.
2. ~~The romdisk.~~ **Done**, see above.
3. ~~Patch DM for this machine.~~ **Patched and building**:
   `tools/build-dm.sh`, `patches/dm/s12e-megacd.patch`. What the patch does:
   - **VBL.** The VBL routine goes in the first free VBL-queue slot and
     returns with `rts`. Vector `$70` is the CD drive's interrupt here.
   - **Palette.** Where the routine programmed Timer B for the mid-screen
     palette change, it now sets `colorptr` to the dungeon view's palette.
     EmuTOS hands that to the servant at the next VBL, through the
     Setpalette path added in the same change. One palette for the whole
     screen, so in darkness the panels darken with the view; a per-tile
     CRAM line could restore the split later.
   - **Fades.** They go through a copy of the palette, read back with
     `Setcolor(n, -1)`.
   - **Sound.** Silent: the sample player returns at once.
   - **Pointer.** The desktop's pointer sprite is hidden with Line-A
     `A00A`.
   - **Disks.** The floppy checks always succeed, and nothing is
     formatted. The data files open from the current drive, and saves go
     to `S:\DMGAME.DAT`.
   - **Memory.** The GEM region is not used. On an ST it is safe only
     because DM stops Timer C; here the gate array's tick keeps calling
     the AES timer hook, which writes into it. The permanent allocations
     go in the bulk arena instead, found through the `SgCD` cookie and
     `SCD_BULK_INFO`.
   - **ReDMCSB bug.** It never defines `G0319_ul_LoadGameTime` without
     the copy protection; the patch defines it.

   Not yet run: that needs a DM 1.2 `DUNGEON.DAT` and `GRAPHICS.DAT`.
   Memory is the first thing to measure. This EmuTOS is 232 KB, which
   fills Word RAM, so the Alt-RAM the plan counted on does not exist, and
   ST-RAM after EmuTOS's 85 KB of BSS and the 32 KB screen leaves DM's
   heap far smaller than a 520ST's. The bulk arena is the reserve.
4. Count cache misses on a walk through level 1, on both boots.
5. Sound on the PCM chip.
6. Saves on S:.
