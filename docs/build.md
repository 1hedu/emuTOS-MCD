# Building it

Two artefacts come out of this tree and they are the same system:

    build/emutosmd-U.iso + .cue      a bootable Mega CD disc
    build/m1emu.bin                  a Mode 1 cartridge, no disc needed

The disc is the ordinary way in. The cartridge exists because a Mega CD
with a dead laser or no CD-R burner is still a Mega CD: it carries
EmuTOS, the ramdisk and the servant in ROM and boots without the drive
ever spinning. See `docs/mode1.md`.

## What you need

**A cross-compiler.** `tools/setup-toolchain.sh` builds binutils 2.42
and gcc 13 for `m68k-elf` into `/opt/m68k-elf`, from the FreeMiNT
project's mirrors. It wants `sudo` and about half an hour. Its header
says why a distribution's `m68k-linux-gnu` will not do: it ICEs on
`-mshort`, and its libgcc is 68020-encoded, which would crash a real
68000.

**Host tools.** `python3`, `genisoimage` and `git` build the images.
`unzip` as well if you run the emulator harness, which is vendored as a
zip, and `curl` if you build Sonic. Nothing else.

**Submodules.** `git clone --recurse-submodules`, or `git submodule
update --init` after the fact. Three are used: `emutos` (this project's
branch of it), `vendor/megadev` (the cartridge header and console
bring-up — four hand-rolled attempts got four "no change"es off the
console before this replaced them), and `SGDK`, which nothing builds
against and which is there for reference.

**For the emulator only, a console BIOS.** Genesis Plus GX and PicoDrive
both need `bios_CD_U.bin` / `bios_CD_E.bin` / `bios_CD_J.bin` to boot a
Sega CD image. They are Sega's; put your own dumps in `vendor/bios/`,
which is git-ignored. Nothing in this repo ships one and nothing about
building the disc needs one — only running it in emulation does.

## The build

    PATH=/opt/m68k-elf/bin:$PATH
    cd emutos && make segacd ELF=1 && cd ..     # -> emutos/emutos-segacd.img
    tools/build-iso.sh U                        # -> build/emutosmd-U.iso + .cue
    tools/build-rom.sh boot/m1emu.S             # -> build/m1emu.bin

`make segacd` is the only step that has to be run by hand; the ISO
script takes the image it produces and fails with the command to run if
it is not there. `J` and `E` build the other two regions — the letter
picks the security block assembled into `ip.bin` and the region code at
0x1F0 of the boot area, and a console will not boot an image built for
another region.

**`J` does not currently boot, and it is not a new fault.** Under
Genesis Plus GX with a Japanese BIOS the disc parks on the BIOS screen
and stays there: at 4500 and at 6000 frames the frame hash is the same
`355696773a9e4d50`, and `--dump-wram` shows the servant's region of
Genesis work RAM — `0xFF1000..0xFF7000`, 24576 bytes — still entirely
zero, so `ip.bin` never ran and nothing was ever handed across. The disc
image built on 11 August behaves identically, so this has been true for
as long as there have been J builds and nobody had checked. `U` and `E`
both reach the desktop. The place to start is `boot/ip.S`: the three
security blocks are 342, 1412 and 1390 bytes and the `bra IP_Start` that
follows one sits at a different offset in each, which is fine if the
BIOS falls through the block and wrong if the Japanese one does anything
else.

The cartridge takes its payloads from `build/`, so it has to be built
after the ISO, and `tools/build-iso.sh` deletes `iofw.bin` and
`ADISK.IMG` before it rebuilds anything: a failed compile used to leave
the previous ones in place and `build-rom.sh` would wrap the stale ones
into a ROM that looked freshly built. A whole hardware round went into
testing a binary that did not contain the change being tested.

`build-rom.sh` also refuses to wrap a filesystem carrying any of the
emulator-only AUTO programs. Those wait for a cartridge swap or hold the
boot forever, which on a television is a console that does some test
instead of booting -- which is exactly how it was reported the one time
such a build reached hardware.

**The romdisk.** `build-rom.sh` gives a cartridge the files a disc
keeps on D:. Whatever is in `vendor/stsoft/` (and in `DDISK_DIR`, when
that is set) goes into a FAT image in the ROM at 512 KB, after the
payloads, and EmuTOS mounts it as R:. Not D:: a cartridge boot can have
a disc in the tray as well, and D: is that disc. A program put in
`vendor/stsoft/` is on D: on a disc boot and R: on a cartridge, and so
is one level of folders under it -- `vendor/stsoft/BIN/GEN.TTP` is
`R:\BIN\GEN.TTP` on the cartridge. The image is sized to
its files in 16 KB steps and can be at most 1.5 MB, because the save RAM
is mapped over the ROM from 2 MB. An empty `vendor/stsoft/` builds the
same ROM as before. The servant reads it a sector at a time for the sub
(cart op 11), and EmuTOS only uses it when sector 0 carries the stamp
this builder writes; a disc boot has no R:, and its icon is not drawn.

