#!/usr/bin/env bash
# Put HiSoft Devpac 3 on D: (the disc) and R: (the cartridge's romdisk).
#
#   tools/install-devpac.sh <Devpac 3 disk image, .st>
#
# Copies the editor, the assembler and its tools, the include files and
# the examples into vendor/stsoft/, which both builds carry, with their
# folders: Devpac looks for \BIN\GEN.TTP and \INCDIR by path.
# HISOFTED.INF, the editor's settings, names those paths on A:; they are
# rewritten to D:, where they are on a disc. tools/build-rom.sh rewrites
# the cartridge's copy again, to R:.
#
# Devpac is HiSoft's and is not in this repository; vendor/ is
# git-ignored. Needs mtools.
#
# Tested with Devpac 3.10: the editor runs, and GEN.TTP assembles a
# source on D: to a program on C: that then runs (docs/ports.md).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMG="${1:?usage: install-devpac.sh <devpac.st>}"
command -v mcopy >/dev/null || { echo "install-devpac.sh needs mtools" >&2; exit 1; }
export MTOOLS_SKIP_CHECK=1

T="$(mktemp -d)"; trap 'rm -rf "$T"' EXIT
mcopy -s -i "$IMG" ::DEVPAC.PRG ::HISOFTED.INF ::BIN ::INCDIR ::EXAMPLES "$T/"
D="$ROOT/vendor/stsoft"
mkdir -p "$D"
cp "$T/DEVPAC.PRG" "$D/"
for sub in BIN INCDIR EXAMPLES; do
  mkdir -p "$D/$sub"
  cp "$T/$sub/"* "$D/$sub/"
done
python3 - "$T/HISOFTED.INF" "$D/HISOFTED.INF" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
d = d.replace(b'a:\\', b'd:\\').replace(b'A:\\', b'D:\\')
open(sys.argv[2], 'wb').write(d)
PY
echo "Devpac (D: or R:) -- $(find "$D/DEVPAC.PRG" "$D/BIN" "$D/INCDIR" "$D/EXAMPLES" -type f | wc -l) files in vendor/stsoft/"
