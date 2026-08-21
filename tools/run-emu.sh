#!/usr/bin/env bash
# Run a build on a headless libretro core and dump a frame (+ optional RAM).
# Pattern and harness inherited from the GEOS-Genesis project's test rig:
# two independent cores (Genesis Plus GX + PicoDrive), agreement between
# them is the fidelity signal. Both support Sega CD.
#
# Usage:
#   tools/run-emu.sh gpgx|gpgx-patched|picodrive <image.bin|.cue|.chd> [frames] [out-dir]
#
# Which gpgx: the stock core segfaults under this harness somewhere
# between frame 1200 and frame 2000, which is short of a desktop -- a CD
# boot needs about 3000. The cause is the harness's, not the core's: it
# answers true to RETRO_ENVIRONMENT_GET_VARIABLE without filling in
# .value and check_variables() hands the NULL to atoi. tools/gpgx-cdtrace.patch
# filters that command out, so `gpgx-patched` -- the core
# tools/trace-emu.sh builds -- is the one to use for anything that has
# to reach the desktop. `gpgx` is still right for the boot screens and
# for cartridge builds, which get there sooner.
#
# Sega CD note: cores need the console BIOS images (e.g. bios_CD_U.bin,
# bios_CD_E.bin, bios_CD_J.bin) in the system dir. They are copyrighted —
# place your own dumps in vendor/bios/ (git-ignored); this script copies
# them into the harness system dir.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

CORE_SEL="${1:?usage: run-emu.sh gpgx|picodrive <image> [frames] [out]}"
IMAGE="${2:?need an image (.bin cart / .cue|.chd disc)}"
FRAMES="${3:-600}"
OUT="${4:-$ROOT/build/emu-$CORE_SEL}"

CACHE="$ROOT/.emu"
BUNDLE="$ROOT/vendor/emulator/genesis-plus-gx-test-emulator-linux-x86_64-binaries.zip"
mkdir -p "$CACHE" "$OUT"

# Harness + GPGX core come from the vendored bundle.
HARNESS="$CACHE/libretro-harness"
if [[ ! -x "$HARNESS" ]]; then
  TMP="$(mktemp -d)"; unzip -q -o "$BUNDLE" -d "$TMP"
  cp "$(find "$TMP" -name libretro-harness -type f | head -1)" "$HARNESS"
  cp "$(find "$TMP" -name genesis_plus_gx_libretro.so | head -1)" "$CACHE/"
  chmod +x "$HARNESS"; rm -rf "$TMP"
fi

case "$CORE_SEL" in
  gpgx)      CORE="$CACHE/genesis_plus_gx_libretro.so"
             if [[ "$FRAMES" -gt 1200 && -f "$CACHE/gpgx_trace.so" ]]; then
               echo "note: $FRAMES frames is past where the stock core dies;" >&2
               echo "      gpgx-patched is built and would survive it." >&2
             fi ;;
  gpgx-patched)
             CORE="$CACHE/gpgx_trace.so"
             [[ -f "$CORE" ]] || { echo "patched core missing — one run of
  tools/trace-emu.sh <cart.bin> <disc.iso>
builds it (clone, apply tools/gpgx-cdtrace.patch, make). See the header
of this file for what the patch is for." >&2; exit 1; } ;;
  picodrive) CORE="$CACHE/picodrive_libretro.so"
             [[ -f "$CORE" ]] || { echo "PicoDrive core missing — build it:
  git clone --depth 1 https://github.com/libretro/picodrive.git $CACHE/picodrive
  git -C $CACHE/picodrive submodule update --init --recursive --depth 1
  make -C $CACHE/picodrive -f Makefile.libretro platform=unix -j\$(nproc)
  cp $CACHE/picodrive/picodrive_libretro.so $CACHE/" >&2; exit 1; } ;;
  *) echo "unknown core: $CORE_SEL" >&2; exit 1 ;;
esac

# Sega CD BIOS files, if the user has provided them.
SYSDIR="$OUT/system"; mkdir -p "$SYSDIR"
[[ -d "$ROOT/vendor/bios" ]] && cp -n "$ROOT"/vendor/bios/* "$SYSDIR"/ 2>/dev/null || true

INARG=()
[[ -n "${INPUT:-}" ]] && INARG=(--input "$INPUT")
"$HARNESS" --core "$CORE" --rom "$IMAGE" --frames "$FRAMES" \
  --dump-frame "$OUT/frame.ppm" --dump-wram "$OUT/wram.bin" \
  --system-dir "$SYSDIR" --save-dir "$OUT" "${INARG[@]}"
echo "frame -> $OUT/frame.ppm  wram -> $OUT/wram.bin"