## Running it

On hardware: burn the `.cue`/`.iso` pair, or flash `m1emu.bin` to a
cartridge. Both boot to the same desktop, and the cartridge does so with
or without a disc in the tray.

In emulation:

    tools/run-emu.sh gpgx-patched build/emutosmd-U.cue 3000
    tools/run-emu.sh gpgx         build/m1emu.bin      1400

It dumps a frame and the Genesis work RAM, and prints a hash of each.
The cartridge reaches the desktop sooner because it has no disc to read.

Why `gpgx-patched` for the disc: the stock core segfaults under this
harness somewhere between frame 1200 and 2000, short of a desktop. The
fault is the harness's — it answers true to
`RETRO_ENVIRONMENT_GET_VARIABLE` without filling in `.value`, and the
core hands that NULL to `atoi`. `tools/gpgx-cdtrace.patch` filters the
command out; one run of `tools/trace-emu.sh` builds the patched core
into `.emu/`. `tools/run-emu.sh` says so if you ask the stock core for
more frames than it will survive.

## Flags

`tools/build-iso.sh` reads these from the environment. The first four
are the ones worth knowing. The programs and README.TXT are on D:
on a disc and R: on a cartridge; C: carries only EMUICON.RSC and
EMUDESK.INF, which EmuTOS reads from the boot drive, unless `FULLC` asks (MANDEL.PRG computes
the picture in DEMO.PI1 on the machine, in about 37 seconds, and saves
it as C:\MANDEL.PI1); `SLIMC=1`, which used to leave
them off, is now what happens anyway.

| | |
|---|---|
| `FULLC=1` | put the programs, README.TXT and DEMO.PI1 on C: as well |
| `ADISK_DIR=<dir>` | put a directory's files on C: |
| `ADISK_SIZE=0x…` | how big C: is, and so how much PRG RAM a payload has left; 0xC000 (48 KB) by default, 0x1C000 at most |
| `SONICACC=1` | add the Sonic accessory (see below) |
| `DIAG=1` | the diagnostic programs on C: and D: |
| `AUTORUN=D:\\X.PRG` | the AES starts that program instead of the desktop; `AUTORUN_GEM=0` for a .PRG that is a TOS program |

The rest — `NOASK`, `PALTEST`, `SHOWAUTO`, `EDITAUTO`, `NATAUTO`,
`PRNAUTO`, `DDAUTO`, `ROMDAUTO`, `SPLITAUTO`, `BRAMAUTO`, `BRAMRW`, `FMTIAUTO`, `ACCAUTO`,
`HELLOA`, `AUDIT`, `CDDTEST` — build programs into `AUTO` that run
before the desktop and then hold, so a headless emulator run exercises
one path without a hand on the pad. They are emulator-only, by name, in
`tools/build-rom.sh`'s guard.

`tools/build-datadisc.sh U` builds a second disc: same bootable system,
`datadisc/`'s contents on D:. `datadisc/` is git-ignored apart from its
README, on the rule `vendor/` follows.

## Adding Sonic

Sonic is not on the released disc and cannot be: he is Sega's art and
Sega's movement constants, and this repository distributes neither. What
is here is the pipeline that reads them out of dumps you already own,
and the port of GEOS-Genesis's engine onto this machine — see
`docs/sonic.md` for what that port had to solve.

