#!/usr/bin/env bash
# Extract Sonic's art out of the owner's own ROMs.
#
# Nothing this produces is committed and nothing is distributed: the ROMs
# are Sega's, the mapping and DPLC tables are the Sonic Retro
# disassembly's reading of them, and both are fetched or copied locally.
# What is ours is the pipeline. assets/ and vendor/s?disasm/ are
# git-ignored for that reason, and build/ always was.
#
# Wants:
#   assets/sonic/sonic1.md      Sonic 1, plain binary
#   assets/sonic/sonic2.md      Sonic 2, plain binary (smd_to_bin.py converts
#                               an interleaved .smd dump)
#   vendor/s1disasm, vendor/s2disasm   four files between them, see below
#
# Produces build/sonic/sonic_data.bin -- the tile pool, the frame index,
# the DPLC runs and the sprite pieces, in the cartridge's own shapes.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

need() { [[ -f "$1" ]] || { echo "missing $1" >&2; echo "$2" >&2; exit 1; }; }
need assets/sonic/sonic1.md "Put your own Sonic 1 dump there."
need assets/sonic/sonic2.md "Put your own Sonic 2 dump there (the dust is Sonic 2's)."

# The four files the extractors read out of the disassemblies. Everything
# else comes from the ROMs themselves.
S1=https://raw.githubusercontent.com/sonicretro/s1disasm/master
S2=https://raw.githubusercontent.com/sonicretro/s2disasm/master
fetch() { # fetch <url> <path>
  [[ -f "$2" ]] && return 0
  mkdir -p "$(dirname "$2")"
  echo "fetching $(basename "$2")"
  curl -sSf --max-time 120 -o "$2" "$1"
}
fetch "$S1/_maps/Sonic.asm"                       vendor/s1disasm/_maps/Sonic.asm
fetch "$S1/_maps/Sonic%20-%20Dynamic%20Gfx%20Script.asm" \
      "vendor/s1disasm/_maps/Sonic - Dynamic Gfx Script.asm"
fetch "$S1/_maps/Signpost.asm"                    vendor/s1disasm/_maps/Signpost.asm
fetch "$S2/art/uncompressed/Splash%20and%20skid%20dust.bin" \
      "vendor/s2disasm/art/uncompressed/Splash and skid dust.bin"
fetch "$S2/mappings/sprite/obj08.asm"             vendor/s2disasm/mappings/sprite/obj08.asm
fetch "$S2/mappings/spriteDPLC/obj08.asm"         vendor/s2disasm/mappings/spriteDPLC/obj08.asm

mkdir -p build/sonic
python3 tools/sonic-tools/build_frames.py \
    --rom assets/sonic/sonic1.md --disasm vendor/s1disasm --out build/sonic
# The jump and the signpost, as per-tick tables. Generated here for the
# same reason the art is: what comes out is Sega's -- the frequencies,
# the voice, the script -- and it belongs in build/ beside the tiles,
# never in the tree. It was committed once, by mistake, when the port's
# sources were first dropped in.
python3 tools/sonic-tools/build_sfx.py -o build/sonic/sfx_jump.inc

python3 tools/sonic-tools/export_data.py \
    --frames build/sonic/frames.json \
    --rom assets/sonic/sonic1.md --rom2 assets/sonic/sonic2.md \
    --disasm vendor/s1disasm --disasm2 vendor/s2disasm \
    -o build/sonic/sonic_data.bin --inc build/sonic/sonic_frames.inc
