#!/usr/bin/env bash
# Build the data disc: the same bootable system, with applications on D:.
#
# Usage: tools/build-datadisc.sh [J|U|E]
# Produces build/emutosmd-data-<region>.iso (+.cue).
#
# Why a second disc rather than more files on the first. The boot disc is a
# system disk and should stay one -- it is what starts the machine, and it
# should be rebuildable and reburnable for reasons that have nothing to do
# with whatever application is being worked on this week. An application
# belongs on a disc that can be thrown away and made again.
#
# The boot half is identical to the base disc's, deliberately. That means the
# data disc can simply be booted, with its own D: already in the drive; or the
# base disc can be booted and this one swapped in with EJECT.PRG. Those are
# the same arrangement approached from either end, and neither needs a second
# kind of image.
#
# Contents come from datadisc/ in the repo -- drop files in, run this. The
# directory is git-ignored apart from its README, on the same rule vendor/
# follows.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
REGION="${1:-U}"

[[ -d "$ROOT/datadisc" ]] || { echo "no datadisc/ -- nothing to put on D:" >&2; exit 1; }

n=$(find "$ROOT/datadisc" -maxdepth 1 -type f ! -name README.md | wc -l)
if [[ "$n" -eq 0 ]]; then
  echo "datadisc/ holds no files. The disc would be the base disc with a" >&2
  echo "different name, which is worse than not building it." >&2
  exit 1
fi

# C: is deliberately small here. The ramdisk sits in PRG RAM above
# EmuTOS, and on the system disc it takes all of it -- which is right,
# because nothing else wants it. A data disc may carry a payload with a
# .MDD beside it, and that goes in whatever the ramdisk left: 32KB of
# ramdisk leaves 112KB of it. See docs/payload.md, "Bulk data".
DDISK_DIR="$ROOT/datadisc" ADISK_SIZE="${ADISK_SIZE:-0x8000}" SLIMC=1 \
  "$ROOT/tools/build-iso.sh" "$REGION" "" "emutosmd-data-$REGION"
echo "data disc: $n file(s) from datadisc/ on D:"
