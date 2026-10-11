#!/usr/bin/env bash
# Vectorman, end to end: his poses out of your cartridge, the payload out
# of payload/vectorman, and both dropped into datadisc/.
#
#   tools/build-vectorman.sh
#
# Two files come out and they go together:
#
#   VECTOR.MDP   5KB    the engine, payload/vectorman/vm.c
#   VECTOR.MDD   57KB   every pose the game drew him in, its tiles and
#                       his palette -- recorded off the running cartridge
#                       by tools/vectorman-tools/extract.py
#
# VECTOR.ACC, the Desk menu entry, is built with the system:
# VECTORACC=1 tools/build-iso.sh puts it on C:. It finds the other two on
# C:, R: or D:. docs/vectorman.md has the recipes.
#
# Wants your own Vectorman (USA, Europe) dump at assets/vectorman/
# vectorman.md. Nothing extracted from it is committed or distributed:
# assets/ and build/ are git-ignored, and so is everything in datadisc/
# but its README. The extractor checks the dump's hash and refuses any
# other, because what it records is cut at frame numbers measured on
# that one.
#
# The extractor runs the cartridge on Genesis Plus GX with two patches:
# tools/gpgx-cdtrace.patch, which keeps the core alive under the harness
# (see tools/run-emu.sh), and tools/gpgx-vdpdump.patch, which writes the
# VDP and work RAM out each frame. The core is built once into .emu/,
# from the same upstream commit the patches were made against.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
PATH=/opt/m68k-elf/bin:$PATH

ROM=assets/vectorman/vectorman.md
[[ -f "$ROM" ]] || { echo "missing $ROM -- put your own Vectorman (USA, Europe) dump there" >&2; exit 1; }

CACHE="$ROOT/.emu"
mkdir -p "$CACHE"
HARNESS="$CACHE/libretro-harness"
if [[ ! -x "$HARNESS" ]]; then
  BUNDLE="$ROOT/vendor/emulator/genesis-plus-gx-test-emulator-linux-x86_64-binaries.zip"
  TMP="$(mktemp -d)"; unzip -q -o "$BUNDLE" -d "$TMP"
  cp "$(find "$TMP" -name libretro-harness -type f | head -1)" "$HARNESS"
  chmod +x "$HARNESS"; rm -rf "$TMP"
fi

CORE="$CACHE/gpgx_vdp.so"
if [[ ! -f "$CORE" ]]; then
  SRC="$CACHE/gpgx-vdp"
  REV=58c341487e5bfcf979ea68413c7987633adb0c56
  if [[ ! -d "$SRC" ]]; then
    git init -q "$SRC"
    git -C "$SRC" fetch -q --depth 1 https://github.com/libretro/Genesis-Plus-GX "$REV"
    git -C "$SRC" checkout -q FETCH_HEAD
    git -C "$SRC" apply "$ROOT/tools/gpgx-cdtrace.patch"
    git -C "$SRC" apply "$ROOT/tools/gpgx-vdpdump.patch"
  fi
  make -C "$SRC" -f Makefile.libretro platform=unix -j"$(nproc)" >/dev/null
  cp "$SRC/genesis_plus_gx_libretro.so" "$CORE"
fi

mkdir -p build/vectorman
python3 tools/vectorman-tools/extract.py --harness "$HARNESS" --core "$CORE" \
  --rom "$ROM" --work build/vectorman/work -o build/vectorman/VECTOR.MDD
rmdir build/vectorman/work 2>/dev/null || rm -rf build/vectorman/work
bash tools/build-payload.sh payload/vectorman build/VECTOR.MDP

mkdir -p datadisc
cp build/VECTOR.MDP datadisc/VECTOR.MDP
cp build/vectorman/VECTOR.MDD datadisc/VECTOR.MDD
ls -l datadisc/VECTOR.MDP datadisc/VECTOR.MDD
echo
echo "a disc:      VECTORACC=1 tools/build-datadisc.sh U"
echo "a cartridge: VECTORACC=1 tools/build-iso.sh U && DDISK_DIR=\$PWD/datadisc tools/build-rom.sh boot/m1emu.S"
