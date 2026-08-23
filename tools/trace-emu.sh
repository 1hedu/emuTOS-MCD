#!/usr/bin/env bash
# Run a Mode 1 build (cart + disc) on an instrumented Genesis Plus GX and
# write a CDD/CDC trace.
#
# Two things make this possible and neither is obvious:
#
#  * gpgx attaches a disc to a cartridge by itself. loadrom.c, for a
#    16-bit cart with CD hardware, tries "<romname>.iso" when no CD image
#    is loaded yet -- so copying the disc next to the .bin is the whole
#    of Mode 1 setup. The console then boots the cart with a disc in the
#    tray, which is what the hardware does and what no emulator run
#    before this one was doing.
#
#  * The stock core segfaults under this harness. It answers true to
#    RETRO_ENVIRONMENT_GET_VARIABLE without filling in .value and
#    check_variables() hands that NULL to atoi; the patch filters the
#    command out so every core option keeps its default. The trace hooks
#    also have to #undef fopen/fprintf/FILE, which libretro's VFS
#    transforms header redefines onto wrappers that crash on a real
#    FILE*.
#
# Usage: tools/trace-emu.sh <cart.bin> <disc.iso> [frames] [out-dir]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CART="${1:?need a cart .bin}"
DISC="${2:?need a disc .iso}"
FRAMES="${3:-1500}"
OUT="${4:-$ROOT/build/trace}"
CACHE="$ROOT/.emu"
SRC="$CACHE/gpgx"

if [[ ! -f "$CACHE/gpgx_trace.so" ]]; then
  [[ -d "$SRC" ]] || git clone --depth 1 https://github.com/libretro/Genesis-Plus-GX "$SRC"
  git -C "$SRC" apply "$ROOT/tools/gpgx-cdtrace.patch"
  git -C "$SRC" apply "$ROOT/tools/gpgx-uartin.patch"
  make -C "$SRC" -f Makefile.libretro platform=unix -j"$(nproc)"
  cp "$SRC/genesis_plus_gx_libretro.so" "$CACHE/gpgx_trace.so"
fi

mkdir -p "$OUT/system"
[[ -d "$ROOT/vendor/bios" ]] && cp -n "$ROOT"/vendor/bios/* "$OUT/system"/ 2>/dev/null || true
# The disc has to sit beside the cart, under the cart's own name.
cp -f "$DISC" "${CART%.*}.iso"

GPGX_CDTRACE="$OUT/cd.log" "$CACHE/libretro-harness" \
  --core "$CACHE/gpgx_trace.so" --rom "$CART" --frames "$FRAMES" \
  --dump-frame "$OUT/frame.ppm" --dump-wram "$OUT/wram.bin" \
  --system-dir "$OUT/system" --save-dir "$OUT"
echo "trace -> $OUT/cd.log  ($(wc -l < "$OUT/cd.log") lines)"
