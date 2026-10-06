#!/usr/bin/env bash
# Put NEOchrome 1.0 on D: -- the disc's, or the cartridge's romdisk.
#
#   tools/install-neochrome.sh <NEOchrome 1.0 disk image, .st>
#
# Copies NEONEW.PRG off the disk into vendor/stsoft/NEOCHROM/, which
# both builds carry on D:, and builds NEO.PRG beside it: the launcher
# that patches NEOchrome in memory for this machine and starts it
# (progs/neo.c says what it changes and why). Run NEO.PRG, not
# NEONEW.PRG; NEONEW.PRG on its own would take the CD drive's
# interrupt vector and write the gate array.
#
# NEOchrome is Atari's and is not in this repository; vendor/ is
# git-ignored. Needs mtools and the m68k-elf toolchain.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${1:?usage: install-neochrome.sh <neochrome.st>}"
command -v mcopy >/dev/null || { echo "install-neochrome.sh needs mtools" >&2; exit 1; }
export MTOOLS_SKIP_CHECK=1
PATH="/opt/m68k-elf/bin:$PATH"

D="$ROOT/vendor/stsoft/NEOCHROM"
mkdir -p "$D"
mcopy -o -i "$IMG" ::NEOCHROM/NEONEW.PRG "$D/NEONEW.PRG"

# The launcher, built the way tools/build-iso.sh builds progs/, with the
# startup that gives memory back so it can load a program.
T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
CC="m68k-elf-gcc -m68000 -mpcrel -Os -fomit-frame-pointer -ffreestanding -Wall --param=min-pagesize=0"
$CC -DSHRINK_STARTUP=1 -c "$ROOT/progs/tosbind.S" -o "$T/tosbind.o"
$CC -c "$ROOT/progs/neo.c" -o "$T/neo.o"
m68k-elf-ld -T "$ROOT/tools/prg.ld" -o "$T/neo.elf" "$T/tosbind.o" "$T/neo.o"
python3 "$ROOT/tools/mkprg.py" "$T/neo.elf" "$T/NEO.PRG" >/dev/null
cp "$T/NEO.PRG" "$D/NEO.PRG"
echo "NEOchrome on D:\\NEOCHROM -- run NEO.PRG ($(stat -c%s "$D/NEO.PRG") bytes), which starts NEONEW.PRG"
