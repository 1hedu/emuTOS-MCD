#!/usr/bin/env bash
# Build Peter Lane's GEM Sokoban for this machine.
#
#   tools/build-sokoban.sh        -> vendor/stsoft/SOKOBAN.PRG, SOKOBAN.RSC
#
# Both builds carry vendor/stsoft/ on D:. The game is pure GEM: a menu
# bar, windows, the VDI, nothing of the hardware. It has the 50 classic
# levels built in and keeps its best scores in I:\SOKOSCOR.TXT, the
# console's own backup RAM, so they survive a power-off on either boot.
#
# Source: Atari_ST_Sources (ggnkua), C/Peter C. Lane/sokoban, pinned.
# Lane's licence is the Open Works License 0.9.4, which allows
# redistribution; the build fetches it rather than carrying it anyway,
# like everything else that is not this project's.
#
# It was written for AHCC, which has 16-bit ints like Pure C, so it is
# built -mshort. MiNTLib has no -mshort build, so the C library is
# libcmini, compiled here for -mshort, which also makes the program a
# third of the size: about 48 KB. patches/sokoban/include/ maps AHCC's
# GEM headers onto GEMlib's; patches/sokoban/megacd.patch is the rest.
#
# Needs m68k-atari-mint-gcc with GEMlib: Vincent Riviere's
# cross-mint-essential (http://vincent.riviere.free.fr/soft/m68k-atari-mint/).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/.cache/sokoban"
W="$ROOT/build/sokoban"
SRC_REPO=https://github.com/ggnkua/Atari_ST_Sources
SRC_REV=63e0a458f522f4cd47e7941d02e693ad776fe0af
SRC_DIR="C/Peter C. Lane/sokoban"
CMINI_REPO=https://github.com/freemint/libcmini.git
CMINI_REV=77eb5e1e0129c5dad932e08e50d893cf792a5347

command -v m68k-atari-mint-gcc >/dev/null || {
  echo "build-sokoban.sh needs m68k-atari-mint-gcc (cross-mint-essential)" >&2
  exit 1; }
mkdir -p "$CACHE"

# The sources: one directory out of a very large repository, so a
# partial, sparse clone at the pinned commit.
if [[ ! -d "$CACHE/src/$SRC_DIR" ]]; then
  rm -rf "$CACHE/src"
  git clone -q --filter=blob:none --no-checkout "$SRC_REPO" "$CACHE/src"
  git -C "$CACHE/src" sparse-checkout set --no-cone "/$SRC_DIR/"
  git -C "$CACHE/src" checkout -q "$SRC_REV"
fi

# libcmini, for -mshort.
CM="$CACHE/libcmini"
if [[ ! -f "$CM/build/mshort/libcmini.a" ]]; then
  rm -rf "$CM"
  git clone -q "$CMINI_REPO" "$CM"
  git -C "$CM" checkout -q "$CMINI_REV"
  make -C "$CM" BUILD_CF=N -j"$(nproc)" >/dev/null
fi

rm -rf "$W"; mkdir -p "$W"
cp "$CACHE/src/$SRC_DIR/"*.c "$CACHE/src/$SRC_DIR/"*.h \
   "$CACHE/src/$SRC_DIR/SOKOBAN.rsh" "$CACHE/src/$SRC_DIR/SOKOBAN.rsc" "$W/"
patch -s -d "$W" -p1 < "$ROOT/patches/sokoban/megacd.patch"

GI="$(m68k-atari-mint-gcc -print-file-name=include)"
( cd "$W" && m68k-atari-mint-gcc -m68000 -mshort -Os -fomit-frame-pointer \
    -DATARIST -nostdinc -I"$ROOT/patches/sokoban/include" -I"$CM/include" \
    -isystem "$GI" -idirafter /usr/m68k-atari-mint/include \
    -nostdlib "$CM/build/mshort/objs/crt0.o" *.c -o SOKOBAN.PRG -s \
    -L"$CM/build/mshort" -L/usr/m68k-atari-mint/lib/mshort \
    -lgem -lcmini -lgcc -lcmini 2>"$W/warnings.txt" )

mkdir -p "$ROOT/vendor/stsoft"
cp "$W/SOKOBAN.PRG" "$ROOT/vendor/stsoft/SOKOBAN.PRG"
cp "$W/SOKOBAN.rsc" "$ROOT/vendor/stsoft/SOKOBAN.RSC"
echo "built: vendor/stsoft/SOKOBAN.PRG ($(stat -c%s "$W/SOKOBAN.PRG") bytes) and SOKOBAN.RSC"
