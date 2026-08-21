#!/usr/bin/env bash
# Sonic, end to end: the art out of the ROMs, the payload out of
# payload/sonic, and both dropped into datadisc/ ready for
# tools/build-datadisc.sh.
#
#   tools/build-sonic.sh
#
# Two files come out and they go together:
#
#   SONIC.MDP   6.5KB   the engine, staged into the servant's planar
#                       cache and jumped to. docs/payload.md.
#   SONIC.MDD   52KB    the art, loaded to sub $60000 -- window bank 3
#                       -- because the payload has 32000 bytes and this
#                       is not going into them.
#
# The .MDD is the general facility, not a Sonic one: NATIVE.PRG loads
# whatever .MDD sits beside the .MDP it is running, and knows nothing
# about what is in it.
#
# The art needs your own Sonic 1 and Sonic 2 dumps in assets/sonic/.
# Nothing extracted from them is committed and nothing is distributed --
# tools/build-sonic-art.sh says the same thing at more length.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

"$ROOT/tools/build-sonic-art.sh"
"$ROOT/tools/build-payload.sh" payload/sonic

mkdir -p "$ROOT/datadisc"
cp "$ROOT/build/SONIC.MDP" "$ROOT/datadisc/SONIC.MDP"
cp "$ROOT/build/sonic/sonic_data.bin" "$ROOT/datadisc/SONIC.MDD"
ls -l "$ROOT/datadisc/SONIC.MDP" "$ROOT/datadisc/SONIC.MDD"
echo
echo "now: tools/build-datadisc.sh U"
