#!/usr/bin/env bash
# Build a bootable Sega CD disc image.
#
# Usage: tools/build-iso.sh [J|U|E] [payload.bin] [out-basename]
#   region   security block + region code baked into the boot area (default J)
#   payload  raw 68000 binary, base/entry 0x200000, loaded to Word RAM as
#            M_INIT.PRG (default: build boot/m_init.S green-screen test)
# Produces build/<out>.iso + .cue.
set -euo pipefail
# A failed compile used to leave the previous iofw.bin and ADISK.IMG in
# place, and build-rom.sh would happily wrap the stale ones into a ROM
# that looked freshly built. A whole hardware round was spent testing a
# binary that did not contain the change being tested. So the outputs
# go before anything is rebuilt: a broken build now produces no ROM at
# all rather than a convincing old one.
rm -f "$(cd "$(dirname "$0")/.." && pwd)/build/iofw.bin" \
      "$(cd "$(dirname "$0")/.." && pwd)/build/fs/ADISK.IMG"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

REGION="${1:-J}"
PAYLOAD="${2:-}"
OUT="${3:-emutosmd-$REGION}"

case "$REGION" in
  J) RSYM=REGION_JAP ;;
  U) RSYM=REGION_USA ;;
  E) RSYM=REGION_EUR ;;
  *) echo "region must be J, U or E" >&2; exit 1 ;;
esac

PATH=/opt/m68k-elf/bin:$PATH
AS="m68k-elf-as -m68000 --register-prefix-optional"
LD=m68k-elf-ld
OC=m68k-elf-objcopy
SEC="$ROOT/vendor/projectcd/src/bootsect"   # security/ blocks live here
# Emptied, not just created. The staging directory is copied wholesale
# into the image, so anything a previous build left behind ships on the
# disc -- and boot/sp.S picks its image by button, so a stale
# EMUTOS3.IMG is not dead weight, it is a second bootable build that
# answers a d-pad. One did: a disc described as carrying the current
# build alone went out with three images from an earlier run still on
# it, and the console obediently booted one of them.
B="$ROOT/build"; rm -rf "$B/fs"; mkdir -p "$B/fs"

bin() { # bin <src.S> <text-base> <out.bin> [extra as flags...]
  local src="$1" base="$2" out="$3"; shift 3
  $AS -I "$B" -I "$ROOT/boot" -I "$SEC" "$@" -o "$out.o" "$src"
  $LD -Ttext "$base" --oformat binary -o "$out" "$out.o"
  rm -f "$out.o"
}

# AUDIT=1 builds the E-A1 memory-audit disc: IP takes two PRG-RAM snapshots
# around the CD activity and the payload parks them in WRAM for --dump-wram.
IPFLAGS=()
if [[ "${AUDIT:-}" = 1 ]]; then IPFLAGS=(--defsym AUDIT=1); OUT="emutosmd-audit"; fi

bin "$ROOT/boot/ip.S"  0x0      "$B/ip.bin" --defsym $RSYM=1 "${IPFLAGS[@]}"
SPFLAGS=()
if [[ "${CDDTEST:-}" = 1 ]]; then SPFLAGS=(--defsym CDDTEST=1); OUT="emutosmd-cddtest"; fi
bin "$ROOT/boot/sp.S"  0x6000   "$B/sp.bin" "${SPFLAGS[@]}"
bin "$ROOT/boot/bootsect.S" 0x0 "$B/bootsect.bin" --defsym $RSYM=1

SIZE=$(stat -c%s "$B/bootsect.bin")
[[ "$SIZE" -eq 32768 ]] || { echo "bootsect.bin is $SIZE bytes, want 32768" >&2; exit 1; }

