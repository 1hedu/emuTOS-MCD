#!/usr/bin/env bash
# Build Jim Kent's Cyber Paint (Antic, 1987) for this machine.
#
#   tools/build-cyberpaint.sh     -> vendor/stsoft/CYP.PRG
#
# Both builds carry vendor/stsoft/ on D:. Cyber Paint is a low-resolution
# paint and cel-animation program; Kent released the source under a BSD
# licence. Source: Atari_ST_Sources (ggnkua), C/Jim Kent/Cyber Paint,
# pinned, fetched rather than carried.
#
# It was written for Manx Aztec C, 16-bit ints, so it is built -mshort
# against libcmini (as Sokoban is). Aztec's assembler dialect goes
# through tools/aztec2gas.py to GNU as. Everything but one file is
# hardware-neutral: PF.ASM, which drives the ST's Timer B to split the
# palette, is replaced by patches/cyberpaint/pf_mcd.c (see there).
# patches/cyberpaint/megacd.patch is the few lines GCC wants changed.
#
# Needs m68k-atari-mint-gcc with GEMlib, python3.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CACHE="$ROOT/.cache/cyberpaint"
W="$ROOT/build/cyberpaint"
SRC_REPO=https://github.com/ggnkua/Atari_ST_Sources
SRC_REV=63e0a458f522f4cd47e7941d02e693ad776fe0af
SRC_DIR="C/Jim Kent/Cyber Paint"
CMINI_REPO=https://github.com/freemint/libcmini.git
CMINI_REV=77eb5e1e0129c5dad932e08e50d893cf792a5347
P="$ROOT/patches/cyberpaint"

command -v m68k-atari-mint-gcc >/dev/null || {
  echo "build-cyberpaint.sh needs m68k-atari-mint-gcc (cross-mint-essential)" >&2
  exit 1; }
mkdir -p "$CACHE"

if [[ ! -d "$CACHE/src/$SRC_DIR" ]]; then
  rm -rf "$CACHE/src"
  git clone -q --filter=blob:none --no-checkout "$SRC_REPO" "$CACHE/src"
  git -C "$CACHE/src" sparse-checkout set --no-cone "/$SRC_DIR/"
  git -C "$CACHE/src" checkout -q "$SRC_REV"
fi

CM="$CACHE/libcmini"
if [[ ! -f "$CM/build/mshort/libcmini.a" ]]; then
  rm -rf "$CM"
  git clone -q "$CMINI_REPO" "$CM"
  git -C "$CM" checkout -q "$CMINI_REV"
  make -C "$CM" BUILD_CF=N -j"$(nproc)" >/dev/null
fi

# Lower-case names (the sources #include them that way) and LF endings.
# OSBIND.H and GEMDEFS.H in the tree are empty stand-ins for Aztec's
# own; they are left out so the real ones are found.
rm -rf "$W"; mkdir -p "$W/obj"
for f in "$CACHE/src/$SRC_DIR/"*; do
  b="$(basename "$f" | tr 'A-Z' 'a-z')"
  case "$b" in osbind.h|gemdefs.h) continue ;; esac
  tr -d '\r' < "$f" > "$W/$b"
done
patch -s -d "$W" -p1 < "$P/megacd.patch"
cp "$P/pf_mcd.c" "$W/"

# The program is the makefile's object list, with pf_mcd for pf.
OBJS="$(sed -n '/^O=/,/^$/p' "$W/makefile" | tr -d '\\' | sed 's/^O=//' |
        tr -s ' \t\n' '\n' | sed '/^$/d; s/\.o$//; s/^pf$/pf_mcd/')"

GI="$(m68k-atari-mint-gcc -print-file-name=include)"
CC=(m68k-atari-mint-gcc -m68000 -mshort -Os -fomit-frame-pointer -w
    -nostdinc -include "$P/include/compat.h" -I"$P/include" -I"$W"
    -I"$CM/include" -isystem "$GI" -idirafter /usr/m68k-atari-mint/include)
LINK=()
for o in $OBJS; do
  if [[ -f "$W/$o.asm" ]]; then
    python3 "$ROOT/tools/aztec2gas.py" "$W/$o.asm" > "$W/obj/$o.s"
    m68k-atari-mint-as -m68000 --register-prefix-optional \
      -o "$W/obj/$o.o" "$W/obj/$o.s"
  else
    "${CC[@]}" -c "$W/$o.c" -o "$W/obj/$o.o"
  fi
  LINK+=("$W/obj/$o.o")
done

m68k-atari-mint-gcc -m68000 -mshort -nostdlib "$CM/build/mshort/objs/crt0.o" \
  "${LINK[@]}" -o "$W/CYP.PRG" -s -L"$CM/build/mshort" \
  -L/usr/m68k-atari-mint/lib/mshort -lgem -lcmini -lgcc -lcmini

mkdir -p "$ROOT/vendor/stsoft"
cp "$W/CYP.PRG" "$ROOT/vendor/stsoft/CYP.PRG"
echo "built: vendor/stsoft/CYP.PRG ($(stat -c%s "$W/CYP.PRG") bytes)"
