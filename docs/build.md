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

## Running it

On hardware: burn the `.cue`/`.iso` pair, or flash `m1emu.bin` to a
cartridge and leave the tray empty. Both boot to the same desktop.

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
are the ones worth knowing:

| | |
|---|---|
| `SLIMC=1` | leave SHOW, EDIT and DEMO.PI1 off C:, freeing ramdisk |
| `ADISK_DIR=<dir>` | put a directory's files on C: |
| `ADISK_SIZE=0x…` | how big C: is, and so how much PRG RAM a payload has left |
| `SONICACC=1` | add the Sonic accessory (see below) |
| `DIAG=1` | the diagnostic programs on C: and D: |

The rest — `NOASK`, `PALTEST`, `SHOWAUTO`, `EDITAUTO`, `NATAUTO`,
`PRNAUTO`, `DDAUTO`, `BRAMAUTO`, `BRAMRW`, `FMTIAUTO`, `ACCAUTO`,
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
    SLIMC=1 SONICACC=1 ADISK_DIR=$PWD/datadisc ADISK_SIZE=0x1C000 \
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

`ADISK_DIR` puts both on C:, `SONICACC` adds the accessory, and `SLIMC`
drops SHOW, EDIT and DEMO.PI1 to make room. The ramdisk is then the
whole region — 114688 bytes, nothing left over — which is fine, because
a file already on C: is not copied anywhere.

**Desk → Sonic** is the whole of the user interface. An accessory rather
than a program, because a `.PRG` launched from the desktop is handed the
screen, and by the time it has said what it is for, the desktop he was
supposed to be standing on is gone. Start ends it; so does walking into
the sign post at the end of the fourth screen.

## One thing to know about this repository's history

Two Sega ROM archives were committed at the top of the tree on the first
day, through GitHub's web uploader, before `assets/` existed and before
the rule that keeps dumps out of here did. They are out of `HEAD` now
and `/*.zip` is git-ignored, but **deleting a file does not remove it
from the history**, and anyone who clones this repository still gets
them. Taking them out for good means rewriting history — `git filter-repo
--invert-paths`, or BFG — and force-pushing, which breaks every existing
clone and every commit hash after the rewrite point. That is a decision
for whoever owns the repository, not something a build script can do.
