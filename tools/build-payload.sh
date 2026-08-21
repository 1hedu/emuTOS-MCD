#!/usr/bin/env bash
# Build a native payload: Genesis-side code that runs with the VDP handed
# to it.
#
#   tools/build-payload.sh payload/hello.S [out.mdp]     one file
#   tools/build-payload.sh payload/sonic   [out.mdp]     a directory
#
# A directory is every .S in it, assembled and linked with its own
# link.ld -- which is what a payload with more than one source and more
# than one section needs, and which is also how image_end gets to be a
# linker symbol rather than a label the last file has to carry.
#
# The image is linked for 0xFF7000 -- Genesis work RAM, and therefore the
# same address whichever way the console booted. docs/payload.md is the
# contract; the header at the front of the source is what the servant
# checks.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SRC="${1:?usage: build-payload.sh <src.S|dir> [out.mdp]}"
PATH=/opt/m68k-elf/bin:$PATH
B="$ROOT/build"; mkdir -p "$B"

# No --register-prefix-optional. GEOS's memory map names its own
# zero-page pseudo-registers a0..a7, and with bare register names allowed
# those .equ's shadow the 68000's -- every `lea sym,%a0' in the engine
# became "bad expression" and the cause was three hundred lines away in
# an include. Every payload source writes %d0 and %a0 anyway.
AS="m68k-elf-as -m68000 -I $B -I $ROOT/payload"

if [ -d "$SRC" ]; then
    NAME="$(basename "$SRC")"
    OUT="${2:-$ROOT/build/$(echo "$NAME" | tr '[:lower:]' '[:upper:]').MDP}"
    OBJS=()
    for f in "$SRC"/*.S; do
        o="$B/$(basename "${f%.S}").o"
        $AS -I "$SRC" -o "$o" "$f"
        OBJS+=("$o")
    done
    m68k-elf-ld -T "$SRC/link.ld" --oformat binary -o "$OUT" "${OBJS[@]}"
    rm -f "${OBJS[@]}"
else
    OUT="${2:-$ROOT/build/$(basename "${SRC%.S}" | tr '[:lower:]' '[:upper:]').MDP}"
    $AS -o "$B/payload.o" "$SRC"
    m68k-elf-ld -Ttext 0xFF7000 --oformat binary -o "$OUT" "$B/payload.o"
    rm -f "$B/payload.o"
fi

# The servant refuses anything that does not fit the space it has, and it
# is better to hear that here than off a television. 32000 bytes is the
# planar cache, which is what a payload borrows.
SZ=$(stat -c%s "$OUT")
python3 - "$OUT" "$SZ" <<'PY'
import struct, sys
path, size = sys.argv[1], int(sys.argv[2])
d = open(path, 'rb').read()
magic, ver, flags, entry, length, work = struct.unpack('>4sHHIII', d[:20])
if magic != b'MDPL':
    sys.exit("%s: no MDPL magic -- is the header first in the source?" % path)
if length != size:
    sys.exit("%s: header says %d bytes, the file is %d -- image_end is wrong"
             % (path, length, size))
if length + work > 32000:
    sys.exit("%s: %d + %d workspace is past the 32000 bytes there are"
             % (path, length, work))
if entry >= length or entry & 1:
    sys.exit("%s: entry offset %d is outside the image or odd" % (path, entry))
print("%s: %d bytes, %d workspace, entry +%d" % (path, length, work, entry))
PY