You need:

  * `assets/sonic/sonic1.md` — your own Sonic 1 dump, plain binary
  * `assets/sonic/sonic2.md` — your own Sonic 2 dump (the skid dust is
    Sonic 2's)

`assets/` is git-ignored. An interleaved `.smd` dump has to be converted
first; `tools/sonic-tools/smd_to_bin.py` does it.

Then:

    tools/build-sonic.sh
    SONICACC=1 ADISK_DIR=$PWD/datadisc ADISK_SIZE=0x1C000 \
        tools/build-iso.sh U
    tools/build-rom.sh boot/m1emu.S             # if you want it on the cart

`build-sonic.sh` runs two steps. `tools/build-sonic-art.sh` unpacks the
tiles, the frame index, the DPLC runs and the sprite pieces out of the
ROMs, and fetches six files from the Sonic Retro disassemblies for the
mappings — that is what wants `curl`, and it caches into
`vendor/s1disasm` and `vendor/s2disasm`, both git-ignored.
`tools/build-payload.sh payload/sonic` assembles the engine. Two files
land in `datadisc/`:

    SONIC.MDP    8380 bytes   the engine, staged into the servant's cache
    SONIC.MDD   52242 bytes   the art, loaded to sub $60000

The `.MDD` is the general facility and not a Sonic one: `NATIVE.PRG`
loads whatever `.MDD` sits beside the `.MDP` it is running and knows
nothing about what is in it.

`ADISK_DIR` puts both on C:, and `SONICACC` adds the accessory. SHOW,
EDIT and DEMO.PI1 are on D:, not C:, which is the room this needs. The ramdisk is then the
whole region — 114688 bytes, nothing left over — which is fine, because
a file already on C: is not copied anywhere.

**Desk → Sonic** is the whole of the user interface. An accessory rather
than a program, because a `.PRG` launched from the desktop is handed the
screen, and by the time it has said what it is for, the desktop he was
supposed to be standing on is gone. Start ends it; so does walking into
the sign post at the end of the fourth screen.


## Adding Dungeon Master

Not working yet: it builds, but has not been run against real data.
`docs/ports.md` has the plan and the state.

    tools/build-dm.sh S11E            # or S12E: the version your disk is
    # copy DUNGEON.DAT and GRAPHICS.DAT from that disk into
    # vendor/stsoft/, then either build:
    ADISK_SIZE=0x8000 tools/build-iso.sh U
    tools/build-rom.sh boot/m1emu.S   # the cartridge: R: is the romdisk

`build-dm.sh` fetches ReDMCSB, cuts DM 1.2 English out of it
(`tools/dm-reduce.py`), applies `patches/dm/s12e-megacd.patch` and builds
it with ReDMCSB's own Megamax C toolchain under Hatari. It needs
`unifdef`, `hatari` and either `7z` or Python's `py7zr`. ReDMCSB is
cached in `.cache/`, which is git-ignored like the data files.

`ADISK_SIZE=0x8000` is there for memory. DM takes all of the
TPA, and its permanent allocations go in the bulk arena: the PRG-RAM
between the end of C: and the servant's scratch. A 32 KB C: leaves 80 KB
there, and the default 48 KB C: leaves 64 KB.

## Adding Devpac

HiSoft's Devpac 3 runs as it is: the editor, and the assembler, which
writes programs to C: that then run.

    tools/install-devpac.sh <your Devpac 3 disk image>.st
    tools/build-iso.sh U && tools/build-rom.sh boot/m1emu.S

The script copies `DEVPAC.PRG`, `BIN`, `INCDIR` and `EXAMPLES` into
`vendor/stsoft/` and points `HISOFTED.INF`'s paths at D:. D: is
read-only, so assemble with the output on C: (in the editor, Options →
Assembly; on the command line, `GEN.TTP file.S -OC:\FILE.PRG`).

## Adding Sokoban

Peter Lane's GEM Sokoban, from the Atari_ST_Sources archive, with the
50 classic levels built in:

    tools/build-sokoban.sh            # -> vendor/stsoft/SOKOBAN.PRG, .RSC

It needs `m68k-atari-mint-gcc` and GEMlib, which Vincent Rivière's
`cross-mint-essential` package provides (his PPA, `ppa:vriviere/ppa`). The
script fetches the source and libcmini at pinned commits into `.cache/`
and builds `-mshort`, because the game was written for AHCC's 16-bit
`int`. Best scores go to `I:\SOKOSCOR.TXT`, so they persist on both
boots. The game is played with the cursor keys: on the pad, hold C and
use the d-pad (C + d-pad sends the arrow keys everywhere, not just here),
or the serial keyboard, or Start for the on-screen one.

## Adding NEOchrome

Atari's NEOchrome 1.0 (Dave Staugas, 1986), the low-resolution paint
program. It is not in this repository; the disk image is yours, for
instance "NEOchrome v1.0" from planetemu's Atari ST applications:

    tools/install-neochrome.sh NEOchrome.st   # -> vendor/stsoft/NEOCHROM/

That copies `NEONEW.PRG` off the disk and builds `NEO.PRG` beside it.
Run `D:\NEOCHROM\NEO.PRG`, not `NEONEW.PRG`: the launcher loads NEOchrome,
patches it in memory for this machine (`progs/neo.c` lists what and
why) and starts it. It works on both boots. Draw with A. The FULL SCREEN
button at the toolbox's left edge hides the toolbox; push the pointer to
the bottom edge of the screen and press A to bring it back. The colour picker's
bands come from the raster palette (docs/raster.md), which needs this
branch's EmuTOS and servant; on anything older the launcher still runs
NEOchrome, with one palette for the whole screen. Save to C:, I: or S:; D: is read-only.