# C program -> flat binary (crt0 first, tools/flat.ld, -Ttext base)
CC="m68k-elf-gcc -m68000 ${CFLAGS_EXTRA:-} -O2 -fomit-frame-pointer -ffreestanding -Wall"
cbin() { # cbin <dir> <base> <out.bin>
  local dir="$1" base="$2" out="$3" c o
  local objs=("$out.crt.o") a
  $CC -c "$dir/crt0.S" -o "$out.crt.o"
  for a in "$dir"/*.S; do            # crt0.S is already first, on purpose
    [[ "$(basename "$a")" = crt0.S ]] && continue
    o="$B/$(basename "${a%.S}").o"
    $CC -c "$a" -o "$o"; objs+=("$o")
  done
  for c in "$dir"/*.c; do
    o="$B/$(basename "${c%.c}").o"
    $CC -c "$c" -I"$dir" -o "$o"; objs+=("$o")
  done
  m68k-elf-ld -T "$ROOT/tools/flat.ld" -Ttext="$base" \
    -o "$out.elf" "${objs[@]}" "$(m68k-elf-gcc -m68000 -print-libgcc-file-name)"
  # Does it fit under the planar cache?
  #
  # The servant lives at 0xFF1000 and the cache starts at 0xFF7000, and
  # nothing was checking. Its BSS had run a kilobyte past that line: the
  # last variables declared were inside the region the pump fills with
  # screen every frame, so they were being written correctly and
  # overwritten within the frame. uart_on -- the serial keyboard's own
  # enable flag -- was one of them. Nothing said a word.
  # A check that cannot find what it is checking has to say so rather
  # than pass, which is the whole lesson above in one line.
  if [[ "$base" = 0xFF1000 ]]; then
    local end
    end=$(m68k-elf-nm "$out.elf" | awk '$3=="__bss_end"{print $1}')
    [[ -n "$end" ]] || { echo "$(basename "$out"): no __bss_end in the link" >&2; exit 1; }
    if (( 0x$end > 0xFF7000 )); then
      echo "$(basename "$out"): BSS ends at 0x$end, past the planar cache at" \
           "0xFF7000 by $((0x$end - 0xFF7000)) bytes" >&2
      exit 1
    fi
  fi
  m68k-elf-objcopy -O binary "$out.elf" "$out"
  rm -f "${objs[@]}" "$out.elf"
}

if [[ -z "$PAYLOAD" ]]; then
  if [[ "${CDDTEST:-}" = 1 ]]; then
    bin "$ROOT/boot/m_cddtest.S" 0x200000 "$B/m_cddtest.bin"
    PAYLOAD="$B/m_cddtest.bin"
  elif [[ "${AUDIT:-}" = 1 ]]; then
    bin "$ROOT/boot/m_audit.S" 0x200000 "$B/m_audit.bin"
    PAYLOAD="$B/m_audit.bin"
  else
    # E2 disc: IOFW (relocates itself to work RAM 0xFF1000) + EmuTOS image
    EMUIMG="${EMUIMG:-$ROOT/emutos/emutos-segacd.img}"
    [[ -f "$EMUIMG" ]] || { echo "missing $EMUIMG — run: cd emutos && make clean && make segacd ELF=1" >&2; exit 1; }
    # The CDBIOS is parked at Word RAM 0xBA000-0xBFFFF while EmuTOS runs
    # (boot/sp.S, segacd.c). EmuTOS's own code is loaded at 0x80000, so
    # the image must not reach that far -- if it ever does, the loader
    # would overwrite the parked firmware with EmuTOS's own tail and the
    # first exchange would jump into nothing.
    ESZ=$(stat -c%s "$EMUIMG")
    if [[ "$ESZ" -gt $((0xBA000 - 0x80000)) ]]; then
      echo "EmuTOS image is $ESZ bytes and would run into the CDBIOS park at 0xBA000" >&2
      exit 1
    fi
    python3 "$ROOT/tools/mkfont.py" "$ROOT/iofw/osk_font.h" >/dev/null
    # ASCII -> scancode for the serial keyboard, inverted out of EmuTOS's
    # own US table so the round trip cannot drift.
    ( cd "$ROOT" && python3 tools/mkkeymap.py )
    cbin "$ROOT/iofw"   0xFF1000 "$B/iofw.bin"
    SZ=$(stat -c%s "$B/iofw.bin")
    [[ "$SZ" -le 24576 ]] || { echo "iofw.bin is $SZ bytes, exceeds the 24K relocation copy" >&2; exit 1; }
    # Real TOS programs on the ramdisk, built PC-relative so they need
    # no relocation table.
    PRGCC="m68k-elf-gcc -m68000 -mpcrel -Os -fomit-frame-pointer -ffreestanding -Wall -I$ROOT/emutos/bios"
    $PRGCC -c "$ROOT/progs/tosbind.S" -o "$B/tosbind.o"
    # The same bindings with an accessory's startup instead of a
    # program's: an .ACC is entered with no user stack and has to make
    # one out of its own basepage. See progs/tosbind.S.
    $PRGCC -DACC_STARTUP=1 -c "$ROOT/progs/tosbind.S" -o "$B/tosbindacc.o"
    # Deliberately no libgcc: it is not built -mpcrel and its helpers
    # call each other absolutely, which cannot survive being loaded.
    prg() { # prg <source.c> <NAME.PRG> [extra cc flags...]
      local src="$1" out="$2"; shift 2
      $PRGCC "$@" -c "$src" -o "$B/prg.o"
      m68k-elf-ld -T "$ROOT/tools/prg.ld" -o "$B/prg.elf" \
          "$B/tosbind.o" "$B/prg.o"
      python3 "$ROOT/tools/mkprg.py" "$B/prg.elf" "$B/$out" >/dev/null
      rm -f "$B/prg.o" "$B/prg.elf" "$B/$out.bin"
    }
    # One source, two programs. The drive a format tool will erase is
    # chosen by which icon you double-click, not by a keypress on a
    # machine whose only input is a d-pad.
    prg "$ROOT/progs/format.c"   FORMATS.PRG -DFORMAT_DRIVE=18
    prg "$ROOT/progs/format.c"   FORMATI.PRG -DFORMAT_DRIVE=8
    if [[ -n "${NOASK:-}" ]]; then      # emulator-only: the prompt pre-answered
      prg "$ROOT/progs/format.c" FMTSNOAS.PRG -DFORMAT_DRIVE=18 -DFORMAT_NOASK=1
      prg "$ROOT/progs/format.c" FMTINOAS.PRG -DFORMAT_DRIVE=8 -DFORMAT_NOASK=1
      prg "$ROOT/progs/sdiag.c"  SDIAGAUT.PRG -DDIAG_DRIVE=18 -DDIAG_AUTO=1
    fi
    prg "$ROOT/progs/sramtool.c" SRAMTOOL.PRG
    prg "$ROOT/progs/eject.c"    EJECT.PRG
    # PRNTEST.PRG proves the printer path without a printer: it writes a
    # line to PRN: and the servant mirrors what it transmits where the
    # harness can read it. PRNAUTO puts it in AUTO so a headless run
    # exercises it. Emulator only -- build-rom.sh refuses a cartridge
    # carrying it.
    prg "$ROOT/progs/prntest.c"  PRNTEST.PRG
    [[ -n "${PRNAUTO:-}" ]] && prg "$ROOT/progs/prntest.c" PRNTAUTO.PRG
    # Sonic, as a desk accessory rather than a program, because that is
    # the only way he runs on the desktop instead of instead of it: a
    # .PRG launched from the desktop is handed the screen, and by the
    # time it has said what it is for, the desktop he was supposed to be
    # standing on is gone. An accessory is picked from the Desk menu and
    # the AES does not touch the picture. See progs/sonicacc.c.
    acc() { # acc <source.c> <NAME.ACC>
      local src="$1" out="$2"; shift 2
      $PRGCC "$@" -c "$src" -o "$B/prg.o"
      m68k-elf-ld -T "$ROOT/tools/prg.ld" -o "$B/prg.elf" \
          "$B/tosbindacc.o" "$B/prg.o"
      python3 "$ROOT/tools/mkprg.py" "$B/prg.elf" "$B/$out" >/dev/null
      rm -f "$B/prg.o" "$B/prg.elf" "$B/$out.bin"
    }
    # Emulator only: no hand on the pad, so it opens itself twice at
    # startup instead. Its own filename, and not SONIC.ACC, because
    # build-rom.sh keeps the emulator-only AUTO programs off cartridges
    # by name and could not tell these two apart otherwise -- an ACCAUTO
    # cart would launch Sonic instead of showing a desktop, which is
    # exactly the failure that guard exists to prevent. The AES loads
    # every *.ACC in the root, so the name is free to say what it is.
    if [[ -n "${ACCAUTO:-}" ]]; then
      acc "$ROOT/progs/sonicacc.c" SONICAUT.ACC -DACC_AUTO=1
    else
      acc "$ROOT/progs/sonicacc.c" SONIC.ACC
    fi
    # Something for SHOW.PRG to show on a machine that has never had a
    # file copied to it. Generated, not shipped: this project
    # distributes nobody's artwork, and the Mandelbrot set is
    # arithmetic. It also suits sixteen colours -- the escape-time
    # bands are contours, so a small palette reads as shading.
    python3 "$ROOT/tools/mkpi1.py" "$B/DEMO.PI1" >/dev/null
    # Its own flag, not NOASK: NOASK also drops SDIAGAUT.PRG into AUTO,
    # and that one holds the boot forever waiting for a cartridge swap,
    # so a palette test built alongside it never runs at all. An hour
    # went into three identical frame hashes before that was noticed.
    [[ -n "${PALTEST:-}" ]]  && prg "$ROOT/progs/paltest.c" PALTEST.PRG
    [[ -n "${SHOWAUTO:-}" ]] && prg "$ROOT/progs/show.c" SHOWAUTO.PRG -DSHOW_AUTO=1
    [[ -n "${EDITAUTO:-}" ]] && prg "$ROOT/progs/edit.c" EDITAUTO.PRG -DEDIT_AUTO=1
    [[ -n "${NATAUTO:-}" ]]  && prg "$ROOT/progs/native.c" NATAUTO.PRG -DNATIVE_AUTO=1
    # Does a backup RAM file take a write in place? This one WRITES.
    [[ -n "${BRAMRW:-}" ]]   && prg "$ROOT/progs/bramrw.c" BRAMRWAU.PRG -DBRAMRW_AUTO=1
    # Format I: from AUTO, headless. On a Sega volume this is what
    # creates the EMUTOS file; see docs/bram-filesystem.md.
    [[ -n "${FMTIAUTO:-}" ]] && prg "$ROOT/progs/format.c" FMTIAUTO.PRG \
                                   -DFORMAT_DRIVE=8 -DFORMAT_NOASK=1 -DFORMAT_AUTO=1
    # What Sega's own Backup RAM manager sees. Read-only.
    [[ -n "${BRAMAUTO:-}" ]] && prg "$ROOT/progs/bramtest.c" BRAMAUT.PRG -DBRAM_AUTO=1
    [[ -n "${DIAG:-}" ]]     && prg "$ROOT/progs/bramtest.c" BRAMTEST.PRG
    # D: read + verify, bounded, from AUTO. See progs/diskmark.c.
    [[ -n "${DDAUTO:-}" ]]   && prg "$ROOT/progs/diskmark.c" DDISKAUT.PRG \
                                   -DDISKMARK_AUTO="${DDAUTO_BYTES:-262144}"
    # HELLOA puts the stub payload on A:, which is where NATAUTO looks
    # first. It used to be part of NATAUTO itself; it is separate now
    # because a data disc carrying a payload of its own would have had a
    # decoy in front of it and the test would have run the wrong thing.
    [[ -n "${HELLOA:-}" ]]   && "$ROOT/tools/build-payload.sh" \
                                    "$ROOT/payload/hello.S" "$B/HELLO.MDP" >/dev/null
    # A picture for SHOWAUTO to find: sixteen vertical bars, one per pen,
    # with a palette that is nothing like the desktop's, in the Degas
    # layout exactly as the format specifies it. If the frame comes back
    # holding these colours in these places, the viewer read the header,
    # set the palette and put the bitmap on the screen.
    if [[ -n "${SHOWAUTO:-}" ]]; then
      python3 - "$B/TEST.PI1" <<'PI1'
import sys, struct
pal = [(i << 8) if i < 8 else ((i - 8) << 4) for i in range(16)]
out = bytearray(struct.pack('>H', 0) + b''.join(struct.pack('>H', c) for c in pal))
for y in range(200):
    for g in range(20):          # 20 groups of 16 pixels, pen = group & 15
        pen = g & 15
        for p in range(4):
            out += struct.pack('>H', 0xFFFF if (pen >> p) & 1 else 0x0000)
open(sys.argv[1], 'wb').write(bytes(out))
PI1
    fi
    # NATIVE.PRG needs the SCD cookie, like EJECT and SWAP.
    prg "$ROOT/progs/native.c"   NATIVE.PRG
    prg "$ROOT/progs/show.c"     SHOW.PRG
    prg "$ROOT/progs/edit.c"     EDIT.PRG
    # The diagnostics, and SWAP.
    #
    # The diagnostics were written to answer faults that are now
    # answered: why S: would not open (the connector erases a
    # hot-inserted cartridge; docs/ramcart-hotswap.md), why I: never
    # mounted, whether D: reads at all.
    #
    # SWAP is a different case and it is worth being exact, because the
    # program is not broken. Everything it does works: the map flip is
    # survived, the picture comes back, the new cartridge is probed,
    # mounted, formatted and read and written for the rest of the
    # session. What does not work is the reason anyone would want it.
    # The insertion erases the cartridge, so the S: it produces is
    # scratch -- and on a Mode 1 boot it replaces an S: that was the
    # boot cartridge's own save RAM. That is a trade of a small volume
    # for a large volatile one, paid for with a real chance of wedging
    # the machine on the pull. Not a tool.
    #
    # All the sources stay, because the next fault will want them, and
    # DIAG=1 puts every one of these back on the images.
    if [[ -n "${DIAG:-}" ]]; then
      prg "$ROOT/progs/swap.c"     SWAP.PRG
      prg "$ROOT/progs/cdtest.c"   CDTEST.PRG
      prg "$ROOT/progs/sdiag.c"    SDIAG.PRG  -DDIAG_DRIVE=18
      prg "$ROOT/progs/sdiag.c"    IDIAG.PRG  -DDIAG_DRIVE=8
      prg "$ROOT/progs/diskmark.c" DISKMARK.PRG
    fi
    rm -f "$B/tosbind.o"

    cbin "$ROOT/subeng" 0x10000  "$B/subprog.bin"
    cp "$B/subprog.bin" "$B/fs/SUBPROG.BIN"
    cp "$EMUIMG" "$B/fs/EMUTOS.IMG"
    # Optional earlier builds, chosen at boot by a held direction in
    # boot/sp.S. One disc instead of one per commit: the images differ
    # by a couple of hundred kilobytes and the disc has twelve megabytes
    # of padding doing nothing.
    for n in 2 3 4; do
      v="EMUIMG$n"
      if [[ -n "${!v:-}" ]]; then
        [[ -f "${!v}" ]] || { echo "missing ${!v}" >&2; exit 1; }
        z=$(stat -c%s "${!v}")
        if [[ "$z" -gt $((0xBA000 - 0x80000)) ]]; then
          echo "${!v} is $z bytes and would run into the CDBIOS park" >&2
          exit 1
        fi
        cp "${!v}" "$B/fs/EMUTOS$n.IMG"
      fi
    done
    cat > "$B/readme.txt" <<'TXT'
EmuTOS for Sega Mega CD
=======================

This desktop is stock EmuTOS running on the
Mega CD's sub 68000, with the ST low-memory
model byte-authentic. The Genesis-side CPU
converts the ST screen to VDP tiles, samples
the controller and Sega Mouse, and proxies
the backup RAM cartridge.

Drive C: this ramdisk, and the system drive.
Drive D: the compact disc.
Drive I: the console's internal backup RAM, 8K.
Drive S: the cartridge's save RAM.

There is no A: and no B:.  This machine has
no floppy drive, so rather than give the name
to something that is not one, every drive is
lettered for what it is.  C: keeps its usual
meaning: the system drive, the current drive
at startup, where the AUTO folder is looked
for and what PATH points at.  It is rebuilt at
every start from the disc, or from the boot
cartridge's ROM, so it is always there and
nothing you write to it is kept.

Settings are, though.  Options > Save Desktop
writes EMUDESK.INF to I:, which is battery-
backed and is read by both ways of booting --
so the background, the icon layout and the
window positions follow you between the disc
and the cartridge.  Format I: once first.

FORMATS.PRG / FORMATI.PRG erase and remake a
filesystem on S: or I:.  Two programs, so the
one you double-click is the one that runs.
Formatting I: takes any Sega game saves with
it -- copy them off first.

SRAMTOOL.PRG copies I: to S:\BRAM.BIN, raw, so
a flash cart carries it out to an SD card.
That is how you keep those saves.

EJECT.PRG changes the disc in the drive.

SHOW.PRG displays Atari ST pictures -- .PI1
(Degas) and .NEO (NEOchrome), low resolution,
uncompressed.  It lists what is in the folder
you run it from; a letter shows one, any key
comes back.  DEMO.PI1 is here to try it on.

NATIVE.PRG runs Genesis-side code off a disc
-- a .MDP file.  EmuTOS is on the Mega CD's
sub 68000 and cannot reach the VDP, the pads
or the sound chips at all; a payload runs on
the other CPU, with the machine handed to it,
and the desktop comes back when it ends.  The
data disc is where they live.

EDIT.PRG is a text editor.  Arrows move the
caret, Escape reaches save and quit.  Both of
those keys are on the on-screen keyboard --
and a real keyboard on the serial port is far
pleasanter, which the keyboard checkbox turns
on.

The cartridge cannot be changed the same way.
A backup RAM cart pushed into a live slot
comes up erased -- the slot has no ground-
first contacts and these carts have no
supervisor, so the array is blank before any
code runs.  Have the cartridge you want in the
slot before you switch the console on.
TXT
    # TOS text viewers need CR+LF; a lone LF line-feeds without a carriage
    # return, so the console staircases the text into unreadable mush.
    sed -i 's/$/\r/' "$B/readme.txt"
    # Drive icons, and the assignment of one to each drive. Both are
    # files EmuTOS looks for in the root of the boot drive -- see
    # app_rdicon() and read_inf_file() in desk/deskapp.c -- so this
    # machine gets icons that match its storage without a line of EmuTOS
    # changing.
    ( cd "$ROOT" && python3 tools/mkicon.py --rsc "$B/EMUICON.RSC" \
                 && python3 tools/mkicon.py --inf "$B/EMUDESK.INF" )
    # How big C: is, and therefore how much PRG RAM is left over for a
    # payload's bulk data. 0x1C000 is the whole of the region between
    # EmuTOS's phystop and the timeshare's scratch, which is the right
    # default for a system disc and the wrong one for a disc carrying a
    # .MDD: the driver takes the ramdisk's length from this image's own
    # boot sector, so shrinking it here is what frees the rest.
    # What goes on C:. SLIMC leaves out everything that is also on D:
    # and everything a data disc has no use for -- the format tools, the
    # picture, the editor -- because the ramdisk's length is what
    # decides how much PRG RAM is left for a payload's bulk data, and a
    # full one leaves none. See ADISK_SIZE above and docs/payload.md.
    CADD=(--add "$B/readme.txt:README.TXT"
          --add "$B/EMUICON.RSC:EMUICON.RSC"
          --add "$B/EMUDESK.INF:EMUDESK.INF"
          --add "$B/EJECT.PRG:EJECT.PRG"
          --add "$B/NATIVE.PRG:NATIVE.PRG")
    CADD+=(--add "$B/FORMATS.PRG:FORMATS.PRG"
           --add "$B/FORMATI.PRG:FORMATI.PRG"
           --add "$B/SRAMTOOL.PRG:SRAMTOOL.PRG")
    if [[ -z "${SLIMC:-}" ]]; then
      CADD+=(--add "$B/SHOW.PRG:SHOW.PRG"
             --add "$B/EDIT.PRG:EDIT.PRG"
             --add "$B/DEMO.PI1:DEMO.PI1")
    fi
    # ADISK_DIR puts files on C: the way DDISK_DIR puts them on D:. A
    # payload's bulk data has to be on C: when there is no other drive --
    # a cartridge boot has none -- and NATIVE.PRG then runs it out of the
    # ramdisk where it lies rather than copying it anywhere.
    if [[ -n "${ADISK_DIR:-}" && -d "${ADISK_DIR}" ]]; then
      while IFS= read -r -d '' f; do
        bn=$(basename "$f")
        [[ "$bn" = README.md ]] && continue
        bn=$(echo "$bn" | tr '[:lower:]' '[:upper:]')
        CADD+=(--add "$f:$bn")
      done < <(find "$ADISK_DIR" -maxdepth 1 -type f -print0 | sort -z)
    fi
    [[ -n "${DIAG:-}" ]] && CADD+=(--add "$B/SWAP.PRG:SWAP.PRG"
                                   --add "$B/CDTEST.PRG:CDTEST.PRG"
                                   --add "$B/SDIAG.PRG:SDIAG.PRG"
                                   --add "$B/IDIAG.PRG:IDIAG.PRG")
    [[ -n "${NOASK:-}" ]] && CADD+=(--add "$B/FMTSNOAS.PRG:FMTSNOAS.PRG"
                                    --add "$B/FMTINOAS.PRG:FMTINOAS.PRG"
                                    --add "$B/SDIAGAUT.PRG:AUTO/SDIAGAUT.PRG")
    [[ -n "${PALTEST:-}" ]] && CADD+=(--add "$B/PALTEST.PRG:AUTO/PALTEST.PRG")
    [[ -n "${SHOWAUTO:-}" ]] && CADD+=(--add "$B/SHOWAUTO.PRG:AUTO/SHOWAUT.PRG"
                                       --add "$B/TEST.PI1:TEST.PI1")
    [[ -n "${EDITAUTO:-}" ]] && CADD+=(--add "$B/EDITAUTO.PRG:AUTO/EDITAUT.PRG")
    [[ -n "${NATAUTO:-}" ]] && CADD+=(--add "$B/NATAUTO.PRG:AUTO/NATAUT.PRG")
    [[ -n "${HELLOA:-}" ]] && CADD+=(--add "$B/HELLO.MDP:HELLO.MDP")
    [[ -n "${PRNAUTO:-}" ]] && CADD+=(--add "$B/PRNTAUTO.PRG:AUTO/PRNTAUT.PRG")
    [[ -n "${DDAUTO:-}" ]] && CADD+=(--add "$B/DDISKAUT.PRG:AUTO/DDISKAUT.PRG")
    [[ -n "${BRAMAUTO:-}" ]] && CADD+=(--add "$B/BRAMAUT.PRG:AUTO/BRAMAUT.PRG")
    [[ -n "${BRAMRW:-}" ]] && CADD+=(--add "$B/BRAMRWAU.PRG:AUTO/BRAMRWAU.PRG")
    [[ -n "${FMTIAUTO:-}" ]] && CADD+=(--add "$B/FMTIAUTO.PRG:AUTO/FMTIAUTO.PRG")
    # The accessory is only worth loading where its payload is: the AES
    # reads every *.ACC in the root of the boot drive at startup, and one
    # that puts a line in the Desk menu leading to a file that is not
    # there is worse than no line at all.
    if [[ -n "${SONICACC:-}" ]]; then
      if [[ -n "${ACCAUTO:-}" ]]; then
        CADD+=(--add "$B/SONICAUT.ACC:SONICAUT.ACC")
      else
        CADD+=(--add "$B/SONIC.ACC:SONIC.ACC")
      fi
    fi
    python3 "$ROOT/tools/mkfat.py" "$B/fs/ADISK.IMG" --size "${ADISK_SIZE:-0x1C000}" \
      --label EMUTOSMD --oem EmuTOSMD "${CADD[@]}" >/dev/null
    # D: -- a FAT16 filesystem that stays on the disc and is read a
    # sector at a time, rather than being loaded into RAM like A:. That
    # is the point of it: it exercises the CD read path with arbitrary
    # sectors on demand, which one test read cannot.
    cat > "$B/dread.txt" <<'TXT'
This file is on the compact disc.

Reading it means the sub CPU seeked the
drive, took the sector off it through the
CDC, and handed it to GEMDOS -- with the
Sega CDBIOS long gone.
TXT
    sed -i 's/$/\r/' "$B/dread.txt"
    # Fill D: with content, and pad the disc with content, because a
    # CD-R full of zeros is a CD-R full of the worst sectors it can
    # hold. Mode-1 data is scrambled before writing precisely because
    # long uniform runs make pathological pit patterns -- the weak
    # sector effect -- and a 1992 lens reading a 10x burn is exactly
    # where that stops being theoretical. Hardware failed repeatedly at
    # LBA 1608: 2042 zero bytes, the tail of the first FAT. Every
    # sector this console has ever read successfully held structured
    # data. So: no zero regions anywhere the driver has to read.
    python3 - "$B/filler.bin" <<'FILL'
import sys
# deterministic, no zero runs: a cheap LCG, byte-wide
n, x, out = 4001 * 1024, 0x1234567, bytearray()
for _ in range(n):
    x = (1103515245 * x + 12345) & 0x7FFFFFFF
    out.append(((x >> 16) & 0xFF) or 0x5A)
open(sys.argv[1], 'wb').write(bytes(out))
FILL
    # What else goes on D:. Two drop folders, both git-ignored like the
    # game dumps, because the pipeline is ours and the software is the
    # owner's: vendor/stsoft/ for ordinary Atari ST programs, and
    # datadisc/ for this project's own applications when
    # tools/build-datadisc.sh sets DDISK_DIR. 8.3 names only, upper-cased,
    # because D: is FAT16 and TOS wants them that way.
    STADD=()
    for dir in "$ROOT/vendor/stsoft" ${DDISK_DIR:+"$DDISK_DIR"}; do
      [[ -d "$dir" ]] || continue
      while IFS= read -r -d '' f; do
        bn=$(basename "$f")
        [[ "$bn" = README.md ]] && continue
        bn=$(echo "$bn" | tr '[:lower:]' '[:upper:]')
        STADD+=(--add "$f:$bn")
      done < <(find "$dir" -maxdepth 1 -type f -print0 | sort -z)
    done
    python3 "$ROOT/tools/mkfat.py" "$B/ddisk.img" --size 0x1000000 --fat16 \
      --label CDROM --oem EmuTOSMD \
      --entropy \
      --add "$B/dread.txt:READCD.TXT" \
      --add "$B/filler.bin:FILLER.BIN" \
      --add "$B/EJECT.PRG:EJECT.PRG" \
      --add "$B/SHOW.PRG:SHOW.PRG" \
      --add "$B/EDIT.PRG:EDIT.PRG" \
      --add "$B/NATIVE.PRG:NATIVE.PRG" \
      --add "$B/DEMO.PI1:DEMO.PI1" \
      ${DIAG:+--add "$B/SWAP.PRG:SWAP.PRG" \
              --add "$B/SDIAG.PRG:SDIAG.PRG" \
              --add "$B/IDIAG.PRG:IDIAG.PRG" \
              --add "$B/DISKMARK.PRG:DISKMARK.PRG" \
              --add "$B/BRAMTEST.PRG:BRAMTEST.PRG"} \
      "${STADD[@]}" >/dev/null

    # Pad the disc past the Red Book 300-sector minimum track length:
    # short tracks are rejected by some burners and some real drives.
    #
    # Sized so the ISO 9660 volume covers the fixed-address regions
    # stamped in below. They used to be appended after the volume ended,
    # which is wrong twice over: the image file went sparse, and the
    # volume size in the descriptor still declared the smaller number,
    # so a tool that honours it produces a disc missing them.
    python3 - "$B/fs/PAD.BIN" <<'PAD'
import sys
x, out = 0x2468ACE, bytearray()
for _ in range(26214400):
    x = (1103515245 * x + 12345) & 0x7FFFFFFF
    out.append(((x >> 16) & 0xFF) or 0xA5)
open(sys.argv[1], 'wb').write(bytes(out))
PAD
    PAYLOAD="$B/iofw.bin"
  fi
fi
cp "$PAYLOAD" "$B/fs/M_INIT.PRG"

genisoimage -quiet -iso-level 1 -G "$B/bootsect.bin" -pad \
  -V EMUTOSMD -o "$B/$OUT.iso" "$B/fs"

# A sector the console can check on its own.
#
# Stage 3 proves "we read the disc" by comparing bytes, and in emulation
# the harness does that against this file. On hardware there is no
# harness, and reading 2048 bytes back off a status line is not a test.
# So the disc carries one sector of a pattern the sub CPU can generate
# from nothing, at an address fixed at build time.
#
# It goes inside the padding file, not after the end of the volume: the
# address is just as fixed, but now the volume descriptor covers it, so
# every tool that reads the image agrees it is there. The same goes for
# the D: filesystem. Must stay in step with CDR_PATTERN_LBA and
# CD_DDISK_LBA in emutos/bios/segacd.c.
python3 "$ROOT/tools/stamp-testsector.py" "$B/$OUT.iso" "$B/ddisk.img"

cat > "$B/$OUT.cue" <<EOF
FILE "$OUT.iso" BINARY
  TRACK 01 MODE1/2048
    INDEX 01 00:00:00
EOF

echo "built: build/$OUT.iso (+.cue), region $REGION, payload $(basename "$PAYLOAD")"
